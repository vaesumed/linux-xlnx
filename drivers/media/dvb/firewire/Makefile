obj-$(CONFIG_DVB_FIREDTV) += firedtv.o

firedtv-y := avc.o firedtv-ci.o firedtv-dvb.o firedtv-fe.o firedtv-rc.o
firedtv-$(CONFIG_DVB_FIREDTV_IEEE1394) += firedtv-1394.o

EXTRA_CFLAGS := -Idrivers/ieee1394
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
