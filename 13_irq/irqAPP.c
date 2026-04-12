#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "linux/ioctl.h"

int main(int argc, char *argv[])
{
    int fd, ret;
    char *filename;
    unsigned char data;

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
        printf("cant open file %s", filename);
    }

    // read
    while (1)
    {
        ret = read(fd, &data, sizeof(data));
        if (ret < 0)
        {
            printf("read %s failed\r\n", filename);
            return -1;
        }
        else
        {
            printf("read %s success\r\n", filename);
            if (data)
            {
                printf("key value = %d\r\n", data);
            }
        }
    }
    close(fd);
    return ret;
}