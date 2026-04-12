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
#include <linux/timer.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

/*宏定义*/
#define LEDON 0
#define LEDOFF 1
#define TIMER_NAME "timer"
#define TIMER_CNT 1
#define CLOSE_CMD (_IO(0XEF, 0X1))
#define OPEN_CMD (_IO(0XEF, 0X2))
#define SETPERIOD_CMD (_IO(0XEF, 0X3))

struct timer_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *node;
    int major;
    int minor;
    int led_gpio;            /*led的gpio*/
    int timeperiod;          /*定时周期ms*/
    struct timer_list timer; /*定义定时器*/
    spinlock_t lock;         /*自旋锁*/
};

static struct timer_dev timer;

static int led_init(void)
{
    int ret = 0;
    timer.node = of_find_node_by_path("/gpioled");
    if (timer.node == NULL)
    {
        printk("cant fine node\r\n");
        return -EINVAL;
    }

    timer.led_gpio = of_get_named_gpio(timer.node, "led-gpio", 0);
    if (timer.led_gpio < 0)
    {
        printk("cant get gpio\r\n");
        return -EINVAL;
    }
    printk("led-gpio num = %d\r\n", timer.led_gpio);

    ret = gpio_request(timer.led_gpio, "led-gpio");
    if (ret)
    {
        printk("request failed\r\n");
        return ret;
    }

    ret = gpio_direction_output(timer.led_gpio, 1);
    if (ret < 0)
    {
        printk("cant set gpio\r\n");
        return -EINVAL;
    }

    return 0;
}

/*
 * @description : ioctl 函数，
 * @param – filp : 要打开的设备文件(文件描述符)
 * @param - cmd : 应用程序发送过来的命令
 * @param - arg : 参数
 * @return : 0 成功;其他 失败
 */
static long timer_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct timer_dev *dev = (struct timer_dev *)filp->private_data;
    int timerperiod;
    unsigned long flags;
    switch (cmd)
    {
    case CLOSE_CMD: /* 关闭定时器 */
        del_timer_sync(&dev->timer);
        break;
    case OPEN_CMD: /* 打开定时器 */
        spin_lock_irqsave(&dev->lock, flags);
        timerperiod = dev->timeperiod;
        spin_unlock_irqrestore(&dev->lock, flags);
        mod_timer(&dev->timer, jiffies + msecs_to_jiffies(dev->timeperiod));
        break;
    case SETPERIOD_CMD: /* 设置定时器周期 */
        spin_lock_irqsave(&dev->lock, flags);
        dev->timeperiod = arg;
        spin_unlock_irqrestore(&dev->lock, flags);
        mod_timer(&dev->timer, jiffies + msecs_to_jiffies(arg));
        break;
    default:
        return -EINVAL;
        break;
    }
    return 0;
}

void timer_callback(unsigned long arg)
{
    struct timer_dev *dev = (struct timer_dev *)arg;
    static int state = 1;
    int timerperiod;
    unsigned long flags;

    state = !state;
    gpio_set_value(dev->led_gpio, state);

    spin_lock_irqsave(&dev->lock, flags);
    timerperiod = dev->timeperiod;
    spin_unlock_irqrestore(&dev->lock, flags);
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(dev->timeperiod));
}

static int timer_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &timer;
    return 0;
}

static ssize_t timer_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
    return 0;
}

static ssize_t timer_write(struct file *filp, const char __user *buf, size_t cnt, loff_t *offt)
{
    return cnt;
}

static int timer_release(struct inode *inode, struct file *filp)
{
    return 0;
}

struct file_operations timer_fops = {
    .owner = THIS_MODULE,
    .open = timer_open,
    .read = timer_read,
    .write = timer_write,
    .release = timer_release,
    .unlocked_ioctl = timer_unlocked_ioctl,
};

static int __init timer_init(void)
{
    int ret = 0;
    int retvalue;

    spin_lock_init(&timer.lock);

    timer.timeperiod = 1000;
    ret = led_init();
    if (ret < 0)
    {
        printk("led init failed\r\n");
        return -EFAULT;
    }

    // 注册字符设备驱动
    // 创建设备号
    if (timer.major)
    {
        timer.devid = MKDEV(timer.major, 0);
        retvalue = register_chrdev_region(timer.devid, TIMER_CNT, TIMER_NAME);
        printk("register success\r\n");
    }
    else
    {
        retvalue = alloc_chrdev_region(&timer.devid, 0, TIMER_CNT, TIMER_NAME);
        timer.major = MAJOR(timer.devid);
        timer.minor = MINOR(timer.devid);
    }
    if (retvalue < 0)
    {
        return retvalue;
    }
    printk("major = %d,minor = %d\r\n", timer.major, timer.minor);

    // 初始化cdev
    timer.cdev.owner = THIS_MODULE;
    cdev_init(&timer.cdev, &timer_fops);

    // 添加cdev
    retvalue = cdev_add(&timer.cdev, timer.devid, TIMER_CNT);
    if (retvalue < 0)
    {
        printk("add cdev failed\r\n");
        return -EINVAL;
    }

    // 创建类
    timer.class = class_create(THIS_MODULE, TIMER_NAME);
    if (IS_ERR(timer.class))
    {
        return PTR_ERR(timer.class);
    }

    // 创建设备
    timer.device = device_create(timer.class, NULL, timer.devid, NULL, TIMER_NAME);
    if (IS_ERR(timer.device))
    {
        return PTR_ERR(timer.device);
    }

    init_timer(&timer.timer);
    timer.timer.function = timer_callback;
    timer.timer.data = (unsigned long)&timer;

    return 0;
}

static void __exit timer_exit(void)
{
    gpio_set_value(timer.led_gpio, 1); /* 卸载驱动的时候关闭LED */
    del_timer_sync(&timer.timer);      /* 删除timer */
#if 0
 del_timer(&timer.tiemr);
#endif

    device_destroy(timer.class, timer.devid);
    class_destroy(timer.class);
    cdev_del(&timer.cdev);
    unregister_chrdev_region(timer.devid, TIMER_CNT);
    gpio_free(timer.led_gpio);
    of_node_put(timer.node);
}

module_init(timer_init);
module_exit(timer_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LY");