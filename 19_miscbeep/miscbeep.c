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
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define miscbeep_name "miscbeep" /* 名字 */
#define miscbeep_minor 144       /* 子设备号 */
#define beepoff 1                /* 关蜂鸣器 */
#define beepon 0                 /* 开蜂鸣器 */

/*miscbeep 设备结构体*/
struct miscbeep_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *node;
    int beep_gpio;
};

struct miscbeep_dev miscbeep;

static int miscbeep_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &miscbeep;
    return 0;
}

static ssize_t miscbeep_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
    u8 databuf[1];
    struct miscbeep_dev *dev = filp->private_data;

    if (cnt < 1)
    {
        printk("error cnt\r\n");
        return -EINVAL;
    }

    if (copy_from_user(databuf, buf, cnt))
    {
        printk("kernel write failed\r\n");
        return -EFAULT;
    }

    if (databuf[0] == beepon)
    {
        gpio_set_value(dev->beep_gpio, 0);
    }
    else if (databuf[0] == beepoff)
    {
        gpio_set_value(dev->beep_gpio, 1);
    }

    return 0;
}

struct file_operations miscbeep_fops = {
    .owner = THIS_MODULE,
    .open = miscbeep_open,
    .write = miscbeep_write,
};

struct miscdevice beep_miscdev = {
    .minor = miscbeep_minor,
    .name = miscbeep_name,
    .fops = &miscbeep_fops,
};

static int miscbeep_probe(struct platform_device *dev)
{
    int ret = 0;
    printk("device and driver match\r\n");

    // 获取设备节点
    miscbeep.node = of_find_node_by_path("/beep");
    if (miscbeep.node == NULL)
    {
        printk("cant get node\r\n");
        return -EINVAL;
    }

    // 获取gpio
    miscbeep.beep_gpio = of_get_named_gpio(miscbeep.node, "beep-gpio", 0);
    if (miscbeep.beep_gpio < 0)
    {
        printk("cant get gpio\r\n");
        return -EINVAL;
    }

    // 申请gpio
    ret = gpio_request(miscbeep.beep_gpio, "beep");
    if (ret < 0)
    {
        printk("cant request gpio\r\n");
        return -EINVAL;
    }

    // 设置电气属性
    ret = gpio_direction_output(miscbeep.beep_gpio, 1);
    if (ret < 0)
    {
        printk("cant set gpio\r\n");
        return -EINVAL;
    }

    ret = misc_register(&beep_miscdev);
    if (ret < 0)
    {
        printk("cant register misc dev\r\n");
        return -EINVAL;
    }

    return 0;
}

static int miscbeep_remove(struct platform_device *dev)
{

    gpio_set_value(miscbeep.beep_gpio, 1);
    gpio_free(miscbeep.beep_gpio);

    misc_deregister(&beep_miscdev);
    return 0;
}

/*匹配列表*/
const struct of_device_id beep_of_match[] = {
    {.compatible = "atkalpha-beep"},
    {/*sentinel*/},
};

struct platform_driver beep_driver = {
    .driver = {
        .name = "imx6ul-beep",
        .of_match_table = beep_of_match,
    },
    .probe = miscbeep_probe,
    .remove = miscbeep_remove,
};

static int __init miscbeep_init(void)
{
    return platform_driver_register(&beep_driver);
}

static void __exit miscbeep_exit(void)
{
    platform_driver_unregister(&beep_driver);
}

module_init(miscbeep_init);
module_exit(miscbeep_exit);

MODULE_LICENSE("GPL");
