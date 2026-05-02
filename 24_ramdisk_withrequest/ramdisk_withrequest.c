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
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define RAMDISK_SIZE (2 * 1024 * 1024) /* 容量大小为2MB */
#define RAMDISK_NAME "ramdisk"         /* 名字 */
#define RADMISK_MINOR 3                /* 表示有三个磁盘分区 */

struct ramdisk_dev
{
    int major;                           /*主设备号*/
    u8 *ramdiskbuf;                      /*内存控件，用于模块块设备*/
    spinlock_t spinlock;                 /*自旋锁*/
    struct gendisk *gendisk;             /*gendisk*/
    struct request_queue *request_queue; /*请求队列*/
};

struct ramdisk_dev ramdiskdev;

static int ramdisk_open(struct block_device *dev, fmode_t mode)
{
    printk("ramdisk open\n");
    return 0;
}

static void ramdisk_release(struct gendisk *disk, fmode_t mode)
{
    printk("ramdisk release\n");
}

static int ramdisk_getgo(struct block_device *dev, struct hd_geometry *geo)
{
    /* 这是相对于机械硬盘的概念 */
    geo->heads = 2;                               /* 磁头 */
    geo->cylinders = 32;                          /* 柱面 */
    geo->sectors = RAMDISK_SIZE / (2 * 32 * 512); /* 磁道上的扇区数量 */
    return 0;
}

static void ramdisk_transfer(struct request *req)
{
    unsigned long start = blk_rq_pos(req) << 9; /*blk_rq_pos 获取到的是扇区地址，左移9 位转换为字节地址*/
    unsigned long len = blk_rq_cur_bytes(req);  /*大小*/
    /* bio 中的数据缓冲区
     * 读：从磁盘读取到的数据存放到buffer 中
     * 写：buffer 保存这要写入磁盘的数据
     */
    void *buffer = bio_data(req->bio);
    if (rq_data_dir(req) == READ) /* 读数据 */
        memcpy(buffer, ramdiskdev.ramdiskbuf + start, len);
    else if (rq_data_dir(req) == WRITE) /* 写数据 */
        memcpy(ramdiskdev.ramdiskbuf + start, buffer, len);
}

/*请求处理函数,主要工作就是依次处理请求队列中的所有请求*/
void ramdisk_request_fn(struct request_queue *q)
{
    int err = 0;
    struct request *req;
    /* 循环处理请求队列中的每个请求 */
    req = blk_fetch_request(q);
    while (req != NULL)
    {
        /* 针对请求做具体的传输处理 */
        ramdisk_transfer(req);
        /* 判断是否为最后一个请求，如果不是的话就获取下一个请求
         * 循环处理完请求队列中的所有请求。
         */
        if (!__blk_end_request_cur(req, err))
            req = blk_fetch_request(q);
    }
}

struct block_device_operations ramdisk_ops = {
    .owner = THIS_MODULE,
    .open = ramdisk_open,
    .release = ramdisk_release,
    .getgeo = ramdisk_getgo,
};

static int __init ramdisk_init(void)
{
    int ret = 0;

    /*#1.申请ramdisk内存*/
    ramdiskdev.ramdiskbuf = kzalloc(RAMDISK_SIZE, GFP_KERNEL);
    if (ramdiskdev.ramdiskbuf == NULL)
    {
        ret = -EINVAL;
        goto ram_fail;
    }
    /*#2.初始化自旋锁*/
    spin_lock_init(&ramdiskdev.spinlock);
    /*#3.注册块设备*/
    ramdiskdev.major = register_blkdev(0, RAMDISK_NAME);
    if (ramdiskdev.major < 0)
    {
        goto register_blkdev_fail;
    }
    printk("ramdisk major = %d\n", ramdiskdev.major);
    /*#4.分配并且初始化gendisk*/
    ramdiskdev.gendisk = alloc_disk(RADMISK_MINOR);
    if (!ramdiskdev.gendisk)
    {
        ret = -EINVAL;
        goto gendisk_alloc_fail;
    }
    /*#5.分配并初始化请求队列*/
    ramdiskdev.request_queue = blk_init_queue(ramdisk_request_fn, &ramdiskdev.spinlock);
    if (!ramdiskdev.request_queue)
    {
        ret = -EINVAL;
        goto blk_init_fail;
    }
    /*#6.注册块设备*/
    ramdiskdev.gendisk->major = ramdiskdev.major;
    ramdiskdev.gendisk->first_minor = 0;
    ramdiskdev.gendisk->fops = &ramdisk_ops;
    ramdiskdev.gendisk->private_data = &ramdiskdev;
    ramdiskdev.gendisk->queue = ramdiskdev.request_queue;
    sprintf(ramdiskdev.gendisk->disk_name, RAMDISK_NAME);
    set_capacity(ramdiskdev.gendisk, RAMDISK_SIZE / 512);

    add_disk(ramdiskdev.gendisk);

    return 0;

blk_init_fail:
    put_disk(ramdiskdev.gendisk);
gendisk_alloc_fail:
    unregister_blkdev(ramdiskdev.major, RAMDISK_NAME);
register_blkdev_fail:
    kfree(ramdiskdev.ramdiskbuf); /* 释放内存 */
ram_fail:
    return ret;
}

static void __exit ramdisk_exit(void)
{
    /*释放disk*/
    del_gendisk(ramdiskdev.gendisk);
    put_disk(ramdiskdev.gendisk);
    /*清楚请求队列*/
    blk_cleanup_queue(ramdiskdev.request_queue);
    /*注销块设备*/
    unregister_blkdev(ramdiskdev.major, RAMDISK_NAME);
    /*释放内存*/
    kfree(ramdiskdev.ramdiskbuf);
}

module_init(ramdisk_init);
module_exit(ramdisk_exit);

MODULE_LICENSE("GPL");