#include "asm/uaccess.h"
#include "linux/device.h"
#include "linux/err.h"
#include "linux/export.h"
#include "linux/gpio/consumer.h"
#include "linux/input.h"
#include "linux/interrupt.h"
#include "linux/irqreturn.h"
#include "linux/kdev_t.h"
#include "linux/mod_devicetable.h"
#include "linux/printk.h"
#include "linux/timekeeping.h"
#include "linux/types.h"
#include "linux/wait.h"
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/version.h>

struct hs0038_dev {
  dev_t hs0038_dev_id;
  struct class *hs0038_class;
  struct device *hs0038_device;
  int hs0038_major;
  wait_queue_head_t hs0038_wq;
  int hs0038_irq;
  struct gpio_desc *hs0038_gpio;
  int hs0038_edge_cnt;
  char hs0038_edge_time[100];
  unsigned int hs0038_data;
  unsigned int hs0038_data_buf[8];
  int r;
  int w;
  struct input_dev *hs0038_input_dev;
};

static struct hs0038_dev hs0038dev;

/* 放入数据 */
static int put_data(unsigned int val) {
  if (((hs0038dev.w + 1) & 7) == hs0038dev.r) {
    hs0038dev.hs0038_data_buf[hs0038dev.w] = val;
    hs0038dev.w = (hs0038dev.w + 1) & 7; /* 移动位置 */
  }
  return val;
}

static int get_data(unsigned int *val) {
  if (hs0038dev.r == hs0038dev.w) {
    return -1;
  } else {
    *val = hs0038dev.hs0038_data_buf[hs0038dev.r];
    hs0038dev.r = (hs0038dev.r + 1) & 7;
    return 0;
  }
}

static int has_data(void) {
  if (hs0038dev.r == hs0038dev.w) {
    return 0;
  } else {
    return 1;
  }
}

static ssize_t hs0038_read(struct file *filp, char __user *buf, size_t size,
                           loff_t *offt) {
  unsigned int val;
  int ret;
  if (size != 4) {
    return -EINVAL;
  }

  wait_event_interruptible(hs0038dev.hs0038_wq, has_data());

  get_data(&val);

  ret = copy_to_user(buf, &val, 4);
  if (ret) {
    printk("copy to user error\n");
  }
  return 4;
}

/*
0:成功，*val中记录数据
-1:没接收完毕
-2:解析错误
-3:超时(中断丢失)
*/
static int hs0038_parse_data(unsigned int *val) {
  u64 tmp;
  unsigned char data[4];
  int i, j, m;

  hs0038dev.hs0038_edge_cnt = 0;

  /* 判断是否有重复码 */
  if (hs0038dev.hs0038_edge_cnt == 4) {
    tmp = hs0038dev.hs0038_edge_time[1] - hs0038dev.hs0038_edge_time[0];
    if (tmp > 8000000 && tmp < 10000000) {
      tmp = hs0038dev.hs0038_edge_time[2] - hs0038dev.hs0038_edge_time[1];
      if (tmp < 3000000) {
        /* 获得了重复码 */
        *val = hs0038dev.hs0038_data;
        return 0;
      }
    }
  }

  /* 接收到68个中断 */
  m = 3;
  if (hs0038dev.hs0038_edge_cnt >= 68) { /* 解析到了数据 */
    for (i = 0; i < 4; i++) {
      data[i] = 0;
      /* 先收到bit0 */
      for (j = 0; j < 8; j++) {
        /* 数值1 */
        if (hs0038dev.hs0038_edge_time[m + 1] - hs0038dev.hs0038_edge_time[m] >
            30000000) {
          data[i] |= (1 << j);
          m += 2;
        }
      }
    }
    /* 校验数据 */
    data[1] = ~data[1];
    if (data[0] != data[1]) {
      return -2;
    }

#if 0
    if ((data[2] & 0x7f) != (~data[3] & 0x7f)) {
      return -2;
    }
#endif
    data[3] = ~data[3];
    if (data[2] != ~data[3]) {
      return -2;
    }

    hs0038dev.hs0038_data = (data[0] << 8) | data[2];

    *val = hs0038dev.hs0038_data;

    return 0;
  } else {
    /* 数据没有接收完毕 */
    return -1;
  }

  return 0;
}

static irqreturn_t hs0038_isr(int irq, void *dev_id) {
  int ret;
  unsigned int val;
  printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
//   wake_up_interruptible(&hs0038dev.hs0038_wq);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
  hs0038dev.hs0038_edge_time[hs0038dev.hs0038_edge_cnt++] =
      ktime_get_boottime_ns();
#else
  hs0038dev.hs0038_edge_time[hs0038dev.hs0038_edge_cnt++] = ktime_get_boot_ns();
#endif

  /* 判断超时 */
  if (hs0038dev.hs0038_edge_cnt >= 2) {
    if (hs0038dev.hs0038_edge_time[hs0038dev.hs0038_edge_cnt - 1] -
            hs0038dev.hs0038_edge_time[hs0038dev.hs0038_edge_cnt - 2] >
        3000000) {
      /*  超时 */
      hs0038dev.hs0038_edge_time[0] =
          hs0038dev.hs0038_edge_time[hs0038dev.hs0038_edge_cnt - 1];

      hs0038dev.hs0038_edge_cnt = 1;
    }
    return IRQ_HANDLED;
  }

  ret = hs0038_parse_data(&val);

  if (!ret) {
    /* 解析成功 */
    hs0038dev.hs0038_edge_cnt = 0;
    // printk("get ir code =0x%x\n", val);
    put_data(val);
    wake_up(&hs0038dev.hs0038_wq);

    /* D.输入系统上报数据 */
    input_event(hs0038dev.hs0038_input_dev, EV_KEY, val, 1);
    input_event(hs0038dev.hs0038_input_dev, EV_KEY, val, 0);
    // input_sync(hs0038dev.hs0038_input_dev);
    input_event(hs0038dev.hs0038_input_dev, EV_SYN, 0, 0);
  } else if (ret == -2) {
    /* 解析失败 */
    hs0038dev.hs0038_edge_cnt = 0;
  }

  return IRQ_HANDLED;
}

