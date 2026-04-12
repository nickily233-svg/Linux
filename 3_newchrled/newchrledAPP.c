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
 *./chrledAPP <filename> <0:1> 0表示开灯 1表示关灯
 *./chrledAPP /dev/chrled 0表示开灯
 *./chrledAPP /dev/chrled 1表示关灯
 */
int main(int argc, char *argv[])
{
    int fd, retvalue;
    char *filename;
    char databuf[1];

    if (argc != 3)
    {
        printf("Error usage!\r\n");
        return -1;
    }

    filename = argv[1];

    databuf[0] = atoi(argv[2]);

    /* open */
    fd = open(filename, O_RDWR);
    if (fd < 0)
    {
        printf("can't open file %s !\r\n", filename);
        return -1;
    }

    /* write */
    retvalue = write(fd, databuf, 1);
    if (retvalue < 0)
    {
        printf("Write file %s failed!\r\n", filename);
        close(fd);
        return -1;
    }

    /* read */
    retvalue = read(fd, databuf, 1);
    if (retvalue < 0)
    {
        printf("Read file %s failed!\r\n", filename);
        return -1;
    }
    else
    {
        printf("led state = %d!\r\n", databuf[0]);
    }

    /* release */
    retvalue = close(fd);
    if (retvalue < 0)
    {
        printf("Release file %s failed!\r\n", filename);
        return -1;
    }
    return 0;
}