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

static int sr04_open(struct inode *node, struct file *filp)
{
}

static ssize_t sr04_read(struct file *filp, char __user *buf, size_t count, loff_t *offt)
{
}

static struct file_operations sr04_fops = {
    .open = sr04_open,
    .read = sr04_read,
};

static int sr04_init(void)
{
    printk("SR04 驱动加载成功\n");
    return 0;
}

static void sr04_exit(void)
{
    printk("SR04 驱动卸载成功\n");
}

module_init(sr04_init);
module_exit(sr04_exit);

MODULE_LICENSE("GPL");