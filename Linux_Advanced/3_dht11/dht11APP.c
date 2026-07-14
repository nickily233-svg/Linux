#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

int main(int argc, char **argv)
{
    int fd;
    int data[4];

    if (argc != 2)
    {
        printf("usage error\n");
        return -1;
    }

    fd = open(argv[1], O_RDWR);
    if (fd == -1)
    {
        printf("can not open dev %s\n", argv[0]);
        return -1;
    }

    while (1)
    {
        if (read(fd, data, 4) == 4)
        {
            printf("get humdity : %d\n", data[0]);
            printf("get tempature : %d\n", data[2]);
            sleep(1);
        }
        else
        {
            printf("get humdity -1");
            printf("get tempature -1");
        }
    }

    close(fd);

    return 0;
}