#include "linux/interrupt.h"
#include "linux/irqreturn.h"
#include "linux/tty.h"
#include "linux/tty_driver.h"
#include "linux/types.h"
#include <asm-generic/termbits.h>
#include <cerrno>
#include <cstddef>
#include <linux/circ_buf.h>
#include <linux/console.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/tty_flip.h>

#define BUF_LEN 1024
#define NEXT_PLACE(i) ((i + 1) & 0x3FF)

static struct uart_port *virtual_port;
static struct proc_dir_entry *virtual_uart_proc_file;

static unsigned char tx_buf[BUF_LEN];
static int tx_buf_r = 0;
static int tx_buf_w = 0;

static unsigned char rx_buf[BUF_LEN];
static int rx_buf_r = 0;
static int rx_buf_w = 0;

static int txbuf_put(unsigned char val);

/*
 * interrupts are disabled on entering
 */
static void virtual_uart_write(struct console *co, const char *s,
                               unsigned int count) {
  int i;
  for (i = 0; i < count; i++) {
    if (txbuf_put(s[i]) != 0) {
      return;
    }
  }
}

static struct tty_driver *virtual_uart_console_dev(struct console *co,
                                                   int *index) {
  /* 把 co->data 强制转换成你自己的虚拟串口结构体 */
  struct uart_driver *my_drv = co->data;
  *index = co->index;
  return my_drv->tty_driver;
};

static struct console virtual_uart_console = {
    .name = "ttyVIRT",
    .write = virtual_uart_write,
    .device = virtual_uart_console_dev,
    .flags = CON_PRINTBUFFER,
    .index = -1,
};

static struct uart_driver virtuial_uart_drv = {
    .owner = THIS_MODULE,
    .driver_name = "VIRT_UART",
    .dev_name = "ttyVIRT",
    .major = 0,
    .minor = 0,
    .nr = 1,
    .cons = &virtual_uart_console,
};

static int is_txbuf_empty(void) { return tx_buf_r == tx_buf_w; }

/* 写指针的下一个位置是不是撞上读指针 --- 说明环形缓冲区满了 */
static int is_txbuf_full(void) { return NEXT_PLACE(tx_buf_w) == tx_buf_r; }

/* 往缓冲区里写入一个字节 */
static int txbuf_put(unsigned char val) {
  if (is_txbuf_full())
    return -1;
  /* 在写指针指向的位置存入数据 val */
  tx_buf[tx_buf_w] = val;
  /* 把写指针往前挪一格 */
  tx_buf_w = NEXT_PLACE(tx_buf_w);
  return 0;
}

/* 从缓冲区里取出一个字节,存到用户传进来的指针 pval 指向的地址里 */
static int txbuf_get(unsigned char *pval) {
  if (is_txbuf_empty())
    return -1;
  *pval = tx_buf[tx_buf_r];
  tx_buf_r = NEXT_PLACE(tx_buf_r);
  return 0;
}

static int txbuf_count(void) {
  /* 写指针在读指针后面 */
  if (tx_buf_w >= tx_buf_r)
    return tx_buf_w - tx_buf_r;
  /* 读指针在写指针后面 */
  else
    return BUF_LEN + tx_buf_w - tx_buf_r;
}

static unsigned int virtual_uart_tx_empty(struct uart_port *uport) {
  /* 因为要发送的数据瞬间存入buffer */
  return 1;
}

/*
 * interrupts disabled on entry
 */
static void virtual_uart_start_tx(struct uart_port *uport) {
  /* 创建一个环形缓冲区 if(head!=tail)代表有数据 */
  struct circ_buf *xmit = &uport->state->xmit;

  /* 只要环形缓冲器非空或者发送没有停止,就继续发送数据 */
  while (!uart_circ_empty(xmit) && !uart_tx_stopped(uport)) {
    /* send xmit->buf[xmit->tail]
     * out the port here */

    /* 把circ buffer中的数据全部存入硬件缓冲区里 tx_buf */

    /* 将环形缓冲区的tail尾部的数据发送到硬件写缓冲区 tx_buf */
    tx_buf[tx_buf_w++] = xmit->buf[xmit->tail];

    /* 将环形缓冲区的指针后移 */
    xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
    /* 代表发送成功多少个字节的计数 */
    uport->icount.tx++;
  }

  /* 如果环形缓冲区的数据小于WAKEUP_CHARS这个阈值,就唤醒write继续写数据 */
  if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
    uart_write_wakeup(uport);
}

static void virtual_uart_set_termios(struct uart_port *uport,
                                     struct ktermios *termios,
                                     struct ktermios *old) {
  return;
}

static int virtual_startup(struct uart_port *port) { return 0; }

static void virtual_set_mctrl(struct uart_port *port, unsigned int mctrl) {}

static unsigned int virtual_get_mctrl(struct uart_port *port) { return 0; }

static void virtual_stop_tx(struct uart_port *port) {}

static void virtual_stop_rx(struct uart_port *port) {}

static void virtual_shutdown(struct uart_port *port) {}

static const char *virtual_type(struct uart_port *port) {
  return "ALIENTEK_VIRTUAL_UART";
}

static ssize_t virtual_uart_buf_write(struct file *filp, const char __user *buf,
                                      size_t size, loff_t *offset) {
  int ret;
  /* get data */
  ret = copy_from_user(rx_buf, buf, size);
  if (ret) {
    printk("copy from user error\n");
    return -EFAULT;
  }
  rx_buf_w = size;

  /* 模拟产生RX中断 */
  /*
   * 调试用 手动伪造一个硬件中断
   * IRQCHIP_STATE_PENDING:这是一个内核宏,意思是“把中断状态置为‘挂起’
   * 1 代表激活这个状态
   */
  irq_set_irqchip_state(virtual_port->irq, IRQCHIP_STATE_PENDING, 1);

  return size;
}

