#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
#include "linux/ioctl.h"
#include "poll.h"
#include "sys/select.h"
#include "sys/time.h"

int main(int argc, char *argv[])
{
    int fd;
    int ret = 0;
    char *filename;
    unsigned char data;
    fd_set readfds;
    struct pollfd fds;
    struct timeval timeout;

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

#if 0
/* 构造结构体 */
fds.fd = fd;
fds.events = POLLIN;
while (1) {
ret = poll(&fds, 1, 500);
if (ret) { /* 数据有效 */
ret = read(fd, &data, sizeof(data));
if(ret < 0) {
/* 读取错误 */
} else {
if(data)
printf("key value = %d \r\n", data);
}
} else if (ret == 0) { /* 超时 */
/* 用户自定义超时处理 */
} else if (ret < 0) { /* 错误 */
/* 用户自定义错误处理 */
}
}
#endif
    // read
    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);

        /*构造超市时间*/
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000;
        ret = select(fd + 1, &readfds, NULL, NULL, &timeout);
        switch (ret)
        {
        case 0:
            break;
        case -1:
            break;
        default:
            if (FD_ISSET(fd, &readfds))
            {
                ret = read(fd, &data, sizeof(data));
                if (ret < 0)
                {
                    return -1;
                }
                else
                {
                    if (data)
                    {
                        printf("key value = %d\r\n", data);
                    }
                }
            }
            break;
        }
    }
    close(fd);
    return ret;
}