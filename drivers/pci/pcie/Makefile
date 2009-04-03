#
# Makefile for PCI-Express PORT Driver
#

# Build PCI Express ASPM if needed
obj-$(CONFIG_PCIEASPM)		+= aspm.o

obj-$(CONFIG_PCIE_ECRC)		+= ecrc.o

pcieportdrv-y			:= portdrv_core.o portdrv_pci.o portdrv_bus.o

obj-$(CONFIG_PCIEPORTBUS)	+= pcieportdrv.o

# Build PCI Express AER if needed
obj-$(CONFIG_PCIEAER)		+= aer/
