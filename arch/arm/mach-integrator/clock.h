/*
 *  linux/arch/arm/mach-integrator/clock.h
 *
 *  Copyright (C) 2004 ARM Limited.
 *  Written by Deep Blue Solutions Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/clkdev.h>

struct clks {
	const char devname[12];
	struct clk *clk;
};
