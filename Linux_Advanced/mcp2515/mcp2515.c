/* 1. 标准 Linux 内核基础头文件 */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>

/* 2. SPI 总线与 GPIO 头文件 */
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/spi/spi.h>

/* 3. 网络子系统核心头文件（必须放在 CAN 头文件之前！） */
#include <linux/etherdevice.h>
#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

/* 4. CAN 子系统头文件（放在网络头文件之后！） */
#include <linux/can/dev.h>
#include <linux/can/error.h>

/* 5. 其他必要文件 */
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/string.h>
#include <linux/workqueue.h>

/* === MCP2515 寄存器地址 === */
#define MCP2515_REG_CANCTRL 0x0F
#define MCP2515_REG_CANINTE 0x2B  /* 中断使能寄存器 */
#define MCP2515_REG_CANINTF 0x2C  /* 中断标志寄存器 */
#define MCP2515_REG_TXB0CTRL 0x30 /* 发送缓冲区 0 控制 */
#define MCP2515_REG_TXB0SIDH 0x31 /* 发送缓冲区 ID 高位 */
#define MCP2515_REG_TXB0SIDL 0x32 /* 发送缓冲区 ID 低位 */
#define MCP2515_REG_TXB0D0 0x36   /* 发送数据字节 0 */
#define MCP2515_REG_RXB0CTRL 0x60 /* 接收缓冲区 0 控制 */
#define MCP2515_REG_RXB0SIDH 0x61
#define MCP2515_REG_RXB0SIDL 0x62
#define MCP2515_REG_RXB0D0 0x62

/* === 寄存器控制位 === */
#define MCP2515_CTRL_REQOP_NORMAL 0x00 /* 正常模式 */
#define MCP2515_CTRL_REQOP_SLEEP 0x20  /* 睡眠模式 */

#define MCP2515_INT_RX0 0x01 /* 接收缓冲区 0 有数据 */
#define MCP2515_INT_TX0 0x04 /* 发送缓冲区 0 发送完成 */

#define MCP2515_TXB_TXREQ 0x08 /* 发送请求位 */

#define MCP2515_REG_CNF1 0x2A
#define MCP2515_REG_CNF2 0x29
#define MCP2515_REG_CNF3 0x28

struct mcp2515_priv {
  struct spi_device *spi;
  struct net_device *net_dev;
  struct gpio_desc *gpio;
  int irq;
  struct work_struct work;
  struct sk_buff *tx_skb;
};

static int spi_write_reg(struct spi_device *spi, unsigned char reg_addr,
                         unsigned char data) {
  struct spi_message spi_msg;
  struct spi_transfer xfer;
  unsigned char tx_buf[3];

  /* 1. 构造SPI帧 命令(0x02)+地址+数据 */
  tx_buf[0] = 0x02;
  tx_buf[1] = reg_addr;
  tx_buf[2] = data;

  /* 2. 初始化SPI消息结构体 */
  spi_message_init(&spi_msg);

  /* 3. 设置传输参数 */
  memset(&xfer, 0, sizeof(xfer));
  xfer.tx_buf = tx_buf;
  xfer.len = 3;

  /* 4. 将传输加进消息队列，并同步发送 */
  spi_message_add_tail(&xfer, &spi_msg);

  return spi_sync(spi, &spi_msg);
}

static int spi_read_reg(struct spi_device *spi, unsigned char reg_addr,
                        unsigned char *data) {
  struct spi_message spi_msg;
  struct spi_transfer xfer[2];
  unsigned char rx_buf[1];
  unsigned char tx_buf[2];

  /* 1. 构造SPI帧 读取指令(0x03)+地址 */
  tx_buf[0] = 0x03;
  tx_buf[1] = reg_addr;

  /* 2. 初始化SPI消息 */
  spi_message_init(&spi_msg);
  memset(&xfer, 0, sizeof(xfer));

  /* 3. 先发送至和地址，再紧接着读取1个字节的数据 */
  xfer[0].tx_buf = tx_buf;
  xfer[0].len = 2;
  spi_message_add_tail(&xfer[0], &spi_msg);

  xfer[1].rx_buf = rx_buf;
  xfer[1].len = 1;
  spi_message_add_tail(&xfer[1], &spi_msg);

  /* 4. 同步发送 */
  spi_sync(spi, &spi_msg);

  /* 5. 把读到的数据传回调用者 */
  *data = rx_buf[0];
  return 0;
}

