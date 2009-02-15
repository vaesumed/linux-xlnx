obj-$(CONFIG_DVB_FIREDTV) += firedtv.o

firedtv-y := avc.o firedtv-ci.o firedtv-dvb.o firedtv-fe.o firedtv-rc.o
firedtv-$(CONFIG_DVB_FIREDTV_IEEE1394) += firedtv-1394.o

ccflags-y += -Idrivers/media/dvb/dvb-core
ccflags-$(CONFIG_DVB_FIREDTV_IEEE1394) += -Idrivers/ieee1394
