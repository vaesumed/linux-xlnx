#
# USB peripheral controller drivers
#
ifeq ($(CONFIG_USB_GADGET_DEBUG),y)
	EXTRA_CFLAGS		+= -DDEBUG
endif

obj-$(CONFIG_USB_DUMMY_HCD)	+= dummy_hcd.o
obj-$(CONFIG_USB_NET2280)	+= net2280.o
obj-$(CONFIG_USB_AMD5536UDC)	+= amd5536udc.o
obj-$(CONFIG_USB_PXA25X)	+= pxa25x_udc.o
obj-$(CONFIG_USB_PXA27X)	+= pxa27x_udc.o
obj-$(CONFIG_USB_GOKU)		+= goku_udc.o
obj-$(CONFIG_USB_OMAP)		+= omap_udc.o
obj-$(CONFIG_USB_LH7A40X)	+= lh7a40x_udc.o
obj-$(CONFIG_USB_S3C2410)	+= s3c2410_udc.o
obj-$(CONFIG_USB_AT91)		+= at91_udc.o
obj-$(CONFIG_USB_ATMEL_USBA)	+= atmel_usba_udc.o
obj-$(CONFIG_USB_FSL_USB2)	+= fsl_usb2_udc.o
obj-$(CONFIG_USB_M66592)	+= m66592-udc.o

#
# USB gadget drivers
#

gadget_core-objs		:= composite.o usbstring.o config.o \
					epautoconf.o
gadget_ether_core-objs		:= u_ether.o
gadget_ecm_core-objs		:= f_ecm.o
gadget_acm_core-objs		:= f_acm.o u_serial.o

g_zero-objs			:= zero.o f_sourcesink.o f_loopback.o
g_ether-objs			:= ether.o f_subset.o
g_serial-objs			:= serial.o f_serial.o
g_midi-objs			:= gmidi.o
gadgetfs-objs			:= inode.o
g_file_storage-objs		:= file_storage.o
g_printer-objs			:= printer.o
g_cdc-objs			:= cdc2.o

ifeq ($(CONFIG_USB_ETH_RNDIS),y)
	g_ether-objs		+= f_rndis.o rndis.o
endif
 
obj-$(CONFIG_USB_ZERO)		+= g_zero.o gadget_core.o
obj-$(CONFIG_USB_ETH)		+= g_ether.o gadget_core.o gadget_ether_core.o \
					gadget_ecm_core.o
obj-$(CONFIG_USB_GADGETFS)	+= gadgetfs.o
obj-$(CONFIG_USB_FILE_STORAGE)	+= g_file_storage.o gadget_core.o
obj-$(CONFIG_USB_G_SERIAL)	+= g_serial.o gadget_core.o gadget_acm_core.o
obj-$(CONFIG_USB_G_PRINTER)	+= g_printer.o gadget_core.o
obj-$(CONFIG_USB_MIDI_GADGET)	+= g_midi.o gadget_core.o
obj-$(CONFIG_USB_CDC_COMPOSITE) += g_cdc.o gadget_core.o gadget_ether_core.o \
					gadget_ether_core.o gadget_acm_core.o
