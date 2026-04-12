#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <linux/irq.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/platform_device.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define led_cnt 1
#define leddev_name "dtsplatled"
#define ledoff 1
#define ledon 0

struct leddev_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *node;
    int major;
    int minor;
    int led0;
};

struct leddev_dev leddev;

void led0_switch(u8 state)
{
    if (state == ledon)
    {
        gpio_set_value(leddev.led0, 0);
    }
    else if (state == ledoff)
    {
        gpio_set_value(leddev.led0, 1);
    }
}

static int led_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &leddev;
    return 0;
}

static ssize_t led_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
    int retvalue;
    unsigned char databuf[2];

    retvalue = copy_from_user(databuf, buf, sizeof(databuf));
    if (retvalue < 0)
    {
        printk("kernel write failed!\r\n");
        return -EFAULT;
    }

    if (databuf[0] == ledon)
    {
        led0_switch(ledon);
    }
    else if (databuf[0] == ledoff)
    {
        led0_switch(ledoff);
    }

    return 0;
}

struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .write = led_write,
};

static int led_probe(struct platform_device *dev)
{
    int ret = 0;
    printk("led driver and device has matched!\r\n");

    // 申请字符设备
    if (leddev.major)
    {
        leddev.devid = MKDEV(leddev.major, 0);
        ret = register_chrdev_region(leddev.devid, led_cnt, leddev_name);
        if (ret < 0)
        {
            printk("cant register chr dev\r\n");
            return -EINVAL;
        }
    }
    else
    {
        ret = alloc_chrdev_region(&leddev.devid, 0, led_cnt, leddev_name);
        if (ret < 0)
        {
            printk("cant register chr dev\r\n");
            return -EINVAL;
        }
        leddev.major = MAJOR(leddev.devid);
        leddev.minor = MINOR(leddev.devid);
    }
    printk("major = %d minor = %d\r\n", leddev.major, leddev.minor);

    cdev_init(&leddev.cdev, &led_fops);

    cdev_add(&leddev.cdev, leddev.devid, led_cnt);

    leddev.class = class_create(THIS_MODULE, leddev_name);
    if (IS_ERR(leddev.class))
    {
        return PTR_ERR(leddev.class);
    }

    leddev.device = device_create(leddev.class, NULL, leddev.devid, NULL, leddev_name);
    if (IS_ERR(leddev.device))
    {
        return PTR_ERR(leddev.device);
    }

    /*获取节点*/
    leddev.node = of_find_node_by_path("/gpioled");
    if (leddev.node == NULL)
    {
        printk("cant get node\r\n");
        return -EINVAL;
    }

    /*获取gpio*/
    leddev.led0 = of_get_named_gpio(leddev.node, "led-gpio", 0);
    if (leddev.led0 < 0)
    {
        printk("cant get gpio\r\n");
        return -EINVAL;
    }

    /*申请gpio*/
    gpio_request(leddev.led0, "led0");

    /*配置gpio电气属性*/
    gpio_direction_output(leddev.led0, 1);

    return 0;
}

static int led_remove(struct platform_device *dev)
{
    gpio_set_value(leddev.led0, 1);

    gpio_free(leddev.led0);
    of_node_put(leddev.node);
    device_destroy(leddev.class, leddev.devid);
    class_destroy(leddev.class);
    cdev_del(&leddev.cdev);
    unregister_chrdev_region(leddev.devid, led_cnt);
    return 0;
}

/*设备匹配表*/
static const struct of_device_id led_of_match[] = {
    {.compatible = "atkalpha-gpioled"},
    {/*sentinel*/},
};

static struct platform_driver led_driver = {
    .driver = {
        .name = "imx6ul-led",
        .of_match_table = led_of_match,
    },
    .probe = led_probe,
    .remove = led_remove,
};

static int __init leddriver_init(void)
{
    return platform_driver_register(&led_driver);
}

static void __exit leddriver_exit(void)
{
    platform_driver_unregister(&led_driver);
}

module_init(leddriver_init);
module_exit(leddriver_exit);

MODULE_LICENSE("GPL");