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
#include <linux/input.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <linux/of_irq.h>
#include <linux/irq.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define IRQCNT 1
#define IRQNAME "keyinput"
#define KEY0VALUE KEY_0
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

struct keyinput_dev
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
    struct input_dev *inputdev;
    unsigned char curkeynum;
};

struct keyinput_dev keyinputdev;

// 按键中断处理函数
static irqreturn_t key0_handler(int irq, void *dev_id)
{
    struct keyinput_dev *dev = (struct keyinput_dev *)dev_id;

    dev->curkeynum = 0;
    dev->timer.data = (volatile long)dev_id;
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(10));
    return IRQ_HANDLED;
}

// 定时器服务函数，用于按键消抖
void timer_function(unsigned long arg)
{
    unsigned char iovalue;
    unsigned char keynum;
    struct irq_keydesc *keydesc;
    struct keyinput_dev *dev = (struct keyinput_dev *)arg;

    keynum = dev->curkeynum;
    keydesc = &dev->irqkeydesc[keynum];

    iovalue = gpio_get_value(keydesc->gpio);
    if (iovalue == 0)
    {
        // 上报按键值
        input_event(dev->inputdev, EV_KEY, keydesc->value, 1);
        input_sync(dev->inputdev);
    }
    else
    {
        input_event(dev->inputdev, EV_KEY, keydesc->value, 0);
        input_sync(dev->inputdev);
    }
}

static int keyio_init(void)
{
    int ret = 0;
    int i;

    /* 1. 获取设备树节点 */
    keyinputdev.node = of_find_node_by_path("/key");
    if (keyinputdev.node == NULL)
    {
        printk("can't find /key node\r\n");
        return -EINVAL;
    }

    /* 2. 先申请底层硬件资源：GPIO 与 中断 (必须放在注册 Input 前面！) */
    for (i = 0; i < KEY_NUM; i++)
    {
        /* 获取 GPIO */
        keyinputdev.irqkeydesc[i].gpio = of_get_named_gpio(keyinputdev.node, "key-gpio", i);
        if (keyinputdev.irqkeydesc[i].gpio < 0)
        {
            ret = -EINVAL;
            goto fail_gpio;
        }

        memset(keyinputdev.irqkeydesc[i].name, 0, sizeof(keyinputdev.irqkeydesc[i].name));
        sprintf(keyinputdev.irqkeydesc[i].name, "KEY%d", i);

        /* 申请 GPIO */
        ret = gpio_request(keyinputdev.irqkeydesc[i].gpio, keyinputdev.irqkeydesc[i].name);
        if (ret)
            goto fail_gpio;

        /* 设置输入方向 */
        ret = gpio_direction_input(keyinputdev.irqkeydesc[i].gpio);
        if (ret)
            goto fail_irq; // GPIO申请成功但方向设置失败，复用释放逻辑

        /* 获取中断号 */
        keyinputdev.irqkeydesc[i].irqnum = irq_of_parse_and_map(keyinputdev.node, i);
        if (keyinputdev.irqkeydesc[i].irqnum <= 0)
        {
            ret = -EINVAL;
            goto fail_irq;
        }

        keyinputdev.irqkeydesc[i].handler = key0_handler;
        keyinputdev.irqkeydesc[i].value = KEY0VALUE;

        /* 申请中断 */
        ret = request_irq(keyinputdev.irqkeydesc[i].irqnum,
                          keyinputdev.irqkeydesc[i].handler,
                          IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
                          keyinputdev.irqkeydesc[i].name,
                          &keyinputdev);
        if (ret)
            goto fail_irq;
    }

    /* 3. 初始化定时器 (规范：提前绑定好 data 参数) */
    init_timer(&keyinputdev.timer);
    keyinputdev.timer.function = timer_function;
    keyinputdev.timer.data = (unsigned long)&keyinputdev;

    /* 4. 底层全部就绪后，最后申请并注册 Input 设备！ */
    keyinputdev.inputdev = input_allocate_device();
    if (!keyinputdev.inputdev)
    {
        ret = -ENOMEM;
        goto fail_input_alloc;
    }
    keyinputdev.inputdev->name = IRQNAME;
    keyinputdev.inputdev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP);
    input_set_capability(keyinputdev.inputdev, EV_KEY, KEY_0);

    ret = input_register_device(keyinputdev.inputdev);
    if (ret)
        goto fail_input_reg;

    return 0;

/* ================= 倒序资源回收机制 ================= */
fail_input_reg:
    input_free_device(keyinputdev.inputdev); // 注册失败则释放内存
fail_input_alloc:
    i = KEY_NUM; // 准备释放所有已申请的硬件资源
fail_irq:
    while (i--)
    {
        free_irq(keyinputdev.irqkeydesc[i].irqnum, &keyinputdev);
        gpio_free(keyinputdev.irqkeydesc[i].gpio);
    }
    return ret;
fail_gpio:
    while (i--)
    {
        // 如果是在某次循环的 gpio_request 前失败，需要释放之前成功的 GPIO
        gpio_free(keyinputdev.irqkeydesc[i].gpio);
    }
    return ret;
}

static int __init keyinputdev_init(void)
{
    return keyio_init();
}

static void __exit keyinputdev_exit(void)
{
    unsigned int i = 0;
    del_timer_sync(&keyinputdev.timer);

    for (i = 0; i < KEY_NUM; i++)
    {
        free_irq(keyinputdev.irqkeydesc[i].irqnum, &keyinputdev);
        gpio_free(keyinputdev.irqkeydesc[i].gpio);
    }

    input_unregister_device(keyinputdev.inputdev);
    keyinputdev.inputdev = NULL;
}

module_init(keyinputdev_init);
module_exit(keyinputdev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LY");