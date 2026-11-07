facetimehd-objs := fthd_ddr.o fthd_hw.o fthd_drv.o fthd_ringbuf.o fthd_isp.o fthd_v4l2.o fthd_buffer.o fthd_debugfs.o

# In-tree kernel build support
obj-$(CONFIG_VIDEO_FACETIMEHD) += facetimehd.o

# Out-of-tree standalone / DKMS build fallback
ifeq ($(CONFIG_VIDEO_FACETIMEHD),)
obj-m := facetimehd.o
endif

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
