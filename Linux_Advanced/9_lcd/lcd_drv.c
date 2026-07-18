#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

struct lcd_dev {
  dev_t lcd_devid;
  struct class *lcd_class;
  struct device *lcd_device;
  int lcd_major;
  int lcd_irq;
  struct spi_device *lcd_spidev;
};

static struct lcd_dev lcddev;

static ssize_t lcd_write(struct file *filp, const char __user *buf, size_t size,
                         loff_t *offset) {
  unsigned short val = 0;
  unsigned char ker_buf[2];
  int err;

  /* 1. 从用户空间复制数据并检查错误 */
  if (copy_from_user(&val, buf, 2)) {
    printk("copy from user error\n");
    return -EFAULT;
  }

  /* 2. lcd的数据 两字节数据 清除高四位，保留12位数据 */
  /* 如果lcd要求配置位置0，直接保留val即可 */
  val = val & 0x0fff;

  /* 3. 构造 SPI 发送的字节顺序(MSB) */
  ker_buf[0] = val >> 8;
  ker_buf[1] = val;

  /* 4. SPI 写入 */
  /* SPI从机设备结构体指针 发送的数据缓冲区首地址 发送的数据字节数 */
  err = spi_write(lcddev.lcd_spidev, ker_buf, 2);
  if (err < 0) {
    return err;
  }

  /* 更新文件偏移 */
  *offset += size;
  return 2;
}

static int lcd_probe(struct spi_device *spi) {
  int err;
  /* 创建设备节点 */
  lcddev.lcd_device =
      device_create(lcddev.lcd_class, &spi->dev, MKDEV(lcddev.lcd_major, 0),
                    NULL, "lcd_device");
  if (IS_ERR(lcddev.lcd_device)) {
    err = PTR_ERR(lcddev.lcd_device);
    printk("%s %s line %d: device_create failed\n", __FILE__, __FUNCTION__,
           __LINE__);
    return err;
  }

  return 0;
}

static int lcd_remove(struct spi_device *dev) {
  printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
  device_destroy(lcddev.lcd_class, MKDEV(lcddev.lcd_major, 0));
  class_destroy(lcddev.lcd_class);
  unregister_chrdev(lcddev.lcd_major, "mylcd");
  return 0;
}

static const struct of_device_id lcd_of_match_table[] = {
    {.compatible = "alientek,lcd"},
    {/* sentinel */},
};

static const struct spi_device_id lcd_ids[] = {
    {"alientek,lcd", 0},
    {/* sentinel */},
};

static struct spi_driver lcd_drv = {
    .probe = lcd_probe,
    .remove = lcd_remove,
    .driver =
        {
            .name = "lcd_drv",
            .of_match_table = lcd_of_match_table,
        },
    .id_table = lcd_ids,
};

static struct file_operations lcd_fops = {
    .owner = THIS_MODULE,
    .write = lcd_write,
};

static int __init lcd_init(void) {
  int err;
  /* 1. 注册字符设备 */
  lcddev.lcd_major = register_chrdev(0, "mylcd", &lcd_fops);
  if (lcddev.lcd_major < 0) {
    printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
    return lcddev.lcd_major;
  }

  /*  2. 创建设备类 */
  lcddev.lcd_class = class_create(THIS_MODULE, "lcd_class");
  if (IS_ERR(lcddev.lcd_class)) {
    printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
    err = PTR_ERR(lcddev.lcd_class);
    unregister_chrdev(lcddev.lcd_major, "mylcd");
    return err;
  }

  /* 3. 注册SPI驱动 */
  err = spi_register_driver(&lcd_drv);
  if (err) {
    printk("%s %s line %d err %d\n", __FILE__, __FUNCTION__, __LINE__, err);
    class_destroy(lcddev.lcd_class);
    unregister_chrdev(lcddev.lcd_major, "mylcd");
    return err;
  }
  return 0;
}

static void __exit lcd_exit(void) {
  spi_unregister_driver(&lcd_drv);
  class_destroy(lcddev.lcd_class);
  unregister_chrdev(lcddev.lcd_major, "lcd");
}

module_init(lcd_init);
module_exit(lcd_exit);

MODULE_LICENSE("GPL");