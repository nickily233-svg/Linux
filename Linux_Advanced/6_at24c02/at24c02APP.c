
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define at24c02_read 1000
#define at24c02_write 1001

struct at24c02_data {
  unsigned char addr;
  unsigned char val;
};

/*
 * at24c02APP /dev/myat24c02 r 10
 * at24c02APP /dev/myat24c02 r 10 123
 */
int main(int argc, char **argv) {

  int fd;
  struct at24c02_data data;

  /* 1. 判断参数是否正确 */
  if ((argc != 4) && (argc != 5)) {
    fprintf(stderr, "Usage: %s <dev> r <addr>\n", argv[0]);
    fprintf(stderr, "       %s <dev> w <addr> <val>\n", argv[0]);
    return -1;
  }

  /* 2. open */
  fd = open(argv[1], O_RDWR);
  if (fd < 0) {
    perror("open device");
    return -1;
  }

  if (argv[2][0] == 'r') {
    /* 读操作 */
    data.addr = (unsigned char)strtoul(argv[3], NULL, 0);
    if (ioctl(fd, at24c02_read, &data) < 0) {
      perror("ioctl read");
      close(fd);
      return -1;
    }
    printf("Read addr 0x%02x: 0x%02x (%d)\n", data.addr, data.val, data.val);
  } else if (argv[2][0] == 'w') {
    // 写操作
    data.addr = (unsigned char)strtoul(argv[3], NULL, 0);
    data.val = (unsigned char)strtoul(argv[4], NULL, 0);
    if (ioctl(fd, at24c02_write, &data) < 0) {
      perror("ioctl write");
      close(fd);
      return -1;
    }
    printf("Write addr 0x%02x: 0x%02x (%d)\n", data.addr, data.val, data.val);
  } else {
    fprintf(stderr, "Unknown command: %c\n", argv[2][0]);
    close(fd);
    return -1;
  }

  /* 3. 关闭文件 */
  close(fd);

  return 0;
}