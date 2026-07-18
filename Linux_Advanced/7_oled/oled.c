#include "linux/err.h"
#include "linux/gpio/consumer.h"
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

#define OLED_SET_XY 99
#define OLED_SET_XY_WRITE_DATA 100
#define OLED_SET_XY_WRITE_DATAS 101
#define OLED_SET_DATAS 102 /* 102为低8位, 高16位用来表示长度 */

static char data_buf[1024];

// 为0 表示命令，为1表示数据
#define OLED_CMD 0
#define OLED_DATA 1

struct oled_dev {
  dev_t oled_devid;
  struct class *oled_class;
  struct device *oled_device;
  int oled_major;
  int oled_irq;
  struct gpio_desc *dc_gpio;
  struct spi_device *oled_spidev;
};

static struct oled_dev oleddev;

/* 核心通信底层 */
/*
 * 负责发送一个字节，通过控制DC引脚的高低电平告诉OLED
 * 0 - 代表这是个命令，1 - 就代表这是个要显示的数据
 * 然后调用 spi_write 把他发过去
 */
static void oled_write_cmd_data(unsigned char uc_data, unsigned char uc_cmd) {
  if (uc_cmd == 0) {
    //*GPIO4_DR_s &= ~(1<<20);//拉低，表示写入指令
    gpiod_set_value(oleddev.dc_gpio, 0);
  } else {
    //*GPIO4_DR_s |= (1<<20);//拉高，表示写入数据
    gpiod_set_value(oleddev.dc_gpio, 1);
  }
  // spi_writeread(ESCPI1_BASE,uc_data);//写入
  spi_write(oleddev.oled_spidev, &uc_data, 1);
}

/*
 * 负责发送多个字节
 * 先将 DC-GPIO 拉高，然后把整段数据通过 SPI 发送
 */
static void oled_write_datas(unsigned char *buf, int len) {
  //*GPIO4_DR_s |= (1<<20);//拉高，表示写入数据
  gpiod_set_value(oleddev.dc_gpio, 1);
  // spi_writeread(ESCPI1_BASE,uc_data);//写入
  spi_write(oleddev.oled_spidev, buf, len);
}

/*
 * 屏幕初始化
 * 写入时，DC-GPIO 拉低在写入数据 uc_data
 */
static int oled_hardware_init(void) {
  oled_write_cmd_data(0xae, OLED_CMD); // 关闭显示

  oled_write_cmd_data(0x00, OLED_CMD); // 设置 lower column address
  oled_write_cmd_data(0x10, OLED_CMD); // 设置 higher column address

  oled_write_cmd_data(0x40, OLED_CMD); // 设置 display start line

  oled_write_cmd_data(0xB0, OLED_CMD); // 设置page address

  oled_write_cmd_data(0x81, OLED_CMD); // contract control
  oled_write_cmd_data(0x66, OLED_CMD); // 128

  oled_write_cmd_data(0xa1, OLED_CMD); // 设置 segment remap

  oled_write_cmd_data(0xa6, OLED_CMD); // normal /reverse

  oled_write_cmd_data(0xa8, OLED_CMD); // multiple ratio
  oled_write_cmd_data(0x3f, OLED_CMD); // duty = 1/64

  oled_write_cmd_data(0xc8, OLED_CMD); // com scan direction

  oled_write_cmd_data(0xd3, OLED_CMD); // set displat offset
  oled_write_cmd_data(0x00, OLED_CMD); //

  oled_write_cmd_data(0xd5, OLED_CMD); // set osc division
  oled_write_cmd_data(0x80, OLED_CMD); //

  oled_write_cmd_data(0xd9, OLED_CMD); // ser pre-charge period
  oled_write_cmd_data(0x1f, OLED_CMD); //

  oled_write_cmd_data(0xda, OLED_CMD); // set com pins
  oled_write_cmd_data(0x12, OLED_CMD); //

  oled_write_cmd_data(0xdb, OLED_CMD); // set vcomh
  oled_write_cmd_data(0x30, OLED_CMD); //

  oled_write_cmd_data(0x8d, OLED_CMD); // set charge pump disable
  oled_write_cmd_data(0x14, OLED_CMD); //

  oled_write_cmd_data(0xaf, OLED_CMD); // set dispkay on

  return 0;
}

/*
 * 坐标定位 设置x,y坐标 命令
 * OLED 寻址方式按页和列来算的
 */
void OLED_DIsp_Set_Pos(int x, int y) {
  oled_write_cmd_data(0xb0 + y, OLED_CMD);
  oled_write_cmd_data((x & 0x0f), OLED_CMD);
  oled_write_cmd_data(((x & 0xf0) >> 4) | 0x10, OLED_CMD);
}

/*
 * 清屏
 */
static void OLED_DIsp_Clear(void) {
  /* 定义x和y坐标 */
  /* 遍历OLED的8个页，每一页128字节，全部写入数据0，像素点清零 */
  unsigned char x, y;
  for (y = 0; y < 8; y++) {
    OLED_DIsp_Set_Pos(0, y);
    for (x = 0; x < 128; x++)
      oled_write_cmd_data(0, OLED_DATA); /* 清零 */
  }
}

/*
 * 用户交互中心
 */
static long oled_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {

  const void __user *from = (const void __user *)arg;
  char param_buf[3];
  int size;
  int err;

  switch (cmd & 0xff) {
    /* OLED_SET_XY：仅设置坐标（APP 传 2 个参数：x, y） */
  case OLED_SET_XY: {
    err = copy_from_user(param_buf, from, 2);
    OLED_DIsp_Set_Pos(param_buf[0], param_buf[1]);
    break;
  }
    /* OLED_SET_XY_WRITE_DATA：设置坐标，然后立刻写 1 个字节的数据（APP 传 3
     * 个参数：x, y, data） */
  case OLED_SET_XY_WRITE_DATA: {
    err = copy_from_user(param_buf, from, 3);
    OLED_DIsp_Set_Pos(param_buf[0], param_buf[1]);
    oled_write_cmd_data(param_buf[2], OLED_DATA);
    break;
  }
    /* OLED_SET_XY_WRITE_DATAS：设置坐标，然后写入一段长度为 size
     * 的数据。比如画一个中文字，需要传 32 个字节 */
  case OLED_SET_XY_WRITE_DATAS: {
    err = copy_from_user(param_buf, from, 3);
    size = param_buf[2];
    err = copy_from_user(data_buf, from + 3, size);

    OLED_DIsp_Set_Pos(param_buf[0], param_buf[1]);
    oled_write_datas(data_buf, size);

    break;
  }
    /* OLED_SET_DATAS：不改变坐标，直接在当前光标处写入一段长度为 size 的数据 */
  case OLED_SET_DATAS: {
    size = cmd >> 8;
    err = copy_from_user(data_buf, from, size);
    oled_write_datas(data_buf, size);
    break;
  }
  }
  return 0;
}

static int oled_probe(struct spi_device *spi) {
  int err;
  printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);

  /* 1.获取GPIO */
  oleddev.dc_gpio = gpiod_get(&spi->dev, "dc-gpio", GPIOD_OUT_HIGH);

  /* 2. 创建设备节点 */
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
  gpiod_put(oleddev.dc_gpio);
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