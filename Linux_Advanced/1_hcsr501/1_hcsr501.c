#include <linux/module.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>

static struct hcsr501_dev
{
    dev_t dev_id;
    struct class *class;
    struct device *device;
    struct gpio_desc *gpio;
    int major;
    int irq;
    wait_queue_head_t hcsr501_wq; /* 等待队列头 */
    int hcsr501_has_data;
};

static struct hcsr501_dev hcsr501dev = {
    .hcsr501_has_data = 0,
};

static irqreturn_t hcsr501_irqhandler(int irq, void *dev_id)
{
    /* 1. 记录数据 */
    /* 2. 唤醒APP:去同一个链表把APP唤醒 */
    hcsr501dev.hcsr501_has_data = 1;

    wake_up(hcsr501dev.hcsr501_wq);

    return IRQ_WAKE_THREAD;
}

static int hcsr501_open(struct inode *node, struct file *filp)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return 0;
}

static ssize_t hcsr501_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
#if 0
    int val;
    int len = (count < 4) ? count : 4;
    val = gpiod_get_value(hcsr501dev.gpio);
    copy_to_user(buf, &val, len);
    return len;
#else
    int val;
    int len = (count < 4) ? count : 4;
    /* 1.有数据就copy_to_user */
    /* 2.无数据就休眠:放入某个链表 */

    wait_event_interruptible(hcsr501dev.hcsr501_wq, hcsr501dev.hcsr501_has_data);
    copy_to_user(buf, &hcsr501dev.hcsr501_has_data, len);
    hcsr501dev.hcsr501_has_data = 0;
    return len;
#endif
}

static ssize_t hcsr501_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return 0;
}

static unsigned int hcsr501_poll(struct file *filp, struct poll_table_struct *wait)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return 0;
}

static int hcsr501_release(struct inode *node, struct file *filp)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    return 0;
}

struct file_operations hcsr501_fops =
    {
        .open = hcsr501_open,
        .read = hcsr501_read,
        .write = hcsr501_write,
        .poll = hcsr501_poll,
        .release = hcsr501_release,
};

static const struct of_device_id alientek_hcsr501[] =
    {
        {.compatible = "alientek,hcsr501"},
        {/* sentinel */},
};

static int hcsr501_probe(struct platform_device *pdev)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
    /* 1. 获得设备信息 */
    hcsr501dev.gpio = gpiod_get(&pdev->dev, NULL, 0);
    /* 2. 设置引脚方向 */
    gpiod_direction_input(hcsr501dev.gpio);
    /* 3. 获取中断号 */
    hcsr501dev.irq = gpiod_to_irq(hcsr501dev.gpio);
    /* 4. 申请中断号 */
    request_irq(hcsr501dev.irq, hcsr501_irqhandler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "irq_hcsr501", NULL);

    /* 一、注册字符设备 */
    hcsr501dev.major = register_chrdev(hcsr501dev.major, "dev_hcsr501", &hcsr501_fops);
    if (hcsr501dev.major < 0)
    {
        printk("cant register chrdev\n");
        return -EBUSY;
    }

    /*  二、创建设备类 */
    hcsr501dev.class = class_create(THIS_MODULE, "class_hcsr501");
    if (IS_ERR(hcsr501dev.class))
    {
        printk("cant create dev class\n");
        return PTR_ERR(hcsr501dev.class);
    }

    /* 三、在类下创建设备节点 */
    hcsr501dev.device = device_create(hcsr501dev.class, NULL, MKDEV(hcsr501dev.major, 0), NULL, "node_hcsr501");
    if (IS_ERR(hcsr501dev.class))
    {
        printk("cant create dev node\n");
        return PTR_ERR(hcsr501dev.device);
    }

    return 0;
}

static int hcsr501_remove(struct platform_device *pdev)
{
    printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);

    device_destroy(hcsr501dev.class, MKDEV(hcsr501dev.major, 0));
    class_destroy(hcsr501dev.class);
    unregister_chrdev(hcsr501dev.major, "dev_hcsr501");
    free_irq(hcsr501dev.irq, NULL);
    gpiod_put(hcsr501dev.gpio);

    return 0;
}

static struct platform_driver hcsr501_driver =
    {
        .probe = hcsr501_probe,
        .remove = hcsr501_remove,
        .driver = {
            .name = "alientck,hcsr501",
            .of_match_table = alientek_hcsr501,
        },
};

/* 入口函数 */
int hcsr501_init(void)
{
    init_waitqueue_head(&hcsr501dev.hcsr501_wq);

    return 0;
}
/* 出口函数 */
void hcsr501_exit(void)
{
}

module_platform_driver(hcsr501_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LiuYang");