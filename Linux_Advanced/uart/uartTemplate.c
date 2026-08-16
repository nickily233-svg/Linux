#include <linux/console.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

/* ============================================================
 * 第 1 部分：硬件相关定义
 * ============================================================ */

/* 寄存器偏移（你的硬件寄存器地址） */
#define REG_RX 0x00     /* 接收数据寄存器 */
#define REG_TX 0x00     /* 发送数据寄存器 */
#define REG_STATUS 0x04 /* 状态寄存器 */
#define REG_CTRL 0x08   /* 控制寄存器 */
#define REG_DIV 0x0C    /* 波特率分频寄存器 */

/* 状态位 */
#define STATUS_RX_READY BIT(0)
#define STATUS_TX_EMPTY BIT(1)

/* 驱动私有数据 */
struct my_uart_priv {
  struct uart_port port; /* 必须放在第一个，或通过 container_of 访问 */
  struct clk *clk;       /* 时钟（可选） */
  void __iomem *membase; /* 寄存器基地址 */
  int irq;               /* 中断号 */
  int line;              /* 端口号 */
};

/* ============================================================
 * 第 2 部分：uart_ops 回调函数实现
 * ============================================================ */

/**
 * tx_empty  - 发送 FIFO 是否为空
 * 返回值: TIOCSER_TEMT (空) 或 0
 */
static unsigned int my_uart_tx_empty(struct uart_port *port) {
  struct my_uart_priv *priv = container_of(port, struct my_uart_priv, port);
  u32 status = readl(priv->membase + REG_STATUS);

  return (status & STATUS_TX_EMPTY) ? TIOCSER_TEMT : 0;
}

/**
 * set_mctrl - 设置 modem 控制信号 (RTS/DTR)
 * 虚拟设备或纯 TX 通常为空
 */
static void my_uart_set_mctrl(struct uart_port *port, unsigned int mctrl) {
  /* 真实硬件: 操作 RTS/DTR 引脚 */
}

/**
 * get_mctrl - 读取 modem 状态信号 (CTS/DSR/CD/RI)
 * 没有硬件连接时返回 0
 */
static unsigned int my_uart_get_mctrl(struct uart_port *port) {
  return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS; /* 模拟载波检测 */
}

/**
 * stop_tx - 停止发送（流控时调用）
 */
static void my_uart_stop_tx(struct uart_port *port) {
  /* 关发送中断，或禁止发送器 */
}

/**
 * start_tx - 核心！启动发送
 * 从 circ_buf 中读数据，往硬件 FIFO 写
 */
static void my_uart_start_tx(struct uart_port *port) {
  struct circ_buf *xmit = &port->state->xmit;
  struct my_uart_priv *priv = container_of(port, struct my_uart_priv, port);

  while (!uart_circ_empty(xmit) && !uart_tx_stopped(port)) {
    /* 往硬件发送寄存器写一个字节 */
    writel(xmit->buf[xmit->tail], priv->membase + REG_TX);

    /* 移动 tail 指针 */
    xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
    port->icount.tx++;
  }

  /* 如果 circ_buf 快空了，唤醒 write 系统调用继续写入 */
  if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
    uart_write_wakeup(port);
}

/**
 * stop_rx - 停止接收
 */
static void my_uart_stop_rx(struct uart_port *port) { /* 关接收中断 */ }

/**
 * enable_ms - 开启 modem 状态变化中断（可选）
 */
static void my_uart_enable_ms(struct uart_port *port) {}

/**
 * startup - 打开串口时调用一次
 * 申请中断、初始化硬件
 */
static int my_uart_startup(struct uart_port *port) {
  struct my_uart_priv *priv = container_of(port, struct my_uart_priv, port);
  int ret;

  /* 申请中断 */
  ret = devm_request_irq(port->dev, priv->irq, my_uart_irq_handler,
                         IRQF_TRIGGER_HIGH, dev_name(port->dev), priv);
  if (ret)
    return ret;

  /* 使能硬件：开接收中断、使能 UART */
  writel(0x01, priv->membase + REG_CTRL); /* 使能位 */

  return 0;
}

/**
 * shutdown - 关闭串口时调用
 * 释放中断、关闭硬件
 */
static void my_uart_shutdown(struct uart_port *port) {
  struct my_uart_priv *priv = container_of(port, struct my_uart_priv, port);

  /* 关硬件 */
  writel(0x00, priv->membase + REG_CTRL);

  /* 释放中断 (devm 自动释放，但标记一下) */
  /* devm_free_irq(port->dev, priv->irq, priv); */
}

/**
 * set_termios - 设置波特率、数据位、校验位、停止位
 * 核心工作：根据 termios 计算、设置硬件分频器
 */
