#include "linux/err.h"
#include "linux/fb.h"
#include "linux/gfp.h"
#include "linux/gpio/consumer.h"
#include "linux/kdev_t.h"
#include "linux/kthread.h"
#include "linux/printk.h"
#include "linux/string.h"
#include "linux/types.h"
#include "linux/wait.h"
#include <cstddef>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/export.h>
#include <linux/fb.h>
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

// 为0 表示命令，为1表示数据
#define OLED_CMD 0
#define OLED_DATA 1

/* 线程 */
static struct task_struct *oled_kthread;

struct oled_dev {
  dev_t oled_devid;
  struct class *oled_class;
  struct device *oled_device;
  int oled_major;
  int oled_irq;
  struct spi_device *oled_spiev;
  struct fb_info *oled_fb_info;
  struct gpio_desc *dc_gpio;
  unsigned int pseudo_palette[16];
  unsigned char *data_buf;
};

static struct oled_dev oleddev;

/* 必须要提供该函数 */
static int mylcd_setcolreg(unsigned regno, unsigned red, unsigned green,
                           unsigned blue, unsigned transp,
                           struct fb_info *info) {
  return 1; /* unkown type */
}

static struct fb_ops myfb_ops = {
    .owner = THIS_MODULE,
    .fb_setcolreg = mylcd_setcolreg,
    .fb_fillrect = cfb_fillrect,
    .fb_copyarea = cfb_copyarea,
    .fb_imageblit = cfb_imageblit,
};

/* oled向特定地址写入数据或者命令 */
static void oled_write_cmd_data(unsigned char uc_data, unsigned char uc_cmd) {
  if (uc_cmd == 0) {
    //*GPIO4_DR_s &= ~(1<<20);//拉低，表示写入指令
    gpiod_set_value(oleddev.dc_gpio, 0);
  } else {
    //*GPIO4_DR_s |= (1<<20);//拉高，表示写入数据
    gpiod_set_value(oleddev.dc_gpio, 1);
  }
  // spi_writeread(ESCPI1_BASE,uc_data);//写入
  spi_write(oleddev.oled_spiev, &uc_data, 1);
}

static void oled_write_datas(unsigned char *buf, int len) {
  int err;
  //*GPIO4_DR_s |= (1<<20);//拉高，表示写入数据
  gpiod_set_value(oleddev.dc_gpio, 1);
  // spi_writeread(ESCPI1_BASE,uc_data);//写入
  err = spi_write(oleddev.oled_spiev, buf, len);
}

/* oled_init的初始化，包括SPI控制器得初始化 */
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

/* 设置要显示的位置 */
void OLED_DIsp_Set_Pos(int x, int y) {
  oled_write_cmd_data(0xb0 + y, OLED_CMD);
  oled_write_cmd_data((x & 0x0f), OLED_CMD);
  oled_write_cmd_data(((x & 0xf0) >> 4) | 0x10, OLED_CMD);
}

/* 整个屏幕显示数据清0 */
static void OLED_DIsp_Clear(void) {
  unsigned char x, y;
  for (y = 0; y < 8; y++) {
    OLED_DIsp_Set_Pos(0, y);
    for (x = 0; x < 128; x++)
      oled_write_cmd_data((y < 4) ? 0 : 0xff, OLED_DATA); /* 清零 */
  }
}

/* 读取显存中指定坐标 (x, y) 处的像素状态是“亮（1）”还是“灭（0）” */
static int get_pixel(int x, int y) {
  unsigned char byte = *(oleddev.oled_fb_info->screen_base +
                         y * oleddev.oled_fb_info->fix.line_length + (x >> 3));
  /*screen_base：显存的首地址 y * line_length：先找到第 y 行在显存中的偏移量 (x
   * >> 3)：等价于 x / 8，算出这一行中，第 x 列像素属于第几个字节
   * 结果：拿到了包含目标像素的那一个字节的数据（赋给了 byte）*/
  int bit = x & 0x7; /* 等价于 x % 8 算出在这个字节里
                        目标像素处于第 0~7 位的哪一位 */
  /* 判断像素状态 */
  /* 1 << bit：生成一个掩码，比如 bit 是 2，就会变成 00000100 byte &
   * (mask)：按位与，检查 byte 的对应位是不是 1。如果是，说明像素点亮，返回
   * 1；否则返回 0 */
  if (byte & (1 << bit)) {
    return 1;
  } else {
    return 0;
  }
}

/* 刷新/打包函数 */
/* 把 Linux 系统维护的“显存（分散的横排像素）”，转换成 OLED
 * 屏幕硬件能够理解的“页数据（字节排列）”，并存入发送缓冲区 */
