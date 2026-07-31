#include "asm-generic/gpio.h"
#include "asm/uaccess.h"
#include "linux/cdev.h"
#include "linux/device.h"
#include "linux/err.h"
#include "linux/export.h"
#include "linux/fs.h"
#include "linux/gfp.h"
#include "linux/gpio/consumer.h"
#include "linux/interrupt.h"
#include "linux/irqreturn.h"
#include "linux/mod_devicetable.h"
#include "linux/platform_device.h"
#include "linux/slab.h"
#include "linux/types.h"
#include <linux/module.h>
#include <linux/spi/spi.h>

struct max7219 {
  dev_t dev_id;
  struct class *cls;
  struct device *dev;
  struct gpio_desc *gpio;
  struct spi_device *spi;
  // int irq;
  struct cdev cdev;
};

// static irqreturn_t max7219_isr(int irq, void *dev_id) { return IRQ_HANDLED; }

static ssize_t max7219_write(struct file *filp, const char __user *buf,
                             size_t size, loff_t *offset) {
  struct max7219 *data = filp->private_data;
  int i;
  int ret;
  char *kbuf;
  unsigned char tx_buf[16]; /* 存放 8 个寄存器的 16 字节数据 */

  if (size > 8) {
    return -EINVAL;
  }

  kbuf = kzalloc(size, GFP_KERNEL);
  if (!kbuf) {
    return -ENOMEM;
  }

  if (copy_from_user(kbuf, buf, size)) {
    kfree(kbuf);
    return -EFAULT;
  }

  /* 需要16位数据包 */
  /* 传进来8个字节，对应DIG0-DIG7 */
  for (i = 0; i < 8 && i < size; i++) {
    tx_buf[i * 2] = i + 1;       /* 第 0~7 个，对应寄存器地址 0x01 ~ 0x08 */
    tx_buf[i * 2 + 1] = kbuf[i]; /* 用户想要显示的数据 */
  }

  ret = spi_write(data->spi, tx_buf, 16);
  kfree(kbuf);

  if (ret < 0) {
    dev_err(&data->spi->dev, "spi write failed\n");
    return ret;
  }

  *offset += size;

  return size;
}

static struct file_operations max7219_fops = {
    .owner = THIS_MODULE,
    .write = max7219_write,
};

static int max7219_probe(struct spi_device *spi) {
  struct max7219 *data;
  int i;
  int ret;
  /* 初始化 MAX7219 寄存器 */
  u8 init_cmds[][2] = {
      {0x0C, 0x01}, /* 关断寄存器：0x01 开启正常模式 */
      {0x0B, 0x07}, /* 扫描限制寄存器：0x07 开启 8 位数码管 */
      {0x09, 0x00}, /* 译码模式：0x00 不译码(接收原始二进制) */
      {0x0A, 0x0A}, /* 亮度寄存器：0x0A 中等亮度 */
  };

  data = devm_kzalloc(&spi->dev, sizeof(*data), GFP_KERNEL);
  if (!data) {
    dev_err(&spi->dev, "can not alloc prv data\n");
    return -ENOMEM;
  }

  data->spi = spi;

  data->gpio = devm_gpiod_get(&spi->dev, "max7219", GPIOD_OUT_LOW);
  if (IS_ERR(data->gpio)) {
    dev_err(&spi->dev, "can not get gpio ; %s %s line %d", __FILE__,
            __FUNCTION__, __LINE__);
    return PTR_ERR(data->gpio);
  }

  // data->irq = gpiod_to_irq(data->gpio);
  // if (data->irq < 0) {
  //   dev_err(&spi->dev, "can not get irq ; %s %s line %d", __FILE__,
  //           __FUNCTION__, __LINE__);
  //   return data->irq;
  // }

  // ret = devm_request_irq(&spi->dev, data->irq, max7219_isr,
  //                        IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
  //                        "max7219_irq", data);
  // if (ret) {
  //   dev_err(&spi->dev, "can not request irq ; %s %s line %d", __FILE__,
  //           __FUNCTION__, __LINE__);
  //   return ret;
  // }

  ret = alloc_chrdev_region(&data->dev_id, 0, 1, "max7219_chr_dev");
  if (ret < 0) {
    dev_err(&spi->dev, "can not alloc chr dev ; %s %s line %d", __FILE__,
            __FUNCTION__, __LINE__);
    return ret;
  }

  cdev_init(&data->cdev, &max7219_fops);

  ret = cdev_add(&data->cdev, data->dev_id, 1);
  if (ret < 0) {
    dev_err(&spi->dev, "cdev add failed\n ; %s %s line %d", __FILE__,
            __FUNCTION__, __LINE__);
    return ret;
  }

  data->cls = class_create(THIS_MODULE, "max7219_cls");
  if (IS_ERR(data->cls)) {
    dev_err(&spi->dev, "can not create class ; %s %s line %d", __FILE__,
            __FUNCTION__, __LINE__);
    return PTR_ERR(data->cls);
  }
  data->dev = device_create(data->cls, NULL, data->dev_id, NULL, "max7219_DEV");
  if (IS_ERR(data->dev)) {
    dev_err(&spi->dev, "can not create dev ; %s %s line %d", __FILE__,
            __FUNCTION__, __LINE__);
    return PTR_ERR(data->dev);
  }

  for (i = 0; i < 4; i++) {
    spi_write(data->spi, init_cmds[i], 2);
  }

  spi_set_drvdata(spi, data);

  dev_info(&spi->dev, "max7219 driver has been initialized");

  return 0;
}

static int max7219_remove(struct spi_device *spi) {
  struct max7219 *data = spi_get_drvdata(spi);
  device_destroy(data->cls, data->dev_id);
  class_destroy(data->cls);
  cdev_del(&data->cdev);
  unregister_chrdev_region(data->dev_id, 1);

  dev_info(&spi->dev, "max7219 driver has been removed");

  return 0;
}

static struct of_device_id max7219_of_match_table[] = {
    {.compatible = "max7219"},
    {/* sentinel */},
};
MODULE_DEVICE_TABLE(of, max7219_of_match_table);

static struct spi_device_id max7219_id_table[] = {
    {"max7219", 0},
    {/* sentinel */},
};
MODULE_DEVICE_TABLE(spi, max7219_id_table);

static struct spi_driver max7219_drv = {
    .driver =
        {
            .name = "max7219",
            .of_match_table = max7219_of_match_table,
        },
    .probe = max7219_probe,
    .remove = max7219_remove,
    .id_table = max7219_id_table,
};

module_spi_driver(max7219_drv);

MODULE_LICENSE("GPL");