static void my_uart_set_termios(struct uart_port *port,
                                struct ktermios *termios,
                                struct ktermios *old) {
  struct my_uart_priv *priv = container_of(port, struct my_uart_priv, port);
  unsigned int baud, cflag, old_cflag;
  unsigned int quot;

  /* 保存旧设置 */
  old_cflag = old ? old->c_cflag : termios->c_cflag;
  cflag = termios->c_cflag;

  /* 计算波特率 */
  baud = uart_get_baud_rate(port, termios, old, 0, 460800);
  quot = port->uartclk / (16 * baud); /* 分频系数 */

  /* 设置硬件分频器 */
  writel(quot, priv->membase + REG_DIV);

  /* 更新 port 结构体中的设置 */
  port->read_status_mask = 0;
  if (termios->c_iflag & INPCK)
        port->read_status_mask |= (???);  /* 根据硬件错误位定义 */

  /* 通知 serial core 波特率已更新 */
  uart_update_timeout(port, cflag, baud);
}

/**
 * type - 返回驱动名称字符串
 */
static const char *my_uart_type(struct uart_port *port) { return "MY_UART"; }

/**
 * release_port / request_port / config_port / verify_port
 * 标准端口资源管理（通常需要实现）
 */
static void my_uart_release_port(struct uart_port *port) {
  struct my_uart_priv *priv = container_of(port, struct my_uart_priv, port);
  iounmap(priv->membase);
}

static int my_uart_request_port(struct uart_port *port) {
  /* 内存资源通常已在 probe 中申请，这里返回 0 即可 */
  return 0;
}

static void my_uart_config_port(struct uart_port *port, int type) {
  if (type & UART_CONFIG_TYPE)
    my_uart_request_port(port);
  port->type = PORT_MYUART; /* 定义在 <linux/serial_core.h> 中 */
}

static int my_uart_verify_port(struct uart_port *port,
                               struct serial_struct *ser) {
  if (ser->type != PORT_UNKNOWN && ser->type != PORT_MYUART)
    return -EINVAL;
  return 0;
}

/* ============================================================
 * 第 3 部分：uart_ops 结构体
 * ============================================================ */

static const struct uart_ops my_uart_ops = {
    .tx_empty = my_uart_tx_empty,
    .set_mctrl = my_uart_set_mctrl,
    .get_mctrl = my_uart_get_mctrl,
    .stop_tx = my_uart_stop_tx,
    .start_tx = my_uart_start_tx,
    .stop_rx = my_uart_stop_rx,
    .enable_ms = my_uart_enable_ms,
    .startup = my_uart_startup,
    .shutdown = my_uart_shutdown,
    .set_termios = my_uart_set_termios,
    .type = my_uart_type,
    .release_port = my_uart_release_port,
    .request_port = my_uart_request_port,
    .config_port = my_uart_config_port,
    .verify_port = my_uart_verify_port,
};

/* ============================================================
 * 第 4 部分：中断处理函数
 * ============================================================ */

static irqreturn_t my_uart_irq_handler(int irq, void *dev_id) {
  struct my_uart_priv *priv = dev_id;
  struct uart_port *port = &priv->port;
  struct tty_port *tport = &port->state->port;
  unsigned long flags;
  u32 status;

  spin_lock_irqsave(&port->lock, flags);

  status = readl(priv->membase + REG_STATUS);

  /* RX 中断：有数据可读 */
  if (status & STATUS_RX_READY) {
    u32 ch;

    /* 从硬件读一个字节 */
    ch = readl(priv->membase + REG_RX);
    port->icount.rx++;

    /* 将数据插入 flip buffer，传给 line discipline */
    tty_insert_flip_char(tport, ch, TTY_NORMAL);
  }

  /* TX 中断：发送 FIFO 空，可以继续发送（一般用 start_tx 轮询即可） */
  if (status & STATUS_TX_EMPTY) {
    /* 如果使用 TX 中断方式，在这里调用 my_uart_start_tx(port) */
  }

  spin_unlock_irqrestore(&port->lock, flags);

  /* 将 flip buffer 中的数据推给 line discipline */
  tty_flip_buffer_push(tport);

  return IRQ_HANDLED;
}

/* ============================================================
 * 第 5 部分：Console 支持（可选，但建议实现）
 * ============================================================ */

static void my_uart_console_write(struct console *co, const char *s,
                                  unsigned int count) {
  struct my_uart_priv *priv;
  struct uart_port *port;
  int i;

  /* 从 co->index 找到对应的 uart_port */
  port = &((struct my_uart_priv *)container_of(co, ...))->port;
  /* 简化：如果只有一个端口，可以直接用全局变量 */

  for (i = 0; i < count; i++) {
    /* 等待 TX 空 */
    /* while (!(readl(priv->membase + REG_STATUS) & STATUS_TX_EMPTY)); */
    /* 写字符 */
    /* writel(s[i], priv->membase + REG_TX); */
  }
}

