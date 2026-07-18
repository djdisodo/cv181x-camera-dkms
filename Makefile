# SPDX-License-Identifier: GPL-2.0

ifneq ($(KERNELRELEASE),)
obj-m := cv181x-camera-common.o
obj-m += cv181x-csi2.o
obj-m += cv181x-vi.o
obj-m += cv181x-isp.o
obj-m += cv181x-camera.o
cv181x-camera-y := cv181x-core.o
else
KDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
endif
