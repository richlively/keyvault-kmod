ifneq ($(KERNELRELEASE),)
# call from kernel build system

kv-objs := kv_mod.c

obj-m	:= kv_mod.o

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif

clean:
	rm -rf *.o *.ko *.mod.c *.order *.symvers

