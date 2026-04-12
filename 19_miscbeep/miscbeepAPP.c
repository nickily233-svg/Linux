#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

#define beepoff 1 /* 关蜂鸣器 */
#define beepon 0  /* 开蜂鸣器 */

int main(int argc, char *argv[])
{
    int fd, ret;
    char *filename;
    unsigned char databuf[1];

    if (argc != 3)
    {
        printf("Error usage\r\n");
        return -1;
    }

    filename = argv[1];
    fd = open(filename, O_RDWR);
    if (fd < 0)
    {
        printf("cant open file %s\r\n", argv[1]);
        return -1;
    }

    databuf[0] = atoi(argv[2]);
    ret = write(fd, databuf, sizeof(databuf));
    if (ret < 0)
    {
        printf("write data failed\r\n");
        return -1;
    }

    ret = close(fd);
    if (ret < 0)
    {
        printf("close file %s failed\r\n");
        return -1;
    }

    return 0;
}