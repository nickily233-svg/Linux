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
#include <linux/fs.h>
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
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/sched.h>

#define DEVCNT 1            /*设备号数量*/
#define DEVNAME "noblockio" /*设备名*/
#define KEY0VALUE 0X01      /*KEY0的按键值*/
#define INVALKEY 0xFF       /*无效按键值*/
#define KEYNUM 1            /*按键数量*/

/*中断io描述结构体*/
struct irq_keydesc
{
    int gpio;                            /*中断gpio*/
    int irqnum;                          /*中断号*/
    unsigned char value;                 /*按键对应的键值*/
    char name[10];                       /*名字*/
    irqreturn_t (*handler)(int, void *); /*中断服务函数*/
};

struct blockio_dev
{
    dev_t devid;                           /*设备号ID*/
    struct cdev cdev;                      /*字符设备cdev*/
    struct class *class;                   /*类*/
    struct device *device;                 /*设备*/
    struct device_node *node;              /*设备节点*/
    struct timer_list timer;               /*定时器*/
    struct irq_keydesc irqkeydesc[KEYNUM]; /*按键中断描述数组*/
    unsigned char curkeynum;               /*当前按键号*/
    int major;                             /*主设备号*/
    int minor;                             /*次设备号*/
    atomic_t ifreleasekey;                 /*原子变量-是否松开按键*/
    atomic_t keyvalue;                     /*原子变量-按键值*/
    wait_queue_head_t read_wait;           /*读等待队列头*/
};

struct blockio_dev blockio;

// 按键中断服务函数，用于开启定时器
static irqreturn_t key0_handler(int irq, void *dev_id)
{
    struct blockio_dev *dev = (struct blockio_dev *)dev_id;

    dev->curkeynum = 0;
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(10));
    return IRQ_HANDLED;
}

// 定时器服务函数
static void timer_function(unsigned long arg)
{
    struct irq_keydesc *keydesc;
    unsigned char value;
    struct blockio_dev *dev = (struct blockio_dev *)arg;
    unsigned char num;

    num = dev->curkeynum;
    keydesc = &dev->irqkeydesc[num];

    value = gpio_get_value(keydesc->gpio);
    if (value == 0)
    {
        atomic_set(&dev->keyvalue, keydesc->value);
    }
    else
    {
        atomic_set(&dev->keyvalue, 0x80 | keydesc->value);
        atomic_set(&dev->ifreleasekey, 1);
    }

    /*唤醒进程*/
    if (atomic_read(&dev->ifreleasekey))
    {
        /*wake up*/
        wake_up_interruptible(&dev->read_wait);
    }
}

static int keyio_init(void) /*按键gpio初始化*/
{
    int i = 0;
    /*获取设备节点*/
    blockio.node = of_find_node_by_path("/key");
    if (blockio.node == NULL)
    {
        printk("cant get node\r\n");
        return -EINVAL;
    }

    /*创建定时器并设置定时器服务函数*/
    init_timer(&blockio.timer);
    blockio.timer.function = timer_function;
    blockio.timer.data = (unsigned long)&blockio;

    for (i = 0; i < KEYNUM; i++)
    {
        int ret = 0;
        /*初始化按键名*/
        memset(blockio.irqkeydesc[i].name, 0, sizeof(blockio.irqkeydesc[i].name));
        sprintf(blockio.irqkeydesc[i].name, "KEY%d", i); /*赋名*/
        /*获取gpio*/
        blockio.irqkeydesc[i].gpio = of_get_named_gpio(blockio.node, "key-gpio", i);
        if (blockio.irqkeydesc[i].gpio < 0)
        {
            printk("cant get gpio\r\n");
            return -EFAULT;
        }
        /*申请gpio*/
        ret = gpio_request(blockio.irqkeydesc[i].gpio, blockio.irqkeydesc[i].name);
        if (ret < 0)
        {
            printk("cant request gpio\r\n");
            return -EFAULT;
        }
        /*设置电气属性*/
        ret = gpio_direction_input(blockio.irqkeydesc[i].gpio);
        if (ret < 0)
        {
            printk("cant set gpio\r\n");
            return -EFAULT;
        }
        //
        ret = blockio.irqkeydesc[i].irqnum = irq_of_parse_and_map(blockio.node, i);
        if (ret <= 0)
        {
            printk("irq cant be set \r\n");
            return -EFAULT;
        }
        printk("key%d irqnum = %d gpio = %d\r\n", i, blockio.irqkeydesc[i].irqnum, blockio.irqkeydesc[i].gpio);
    }

    /*初始化key0按键中断*/
    blockio.irqkeydesc[0].handler = key0_handler;
    blockio.irqkeydesc[0].value = KEY0VALUE;

    /*申请中断*/
    for (i = 0; i < KEYNUM; i++)
    {
        int ret = request_irq(blockio.irqkeydesc[i].irqnum, blockio.irqkeydesc[i].handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, blockio.irqkeydesc[i].name, &blockio);
        if (ret < 0)
        {
            printk("request irq failed\r\n");
            return -EFAULT;
        }
    }

    init_waitqueue_head(&blockio.read_wait); /*初始化等待队列头*/

    return 0;
}

unsigned int noblockio_poll(struct file *filp, struct poll_table_struct *wait) /*驱动层poll函数，应用层调用poll就是执行*/
{
    unsigned int mask = 0;
    struct blockio_dev *dev = (struct blockio_dev *)filp->private_data;

    poll_wait(filp, &dev->read_wait, wait); /*将等待队列头添加到poll_table中*/
    if (atomic_read(&dev->ifreleasekey))
    {
        mask = POLLIN | POLLRDNORM; /*按键有效就访问POLLIN事件*/
    }
    return mask;
}

