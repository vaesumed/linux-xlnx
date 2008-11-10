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
 * __pfm_load_ctx_thread - attach context to a thread
 * @ctx: context to operate on
 * @task: thread to attach to
 *
 * The function must be called with the context locked and interrupts disabled.
 */
static int pfm_load_ctx_thread(struct pfm_context *ctx,
			       struct task_struct *task)
{
	struct pfm_event_set *set;
	struct pfm_context *old;
	int ret;
	u16 max;

	PFM_DBG("pid=%d",  task->pid);

	/*
	 * we must use cmpxchg to avoid race condition with another
	 * context trying to attach to the same task.
	 *
	 * per-thread:
	 *   - task to attach to is checked in sys_pfm_load_context() to avoid
	 *     locking issues. if found, and not self,  task refcount was
	 *     incremented.
	 */
	old = cmpxchg(&task->pfm_context, NULL, ctx);
	if (old) {
		PFM_DBG("load_pid=%d has a context "
			"old=%p new=%p cur=%p",
			task->pid,
			old,
			ctx,
			task->pfm_context);
		return -EEXIST;
	}

	/*
	 * initialize sets
	 */
	set = ctx->active_set;

	/*
	 * cleanup bitvectors
	 */
	max = ctx->regs.max_intr_pmd;
	bitmap_zero(cast_ulp(set->povfl_pmds), max);

	set->npend_ovfls = 0;

	/*
	 * we cannot just use plain clear because of arch-specific flags
	 */
	set->priv_flags &= ~PFM_SETFL_PRIV_MOD_BOTH;

	/*
 	 * link context to task
 	 */
	ctx->task = task;

	/*
	 * perform any architecture specific actions
	 */
	ret = pfm_arch_load_context(ctx);
	if (ret)
		goto error_noload;

	/*
	 * now reserve the session, before we can proceed with
	 * actually accessing the PMU hardware
	 */
	ret = pfm_session_acquire();
	if (ret)
		goto error;


	if (ctx->task != current) {

		/* not self-monitoring */
		ctx->flags.is_self = 0;

		/* force a full reload */
		ctx->last_act = PFM_INVALID_ACTIVATION;
		ctx->last_cpu = -1;
		set->priv_flags |= PFM_SETFL_PRIV_MOD_BOTH;

	} else {
		/*
 		 * on UP, we may have to push out the PMU
 		 * state of the last monitored thread
 		 */
		pfm_check_save_prev_ctx();

		ctx->last_cpu = smp_processor_id();
		__get_cpu_var(pmu_activation_number)++;
		ctx->last_act = __get_cpu_var(pmu_activation_number);

		ctx->flags.is_self = 1;

		/*
		 * load PMD from set
		 * load PMC from set
		 */
		pfm_arch_restore_pmds(ctx, set);
		pfm_arch_restore_pmcs(ctx, set);

		/*
		 * set new ownership
		 */
		pfm_set_pmu_owner(ctx->task, ctx);
	}

	/*
 	 * will cause switch_to() to invoke PMU
 	 * context switch code
 	 */
	set_tsk_thread_flag(task, TIF_PERFMON_CTXSW);

	ctx->state = PFM_CTX_LOADED;

	return 0;

error:
	pfm_arch_unload_context(ctx);
	ctx->task = NULL;
error_noload:
	/*
	 * detach context
	 */
	task->pfm_context = NULL;
	return ret;
}

/**
 * __pfm_load_context - attach context to a thread
 * @ctx: context to operate on
 * @task: thread to attach to
 */
int __pfm_load_context(struct pfm_context *ctx, struct task_struct *task)
{
	return pfm_load_ctx_thread(ctx, task);
}

/**
 * pfm_update_ovfl_pmds - account for pending ovfls on PMDs
 * @ctx: context to operate on
 *
 * This function is always called after pfm_stop has been issued
 */
static void pfm_update_ovfl_pmds(struct pfm_context *ctx)
{
	struct pfm_event_set *set;
	u64 *cnt_pmds;
	u64 ovfl_mask;
	u16 num_ovfls, i;

	ovfl_mask = pfm_pmu_conf->ovfl_mask;
	cnt_pmds = ctx->regs.cnt_pmds;
	set = ctx->active_set;

	if (!set->npend_ovfls)
		return;

	num_ovfls = set->npend_ovfls;
	PFM_DBG("novfls=%u", num_ovfls);

	for (i = 0; num_ovfls; i++) {
		if (test_bit(i, cast_ulp(set->povfl_pmds))) {
			/* only correct value for counters */
			if (test_bit(i, cast_ulp(cnt_pmds)))
				set->pmds[i] += 1 + ovfl_mask;
			num_ovfls--;
		}
		PFM_DBG("pmd%u val=0x%llx",
			i,
			(unsigned long long)set->pmds[i]);
	}
	/*
	 * we need to clear to prevent a pfm_getinfo_evtsets() from
	 * returning stale data even after the context is unloaded
	 */
	set->npend_ovfls = 0;
	bitmap_zero(cast_ulp(set->povfl_pmds),
		    ctx->regs.max_intr_pmd);
}

/**
 * __pfm_unload_context - detach context from CPU or thread
 * @ctx: context to operate on
 *
 * The function must be called with the context locked and interrupts disabled.
 */
int __pfm_unload_context(struct pfm_context *ctx)
{
	int ret;

	PFM_DBG("ctx_state=%d task [%d]",
		ctx->state,
		ctx->task ? ctx->task->pid : -1);

	/*
	 * check unload-able state
	 */
	if (ctx->state == PFM_CTX_UNLOADED)
		return -EINVAL;

	/*
	 * stop monitoring
	 */
	ret = __pfm_stop(ctx);
	if (ret)
		return ret;

	ctx->state = PFM_CTX_UNLOADED;

	/*
	 * save active set
	 * UP:
	 * 	if not current task and due to lazy, state may
	 * 	still be live
	 * for system-wide, guaranteed to run on correct CPU
	 */
	if (__get_cpu_var(pmu_ctx) == ctx) {
		/*
		 * pending overflows have been saved by pfm_stop()
		 */
		pfm_save_pmds(ctx);
		pfm_set_pmu_owner(NULL, NULL);
		PFM_DBG("released ownership");
	}

	/*
	 * account for pending overflows
	 */
	pfm_update_ovfl_pmds(ctx);

	/*
	 * arch-specific unload operations
	 */
	pfm_arch_unload_context(ctx);

	/*
	 * per-thread: disconnect from monitored task
	 */
	if (ctx->task) {
		ctx->task->pfm_context = NULL;
		clear_tsk_thread_flag(ctx->task, TIF_PERFMON_CTXSW);
		ctx->task = NULL;
	}
	return 0;
}

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
