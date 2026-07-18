#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * lcdAPP /dev/mylcd <val>
 */

int main(int argc, char **argv) {
  int fd;
  unsigned short lcd_val = 0;

  if (argc != 3) {
    printf("Usage: %s <dev> <val>\n", argv[0]);
    return -1;
  }

  /* open返回字符描述符，以后操作fd就相当于在操作驱动程序 */
  fd = open(argv[1], O_RDWR);
  if (fd < 0) {
    printf(" can not open %s\n", argv[1]);
    return -1;
  }

  /* String to Unsigned Long（字符串转无符号长整型） */
  lcd_val = strtoul(argv[2], NULL, 0);

  while (1) {
    /* 向文件描述符写入数据 */
    /*
     *fd 文件描述符 要发送的数据 数据的大小
     */
    write(fd, &lcd_val, 2);
    lcd_val += 50;

    usleep(50000); /* 延时 50ms，让 lcd 慢慢输出变化 */
#if 0
    /* 做 12位 lcd 溢出归零（如果想让其在0~4095循环锯齿波） */
    if (lcd_val >= 4095) {
      lcd_val = 0;
    }
#endif
  }
  close(fd);

  return 0;
}