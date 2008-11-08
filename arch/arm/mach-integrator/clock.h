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
struct module;

#include <asm/hardware/icst525.h>

struct clk {
	unsigned long		rate;
	struct module		*owner;
	const struct icst525_params *params;
	void			*data;
	void			(*setvco)(struct clk *, struct icst525_vco vco);
};

struct clk_lookup {
	struct list_head	node;
	const char		*devname;
	struct clk		*clk;
};

int clk_register_lookup(struct clk_lookup *cl);
void clk_unregister_lookup(struct clk_lookup *cl);
