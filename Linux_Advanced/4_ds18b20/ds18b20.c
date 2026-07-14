#include "asm/irqflags.h"
#include "asm/processor.h"
#include "asm/uaccess.h"
#include "linux/irqflags.h"
#include <asm/processor.h> // 用于 cpu_relax() 函数（部分架构也包含在 linux/processor.h 中）
#include <cerrno>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/timekeeping.h> // 用于 ktime_get_boot_ns() 和 ktime_get_boottime_ns() 函数
#include <linux/uaccess.h>
#include <linux/version.h> // 用于 LINUX_VERSION_CODE 和 KERNEL_VERSION 宏

struct ds18b20_dev {
  dev_t ds18b20_devid;
  struct class *ds18b20_class;
  struct device *ds18b20_device;
  int ds18b20_irq;
  struct gpio_desc *ds18b20_gpio;
  int major;
  wait_queue_head_t ds18b20_wq;
  int ds18b20_edge_cnt;
  int ds18b20_data;
  int ds18b20_edge_time[100];
};

struct ds18b20_dev ds18b20dev;

static void ds18b20_delay(unsigned long us) {
  int start = 0, end = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 0, 0)
  start = ktime_get_boot_ns();
  end = start + (u64)us * 1000ULL;
  while (ktime_get_boot_ns() < end) {
    cpu_relax();
  }
#else
  start = ktime_get_boottime_ns();
  end = start + (u64)us * 1000ULL;
  while (ktime_get_boottime_ns() < end) {
    cpu_relax();
  }
#endif
}
static void ds18b20_write_byte(unsigned char data) {
  int i = 0;
  for (i = 0; i < 8; i++) {
    if (data & (1 << i)) {
      /* 输出1 */
      gpiod_direction_output(ds18b20dev.ds18b20_gpio, 0);
      ds18b20_delay(2);
      /* 设置为输入：引脚默认被上拉电阻拉到高电平 */
      gpiod_direction_input(ds18b20dev.ds18b20_gpio);
      ds18b20_delay(60);
    } else {
      /* 输出0*/
      gpiod_direction_output(ds18b20dev.ds18b20_gpio, 0);
      ds18b20_delay(2);
      /* 设置为输入：引脚默认被上拉电阻拉到高电平 */
      gpiod_direction_input(ds18b20dev.ds18b20_gpio);
      ds18b20_delay(2);
    }
  }
}

static unsigned char ds18b20_read_byte(void) {
  unsigned char data = 0;
  int i;
  for (i = 0; i < 8; i++) {
    gpiod_direction_output(ds18b20dev.ds18b20_gpio, 0);
    ds18b20_delay(2);

    /* 设置为输入 */
    gpiod_direction_input(ds18b20dev.ds18b20_gpio);

    /* 7us之后读取引脚的值 */
    ds18b20_delay(2);

    if (gpiod_get_value(ds18b20dev.ds18b20_gpio)) {
      data |= (1 << i);
    }

    /* 读到数据之后，等待足够60us */
    ds18b20_delay(60);
  }

  return data;
}

static int ds18b20_wait_ack(void) {
  int timeout_us = 500;
  /* 如果是高电平，等待 */
  while (gpiod_get_value(ds18b20dev.ds18b20_gpio) && --timeout_us) {
    udelay(1);
  }
  if (!timeout_us) {
    return -1;
  }

  /* 现在是低电平，等待 */
  timeout_us = 200;
  while (!gpiod_get_value(ds18b20dev.ds18b20_gpio) && --timeout_us) {
    udelay(1);
  }
  if (!timeout_us) {
    return -1;
  }

  return 0;
}

static int ds18b20_reset(void) {
  gpiod_direction_output(ds18b20dev.ds18b20_gpio, 0);
  ds18b20_delay(480);

  gpiod_direction_input(ds18b20dev.ds18b20_gpio);
  if (ds18b20_wait_ack()) {
    return -1;
  } else {
    return 0;
  }
}

// static void ds18b20_start(void) {
//   /* 建立通信 */
//   mdelay(30);
//   gpiod_set_value(ds18b20dev.ds18b20_gpio, 0);
//   mdelay(20);
//   gpiod_set_value(ds18b20dev.ds18b20_gpio, 1);
//   udelay(30);
//   gpiod_direction_input(ds18b20dev.ds18b20_gpio);
//   udelay(2);
//   /* 初始设置为输入引脚 */
// }

// static int ds18b20_wait_for_ready(void) {
//   int timeout_us = 200;
//   /* 等待低电平 */
//   while (gpiod_get_value(ds18b20dev.ds18b20_gpio) && --timeout_us) {
//     udelay(1);
//   }
//   if (!timeout_us) {
//     printk("%s %s line %d \n", __FILE__, __FUNCTION__, __LINE__);
//     return -1;
//   }

