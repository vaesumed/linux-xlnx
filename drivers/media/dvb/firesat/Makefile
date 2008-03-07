dvb-firesat-objs := firesat.o avc_api.o cmp.o firesat-rc.o firesat-ci.o

obj-$(CONFIG_DVB_FIRESAT) += dvb-firesat.o

EXTRA_CFLAGS := -Idrivers/ieee1394
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
#EXTRA_CFLAGS += -I/usr/src/linux-headers-$(shell uname -r)
#EXTRA_CFLAGS += -I/usr/src/linux-headers-$(shell uname -r)/include

#EXTRA_LFLAGS := -L/proc/kallsyms

