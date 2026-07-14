#include <fcntl.h> // 提供 open 和 O_RDWR
#include <linux/input.h>
#include <stdio.h>  // 提供 printf
#include <stdlib.h> // 提供标准库功能
#include <string.h> // 提供字符串操作
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h> // 提供 read, write, close

int main(int argc, char **argv) {
  // unsigned int data[4];
  int fd;
  struct input_event data;

  if (argc != 2) {
    printf("use error\n");
    return -1;
  }

  fd = open(argv[1], O_RDWR);
  if (fd == -1) {
    printf("can not open file %s\n", argv[1]);
    return -1;
  }

  while (1) {
    if (read(fd, &data, 4) == 4) {
      // printf("get IR code : 0x%x\n", data);
      printf(" Type : 0x%x\n", data.type);
      printf(" code : 0x%x\n", data.code);
      printf(" value : 0x%x\n", data.value);
    } else {
      printf("get IR code : -1\n");
    }
  }

  close(fd);
  return 0;
}