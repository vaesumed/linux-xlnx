#ifndef __ASM_MACH_CPUTYPE_H
#define __ASM_MACH_CPUTYPE_H

#include <asm/cputype.h>

/*
 *  CPU   Stepping   OLD_ID       CPU_ID      CHIP_ID
 *
 * PXA168    A0    0x41159263   0x56158400   0x00A0A333
 */

#ifndef CONFIG_CPU_MOHAWK_OLD_ID

#ifdef CONFIG_CPU_PXA168
#  define __cpu_is_pxa168(id)	\
	({ unsigned int _id = ((id) >> 8) & 0xff; _id == 0x84; })
#else
#  define __cpu_is_pxa168(id)	(0)
#endif

#else

#ifdef CONFIG_CPU_PXA168
#  define __cpu_is_pxa168(id)	\
	({ unsigned int _id = (id) & 0xffff; _id == 0x9263; })
#else
#  define __cpu_is_pxa168(id)	(0)
#endif

#endif /* CONFIG_CPU_MOHAWK_OLD_ID */

#define cpu_is_pxa168()		({ __cpu_is_pxa168(read_cpuid_id()); })
#define cpu_is_pxa910()		({ __cpu_is_pxa910(read_cpuid_id()); })

#endif /* __ASM_MACH_CPUTYPE_H */
