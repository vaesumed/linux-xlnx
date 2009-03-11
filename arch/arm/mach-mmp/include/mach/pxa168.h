#ifndef __ASM_MACH_PXA168_H
#define __ASM_MACH_PXA168_H

#include <mach/devices.h>

static inline int pxa168_add_uart1(void)
{
	return pxa_register_device(&pxa168_device_uart1, NULL, 0);
}

static inline int pxa168_add_uart2(void)
{
	return pxa_register_device(&pxa168_device_uart2, NULL, 0);
}

static inline int pxa168_add_uart3(void)
{
	return pxa_register_device(&pxa168_device_uart3, NULL, 0);
}
#endif /* __ASM_MACH_PXA168_H */
