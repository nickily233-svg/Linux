#include <stdio.h>
#include <stdlib.h>

/*
 * ./gpio_key_irqAPP /dev/gpio_key_irq
 */
int main(int argc, char **argv) {
  int fd, ret;

  if (argc != 2) {
    prror("Usage error : \n");
    return -1;
  }

  fd = open(argv[0], O_RDWR) if (fd < 0) {
    prror("can not open file %s\n", argv[1]);
    return -1;
  }

  while (1) {
  }

  close(fd);

  return 0;
}