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

#define keycnt 1
#define keyname "key"
// 定义按键值
#define key0value 0XF0
#define invakey 0X00

struct key_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *node;
    int major;
    int minor;
    int key_gpio;
    atomic_t keyvalue;
};

static struct key_dev keydev;

static int keyio_init(void)
{
    int ret;
    // 注册设备节点
    keydev.node = of_find_node_by_path("/key");
    if (keydev.node == NULL)
    {
        printk("register node failed\r\n");
        return -EINVAL;
    }

    // 获取gpio
    keydev.key_gpio = of_get_named_gpio(keydev.node, "key-gpio", 0);
    if (keydev.key_gpio < 0)
    {
        printk("get gpio failed\r\n");
        return -EINVAL;
    }
    printk("gpio num %d\r\n", keydev.key_gpio);

    // 申请gpio
    ret = gpio_request(keydev.key_gpio, "key-0");
    if (ret < 0)
    {
        printk("request gpio failed\r\n");
        return -EINVAL;
    }

    // 配置gpio属性
    ret = gpio_direction_input(keydev.key_gpio);
    if (ret < 0)
    {
        printk("set gpio failed\r\n");
        return -EINVAL;
    }

    return 0;
}

static int key_open(struct inode *inode, struct file *filp)
{

    struct key_dev *dev;
    filp->private_data = &keydev;
    dev = filp->private_data;

    printk("open\r\n");

    return 0;
}

static ssize_t key_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
    int ret = 0;
    unsigned char value;
    struct key_dev *dev;

    filp->private_data = &keydev;
    dev = filp->private_data;

    if (gpio_get_value(dev->key_gpio) == 0)
    {
        while (!gpio_get_value(dev->key_gpio))
        {
        }
        atomic_set(&dev->keyvalue, key0value);
    }
    else
    {
        atomic_set(&dev->keyvalue, invakey);
    }
    value = atomic_read(&dev->keyvalue);
    ret = copy_to_user(buf, &value, sizeof(value));

    return 1;
}

static ssize_t key_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
    struct key_dev *dev;
    filp->private_data = &keydev;
    dev = filp->private_data;

    return 1;
}

static int key_release(struct inode *inode, struct file *filp)
{
    struct key_dev *dev;
    filp->private_data = &keydev;
    dev = filp->private_data;
    printk("release\r\n");
    return 0;
}

struct file_operations key_fops = {
    .owner = THIS_MODULE,
    .open = key_open,
    .read = key_read,
    .write = key_write,
    .release = key_release,
};
static int __init keyinit(void)
{
    int ret;

    ret = keyio_init();
    if (ret < 0)
    {
        return ret;
    }

    // 初始化原子变量
    atomic_set(&keydev.keyvalue, invakey);

    // 获取设备节点
    if (keydev.major)
    {
        keydev.devid = MKDEV(keydev.major, 0);
        register_chrdev_region(keydev.devid, keycnt, keyname);
    }
    else
    {
        alloc_chrdev_region(&keydev.devid, 0, keycnt, keyname);
        keydev.major = MAJOR(keydev.devid);
        keydev.minor = MINOR(keydev.devid);
    }
    printk("major : %d minor : %d\r\n", keydev.major, keydev.minor);

    // 初始化cdev
    cdev_init(&keydev.cdev, &key_fops);

    // 添加cdev
    ret = cdev_add(&keydev.cdev, keydev.devid, keycnt);
    if (ret < 0)
    {
        printk("add cdev failed\r\n");
        return -EFAULT;
    }

    // 创建类
    keydev.class = class_create(THIS_MODULE, keyname);
    if (IS_ERR(keydev.class))
    {
        return PTR_ERR(keydev.class);
    }

    // 创建设备
    keydev.device = device_create(keydev.class, NULL, keydev.devid, NULL, keyname);
    if (IS_ERR(keydev.device))
    {
        return PTR_ERR(keydev.device);
    }

    return 0;
}

static void __exit keyexit(void)
{
    device_destroy(keydev.class, keydev.devid);
    class_destroy(keydev.class);
    cdev_del(&keydev.cdev);
    unregister_chrdev_region(keydev.devid, keycnt);
    of_node_put(keydev.node);
    gpio_free(keydev.key_gpio);
}

module_init(keyinit);
module_exit(keyexit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LiuYang");