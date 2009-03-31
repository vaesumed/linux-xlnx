#
# Makefile for caching in a mounted filesystem
#

cachefiles-y := \
	cf-bind.o \
	cf-daemon.o \
	cf-interface.o \
	cf-key.o \
	cf-main.o \
	cf-namei.o \
	cf-rdwr.o \
	cf-security.o \
	cf-xattr.o

cachefiles-$(CONFIG_CACHEFILES_HISTOGRAM) += cf-proc.o

obj-$(CONFIG_CACHEFILES) := cachefiles.o
