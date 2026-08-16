#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * ./uartAPP /dev/tty
 */
int main(int argc, char **argv) {
  int fd;
  int ret;

  if (argc != 2) {
    printf("Usage Error : %s <dev>\n", argv[1]);
    return -1;
  }

  fd = open(argv[1], O_RDWR);
  if (fd < 0) {
    printf("can not open dev\n");
    return -1;
  }

  close(fd);

  return 0;
}