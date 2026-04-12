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
#include <asm/mach/map.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>

#if 0
/* 寄存器物理地址 */
#define CCM_CCGR1_BASE (0X020C406C)
#define SW_MUX_GPIO1_IO03_BASE (0X020E0068)
#define SW_PAD_GPIO1_IO03_BASE (0X020E02F4)
#define GPIO1_DR_BASE (0X0209C000)
#define GPIO1_GDIR_BASE (0X0209C004)
#endif

/* 定义LED状态宏 */
#define LEDON (0)
#define LEDOFF (1)

/* 地址映射之后的虚拟地址指针 */
/* void * 表示通用指针，不限定具体数据类型 */
/* 表示指向io内存映射的虚拟地址指针 */
static void __iomem *IMX6ULL_CCM_CCGR1;
static void __iomem *SW_MUX_GPIO1_IO03;
static void __iomem *SW_PAD_GPIO1_IO03;
static void __iomem *GPIO1_DR;
static void __iomem *GPIO1_GDIR;

static void led_iounmap(void)
{
    /* 解映射 */
    if (IMX6ULL_CCM_CCGR1)
    {
        iounmap(IMX6ULL_CCM_CCGR1);
        IMX6ULL_CCM_CCGR1 = NULL;
    }
    if (SW_MUX_GPIO1_IO03)
    {
        iounmap(SW_MUX_GPIO1_IO03);
        SW_MUX_GPIO1_IO03 = NULL;
    }
    if (SW_PAD_GPIO1_IO03)
    {
        iounmap(SW_PAD_GPIO1_IO03);
        SW_PAD_GPIO1_IO03 = NULL;
    }
    if (GPIO1_DR)
    {
        iounmap(GPIO1_DR);
        GPIO1_DR = NULL;
    }
    if (GPIO1_GDIR)
    {
        iounmap(GPIO1_GDIR);
        GPIO1_GDIR = NULL;
    }
}

static void led_switch(u8 state)
{
    u32 val;
    if (state == LEDON)
    {
        val = readl(GPIO1_DR);
        val &= ~(1 << 3);
        writel(val, GPIO1_DR);
    }
    else if (state == LEDOFF)
    {
        val = readl(GPIO1_DR);
        val |= (1 << 3);
        writel(val, GPIO1_DR);
    }
}

static u8 led_getstate(void)
{
    u32 val;
    val = readl(GPIO1_DR);
    val = (val >> 3) & (0X01);
    if (val == 0)
    {
        return LEDON;
    }
    else
    {
        return LEDOFF;
    }
}

static struct dtled_dev
{
    dev_t leddevid;
    struct cdev ledcdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *nd;
} dtsled;

#if 0
static struct dtsled_dev dtsled;
#endif

static int led_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &dtsled;
    return 0;
}

static ssize_t led_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
    u8 ledstate;

    if (count < 1)
    {
        return -EINVAL;
    }

    ledstate = led_getstate();
    if (copy_to_user(buf, &ledstate, 1))
    {
        return -EFAULT;
    }
    return 1;
}

static ssize_t led_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
    u8 ledstate;
    u8 databuf[1];
    int ret;

    if (count < 1)
    {
        return -EINVAL;
    }

    ret = copy_from_user(databuf, buf, 1);
    if (ret)
    {
        return -EFAULT;
    }

    ledstate = databuf[0];

    if (ledstate == LEDON)
    {
        led_switch(LEDON);
    }
    else if (ledstate == LEDOFF)
    {
        led_switch(LEDOFF);
    }
    else
    {
        return -EINVAL;
    }

    printk("databuf[0]=%d\r\n", databuf[0]);
    return 1;
}

static int led_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static struct file_operations dtsled_fops =
    {
        .owner = THIS_MODULE,
        .open = led_open,
        .read = led_read,
        .write = led_write,
        .release = led_release,
};

