/*
 * Copyright (c) 2001-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
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

#ifndef __PERFMON_PRIV_H__
#define __PERFMON_PRIV_H__
/*
 * This file contains all the definitions of data structures, variables, macros
 * that are to private to the generic code, i.e., not shared with any code that
 * lives under arch/ or include/asm-XX
 *
 * For shared definitions, use include/linux/perfmon_kern.h
 */

#ifdef CONFIG_PERFMON

/*
 * context lazy save/restore activation count
 */
#define PFM_INVALID_ACTIVATION	((u64)~0)

DECLARE_PER_CPU(u64, pmu_activation_number);

static inline void pfm_set_pmu_owner(struct task_struct *task,
				     struct pfm_context *ctx)
{
	__get_cpu_var(pmu_owner) = task;
	__get_cpu_var(pmu_ctx) = ctx;
}

int pfm_init_ctx(void);
int __pfm_write_pmcs(struct pfm_context *ctx, struct pfarg_pmr *req,
		     int count);
int __pfm_write_pmds(struct pfm_context *ctx, struct pfarg_pmr *req,
		     int count);
int __pfm_read_pmds(struct pfm_context *ctx, struct pfarg_pmr *req, int count);

int pfm_session_acquire(void);
void pfm_session_release(void);

int  pfm_init_sysfs(void);

void pfm_free_context(struct pfm_context *ctx);

int __pfm_stop(struct pfm_context *ctx);
int __pfm_start(struct pfm_context *ctx);

int __pfm_load_context(struct pfm_context *ctx, struct task_struct *task);
int __pfm_unload_context(struct pfm_context *ctx);

ssize_t pfm_sysfs_res_show(char *buf, size_t sz, int what);

int pfm_pmu_acquire(struct pfm_context *ctx);
void pfm_pmu_release(void);

void pfm_save_pmds(struct pfm_context *ctx);

/*
 * check_mask bitmask values for pfm_check_task_state()
 */
#define PFM_CMD_STOPPED		0x01	/* command needs thread stopped */
#define PFM_CMD_UNLOADED	0x02	/* command needs ctx unloaded */
#define PFM_CMD_UNLOAD		0x04	/* command is unload */

/**
 * pfm_save_prev_ctx - check if previous context exists and save state
 *
 * called from pfm_load_ctx_thread() and __pfm_ctxsin_thread() to
 * check if previous context exists. If so saved its PMU state. This is used
 * only for UP kernels.
 *
 * PMU ownership is not cleared because the function is always called while
 * trying to install a new owner.
 */
static inline void pfm_check_save_prev_ctx(void)
{
#ifdef CONFIG_SMP
	struct pfm_context *ctxp;

	ctxp = __get_cpu_var(pmu_ctx);
	if (!ctxp)
		return;
	/*
	 * in UP per-thread, due to lazy save
	 * there could be a context from another
	 * task. We need to push it first before
	 * installing our new state
	 */
	pfm_save_pmds(ctxp);
	/*
	 * do not clear ownership because we rewrite
	 * right away
	 */
#endif
}

int pfm_init_fs(void);

static inline void pfm_post_work(struct task_struct *task,
				 struct pfm_context *ctx, int type)
{
	ctx->flags.work_type = type;
	set_tsk_thread_flag(task, TIF_PERFMON_WORK);
}

#define PFM_PMC_STK_ARG	PFM_ARCH_PMC_STK_ARG
#define PFM_PMD_STK_ARG	PFM_ARCH_PMD_STK_ARG

#endif /* CONFIG_PERFMON */

#endif /* __PERFMON_PRIV_H__ */
