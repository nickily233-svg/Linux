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

struct dac_dev {
  dev_t dac_devid;
  struct class *dac_class;
  struct device *dac_device;
  int dac_major;
  int dac_irq;
  struct spi_device *dac_spidev;
};

static struct dac_dev dacdev;

static ssize_t dac_write(struct file *filp, const char __user *buf, size_t size,
                         loff_t *offset) {
  unsigned short val = 0;
  unsigned char ker_buf[2];
  int err;

  /* 1. 从用户空间复制数据并检查错误 */
  if (copy_from_user(&val, buf, 2)) {
    printk("copy from user error\n");
    return -EFAULT;
  }

  /* 2. DAC的数据 两字节数据 清除高四位，保留12位数据 */
  /* 如果DAC要求配置位置0，直接保留val即可 */
  val = val & 0x0fff;

  /* 3. 构造 SPI 发送的字节顺序(MSB) */
  ker_buf[0] = val >> 8;
  ker_buf[1] = val;

  /* 4. SPI 写入 */
  /* SPI从机设备结构体指针 发送的数据缓冲区首地址 发送的数据字节数 */
  err = spi_write(dacdev.dac_spidev, ker_buf, 2);
  if (err < 0) {
    return err;
  }

  /* 更新文件偏移 */
  *offset += size;
  return 2;
}

static int dac_probe(struct spi_device *spi) {
  int err;
  /* 创建设备节点 */
  dacdev.dac_device =
      device_create(dacdev.dac_class, &spi->dev, MKDEV(dacdev.dac_major, 0),
                    NULL, "dac_device");
  if (IS_ERR(dacdev.dac_device)) {
    err = PTR_ERR(dacdev.dac_device);
    printk("%s %s line %d: device_create failed\n", __FILE__, __FUNCTION__,
           __LINE__);
    return err;
  }

  return 0;
}

static int dac_remove(struct spi_device *dev) {
  printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
  device_destroy(dacdev.dac_class, MKDEV(dacdev.dac_major, 0));
  class_destroy(dacdev.dac_class);
  unregister_chrdev(dacdev.dac_major, "mydac");
  return 0;
}

static const struct of_device_id dac_of_match_table[] = {
    {.compatible = "alientek,dac"},
    {/* sentinel */},
};

static const struct spi_device_id dac_ids[] = {
    {"alientek,dac", 0},
    {/* sentinel */},
};

static struct spi_driver dac_drv = {
    .probe = dac_probe,
    .remove = dac_remove,
    .driver =
        {
            .name = "dac_drv",
            .of_match_table = dac_of_match_table,
        },
    .id_table = dac_ids,
};

static struct file_operations dac_fops = {
    .owner = THIS_MODULE,
    .write = dac_write,
};

static int __init dac_init(void) {
  int err;
  /* 1. 注册字符设备 */
  dacdev.dac_major = register_chrdev(0, "mydac", &dac_fops);
  if (dacdev.dac_major < 0) {
    printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
    return dacdev.dac_major;
  }

  /*  2. 创建设备类 */
  dacdev.dac_class = class_create(THIS_MODULE, "dac_class");
  if (IS_ERR(dacdev.dac_class)) {
    printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
    err = PTR_ERR(dacdev.dac_class);
    unregister_chrdev(dacdev.dac_major, "mydac");
    return err;
  }

  /* 3. 注册SPI驱动 */
  err = spi_register_driver(&dac_drv);
  if (err) {
    printk("%s %s line %d err %d\n", __FILE__, __FUNCTION__, __LINE__, err);
    class_destroy(dacdev.dac_class);
    unregister_chrdev(dacdev.dac_major, "mydac");
    return err;
  }
  return 0;
}

static void __exit dac_exit(void) {
  spi_unregister_driver(&dac_drv);
  class_destroy(dacdev.dac_class);
  unregister_chrdev(dacdev.dac_major, "dac");
}

module_init(dac_init);
module_exit(dac_exit);

MODULE_LICENSE("GPL");