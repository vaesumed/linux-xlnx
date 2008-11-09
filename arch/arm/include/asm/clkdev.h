/*
 *  arch/arm/include/asm/clkdev.h
 *
 *  Copyright (C) 2008 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Helper for the clk API to assist looking up a struct clk.
 */
#ifndef __ASM_CLKDEV_H
#define __ASM_CLKDEV_H

struct clk;

#include <mach/clkdev.h>

struct clk_lookup *clkdev_add(struct clk *clk, const char *con_id,
	const char *dev_fmt, ...);

void clkdev_remove(struct clk_lookup *cl);

#endif
