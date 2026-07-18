#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

int fd;

#define OLED_SET_XY 99
#define OLED_SET_XY_WRITE_DATA 100
#define OLED_SET_XY_WRITE_DATAS 101
#define OLED_SET_DATAS 102 /* 102为低8位, 高16位用来表示长度 */

/*
 * 显示单个字符
 */
void OLED_DIsp_Char(int x, int y, unsigned char chr) {
  int i = 0;
  /* 得到字模 */
  /* oled_asc2_8x16 是一个全局数组，里面存了所有可见 ASCII 字符的位图数据 */
  /* 因为第一个可见字符是空格（ASCII 码 0x20），所以通过 chr - ' ' 就能算出 c
   * 这个字符在字库数组里的偏移量 */
  const unsigned char *dots = oled_asc2_8x16[chr - ' '];
  /* 定义位置数组 */
  char pos[2];
#if 0
	/* 发给OLED */
	OLED_DIsp_Set_Pos(x, y);
	/* 发出8字节数据 */
	for (i = 0; i < 8; i++)
		oled_write_cmd_data(dots[i], OLED_DATA);
#endif

  pos[0] = x;
  /* OLED的第一页 */
  pos[1] = y;

  ioctl(fd, OLED_SET_XY, pos);
  ioctl(fd, OLED_SET_DATAS | (8 << 8), dots);

#if 0
	OLED_DIsp_Set_Pos(x, y+1);
	/* 发出8字节数据 */
	for (i = 0; i < 8; i++)
		oled_write_cmd_data(dots[i+8], OLED_DATA);
#endif
  pos[0] = x;
  /* OLED的第二页 */
  pos[1] = y + 1;

  ioctl(fd, OLED_SET_XY, pos);
  ioctl(fd, OLED_SET_DATAS | (8 << 8), &dots[8]);
}

/*
 * 显示字符串
 */
void OLED_DIsp_String(int x, int y, char *str) {
  unsigned char j = 0;
  while (str[j]) { // 1. 从字符串第 0 个字符开始，一直循环直到遇到字符串结束符
                   // '\0' (即 0)
    OLED_DIsp_Char(x, y, str[j]); // 2. 在 (x, y) 位置显示当前这个字符
    x += 8;        // 3. 一个字符宽 8 像素，所以 X 坐标向右平移 8 个像素
    if (x > 127) { // 4. 如果 X 超过了 127（屏幕最右侧），说明一行写满了！
      x = 0;       //    回到最左边（第 0 列）
      y += 2; //    换行！因为 8x16 的字符高度是 16 像素，正好跨越 OLED 的 2 页
    }
    j++; // 5. 处理字符串的下一个字符
  }
}

/*
 * 显示汉字
 */
void OLED_DIsp_CHinese(unsigned char x, unsigned char y, unsigned char no) {
  unsigned char t, addr = 0;
  char pos[2];

  pos[0] = x;
  pos[1] = y;

  ioctl(fd, OLED_SET_XY, pos);

  for (t = 0; t < 16; t++) { // 显示上半截字符
    // oled_write_cmd_data(hz_1616[no][t*2],OLED_DATA);
    ioctl(fd, OLED_SET_DATAS | (1 << 8), &hz_1616[no][t * 2]);

    addr += 1;
  }

  pos[0] = x;
  pos[1] = y + 1;

  ioctl(fd, OLED_SET_XY, pos);

  for (t = 0; t < 16; t++) { // 显示下半截字符
    // oled_write_cmd_data(hz_1616[no][t*2+1],OLED_DATA);
    ioctl(fd, OLED_SET_DATAS | (1 << 8), &hz_1616[no][t * 2 + 1]);

    addr += 1;
  }
}

/*
 * 显示测试
 */
void OLED_DIsp_Test(void) {
  int i;

  OLED_DIsp_String(0, 0, "wiki.100ask.net");
  OLED_DIsp_String(0, 2, "book.100ask.net");
  OLED_DIsp_String(0, 4, "bbs.100ask.net");
  /* 显示汉字 */
  for (i = 0; i < 3; i++) {
    OLED_DIsp_CHinese(32 + i * 16, 6, i);
  }
}

/*
 * oledAPP /dev/myoled
 */

int main(int argc, char **argv) {
  int buf[2];

  if (argc != 2) {
    printf("Usage: %s <dev>\n", argv[0]);
    return -1;
  }

  fd = open(argv[1], O_RDWR);
  if (fd < 0) {
    printf(" can not open %s\n", argv[1]);
    return -1;
  }

  OLED_DIsp_Test();

  return 0;
}