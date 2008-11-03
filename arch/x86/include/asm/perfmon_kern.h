/*
 * Copyright (c) 2005-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * Copyright (c) 2007 Advanced Micro Devices, Inc.
 * Contributed by Robert Richter <robert.richter@amd.com>
 *
 * This file contains X86 Processor Family specific definitions
 * for the perfmon interface. This covers P6, Pentium M, P4/Xeon
 * (32-bit and 64-bit, i.e., EM64T) and AMD X86-64.
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
#ifndef _ASM_X86_PERFMON_KERN_H_
#define _ASM_X86_PERFMON_KERN_H_

#ifdef CONFIG_PERFMON
#include <linux/unistd.h>
#ifdef CONFIG_4KSTACKS
#define PFM_ARCH_STK_ARG	8
#else
#define PFM_ARCH_STK_ARG	16
#endif

struct pfm_arch_pmu_info {
	u32 flags;		/* PMU feature flags */
	/*
	 * mandatory model-specific callbacks
	 */
	int  (*stop_save)(struct pfm_context *ctx, struct pfm_event_set *set);
	int  (*has_ovfls)(struct pfm_context *ctx);
	void (*quiesce)(void);

	/*
	 * optional model-specific callbacks
	 */
	void (*acquire_pmu_percpu)(void);
	void (*release_pmu_percpu)(void);
	int (*load_context)(struct pfm_context *ctx);
	void (*unload_context)(struct pfm_context *ctx);
};

/*
 * PMU feature flags
 */
#define PFM_X86_FL_NO_SHARING	0x02	/* no sharing with other subsystems */
#define PFM_X86_FL_SHARING	0x04	/* PMU is being shared */

struct pfm_x86_ctx_flags {
	unsigned int insecure:1;  /* rdpmc per-thread self-monitoring */
	unsigned int reserved:31; /* for future use */
};

struct pfm_arch_context {
	u64 saved_real_iip;		/* instr pointer of last NMI intr */
	struct pfm_x86_ctx_flags flags;	/* flags */
};

/*
 * functions implemented as inline on x86
 */

/**
 * pfm_arch_write_pmc - write a single PMC register
 * @ctx: context to work on
 * @cnum: PMC index
 * @value: PMC 64-bit value
 *
 * in certain situations, ctx may be NULL
 */
static inline void pfm_arch_write_pmc(struct pfm_context *ctx,
				      unsigned int cnum, u64 value)
{
	/*
	 * we only write to the actual register when monitoring is
	 * active (pfm_start was issued)
	 */
	if (ctx && ctx->flags.started == 0)
		return;

	PFM_DBG_ovfl("pfm_arch_write_pmc(0x%lx, 0x%Lx)",
		     pfm_pmu_conf->pmc_desc[cnum].hw_addr,
		     (unsigned long long) value);

	wrmsrl(pfm_pmu_conf->pmc_desc[cnum].hw_addr, value);
}

/**
 * pfm_arch_write_pmd - write a single PMD register
 * @ctx: context to work on
 * @cnum: PMD index
 * @value: PMD 64-bit value
 */
static inline void pfm_arch_write_pmd(struct pfm_context *ctx,
				      unsigned int cnum, u64 value)
{
	/*
	 * to make sure the counter overflows, we set the
	 * upper bits. we also clear any other unimplemented
	 * bits as this may cause crash on some processors.
	 */
	if (pfm_pmu_conf->pmd_desc[cnum].type & PFM_REG_C64)
		value = (value | ~pfm_pmu_conf->ovfl_mask)
		      & ~pfm_pmu_conf->pmd_desc[cnum].rsvd_msk;

	PFM_DBG_ovfl("pfm_arch_write_pmd(0x%lx, 0x%Lx)",
		     pfm_pmu_conf->pmd_desc[cnum].hw_addr,
		     (unsigned long long) value);

	wrmsrl(pfm_pmu_conf->pmd_desc[cnum].hw_addr, value);
}

/**
 * pfm_arch_read_pmd - read a single PMD register
 * @ctx: context to work on
 * @cnum: PMD index
 *
 * return value is register 64-bit value
 */
