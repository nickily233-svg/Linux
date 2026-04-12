KERNELKDIR := /home/ubuntu/linux/IMX6ULL/linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek
CURRENT_PATH := $(shell pwd)
obj-m := chrdevbase.o

build: kernel_modules
#-C:跳转目录 -M:外部模块源码目录 moudules:目标
#先进入内核源码树，再按内核的模块规则，去编译M指定目录里的外部模块，目标是 modules
kernel_modules:
	$(MAKE) -C $(KERNELKDIR) M=$(CURRENT_PATH) modules
clean:
	$(MAKE) -C $(KERNELKDIR) M=$(CURRENT_PATH) clean