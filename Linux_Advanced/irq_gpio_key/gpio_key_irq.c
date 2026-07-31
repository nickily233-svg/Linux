#include "asm-generic/fcntl.h"
#include "asm-generic/gpio.h"
#include "asm/atomic.h"
#include "asm/gpio.h"
#include "asm/uaccess.h"
#include "linux/cdev.h"
#include "linux/device.h"
#include "linux/export.h"
#include "linux/fs.h"
#include "linux/gfp.h"
#include "linux/gpio.h"
#include "linux/gpio/consumer.h"
#include "linux/irqreturn.h"
#include "linux/mod_devicetable.h"
#include "linux/mutex.h"
#include "linux/types.h"
#include "linux/wait.h"
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <stdatomic.h>

struct irq_gpio_key_dev {
  int gpio;
  int irq;
  dev_t dev_id;
  struct cdev cdev;
  struct class *cls;
  struct device *dev;
  wait_queue_head_t wq;
  atomic_t preee_cnt;
  struct mutex lock;
};

static irqreturn_t irq_gpio_key_isr(int irq, void *dev_id) {
  struct irq_gpio_key_dev *data = (struct irq_gpio_key_dev *)dev_id;

  int val = gpio_get_value(data->gpio);

  /* 读到高电平 唤醒APP */
  if (val == 1) {
    atomic_inc(&data->preee_cnt);
    __wake_up(&data->wq, TASK_INTERRUPTIBLE, 1, NULL);
  }

  return -IRQ_HANDLED;
}

static ssize_t irq_gpio_key_read(struct file *filp, char __user *buf,
                                 size_t size, loff_t *offset) {
  struct irq_gpio_key_dev *data = filp->private_data;
  int cnt;

  if (size < sizeof(int)) {
    return -EINVAL;
  }

  mutex_lock(&data->lock);

  if (atomic_read(&data->preee_cnt) == 0) {
    if (filp->f_flags & O_NONBLOCK) {
      mutex_unlock(&data->lock);
      return -EAGAIN;
    }

    wait_event_interruptible(data->wq, atomic_read(&data->preee_cnt) != 0);
  }

  cnt = atomic_read(&data->preee_cnt);
  if (copy_to_user(buf, &cnt, sizeof(cnt))) {
    mutex_unlock(&data->lock);
    return -EFAULT;
  }

  atomic_set(&data->preee_cnt, 0);
  mutex_unlock(&data->lock);

  return 0;
}

static const struct file_operations irq_gpio_key_fops = {
    .owner = THIS_MODULE,
    .read = irq_gpio_key_read,
};

static int irq_gpio_key_probe(struct platform_device *pdev) {
  struct irq_gpio_key_dev *data;
  int ret;

  /* 分配设备结构体内存 */
  data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
  if (!data) {
    return -ENOMEM;
  }
  platform_set_drvdata(pdev, data);

  mutex_init(&data->lock);
  init_waitqueue_head(&data->wq);
  atomic_set(&data->preee_cnt, 0);

  /* 从设备树中获取 GPIO 信息 */
  data->gpio = of_get_named_gpio(pdev->dev.of_node, "key_gpios", 0);
  if (!gpio_is_valid(data->gpio)) {
    dev_err(&pdev->dev, "invaild gpio\n");
    return -EINVAL;
  }

  /* 申请 GPIO */
  ret = devm_gpio_request_one(&pdev->dev, data->gpio, GPIOF_IN, "key-gpio");
  if (!ret) {
    dev_err(&pdev->dev, "failed to request gpio\n");
    return ret;
  }

  /* 获取中断 */
  data->irq = gpio_to_irq(data->gpio);
  if (!data->irq) {
    dev_err(&pdev->dev, "failed to get irq\n");
    return data->irq;
  }

  /* 注册中断服务函数 */
  ret = devm_request_irq(&pdev->dev, data->irq, irq_gpio_key_isr,
                         IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                         "irq_gpio_key", &data->dev_id);
  if (ret) {
    dev_err(&pdev->dev, "failed to request irq\n");
    return ret;
  }

  /* 注册字符设备 */
  alloc_chrdev_region(&data->dev_id, 0, 1, "irq_gpio_key");
  cdev_init(&data->cdev, &irq_gpio_key_fops);
  data->cdev.owner = THIS_MODULE;
  cdev_add(&data->cdev, data->dev_id, 1);
  /* 创建设备类 */
  data->cls = class_create(THIS_MODULE, "irq_gpio_key_cls");
  /* 创建设备 */
  data->dev =
      device_create(data->cls, NULL, data->dev_id, NULL, "irq_gpio_key_dev");

  dev_info(&pdev->dev, "gpio key driver has been initialized\n");

  return 0;
}

static int irq_gpio_key_remove(struct platform_device *pdev) {
  struct irq_gpio_key_dev *data = platform_get_drvdata(pdev);
  device_destroy(data->cls, data->dev_id);
  class_destroy(data->cls);
  cdev_del(&data->cdev);
  unregister_chrdev_region(data->dev_id, 1);

  dev_info(&pdev->dev, "gpio key driver has been removed\n");

  return 0;
}

static const struct platform_device_id irq_gpio_key_ids[] = {
    {"irq_gpio_key", 0},
    {/* sentinel */},
};
MODULE_DEVICE_TABLE(platform, irq_gpio_key_ids);

static const struct of_device_id irq_gpio_key_of_match_table[] = {
    {.compatible = "irq_gpio_key"},
    {/* sentinel */},
};
MODULE_DEVICE_TABLE(of, irq_gpio_key_of_match_table);

static struct platform_driver irq_gpio_key_driver = {
    .driver =
        {
            .name = "irq_gpio_key",
            .of_match_table = irq_gpio_key_of_match_table,
        },
    .probe = irq_gpio_key_probe,
    .remove = irq_gpio_key_remove,
    .id_table = irq_gpio_key_ids,

};

module_platform_driver(irq_gpio_key_driver);

MODULE_LICENSE("GPL v2");
