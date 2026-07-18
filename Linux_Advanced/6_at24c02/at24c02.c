#include "linux/err.h"
#include "linux/kdev_t.h"
#include "linux/printk.h"
#include <cstddef>
#include <linux/device.h> // 需要 device_create 等
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/uaccess.h> // 替换 "asm/uaccess.h"

#define at24c02_read 1000
#define at24c02_write 1001

struct at24c02_dev {
  dev_t at24c02_devid;
  struct class *at24c02_class;
  struct device *at24c02_device;
  int at24c02_major;
  int at24c02_irq;
  struct i2c_client *at24c02_client;
};

static struct at24c02_dev at24c02dev;

/*
 *arg 是一个指针，指向用户空间的一个结构体
 */
static long at24c02_ioctl(struct file *filp, unsigned int cmd,
                          unsigned long arg) {

  unsigned char addr;
  unsigned char data;
  unsigned int ker_buf[2];                     // 内核空间的缓冲区
  unsigned int *usr_buf = (unsigned int *)arg; // 用户空间的指针（来自应用程序）
  unsigned char byte_buf[2];
  struct i2c_msg msgs[2];
  int ret;

  // 从用户空间拷贝参数（8字节：地址+数据）
  if (copy_from_user(ker_buf, usr_buf, 8)) {
    printk("%s %s line %d: copy_from_user failed\n", __FILE__, __FUNCTION__,
           __LINE__);
    return -EFAULT;
  }

  addr = ker_buf[0];
  data = ker_buf[1];

  switch (cmd) {
    /* 读操作 */
  case at24c02_read:
    // 第1个消息：写（发送要读取的地址）
    msgs[0].addr = at24c02dev.at24c02_client->addr; // 芯片地址
    msgs[0].flags = 0;                              // 0 表示写
    msgs[0].len = 1;                                // 长度 1 字节
    msgs[0].buf = &addr;                            // 要发送的地址数据

    // 第2个消息：读（读取芯片返回的数据）
    msgs[1].addr = at24c02dev.at24c02_client->addr;
    msgs[1].flags = I2C_M_RD; // 读标志
    msgs[1].len = 1;          // 读 1 个字节
    msgs[1].buf = &data;      // 存放读到的数据

    /* i2c_transfer 会依次执行这两个 I2C 消息，并且它们之间会自动处理 Restart */
    ret = i2c_transfer(at24c02dev.at24c02_client->adapter, msgs, 2);
    if (ret != 2) {
      printk("%s %s line %d: i2c read error %d\n", __FILE__, __FUNCTION__,
             __LINE__, ret);
      return -EIO;
    }

    // 把读到的数据拷贝回用户空间
    ker_buf[1] = data;
    if (copy_to_user(usr_buf, ker_buf, 8)) {
      printk("%s %s line %d: copy_to_user failed\n", __FILE__, __FUNCTION__,
             __LINE__);
      return -EFAULT;
    }
    break;

    /* 写操作 */
  case at24c02_write:
    // 写操作：先写地址，再写数据（I2C连续写）
    byte_buf[0] = addr; // 要写入的地址
    byte_buf[1] = data; // 要写入的数据

    msgs[0].addr = at24c02dev.at24c02_client->addr;
    msgs[0].flags = 0;      // 写
    msgs[0].len = 2;        // 长度 2 字节（地址+数据）
    msgs[0].buf = byte_buf; // 缓冲区

    /* i2c_transfer 的返回值：成功返回的是成功传输的消息数量 */
    ret = i2c_transfer(at24c02dev.at24c02_client->adapter, msgs, 1);
    if (ret != 1) {
      printk("%s %s line %d: i2c write error %d\n", __FILE__, __FUNCTION__,
             __LINE__, ret);
      return -EIO;
    }
    break;

    /* 其他cmd */
  default:
    printk("%s %s line %d: unknown cmd %d\n", __FILE__, __FUNCTION__, __LINE__,
           cmd);
    return -EINVAL;
  }

  return 0;
}

static int at24c02_probe(struct i2c_client *client,
                         const struct i2c_device_id *id) {
  int err;
  at24c02dev.at24c02_client = client;
  printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);

  /* 创建设备节点 */
  at24c02dev.at24c02_device =
      device_create(at24c02dev.at24c02_class, &client->dev,
                    MKDEV(at24c02dev.at24c02_major, 0), NULL, "at25c02_device");
  printk("%s %s line %d: device_create failed\n", __FILE__, __FUNCTION__,
         __LINE__);
  if (IS_ERR(at24c02dev.at24c02_device)) {
    err = PTR_ERR(at24c02dev.at24c02_device);
    return err;
  }

  return 0;
}

static int at24c02_remove(struct i2c_client *client) {
  printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
  device_destroy(at24c02dev.at24c02_class, MKDEV(at24c02dev.at24c02_major, 0));
  return 0;
}

static const struct of_device_id at24c02_match_table[] = {
    {.compatible = "alientek,at24c02"},
    {/* sentinel */},
};

static const struct i2c_device_id at24c02_ids[] = {
    {"alientek,at24c02", 0},
    {/* sentinel */},
};

static struct i2c_driver at24c02_drv = {
    .probe = at24c02_probe,
    .remove = at24c02_remove,
    .driver =
        {
            .name = "at24c02_drv",
            .of_match_table = at24c02_match_table,
        },
    .id_table = at24c02_ids,
};

static struct file_operations at24c02_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = at24c02_ioctl,
};

static int __init at24c02_init(void) {
  int err;

  /* 1. 注册字符设备 */
  at24c02dev.at24c02_major = register_chrdev(0, "myat24c02", &at24c02_fops);
  if (at24c02dev.at24c02_major < 0) {
    printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
    return at24c02dev.at24c02_major;
  }

  /*  2. 创建设备类 */
  at24c02dev.at24c02_class = class_create(THIS_MODULE, "at24c02_class");
  if (IS_ERR(at24c02dev.at24c02_class)) {
    printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
    err = PTR_ERR(at24c02dev.at24c02_class);
    unregister_chrdev(at24c02dev.at24c02_major, "myat24c02");
    return err;
  }

  /* 3. 注册I2C驱动 */
  err = i2c_add_driver(&at24c02_drv);
  if (err) {
    printk("%s %s line %d err %d\n", __FILE__, __FUNCTION__, __LINE__, err);
    class_destroy(at24c02dev.at24c02_class);
    unregister_chrdev(at24c02dev.at24c02_major, "myat24c02");
    return err;
  }

  printk("%s %s line %d: init ok\n", __FILE__, __FUNCTION__, __LINE__);
  return 0;
}

static void __exit at24c02_exit(void) {
  printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

  i2c_del_driver(&at24c02_drv);
  class_destroy(at24c02dev.at24c02_class);
  unregister_chrdev(at24c02dev.at24c02_major, "at24c02");
}

module_init(at24c02_init);
module_exit(at24c02_exit);

MODULE_LICENSE("GPL");