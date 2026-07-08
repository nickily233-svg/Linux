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
    int fd, ret;
    int us;

    /* 1. 判断参数 */
    if (argc != 2)
    {
        printf("Usage: %s <dev>\n", argv[0]);
        close(fd);
        return -1;
    }

    /* 2. 打开文件 */
    // fd = open(argv[1], O_RDWR | O_NONBLOCK);
    fd = open(argv[1], O_RDWR);
    if (fd == -1)
    {
        printf("can not open file %s\n", argv[1]);
        return -1;
    }

    while (1)
    {
        if (read(fd, &us, 4) == 4)
        {
            printf("get distance : %d", us * 340 / 1000000 / 2);
        }
        else
        {
            printf("get distance : -1\n");
        }
    }
    close(fd);

    return 0;
}