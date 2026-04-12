#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/io.h>

/* 完整设备号 */
static dev_t leddevid;
/* 描述设备的结构体 */
static struct cdev led_cdev;

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

/* 声明函数 */
static void led_switch(u8 state);
static ssize_t led_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos);
static ssize_t led_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos);
static int led_open(struct inode *inode, struct file *filp);
static int led_release(struct inode *inode, struct file *filp);
static int __init led_init(void);
static void __exit led_exit(void);
static u8 led_getstate(void);

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

static int led_open(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t led_read(struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
    u8 ledstate;
    ledstate = led_getstate();
    if (copy_to_user(buf, &ledstate, 1))
    {
        return -EFAULT;
    }
    return 1;
}

static ssize_t led_write(struct file *filp, const char __user *buf, size_t count, loff_t *ppos)
{
    u8 databuf[1];
    u8 ledstate;

    if (copy_from_user(databuf, buf, 1))
    {
        printk("kernel write errorr!\r\n");
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

    return 1;
}

static int led_release(struct inode *inode, struct file *filp)
{
    return 0;
}

/* 定义字符设备的操作函数表 */
static struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .open = led_open,
    .read = led_read,
    .write = led_write,
    .release = led_release,
};

/* @note : 入口函数
 * @parameter : 无
 * @return : 0:入口函数完成 其他 入口函数错误
 */
static int __init led_init(void)
{
    int registerval;
    int retvalue;
    /* 1、地址映射 */
    IMX6ULL_CCM_CCGR1 = ioremap(CCM_CCGR1_BASE, 4);
    SW_MUX_GPIO1_IO03 = ioremap(SW_MUX_GPIO1_IO03_BASE, 4);
    SW_PAD_GPIO1_IO03 = ioremap(SW_PAD_GPIO1_IO03_BASE, 4);
    GPIO1_DR = ioremap(GPIO1_DR_BASE, 4);
    GPIO1_GDIR = ioremap(GPIO1_GDIR_BASE, 4);

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

    /* 1、动态申请设备号 */
    /* 参数：1、输出参数，内核分配好的设备号会放在这里
     * 2、起始次设备号
     * 3、申请设备号数量
     * 4、设备名字
     */
    retvalue = alloc_chrdev_region(&leddevid, 0, 1, "led");
    /* 申请失败则返回内核给出的错误码 */
    if (retvalue < 0)
    {
        return retvalue;
    }
    /* 2、初始化字符设备对象，并绑定fops */
    cdev_init(&led_cdev, &led_fops);
    led_cdev.owner = THIS_MODULE;
    /*/3、将字符设备正式注册到内核
     * 参数：1、加入内核的字符设备对象
     * 2、设备号
     * 3、管理几个连续的设备号
     */
    retvalue = cdev_add(&led_cdev, leddevid, 1);
    if (retvalue < 0)
    {
        unregister_chrdev_region(leddevid, 1);
        return retvalue;
    }
    /* 打印主次设备号 */
    printk("major = %d minor = %d\r\n", MAJOR(leddevid), MINOR(leddevid));
    return 0;
}
/* @note : 出口函数
 * @parameter : 无
 * @return : 无
 */
static void __exit led_exit(void)
{
    /* 解映射 */
    iounmap(IMX6ULL_CCM_CCGR1);
    iounmap(SW_MUX_GPIO1_IO03);
    iounmap(SW_PAD_GPIO1_IO03);
    iounmap(GPIO1_DR);
    iounmap(GPIO1_GDIR);

    /* 删掉注册进内核的字符设备对象 */
    cdev_del(&led_cdev);
    /* 将设备号还回内核 */
    unregister_chrdev_region(leddevid, 1);
    printk("led exit\r\n");
}

/* 加载卸载驱动模块函数 */
module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LiuYang");