//   /* 现在为低电平，等待高电平 */
//   timeout_us = 200;
//   while (!gpiod_get_value(ds18b20dev.ds18b20_gpio) && --timeout_us) {
//     udelay(1);
//   }
//   if (!timeout_us) {
//     return -1;
//   }

//   /* 现在是高电平，等待低电平 */
//   timeout_us = 200;
//   while (gpiod_get_value(ds18b20dev.ds18b20_gpio) && --timeout_us) {
//     udelay(1);
//   }
//   if (!timeout_us) {
//     return -1;
//   }

//   return 0;
// }

// static irqreturn_t ds18b20_isq(int irq, void *dev_id) {
//   /* 1. 记录时间 */
//   ds18b20dev.ds18b20_edge_time[ds18b20dev.ds18b20_edge_cnt++] =
//       ktime_get_boot_ns();
//   if (ds18b20dev.ds18b20_edge_cnt >= 80) {
//     /* 2. 唤醒APP：去同一个链表把APP唤醒 */
//     ds18b20dev.ds18b20_data = 1;
//     wake_up(&ds18b20dev.ds18b20_wq);
//   }

// return IRQ_HANDLED;
// }

#if 0
static int dhtr11_read_byte(unsigned char *buf)
{
    int i;
    unsigned char data = 0;
    int timeout_us = 200;
    int us = 0;

    /* 循环8bit */
    for (i = 0; i < 8; i++)
    {
        /* 现在为低电平，等待高电平 */
        while (!gpiod_get_value(ds18b20dev.ds18b20_gpio) && --timeout_us)
        {
            udelay(1);
        }
        /* 超时 */
        if (!timeout_us)
        {
            return -1;
        }

        /* 现在是高电平，等待低电平，累加高电平的时间 */
        timeout_us = 200;
        us = 0;
        while (gpiod_get_value(ds18b20dev.ds18b20_gpio) && --timeout_us)
        {
            udelay(1);
            us++;
        }
        /* 超时 */
        if (!timeout_us)
        {
            return -1;
        }

        /* 根据us的长度，判断高低电平 > 40 = bit 1
                                    < 40 = bit 0 */
        if (us > 40)
        {
            /* get bit 1 */
            data = (data << 1) | 1;
        }
        else
        {
            /*get bit 0*/
            data = (data << 1) | 0;
        }
    }

    *buf = data;

    return 0;
}
#endif

// static int ds18b20_parse_data(char *data) {
//   int i, j, m = 0;
//   for (i = 0; i < 5; i++) {
//     data[i] = 0;
//     for (j = 0; j < 8; j++) {
//       /* 高电平持续时间判断 */
//       if (ds18b20dev.ds18b20_edge_time[m + 2] -
//               ds18b20dev.ds18b20_edge_time[m + 1] >=
//           40000) {
//         data[i] = (data[i] << 1) | 1;
//         m += 2;
//       } else {
//         data[i] = (data[i] << 1) | 0;
//       }
//     }
//   }

//   /* 4. 根据校验位验证数据  */
//   if (data[4] != data[0] + data[1] + data[2] + data[3]) {
//     printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
//     return -1;
//   } else {
//     return 0;
//   }
// }

