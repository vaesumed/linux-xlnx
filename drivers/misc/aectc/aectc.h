#ifndef _SCULL_H_
#define _SCULL_H_

#include <linux/ioctl.h>
#include <linux/types.h>

struct aectc_reg {
	__u16	reg;
	__u16	length;
	__u32	data;
};

#define AECTC_IOC_MAGIC  0xEC

#define AEC_IOC_READREG		_IOWR(AECTC_IOC_MAGIC,  0, struct aectc_reg)
#define AEC_IOC_WRITEREG	_IOW(AECTC_IOC_MAGIC,  0, struct aectc_reg)

#endif
