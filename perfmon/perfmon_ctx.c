/*
 * perfmon_ctx.c: perfmon2 context functions
 *
 * This file implements the perfmon2 interface which
 * provides access to the hardware performance counters
 * of the host processor.
 *
 *
 * The initial version of perfmon.c was written by
 * Ganesh Venkitachalam, IBM Corp.
 *
 * Then it was modified for perfmon-1.x by Stephane Eranian and
 * David Mosberger, Hewlett Packard Co.
 *
 * Version Perfmon-2.x is a complete rewrite of perfmon-1.x
 * by Stephane Eranian, Hewlett Packard Co.
 *
 * Copyright (c) 1999-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *                David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * More information about perfmon available at:
 * 	http://www.hpl.hp.com/research/linux/perfmon
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/perfmon_kern.h>
#include "perfmon_priv.h"

/*
 * context memory pool pointer
 */
static struct kmem_cache *pfm_ctx_cachep;

/*
 * This function is called when we need to perform asynchronous
 * work on a context. This function is called ONLY when about to
 * return to user mode (very much like with signal handling).
 *
 * we come here if:
 *
 *  - we are zombie and we need to cleanup our state
 *
 * pfm_handle_work() can be called with interrupts enabled
 * (TIF_NEED_RESCHED) or disabled.
 */
void pfm_handle_work(struct pt_regs *regs)
{
	struct pfm_context *ctx;
	unsigned long flags;
	int type;

	if (!user_mode(regs))
		return;

	clear_thread_flag(TIF_PERFMON_WORK);

	ctx = current->pfm_context;
	if (ctx == NULL) {
		PFM_DBG("[%d] has no ctx", current->pid);
		return;
	}

	spin_lock_irqsave(&ctx->lock, flags);

	type = ctx->flags.work_type;
	ctx->flags.work_type = PFM_WORK_NONE;

	PFM_DBG("work_type=%d", type);

	switch (type) {
	case PFM_WORK_ZOMBIE:
		goto do_zombie;
	default:
		PFM_DBG("unkown type=%d", type);
		goto nothing_todo;
	}
nothing_todo:
	/*
	 * restore flags as they were upon entry
	 */
	spin_unlock_irqrestore(&ctx->lock, flags);
	return;

do_zombie:
	PFM_DBG("context is zombie, bailing out");

	/* always returns 0 in this case */
	 __pfm_unload_context(ctx);

	/*
	 * keep the spinlock check happy
	 */
	spin_unlock(&ctx->lock);

	/*
	 * enable interrupt for vfree()
	 */
	local_irq_enable();

	/*
	 * actual context free
	 */
	pfm_free_context(ctx);

	/*
	 * restore interrupts as they were upon entry
	 */
	local_irq_restore(flags);

	/*
	 * pfm_unload always successful, so can release
	 * session safely
	 */
	pfm_session_release();
}

/**
 * pfm_free_context - de-allocate context and associated resources
 * @ctx: context to free
 */
void pfm_free_context(struct pfm_context *ctx)
{
	pfm_arch_context_free(ctx);

	PFM_DBG("free ctx @0x%p", ctx);
	kmem_cache_free(pfm_ctx_cachep, ctx);
	/*
	 * decrease refcount on:
	 * 	- PMU description table
	 */
	pfm_pmu_release();
}

/**
 * pfm_init_ctx -- initialize context SLAB
 *
 * called from pfm_init
 */
int __init pfm_init_ctx(void)
{
	pfm_ctx_cachep = kmem_cache_create("pfm_context",
				   sizeof(struct pfm_context)+PFM_ARCH_CTX_SIZE,
				   SLAB_HWCACHE_ALIGN, 0, NULL);
	if (!pfm_ctx_cachep) {
		PFM_ERR("cannot initialize context slab");
		return -ENOMEM;
	}
	return 0;
}
