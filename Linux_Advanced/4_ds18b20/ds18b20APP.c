#include <errno.h>
#include <fcntl.h>  // 提供 open 和 O_RDWR
#include <stdio.h>  // 提供 printf
#include <stdlib.h> // 提供标准库功能
#include <string.h> // 提供字符串操作
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h> // 提供 read, write, close

/*
./ds1820APP /dev/device_ds18b20
*/
int main(int argc, char **argv) {
  int fd;
  unsigned char data[5];
  unsigned int interger;
  unsigned char decimal;

  if (argc != 2) {
    printf("usage error\n");
    return -1;
  }

  fd = open(argv[1], O_RDWR);
  if (fd == -1) {
    printf("can not open dev %s\n", argv[0]);
    return -1;
  }

  while (1) {
    if (read(fd, data, 5) == 5) {
      interger = data[0] | data[1] << 8 | data[2] << 16 | data[3] << 24;
      decimal = data[4];
      printf("get tempature :%d.%d\n", interger, decimal);
    } else {
      printf("get tempature -1\n");
    }
  }

  close(fd);

  return 0;
}