static int __init dtsled_init(void)
{
    u32 registerval;
    int ret;
    struct property *proper;
    const char *str;
    u32 regdata[10];
    /* 获取设备数的属性数据 */
    /* 1、获取设备节点:aplhaled */
    dtsled.nd = of_find_node_by_path("/alphaled");
    if (dtsled.nd == NULL)
    {
        printk("alphaled node cant't find\r\n");
        return -EINVAL;
    }
    else
    {
        printk("alphaled node has been found\r\n");
    }
    /* 2、获取compatible属性的内容 */
    proper = of_find_property(dtsled.nd, "compatible", NULL);
    if (proper == NULL)
    {
        printk("compatible property cant't find\r\n");
    }
    else
    {
        printk("compatible property : %s\r\n", (char *)proper->value);
    }
    /* 3、获取status属性内容 */
    ret = of_property_read_string(dtsled.nd, "status", &str);
    if (ret < 0)
    {
        printk("status cant't find\r\n");
    }
    else
    {
        printk("status : %s\r\n", str);
    }
    /* 获取reg属性内容 */
    ret = of_property_read_u32_array(dtsled.nd, "reg", regdata, 10);
    if (ret < 0)
    {
        printk("reg property cant't find\r\n");
    }
    else
    {
        u8 i = 0;
        printk("reg data :\r\n");
        for (i = 0; i < 10; i++)
        {
            printk("%#X", regdata[i]);
            printk("\r\n");
        }
    }

#if 0
    /* 1、地址映射 */
    IMX6ULL_CCM_CCGR1 = ioremap(regdata[0], regdata[1]);
    SW_MUX_GPIO1_IO03 = ioremap(regdata[2], regdata[3]);
    SW_PAD_GPIO1_IO03 = ioremap(regdata[4], regdata[5]);
    GPIO1_DR = ioremap(regdata[6], regdata[7]);
    GPIO1_GDIR = ioremap(regdata[8], regdata[9]);
#else
    IMX6ULL_CCM_CCGR1 = of_iomap(dtsled.nd, 0);
    SW_MUX_GPIO1_IO03 = of_iomap(dtsled.nd, 1);
    SW_PAD_GPIO1_IO03 = of_iomap(dtsled.nd, 2);
    GPIO1_DR = of_iomap(dtsled.nd, 3);
    GPIO1_GDIR = of_iomap(dtsled.nd, 4);
#endif

    if (!IMX6ULL_CCM_CCGR1 || !SW_MUX_GPIO1_IO03 || !SW_PAD_GPIO1_IO03 || !GPIO1_DR || !GPIO1_GDIR)
    {
        ret = -ENOMEM;
        goto err_iomap;
    }

    /* 2、使能GPIO1时钟 */
    /* 遵守读-写-读的顺序，修改寄存器的值 */
    registerval = readl(IMX6ULL_CCM_CCGR1);
    registerval &= ~(3 << 26);
    registerval |= (3 << 26);
    writel(registerval, IMX6ULL_CCM_CCGR1);

    /* 设置复用寄存器 */
    writel(0X05, SW_MUX_GPIO1_IO03);
    /* 设置电气属性寄存器 */
    writel(0X10B0, SW_PAD_GPIO1_IO03);
    /* 设置输出，默认LED开启 */
    registerval = readl(GPIO1_GDIR);
    registerval &= ~(1 << 3);
    registerval |= (1 << 3);
    writel(registerval, GPIO1_GDIR);

    registerval = readl(GPIO1_DR);
    registerval &= ~(1 << 3);
    writel(registerval, GPIO1_DR);

    ret = alloc_chrdev_region(&dtsled.leddevid, 0, 1, "dtsled");
    if (ret < 0)
    {
        printk("register dev failed\r\n");
        goto err_iomap;
    }

    dtsled.major = MAJOR(dtsled.leddevid);
    dtsled.minor = MINOR(dtsled.leddevid);

    cdev_init(&dtsled.ledcdev, &dtsled_fops);

    ret = cdev_add(&dtsled.ledcdev, dtsled.leddevid, 1);
    if (ret < 0)
    {
        printk("add cdev failed\r\n");
        goto err_chrdev;
    }

    dtsled.class = class_create(THIS_MODULE, "dtsledclass");
    if (IS_ERR(dtsled.class))
    {
        ret = PTR_ERR(dtsled.class);
        goto err_cdev;
    }
    dtsled.device = device_create(dtsled.class, NULL, dtsled.leddevid, NULL, "dtsled");
    if (IS_ERR(dtsled.device))
    {
        ret = PTR_ERR(dtsled.device);
        goto err_class;
    }
    printk("MAJOR : %d,MINOR : %d\r\n", dtsled.major, dtsled.minor);
    printk("LED init\r\n");
    return 0;

err_class:
    class_destroy(dtsled.class);
err_cdev:
    cdev_del(&dtsled.ledcdev);
err_chrdev:
    unregister_chrdev_region(dtsled.leddevid, 1);
err_iomap:
    led_iounmap();
    return ret;
}

static void __exit dtsled_exit(void)
{
    device_destroy(dtsled.class, dtsled.leddevid);
    class_destroy(dtsled.class);
    cdev_del(&dtsled.ledcdev);
    unregister_chrdev_region(dtsled.leddevid, 1);
    led_iounmap();
    printk("LED exit\r\n");
}

module_init(dtsled_init);
module_exit(dtsled_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LiuYang");