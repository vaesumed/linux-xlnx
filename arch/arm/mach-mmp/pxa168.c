/*
 *  linux/arch/arm/mach-mmp/pxa168.c
 *
 *  Code specific to PXA168
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>

#include <mach/addr-map.h>
#include <mach/cputype.h>
#include <mach/regs-apbc.h>
#include <mach/irqs.h>
#include <mach/dma.h>

#include "common.h"
#include "clock.h"

void __init pxa168_init_irq(void)
{
	icu_init_irq();
}

/* clocks for external use */
APBC_CLK(pxa168_gpio, PXA168_GPIO, 0, 0);
APBC_CLK(pxa168_timers, PXA168_TIMERS, 3, 3250000);

/* APB peripheral clocks */
static APBC_CLK(uart1, PXA168_UART1, 1, 14745600);
static APBC_CLK(uart2, PXA168_UART2, 1, 14745600);
static APBC_CLK(uart3, PXA168_UART3, 1, 14745600);

static struct clk_lookup pxa168_clkregs[] = {
	INIT_CLKREG(&clk_uart1, "pxa2xx-uart.0", NULL),
	INIT_CLKREG(&clk_uart2, "pxa2xx-uart.1", NULL),
	INIT_CLKREG(&clk_uart3, "pxa2xx-uart.2", NULL),
};

static int __init pxa168_init(void)
{
	if (cpu_is_pxa168()) {
		pxa_init_dma(IRQ_PXA168_DMA_INT0, 32);
		clks_register(ARRAY_AND_SIZE(pxa168_clkregs));
	}

	return 0;
}
postcore_initcall(pxa168_init);
