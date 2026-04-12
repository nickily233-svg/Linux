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
#include <linux/semaphore.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <linux/uaccess.h>
#include <asm/io.h>

#define ledon 0
#define ledoff 1
#define ledname "led"
#define ledcnt 1

struct led_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *node;
    int gpio;
    int major;
    int minor;
};

static struct led_dev led;

static int led_open(struct inode *inode, struct file *filp)
{
    struct led_dev *dev;
    filp->private_data = &led;
    dev = filp->private_data;

    return 0;
}

static ssize_t led_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
    return 0;
}

static ssize_t led_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
    u8 databuf[1];
    int ledstate;

    struct led_dev *dev = filp->private_data;

    if (cnt < 1)
    {
        printk("cnt ovweflow\r\n");
        return -EINVAL;
    }
    if (copy_from_user(databuf, buf, 1))
    {
        printk("kerbnel write failed\r\n");
        return -EFAULT;
    }

    ledstate = databuf[0];

    if (ledstate == ledon)
    {
        gpio_set_value(dev->gpio, 0); /* 打开LED 灯 */
    }
    else if (ledstate == ledoff)
    {
        gpio_set_value(dev->gpio, 1); /* 关闭LED 灯 */
    }

    return 1;
}

static int led_release(struct inode *inode, struct file *filp)
{
    struct led_dev *dev;
    dev = filp->private_data;

    return 0;
}

struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .read = led_read,
    .write = led_write,
    .release = led_release,
};

static int __init led_init(void)
{
    int retvalue;

    // 获取设备节点
    led.node = of_find_node_by_path("/gpioled");
    if (led.node == NULL)
    {
        printk("cant fine node\r\n");
        return -EINVAL;
    }
    else
    {
        printk("node has been found\r\n");
    }

    // 获取gpio属性
    led.gpio = of_get_named_gpio(led.node, "led-gpio", 0);
    if (led.gpio < 0)
    {
        printk("cant get gpio\r\n");
        return -EINVAL;
    }
    printk("led-gpio num =%d\r\n", led.gpio);

    // 申请gpio
    retvalue = gpio_request(led.gpio, "led-gpio");
    if (retvalue)
    {
        printk("gpio request failed\r\n");
        return retvalue;
    }

    // 设置电气属性
    retvalue = gpio_direction_output(led.gpio, 1);
    if (retvalue < 0)
    {
        printk("set gpio failed\r\n");
        gpio_free(led.gpio);
        return retvalue;
    }

    // 注册字符设备驱动
    // 创建设备号
    if (led.major)
    {
        led.devid = MKDEV(led.major, 0);
        retvalue = register_chrdev_region(led.devid, ledcnt, ledname);
        printk("register success\r\n");
    }
    else
    {
        retvalue = alloc_chrdev_region(&led.devid, 0, ledcnt, "led");
        led.major = MAJOR(led.devid);
        led.minor = MINOR(led.devid);
    }
    if (retvalue < 0)
    {
        return retvalue;
    }
    printk("major = %d,minor = %d\r\n", led.major, led.minor);

    // 初始化cdev
    led.cdev.owner = THIS_MODULE;
    cdev_init(&led.cdev, &led_fops);

    // 添加cdev
    retvalue = cdev_add(&led.cdev, led.devid, ledcnt);
    if (retvalue < 0)
    {
        printk("add cdev failed\r\n");
        return -EINVAL;
    }

    // 创建类
    led.class = class_create(THIS_MODULE, ledname);
    if (IS_ERR(led.class))
    {
        printk("create class failed\r\n");
        return PTR_ERR(led.class);
    }
    printk("class_create success\n");

    // 创建设备
    led.device = device_create(led.class, NULL, led.devid, NULL, ledname);
    if (IS_ERR(led.device))
    {
        printk("create device failed\r\n");
        return PTR_ERR(led.device);
    }
    printk("device_create success\n");
    return 0;
}

static void __exit led_exit(void)
{
    device_destroy(led.class, led.devid);
    class_destroy(led.class);
    cdev_del(&led.cdev);
    unregister_chrdev_region(led.devid, ledcnt);
    gpio_free(led.gpio);
    of_node_put(led.node);
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LiuYang");