static inline u64 pfm_arch_read_pmd(struct pfm_context *ctx, unsigned int cnum)
{
	u64 tmp;

	rdmsrl(pfm_pmu_conf->pmd_desc[cnum].hw_addr, tmp);

	PFM_DBG_ovfl("pfm_arch_read_pmd(0x%lx) = 0x%Lx",
		     pfm_pmu_conf->pmd_desc[cnum].hw_addr,
		     (unsigned long long) tmp);
	return tmp;
}

/**
 * pfm_arch_read_pmc - read a single PMC register
 * @ctx: context to work on
 * @cnum: PMC index
 *
 * return value is register 64-bit value
 */
static inline u64 pfm_arch_read_pmc(struct pfm_context *ctx, unsigned int cnum)
{
	u64 tmp;

	rdmsrl(pfm_pmu_conf->pmc_desc[cnum].hw_addr, tmp);

	PFM_DBG_ovfl("pfm_arch_read_pmc(0x%lx) = 0x%016Lx",
		     pfm_pmu_conf->pmc_desc[cnum].hw_addr,
		     (unsigned long long) tmp);
	return tmp;
}

/**
 * pfm_arch_is_active - return non-zero is monitoring has been started
 * @ctx: context to check
 *
 * At certain points, perfmon needs to know if monitoring has been
 * explicitly started.
 *
 * On x86, there is not other way but to use pfm_start/pfm_stop
 * to activate monitoring, thus we can simply check flags.started
 */
static inline int pfm_arch_is_active(struct pfm_context *ctx)
{
	return ctx->flags.started;
}


/**
 * pfm_arch_unload_context - detach context from thread or CPU
 * @ctx: context to detach
 *
 * in system-wide ctx->task is NULL, otherwise it points to the
 * attached thread
 */
static inline void pfm_arch_unload_context(struct pfm_context *ctx)
{
	struct pfm_arch_pmu_info *pmu_info;
	struct pfm_arch_context *ctx_arch;

	ctx_arch = pfm_ctx_arch(ctx);
	pmu_info = pfm_pmu_info();

	if (ctx_arch->flags.insecure) {
		PFM_DBG("clear cr4.pce");
		clear_in_cr4(X86_CR4_PCE);
	}

	if (pmu_info->unload_context)
		pmu_info->unload_context(ctx);
}

/**
 * pfm_arch_load_context - attach context to thread or CPU
 * @ctx: context to attach
 */
static inline int pfm_arch_load_context(struct pfm_context *ctx)
{
	struct pfm_arch_pmu_info *pmu_info;
	struct pfm_arch_context *ctx_arch;
	int ret = 0;

	ctx_arch = pfm_ctx_arch(ctx);
	pmu_info = pfm_pmu_info();

	/*
	 * RDPMC authorized in system-wide and
	 * per-thread self-monitoring.
	 *
	 * RDPMC only gives access to counts.
	 *
	 * The context-switch routine code does not restore
	 * all the PMD registers (optimization), thus there
	 * is a possible leak of counts there in per-thread
	 * mode.
	 */
	if (ctx->task == current) {
		PFM_DBG("set cr4.pce");
		set_in_cr4(X86_CR4_PCE);
		ctx_arch->flags.insecure = 1;
	}

	if (pmu_info->load_context)
		ret = pmu_info->load_context(ctx);

	return ret;
}

void pfm_arch_restore_pmcs(struct pfm_context *ctx, struct pfm_event_set *set);
void pfm_arch_start(struct task_struct *task, struct pfm_context *ctx);
void pfm_arch_stop(struct task_struct *task, struct pfm_context *ctx);

/**
 * pfm_arch_intr_freeze_pmu - stop monitoring when handling PMU interrupt
 * @ctx: current context
 * @set: current event set
 *
 * called from __pfm_interrupt_handler().
 * ctx is not NULL. ctx is locked. interrupts are masked
 *
 * The following actions must take place:
 *  - stop all monitoring to ensure handler has consistent view.
 *  - collect overflowed PMDs bitmask into povfls_pmds and
 *    npend_ovfls. If no interrupt detected then npend_ovfls
 *    must be set to zero.
 */
