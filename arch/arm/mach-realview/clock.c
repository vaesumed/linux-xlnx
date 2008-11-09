/*
 *  linux/arch/arm/mach-realview/clock.c
 *
 *  Copyright (C) 2004 ARM Limited.
 *  Written by Deep Blue Solutions Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/mutex.h>

#include <asm/hardware/icst307.h>

#include "clock.h"

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);

struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk_lookup *p;
	struct clk *clk = ERR_PTR(-ENOENT);
	const char *devid = dev_name(dev);

	mutex_lock(&clocks_mutex);
	list_for_each_entry(p, &clocks, node) {
		if (strcmp(devid, p->devname) == 0) {
			clk = p->clk;
			break;
		}
	}
	mutex_unlock(&clocks_mutex);

	return clk;
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);

int clk_enable(struct clk *clk)
{
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	struct icst307_vco vco;
	vco = icst307_khz_to_vco(clk->params, rate / 1000);
	return icst307_khz(clk->params, vco) * 1000;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -EIO;

	if (clk->setvco) {
		struct icst307_vco vco;

		vco = icst307_khz_to_vco(clk->params, rate / 1000);
		clk->rate = icst307_khz(clk->params, vco) * 1000;
		clk->setvco(clk, vco);
		ret = 0;
	}
	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

int clk_register_lookup(struct clk_lookup *cl)
{
	mutex_lock(&clocks_mutex);
	list_add(&cl->node, &clocks);
	mutex_unlock(&clocks_mutex);
	return 0;
}
EXPORT_SYMBOL(clk_register_lookup);

void clk_unregister_lookup(struct clk_lookup *cl)
{
	mutex_lock(&clocks_mutex);
	list_del(&cl->node);
	mutex_unlock(&clocks_mutex);
}
EXPORT_SYMBOL(clk_unregister_lookup);