static ssize_t virtual_uart_buf_read(struct file *filp, char __user *buf,
                                     size_t size, loff_t *offset) {
  /* 把 tx_buf 中的数据 copy_to_user */
  int cnt = txbuf_count();
  int i;
  int ret;
  unsigned char val;

  cnt = (cnt > size) ? size : cnt;

  for (i = 0; i < cnt; i++) {
    txbuf_get(&val);
    ret = copy_to_user(buf + i, &val, 1);
    if (ret) {
      printk("copy to user error\n");
      return -EFAULT;
    }
  }

  return cnt;
}

/* 对缓冲区的读写函数 */
static const struct file_operations virtual_uart_buf_fops = {
    .write = virtual_uart_buf_write,
    .read = virtual_uart_buf_read,
};

static const struct uart_ops virtual_pops = {
    .start_tx = virtual_uart_start_tx,
    .set_termios = virtual_uart_set_termios,
    .tx_empty = virtual_uart_tx_empty,
    .type = virtual_type,
    //.config_port	= imx_config_port,
    //.verify_port	= imx_verify_port,
    .stop_rx = virtual_stop_rx,
    //.enable_ms	= imx_enable_ms,
    //.break_ctl	= imx_break_ctl,
    .startup = virtual_startup,
    .shutdown = virtual_shutdown,
    //.flush_buffer	= imx_flush_buffer,
    .set_mctrl = virtual_set_mctrl,
    .get_mctrl = virtual_get_mctrl,
    .stop_tx = virtual_stop_tx,
};

static irqreturn_t virtual_uart_rxint(int irq, void *dev_id) {
  struct tty_port *tport;
  struct uart_port *uport;
  unsigned long flags;
  int i;

  spin_lock_irqsave(&uport->lock, flags);

  for (i = 0; i < rx_buf_w; i++) {
    uport->icount.rx++;

    /* get data from hardware/rxbuf */

    /* put data to ldisc */
    tty_insert_flip_char(tport, rx_buf[i], TTY_NORMAL);
  }

  rx_buf_w = 0;

  spin_unlock_irqrestore(&uport->lock, flags);
  tty_flip_buffer_push(tport);

  return IRQ_HANDLED;
}

static int virtual_uart_probe(struct platform_device *pdev) {
  int rx_irq;
  int ret;

  /*
   * /proc 创建
   * arg[0] 文件名
   * arg[1] 访问权限 类似chmod 777
   * arg[2] 父目录 NULL为默认/proc目录
   * arg[3] 操作接口
   */
  virtual_uart_proc_file =
      proc_create("virtual_uart_buf", 0, NULL, &virtual_uart_buf_fops);
  // uart_add_one_port(struct uart_driver * drv, struct uart_port * uport);

  /* 从 DTS 中获取硬件中断信息 */
  rx_irq = platform_get_irq(pdev, 0);

  /* 分配设置注册 uart_port */
  virtual_port = devm_kzalloc(&pdev->dev, sizeof(*virtual_port), GFP_KERNEL);

  /* 将当前平台设备的通用设备结构体，关联给这个串口端口 */
  virtual_port->dev = &pdev->dev;
  /* 设置串口寄存器的访问方式 --- 内存映射I/O */
  virtual_port->iotype = UPIO_MEM;
  /* 给串口端口分配中断号 */
  virtual_port->irq = rx_irq;
  /* 设置FIFO的深度 */
  virtual_port->fifosize = 32;
  /* 绑定 uart操作函数集 */
  virtual_port->ops = &virtual_pops;
  /* 串口设置启动时自动配置,设置此标志位就会强制调用set_termios的波特率 */
  virtual_port->flags = UPF_BOOT_AUTOCONF;

  ret = devm_request_irq(&pdev->dev, rx_irq, virtual_uart_rxint, 0,
                         dev_name(&pdev->dev), virtual_port);

  return uart_add_one_port(&virtuial_uart_drv, virtual_port);
}

static int virtual_uart_remove(struct platform_device *pdev) {
  uart_remove_one_port(&virtuial_uart_drv, virtual_port);
  proc_remove(virtual_uart_proc_file);
  return 0;
}

static struct of_device_id uart_of_match_table[] = {
    {.compatible = "alientek_virtual_uart"},
    {},
};
MODULE_DEVICE_TABLE(of, uart_of_match_table);

static struct platform_driver virtual_uart_platform_driver = {
    .driver =
        {
            /*
             * 为了适配设备数不支持的内核
             * 如果内核支持设备树,原样返回给定的匹配表;不支持设备树的话,返回空指针
             */
            .of_match_table = of_match_ptr(uart_of_match_table),
            .name = "alientek_virtual_uart",
        },
    .probe = virtual_uart_probe,
    .remove = virtual_uart_remove,
};

static int __init virtual_uart_init(void) {
  int ret;
  printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);

  ret = uart_register_driver(&virtuial_uart_drv);

  if (ret) {
    printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);
    return ret;
  }

  return platform_driver_register(&virtual_uart_platform_driver);
}

static void __exit virtual_uart_exit(void) {
  printk("%s %s line %d", __FILE__, __FUNCTION__, __LINE__);

  platform_driver_unregister(&virtual_uart_platform_driver);

  uart_unregister_driver(&virtuial_uart_drv);
}

module_init(virtual_uart_init);
module_exit(virtual_uart_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LIUYANG");