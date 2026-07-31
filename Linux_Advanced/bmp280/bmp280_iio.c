#include "linux/iio/types.h"
#include "linux/kernel.h"
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/kdev_t.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/uaccess.h>

struct bmp280_data {
  unsigned char chip_id;
  struct i2c_client *bmp280data_i2c_client;
  struct mutex lock;

  /* --- 校准系数 --- */
  unsigned short dig_T1;
  short dig_T2, dig_T3;
  unsigned short dig_P1;
  short dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
};

static struct bmp280_data bmp280data;

static int bmp280_read_raw(struct iio_dev *indio_dev,
                           struct iio_chan_spec const *channel, int *val1,
                           int *val2, long mask) {

  struct bmp280_data *data = iio_priv(indio_dev);
  struct i2c_client *client = data->bmp280data_i2c_client;
  unsigned char raw_data[6];
  unsigned char write_buf[3] = {0xF4, 0x27, 0x3F};
  unsigned char reg_0xF7_addr = 0xF7;

  int32_t adc_p, adc_t;
  int32_t var1, var2, t_fine;
  int32_t T;
  int32_t P;
  int64_t var1_64, var2_64, p;

  struct i2c_msg msgs[3];

  mutex_lock(&data->lock);

  /* 检查是否有 i2c_client */
  if (!bmp280data.bmp280data_i2c_client) {
    mutex_unlock(&data->lock);
    return -EIO;
  }

  /* 向寄存器 0xF4 写入 0x27，触发一次温度+气压的测量 */
  /* msg[0] */
  msgs[0].addr = bmp280data.bmp280data_i2c_client->addr;
  msgs[0].buf = write_buf;
  msgs[0].flags = 0;
  msgs[0].len = 2;

  if (i2c_transfer(client->adapter, &msgs[0],
                   1 /* 一次性发送几条i2c_msg消息 */) != 1) {
    mutex_unlock(&data->lock);
    dev_err(&client->dev, "write measurement cmd failed\n");
    return -EIO;
  }

  mutex_lock(&data->lock);
  /* 轮询等待测量完成（bmp280data 需要约 20~30 毫秒） */
  msleep(30);
  mutex_unlock(&data->lock);

  /* 先写从哪个寄存器开始读,再读取数据 */
  msgs[1].addr = bmp280data.bmp280data_i2c_client->addr;
  msgs[1].flags = 0;
  msgs[1].len = 1;
  msgs[1].buf = &reg_0xF7_addr;

  msgs[2].addr = bmp280data.bmp280data_i2c_client->addr;
  msgs[2].flags = 1;
  msgs[2].len = 6;
  msgs[2].buf = raw_data;

  if (i2c_transfer(client->adapter,
                   &msgs[1] /* 传入了 msgs[1] 为首地址，数量是 2 */, 2) != 2) {
    mutex_unlock(&data->lock);
    dev_err(&client->dev, "read data failed\n");
    return -EIO;
  }

  /* 处理原始数据 */
  /* 换算成压强 */
  adc_p =
      (int32_t)((raw_data[0]) << 12 | (raw_data[1] << 4) | (raw_data[2] >> 4));
  adc_t =
      (int32_t)((raw_data[3] << 12) | (raw_data[4] << 4) | (raw_data[5] >> 4));

  /*  温度补偿 */
  var1 = ((((adc_t >> 3) - ((int32_t)bmp280data.dig_T1 << 1))) *
          ((int32_t)bmp280data.dig_T2)) >>
         11;
  var2 = (((((adc_t >> 4) - ((int32_t)bmp280data.dig_T1)) *
            ((adc_t >> 4) - ((int32_t)bmp280data.dig_T1))) >>
           12) *
          ((int32_t)bmp280data.dig_T3)) >>
         14;
  t_fine = var1 + var2;
  T = (t_fine * 5 + 128) >>
      8; // 这里是真实的温度值，乘以 100 (例如 25.5℃ 算出来是 2550)

  /* 气压补偿 */
  var1_64 = ((int64_t)t_fine) - 128000;
  var2_64 = var1_64 * var1_64 * (int64_t)bmp280data.dig_P6;
  var2_64 = var2_64 + ((var1_64 * (int64_t)bmp280data.dig_P5) << 17);
  var2_64 = var2_64 + (((int64_t)bmp280data.dig_P4) << 35);
  var1_64 = ((var1_64 * var1_64 * (int64_t)bmp280data.dig_P3) >> 8) +
            ((var1_64 * (int64_t)bmp280data.dig_P2) << 12);
  var1_64 =
      (((((int64_t)1) << 47) + var1_64)) * ((int64_t)bmp280data.dig_P1) >> 33;

  /* 防止除零导致内核崩溃 */
  if (var1_64 == 0) {
    printk("bmp280data: result = 0 read failed\n");
    return -EFAULT;
  }

  p = 1048576 - adc_p;
  p = (((p << 31) - var2_64) * 3125) / var1_64;
  var1_64 = (((int64_t)bmp280data.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
  var2_64 = (((int64_t)bmp280data.dig_P8) * p) >> 19;
  p = ((p + var1_64 + var2_64) >> 8) + (((int64_t)bmp280data.dig_P7) << 4);

  /* 最终气压，单位：帕斯卡 (Pa) */
  P = (int32_t)p >> 8;

  /* --- 根据 IIO 通道要求返回数据 --- */
  switch (mask) {
  case IIO_CHAN_INFO_RAW:
    if (channel->type == IIO_TEMP) {
      *val1 = T;
    } else if (channel->type == IIO_PRESSURE) {
      *val1 = P;
    }
    return IIO_VAL_INT;
  case IIO_CHAN_INFO_SCALE:
    if (channel->type == IIO_TEMP) {
      *val1 = 1;
      *val2 = 100;
      return IIO_VAL_INT_PLUS_MICRO;
    } else if (channel->type == IIO_PRESSURE) {
      *val1 = 1;
      *val2 = 0;
      return IIO_VAL_INT_PLUS_MICRO;
    }
    break;
  default:
    return -EINVAL;
  }
  return -EINVAL;
}

/* IIO 核心操作表 */
static struct iio_info bmp280_info = {
    .read_raw = bmp280_read_raw,
};

/* --- 定义传感器的物理通道 --- */
static struct iio_chan_spec bmp280_channels[] = {
    {
        .type = IIO_TEMP,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
    },
    {
        .type = IIO_PRESSURE,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
    },
};

static int bmp280data_probe(struct i2c_client *client,
                            const struct i2c_device_id *id) {
  struct iio_dev *indio_dev;
  struct bmp280_data *bmpdata;
  int ret;
  struct i2c_msg msg[2];
  struct i2c_msg check[2];
  unsigned char reg_0x88_addr = 0x88;
  unsigned char reg_0xd0_addr = 0xd0; /* 存放 ID 寄存器的地址 */
  unsigned char read_id;
  /* 校准数据 */
  unsigned char calib_data[24];

  /* 1. 分配 IIO 设备结构体 */
  indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*bmpdata));
  if (!(indio_dev)) {
    dev_err(&client->dev, "failed trop allocate iio dev\n");
    return -ENOMEM;
  }

  /*  设置私有数据 */
  bmpdata = iio_priv(indio_dev);
  /* 保存 I2C指针 */
  bmpdata->bmp280data_i2c_client = client;
  mutex_init(&bmpdata->lock);

  /* 2. 校验芯片 ID */
  check[0].addr = client->addr;
  check[0].buf = &reg_0xd0_addr;
  check[0].flags = 0;
  check[0].len = 1;

  check[1].addr = client->addr;
  check[1].buf = &read_id;
  check[1].flags = 1;
  check[1].len = 1;

  if (i2c_transfer(client->adapter, &check[0], 2) != 2) {
    dev_err(&client->dev, "failed to read chip id\n");
    return -EIO;
  }
  if (read_id != 0x58) {
    dev_err(&client->dev, "invalid chip ID 0x%x, expected 0x58\n", read_id);
    return -EIO;
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
    dev_err(&client->dev, "failed to read calibration data\n");
    return -EIO;
  }

  /* 按照高位在前低位在后的规则，解析到结构体里 */
  bmp280data.dig_T1 = (calib_data[1] << 8) | calib_data[0];
  bmp280data.dig_T2 = (calib_data[3] << 8) | calib_data[2];
  bmp280data.dig_T3 = (calib_data[5] << 8) | calib_data[4];
  bmp280data.dig_P1 = (calib_data[7] << 8) | calib_data[6];
  bmp280data.dig_P2 = (calib_data[9] << 8) | calib_data[8];
  bmp280data.dig_P3 = (calib_data[11] << 8) | calib_data[10];
  bmp280data.dig_P4 = (calib_data[13] << 8) | calib_data[12];
  bmp280data.dig_P5 = (calib_data[15] << 8) | calib_data[14];
  bmp280data.dig_P6 = (calib_data[17] << 8) | calib_data[16];
  bmp280data.dig_P7 = (calib_data[19] << 8) | calib_data[18];
  bmp280data.dig_P8 = (calib_data[21] << 8) | calib_data[20];
  bmp280data.dig_P9 = (calib_data[23] << 8) | calib_data[22];

  /* 绑定 IIO 接口、通道并且注册到内核 */
  indio_dev->name = "bmp280";
  indio_dev->channels = bmp280_channels;
  indio_dev->num_channels = ARRAY_SIZE(bmp280_channels);
  indio_dev->info = &bmp280_info;

  return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct of_device_id bmp280data_of_match_table[] = {
    {.compatible = "alientek_bmp280data"},
    {/* sentinel */},
};
MODULE_DEVICE_TABLE(of, bmp280data_of_match_table);

static const struct i2c_device_id bmp280data_ids[] = {
    {"alientek_bmp280data"},
    {/* sentinel */},
};
MODULE_DEVICE_TABLE(i2c, bmp280data_ids);

static struct i2c_driver bmp280data_i2c_driver = {
    .driver =
        {
            .name = "bmp280data",
            .of_match_table = bmp280data_of_match_table,
        },
    .probe = bmp280data_probe,
    .id_table = bmp280data_ids,
};

module_i2c_driver(bmp280data_i2c_driver);

MODULE_LICENSE("GPL v2");