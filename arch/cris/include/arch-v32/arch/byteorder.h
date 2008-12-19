#ifndef _ASM_CRIS_ARCH_BYTEORDER_H
#define _ASM_CRIS_ARCH_BYTEORDER_H

#include <asm/types.h>

static inline __const__ __u32
__arch_swab32(__u32 x)
{
	__asm__ __volatile__ ("swapwb %0" : "=r" (x) : "0" (x));
	return (x);
}
#define __arch_swab32 __arch_swab32

static inline __const__ __u16
__arch_swab16(__u16 x)
{
	__asm__ __volatile__ ("swapb %0" : "=r" (x) : "0" (x));
	return (x);
}
#define __arch_swab16 __arch_swab16

#endif /* _ASM_CRIS_ARCH_BYTEORDER_H */
