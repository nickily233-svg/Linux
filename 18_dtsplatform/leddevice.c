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

/*寄存器地址定义*/
#define CCM_CCGR1_BASE (0X020C406C)
#define SW_MUX_GPIO1_IO03_BASE (0X020E0068)
#define SW_PAD_GPIO1_IO03_BASE (0X020E02F4)
#define GPIO1_DR_BASE (0X0209C000)
#define GPIO1_GDIR_BASE (0X0209C004)
#define REGISTER_LENGTH 4

/*
 *@note:释放platform设备时，此函数执行
 *@paramter:dev:要释放的platform设备
 *@return:无
 */
static void led_release(struct device *dev)
{
    printk("led device released!\r\n");
}

/*设备资源信息*/
static struct resource led_resource[] = {
    [0] = {
        .start = CCM_CCGR1_BASE,
        .end = (CCM_CCGR1_BASE + REGISTER_LENGTH - 1),
        .flags = IORESOURCE_MEM,
    },

    [1] = {
        .start = SW_MUX_GPIO1_IO03_BASE,
        .end = (SW_MUX_GPIO1_IO03_BASE + REGISTER_LENGTH - 1),
        .flags = IORESOURCE_MEM,
    },

    [2] = {
        .start = SW_PAD_GPIO1_IO03_BASE,
        .end = (SW_PAD_GPIO1_IO03_BASE + REGISTER_LENGTH - 1),
        .flags = IORESOURCE_MEM,
    },

    [3] = {
        .start = GPIO1_DR_BASE,
        .end = (GPIO1_DR_BASE + REGISTER_LENGTH - 1),
        .flags = IORESOURCE_MEM,
    },

    [4] = {
        .start = GPIO1_GDIR_BASE,
        .end = (GPIO1_GDIR_BASE + REGISTER_LENGTH - 1),
        .flags = IORESOURCE_MEM,
    },
};

/*platform设备结构体*/
static struct platform_device leddevice = {
    .name = "imx6ul-led",
    .id = -1,
    .dev = {
        .release = &led_release,
    },
    .num_resources = ARRAY_SIZE(led_resource),
    .resource = led_resource,
};

#if 0
struct led_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
};

struct led_dev led;

static int led_open(struct inode *inode, struct file *filp)
{
    struct led_dev *dev = &led;
    return 0;
}

static ssize_t led_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
    return 0;
}

static int led_release(struct inode *inode, struct file *filp)
{
    return 0;
}

struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .read = led_read,
    .release = led_release,
};
#endif

/*设备模块加载*/
static int __init leddevice_init(void)
{
    return platform_device_register(&leddevice);
}
/*设备模块注销*/
static void __exit leddevice_exit(void)
{
    platform_device_unregister(&leddevice);
}

module_init(leddevice_init);
module_exit(leddevice_exit);

MODULE_LICENSE("GPL");