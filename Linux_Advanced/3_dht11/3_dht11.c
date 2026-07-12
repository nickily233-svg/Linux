/*
  Linux中断延迟，运行时，如果开启了很多其他的驱动，系统的软中断和硬中断响应会有微秒级的随机抖动
  一旦某个边沿的中断延迟被延迟了10秒，ktime_get_boot_ns()时序就会错位，导致解析出来的所有数据全错
  用中断抓取时间很短的短脉冲，是不推荐的
*/

/* 使用中断的思路就是：
1. 用户层触发（APP 调用 read）：
APP 执行 read(fd, data, 5)。
内核调用 dht11_read。
驱动发送 DHT11 启动信号（拉低 20ms 再拉高）。
调用 wait_event_timeout(dht11dev.dht11_wq, dht11dev.dht11_data, HZ)，APP 进程进入休眠（挂起），让出 CPU，等待硬件信号。
2. 硬件层抓取（中断服务函数 dht11_isq 执行）：
DHT11 响应后，会在数据线上产生 40 个脉冲（为了表示 40 位数据）。
每次电平跳变（上升沿或下降沿）都会触发一次中断，执行 dht11_isq。
您在中断里做的事：调用 ktime_get_boot_ns() 记下当前的纳秒时间戳，存进 dht11_edge_time 数组里，并把记录数 dht11_edge_cnt++。
触发唤醒：当计数器 cnt 累加到 80（40 个位 × 2 个边沿 = 80 次中断）时，说明一帧数据采集完毕。代码设置 dht11dev.dht11_data = 1，并调用 wake_up。
数据解析与返回（dht11_parse_data 运行）：
之前休眠的 APP 被 wake_up 唤醒，wait_event_timeout 返回，程序继续往下走。
调用 dht11_parse_data(data)，在完全脱离中断的上下文中，慢慢去计算 edge_time 数组里相邻两个时间戳的差值。
算出高低电平的时长，拼成字节，检查校验和，最后通过 copy_to_user 把 5 个字节传给 APP。
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>

struct dht11_dev
{
    dev_t dht11_devid;
    struct class *dht11_class;
    struct device *dht11_device;
    int dht11_irq;
    struct gpio_desc *dht11_gpio;
    int major;
    wait_queue_head_t dht11_wq;
    int dht11_edge_cnt;
    int dht11_data;
    int dht11_edge_time[100];
};

struct dht11_dev dht11dev;

static void dht11_reset(void)
{
    gpiod_direction_output(dht11dev.dht11_gpio, 1);
}

static void dht11_start(void)
{
    /* 建立通信 */
    mdelay(30);
    gpiod_set_value(dht11dev.dht11_gpio, 0);
    mdelay(20);
    gpiod_set_value(dht11dev.dht11_gpio, 1);
    udelay(30);
    gpiod_direction_input(dht11dev.dht11_gpio);
    udelay(2);
    /* 初始设置为输入引脚 */
}

static int dht11_wait_for_ready(void)
{
    int timeout_us = 200;
    /* 等待低电平 */
    while (gpiod_get_value(dht11dev.dht11_gpio) && --timeout_us)
    {
        udelay(1);
    }
    if (!timeout_us)
    {
        printk("%s %s line %d \n", __FILE__, __FUNCTION__, __LINE__);
        return -1;
    }

    /* 现在为低电平，等待高电平 */
    timeout_us = 200;
    while (!gpiod_get_value(dht11dev.dht11_gpio) && --timeout_us)
    {
        udelay(1);
    }
    if (!timeout_us)
    {
        return -1;
    }

    /* 现在是高电平，等待低电平 */
    timeout_us = 200;
    while (gpiod_get_value(dht11dev.dht11_gpio) && --timeout_us)
    {
        udelay(1);
    }
    if (!timeout_us)
    {
        return -1;
    }

    return 0;
}

static irqreturn_t dht11_isq(int irq, void *dev_id)
{
    /* 1. 记录时间 */
    dht11dev.dht11_edge_time[dht11dev.dht11_edge_cnt++] = ktime_get_boot_ns();
    if (dht11dev.dht11_edge_cnt >= 80)
    {
        /* 2. 唤醒APP：去同一个链表把APP唤醒 */
        dht11dev.dht11_data = 1;
        wake_up(&dht11dev.dht11_wq);
    }

    return IRQ_HANDLED;
}

#if 0
static int dhtr11_read_byte(unsigned char *buf)
{
    int i;
    unsigned char data = 0;
    int timeout_us = 200;
    int us = 0;

    /* 循环8bit */
    for (i = 0; i < 8; i++)
    {
        /* 现在为低电平，等待高电平 */
        while (!gpiod_get_value(dht11dev.dht11_gpio) && --timeout_us)
        {
            udelay(1);
        }
        /* 超时 */
        if (!timeout_us)
        {
            return -1;
        }

        /* 现在是高电平，等待低电平，累加高电平的时间 */
        timeout_us = 200;
        us = 0;
        while (gpiod_get_value(dht11dev.dht11_gpio) && --timeout_us)
        {
            udelay(1);
            us++;
        }
        /* 超时 */
        if (!timeout_us)
        {
            return -1;
        }

        /* 根据us的长度，判断高低电平 > 40 = bit 1
                                    < 40 = bit 0 */
        if (us > 40)
        {
            /* get bit 1 */
            data = (data << 1) | 1;
        }
        else
        {
            /*get bit 0*/
            data = (data << 1) | 0;
        }
    }

    *buf = data;

    return 0;
}
#endif

