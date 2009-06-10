/*
 * This file is designed to be included in the boot system
 * so must be as minimal as possible
 */
#ifndef _ASM_VOYAGER_BOOT_H
#define _ASM_VOYAGER_BOOT_H

#include <asm/setup.h>
#include <asm/voyager_bios.h>

#ifdef CONFIG_X86_VOYAGER

static inline int is_voyager(void)
{
	return boot_params.voyager_bios_info.len != NOT_VOYAGER_BIOS_SIG;
}

#else

static inline int is_voyager(void)
{
	return 0;
}

#endif /* CONFIG_X86_VOYAGER */

#endif
