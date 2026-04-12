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
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define LEDON 0
#define LEDOFF 1

#define GPIOLED_NAME "gpioled"
#define GPIOLED_CNT 1

struct gpioled_dev
{
    dev_t gpioledid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *node;
    int led_gpio;
};

struct gpioled_dev gpioleddev;

static int gpioled_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &gpioleddev;
    printk("gpioled open\r\n");
    return 0;
}

static ssize_t gpioled_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
    return 0;
}

static ssize_t gpioled_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
    int retvalue;
    u8 databuf[1];
    u8 ledstate;
    struct gpioled_dev *dev = filp->private_data;

    if (cnt < 1)
        return -EINVAL;

    retvalue = copy_from_user(databuf, buf, 1);
    if (retvalue)
    {
        printk("can not write\r\n");
        return -EFAULT;
    }

    ledstate = databuf[0];
    if (ledstate == LEDON)
    {
        gpio_set_value(dev->led_gpio, 0);
    }
    else if (ledstate == LEDOFF)
    {
        gpio_set_value(dev->led_gpio, 1);
    }
    else
    {
        printk("can not control led\r\n");
        return -EFAULT;
    }

    return 1;
}

static int gpioled_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static struct file_operations gpioled_fops =
    {
        .owner = THIS_MODULE,
        .open = gpioled_open,
        .read = gpioled_read,
        .write = gpioled_write,
        .release = gpioled_release,
};

static int __init gpioled_init(void)
{
    int retvalue;
    // 获取节点
    gpioleddev.node = of_find_node_by_path("/gpioled");
    if (gpioleddev.node == NULL)
    {
        printk("gpioled node can not fine\r\n");
        return -EINVAL;
    }
    else
    {
        printk("gpioled node has been found");
    }
    // 获取设备数的gpio属性
    gpioleddev.led_gpio = of_get_named_gpio(gpioleddev.node, "led-gpio", 0);
    if (gpioleddev.led_gpio < 0)
    {
        printk("can not get led-gpio\r\n");
        return -EINVAL;
    }
    printk("led-gpio num = %d\r\n", gpioleddev.led_gpio);

    // 申请gpio
    retvalue = gpio_request(gpioleddev.led_gpio, "led-gpio");
    if (retvalue)
    {
        return retvalue;
    }
    // 配置gpio电气属性
    retvalue = gpio_direction_output(gpioleddev.led_gpio, 0);
    if (retvalue < 0)
    {
        printk("can not set gpio\r\n");
        return retvalue;
    }

    // 注册字符设备驱动
    if (gpioleddev.major)
    {
        gpioleddev.gpioledid = MKDEV(gpioleddev.major, 0);
        register_chrdev_region(gpioleddev.gpioledid, GPIOLED_CNT, GPIOLED_NAME);
    }
    else
    {
        alloc_chrdev_region(&gpioleddev.gpioledid, 0, GPIOLED_CNT, GPIOLED_NAME);
        gpioleddev.major = MAJOR(gpioleddev.gpioledid);
        gpioleddev.minor = MINOR(gpioleddev.gpioledid);
    }
    printk("gpioled maojr = %d,gpioled minor = %d\r\n", gpioleddev.major, gpioleddev.minor);

    // 初始化cdev
    gpioleddev.cdev.owner = THIS_MODULE;
    cdev_init(&gpioleddev.cdev, &gpioled_fops);

    // 添加一个cdev
    cdev_add(&gpioleddev.cdev, gpioleddev.gpioledid, GPIOLED_CNT);

    // 创建类
    gpioleddev.class = class_create(THIS_MODULE, GPIOLED_NAME);
    if (IS_ERR(gpioleddev.class))
    {
        return PTR_ERR(gpioleddev.class);
    }

    // 创建设备
    gpioleddev.device = device_create(gpioleddev.class, NULL, gpioleddev.gpioledid, NULL, GPIOLED_NAME);
    if (IS_ERR(gpioleddev.device))
    {
        return PTR_ERR(gpioleddev.device);
    }

    return 0;
}

static void __exit gpioled_exit(void)
{
    cdev_del(&gpioleddev.cdev);
    unregister_chrdev_region(gpioleddev.gpioledid, 1);
    device_destroy(gpioleddev.class, gpioleddev.gpioledid);
    class_destroy(gpioleddev.class);

    gpio_free(gpioleddev.led_gpio);
    of_node_put(gpioleddev.node);
}

module_init(gpioled_init);
module_exit(gpioled_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LiuYang");