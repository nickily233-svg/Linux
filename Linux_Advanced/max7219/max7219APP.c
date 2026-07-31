#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
  int fd;
  int ret;
  unsigned char display_data[8] = {0x7E, 0x30, 0x6D, 0x79,
                                   0x33, 0x5B, 0x5F, 0x70}; // 显示 0~7

  if (argc != 2) {
    printf("Usage Error : %s <dev>\n", argv[0]);
    return -1;
  }

  fd = open(argv[1], O_RDWR);
  if (fd < 0) {
    printf("can not open dev %s", argv[1]);
    return -1;
  }

  write(fd, display_data, 8);

  close(fd);

  return 0;
}