#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/time.h>
#include <linux/ioctl.h>
#include <signal.h>

static int fd = 0;

static void sigio_signale_func(int signum)
{
    int err = 0;
    unsigned int keyvalue = 0;

    err = read(fd, &keyvalue, sizeof(keyvalue));
    if (err < 0)
    {
        /*读取失败*/
    }
    else
    {
        printf("sigio signale ! key value = %d\r\n", keyvalue);
    }
}

int main(int argc, char *argv[])
{
    int flags = 0;
    char *filename;

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
    /*设置信号SIGIO的处理函数*/
    signal(SIGIO, sigio_signale_func);

    fcntl(fd, F_SETOWN, getpid());      /*将当前进程的进程号告诉内核*/
    flags = fcntl(fd, F_GETFL);         /*获取当前的进程状态*/
    fcntl(fd, F_SETFL, flags | FASYNC); /*设置进程启用异步通知功能*/

    while (1)
    {
        sleep(2);
    }
    close(fd);
    return 0;
}