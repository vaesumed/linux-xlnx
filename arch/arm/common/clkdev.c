/*
 *  arch/arm/common/clkdev.c
 *
 *  Copyright (C) 2008 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Helper for the clk API to assist looking up a struct clk.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/mutex.h>

#include <asm/clkdev.h>

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);

struct clk_lookup {
	struct list_head	node;
	const char		*dev_id;
	const char		*con_id;
	struct clk		*clk;
	char			strings[0];
};

static struct clk *clk_find(const char *dev_id, const char *con_id)
{
	struct clk_lookup *p;
	struct clk *clk = NULL;

	list_for_each_entry(p, &clocks, node) {
		if ((p->dev_id && !dev_id) || (p->con_id && !con_id))
			continue;
		if (p->dev_id && strcmp(p->dev_id, dev_id) != 0)
			continue;
		if (p->con_id && strcmp(p->con_id, con_id) != 0)
			continue;
		/*
		 * If we find more than one match, we failed.
		 */
		if (clk)
			return NULL;
		clk = p->clk;
	}
	return clk;
}

struct clk *clk_get(struct device *dev, const char *con_id)
{
	struct clk *clk;
	const char *dev_id = dev ? dev_name(dev) : NULL;

	mutex_lock(&clocks_mutex);
	clk = clk_find(dev_id, con_id);

	if (clk && !__clk_get(clk))
		clk = NULL;
	mutex_unlock(&clocks_mutex);

	return clk ? clk : ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
	__clk_put(clk);
}
EXPORT_SYMBOL(clk_put);

struct clk_lookup *clkdev_add(struct clk *clk, const char *con_id,
	const char *dev_fmt, ...)
{
	struct clk_lookup *cl;
	char dev_id[BUS_ID_SIZE];
	int con_size = 0, dev_size = 0;

	if (con_id)
		con_size = strlen(con_id) + 1;

	if (dev_fmt) {
		va_list ap;

		va_start(ap, dev_fmt);
		dev_size = vscnprintf(dev_id, sizeof(dev_id), dev_fmt, ap) + 1;
		va_end(ap);
	}

	cl = kzalloc(sizeof(*cl) + con_size + dev_size, GFP_KERNEL);
	if (!cl)
		return ERR_PTR(-ENOMEM);

	cl->clk = clk;
	if (con_size) {
		cl->con_id = cl->strings;
		strcpy(cl->strings, con_id);
	}
	if (dev_size) {
		cl->dev_id = cl->strings + con_size;
		strcpy(cl->strings + con_size, dev_id);
	}

	mutex_lock(&clocks_mutex);
	list_add(&cl->node, &clocks);
	mutex_unlock(&clocks_mutex);

	return cl;
}
EXPORT_SYMBOL(clkdev_add);

void clkdev_remove(struct clk_lookup *cl)
{
	mutex_lock(&clocks_mutex);
	list_del(&cl->node);
	mutex_unlock(&clocks_mutex);
	kfree(cl);
}
EXPORT_SYMBOL(clkdev_remove);
