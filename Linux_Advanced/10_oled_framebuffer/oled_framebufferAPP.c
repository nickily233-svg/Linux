#include "linux/fb.h"
#include <fcntl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int fb_fd;
static struct fb_var_screeninfo var; /* Current var */
static int screen_size;
static unsigned char *fb_base;
static unsigned int line_width;
static unsigned int pixel_width;

#define OLED_SET_XY 99
#define OLED_SET_XY_WRITE_DATA 100
#define OLED_SET_XY_WRITE_DATAS 101
#define OLED_SET_DATAS 102 /* 102为低8位, 高16位用来表示长度 */

/* 在LCD指定位置上输出指定颜色（描点） */
void lcd_put_pixel(int x, int y, unsigned int color) {
  unsigned char *pen_8 = fb_base + y * line_width + x * pixel_width;
  unsigned short *pen_16;
  unsigned int *pen_32;

  unsigned int red, green, blue;

  pen_16 = (unsigned short *)pen_8;
  pen_32 = (unsigned int *)pen_8;

  switch (var.bits_per_pixel) {
  case 8: {
    *pen_8 = color;
    break;
  }
  case 16: {
    /* 565 */
    red = (color >> 16) & 0xff;
    green = (color >> 8) & 0xff;
    blue = (color >> 0) & 0xff;
    color = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
    *pen_16 = color;
    break;
  }
  case 32: {
    *pen_32 = color;
    break;
  }
  default: {
    printf("can't surport %dbpp\n", var.bits_per_pixel);
    break;
  }
  }
}

/*
 * ./oled_frame_bufferAPP <dev>
 */

int main(int argc, char **argv) {
  int i;

  if (argc != 2) {
    printf("Usage: %s <dev>\n", argv[0]);
    return -1;
  }

  fb_fd = open(argv[1], O_RDWR);
  if (fb_fd < 0) {
    printf(" can not open %s\n", argv[1]);
    return -1;
  }

  if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var)) {
    printf("can't get var\n");
    return -1;
  }

  printf("LCD info: %d x %d, %dbpp\n", var.xres, var.yres, var.bits_per_pixel);

  line_width = var.xres * var.bits_per_pixel / 8;
  pixel_width = var.bits_per_pixel / 8;
  screen_size = var.xres * var.yres * var.bits_per_pixel / 8;
  fb_base = (unsigned char *)mmap(NULL, screen_size, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, fb_fd, 0);
  if (fb_base == (unsigned char *)-1) {
    printf("can't mmap\n");
    return -1;
  }

  /* 清屏: 全部设为半白半黑 */
  memset(fb_base, 0xff, screen_size / 2);
  memset(fb_base + screen_size / 2, 0, screen_size / 2);

  munmap(fb_base, screen_size);

  close(fb_fd);

  return 0;
}