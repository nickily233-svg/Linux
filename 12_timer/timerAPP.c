#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "linux/ioctl.h"

/* 命令值 */
#define CLOSE_CMD (_IO(0XEF, 0x1))     /* 关闭定时器 */
#define OPEN_CMD (_IO(0XEF, 0x2))      /* 打开定时器 */
#define SETPERIOD_CMD (_IO(0XEF, 0x3)) /* 设置定时器周期命令 */

int main(int argc, char *argv[])
{
    int fd, ret;
    char *filename;
    unsigned int cmd;
    unsigned int arg;
    unsigned char str[100];
    int ch;

    if (argc != 2)
    {
        printf("Error usage\r\n");
        return -1;
    }

    filename = argv[1];

    // open
    fd = open(filename, O_RDWR);
    if (fd < 0)
    {
        printf("cant open file %s\r\n", filename);
        return -1;
    }

    while (1)
    {
        /* code */
        printf("input cmd\r\n");
        ret = scanf("%u", &cmd);
        if (ret == 0)
        {
            while ((ch = getchar()) != '\n' && ch != EOF)
                ;
        }
        if (cmd == 1)
        {
            cmd = CLOSE_CMD;
        }
        else if (cmd == 2)
        {
            cmd = OPEN_CMD;
        }
        else if (cmd == 3)
        {
            cmd = SETPERIOD_CMD;
            printf("input timer period\r\n");
            ret = scanf("%u", &arg);
            if (ret == 0)
            {
                while ((ch = getchar()) != '\n' && ch != EOF)
                    ;
            }
        }
        ioctl(fd, cmd, arg);
    }
    close(fd);
}