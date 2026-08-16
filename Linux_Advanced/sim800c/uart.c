#include "asm-generic/fcntl.h"
#include "linux/device.h"
#include "linux/err.h"
#include "linux/export.h"
#include "linux/fs.h"
#include "linux/gfp.h"
#include "linux/gpio/consumer.h"
#include "linux/jiffies.h"
#include "linux/kthread.h"
#include "linux/mod_devicetable.h"
#include "linux/platform_device.h"
#include "linux/string.h"
#include "linux/types.h"
#include "linux/workqueue.h"
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

struct sim800c_device {
  struct delayed_work poll_work; /* 延时工作队列，用于轮询 */
  struct gpio_desc *reset_gpio;
  struct platform_device *pdev;
  struct file *uart_file; /* 串口设备文件句柄 */
};

/* 定时轮询检查网络状态 */
static void sim800c_poll_work_func(struct work_struct *work) {
  struct sim800c_device *dev =
      container_of(work, struct sim800c_device, poll_work.work);

  char tx_buf[] = "AT+CREG?\r\n";
  char rx_buf[128] = {0};
  loff_t pos = 0;
  int ret;

  if (!dev->uart_file || IS_ERR(dev->uart_file)) {
    goto resched;
  }

  /* 发送AT指令查询网络注册状态 */
  kernel_write(dev->uart_file, tx_buf, strlen(tx_buf), pos);

  /* 延时等待模组返回数据 */
  /* 工作队列运行在“进程上下文”，是可以使用msleep进行睡眠等待的 */
  msleep(500);

  pos = 0;
  memset(rx_buf, 0, sizeof(rx_buf));
  ret = kernel_read(dev->uart_file, pos, rx_buf, sizeof(rx_buf) - 1);
  if (ret > 0) {
    printk("KERNM_INFO sim800c response : %s", rx_buf);
    /* 判断是否注册网络成功（+CREG: 0,1 或 0,5 代表注册成功） */
    if (!strstr(rx_buf, "0,1") && !strstr(rx_buf, "0,5")) {
      printk("KERN_WARNING sim800c offline ready to hw reset\n");

      if (dev->reset_gpio) {
        gpiod_set_value(dev->reset_gpio, 1);
        msleep(msecs_to_jiffies(1000));
        gpiod_set_value(dev->reset_gpio, 0);
        msleep(msecs_to_jiffies(2000));
      }
    }
  }

resched:
  schedule_delayed_work(&dev->poll_work, msecs_to_jiffies(10000));
}

static int sim800c_probe(struct platform_device *pdev) {
  struct sim800c_device *dev;

  /* 分配驱动私有结构体内存 */
  dev = devm_kzalloc(&pdev->dev, sizeof(struct sim800c *), GFP_KERNEL);
  if (!dev) {
    dev_err(&pdev->dev, "can not allocate prv mem\n");
    return -ENOMEM;
  }

  dev->pdev = pdev;

  platform_set_drvdata(pdev, dev);

  dev->reset_gpio = devm_gpiod_get(&pdev->dev, "reset", GPIOD_OUT_LOW);
  if (IS_ERR(dev->reset_gpio)) {
    dev_err(&pdev->dev, "get gpio failed\n");
    return PTR_ERR(dev->reset_gpio);
  }

  /* 打开串口设备文件 */
  dev->uart_file = filp_open("/dev/ttymxc1", O_RDWR | O_NOCTTY, 0);
  if (IS_ERR(dev->uart_file)) {
    dev_err(&pdev->dev, "can not open /dev/ttymxc1 Error Code : %ld\n",
            PTR_ERR(dev->uart_file));
    return PTR_ERR(dev->uart_file);
  }

  /* 初始化延时工作队列 */
  INIT_DELAYED_WORK(&dev->poll_work, sim800c_poll_work_func);

  /* 触发第一次检查 */
  schedule_delayed_work(&dev->poll_work, msecs_to_jiffies(3000));

  dev_info(&pdev->dev, "sim800c auto reload driver sucess\n");

  return 0;
}

static int sim800c_remove(struct platform_device *pdev) {
  struct sim800c_device *dev = platform_get_drvdata(pdev);

  cancel_delayed_work(&dev->poll_work);

  if (dev->uart_file && !IS_ERR(dev->uart_file)) {
    filp_close(dev->uart_file, NULL);
  }

  dev_info(&pdev->dev, "sim800c driver uninstall success\n");

  return 0;
}

static struct of_device_id sim800c_of_match_table[] = {
    {.compatible = "sim800c"},
    {/* sentinel */},
};
MODULE_DEVICE_TABLE(of, sim800c_of_match_table);

static struct platform_driver sim800c_driver = {
    .driver =
        {
            .name = "sim800c",
            .of_match_table = sim800c_of_match_table,
        },
    .probe = sim800c_probe,
    .remove = sim800c_remove,
};

static int __init sim800c_init(void) {
  platform_driver_register(&sim800c_driver);
  return printk("KERN_INFO 模块记载成功\n");
}

static void __exit sim800c_exit(void) {
  platform_driver_unregister(&sim800c_driver);
  printk("KERN_INFO 模块卸载成功\n");
}

module_init(sim800c_init);
module_exit(sim800c_exit);

MODULE_LICENSE("GPL");