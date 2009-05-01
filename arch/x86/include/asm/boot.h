#ifndef _ASM_X86_BOOT_H
#define _ASM_X86_BOOT_H

/* Internal svga startup constants */
#define NORMAL_VGA	0xffff		/* 80x25 mode */
#define EXTENDED_VGA	0xfffe		/* 80x50 mode */
#define ASK_VGA		0xfffd		/* ask for it at bootup */

#ifdef __KERNEL__

#include <asm/page_types.h>

/* Permitted physical alignment of the kernel */
#if defined(CONFIG_X86_64) && CONFIG_PHYSICAL_ALIGN < PMD_PAGE_SIZE
#define LOAD_PHYSICAL_ALIGN	PMD_PAGE_SIZE
#else
#define LOAD_PHYSICAL_ALIGN	CONFIG_PHYSICAL_ALIGN
#endif

/* Physical address where kernel should be loaded. */
#define LOAD_PHYSICAL_ADDR ((CONFIG_PHYSICAL_START \
				+ (LOAD_PHYSICAL_ALIGN - 1)) \
				& ~(LOAD_PHYSICAL_ALIGN - 1))

#ifdef CONFIG_KERNEL_BZIP2
#define BOOT_HEAP_SIZE             0x400000
#else /* !CONFIG_KERNEL_BZIP2 */

#ifdef CONFIG_X86_64
#define BOOT_HEAP_SIZE	0x7000
#else
#define BOOT_HEAP_SIZE	0x4000
#endif

#endif /* !CONFIG_KERNEL_BZIP2 */

#ifdef CONFIG_X86_64
#define BOOT_STACK_SIZE	0x4000
#else
#define BOOT_STACK_SIZE	0x1000
#endif

#endif /* __KERNEL__ */

#endif /* _ASM_X86_BOOT_H */
