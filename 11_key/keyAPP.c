#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#define KEY0VALUE 0XF0
#define INVAKEY 0X00

/*
 *argc:应用程序参数个数
 *argv[]:具体的参数内容，字符串形式
 *./keyAPP <filename> <0:1> 1表示开蜂鸣器 0表示关蜂鸣器
 *./keyAPP /dev/key /dev/led
 *./keyAPP /dev/key /dev/led
 */
int main(int argc, char *argv[])
{
    int keyfd, ledfd, ret;
    char *keyfilename, *ledfilename;
    char databuf[1];
    unsigned char keyvalue;
    unsigned char ledstate = 0;

    if (argc != 3)
    {
        printf("Error usage\r\n");
        return -1;
    }

    keyfilename = argv[1];
    ledfilename = argv[2];

    // open
    keyfd = open(keyfilename, O_RDWR);
    ledfd = open(ledfilename, O_RDWR);
    if ((keyfd < 0 || ledfd < 0))
    {
        printf("cant open file\r\n");
        return -1;
    }

    // read
    while (1)
    {
        if (read(keyfd, &keyvalue, 1) == 1)
        {
            if (keyvalue == KEY0VALUE)
            {
                ledstate = !ledstate;
                if (write(ledfd, &ledstate, 1) != 1)
                {
                    printf("write failed\r\n");
                    break;
                }
                printf("key0 pressed ledstate = %d\r\n", ledstate);
                usleep(200000);
            }
        }
    }

    // write

    // release
    close(keyfd);
    close(ledfd);

    return 0;
}