static int hs0038_probe(struct platform_device *pdev) {
  int ret;
  /* 1. 获取硬件资源信息 */
  hs0038dev.hs0038_gpio = gpiod_get(&pdev->dev, NULL, 0);
  if (IS_ERR(hs0038dev.hs0038_gpio)) {
    ret = PTR_ERR(hs0038dev.hs0038_gpio);
    printk("can not get gpio\n");
    return ret;
  }

  hs0038dev.hs0038_irq = gpiod_to_irq(hs0038dev.hs0038_gpio);
  if (hs0038dev.hs0038_irq < 0) {
    printk("gpio convert to irq failed\n");
    return -EFAULT;
  }
  ret = request_irq(hs0038dev.hs0038_irq, hs0038_isr,
                    IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "hs30038_irq",
                    NULL);
  if (ret) {
    printk("request irq failed\n");
  }

  /* 3. 创建设备 */
  hs0038dev.hs0038_device =
      device_create(hs0038dev.hs0038_class, NULL,
                    MKDEV(hs0038dev.hs0038_major, 0), NULL, "hs0038_device");
  if (IS_ERR(hs0038dev.hs0038_device)) {
    printk("create device failed\n");
    ret = PTR_ERR(hs0038dev.hs0038_device);
    return ret;
  }

  /* 输入子系统的代码 */
  /* A. 分配input_dev */
  hs0038dev.hs0038_input_dev = devm_input_allocate_device(&pdev->dev);
  /* B. 设置input_dev */
  hs0038dev.hs0038_input_dev->name = "hs0038";
  hs0038dev.hs0038_input_dev->phys = "hs0038";

  /* B.1 能产生哪类事件 */
  __set_bit(EV_KEY, hs0038dev.hs0038_input_dev->evbit);
  __set_bit(EV_REP, hs0038dev.hs0038_input_dev->evbit);
  /* B.2 设置input_dev */
  __set_bit(KEY_0, hs0038dev.hs0038_input_dev->keybit);
  memset(hs0038dev.hs0038_input_dev->keybit, 0xff,
         sizeof(hs0038dev.hs0038_input_dev->keybit));
  /* C. 注册input_dev */
  ret = input_register_device(hs0038dev.hs0038_input_dev);
  if (ret) {
    printk("can not register input dev\n");
  }
  return 0;
}

static int hs0038_remove(struct platform_device *pdev) {

  input_unregister_device(hs0038dev.hs0038_input_dev);
  device_destroy(hs0038dev.hs0038_class, MKDEV(hs0038dev.hs0038_major, 0));
  class_destroy(hs0038dev.hs0038_class);
  unregister_chrdev(hs0038dev.hs0038_major, "hs0038_device");

  free_irq(hs0038dev.hs0038_irq, NULL);
  gpiod_put(hs0038dev.hs0038_gpio);
  return 0;
}

static struct of_device_id hs0038_idtable[] = {
    {.compatible = "alientek,hs0038"},
    {/* sentinel */},
};

static struct platform_driver hs0038_driver = {
    .probe = hs0038_probe,
    .remove = hs0038_remove,
    .driver =
        {
            .name = "hs0038",
            .of_match_table = hs0038_idtable,
        },
};

static struct file_operations hs0038_fops = {
    .owner = THIS_MODULE,
    .read = hs0038_read,
};

static int __init hs0038_init(void) {
  int ret;
  /* 1. 注册字符设备 */
  hs0038dev.hs0038_major =
      register_chrdev(hs0038dev.hs0038_major, "hns0038", &hs0038_fops);
  if (hs0038dev.hs0038_major < 0) {
    printk("register char dev failed\n");
    return -ENOMEM;
  }

  /* 2. 创建设备类 */
  hs0038dev.hs0038_class = class_create(THIS_MODULE, "hs0038_class");
  if (IS_ERR(hs0038dev.hs0038_class)) {
    ret = PTR_ERR(hs0038dev.hs0038_class);
    printk("can not create device class\n");
    return ret;
  }

  /* 3. 注册平台驱动 */
  platform_driver_register(&hs0038_driver);

  /* 4. 初始化等待队列 */
  init_waitqueue_head(&hs0038dev.hs0038_wq);

  return 0;
}

static void __exit hs0038_exit(void) {
  printk("%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
  platform_driver_unregister(&hs0038_driver);
}

module_init(hs0038_init);
module_exit(hs0038_exit);

MODULE_LICENSE("GPL");