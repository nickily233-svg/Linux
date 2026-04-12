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
#include <linux/i2c.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include "ap3216creg.h"

#define ap3216c_cnt 1
#define ap3216c_name "ap3216c"

struct ap3216c_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *node;
    int major;
    int minor;
    void *private_data;
    unsigned short ir, als, ps;
};

struct ap3216c_dev ap3216cdev;

/*
 * @description : 从ap3216c 读取多个寄存器数据
 * @param – dev : ap3216c 设备
 * @param – reg : 要读取的寄存器首地址
 * @param – val : 读取到的数据
 * @param – len : 要读取的数据长度
 * @return : 操作结果
 */
static int ap3216c_read_regs(struct ap3216c_dev *dev, u8 reg, void *val, int len)
{
    int ret;
    struct i2c_msg msg[2];
    struct i2c_client *client = (struct i2c_client *)dev->private_data;

    msg[0].addr = client->addr;
    msg[0].flags = 0;
    msg[0].buf = &reg;
    msg[0].len = 1;

    msg[1].addr = client->addr;
    msg[1].flags = I2C_M_RD;
    msg[1].buf = val;
    msg[1].len = len;

    ret = i2c_transfer(client->adapter, msg, 2);
    if (ret == 2)
    {
        ret = 0;
    }
    else
    {
        printk("i2c read failed\r\n");
        ret = -EREMOTEIO;
    }

    return ret;
}

/*
 * @description : 向ap3216c 多个寄存器写入数据
 * @param – dev : ap3216c 设备
 * @param – reg : 要写入的寄存器首地址
 * @param – val : 要写入的数据缓冲区
 * @param – len : 要写入的数据长度
 * @return : 操作结果
 */

static s32 ap3216c_write_regs(struct ap3216c_dev *dev, u8 reg, u8 *buf, u8 len)
{
    u8 b[256];
    struct i2c_msg msg;
    struct i2c_client *client = (struct i2c_client *)dev->private_data;

    b[0] = reg;
    memcpy(&b[1], buf, len);

    msg.addr = client->addr;
    msg.flags = 0;

    msg.buf = b;
    msg.len = len + 1;

    return i2c_transfer(client->adapter, &msg, 1);
}

/*
 * @description : 读取ap3216c 指定寄存器值，读取一个寄存器
 * @param – dev : ap3216c 设备
 * @param – reg : 要读取的寄存器
 * @return : 读取到的寄存器值
 */
static unsigned char ap3216c_read_reg(struct ap3216c_dev *dev, u8 reg)
{
    u8 data = 0;
    ap3216c_read_regs(dev, reg, &data, 1);
    return data;
}

/*
 * @description : 向ap3216c 指定寄存器写入指定的值，写一个寄存器
 * @param – dev : ap3216c 设备
 * @param – reg : 要写的寄存器
 * @param – data : 要写入的值
 * @return : 无
 */
static void ap3216c_write_reg(struct ap3216c_dev *dev, u8 reg, u8 data)
{
    ap3216c_write_regs(dev, reg, &data, 1);
}

/*
 * @description : 读取AP3216C 的数据，读取原始数据，包括ALS,PS 和IR,
 * :同时打开ALS,IR+PS 的话两次数据读取的间隔要大于112.5ms
 * @param - ir : ir 数据
 * @param - ps : ps 数据
 * @param - ps : als 数据
 * @return : 无。
 */
void ap3216c_readdata(struct ap3216c_dev *dev)
{
    unsigned char i = 0;
    unsigned char buf[6];

    /*循环读取所有传感器的数据*/
    for (i = 0; i < 6; i++)
    {
        buf[i] = ap3216c_read_reg(dev, AP3216C_IRDATALOW + i);
    }

    if (buf[0] & 0x80) /*IR_OF位为1，则数据无效*/
    {
        dev->ir = 0;
    }
    else
    {
        dev->ir = ((unsigned char)buf[1] << 2 | (buf[0] & 0X03));
    }

    dev->als = ((unsigned char)buf[3] << 8) | buf[2];

    if (buf[4] & 0X40)
    {
        dev->ps = 0;
    }
    else
    {
        dev->ps = (((unsigned short)(buf[5] & 0x3F)) << 4) | (buf[4] & 0x0F);
    }
}

static int ap3216c_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &ap3216cdev;

    ap3216c_write_reg(&ap3216cdev, AP3216C_SYSTEMCONG, 0x04);
    mdelay(50);
    ap3216c_write_reg(&ap3216cdev, AP3216C_SYSTEMCONG, 0x03);

    return 0;
}

static ssize_t ap3216c_read(struct file *filp, char __user *buf, size_t cnt, loff_t *off)
{
    short data[3];
    long err = 0;
    struct ap3216c_dev *dev = (struct ap3216c_dev *)filp->private_data;

    ap3216c_readdata(dev);

    data[0] = dev->ir;
    data[1] = dev->als;
    data[2] = dev->ps;

    err = copy_to_user(buf, data, sizeof(data));
    return 0;
}

static int ap3216c_release(struct inode *inode, struct file *filp)
{
    return 0;
}

const struct file_operations ap3216c_ops = {
    .owner = THIS_MODULE,
    .open = ap3216c_open,
    .read = ap3216c_read,
    .release = ap3216c_release,
};

static int ap3216c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    ap3216cdev.private_data = client;

    if (ap3216cdev.major)
    {
        ap3216cdev.devid = MKDEV(ap3216cdev.major, 0);
        register_chrdev_region(ap3216cdev.devid, ap3216c_cnt, ap3216c_name);
    }
    else
    {
        alloc_chrdev_region(&ap3216cdev.devid, 0, ap3216c_cnt, ap3216c_name);
        ap3216cdev.major = MAJOR(ap3216cdev.devid);
    }

    cdev_init(&ap3216cdev.cdev, &ap3216c_ops);

    cdev_add(&ap3216cdev.cdev, ap3216cdev.devid, ap3216c_cnt);

    ap3216cdev.class = class_create(THIS_MODULE, ap3216c_name);
    if (IS_ERR(ap3216cdev.class))
    {
        return PTR_ERR(ap3216cdev.class);
    }

    ap3216cdev.device = device_create(ap3216cdev.class, NULL, ap3216cdev.devid, NULL, ap3216c_name);
    if (IS_ERR(ap3216cdev.device))
    {
        return PTR_ERR(ap3216cdev.device);
    }

    return 0;
}

static int ap3216c_remove(struct i2c_client *client)
{

    device_destroy(ap3216cdev.class, ap3216cdev.devid);
    class_destroy(ap3216cdev.class);
    cdev_del(&ap3216cdev.cdev);
    unregister_chrdev_region(ap3216cdev.devid, ap3216c_cnt);
    return 0;
}

static const struct i2c_device_id ap3216c_id[] = {
    {"alientek,ap3216c", 0},
    {}};

static const struct of_device_id ap3216c_of_match[] = {
    {.compatible = "alientek,ap3216c"},
    {/* Sentinel */},
};

/* i2c 驱动结构体 */
struct i2c_driver ap3216c_driver = {
    .probe = ap3216c_probe,
    .remove = ap3216c_remove,
    .driver = {
        .owner = THIS_MODULE,
        .name = "ap3216c",
        .of_match_table = ap3216c_of_match,
    },
    .id_table = ap3216c_id,
};

static int __init ap3216c_init(void)
{
    return i2c_add_driver(&ap3216c_driver);
}

static void __exit ap3216c_exit(void)
{
    i2c_del_driver(&ap3216c_driver);
}
module_init(ap3216c_init);
module_exit(ap3216c_exit);

MODULE_LICENSE("GPL");