static void mcp2515_tx_work_handler(struct work_struct *work) {
  struct mcp2515_priv *priv = container_of(work, struct mcp2515_priv, work);
  struct net_device *dev = priv->net_dev;
  unsigned char intf = 0;

  /* 1. 读一下是什么中断触发了 */
  spi_read_reg(priv->spi, MCP2515_REG_CANINTF, &intf);

  /* 2. 如果是发送完成中断 (TX0) */
  if (intf & MCP2515_INT_TX0) {
    /* 唤醒网络队列，允许继续发送下一帧 */
    netif_wake_queue(dev);
    /* 如果有暂存的 skb，释放它（因为你之前用的
     * can_put_echo_skb，这里发送成功其实不需要手动释放 skb） */
    if (priv->tx_skb) {
      priv->tx_skb = NULL;
    }
  }

  /* 3. 如果是接收中断 (RX0) */
  if (intf & MCP2515_INT_RX0) {
    struct sk_buff *skb;
    struct can_frame *frame;
    u8 sidh, sidl;
    u8 i;

    /* 读取接收缓冲区的 ID */
    spi_read_reg(priv->spi, MCP2515_REG_RXB0SIDH, &sidh);
    spi_read_reg(priv->spi, MCP2515_REG_RXB0SIDL, &sidl);

    /* 分配一个网络数据包 (skb) */
    skb = alloc_can_skb(dev, &frame);
    if (!skb) {
      goto out_clear;
    }

    /* 还原 CAN ID (11 位标准帧) */
    frame->can_id = (sidh << 3) | (sidl >> 5);

    /* 读取 8 个字节的数据 */
    for (i = 0; i < 8; i++) {
      spi_read_reg(priv->spi, MCP2515_REG_RXB0D0 + i, &frame->data[i]);
    }

    /* 把收到的数据包交给 Linux 内核网络协议栈 */
    netif_rx(skb);
  }

out_clear:
  /* 4. 重要！清除中断标志位，否则芯片不会再触发中断 */
  spi_write_reg(priv->spi, MCP2515_REG_CANINTF, intf);
}

static irqreturn_t mcp2515_isr(int irq, void *dev_id) {
  struct mcp2515_priv *priv = dev_id;
  /* 发现中断发生，立即唤醒工作队列去处理 */
  schedule_work(&priv->work);
  return IRQ_HANDLED;
}

/* 打开网卡 */
static int mcp2515_open(struct net_device *dev) {
  struct mcp2515_priv *priv = netdev_priv(dev);
  unsigned char int_enable = MCP2515_INT_RX0 | MCP2515_INT_TX0;
  /* 在这里通过 SPI 向 MCP2515 发送“开启”指令 */
  /* 1. 配置CAN波特率 */
  spi_write_reg(priv->spi, MCP2515_REG_CNF1, 0x00);
  spi_write_reg(priv->spi, MCP2515_REG_CNF2, 0x94);
  spi_write_reg(priv->spi, MCP2515_REG_CNF3, 0x02);
  /* 2. SPI写寄存器，设置CANCTRL为正常模式 */
  spi_write_reg(priv->spi, MCP2515_REG_CANCTRL,
                (unsigned char)MCP2515_CTRL_REQOP_NORMAL);

  /* 3. 使能硬件中断（开启接收和发送完成中断） */
  spi_write_reg(priv->spi, MCP2515_REG_CANINTE, (unsigned char)int_enable);

  /* 4. 启动网络队列 */
  netif_start_queue(dev);
  return 0;
}

/* 关闭网卡 */
static int mcp2515_stop(struct net_device *dev) {
  struct mcp2515_priv *priv = netdev_priv(dev);
  /* 1. 停止网络队列 */
  netif_stop_queue(dev);
  /* 2. 在这里通过 SPI 向 MCP2515 发送“关闭”指令 */
  spi_write_reg(priv->spi, (unsigned char)MCP2515_REG_CANCTRL,
                (unsigned char)MCP2515_CTRL_REQOP_SLEEP);
  return 0;
}

