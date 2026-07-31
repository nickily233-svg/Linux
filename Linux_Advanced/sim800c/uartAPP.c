#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  int fd;

  if (argc != 2) {
    printf("Usage Error\n");
    return -1;
  }

  fd = open(argv[1], O_RDWR);
  if (fd < 0) {
    printf("can not open dev %s", argv[2]);
    return -1;
  }

  close(fd);

  return 0;
}