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
 *./chedevbaseAPP <filename> <1:2> 1表示读 2表示写
 *./chedevbaseAPP /dev/chrdevbase 1 表示从驱动里面读
 *./chedevbaseAPP /dev/chrdevbase 2 表示从驱动里面写
 */
int main(int argc, char *argv[])
{
    /* 文件标识符 */
    /* 是文件打开后的编号，如果open函数打开成功，内核返回给用户程序的“整数句柄”，以后对文件进行操作都依靠fd */
    int fd;
    int retvalue;
    char *filename;
    char readbuf[100];
    char writebuf[100];
    static char usrdata[] = {"usr data!"};

    if (argc != 3)
    {
        printf("Error usage!\r\n");
        return -1;
    }

    filename = argv[1];
    /* open */
    /* 打开设备文件;本质上不是“普通文件读写”，而是通过/dev下的设备节点去访问驱动 */
    fd = open(filename, O_RDWR);
    if (fd < 0)
    {
        printf("Can't open file %s\r\n", filename);
        return -1;
    }

    /* read */
    /* atoi函数将字符串转换为整数 */
    if (atoi(argv[2]) == 1)
    {
        memset(readbuf, 0, sizeof(readbuf));
        retvalue = read(fd, readbuf, 50);
        if (retvalue < 0)
        {
            printf("File %s read error\r\n", filename);
            return -1;
        }
        else
        {
            printf("APP read data %s\r\n", readbuf);
        }
    }

    /* write */
    if (atoi(argv[2]) == 2)
    {
        memset(writebuf, 0, sizeof(writebuf));
        memcpy(writebuf, usrdata, sizeof(usrdata));
        retvalue = write(fd, writebuf, sizeof(usrdata));
        if (retvalue < 0)
        {
            printf("Write file %s error\r\n", filename);
            return -1;
        }
        else
        {
            printf("APP write data %s\r\n", writebuf);
        }
    }

    /* close */
    retvalue = close(fd);
    if (retvalue < 0)
    {
        printf("Close file %s error\r\n", filename);
    }

    return 0;
}
