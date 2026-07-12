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
    int sr04_irq;
    wait_queue_t sr04_wait_queue;
    u64 sr04_date_us;
};

static struct sr04_dev sr04dev;

static int sr04_open(struct inode *node, struct file *filp)
{
    return 0;
}

static ssize_t sr04_read(struct file *filp, char __user *buf, size_t count, loff_t *offt)
{
    int timeout;

    gpiod_set_value(sr04dev.trig_gpio, 1);
    udelay(15);
    gpiod_set_value(sr04dev.trig_gpio, 0);

    /* 等待数据 */
    timeout = wait_event_interruptible_timeout(sr04dev.sr04_wait_queue, sr04dev.sr04_date_us, HZ / 5);
    if (timeout)
    {
        if (copy_to_user(buf, &sr04dev.sr04_date_us, 4))
        {
            return -EFAULT;
        }
        sr04dev.sr04_date_us = 0;
        return 4;
    }
    else
    {
        return -EAGAIN;
    }
}

static irq_handler_t sr04_isr(int irq, void *dev_id)
{
    int val = gpiod_get_value(sr04dev.echo_gpio);
    if (val == 1)
    {
        /* 更新数据,记录时间戳 */
        sr04dev.sr04_date_us = ktime_get_ns() / 1000;
    }
    else
    {
        sr04dev.sr04_date_us = ktime_get_ns() / 1000 - sr04dev.sr04_date_us;
        /* 唤醒APP */
        wake_up(sr04dev.sr04_wait_queue);
    }

    return IRQ_HANDLED;
}

static int sr04_probe(struct platform_device *pdev)
{
    int ret;
    /* 1. 获取设备GPIO资源 */
    sr04dev.trig_gpio = gpiod_get(&pdev->dev, "trig-gpio", GPIOD_OUT_HIGH);
    if (IS_ERR(sr04dev.trig_gpio))
    {
        printk("get trig-gpio failed\n");
        return PTR_ERR(sr04dev.trig_gpio);
    }
    sr04dev.echo_gpio = gpiod_get(&pdev->dev, "echo-gpio", GPIOD_IN);
    if (IS_ERR(sr04dev.echo_gpio))
    {
        printk("get echo-gpio failed\n");
        ret = PTR_ERR(sr04dev.echo_gpio);
        goto err_put_trig;
    }
    /* 2. 获取中断号并申请中断 */
    sr04dev.sr04_irq = gpiod_to_irq(sr04dev.echo_gpio);
    ret = request_irq(sr04dev.sr04_irq, sr04_isr, IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING, "_irqsr04", NULL);
    if (ret < 0)
    {
        printk("request irq failed\n");
        goto err_put_echo;
    }
    /* 3. 创建设备 */
    sr04dev.sr04_device = device_create(sr04dev.sr04_class, NULL, MKDEV(sr04dev.major, 0), NULL, "device_sr04");
    if (IS_ERR(sr04dev.sr04_device))
    {
        printk("创建设备失败\n");
        ret = PTR_ERR(sr04dev.sr04_device);
        goto err_free_irq;
    }

    return ret;

err_free_irq:
    free_irq(sr04dev.sr04_irq, NULL);
err_put_echo:
    gpiod_put(sr04dev.echo_gpio);
err_put_trig:
    gpiod_put(sr04dev.trig_gpio);
    return ret;
}

static int sr04_remove(struct platform_device *pdev)
{

    device_destroy(sr04dev.sr04_class, MKDEV(sr04dev.major, 0));
    class_destroy(sr04dev.sr04_class);
    unregister_chrdev(sr04dev.major, "sr04");

    free_irq(sr04dev.sr04_irq, NULL);
    gpiod_put(sr04dev.echo_gpio);
    gpiod_put(sr04dev.trig_gpio);
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

    init_waitqueue_head(&sr04dev.sr04_wait_queue);

    platform_driver_register(&sr04_driver);

    return 0;
}

static void sr04_exit(void)
{
    platform_driver_unregister(&sr04_driver);
}

module_init(sr04_init);
module_exit(sr04_exit);

MODULE_LICENSE("GPL");