facetimehd-objs := fthd_hw.o fthd_drv.o fthd_ringbuf.o fthd_isp.o fthd_v4l2.o fthd_buffer.o fthd_debugfs.o
obj-m := facetimehd.o

KVERSION := $(KERNELRELEASE)
ifeq ($(origin KERNELRELEASE), undefined)
KVERSION := $(shell uname -r)
endif
KDIR := /lib/modules/$(KVERSION)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
