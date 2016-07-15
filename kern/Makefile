
ifneq ($(KERNELRELEASE),)
# call from kernel build system

uxfs-objs :=ux_inode.o ux_dir.o ux_alloc.o ux_file.o
obj-m	:= uxfs.o
else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD       := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif



clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions

depend .depend dep:
	$(CC) -M *.c > .depend


ifeq (.depend,$(wildcard .depend))
include .depend
endif

