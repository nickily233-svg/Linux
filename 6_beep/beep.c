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

#define beepon 1
#define beepoff 0
#define beepcnt 1
#define beepname "beep"

struct beep_dev
{
    dev_t beepdevid;
    struct cdev beepdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *node;
    int gpiobeep;
};

static struct beep_dev beep;

static int beep_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &beep;
    printk("open beep success\r\n");
    return 0;
}

static ssize_t beep_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
    return 0;
}

static ssize_t beep_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
    int retvalue;
    u8 databuf[1];
    u8 beepstate;
    struct beep_dev *dev = filp->private_data;

    if (cnt < 1)
    {
        return -EINVAL;
    }
    // 返回值是没拷贝成功的字节数
    retvalue = copy_from_user(databuf, buf, cnt);
    if (retvalue)
    {
        return retvalue;
    }

    beepstate = databuf[0];

    if (beepstate == beepon)
    {
        gpio_set_value(dev->gpiobeep, 1);
    }
    else if (beepstate == beepoff)
    {
        gpio_set_value(dev->gpiobeep, 0);
    }

    return 1;
}

static int beep_release(struct inode *inode, struct file *filp)
{
    return 0;
}

struct file_operations beep_fops =
    {
        .owner = THIS_MODULE,
        .open = beep_open,
        .read = beep_read,
        .write = beep_write,
        .release = beep_release,
};

static int __init beep_init(void)
{
    int retvalue;

    // 获取设备节点
    beep.node = of_find_node_by_path("/beep");
    if (beep.node == NULL)
    {
        printk("beep node can not find\r\n");
        return -EINVAL;
    }
    else
    {
        printk("beep node has been found\r\n");
    }

    // 获取gpio属性
    beep.gpiobeep = of_get_named_gpio(beep.node, "beep-gpio", 0);
    if (beep.gpiobeep < 0)
    {
        printk("beep-gpio can not find\r\n");
        return -EINVAL;
    }
    printk("beep-gpio num = %d\r\n", beep.gpiobeep);

    // 申请gpio
    retvalue = gpio_request(beep.gpiobeep, "beep-gpio");
    if (retvalue < 0)
    {
        printk("request gpio failed\r\n");
        return retvalue;
    }

    // 配置gpio电气属性
    retvalue = gpio_direction_output(beep.gpiobeep, 1);
    if (retvalue < 0)
    {
        printk("can not set gpio\r\n");
        return retvalue;
    }

    // 注册字符设备驱动
    if (beep.major)
    {
        retvalue = register_chrdev_region(beep.beepdevid, beepcnt, beepname);
        if (retvalue < 0)
        {
            printk("can not register chrdev\r\n");
            return -EINVAL;
        }
        beep.beepdevid = MKDEV(beep.beepdevid, 0);
    }
    else
    {
        alloc_chrdev_region(&beep.beepdevid, 0, beepcnt, beepname);
        beep.major = MAJOR(beep.beepdevid);
        beep.minor = MINOR(beep.beepdevid);
    }
    printk("beepdev major = %d , minor = %d\r\n", beep.major, beep.minor);

    // 初始化cdev
    cdev_init(&beep.beepdev, &beep_fops);

    // 添加cdev
    retvalue = cdev_add(&beep.beepdev, beep.beepdevid, beepcnt);
    if (retvalue < 0)
    {
        return -EINVAL;
    }
    // 创建类
    beep.class = class_create(THIS_MODULE, beepname);
    if (IS_ERR(beep.class))
    {
        return PTR_ERR(beep.class);
    }
    // 创建设备
    beep.device = device_create(beep.class, NULL, beep.beepdevid, NULL, beepname);
    if (IS_ERR(beep.device))
    {
        return PTR_ERR(beep.device);
    }

    return 0;
}

static void __exit beep_exit(void)
{
    device_destroy(beep.class, beep.beepdevid);
    class_destroy(beep.class);
    cdev_del(&beep.beepdev);
    unregister_chrdev_region(beep.beepdevid, 1);
    gpio_free(beep.gpiobeep);
    of_node_put(beep.node);
}

module_init(beep_init);
module_exit(beep_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LiuYang");