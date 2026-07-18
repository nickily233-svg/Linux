#include "linux/err.h"
#include "linux/kdev_t.h"
#include "linux/printk.h"
#include <linux/device.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>

#define oled_read 1000
#define oled_write 1001

struct oled_dev {
  dev_t oled_devid;
  struct class *oled_class;
  struct device *oled_device;
  int oled_major;
  int oled_irq;
};

static struct oled_dev oleddev;

static long oled_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {

#if 0
  int len0, len1, len2;
  unsigned char *buf0, buf1, buf2, buf3;
#endif

  struct spi_message msg;
  struct spi_transfer trans[3];

#if 0
  trans[0].tx_buf = buf0;
  trans[0].len = len0;

  trans[1].rx_buf = &buf1;
  trans[1].len = len1;

  trans[2].tx_buf = &buf2;
  trans[2].rx_buf = &buf3;
  trans[3].len = len2;
#endif

  /* 初始化spi环形缓冲区 */
  spi_message_init(&msg);
  /* 将消息数组发送到环形缓冲区 */
  spi_message_add_tail(&trans[0], &msg);
  spi_message_add_tail(&trans[1], &msg);
  spi_message_add_tail(&trans[2], &msg);

  return 0;
}

static int oled_probe(struct spi_device *spi) {
  int err;
  printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);

  /* 创建设备节点 */
  oleddev.oled_device =
      device_create(oleddev.oled_class, &spi->dev, MKDEV(oleddev.oled_major, 0),
                    NULL, "oled_device");
  if (IS_ERR(oleddev.oled_device)) {
    err = PTR_ERR(oleddev.oled_device);
    printk("%s %s line %d: device_create failed\n", __FILE__, __FUNCTION__,
           __LINE__);
    return err;
  }

  return 0;
}

static int oled_remove(struct spi_device *dev) {
  printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
  device_destroy(oleddev.oled_class, MKDEV(oleddev.oled_major, 0));
  class_destroy(oleddev.oled_class);
  unregister_chrdev(oleddev.oled_major, "myoled");
  return 0;
}

static const struct of_device_id oled_of_match_table[] = {
    {.compatible = "alientek,oled"},
    {/* sentinel */},
};

static const struct spi_device_id oled_ids[] = {
    {"alientek,oled", 0},
    {/* sentinel */},
};

static struct spi_driver oled_drv = {
    .probe = oled_probe,
    .remove = oled_remove,
    .driver =
        {
            .name = "oled_drv",
            .of_match_table = oled_of_match_table,
        },
    .id_table = oled_ids,
};

static struct file_operations oled_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = oled_ioctl,
};

static int __init oled_init(void) {
  int err;

  /* 1. 注册字符设备 */
  oleddev.oled_major = register_chrdev(0, "myoled", &oled_fops);
  if (oleddev.oled_major < 0) {
    printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
    return oleddev.oled_major;
  }

  /*  2. 创建设备类 */
  oleddev.oled_class = class_create(THIS_MODULE, "oled_class");
  if (IS_ERR(oleddev.oled_class)) {
    printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
    err = PTR_ERR(oleddev.oled_class);
    unregister_chrdev(oleddev.oled_major, "myoled");
    return err;
  }

  /* 3. 注册SPI驱动 */
  err = spi_register_driver(&oled_drv);
  if (err) {
    printk("%s %s line %d err %d\n", __FILE__, __FUNCTION__, __LINE__, err);
    class_destroy(oleddev.oled_class);
    unregister_chrdev(oleddev.oled_major, "myoled");
    return err;
  }

  printk("%s %s line %d: init ok\n", __FILE__, __FUNCTION__, __LINE__);
  return 0;
}

static void __exit oled_exit(void) {
  printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

  spi_unregister_driver(&oled_drv);
  class_destroy(oleddev.oled_class);
  unregister_chrdev(oleddev.oled_major, "oled");
}

module_init(oled_init);
module_exit(oled_exit);

MODULE_LICENSE("GPL");