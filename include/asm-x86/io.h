#ifndef _ASM_X86_IO_H
#define _ASM_X86_IO_H

#ifdef CONFIG_X86_32
# include "io_32.h"
#else
# include "io_64.h"
#endif

extern int ioremap_change_attr(unsigned long vaddr, unsigned long size,
				unsigned long prot_val);

extern void *xlate_dev_mem_ptr(unsigned long phys);
extern void unxlate_dev_mem_ptr(unsigned long phys, void *addr);

#endif /* _ASM_X86_IO_H */
