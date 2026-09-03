obj-m += aerox16-laptop.o

aerox16-laptop-y := aerox16-laptop-probe.o aerox16-laptop-wmi.o aerox16-laptop-sysfs.o aerox16-laptop-hwmon.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

install:
	mkdir -p /lib/modules/$(shell uname -r)/extra
	cp aerox16-laptop.ko /lib/modules/$(shell uname -r)/extra/
	cp aerox16-laptop.conf /etc/modules-load.d/
	depmod -a
	modprobe aerox16_laptop


uninstall:
	rmmod aerox16_laptop
	rm /etc/modules-load.d/aerox16-laptop.conf
	rm /lib/modules/$(shell uname -r)/extra/aerox16-laptop.ko

indent:
	indent -linux -l120 -i4 -nut aerox16-laptop-probe.c aerox16-laptop-wmi.c aerox16-laptop-sysfs.c aerox16-laptop-hwmon.c