static int blockio_open(struct inode *inode, struct file *filp)
{
    struct blockio_dev *dev = &blockio;
    filp->private_data = dev;
    return 0;
}

static ssize_t blockio_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
    int ret = 0;
    unsigned char keyvalue = 0;
    unsigned char ifreleasekey = 0;
    struct blockio_dev *dev = (struct blockio_dev *)filp->private_data;
#if 0
    DECLARE_WAITQUEUE(wait, current); /*定义并初始化任务队列*/

    if (atomic_read(&dev->ifreleasekey) == 0) /*如果此时按键没有按下*/
    {
        add_wait_queue(&dev->read_wait, &wait); /*添加等待队列头到等待队列wait*/
        set_current_state(TASK_INTERRUPTIBLE);  /*设置任务状态*/
        schedule();                             /*进行一次任务切换*/
        if (signal_pending(current))            /*判断是否为信号引起的唤醒*/
        {
            ret = -ERESTARTSYS;
            goto wait_error;
        }
        set_current_state(TASK_RUNNING);           /*设置为运行状态*/
        remove_wait_queue(&dev->read_wait, &wait); /*将等待队列头从等待队列中移除*/
    }
#endif

    if (filp->f_flags & O_NONBLOCK) /*判断是不是非阻塞式读取访问，是就判断按键是否有效，没有就返回*/
    {
        if (atomic_read(&dev->ifreleasekey) == 0)
        {
            return -EAGAIN;
        }
    }
    else
    {
        /*加入等待队列，等待被唤醒，也就是有按键按下*/
        ret = wait_event_interruptible(dev->read_wait, atomic_read(&dev->ifreleasekey));
        if (ret)
        {
            goto wait_error;
        }
    }

    keyvalue = atomic_read(&dev->keyvalue);
    ifreleasekey = atomic_read(&dev->ifreleasekey);
    if (ifreleasekey)
    { /* 有按键按下 */
        if (keyvalue & 0x80)
        {
            keyvalue &= ~0x80;
            ret = copy_to_user(buf, &keyvalue, sizeof(keyvalue));
        }
        else
        {
            goto data_error;
        }
        atomic_set(&dev->ifreleasekey, 0); /* 按下标志清零 */
    }
    else
    {
        goto data_error;
    }
    return 0;
data_error:
    return -EINVAL;

wait_error:
#if 0
    set_current_state(TASK_RUNNING);
    remove_wait_queue(&dev->read_wait, &wait);
#endif
    return -EINVAL;
}

static struct file_operations block_fops = {
    .owner = THIS_MODULE,
    .open = blockio_open,
    .read = blockio_read,
    .poll = noblockio_poll,
};

static int __init blockio_init(void) /*入口函数*/
{
    int ret = 0;
    /*申请cdev*/
    if (blockio.major)
    {
        // 静态分配设备号
        blockio.devid = MKDEV(blockio.major, 0);
        ret = register_chrdev_region(blockio.devid, DEVCNT, DEVNAME);
        if (ret < 0)
        {
            printk("cant register chrdev\r\n");
            return -EINVAL;
        }
    }
    else
    {
        /*动态分配设备号*/
        ret = alloc_chrdev_region(&blockio.devid, 0, DEVCNT, DEVNAME);
        if (ret < 0)
        {
            printk("cant register chrdev\r\n");
            return -EINVAL;
        }
        blockio.major = MAJOR(blockio.devid);
        blockio.minor = MINOR(blockio.devid);
    }
    printk("major = %d minor = %d\r\n", blockio.major, blockio.minor);

    /*dev_init 绑定devid和fops*/
    cdev_init(&blockio.cdev, &block_fops);

    /*cdev_add 添加cdev 绑定fops和cdev*/
    ret = cdev_add(&blockio.cdev, blockio.devid, DEVCNT);
    if (ret < 0)
    {
        printk("cant add cdev\r\n");
        return -EINVAL;
    }

    /*添加类和设备*/
    blockio.class = class_create(THIS_MODULE, DEVNAME);
    if (IS_ERR(blockio.class))
    {
        return PTR_ERR(blockio.class);
    }

    blockio.device = device_create(blockio.class, NULL, blockio.devid, NULL, DEVNAME);
    if (IS_ERR(blockio.device))
    {
        return PTR_ERR(blockio.device);
    }

    /*初始化原子变量*/
    atomic_set(&blockio.ifreleasekey, 0);
    atomic_set(&blockio.keyvalue, 0);

    // io初始化
    ret = keyio_init();
    if (ret < 0)
    {
        printk("cant init key io\r\n");
        return -EINVAL;
    }
    return 0;
}

static void __exit blockio_exit(void) /*出口函数*/
{
    int i = 0;
    /*注销设备、类、cdev、设备号、gpio*/
    of_node_put(blockio.node);
    del_timer_sync(&blockio.timer);
    for (i = 0; i < KEYNUM; i++)
    {
        gpio_free(blockio.irqkeydesc[i].gpio);
        free_irq(blockio.irqkeydesc[i].irqnum, &blockio);
    }
    device_destroy(blockio.class, blockio.devid);
    class_destroy(blockio.class);
    cdev_del(&blockio.cdev);
    unregister_chrdev_region(blockio.devid, DEVCNT);
}

module_init(blockio_init);
module_exit(blockio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LY");