static int dht11_parse_data(char *data)
{
    int i, j, m;
    for (i = 0; i < 5; i++)
    {
        data[i] = 0;
        for (j = 0; j < 8; j++)
        {
            /* 高电平持续时间判断 */
            if (dht11dev.dht11_edge_time[m + 2] - dht11dev.dht11_edge_time[m + 1] >= 40000)
            {
                data[i] = (data[i] << 1) | 1;
                m += 2;
            }
            else
            {
                data[i] = (data[i] << 1) | 0;
            }
        }
    }

    /* 4. 根据校验位验证数据  */
    if (data[4] != data[0] + data[1] + data[2] + data[3])
    {
        printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
        return -1;
    }
    else
    {
        return 0;
    }
}

static ssize_t dht11_read(struct file *filep, char __user *buf, size_t count, loff_t *offt)
{
    unsigned long flags;
    unsigned char data[5];
    int i;
    int ret;
    int timeout;

    if (count != 5)
    {
        return -EINVAL;
    }

    // local_irq_save(flags);
    /* 1. 发送高脉冲启动DHT11 */
    dht11_reset();
    dht11_start();
    /* 2. 等待DHT11就绪 */
    if (dht11_wait_for_ready())
    {
        // local_irq_restore(flags);
        printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
        dht11_reset();
        return -EAGAIN;
    }

    dht11dev.dht11_edge_cnt = 0;
    dht11dev.dht11_data = 0;

    timeout = wait_event_timeout(dht11dev.dht11_wq, dht11dev.dht11_data, HZ);

    dht11_reset();

    if (!timeout)
    {
        return -ETIMEDOUT;
    }

    if (!dht11_parse_data(data))
    {
        ret = copy_to_user(buf, data, sizeof(data));
        if (ret)
        {
            return -EAGAIN;
        }

        dht11_reset();

        /* 返回读取的值 */
        return 5;
    }

    // /* 3. 读8字节数据 */
    // for (i = 0; i < 5; i++)
    // {
    //     if (dhtr11_read_byte(&data[i]))
    //     {
    //         local_irq_restore(flags);
    //         return -EAGAIN;
    //     }
    // }
    // local_irq_save(flags);

    /* 5. copy_to_user */
    /* data[0]/data[1] : 湿度*/
    /* data[2]/data[3] : 温度*/
}

int dht11_probe(struct platform_device *pdev)
{
    int ret;
    /* 1. 获取设备GPIO资源 */
    dht11dev.dht11_gpio = gpiod_get(&pdev->dev, "data-gpio", GPIOD_OUT_HIGH);
    if (IS_ERR(dht11dev.dht11_gpio))
    {
        printk("can not get data-gpio\n");
        ret = PTR_ERR(dht11dev.dht11_gpio);
        return ret;
    }

    /* 2. 获取终端号并且申请中断 */
    dht11dev.dht11_irq = gpiod_to_irq(dht11dev.dht11_gpio);
    ret = request_irq(dht11dev.dht11_irq, dht11_isq, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "irq_dht11", NULL);
    if (ret < 0)
    {
        printk("can not request irq\n");
        return ret;
    }

    /* 3. 创建设备 */
    device_create(dht11dev.dht11_class, NULL, MKDEV(dht11dev.major, 0), NULL, "device_dht11");
    if (IS_ERR(dht11dev.dht11_device))
    {
        return PTR_ERR(dht11dev.dht11_device);
    }

    return 0;
}

int dht11_remove(struct platform_device *pdev)
{
    device_destroy(dht11dev.dht11_class, MKDEV(dht11dev.major, 0));
    class_destroy(dht11dev.dht11_class);
    unregister_chrdev(dht11dev.major, "dht11_device");

    // free_irq(dht11dev.dht11_irq, NULL);
    gpiod_put(dht11dev.dht11_gpio);
    return 0;
}
static struct of_device_id dht11_match_table[] =
    {
        {.compatible = "alientek,dht11"},
        {/* sentinel */},
};

static struct platform_driver dht11_driver =
    {
        .probe = dht11_probe,
        .remove = dht11_remove,
        .driver = {
            .name = "dht11",
            .of_match_table = dht11_match_table,
        },
};

static struct file_operations dht11_fops =
    {
        .owner = THIS_MODULE,
        .read = dht11_read,
};

static int dht11_init(void)
{
    int err;
    /* 1. 注册字符设备 */
    dht11dev.major = register_chrdev(dht11dev.major, "dht11_device", &dht11_fops);
    if (dht11dev.major < 0)
    {
        printk("can not register chardev\n");
        return -ENOMEM;
    }
    /* 2. 创建设备类 */
    dht11dev.dht11_class = class_create(THIS_MODULE, "class_dht11");
    if (IS_ERR(dht11dev.dht11_class))
    {
        printk("can not create class\n");
        unregister_chrdev(dht11dev.major, "dht11_device");
        return PTR_ERR(dht11dev.dht11_class);
    }

    init_waitqueue_head(&dht11dev.dht11_wq);

    err = platform_driver_register(&dht11_driver);

    return err;
}

static void dht11_exit(void)
{
    platform_driver_unregister(&dht11_driver);
}

module_init(dht11_init);
module_exit(dht11_exit);

MODULE_LICENSE("GPL");