/*
 * perfmon_attach.c: perfmon2 load/unload functions
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

/**
 * __pfm_exit_thread - detach and free context on thread exit
 */
void __pfm_exit_thread(void)
{
	struct pfm_context *ctx;
	unsigned long flags;
	int free_ok = 0, ret = -1;

	ctx  = current->pfm_context;

	spin_lock_irqsave(&ctx->lock, flags);

	PFM_DBG("state=%d is_self=%d", ctx->state, ctx->flags.is_self);

	/*
	 * __pfm_unload_context() cannot fail
	 * in the context states we are interested in
	 */
	switch (ctx->state) {
	case PFM_CTX_LOADED:
		ret = __pfm_unload_context(ctx);
		break;
	case PFM_CTX_ZOMBIE:
		ret = __pfm_unload_context(ctx);
		free_ok = 1;
		break;
	default:
		BUG_ON(ctx->state != PFM_CTX_LOADED);
		break;
	}
	spin_unlock_irqrestore(&ctx->lock, flags);

	if (!ret)
		pfm_session_release();

	/*
	 * All memory free operations (especially for vmalloc'ed memory)
	 * MUST be done with interrupts ENABLED.
	 */
	if (free_ok)
		pfm_free_context(ctx);
}