static struct tty_driver *my_uart_console_device(struct console *co,
                                                 int *index) {
  struct uart_driver *drv = co->data;
  *index = co->index;
  return drv->tty_driver;
}

static struct console my_uart_console = {
    .name = "ttyMYUART", /* 对应 cmdline 的 console=ttyMYUART0 */
    .write = my_uart_console_write,
    .device = my_uart_console_device,
    .flags = CON_PRINTBUFFER,
    .index = -1, /* -1 = 自动选择 */
};

/* ============================================================
 * 第 6 部分：uart_driver 结构体
 * ============================================================ */

static struct uart_driver my_uart_drv = {
    .owner = THIS_MODULE,
    .driver_name = "my_uart", /* /proc/tty/drivers 中显示的名称 */
    .dev_name = "ttyMYUART",  /* 设备节点名 → /dev/ttyMYUART0 */
    .major = 0,               /* 0 = 内核动态分配 */
    .minor = 0,               /* 起始次设备号 */
    .nr = 1,                  /* 支持的串口数量 */
    .cons = &my_uart_console, /* 绑定 console（可选） */
};

/* ============================================================
 * 第 7 部分：probe / remove
 * ============================================================ */

static int my_uart_probe(struct platform_device *pdev) {
  struct my_uart_priv *priv;
  struct uart_port *port;
  struct resource *res;
  int ret;

  /* 1. 分配私有数据结构 */
  priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
  if (!priv)
    return -ENOMEM;

  /* 2. 获取硬件资源 */
  res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
  priv->membase = devm_ioremap_resource(&pdev->dev, res);
  if (IS_ERR(priv->membase))
    return PTR_ERR(priv->membase);

  priv->irq = platform_get_irq(pdev, 0);
  if (priv->irq < 0)
    return priv->irq;

  /* 3. 填充 uart_port */
  port = &priv->port;
  port->dev = &pdev->dev;
  port->iotype = UPIO_MEM; /* 内存映射 I/O */
  port->membase = priv->membase;
  port->mapbase = res->start; /* 物理地址 */
  port->irq = priv->irq;
  port->uartclk = 24000000; /* 时钟频率，从 clk 或 DTS 获取 */
  port->fifosize = 16;      /* 硬件 FIFO 深度 */
  port->ops = &my_uart_ops;
  port->flags = UPF_BOOT_AUTOCONF;
  port->type = PORT_MYUART;
  port->line = 0; /* 端口号，从 pdev->id 或 DTS 获取 */

  /* 4. 将平台设备与私有数据关联 */
  platform_set_drvdata(pdev, priv);

  /* 5. 注册到 serial core */
  ret = uart_add_one_port(&my_uart_drv, port);
  if (ret)
    return ret;

  dev_info(&pdev->dev, "My UART probed, irq=%d\n", priv->irq);
  return 0;
}

static int my_uart_remove(struct platform_device *pdev) {
  struct my_uart_priv *priv = platform_get_drvdata(pdev);

  uart_remove_one_port(&my_uart_drv, &priv->port);
  return 0;
}

/* ============================================================
 * 第 8 部分：设备树匹配表 + platform_driver
 * ============================================================ */

static const struct of_device_id my_uart_of_match[] = {
    {
        .compatible = "vendor,my-uart",
    },
    {/* sentinel */},
};
MODULE_DEVICE_TABLE(of, my_uart_of_match);

static struct platform_driver my_uart_platform_driver = {
    .probe = my_uart_probe,
    .remove = my_uart_remove,
    .driver =
        {
            .name = "my_uart",
            .of_match_table = my_uart_of_match,
        },
};

/* ============================================================
 * 第 9 部分：模块初始化/退出
 * ============================================================ */

static int __init my_uart_init(void) {
  int ret;

  /* 必须先注册 uart_driver，再注册 platform_driver */
  /* 需要先建立 TTY 框架 */
  ret = uart_register_driver(&my_uart_drv);
  if (ret)
    return ret;

  /* 等待硬件操作 */
  ret = platform_driver_register(&my_uart_platform_driver);
  if (ret)
    uart_unregister_driver(&my_uart_drv);

  return ret;
}

static void __exit my_uart_exit(void) {
  platform_driver_unregister(&my_uart_platform_driver);
  uart_unregister_driver(&my_uart_drv);
}

module_init(my_uart_init);
module_exit(my_uart_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("My UART Driver");
