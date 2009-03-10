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
#include <linux/io.h>
#include <linux/clk.h>

#include <mach/addr-map.h>
#include <mach/cputype.h>
#include <mach/regs-apbc.h>
#include <mach/irqs.h>
#include <mach/gpio.h>
#include <mach/dma.h>
#include <mach/mfp.h>

#include "common.h"
#include "clock.h"

#define MFPR_VIRT_BASE	(APB_VIRT_BASE + 0x1e000)

static struct mfp_addr_map pxa168_mfp_addr_map[] __initdata =
{
	MFP_ADDR_X(GPIO0,   GPIO36,  0x04c),
	MFP_ADDR_X(GPIO37,  GPIO55,  0x000),
	MFP_ADDR_X(GPIO56,  GPIO123, 0x0e0),
	MFP_ADDR_X(GPIO124, GPIO127, 0x0f4),

	MFP_ADDR_END,
};

#define APMASK(i)	(GPIO_REGS_VIRT + BANK_OFF(i) + 0x09c)

static void __init pxa168_init_gpio(void)
{
	int i;

	clk_enable(&clk_pxa168_gpio);

	/* unmask GPIO edge detection for all 4 banks - APMASKx */
	for (i = 0; i < 4; i++)
		__raw_writel(0xffffffff, APMASK(i));

	pxa_init_gpio(IRQ_PXA168_GPIOX, 0, 127, NULL);
}

void __init pxa168_init_irq(void)
{
	icu_init_irq();
	pxa168_init_gpio();
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
		mfp_init_base(MFPR_VIRT_BASE);
		mfp_init_addr(pxa168_mfp_addr_map);
		pxa_init_dma(IRQ_PXA168_DMA_INT0, 32);
		clks_register(ARRAY_AND_SIZE(pxa168_clkregs));
	}

	return 0;
}
postcore_initcall(pxa168_init);
