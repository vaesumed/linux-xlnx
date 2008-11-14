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
#include <linux/fdtable.h>
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

/**
 * pfm_ctx_permissions - check authorization to create new context
 * @ctx_flags: context flags passed by user
 *
 * check for permissions to create a context.
 *
 * A sysadmin may decide to restrict creation of per-thread
 * context to a group of users using the group id via
 * /sys/kernel/perfmon/task_group
 *
 * Once we identify a user level package which can be used
 * to grant/revoke Linux capabilites at login via PAM, we will
 * be able to use capabilities. We would also need to increase
 * the size of cap_t to support more than 32 capabilities (it
 * is currently defined as u32 and 32 capabilities are alrady
 * defined).
 */
static inline int pfm_ctx_permissions(u32 ctx_flags)
{
	if (pfm_controls.task_group != PFM_GROUP_PERM_ANY
		   && !in_group_p(pfm_controls.task_group)) {
		PFM_DBG("user group not allowed to create a task context");
		return -EPERM;
	}
	return 0;
}

/**
 * pfm_create_initial_set - create initial set from __pfm_c reate_context
 * @ctx: context to atatched the set to
 */
static void pfm_create_initial_set(struct pfm_context *ctx)
{
	struct pfm_event_set *set;
	u64 *impl_pmcs;
	u16 i, max_pmc;

	set = ctx->active_set;
	max_pmc = ctx->regs.max_pmc;
	impl_pmcs =  ctx->regs.pmcs;

	/*
	 * install default values for all PMC  registers
	 */
	for (i = 0; i < max_pmc; i++) {
		if (pfm_arch_bv_test_bit(i, impl_pmcs)) {
			set->pmcs[i] = pfm_pmu_conf->pmc_desc[i].dfl_val;
			PFM_DBG("pmc%u=0x%llx",
				i,
				(unsigned long long)set->pmcs[i]);
		}
	}
	/*
	 * PMD registers are set to 0 when the event set is allocated,
	 * hence we do not need to explicitly initialize them.
	 *
	 * For virtual PMD registers (i.e., those tied to a SW resource)
	 * their value becomes meaningful once the context is attached.
	 */
}

/**
 * __pfm_create_context - allocate and initialize a perfmon context
 * @ctx_flags : user context flags
 * @sif: pointer to pfarg_sinfo to be updated
 * @new_ctx: will contain new context address on return
 *
 * function used to allocate a new context. A context is allocated along
 * with the default event set. If a sampling format is used, the buffer
 * may be allocated and initialized.
 *
 * The file descriptor identifying the context is allocated and returned
 * to caller.
 *
 * This function operates with no locks and interrupts are enabled.
 * return:
 * 	>=0: the file descriptor to identify the context
 * 	<0 : the error code
 */
int __pfm_create_context(__u32 ctx_flags,
			 struct pfarg_sinfo *sif,
			 struct pfm_context **new_ctx)
{
	struct pfm_context *ctx;
	struct file *filp = NULL;
	int fd = 0, ret = -EINVAL;

	if (!pfm_pmu_conf)
		return -ENOSYS;

	/* no context flags supported yet */
	if (ctx_flags)
		goto error_alloc;

	ret = pfm_ctx_permissions(ctx_flags);
	if (ret < 0)
		goto error_alloc;

	/*
	 * we can use GFP_KERNEL and potentially sleep because we do
	 * not hold any lock at this point.
	 */
	might_sleep();
	ret = -ENOMEM;
	ctx = kmem_cache_zalloc(pfm_ctx_cachep, GFP_KERNEL);
	if (!ctx)
		goto error_alloc;

	PFM_DBG("alloc ctx @0x%p", ctx);

	ctx->active_set = &ctx->_set0;

	spin_lock_init(&ctx->lock);

	/*
	 * context is unloaded
	 */
	ctx->state = PFM_CTX_UNLOADED;


	ret = pfm_pmu_acquire(ctx);
	if (ret)
		goto error_file;
	/*
	 * check if PMU is usable
	 */
	if (!(ctx->regs.num_pmcs && ctx->regs.num_pmcs)) {
		PFM_DBG("no usable PMU registers");
		ret = -EBUSY;
		goto error_file;
	}

	ret = -ENFILE;
	fd = pfm_alloc_fd(&filp);
	if (fd < 0)
		goto error_file;

	/*
	 * initialize arch-specific section
	 * must be done before fmt_init()
	 */
	ret = pfm_arch_context_create(ctx, ctx_flags);
	if (ret)
		goto error_set;

	ret = -ENOMEM;

	/*
	 * add initial set
	 */
	pfm_create_initial_set(ctx);

	filp->private_data = ctx;

	ctx->last_act = PFM_INVALID_ACTIVATION;
	ctx->last_cpu = -1;

	PFM_DBG("flags=0x%x fd=%d", ctx_flags, fd);

	if (new_ctx)
		*new_ctx = ctx;

	/*
	 * copy bitmask of available PMU registers
	 *
	 * must copy over the entire vector to avoid
	 * returning bogus upper bits pass by user
	 */
	pfm_arch_bv_copy(sif->sif_avail_pmcs,
			 ctx->regs.pmcs,
			 PFM_MAX_PMCS);

	pfm_arch_bv_copy(sif->sif_avail_pmds,
			 ctx->regs.pmds,
			 PFM_MAX_PMDS);

	/*
	 * we defer the fd_install until we are certain the call succeeded
	 * to ensure we do not have to undo its effect. Neither put_filp()
	 * nor put_unused_fd() undoes the effect of fd_install().
	 */
	fd_install(fd, filp);

	return fd;

error_set:
	put_filp(filp);
	put_unused_fd(fd);
error_file:
	/*
	 * calls the right *_put() functions
	 * calls pfm_release_pmu()
	 */
	pfm_free_context(ctx);
	return ret;
error_alloc:
	return ret;
}

/**
 * pfm_undo_create -- undo context creation
 * @fd: file descriptor to close
 * @ctx: newly created context
 *
 * upon return neither fd nor ctx are useable
 */
void pfm_undo_create(int fd, struct pfm_context *ctx)
{
       struct files_struct *files = current->files;
       struct file *file;
       int fput_needed;

       file = fget_light(fd, &fput_needed);
       /*
	* there is no fd_uninstall(), so we do it
	* here. put_unused_fd() does not remove the
	* effect of fd_install().
	*/

       spin_lock(&files->file_lock);
       files->fd_array[fd] = NULL;
       spin_unlock(&files->file_lock);

       fput_light(file, fput_needed);

       /*
	* decrement ref count and kill file
	*/
       put_filp(file);

       put_unused_fd(fd);

       pfm_free_context(ctx);
}
