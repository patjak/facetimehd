ifneq ($(KERNELRELEASE),)
	facetimehd-objs := fthd_ddr.o fthd_hw.o fthd_drv.o fthd_ringbuf.o fthd_isp.o fthd_v4l2.o fthd_buffer.o fthd_debugfs.o
	obj-m := facetimehd.o
else
	# normal makefile
	KDIR ?= /lib/modules/`uname -r`/build

default:
	$(MAKE) -C $(KDIR) M=$$PWD

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean

install:
	$(MAKE) -C $(KDIR) M=$$PWD modules_install
endif
