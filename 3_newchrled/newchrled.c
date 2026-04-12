#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/device.h>

/* 寄存器物理地址 */
#define CCM_CCGR1_BASE (0X020C406C)
#define SW_MUX_GPIO1_IO03_BASE (0X020E0068)
#define SW_PAD_GPIO1_IO03_BASE (0X020E02F4)
#define GPIO1_DR_BASE (0X0209C000)
#define GPIO1_GDIR_BASE (0X0209C004)

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

static struct led_dev
{
    dev_t leddevid;
    struct cdev ledcdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
} leddev;

static int led_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &leddev;
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

static struct file_operations newchrled_fops =
    {
        .owner = THIS_MODULE,
        .open = led_open,
        .read = led_read,
        .write = led_write,
        .release = led_release,
};

static int __init newledchr_init(void)
{
    u32 registerval;
    int ret;

    /* 1、地址映射 */
    IMX6ULL_CCM_CCGR1 = ioremap(CCM_CCGR1_BASE, 4);
    SW_MUX_GPIO1_IO03 = ioremap(SW_MUX_GPIO1_IO03_BASE, 4);
    SW_PAD_GPIO1_IO03 = ioremap(SW_PAD_GPIO1_IO03_BASE, 4);
    GPIO1_DR = ioremap(GPIO1_DR_BASE, 4);
    GPIO1_GDIR = ioremap(GPIO1_GDIR_BASE, 4);

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

    ret = alloc_chrdev_region(&leddev.leddevid, 0, 1, "newchrdevled");
    if (ret < 0)
    {
        printk("register dev failed\r\n");
        goto err_iomap;
    }

    leddev.major = MAJOR(leddev.leddevid);
    leddev.minor = MINOR(leddev.leddevid);

    cdev_init(&leddev.ledcdev, &newchrled_fops);

    ret = cdev_add(&leddev.ledcdev, leddev.leddevid, 1);
    if (ret < 0)
    {
        printk("add cdev failed\r\n");
        goto err_chrdev;
    }

    leddev.class = class_create(THIS_MODULE, "ledclass");
    if (IS_ERR(leddev.class))
    {
        ret = PTR_ERR(leddev.class);
        goto err_cdev;
    }
    leddev.device = device_create(leddev.class, NULL, leddev.leddevid, NULL, "newchrled");
    if (IS_ERR(leddev.device))
    {
        ret = PTR_ERR(leddev.device);
        goto err_class;
    }
    printk("LED init\r\n");
    return 0;

err_class:
    class_destroy(leddev.class);
err_cdev:
    cdev_del(&leddev.ledcdev);
err_chrdev:
    unregister_chrdev_region(leddev.leddevid, 1);
err_iomap:
    led_iounmap();
    return ret;
}

static void __exit newledchr_exit(void)
{
    device_destroy(leddev.class, leddev.leddevid);
    class_destroy(leddev.class);
    cdev_del(&leddev.ledcdev);
    unregister_chrdev_region(leddev.leddevid, 1);
    led_iounmap();
    printk("LED exit\r\n");
}

module_init(newledchr_init);
module_exit(newledchr_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LiuYang");