static ssize_t ds18b20_read(struct file *filep, char __user *buf, size_t count,
                            loff_t *offt) {
  unsigned long flags;
  char data[5];
  unsigned char tempL = 0, tempH = 0;
  unsigned int interger;
  unsigned char decimal1, decimal2, decimal;
  /* 关中断 */
  local_irq_save(flags);

  if (count != 5) {
    return -EINVAL;
  }

  if (ds18b20_reset()) {
    gpiod_direction_output(ds18b20dev.ds18b20_gpio, 1);
    local_irq_restore(flags);
    return -ENODEV;
  }

  ds18b20_write_byte(0xcc); /* 忽略rom指令，直接使用功能指令 */
  ds18b20_write_byte(0x44); /* 温度转换指令 */

  /*  */
  gpiod_direction_output(ds18b20dev.ds18b20_gpio, 1);

  local_irq_save(flags);
  /* 转换需要时间，延时1s */
  set_current_state(TASK_INTERRUPTIBLE);
  schedule_timeout(HZ);

  if (ds18b20_reset()) {
    gpiod_direction_output(ds18b20dev.ds18b20_gpio, 1);
    local_irq_restore(flags);
    return -ENODEV;
  }

  ds18b20_write_byte(0xcc); /* 忽略rom指令，直接使用功能指令 */
  ds18b20_write_byte(0xbe); /* 读取暂存器指令 */

  tempL = ds18b20_read_byte();
  tempH = ds18b20_read_byte();

  if (tempH > 0x0f) {
    tempL = ~tempL; /* 补码转换，取反+1 */
    tempH = tempH + 1;
    interger = tempL / 16 + tempH * 16;        /* 整数部分 */
    decimal1 = (tempL & 0x0f) * 10 / 16;       /* 小数第一位 */
    decimal2 = (tempL & 0x0f) * 100 / 16 % 10; /* 小数第二位 */
    decimal = decimal1 * 10 + decimal2;        /* 小数两位 */
  } else {
    interger = tempL / 16 + tempH * 16;        /* 整数部分 */
    decimal1 = (tempL & 0x0f) * 10 / 16;       /* 小数第一位 */
    decimal2 = (tempL & 0x0f) * 100 / 16 % 10; /* 小数第二位 */
    decimal = decimal1 * 10 + decimal2;        /* 小数两位 */
  }
  /* 开中断 */
  local_irq_restore(flags);

  data[0] = interger & 0xff;
  data[1] = (interger >> 8) & 0xff;
  data[2] = decimal;
  if (copy_to_user(buf, data, sizeof())) {
    return -EFAULT;
  }

  return 5;
}

int ds18b20_probe(struct platform_device *pdev) {
  int ret;
  /* 1. 获取设备GPIO资源 */
  ds18b20dev.ds18b20_gpio = gpiod_get(&pdev->dev, "data-gpio", GPIOD_OUT_HIGH);
  if (IS_ERR(ds18b20dev.ds18b20_gpio)) {
    printk("can not get data-gpio\n");
    ret = PTR_ERR(ds18b20dev.ds18b20_gpio);
    return ret;
  }

  // /* 2. 获取终端号并且申请中断 */
  // ds18b20dev.ds18b20_irq = gpiod_to_irq(ds18b20dev.ds18b20_gpio);
  // ret = request_irq(ds18b20dev.ds18b20_irq, ds18b20_isq,
  //                   IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
  //                   "irq_ds18b20", NULL);
  // if (ret < 0) {
  //   printk("can not request irq\n");
  //   return ret;
  // }

  /* 3. 创建设备 */
  device_create(ds18b20dev.ds18b20_class, NULL, MKDEV(ds18b20dev.major, 0),
                NULL, "device_ds18b20");
  if (IS_ERR(ds18b20dev.ds18b20_device)) {
    return PTR_ERR(ds18b20dev.ds18b20_device);
  }

  return 0;
}

int ds18b20_remove(struct platform_device *pdev) {
  device_destroy(ds18b20dev.ds18b20_class, MKDEV(ds18b20dev.major, 0));
  class_destroy(ds18b20dev.ds18b20_class);
  unregister_chrdev(ds18b20dev.major, "ds18b20_device");

  // free_irq(ds18b20dev.ds18b20_irq, NULL);
  gpiod_put(ds18b20dev.ds18b20_gpio);
  return 0;
}
static struct of_device_id ds18b20_match_table[] = {
    {.compatible = "alientek,ds18b20"},
    {/* sentinel */},
};

static struct platform_driver ds18b20_driver = {
    .probe = ds18b20_probe,
    .remove = ds18b20_remove,
    .driver =
        {
            .name = "ds18b20",
            .of_match_table = ds18b20_match_table,
        },
};

static struct file_operations ds18b20_fops = {
    .owner = THIS_MODULE,
    .read = ds18b20_read,
};

static int __init ds18b20_init(void) {
  int err;
  /* 1. 注册字符设备 */
  ds18b20dev.major =
      register_chrdev(ds18b20dev.major, "ds18b20_device", &ds18b20_fops);
  if (ds18b20dev.major < 0) {
    printk("can not register chardev\n");
    return -ENOMEM;
  }
  /* 2. 创建设备类 */
  ds18b20dev.ds18b20_class = class_create(THIS_MODULE, "class_ds18b20");
  if (IS_ERR(ds18b20dev.ds18b20_class)) {
    printk("can not create class\n");
    unregister_chrdev(ds18b20dev.major, "ds18b20_device");
    return PTR_ERR(ds18b20dev.ds18b20_class);
  }

  init_waitqueue_head(&ds18b20dev.ds18b20_wq);

  err = platform_driver_register(&ds18b20_driver);

  return err;
}

static void __exit ds18b20_exit(void) {
  platform_driver_unregister(&ds18b20_driver);
}

module_init(ds18b20_init);
module_exit(ds18b20_exit);

MODULE_LICENSE("GPL");