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
#include <linux/of_irq.h>
#include <linux/irq.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define IRQCNT 1
#define IRQNAME "irq"
#define KEY0VALUE 0X01
#define INVALKEY 0X0FF
#define KEY_NUM 1

struct irq_keydesc
{
    int gpio;
    int irqnum;
    unsigned char value;
    char name[10];
    irqreturn_t (*handler)(int, void *);
};

struct imx6ullirq_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *node;
    int major;
    int minor;
    atomic_t keyvalue;
    atomic_t releasekey;
    struct timer_list timer;
    struct irq_keydesc irqkeydesc[KEY_NUM];
    unsigned char curkeynum;
};

struct imx6ullirq_dev imx6ullirq;

// 按键中断处理函数
static irqreturn_t key0_handler(int irq, void *dev_id)
{
    struct imx6ullirq_dev *dev = (struct imx6ullirq_dev *)dev_id;

    dev->curkeynum = 0;
    dev->timer.data = (unsigned long)dev_id;
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(10));
    return IRQ_HANDLED;
}

// 定时器服务函数，用于按键消抖
void timer_function(unsigned long arg)
{
    unsigned char iovalue;
    unsigned char keynum;
    struct irq_keydesc *keydesc;
    struct imx6ullirq_dev *dev = (struct imx6ullirq_dev *)arg;

    keynum = dev->curkeynum;
    keydesc = &dev->irqkeydesc[keynum];

    iovalue = gpio_get_value(keydesc->gpio);
    if (iovalue == 0)
    {
        atomic_set(&dev->keyvalue, keydesc->value);
    }
    else
    {
        atomic_set(&dev->keyvalue, 0X80 | keydesc->value);
        atomic_set(&dev->releasekey, 1);
    }
}
#if 0
static int keyio_init(void)
{
    unsigned char i = 0;

    int ret = 0;
    // 获取设备节点
    imx6ullirq.node = of_find_node_by_path("/key");
    if (imx6ullirq.node == NULL)
    {
        printk("cant get node\r\n");
        return -EINVAL;
    }

    // 获取gpio
    for (i = 0; i < KEY_NUM; i++)
    {
        imx6ullirq.irqkeydesc[i].gpio = of_get_named_gpio(imx6ullirq.node, "key-gpio", i);
        if (imx6ullirq.irqkeydesc[i].gpio < 0)
        {
            printk("cant get gpio\r\n");
            return -EINVAL;
        }
    }

    for (i = 0; i < KEY_NUM; i++)
    {
        memset(imx6ullirq.irqkeydesc[i].name, 0, sizeof(imx6ullirq.irqkeydesc[i].name));
        sprintf(imx6ullirq.irqkeydesc[i].name, "KEY%d\r\n", i);
        // 申请gpio
        gpio_request(imx6ullirq.irqkeydesc[i].gpio, imx6ullirq.irqkeydesc[i].name);
        // 配置电气属性
        gpio_direction_input(imx6ullirq.irqkeydesc[i].gpio);
        // 如果设备节点有中断属性
        imx6ullirq.irqkeydesc[i].irqnum = irq_of_parse_and_map(imx6ullirq.node, i);
// 使用gpio
#if 0
imx6ullirq.irqkeydesc[i].irqnum = gpio_to_irq(imx6ullirq.irqkeydesc[i].gpio);
#endif
        printk("key%d gpio = %d irqnum =%d\r\n", i, imx6ullirq.irqkeydesc[i].gpio, imx6ullirq.irqkeydesc[i].irqnum);

        // 创建定时器
        init_timer(&imx6ullirq.timer);
        imx6ullirq.timer.function = timer_function;

        // 申请中断
        imx6ullirq.irqkeydesc[i].handler = key0_handler;
        imx6ullirq.irqkeydesc[i].value = KEY0VALUE;

        for (i = 0; i < KEY_NUM; i++)
        {
            ret = request_irq(imx6ullirq.irqkeydesc[i].irqnum, imx6ullirq.irqkeydesc[i].handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, imx6ullirq.irqkeydesc[i].name, &imx6ullirq);
            if (ret < 0)
            {
                printk("irq %d request failed\r\n", imx6ullirq.irqkeydesc[i].irqnum);
                return -EFAULT;
            }
        }
    }

    return 0;
}
#endif

