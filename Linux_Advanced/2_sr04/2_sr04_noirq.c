#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/pid.h>

struct sr04_dev
{
    dev_t sr04_devid;
    struct class *sr04_class;
    struct device *sr04_device;
    int major;
    struct gpio_desc *trig_gpio;
    struct gpio_desc *echo_gpio;
    int irq;
};

static struct sr04_dev sr04dev;

static int sr04_open(struct inode *node, struct file *filp)
{
    return 0;
}

static ssize_t sr04_read(struct file *filp, char __user *buf, size_t count, loff_t *offt)
{
    int us = 0;
    unsigned long flags;
    int timeout_us = 100000;
    /* 关中断 */
    local_irq_save(flags);
    gpiod_set_value(sr04dev.trig_gpio, 1);
    udelay(15);
    gpiod_set_value(sr04dev.trig_gpio, 0);

    while (!gpiod_get_value(sr04dev.echo_gpio) && timeout_us--)
    {
        udelay(1);
    }

    if (!timeout_us)
    {
        local_irq_restore(flags);
        return -EAGAIN;
    }

    while (gpiod_get_value(sr04dev.echo_gpio) && timeout_us)
    {
        udelay(1);
        us++;
        timeout_us--;
    }

    if (!timeout_us)
    {
        local_irq_restore(flags);
        return -EAGAIN;
    }

    /* 开中断 */
    local_irq_restore(flags);

    if (copy_to_user(buf, &us, 4))
    {
        return -EFAULT;
    }
    return 4;
}

static int sr04_probe(struct platform_device *pdev)
{
    /* 1. 获取设备GPIO */
    sr04dev.trig_gpio = gpiod_get(&pdev->dev, "trig-gpio", GPIOD_OUT_HIGH);
    if (IS_ERR(sr04dev.trig_gpio))
    {
        return PTR_ERR(sr04dev.trig_gpio);
    }
    sr04dev.echo_gpio = gpiod_get(&pdev->dev, "echo-gpio", GPIOD_IN);
    if (IS_ERR(sr04dev.echo_gpio))
    {
        return PTR_ERR(sr04dev.echo_gpio);
    }

    return 0;
}

static int sr04_remove(struct platform_device *pdev)
{
    return 0;
}

static const struct of_device_id alientek_sr04_tanble[] =
    {
        {.compatible = "alientek,sr04"},
        {/*sentinel*/},
};

static struct platform_driver sr04_driver =
    {
        .probe = sr04_probe,
        .remove = sr04_remove,
        .driver = {
            .name = "sr04",
            .of_match_table = alientek_sr04_tanble,
        },
};

static struct file_operations sr04_fops = {
    .owner = THIS_MODULE,
    .open = sr04_open,
    .read = sr04_read,
};

static int sr04_init(void)
{
    printk("SR04 驱动加载成功\n");
    /* 1. 注册字符设备 */
    sr04dev.major = register_chrdev(sr04dev.major, "sr04", &sr04_fops);
    if (sr04dev.major < 0)
    {
        printk("注册字符设备失败\n");
        return -ENOMEM;
    }
    /* 2. 创建设备类 */
    sr04dev.sr04_class = class_create(THIS_MODULE, "class_sr04");
    if (IS_ERR(sr04dev.sr04_class))
    {
        printk("创建设备类失败\n");
        return PTR_ERR(sr04dev.sr04_class);
    }
    /* 3. 创建设备 */
    sr04dev.sr04_device = device_create(sr04dev.sr04_class, NULL, MKDEV(sr04dev.major, 0), NULL, "device_sr04");
    if (IS_ERR(sr04dev.sr04_device))
    {
        printk("创建设备失败\n");
        return PTR_ERR(sr04dev.sr04_device);
    }

    platform_driver_register(&sr04_driver);

    return 0;
}

static void sr04_exit(void)
{
    platform_driver_unregister(&sr04_driver);
    device_destroy(sr04dev.sr04_class, MKDEV(sr04dev.major, 0));
    class_destroy(sr04dev.sr04_class);
    unregister_chrdev(sr04dev.major, "sr04");
    gpiod_put(sr04dev.echo_gpio);
    gpiod_put(sr04dev.trig_gpio);
    printk("SR04 驱动卸载成功\n");
}

module_init(sr04_init);
module_exit(sr04_exit);

MODULE_LICENSE("GPL");