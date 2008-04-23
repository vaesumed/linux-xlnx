#ifndef ASM_X86_KMEMCHECK_32_H
#define ASM_X86_KMEMCHECK_32_H

#include <linux/percpu.h>
#include <asm/pgtable.h>

enum kmemcheck_method {
	KMEMCHECK_READ,
	KMEMCHECK_WRITE,
};

#ifdef CONFIG_KMEMCHECK
void kmemcheck_prepare(struct pt_regs *regs);

void kmemcheck_show(struct pt_regs *regs);
void kmemcheck_hide(struct pt_regs *regs);

void kmemcheck_access(struct pt_regs *regs,
	unsigned long address, enum kmemcheck_method method);
#endif

#endif