static int keyio_init(void)
{
    int ret = 0;
    int i;

    /* 1. 获取设备树节点 */
    imx6ullirq.node = of_find_node_by_path("/key");
    if (imx6ullirq.node == NULL)
    {
        printk("can't find /key node\r\n");
        return -EINVAL;
    }

    /* 2. 初始化定时器（整个设备只需要一个） */
    init_timer(&imx6ullirq.timer);
    imx6ullirq.timer.function = timer_function;
    imx6ullirq.timer.data = (unsigned long)&imx6ullirq;

    /* 3. 逐个按键初始化：GPIO、名字、方向、IRQ号、处理函数、键值 */
    for (i = 0; i < KEY_NUM; i++)
    {

        /* 3.1 从设备树获取 GPIO */
        imx6ullirq.irqkeydesc[i].gpio =
            of_get_named_gpio(imx6ullirq.node, "key-gpio", i);
        if (imx6ullirq.irqkeydesc[i].gpio < 0)
        {
            printk("can't get key%d gpio\r\n", i);
            ret = -EINVAL;
            goto fail_gpio;
        }

        /* 3.2 设置按键名字 */
        memset(imx6ullirq.irqkeydesc[i].name, 0,
               sizeof(imx6ullirq.irqkeydesc[i].name));
        sprintf(imx6ullirq.irqkeydesc[i].name, "KEY%d", i);

        /* 3.3 申请 GPIO */
        ret = gpio_request(imx6ullirq.irqkeydesc[i].gpio,
                           imx6ullirq.irqkeydesc[i].name);
        if (ret)
        {
            printk("gpio %d request failed\r\n",
                   imx6ullirq.irqkeydesc[i].gpio);
            goto fail_gpio;
        }

        /* 3.4 配置为输入 */
        ret = gpio_direction_input(imx6ullirq.irqkeydesc[i].gpio);
        if (ret)
        {
            printk("gpio %d direction input failed\r\n",
                   imx6ullirq.irqkeydesc[i].gpio);
            goto fail_gpio;
        }

        /* 3.5 获取中断号 */
        imx6ullirq.irqkeydesc[i].irqnum =
            irq_of_parse_and_map(imx6ullirq.node, i);
        if (imx6ullirq.irqkeydesc[i].irqnum <= 0)
        {
            printk("key%d irq parse failed\r\n", i);
            ret = -EINVAL;
            goto fail_gpio;
        }

        /* 3.6 绑定中断处理函数和键值 */
        imx6ullirq.irqkeydesc[i].handler = key0_handler;
        imx6ullirq.irqkeydesc[i].value = KEY0VALUE;

        printk("key%d: gpio=%d irq=%d\r\n",
               i,
               imx6ullirq.irqkeydesc[i].gpio,
               imx6ullirq.irqkeydesc[i].irqnum);
    }

    /* 4. 逐个申请中断 */
    for (i = 0; i < KEY_NUM; i++)
    {
        ret = request_irq(imx6ullirq.irqkeydesc[i].irqnum,
                          imx6ullirq.irqkeydesc[i].handler,
                          IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                          imx6ullirq.irqkeydesc[i].name,
                          &imx6ullirq);
        if (ret)
        {
            printk("irq %d request failed\r\n",
                   imx6ullirq.irqkeydesc[i].irqnum);
            goto fail_irq;
        }
    }

    return 0;

fail_irq:
    while (i--)
    {
        free_irq(imx6ullirq.irqkeydesc[i].irqnum, &imx6ullirq);
    }
    i = KEY_NUM;

fail_gpio:
    while (i--)
    {
        if (gpio_is_valid(imx6ullirq.irqkeydesc[i].gpio))
            gpio_free(imx6ullirq.irqkeydesc[i].gpio);
    }

    return ret;
}

static int irq_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &imx6ullirq;
    return 0;
}

static ssize_t irq_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
    int ret = 0;
    unsigned char releasekey = 0;
    unsigned char keyvalue = 0;

    struct imx6ullirq_dev *dev = filp->private_data;
    releasekey = atomic_read(&dev->releasekey);
    keyvalue = atomic_read(&dev->keyvalue);

    if (cnt < sizeof(keyvalue))
    {
        return -EINVAL;
    }

    if (keyvalue)
    {
        if (!releasekey)
        {
            return -EINVAL;
        }
        if (keyvalue & 0X80)
        {
            keyvalue &= ~0X80;
            ret = copy_to_user(buf, &keyvalue, sizeof(keyvalue));
            if (ret)
            {
                return -EFAULT;
            }
        }
        else
        {
            goto data_error;
        }
        atomic_set(&dev->releasekey, 0);
        atomic_set(&dev->keyvalue, 0);
    }
    else
    {
        goto data_error;
    }
    return sizeof(keyvalue);

data_error:
    return -EINVAL;
}

struct file_operations irq_fops = {
    .owner = THIS_MODULE,
    .open = irq_open,
    .read = irq_read,
};

static int __init imx6ullirq_init(void)
{
    // 构建设备号
    if (imx6ullirq.major)
    {
        imx6ullirq.devid = MKDEV(imx6ullirq.major, 0);
        register_chrdev_region(imx6ullirq.devid, IRQCNT, IRQNAME);
    }
    else
    {
        alloc_chrdev_region(&imx6ullirq.devid, 0, IRQCNT, IRQNAME);
        imx6ullirq.major = MAJOR(imx6ullirq.devid);
        imx6ullirq.minor = MINOR(imx6ullirq.devid);
    }
    printk("major = %d minor = %d\r\n", imx6ullirq.major, imx6ullirq.minor);

    // 注册 添加cdev
    cdev_init(&imx6ullirq.cdev, &irq_fops);
    cdev_add(&imx6ullirq.cdev, imx6ullirq.devid, IRQCNT);

    // 创建类和设备
    imx6ullirq.class = class_create(THIS_MODULE, IRQNAME);
    if (IS_ERR(imx6ullirq.class))
    {
        return PTR_ERR(imx6ullirq.class);
    }
    imx6ullirq.device = device_create(imx6ullirq.class, NULL, imx6ullirq.devid, NULL, IRQNAME);
    if (IS_ERR(imx6ullirq.device))
    {
        return PTR_ERR(imx6ullirq.device);
    }

    // 初始化按键
    atomic_set(&imx6ullirq.keyvalue, 0);
    atomic_set(&imx6ullirq.releasekey, 0);
    keyio_init();
    return 0;
}

static void __exit imx6ullirq_exit(void)
{
    unsigned int i = 0;
    del_timer_sync(&imx6ullirq.timer);

    for (i = 0; i < KEY_NUM; i++)
    {
        free_irq(imx6ullirq.irqkeydesc[i].irqnum, &imx6ullirq);
        gpio_free(imx6ullirq.irqkeydesc[i].gpio);
    }

    // 注销类和设备
    device_destroy(imx6ullirq.class, imx6ullirq.devid);
    class_destroy(imx6ullirq.class);
    cdev_del(&imx6ullirq.cdev);
    of_node_put(imx6ullirq.node);
    unregister_chrdev_region(imx6ullirq.devid, IRQCNT);
}

module_init(imx6ullirq_init);
module_exit(imx6ullirq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LY");