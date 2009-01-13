#ifndef _CRIS_BYTEORDER_H
#define _CRIS_BYTEORDER_H

#define __LITTLE_ENDIAN
#define __SWAB_64_THRU_32__

#ifdef __KERNEL__
# include <arch/byteorder.h>
#endif

#include <linux/byteorder.h>

#endif


