/*
 * This file is designed to be included in the boot system
 * so must be as minimal as possible
 */
#ifndef _ASM_VOYAGER_BIOS_H
#define _ASM_VOYAGER_BIOS_H
#include <linux/types.h>

/* non voyager signature in the len field (voyager bios length is small) */
#define NOT_VOYAGER_BIOS_SIG	0xff

struct voyager_bios_info {
	__u8	len;
	__u8	major;
	__u8	minor;
	__u8	debug;
	__u8	num_classes;
	__u8	class_1;
	__u8	class_2;
};

#endif /* _ASM_VOYAGER_BIOS_H */