static void convert_fb_to_oled(void) {
  unsigned char data;
  int i = 0;
  int x, page;
  int bit;

  for (page = 0; page < 8; page++) {
    for (x = 0; x < 128; x++) {
      data = 0;
      /* 把同一列、垂直相邻的 8 个像素点，拼成一个 1 字节（8位） 的数据 */
      for (bit = 0; bit < 8; bit++) {
        data |= (get_pixel(x, page * 8 + bit) << bit);
      }
      /* 发送缓冲 */
      oleddev.data_buf[i++] = data;
    }
  }
}

static int oled_thread(void *data) {
  unsigned char y;
  while (1) {
    /* 把Framebuffer的数据刷到OLED上去 */
    convert_fb_to_oled();
    // memcpy(data_buf, oled_fb_info->screen_base, 1024);
    for (y = 0; y < 8; y++) {
      OLED_DIsp_Set_Pos(0, y);
      oled_write_datas(&oleddev.data_buf[y * 128], 128);
      // oled_write_datas(oled_fb_info->screen_base+y*128, 128);
    }
    set_current_state(TASK_INTERRUPTIBLE);
    schedule_timeout(HZ);

    if (kthread_should_stop()) {
      set_current_state(TASK_RUNNING);
      break;
    }
  }
  return 0;
}

static int oled_probe(struct spi_device *spi) {
  /* DMA 地址类型 保存物理地址告诉硬件 */
  dma_addr_t phy_addr;

  /* 设置SPI设备 */
  oleddev.oled_spiev = spi;

  /* 使用frambuffer
   * 三步走 分配/设置/注册 fb_info
   */
  /* 1.设置fb_info */
  oleddev.oled_fb_info = framebuffer_alloc(0, &spi->dev);

  /* 2. 设置fb_info */
  /* a. var变量 : LCD分辨率、颜色格式 */
  oleddev.oled_fb_info->var.xres_virtual = oleddev.oled_fb_info->var.xres = 128;
  oleddev.oled_fb_info->var.yres_virtual = oleddev.oled_fb_info->var.yres = 64;

  oleddev.oled_fb_info->var.bits_per_pixel = 1; /* rgb565 */

  /* b. fix固定量 */
  strcpy(oleddev.oled_fb_info->fix.id, "alientek_lcd");
  /* 字节大小 x*y*每个像素大小/8 */
  oleddev.oled_fb_info->fix.smem_len =
      oleddev.oled_fb_info->var.xres * oleddev.oled_fb_info->var.yres *
      oleddev.oled_fb_info->var.bits_per_pixel / 8;

  /* c. 分配显存 */
  oleddev.oled_fb_info->screen_base = dma_alloc_writecombine(
      NULL, oleddev.oled_fb_info->fix.smem_len, &phy_addr, GFP_KERNEL);

  oleddev.oled_fb_info->fix.smem_start = phy_addr; /* fb的物理地址 */

  oleddev.oled_fb_info->fix.type = FB_TYPE_PACKED_PIXELS;
  oleddev.oled_fb_info->fix.visual = FB_VISUAL_MONO10;

  /* 每一行的长度 */
  oleddev.oled_fb_info->fix.line_length =
      oleddev.oled_fb_info->var.xres * oleddev.oled_fb_info->var.yres *
      oleddev.oled_fb_info->var.bits_per_pixel / 8;

  /* d. fbops */
  oleddev.oled_fb_info->fbops = &myfb_ops;
  oleddev.oled_fb_info->pseudo_palette = oleddev.pseudo_palette;

  register_framebuffer(oleddev.oled_fb_info);

  /* spi oled init */
  oleddev.dc_gpio = gpiod_get(&spi->dev, "dc", GPIOD_OUT_HIGH);

  oleddev.data_buf = dma_alloc_writecombine(
      NULL, oleddev.oled_fb_info->fix.smem_len, &phy_addr, GFP_KERNEL);

  /* 硬件初始化 */
  oled_hardware_init();
  /* 清屏 */
  OLED_DIsp_Clear();

  /* 创建内核线程，用来把Framebuffer的数据通过SPI控制器发送给OLED */
  oled_kthread = kthread_create(oled_kthread, NULL, "alientek_oled");

  wake_up_process(oled_kthread);

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

// static const struct spi_device_id oled_ids[] = {
//     {"alientek,oled", 0},
//     {/* sentinel */},
// };

static struct spi_driver oled_drv = {
    .probe = oled_probe,
    .remove = oled_remove,
    .driver =
        {
            .name = "oled_drv",
            .of_match_table = oled_of_match_table,
        },
    // .id_table = oled_ids,
};

static int __init oled_init(void) {
  int err;
  /* 注册SPI驱动 */
  err = spi_register_driver(&oled_drv);
  if (err) {
    printk("%s %s line %d err %d\n", __FILE__, __FUNCTION__, __LINE__, err);
    class_destroy(oleddev.oled_class);
    unregister_chrdev(oleddev.oled_major, "myoled");
    return err;
  }
  return err;
}

static void __exit oled_exit(void) { spi_unregister_driver(&oled_drv); }

module_init(oled_init);
module_exit(oled_exit);

MODULE_LICENSE("GPL");