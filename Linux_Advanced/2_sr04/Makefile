# 1. 使用不同的开发板内核时，要修改KERN_DIR
# 2. KERN_DIR中的内核要事先配置、编译，为了能够编译内核，要先设置以下环境变量
# 2.1 设置目标架构 ARCH
# export ARCH=arm
# 2.2 设置交叉编译器前缀 CROSS_COMPILE
# export CROSS_COMPILE=arm-linux-gnueabihf-
# 2.3 
# export PATH=$PATH:/usr/bin
# 注意：不同的开发板不同的编译器上述三个环境变量不一定相同，需要参考高级用户使用手册

KERN_DIR := /home/ubuntu/linux/IMX6ULL/linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek # 板子所用内核源码的目录
CURRENT_PATH := $(shell pwd)

build: kernel_modules
# -C:跳转目录 -M:外部模块源码目录 moudules:目标
# 先进入内核源码树，再按内核的模块规则，去编译M指定目录里的外部模块，目标是 modules
kernel_modules:
	$(MAKE) -C $(KERN_DIR) M="$(CURRENT_PATH)" modules
clean:
	$(MAKE) -C $(KERN_DIR) M="$(CURRENT_PATH)" clean

.PHONY: build kernel_modules clean

# 参考内核源码drivers/char/ipmi/Makefile
# 要想把a.c, b.c编译成ab.ko，可以这样指定：
# ab-y := a.o b.o
# obj-m += ab.o

obj-m += 2_sr04.o