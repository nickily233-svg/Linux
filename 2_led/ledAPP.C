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
 *./ledAPP <filename> <0:1> 0表示开灯 1表示关灯
 *./ledAPP /dev/led 0表示开灯
 *./ledAPP /dev/led 1表示关灯
 */
int main(int argc, char *argv[])
{
    int fd, retvalue;
    char databuf[2];
    char ledstate;
    char *filename;

    if (argc != 3)
    {
        printf("Error usage!\r\n");
        return -1;
    }

    filename = argv[1];

    /* open */
    fd = open(filename, O_RDWR);
    if (fd < 0)
    {
        printf("Open file %s error\r\n", argv[1]);
        return -1;
    }
    /* write */
    databuf[0] = atoi(argv[2]);

    retvalue = write(fd, databuf, 1);
    if (retvalue < 0)
    {
        printf("LED control error!\r\n");
        close(fd);
        return -1;
    }

    /* read */
    retvalue = read(fd, &ledstate, 1);

    if (retvalue < 0)
    {
        printf("Read file %s error\r\n", argv[1]);
    }

    retvalue = close(fd);
    if (retvalue < 0)
    {
        printf("Close file %s error!\r\n", argv[1]);
    }
    return 0;
}