/*
 * Copyright (c) 2001-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@gmail.com>
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

#ifndef __LINUX_PERFMON_KERN_H__
#define __LINUX_PERFMON_KERN_H__
/*
 * This file contains all the definitions of data structures, variables, macros
 * that are to be shared between generic code and arch-specific code
 *
 * For generic only definitions, use perfmon/perfmon_priv.h
 */
#include <linux/file.h>
#include <linux/sched.h>
#include <linux/perfmon.h>

#ifdef CONFIG_PERFMON

/*
 * system adminstrator configuration controls available via
 * the /sys/kerne/perfmon interface
 */
struct pfm_controls {
	u32	debug;		/* debugging control bitmask */
	gid_t	task_group;	/* gid to create a per-task context */
	size_t	arg_mem_max;	/* maximum vector argument size */
};
extern struct pfm_controls pfm_controls;

/*
 * event_set: encapsulates the full PMU state
 */
struct pfm_event_set {
	u16 nused_pmds;			/* max number of used PMDs */
	u16 nused_pmcs;			/* max number of used PMCs */
	u32 priv_flags;			/* private flags (see below) */
	u32 npend_ovfls;		/* number of pending PMD overflow */
	u32 pad1;			/* padding */
	u64 used_pmds[PFM_PMD_BV];	/* used PMDs */
	u64 povfl_pmds[PFM_PMD_BV];	/* pending overflowed PMDs */
	u64 used_pmcs[PFM_PMC_BV];	/* used PMCs */
	u64 pmcs[PFM_MAX_PMCS];		/* PMC values */
	u64 pmds[PFM_MAX_PMDS];		/* PMD values */
};

/*
 * common private event set flags (priv_flags)
 *
 * upper 16 bits: for arch-specific use
 * lower 16 bits: for common use
 */
#define PFM_SETFL_PRIV_MOD_PMDS 0x1 /* PMD register(s) modified */
#define PFM_SETFL_PRIV_MOD_PMCS 0x2 /* PMC register(s) modified */
#define PFM_SETFL_PRIV_MOD_BOTH	(PFM_SETFL_PRIV_MOD_PMDS \
				| PFM_SETFL_PRIV_MOD_PMCS)


/*
 * context flags
 */
struct pfm_context_flags {
	unsigned int started:1;		/* pfm_start() issued */
	unsigned int is_self:1;		/* per-thread and self-montoring */
	unsigned int work_type:2;	/* type of work for pfm_handle_work */
	unsigned int reserved:28;	/* for future use */
};
/*
 * values for work_type (TIF_PERFMON_WORK must be set)
 */
#define PFM_WORK_NONE	0	/* nothing to do */
#define PFM_WORK_ZOMBIE	1	/* cleanup zombie context */


/*
 * perfmon context state
 */
#define PFM_CTX_UNLOADED	1 /* context is detached */
#define PFM_CTX_LOADED		2 /* context is attached */
#define PFM_CTX_ZOMBIE		3 /* context lost owner but still attached */

/*
 * registers description
 */
struct pfm_regdesc {
	u64 pmcs[PFM_PMC_BV];		/* available PMC */
	u64 pmds[PFM_PMD_BV];		/* available PMD */
	u64 rw_pmds[PFM_PMD_BV];	/* available RW PMD */
	u64 intr_pmds[PFM_PMD_BV];	/* PMD generating intr */
	u64 cnt_pmds[PFM_PMD_BV];	/* PMD counters */
	u16 max_pmc;			/* highest+1 avail PMC */
	u16 max_pmd;			/* highest+1 avail PMD */
	u16 max_rw_pmd;			/* highest+1 avail RW PMD */
	u16 first_intr_pmd;		/* first intr PMD */
	u16 max_intr_pmd;		/* highest+1 intr PMD */
	u16 num_rw_pmd;			/* number of avail RW PMD */
	u16 num_pmcs;			/* number of logical PMCS */
	u16 num_pmds;			/* number of logical PMDS */
	u16 num_counters;		/* number of counting PMD */
};


/*
 * context: contains all the state of a session
 */
struct pfm_context {
	spinlock_t		lock;		/* context protection */

	struct pfm_context_flags flags;
	u32			state;		/* current state */
	struct task_struct 	*task;		/* attached task */

	u64 			last_act;	/* last activation */
	u32			last_cpu;   	/* last CPU used (SMP only) */

	struct pfm_event_set	*active_set;	/* active set */
	struct pfm_event_set	_set0;		/* event set 0 */

	struct pfm_regdesc	regs;		/* registers available to context */
};

/*
 * logging
 */
