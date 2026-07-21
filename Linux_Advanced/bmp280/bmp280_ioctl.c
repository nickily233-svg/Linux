#include "asm-generic/ioctl.h"
#include "asm/uaccess.h"
#include "linux/i2c.h"
#include "linux/mutex.h"
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/kdev_t.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define BMP280_READ_P _IOR('b', 1, s32)
#define BMP280_READ_T _IOR('b', 2, s32)

struct bmp280dev {
  u8 chip_id;
  dev_t bmp280_dev_id;
  struct class *bmp280_class;
  struct device *bmp280_device;
  int bmp280_major;
  struct i2c_client *bmp280_i2c_client;
  struct hrtimer *bmp280_hrtimer;
  int bmp280_irq;
  struct mutex bmp280_lock;

  /* --- 校准系数 --- */
  unsigned short dig_T1;
  short dig_T2, dig_T3;
  unsigned short dig_P1;
  short dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
};

static struct bmp280dev bmp280;

static long bmp280_ioctl(struct file *filp, unsigned int cmd,
                         unsigned long arg) {

  struct i2c_client *client = bmp280.bmp280_i2c_client;
  s32 LastP = 0;
  s32 LastT = 0;
  int err;

  unsigned char raw_data[6];
  unsigned char write_buf[3] = {0xF4, 0x27, 0x3F};
  unsigned char reg_0xF7_addr = 0xF7;
  s32 adc_p, adc_t;
  s32 var1, var2, t_fine;
  s32 T;
  s32 P;
  s64 var1_64, var2_64, p;
  struct i2c_msg msgs[3];

  switch (cmd) {
  case BMP280_READ_P:
  case BMP280_READ_T:
    /* 避免多线程并发导致 I2C 总线冲突 加互斥锁*/
    mutex_lock(&bmp280.bmp280_lock);
    /* 检查是否有 i2c_client */
    if (!bmp280.bmp280_i2c_client) {
      printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
      goto out_unlock;
    }

    /* 向寄存器 0xF4 写入 0x27，触发一次温度+气压的测量 */
    /* msg[0] */
    msgs[0].addr = bmp280.bmp280_i2c_client->addr;
    msgs[0].buf = write_buf;
    msgs[0].flags = 0;
    msgs[0].len = 2;

    err = i2c_transfer(client->adapter, &msgs[0],
                       1 /* 一次性发送几条i2c_msg消息 */);
    if (err != 1) {
      printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
      goto out_unlock;
    }

    /* 轮询等待测量完成（BMP280 需要约 20~30 毫秒） */
    /* 释放锁进行休眠，让出 I2C 总线 */
    mutex_unlock(&bmp280.bmp280_lock);
    msleep(30);
    mutex_lock(&bmp280.bmp280_lock);

    /* 先写从哪个寄存器开始读,再读取数据 */
    msgs[1].addr = bmp280.bmp280_i2c_client->addr;
    msgs[1].flags = 0;
    msgs[1].len = 1;
    msgs[1].buf = &reg_0xF7_addr;

    msgs[2].addr = bmp280.bmp280_i2c_client->addr;
    msgs[2].flags = 1;
    msgs[2].len = 6;
    msgs[2].buf = raw_data;

    err = i2c_transfer(client->adapter,
                       &msgs[1] /* 传入了 msgs[1] 为首地址，数量是 2 */, 2);
    if (err != 2) {
      printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
      goto out_unlock;
    }

    /* 处理原始数据 */
    /* 换算成压强 */
    adc_p =
        (s32)((raw_data[0]) << 12 | (raw_data[1] << 4) | (raw_data[2] >> 4));
    adc_t =
        (s32)((raw_data[3] << 12) | (raw_data[4] << 4) | (raw_data[5] >> 4));

    /*  温度补偿 */
    var1 =
        ((((adc_t >> 3) - ((s32)bmp280.dig_T1 << 1))) * ((s32)bmp280.dig_T2)) >>
        11;
    var2 = (((((adc_t >> 4) - ((s32)bmp280.dig_T1)) *
              ((adc_t >> 4) - ((s32)bmp280.dig_T1))) >>
             12) *
            ((s32)bmp280.dig_T3)) >>
           14;
    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >>
        8; // 这里是真实的温度值，乘以 100 (例如 25.5℃ 算出来是 2550)

    /* 气压补偿 */
    var1_64 = ((int64_t)t_fine) - 128000;
    var2_64 = var1_64 * var1_64 * (int64_t)bmp280.dig_P6;
    var2_64 = var2_64 + ((var1_64 * (int64_t)bmp280.dig_P5) << 17);
    var2_64 = var2_64 + (((int64_t)bmp280.dig_P4) << 35);
    var1_64 = ((var1_64 * var1_64 * (int64_t)bmp280.dig_P3) >> 8) +
              ((var1_64 * (int64_t)bmp280.dig_P2) << 12);
    var1_64 =
        (((((int64_t)1) << 47) + var1_64)) * ((int64_t)bmp280.dig_P1) >> 33;

    /* 防止除零导致内核崩溃 */
    if (var1_64 == 0) {
      printk("bmp280: result = 0 read failed\n");
      return -EFAULT;
    }

    p = 1048576 - adc_p;
    p = (((p << 31) - var2_64) * 3125) / var1_64;
    var1_64 = (((int64_t)bmp280.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2_64 = (((int64_t)bmp280.dig_P8) * p) >> 19;
    p = ((p + var1_64 + var2_64) >> 8) + (((int64_t)bmp280.dig_P7) << 4);

    /* 最终温度，单位：摄氏度 (°C) */
    LastT = T;
    /* 最终气压，单位：帕斯卡 (Pa) */
    LastP = (s32)p >> 8;

    /* 解互斥锁 */
    mutex_unlock(&bmp280.bmp280_lock);

    if (cmd == BMP280_READ_P) {
      if (copy_to_user((s32 __user *)arg, &LastP, sizeof(LastP))) {
        printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
        return -EFAULT;
      }
    } else if (cmd == BMP280_READ_T) {
      if (copy_to_user((s32 __user *)arg, &LastT, sizeof(LastT))) {
        printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
        return -EFAULT;
      }
    }
    break;
  default:
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return -EINVAL;
    break;
  }

out_unlock:
  mutex_unlock(&bmp280.bmp280_lock);
  return -EFAULT;

  return 0;
}

static struct file_operations bmp280_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = bmp280_ioctl,
};

static int bmp280_probe(struct i2c_client *client,
                        const struct i2c_device_id *id) {
  int ret;
  struct i2c_msg msg[2];
  struct i2c_msg check[2];
  unsigned char reg_0x88_addr = 0x88;
  unsigned char reg_0xd0_addr = 0xd0; /* 存放 ID 寄存器的地址 */
  unsigned char read_id;
  /* 校准数据 */
  unsigned char calib_data[24];
  /* 保存 I2C指针 */
  bmp280.bmp280_i2c_client = client;

  /* 创建设备 */
  bmp280.bmp280_device =
      device_create(bmp280.bmp280_class, NULL, MKDEV(bmp280.bmp280_major, 0),
                    NULL, "bmp280_device");
  if (IS_ERR((bmp280.bmp280_device))) {
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return PTR_ERR(bmp280.bmp280_device);
  }

  check[0].addr = client->addr;
  check[0].buf = &reg_0xd0_addr;
  check[0].flags = 0;
  check[0].len = 1;

  check[1].addr = client->addr;
  check[1].buf = &read_id;
  check[1].flags = 1;
  check[1].len = 1;

  if (i2c_transfer(client->adapter, &check[0], 2) != 2) {
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return -EIO;
  }
  if (read_id != 0x58) {
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return -ENODEV;
  }

  /* 从 0x88 寄存器开始，连续读取 24 字节的出厂校准系数 */
  msg[0].addr = client->addr;
  msg[0].flags = 0;
  msg[0].len = 1;
  msg[0].buf = &reg_0x88_addr;

  msg[1].addr = client->addr;
  msg[1].flags = 1;
  msg[1].len = 24;
  msg[1].buf = calib_data;

  ret = i2c_transfer(client->adapter, &msg[0], 2);
  if (ret) {
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return -EFAULT;
  }

  /* 按照高位在前低位在后的规则，解析到结构体里 */
  bmp280.dig_T1 = (calib_data[1] << 8) | calib_data[0];
  bmp280.dig_T2 = (calib_data[3] << 8) | calib_data[2];
  bmp280.dig_T3 = (calib_data[5] << 8) | calib_data[4];
  bmp280.dig_P1 = (calib_data[7] << 8) | calib_data[6];
  bmp280.dig_P2 = (calib_data[9] << 8) | calib_data[8];
  bmp280.dig_P3 = (calib_data[11] << 8) | calib_data[10];
  bmp280.dig_P4 = (calib_data[13] << 8) | calib_data[12];
  bmp280.dig_P5 = (calib_data[15] << 8) | calib_data[14];
  bmp280.dig_P6 = (calib_data[17] << 8) | calib_data[16];
  bmp280.dig_P7 = (calib_data[19] << 8) | calib_data[18];
  bmp280.dig_P8 = (calib_data[21] << 8) | calib_data[20];
  bmp280.dig_P9 = (calib_data[23] << 8) | calib_data[22];

  return 0;
}

static int bmp280_remove(struct i2c_client *client) {
  device_destroy(bmp280.bmp280_class, MKDEV(bmp280.bmp280_major, 0));
  class_destroy(bmp280.bmp280_class);
  unregister_chrdev(bmp280.bmp280_major, "bmp280_dev");
  return 0;
}

static const struct of_device_id bmp280_of_match_table[] = {
    {.compatible = "alientek_bmp280"},
    {/* sentinel */},
};

static const struct i2c_device_id bmp280_ids[] = {
    {"alientek_bmp280"},
    {/* sentinel */},
};

static struct i2c_driver bmp280_i2c_driver = {
    .driver =
        {
            .name = "bmp280",
            .of_match_table = bmp280_of_match_table,
        },
    .probe = bmp280_probe,
    .remove = bmp280_remove,
    .id_table = bmp280_ids,
};

static int __init bmp280_init(void) {
  int err;
  /* 注册字符设备 */
  bmp280.bmp280_major =
      register_chrdev(bmp280.bmp280_major, "bmp280_dev", &bmp280_fops);
  if (bmp280.bmp280_major < 0) {
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return -ENOMEM;
  }

  /* 创建设备类 */
  bmp280.bmp280_class = class_create(THIS_MODULE, "bmp280_class");
  if (IS_ERR(bmp280.bmp280_class)) {
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return PTR_ERR(bmp280.bmp280_class);
  }

  /* 注册 I2C */
  err = i2c_add_driver(&bmp280_i2c_driver);
  if (err) {
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return -EFAULT;
  }
  return 0;
}

static void __exit bmp280_exit(void) { /* 注销 I2C */
  i2c_del_driver(&bmp280_i2c_driver);
}

module_init(bmp280_init);
module_exit(bmp280_exit);

MODULE_LICENSE("GPL v2");