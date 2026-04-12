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
    int fd;
    int ret = 0;
    char *filename;
    unsigned char data;

    if (argc != 2)
    {
        printf("Error usager\r\n");
        return -1;
    }
    filename = argv[1];
    // open
    fd = open(filename, O_RDWR);
    if (fd < 0)
    {
        printf("cant open file %s\r\n", argv[1]);
        return -1;
    }
    // read
    while (1)
    {
        ret = read(fd, &data, sizeof(data));
        if (ret < 0)
        {
            printf("cant read file %s\r\n", argv[1]);
            return -1;
        }
        else
        {
        }
        if (data)
        {
            printf("key value : %#x\r\n", data);
        }
    }
    close(fd);
    return ret;
}