#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define BMP280_READ_P _IOR('b', 1, int32_t)
#define BMP280_READ_T _IOR('b', 2, int32_t)

/*
 * ./bmp280APP /dev/bmp280_dev T
 * ./bmp280APP /dev/bmp280_dev P
 */
int main(int argc, char **argv) {
  int fd;
  int ret;
  int pressure;
  int temperature;

  if (argc != 3) {
    printf("Usage error : %s  <dev> <command>\n", argv[0]);
    printf("Example: %s /dev/bmp280_device BMP280_READ_P\n", argv[0]);
    return -1;
  }

  fd = open(argv[1], O_RDWR);
  if (fd < 0) {
    printf("can not open file %s", argv[1]);
    return -1;
  }

  /* 2. 解析命令行参数，执行对应的 ioctl 命令 */
  if (strcmp(argv[2], "BMP280_READ_P") == 0) {
    if (ioctl(fd, BMP280_READ_P, &pressure) < 0) {
      printf("ioctl read pressure failed");
      close(fd);
      return -1;
    }
    printf("Pressure: %d Pa\n", pressure);
  } else if (strcmp(argv[2], "BMP280_READ_T") == 0) {
    if (ioctl(fd, BMP280_READ_T, &temperature) < 0) {
      printf("ioctl read temperature failed");
      close(fd);
      return -1;
    }
    /* 内核返回的温度是真实值的100倍（如 25.5℃ 返回
     2550），需要转换成带小数的摄氏度 */
    printf("Temperature: %d.%d C\n", temperature / 100, temperature % 100);
  } else {
    printf("Error : Unknown command '%s'\n", argv[2]);
    close(fd);
    return -1;
  }

  close(fd);

  return 0;
}

/*
 * ./bmp280APP /dev/bmp280_dev
 */
#if 0 
int main(int argc, char **argv) {
  int fd;
  int ret;
  int pressure;

  if (argc != 2) {
    printf("Usage error : %s  ", argv[0]);
    return -1;
  }

  fd = open(argv[1], O_RDWR);
  if (fd < 0) {
    printf("can not open file %s", argv[1]);
    return -1;
  }

  while (1) {
    ret = read(fd, &pressure, 4);
    if (ret != 4) {
      printf("Read error!\n");
      break;
    }
    printf("current pressure : %d Pa\n", pressure);
    sleep(1);
  }

  close(fd);

  return 0;
}
#endif