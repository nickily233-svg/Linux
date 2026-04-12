#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/*
 *argc:应用程序参数个数
 *argv[]:具体的参数内容，字符串形式
 *./beepAPP <filename> <0:1> 1表示开蜂鸣器 0表示关蜂鸣器
 *./beepAPP /dev/beepAPP 1表示开蜂鸣器
 *./beepAPP /dev/beepAPP 0表示关蜂鸣器
 */
int main(int argc, char *argv[])
{
    int fd, retvalue;
    char *filename;
    char databuf[1];
    int beepstate;
    unsigned int cnt;

    if (argc != 3)
    {
        printf("Error usage\r\n");
        return -1;
    }

    filename = argv[1];

    // open
    fd = open(filename, O_RDWR);
    if (fd < 0)
    {
        printf("open file %s failed\r\n", argv[1]);
        return -1;
    }
    // read
    retvalue = read(fd, &beepstate, 1);
    if (retvalue < 0)
    {
        printf("read file %s failed\r\n", argv[1]);
        return -1;
    }
    // write
    databuf[0] = atoi(argv[2]);
    retvalue = write(fd, databuf, 1);
    if (retvalue < 0)
    {
        printf("write file %s failed\r\n", argv[1]);
        close(fd);
        return -1;
    }
    while (1)
    {
        sleep(1);
        cnt++;
        printf("APP running times : %d\r\n", cnt);
        if (cnt > 5)
            break;
    }

    printf("APP running finished\r\n");

    // close
    retvalue = close(fd);
    if (retvalue < 0)
    {
        printf("close file %s failed\r\n", argv[1]);
        close(fd);
        return -1;
    }
    return 0;
}