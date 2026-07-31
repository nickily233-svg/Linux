#include "linux/export.h"
#include "linux/fs.h"
#include "linux/gpio/consumer.h"
#include "linux/kthread.h"
#include "linux/mod_devicetable.h"
#include "linux/platform_device.h"
#include "linux/types.h"
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

struct uart {
  dev_t dev_id;
  struct class *cls;
  struct device *dev;
  struct gpio_desc *gpio;
  struct kthread_work *work;
};

static ssize_t uart_write(struct file *filp, const char __user *buf,
                          size_t size, loff_t *offset) {
  struct uart *uart;
  return 0;
}

static struct file_operations uart_fops = {
    .owner = THIS_MODULE,
    .write = uart_write,
};

static int uart_probe(struct platform_device *pdev) {
  struct uart *uart;
  return 0;
}

static int uart_remove(struct platform_device *pdev) {
  struct uart *uart;
  return 0;
}

static struct of_device_id uart_of_match_table[] = {
    {.compatible = "uart"},
    {/* sentinel */},
};
MODULE_DEVICE_TABLE(of, uart_of_match_table);

static struct platform_driver uart_driver = {
    .driver =
        {
            .name = "uart",
            .of_match_table = uart_of_match_table,
        },
    .probe = uart_probe,
    .remove = uart_remove,
};

static int __init uart_init(void) {
  platform_driver_register(&uart_driver);
  printk("KERN_INFO 模块记载成功\n");
  return 0;
}

static void __exit uart_exit(void) {
  platform_driver_unregister(&uart_driver);
  printk("KERN_INFO 模块卸载成功\n");
}

module_init(uart_init);
module_exit(uart_exit);

MODULE_LICENSE("GPL");