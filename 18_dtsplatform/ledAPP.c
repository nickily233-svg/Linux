#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"

#define ledon 0
#define ledoff 1

int main(int argc, char *argv[])
{
    int fd;
    int retvalue;
    char *filename;
    char databuf[2];

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
    retvalue = write(fd, databuf, sizeof(databuf));
    if (retvalue < 0)
    {
        printf("led control failed\r\n");
        close(fd);
        return -1;
    }

    retvalue = close(fd);
    if (retvalue < 0)
    {
        printf("cant close file %s", argv[1]);
        return -1;
    }
    return 0;
}