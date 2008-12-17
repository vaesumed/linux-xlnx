#
# Makefile for general filesystem caching code
#

fscache-y := \
	fsc-cache.o \
	fsc-cookie.o \
	fsc-fsdef.o \
	fsc-main.o \
	fsc-netfs.o \
	fsc-object.o \
	fsc-operation.o \
	fsc-page.o

fscache-$(CONFIG_PROC_FS) += fsc-proc.o
fscache-$(CONFIG_FSCACHE_STATS) += fsc-stats.o
fscache-$(CONFIG_FSCACHE_HISTOGRAM) += fsc-histogram.o

obj-$(CONFIG_FSCACHE) := fscache.o
