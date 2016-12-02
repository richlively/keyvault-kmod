ifneq ($(KERNELRELEASE),)
# call from kernel build system

kvmod-objs := kv_mod.o key_vault.o

obj-m	:= kvmod.o

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif

clean:
	rm -rf *.o *.ko *.mod.c *.order *.symvers