#define PFM_ERR(f, x...)  printk(KERN_ERR     "perfmon: " f "\n", ## x)
#define PFM_WARN(f, x...) printk(KERN_WARNING "perfmon: " f "\n", ## x)
#define PFM_LOG(f, x...)  printk(KERN_NOTICE  "perfmon: " f "\n", ## x)
#define PFM_INFO(f, x...) printk(KERN_INFO    "perfmon: " f "\n", ## x)

/*
 * debugging
 *
 * Printk rate limiting is enforced to avoid getting flooded with too many
 * error messages on the console (which could render the machine unresponsive).
 * To get full debug output (turn off ratelimit):
 * 	$ echo 0 >/proc/sys/kernel/printk_ratelimit
 *
 * debug is a bitmask where bits are defined as follows:
 * bit  0: enable non-interrupt code degbug messages
 * bit  1: enable interrupt code debug messages
 */
#ifdef CONFIG_PERFMON_DEBUG
#define _PFM_DBG(lm, f, x...) \
	do { \
		if (unlikely((pfm_controls.debug & lm) && printk_ratelimit())) { \
			printk("perfmon: %s.%d: CPU%d [%d]: " f "\n", \
			       __func__, __LINE__, \
			       smp_processor_id(), current->pid , ## x); \
		} \
	} while (0)

#define PFM_DBG(f, x...) _PFM_DBG(0x1, f, ##x)
#define PFM_DBG_ovfl(f, x...) _PFM_DBG(0x2, f, ##x)
#else
#define PFM_DBG(f, x...)	do {} while (0)
#define PFM_DBG_ovfl(f, x...)	do {} while (0)
#endif

extern struct pfm_pmu_config  *pfm_pmu_conf;
extern int perfmon_disabled;

static inline struct pfm_arch_context *pfm_ctx_arch(struct pfm_context *c)
{
	return (struct pfm_arch_context *)(c+1);
}

#include <linux/perfmon_pmu.h>

extern const struct file_operations pfm_file_ops;

#define cast_ulp(_x) ((unsigned long *)_x)

void pfm_handle_work(struct pt_regs *regs);
void __pfm_exit_thread(void);
void pfm_ctxsw_in(struct task_struct *prev, struct task_struct *next);
void pfm_ctxsw_out(struct task_struct *prev, struct task_struct *next);
void __pfm_init_percpu(void *dummy);

static inline void pfm_exit_thread(void)
{
	if (current->pfm_context)
		__pfm_exit_thread();
}

/*
 * include arch-specific kernel level definitions
 */
#include <asm/perfmon_kern.h>

static inline void pfm_copy_thread(struct task_struct *task)
{
	/*
	 * context or perfmon TIF state  is NEVER inherited
	 * in child task. Holds for per-thread and system-wide
	 */
	task->pfm_context = NULL;
	clear_tsk_thread_flag(task, TIF_PERFMON_CTXSW);
}

/*
 * read a single PMD register.
 */
static inline u64 pfm_read_pmd(struct pfm_context *ctx, unsigned int cnum)
{
	return pfm_arch_read_pmd(ctx, cnum);
}
/*
 * write a single PMD register.
 */
static inline void pfm_write_pmd(struct pfm_context *ctx, unsigned int cnum,
				 u64 value)
{
	/*
	 * PMD writes are ignored for read-only registers
	 */
	if (pfm_pmu_conf->pmd_desc[cnum].type & PFM_REG_RO)
		return;

	/*
	 * clear unimplemented bits
	 */
	value &= ~pfm_pmu_conf->pmd_desc[cnum].rsvd_msk;

	pfm_arch_write_pmd(ctx, cnum, value);
}

DECLARE_PER_CPU(struct pfm_context *, pmu_ctx);
DECLARE_PER_CPU(struct task_struct *, pmu_owner);

/*
 * number of u64 to use for stack buffer in
 * syscalls which take vector argument
 */
#ifndef PFM_ARCH_STK_ARG
#define PFM_ARCH_STK_ARG	2
#endif

#define PFM_STK_ARG	PFM_ARCH_STK_ARG

#else /* !CONFIG_PERFMON */
/*
 * perfmon hooks are nops when CONFIG_PERFMON is undefined
 */

static inline void pfm_exit_thread(void)
{}

static inline void pfm_handle_work(struct pt_regs *regs)
{}

static inline void pfm_copy_thread(struct task_struct *t)
{}

static inline void pfm_ctxsw_in(struct task_struct *p, struct task_struct *n)
{}

static inline void pfm_ctxsw_out(struct task_struct *p, struct task_struct *n)
{}
#endif /* CONFIG_PERFMON */
#endif /* __LINUX_PERFMON_KERN_H__ */
