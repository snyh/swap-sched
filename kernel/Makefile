obj-m := uicache.o fault.o
uicache-objs := pg.o hook.o main.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all: build test

build:
	$(MAKE) -C $(KERNELDIR) M=$(PWD)

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -rf .tmp_versions
	rm Module.symvers
	rm modules.order

test:
	-@sudo rmmod fault
	-@sudo rmmod uicache
	sudo insmod ./uicache.ko
	sudo insmod ./fault.ko
	sudo dmesg | tail
