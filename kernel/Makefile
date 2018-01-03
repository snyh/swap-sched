obj-m := ui-swap.o

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all: build

build:
	$(MAKE) -C $(KERNELDIR) M=$(PWD)

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -rf .tmp_versions
	rm Module.symvers
	rm modules.order