/* 发送CAN数据包 */
static netdev_tx_t mcp2515_start_xmit(struct sk_buff *skb,
                                      struct net_device *dev) {
  struct mcp2515_priv *priv = netdev_priv(dev);

  /* 1. 保护检查：如果还在发上一包，就告诉上层等一下 */
  if (priv->tx_skb) {
    netif_stop_queue(dev);
    return NETDEV_TX_BUSY;
  }

  /* 2. 先停掉发送队列 */
  netif_stop_queue(dev);

  /* 3. 把数据包存进私有结构里，留着让工作队列慢慢发 */
  priv->tx_skb = skb;

  /* 4. 唤醒工作队列去干活 */
  schedule_work(&priv->work);

  return NETDEV_TX_OK;
}

static struct net_device_ops mcp2515_net_fops = {
    .ndo_open = mcp2515_open,
    .ndo_stop = mcp2515_stop,
    .ndo_start_xmit = mcp2515_start_xmit,
};

static int mcp2515_probe(struct spi_device *spi) {
  struct mcp2515_priv *priv;
  struct net_device *net_dev;
  int ret;

  /*  1. 让内核分配网络设备和私有数据 */
  net_dev = alloc_candev(sizeof(struct mcp2515_priv), 16);
  if (!net_dev) {
    dev_err(&spi->dev, "can not allocate net dev\n");
    return -ENOMEM;
  }

  /* 2. 通过netdev_priv拿到私有数据指针 */
  priv = netdev_priv(net_dev);
  priv->spi = spi;
  priv->net_dev = net_dev;

  /* 3. 把net_dev挂载到spi总线上，便于在remove中取出 */
  spi_set_drvdata(spi, net_dev);

  /* 4. 绑定网络设备的操作接口 */
  net_dev->netdev_ops = &mcp2515_net_fops;

  /* 5. 申请 GPIO 和中断 */
  priv->gpio = devm_gpiod_get(&spi->dev, "mcp2515", GPIOD_IN);
  if (IS_ERR(priv->gpio)) {
    ret = PTR_ERR(priv->gpio);
    goto err_free_netdev;
  }

  INIT_WORK(&priv->work, mcp2515_tx_work_handler);

  priv->irq = gpiod_to_irq(priv->gpio);
  if (priv->irq < 0) {
    ret = priv->irq;
    goto err_free_netdev;
  }

  ret = devm_request_irq(&spi->dev, priv->irq, mcp2515_isr,
                         IRQF_TRIGGER_FALLING, "mcp2515_irq", priv);
  if (ret) {
    dev_err(&spi->dev, "can not request irq\n");
    goto err_free_netdev;
  }

  /* 6. 注册CAN网络设备 */
  ret = register_candev(net_dev);
  if (ret) {
    dev_err(&spi->dev, "register can dev failed\n");
    goto err_free_netdev;
  }

  dev_info(&spi->dev, "mcp2515 driver has been initialized\n");

  return 0;

err_free_netdev:
  free_candev(net_dev);
  return ret;
}

static int mcp2515_remove(struct spi_device *spi) {
  struct net_device *net_dev = spi_get_drvdata(spi);
  if (net_dev) {
    unregister_candev(net_dev);
    free_candev(net_dev);
  }
  return 0;
}

static struct of_device_id mcp2515_of_match_table[] = {
    {.compatible = "mcp2515"},
    {/* sentinel */},
};
MODULE_DEVICE_TABLE(of, mcp2515_of_match_table);

static struct spi_device_id mcp2515_id_table[] = {
    {"mcp2515", 0},
    {/* sentinel */},
};
MODULE_DEVICE_TABLE(spi, mcp2515_id_table);

static struct spi_driver mcp2515_drv = {
    .driver =
        {
            .name = "mcp2515",
            .of_match_table = mcp2515_of_match_table,
        },
    .probe = mcp2515_probe,
    .remove = mcp2515_remove,
    .id_table = mcp2515_id_table,
};

module_spi_driver(mcp2515_drv);

MODULE_LICENSE("GPL");