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

struct ds18b20_dev
{
    dev_t ds18b20_devid;
    struct class *ds18b20_class;
    struct device *ds18b20_device;
    int ds18b20_irq;
    struct gpio_desc *ds18b20_gpio;
    int major;
    wait_queue_head_t ds18b20_wq;
    int ds18b20_edge_cnt;
    int ds18b20_data;
    int ds18b20_edge_time[100];
};

struct ds18b20_dev ds18b20dev;

static void ds18b20_reset(void)
{
    gpiod_direction_output(ds18b20dev.ds18b20_gpio, 1);
}

static void ds18b20_start(void)
{
    /* 建立通信 */
    mdelay(30);
    gpiod_set_value(ds18b20dev.ds18b20_gpio, 0);
    mdelay(20);
    gpiod_set_value(ds18b20dev.ds18b20_gpio, 1);
    udelay(30);
    gpiod_direction_input(ds18b20dev.ds18b20_gpio);
    udelay(2);
    /* 初始设置为输入引脚 */
}

static int ds18b20_wait_for_ready(void)
{
    int timeout_us = 200;
    /* 等待低电平 */
    while (gpiod_get_value(ds18b20dev.ds18b20_gpio) && --timeout_us)
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
    while (!gpiod_get_value(ds18b20dev.ds18b20_gpio) && --timeout_us)
    {
        udelay(1);
    }
    if (!timeout_us)
    {
        return -1;
    }

    /* 现在是高电平，等待低电平 */
    timeout_us = 200;
    while (gpiod_get_value(ds18b20dev.ds18b20_gpio) && --timeout_us)
    {
        udelay(1);
    }
    if (!timeout_us)
    {
        return -1;
    }

    return 0;
}

static irqreturn_t ds18b20_isq(int irq, void *dev_id)
{
    /* 1. 记录时间 */
    ds18b20dev.ds18b20_edge_time[ds18b20dev.ds18b20_edge_cnt++] = ktime_get_boot_ns();
    if (ds18b20dev.ds18b20_edge_cnt >= 80)
    {
        /* 2. 唤醒APP：去同一个链表把APP唤醒 */
        ds18b20dev.ds18b20_data = 1;
        wake_up(&ds18b20dev.ds18b20_wq);
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
        while (!gpiod_get_value(ds18b20dev.ds18b20_gpio) && --timeout_us)
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
        while (gpiod_get_value(ds18b20dev.ds18b20_gpio) && --timeout_us)
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

static int ds18b20_parse_data(char *data)
{
    int i, j, m;
    for (i = 0; i < 5; i++)
    {
        data[i] = 0;
        for (j = 0; j < 8; j++)
        {
            /* 高电平持续时间判断 */
            if (ds18b20dev.ds18b20_edge_time[m + 2] - ds18b20dev.ds18b20_edge_time[m + 1] >= 40000)
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

static ssize_t ds18b20_read(struct file *filep, char __user *buf, size_t count, loff_t *offt)
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
    /* 1. 发送高脉冲启动ds18b20 */
    ds18b20_reset();
    ds18b20_start();
    /* 2. 等待ds18b20就绪 */
    if (ds18b20_wait_for_ready())
    {
        // local_irq_restore(flags);
        printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
        ds18b20_reset();
        return -EAGAIN;
    }

    ds18b20dev.ds18b20_edge_cnt = 0;
    ds18b20dev.ds18b20_data = 0;

    timeout = wait_event_timeout(ds18b20dev.ds18b20_wq, ds18b20dev.ds18b20_data, HZ);

    ds18b20_reset();

    if (!timeout)
    {
        return -ETIMEDOUT;
    }

    if (!ds18b20_parse_data(data))
    {
        ret = copy_to_user(buf, data, sizeof(data));
        if (ret)
        {
            return -EAGAIN;
        }

        ds18b20_reset();

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

int ds18b20_probe(struct platform_device *pdev)
{
    int ret;
    /* 1. 获取设备GPIO资源 */
    ds18b20dev.ds18b20_gpio = gpiod_get(&pdev->dev, "data-gpio", GPIOD_OUT_HIGH);
    if (IS_ERR(ds18b20dev.ds18b20_gpio))
    {
        printk("can not get data-gpio\n");
        ret = PTR_ERR(ds18b20dev.ds18b20_gpio);
        return ret;
    }

    /* 2. 获取终端号并且申请中断 */
    ds18b20dev.ds18b20_irq = gpiod_to_irq(ds18b20dev.ds18b20_gpio);
    ret = request_irq(ds18b20dev.ds18b20_irq, ds18b20_isq, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "irq_ds18b20", NULL);
    if (ret < 0)
    {
        printk("can not request irq\n");
        return ret;
    }

    /* 3. 创建设备 */
    device_create(ds18b20dev.ds18b20_class, NULL, MKDEV(ds18b20dev.major, 0), NULL, "device_ds18b20");
    if (IS_ERR(ds18b20dev.ds18b20_device))
    {
        return PTR_ERR(ds18b20dev.ds18b20_device);
    }

    return 0;
}

int ds18b20_remove(struct platform_device *pdev)
{
    device_destroy(ds18b20dev.ds18b20_class, MKDEV(ds18b20dev.major, 0));
    class_destroy(ds18b20dev.ds18b20_class);
    unregister_chrdev(ds18b20dev.major, "ds18b20_device");

    // free_irq(ds18b20dev.ds18b20_irq, NULL);
    gpiod_put(ds18b20dev.ds18b20_gpio);
    return 0;
}
static struct of_device_id ds18b20_match_table[] =
    {
        {.compatible = "alientek,ds18b20"},
        {/* sentinel */},
};

static struct platform_driver ds18b20_driver =
    {
        .probe = ds18b20_probe,
        .remove = ds18b20_remove,
        .driver = {
            .name = "ds18b20",
            .of_match_table = ds18b20_match_table,
        },
};

static struct file_operations ds18b20_fops =
    {
        .owner = THIS_MODULE,
        .read = ds18b20_read,
};

static int ds18b20_init(void)
{
    int err;
    /* 1. 注册字符设备 */
    ds18b20dev.major = register_chrdev(ds18b20dev.major, "ds18b20_device", &ds18b20_fops);
    if (ds18b20dev.major < 0)
    {
        printk("can not register chardev\n");
        return -ENOMEM;
    }
    /* 2. 创建设备类 */
    ds18b20dev.ds18b20_class = class_create(THIS_MODULE, "class_ds18b20");
    if (IS_ERR(ds18b20dev.ds18b20_class))
    {
        printk("can not create class\n");
        unregister_chrdev(ds18b20dev.major, "ds18b20_device");
        return PTR_ERR(ds18b20dev.ds18b20_class);
    }

    init_waitqueue_head(&ds18b20dev.ds18b20_wq);

    err = platform_driver_register(&ds18b20_driver);

    return err;
}

static void ds18b20_exit(void)
{
    platform_driver_unregister(&ds18b20_driver);
}

module_init(ds18b20_init);
module_exit(ds18b20_exit);

MODULE_LICENSE("GPL");