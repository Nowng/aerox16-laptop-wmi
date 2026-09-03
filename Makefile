obj-m += aerox16-laptop.o

aerox16-laptop-y := aerox16-laptop-probe.o aerox16-laptop-wmi.o aerox16-laptop-sysfs.o aerox16-laptop-hwmon.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

indent:
	indent -linux -l120 -i4 -nut aerox16-laptop-probe.c aerox16-laptop-wmi.c aerox16-laptop-sysfs.c aerox16-laptop-hwmon.c
