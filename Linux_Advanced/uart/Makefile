export ARCH = arm
export CROSS_COMPILE = arm-linux-gnueabihf-

KERN_DIR = /home/ubuntu/linux/IMX6ULL/linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek

CURRENT_PATH = $(shell pwd)

all:kernel_modules app

kernel_modules:
	$(MAKE) -C $(KERN_DIR) M="$(CURRENT_PATH)" ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

app:
	$(CROSS_COMPILE)gcc -o uartAPP uartAPP.c

clean:
	$(MAKE) -C $(KERN_DIR) M="$(CURRENT_PATH)" clean
	rm -rf modules.order uartAPP

.PHONY:
	build kernel_modules clean app

obj-m += uart.o