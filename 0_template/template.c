#include <linux/>

#define

struct xxx_dev
{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct device_node *nd;
};

struct xxx_dev xxx;

static int xxx_open(struct inode *inode, struct file *filp)
{
    struct xxx_dev *dev = &xxx;
    return 0;
}

static ssize_t xxx_read(struct file *filp, char __user *buf, size_t cnt, loff_t *offt)
{
    return 0;
}

static int xxx_release(struct inode *inode, struct file *filp)
{
    return 0;
}

struct file_operations xxx_fops = {
    .owner = THIS_MODULE,
    .open = xxx_open,
    .read = xxx_read,
    .release = xxx_release,
};

static int __init xxx_init()
{
    return 0;
}

static void __exit xxx_exit()
{
}

module_init(xxx_init);
module_exit(xxx_exit);

MODULE_LICENSE("GPL");