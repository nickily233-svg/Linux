#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

struct dht11_dev
{
    dev_t dht11_devid;
    struct class *dht11_class;
    struct device *dht11_device;
    int dht11_irq;
    struct gpio_desc *dht11_gpio;
    int major;
    wait_queue_head_t dht11_wq;
};

struct dht11_dev dht11dev;

static void dht11_start(void)
{
    /* 建立通信 */
    gpiod_direction_output(dht11dev.dht11_gpio, GPIOD_OUT_HIGH);
    udelay(2);
    gpiod_set_value(dht11dev.dht11_gpio, 0);
    mdelay(20);
    gpiod_set_value(dht11dev.dht11_gpio, 1);
    udelay(30);
    gpiod_direction_input(dht11dev.dht11_gpio);
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

static ssize_t dht11_read(struct file *filep, char __user *buf, size_t count, loff_t *offt)
{
    unsigned long flags;
    unsigned char data[5];
    int i;
    int ret;

    if (count != 5)
    {
        return -EINVAL;
    }

    local_irq_save(flags);
    /* 1. 发送高脉冲启动DHT11 */
    dht11_start();
    /* 2. 等待DHT11就绪 */
    if (dht11_wait_for_ready())
    {
        local_irq_restore(flags);
        return -EAGAIN;
    }
    /* 3. 读8字节数据 */
    for (i = 0; i < 5; i++)
    {
        if (dhtr11_read_byte(&data[i]))
        {
            local_irq_restore(flags);
            return -EAGAIN;
        }
    }
    local_irq_save(flags);
    /* 4. 根据校验位验证数据  */
    if (data[4] != data[0] + data[1] + data[2] + data[3])
    {
        printk("check err\n");
        return -1;
    }
    /* 5. copy_to_user */
    /* data[0]/data[1] : 湿度*/
    /* data[2]/data[3] : 温度*/
    ret = copy_to_user(buf, data, sizeof(data));
    if (ret)
    {
        return -EFAULT;
    }

    /* 返回读取的值 */
    return 5;
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
    // dht11dev.dht11_irq = gpiod_to_irq(dht11dev.dht11_gpio);
    // ret = request_irq(dht11dev.dht11_irq, dht11_isq, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "irq_dht11", NULL);
    // if (ret < 0)
    // {
    //     printk("can not request irq\n");
    //     return ret;
    // }

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