static inline void pfm_arch_intr_freeze_pmu(struct pfm_context *ctx,
					    struct pfm_event_set *set)
{
	/*
	 * on X86, freezing is equivalent to stopping
	 */
	pfm_arch_stop(current, ctx);

	/*
	 * we mark monitoring as stopped to avoid
	 * certain side effects especially in
	 * pfm_switch_sets_from_intr() and
	 * pfm_arch_restore_pmcs()
	 */
	ctx->flags.started = 0;
}

/**
 * pfm_arch_intr_unfreeze_pmu - conditionally reactive monitoring
 * @ctx: current context
 *
 * current context may be not when dealing when spurious interrupts
 *
 * Must re-activate monitoring if context is not MASKED.
 * interrupts are masked.
 */
static inline void pfm_arch_intr_unfreeze_pmu(struct pfm_context *ctx)
{
	if (ctx == NULL)
		return;

	PFM_DBG_ovfl("state=%d", ctx->state);

	/*
	 * restore flags.started which is cleared in
	 * pfm_arch_intr_freeze_pmu()
	 */
	ctx->flags.started = 1;

	pfm_arch_restore_pmcs(ctx, ctx->active_set);
}

/**
 * pfm_arch_ovfl_reset_pmd - reset pmd on overflow
 * @ctx: current context
 * @cnum: PMD index
 *
 * On some CPUs, the upper bits of a counter must be set in order for the
 * overflow interrupt to happen. On overflow, the counter has wrapped around,
 * and the upper bits are cleared. This function may be used to set them back.
 *
 * For x86, the current version loses whatever is remaining in the counter,
 * which is usually has a small count. In order not to loose this count,
 * we do a read-modify-write to set the upper bits while preserving the
 * low-order bits. This is slow but works.
 */
static inline void pfm_arch_ovfl_reset_pmd(struct pfm_context *ctx, unsigned int cnum)
{
	u64 val;
	val = pfm_arch_read_pmd(ctx, cnum);
	pfm_arch_write_pmd(ctx, cnum, val);
}

/**
 * pfm_arch_context_create - create context
 * @ctx: newly created context
 * @flags: context flags as passed by user
 *
 * called from __pfm_create_context()
 */
static inline int pfm_arch_context_create(struct pfm_context *ctx, u32 ctx_flags)
{
	return 0;
}

/**
 * pfm_arch_context_free - free context
 * @ctx: context to free
 */
static inline void pfm_arch_context_free(struct pfm_context *ctx)
{}

/*
 * functions implemented in arch/x86/perfmon/perfmon.c
 */
int  pfm_arch_init(void);
void pfm_arch_resend_irq(struct pfm_context *ctx);

int  pfm_arch_ctxswout_thread(struct task_struct *task, struct pfm_context *ctx);
void pfm_arch_ctxswin_thread(struct task_struct *task, struct pfm_context *ctx);

void pfm_arch_restore_pmds(struct pfm_context *ctx, struct pfm_event_set *set);
int  pfm_arch_pmu_config_init(struct pfm_pmu_config *cfg);
void pfm_arch_pmu_config_remove(void);
char *pfm_arch_get_pmu_module_name(void);
int pfm_arch_pmu_acquire(u64 *unavail_pmcs, u64 *unavail_pmds);
void pfm_arch_pmu_release(void);

static inline void pfm_arch_serialize(void)
{}

static inline void pfm_arch_arm_handle_work(struct task_struct *task)
{}

static inline void pfm_arch_disarm_handle_work(struct task_struct *task)
{}

#define PFM_ARCH_CTX_SIZE	(sizeof(struct pfm_arch_context))
/*
 * x86 does not need extra alignment requirements for the sampling buffer
 */
#define PFM_ARCH_SMPL_ALIGN_SIZE	0

asmlinkage void  pmu_interrupt(void);

#endif /* CONFIG_PEFMON */

#endif /* _ASM_X86_PERFMON_KERN_H_ */
