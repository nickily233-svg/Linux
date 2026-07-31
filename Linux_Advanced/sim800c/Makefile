# 设置架构
export ARCH = arm
# 设置交叉编译器
export COESS_COMPILE = arm-linux-gnueabihf-
# 内核路径
KERN_DIR := /home/ubuntu/linux/IMX6ULL/linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek
# 当前地址
CURRENT_PATH = $(shell pwd)

all:kernel_modules app

kernel_modules:
	$(MAKE) -C $(KERN_DIR) M="$(CURRENT_PATH)" ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules
app:
	$(COESS_COMPILE)gcc -o uartAPP uartAPP.c
clean:
	$(MAKE) -C $(KERN_DIR) M="$(CURRENT_PATH)" clean
	rm -rf modules.order uartAPP
# 伪目标
.PHONY: build kernel_modules clean app

# 编译成模块
obj-m += uart.o