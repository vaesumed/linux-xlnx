/*
 * perfmon_syscalls.c: perfmon2 system call interface
 *
 * This file implements the perfmon2 interface which
 * provides access to the hardware performance counters
 * of the host processor.
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
 * 	http://perfmon2.sf.net
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
#include <linux/ptrace.h>
#include <linux/perfmon_kern.h>
#include <linux/uaccess.h>
#include "perfmon_priv.h"

/*
 * Context locking rules:
 * ---------------------
 * 	- any thread with access to the file descriptor of a context can
 * 	  potentially issue perfmon calls
 *
 * 	- calls must be serialized to guarantee correctness
 *
 * 	- as soon as a context is attached to a thread or CPU, it may be
 * 	  actively monitoring. On some architectures, such as IA-64, this
 * 	  is true even though the pfm_start() call has not been made. This
 * 	  comes from the fact that on some architectures, it is possible to
 * 	  start/stop monitoring from userland.
 *
 *	- If monitoring is active, then there can PMU interrupts. Because
 *	  context accesses must be serialized, the perfmon system calls
 *	  must mask interrupts as soon as the context is attached.
 *
 *	- perfmon system calls that operate with the context unloaded cannot
 *	  assume it is actually unloaded when they are called. They first need
 *	  to check and for that they need interrupts masked. Then, if the
 *	  context is actually unloaded, they can unmask interrupts.
 *
 *	- interrupt masking holds true for other internal perfmon functions as
 *	  well. Except for PMU interrupt handler because those interrupts
 *	  cannot be nested.
 *
 * 	- we mask ALL interrupts instead of just the PMU interrupt because we
 * 	  also need to protect against timer interrupts which could trigger
 * 	  a set switch.
 */

struct pfm_syscall_cookie {
	struct file *filp;
	int fput_needed;
};

/*
 * cannot attach if :
 * 	- kernel task
 * 	- task not owned by caller (checked by ptrace_may_attach())
 * 	- task is dead or zombie
 * 	- cannot use blocking notification when self-monitoring
 */
static int pfm_task_incompatible(struct pfm_context *ctx,
				 struct task_struct *task)
{
	/*
	 * cannot attach to a kernel thread
	 */
	if (!task->mm) {
		PFM_DBG("cannot attach to kernel thread [%d]", task->pid);
		return -EPERM;
	}

	/*
	 * cannot attach to a zombie task
	 */
	if (task->exit_state == EXIT_ZOMBIE || task->exit_state == EXIT_DEAD) {
		PFM_DBG("cannot attach to zombie/dead task [%d]", task->pid);
		return -EBUSY;
	}
	return 0;
}

/**
 * pfm_get_task -- check permission and acquire task to monitor
 * @ctx: perfmon context
 * @pid: identification of the task to check
 * @task: upon return, a pointer to the task to monitor
 *
 * This function  is used in per-thread mode only AND when not
 * self-monitoring. It finds the task to monitor and checks
 * that the caller has permissions to attach. It also checks
 * that the task is stopped via ptrace so that we can safely
 * modify its state.
 *
 * task refcount is incremented when succesful.
 */
static int pfm_get_task(struct pfm_context *ctx, pid_t pid,
			struct task_struct **task)
{
	struct task_struct *p;
	int ret = 0, ret1 = 0;

	/*
	 * When attaching to another thread we must ensure
	 * that the thread is actually stopped. Just like with
	 * perfmon system calls, we enforce that the thread
	 * be ptraced and STOPPED by using ptrace_check_attach().
	 *
	 * As a consequence, only the ptracing parent can actually
	 * attach a context to a thread. Obviously, this constraint
	 * does not exist for self-monitoring threads.
	 *
	 * We use ptrace_may_access() to check for permission.
	 */
	read_lock(&tasklist_lock);

	p = find_task_by_vpid(pid);
	if (p)
		get_task_struct(p);

	read_unlock(&tasklist_lock);

	if (!p) {
		PFM_DBG("task not found %d", pid);
		return -ESRCH;
	}

	ret = -EPERM;

	/*
	 * returns 0 if cannot attach
	 */
	ret1 = ptrace_may_access(p, PTRACE_MODE_ATTACH);
	if (ret1)
		ret = ptrace_check_attach(p, 0);

	PFM_DBG("may_attach=%d check_attach=%d", ret1, ret);

	if (ret || !ret1)
		goto error;

	ret = pfm_task_incompatible(ctx, p);
	if (ret)
		goto error;

	*task = p;

	return 0;
error:
	if (!(ret1 || ret))
		ret = -EPERM;

	put_task_struct(p);

	return ret;
}

/*
 * context must be locked when calling this function
 */
int __pfm_check_task_state(struct pfm_context *ctx, int check_mask,
			 unsigned long *flags)
{
	struct task_struct *task;
	unsigned long local_flags, new_flags;
	int state, ret;

recheck:
	/*
	 * task is NULL for system-wide context
	 */
	task = ctx->task;
	state = ctx->state;
	local_flags = *flags;

	PFM_DBG("state=%d check_mask=0x%x task=[%d]",
		state, check_mask, task ? task->pid:-1);
	/*
	 * if the context is detached, then we do not touch
	 * hardware, therefore there is not restriction on when we can
	 * access it.
	 */
	if (state == PFM_CTX_UNLOADED)
		return 0;
	/*
	 * no command can operate on a zombie context.
	 * A context becomes zombie when the file that identifies
	 * it is closed while the context is still attached to the
	 * thread it monitors.
	 */
	if (state == PFM_CTX_ZOMBIE)
		return -EINVAL;

	/*
	 * at this point, state is PFM_CTX_LOADED
	 */

	/*
	 * some commands require the context to be unloaded to operate
	 */
	if (check_mask & PFM_CMD_UNLOADED)  {
		PFM_DBG("state=%d, cmd needs context unloaded", state);
		return -EBUSY;
	}

	/*
	 * self-monitoring always ok.
	 */
	if (task == current)
		return 0;

	/*
	 * at this point, monitoring another thread
	 */

	/*
	 * When we operate on another thread, we must wait for it to be
	 * stopped and completely off any CPU as we need to access the
	 * PMU state (or machine state).
	 *
	 * A thread can be put in the STOPPED state in various ways
	 * including PTRACE_ATTACH, or when it receives a SIGSTOP signal.
	 * We enforce that the thread must be ptraced, so it is stopped
	 * AND it CANNOT wake up while we operate on it because this
	 * would require an action from the ptracing parent which is the
	 * thread that is calling this function.
	 *
	 * The dependency on ptrace, imposes that only the ptracing
	 * parent can issue command on a thread. This is unfortunate
	 * but we do not know of a better way of doing this.
	 */
	if (check_mask & PFM_CMD_STOPPED) {

		spin_unlock_irqrestore(&ctx->lock, local_flags);

		/*
		 * check that the thread is ptraced AND STOPPED
		 */
		ret = ptrace_check_attach(task, 0);

		spin_lock_irqsave(&ctx->lock, new_flags);

		/*
		 * flags may be different than when we released the lock
		 */
		*flags = new_flags;

		if (ret)
			return ret;
		/*
		 * we must recheck to verify if state has changed
		 */
		if (unlikely(ctx->state != state)) {
			PFM_DBG("old_state=%d new_state=%d",
				state,
				ctx->state);
			goto recheck;
		}
	}
	return 0;
}

int pfm_check_task_state(struct pfm_context *ctx, int check_mask,
			 unsigned long *flags)
{
	int ret;
	ret  = __pfm_check_task_state(ctx, check_mask, flags);
	PFM_DBG("ret=%d",ret);
	return ret;
}

/**
 * pfm_get_args - Function used to copy the syscall argument into kernel memory
 * @ureq: user argument
 * @sz: user argument size
 * @lsz: size of stack buffer
 * @laddr: stack buffer address
 * @req: point to start of kernel copy of the argument
 * @ptr_free: address of kernel copy to free
 *
 * There are two options:
 * 	- use a stack buffer described by laddr (addresses) and lsz (size)
 * 	- allocate memory
 *
 * return:
 * 	< 0 : in case of error (ptr_free may not be updated)
 * 	  0 : success
 *      - req: points to base of kernel copy of arguments
 *	- ptr_free: address of buffer to free by caller on exit.
 *		    NULL if using the stack buffer
 *
 * when ptr_free is not NULL upon return, the caller must kfree()
 */
int pfm_get_args(void __user *ureq, size_t sz, size_t lsz, void *laddr,
		 void **req, void **ptr_free)
{
	void *addr;

	/*
	 * check syadmin argument limit
	 */
	if (unlikely(sz > pfm_controls.arg_mem_max)) {
		PFM_DBG("argument too big %zu max=%zu",
			sz,
			pfm_controls.arg_mem_max);
		return -E2BIG;
	}

	/*
	 * check if vector fits on stack buffer
	 */
	if (sz > lsz) {
		addr = kmalloc(sz, GFP_KERNEL);
		if (unlikely(addr == NULL))
			return -ENOMEM;
		*ptr_free = addr;
	} else {
		addr = laddr;
		*req = laddr;
		*ptr_free = NULL;
	}

	/*
	 * bring the data in
	 */
	if (unlikely(copy_from_user(addr, ureq, sz))) {
		if (addr != laddr)
			kfree(addr);
		return -EFAULT;
	}

	/*
	 * base address of kernel buffer
	 */
	*req = addr;

	return 0;
}

/**
 * pfm_acquire_ctx_from_fd -- get ctx from file descriptor
 * @fd: file descriptor
 * @ctx: pointer to pointer of context updated on return
 * @cookie: opaque structure to use for release
 *
 * This helper function extracts the ctx from the file descriptor.
 * It also increments the refcount of the file structure. Thus
 * it updates the cookie so the refcount can be decreased when
 * leaving the perfmon syscall via pfm_release_ctx_from_fd
 */
static int pfm_acquire_ctx_from_fd(int fd, struct pfm_context **ctx,
				   struct pfm_syscall_cookie *cookie)
{
	struct file *filp;
	int fput_needed;

	filp = fget_light(fd, &fput_needed);
	if (unlikely(filp == NULL)) {
		PFM_DBG("invalid fd %d", fd);
		return -EBADF;
	}

	*ctx = filp->private_data;

	if (unlikely(!*ctx || filp->f_op != &pfm_file_ops)) {
		PFM_DBG("fd %d not related to perfmon", fd);
		return -EBADF;
	}
	cookie->filp = filp;
	cookie->fput_needed = fput_needed;

	return 0;
}

/**
 * pfm_release_ctx_from_fd -- decrease refcount of file associated with context
 * @cookie: the cookie structure initialized by pfm_acquire_ctx_from_fd
 */
static inline void pfm_release_ctx_from_fd(struct pfm_syscall_cookie *cookie)
{
	fput_light(cookie->filp, cookie->fput_needed);
}

/**
 * pfm_validate_type_sz -- validate sz based on type
 * @type : PFM_RW_XX type passed to pfm_write or pfm_read
 * @sz   : vector size in bytes
 *
 * return:
 *    the number of elements in the vector, 0 if error
 */
static size_t pfm_validate_type_sz(int type, size_t sz)
{
	size_t count, sz_type;

	switch(type) {
	case PFM_RW_PMD:
	case PFM_RW_PMC:
		sz_type = sizeof(struct pfarg_pmr);
		break;
	default:
		PFM_DBG("invalid type=%d", type);
		return 0;
	}

	count = sz / sz_type;

	if ((count * sz_type) != sz) {
		PFM_DBG("invalid size=%zu for type=%d", sz, type);
		return 0;
	}

	PFM_DBG("sz=%zu sz_type=%zu count=%zu",
		sz,
		sz_type,
		count);

	return count;
}

/*
 * unlike the other perfmon system calls, this one returns a file descriptor
 * or a value < 0 in case of error, very much like open() or socket()
 */
asmlinkage long sys_pfm_create(int flags, struct pfarg_sinfo __user *ureq)
{
	struct pfm_context *new_ctx;
	struct pfarg_sinfo sif;
	int ret;

	PFM_DBG("flags=0x%x sif=%p", flags, ureq);

	if (perfmon_disabled)
		return -ENOSYS;

	if (flags) {
		PFM_DBG("no flags accepted yet");
		return -EINVAL;
	}
	ret = __pfm_create_context(flags, &sif, &new_ctx);

	/*
	 * copy sif to user level argument, if requested
	 */
	if (ureq && copy_to_user(ureq, &sif, sizeof(sif))) {
		pfm_undo_create(ret, new_ctx);
		ret  = -EFAULT;
	}
	return ret;
}

asmlinkage long sys_pfm_write(int fd, int uflags,
			      int type,
			      void __user *ureq,
			      size_t sz)
{
	u64 buf[PFM_STK_ARG];
	struct pfm_context *ctx;
	struct pfm_syscall_cookie cookie;
	void *req, *fptr;
	unsigned long flags;
	size_t count;
	int ret;

	PFM_DBG("fd=%d flags=0x%x type=%d req=%p sz=%zu",
		fd, uflags, type, ureq, sz);

	if (uflags) {
		PFM_DBG("no flags defined");
		return -EINVAL;
	}

	count = pfm_validate_type_sz(type, sz);
	if (!count)
		return -EINVAL;

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	ret = pfm_get_args(ureq, sz, sizeof(buf), buf, (void **)&req, &fptr);
	if (ret)
		goto error;

	spin_lock_irqsave(&ctx->lock, flags);

	ret = pfm_check_task_state(ctx, PFM_CMD_STOPPED, &flags);
	if (ret)
		goto skip;
	switch(type) {
	case PFM_RW_PMC:
		ret = __pfm_write_pmcs(ctx, req, count);
		break;
	case PFM_RW_PMD:
		ret = __pfm_write_pmds(ctx, req, count);
		break;
	default:
		PFM_DBG("invalid type=%d", type);
		ret = -EINVAL;
	}
skip:
	spin_unlock_irqrestore(&ctx->lock, flags);

	/*
	 * This function may be on the critical path.
	 * We want to avoid the branch if unecessary.
	 */
	if (fptr)
		kfree(fptr);
error:
	pfm_release_ctx_from_fd(&cookie);
	return ret;
}

asmlinkage long sys_pfm_read(int fd, int uflags,
			     int type,
			     void __user *ureq,
			     size_t sz)
{
	u64 buf[PFM_STK_ARG];
	struct pfm_context *ctx;
	struct pfm_syscall_cookie cookie;
	void *req, *fptr;
	unsigned long flags;
	size_t count;
	int ret;

	PFM_DBG("fd=%d flags=0x%x type=%d req=%p sz=%zu",
		fd, uflags, type, ureq, sz);

	if (uflags) {
		PFM_DBG("no flags defined");
		return -EINVAL;
	}

	count = pfm_validate_type_sz(type, sz);
	if (!count)
		return -EINVAL;

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	ret = pfm_get_args(ureq, sz, sizeof(buf), buf, (void **)&req, &fptr);
	if (ret)
		goto error;

	spin_lock_irqsave(&ctx->lock, flags);

	ret = pfm_check_task_state(ctx, PFM_CMD_STOPPED, &flags);
	if (ret)
		goto skip;

	switch(type) {
	case PFM_RW_PMD:
		ret = __pfm_read_pmds(ctx, req, count);
		break;
	default:
		PFM_DBG("invalid type=%d", type);
		ret = -EINVAL;
	}
skip:
	spin_unlock_irqrestore(&ctx->lock, flags);

	if (copy_to_user(ureq, req, sz))
		ret = -EFAULT;

	if (fptr)
		kfree(fptr);
error:
	pfm_release_ctx_from_fd(&cookie);
	return ret;
}

asmlinkage long sys_pfm_set_state(int fd, int uflags, int state)
{
	struct pfm_context *ctx;
	struct pfm_syscall_cookie cookie;
	unsigned long flags;
	int ret;

	PFM_DBG("fd=%d uflags=0x%x state=0x%x", fd, uflags, state);

	if (uflags) {
		PFM_DBG("no flags defined");
		return -EINVAL;
	}

	switch(state) {
	case PFM_ST_START:
	case PFM_ST_STOP:
		break;
	default:
		PFM_DBG("invalid state=0x%x", state);
		return -EINVAL;
	}

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	spin_lock_irqsave(&ctx->lock, flags);

	ret = pfm_check_task_state(ctx, PFM_CMD_STOPPED, &flags);
	if (!ret) {
		if (state == PFM_ST_STOP)
			ret = __pfm_stop(ctx);
		else
			ret = __pfm_start(ctx);
	}

	spin_unlock_irqrestore(&ctx->lock, flags);

	pfm_release_ctx_from_fd(&cookie);

	return ret;
}

static long pfm_detach(int fd, int uflags)
{
	struct pfm_context *ctx;
	struct pfm_syscall_cookie cookie;
	unsigned long flags;
	int ret;

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	spin_lock_irqsave(&ctx->lock, flags);

	ret = pfm_check_task_state(ctx, PFM_CMD_STOPPED|PFM_CMD_UNLOAD, &flags);
	if (!ret)
		ret = __pfm_unload_context(ctx);

	spin_unlock_irqrestore(&ctx->lock, flags);

	/*
	 * if unload was successful, then release the session
	 * must be called with interrupts enabled, thus we need
	 * to defer until are out of __pfm_unload_context()
	 */
	if (!ret)
		pfm_session_release();

	pfm_release_ctx_from_fd(&cookie);

	return ret;
}

asmlinkage long sys_pfm_attach(int fd, int uflags, int target)
{
	struct pfm_context *ctx;
	struct task_struct *task;
	struct pfm_syscall_cookie cookie;
	unsigned long flags;
	int ret;

	PFM_DBG("fd=%d uflags=0x%x target=%d", fd, uflags, target);

	if (uflags) {
		PFM_DBG("invalid flags");
		return -EINVAL;
	}

	/*
 	 * handle detach in a separate function
 	 */
	if (target == PFM_NO_TARGET)
		return pfm_detach(fd, uflags);

	ret = pfm_acquire_ctx_from_fd(fd, &ctx, &cookie);
	if (ret)
		return ret;

	task = current;

	/*
	 * in per-thread mode (not self-monitoring), get a reference
	 * on task to monitor. This must be done with interrupts enabled
	 * Upon succesful return, refcount on task has increased.
	 *
	 * fget_light() is protecting the context.
	 */
   	if (target != current->pid) {
		ret = pfm_get_task(ctx, target, &task);
		if (ret)
			goto error;
	}

	/*
	 * irqsave is required to avoid race in case context is already
	 * loaded or with switch timeout in the case of self-monitoring
	 */
	spin_lock_irqsave(&ctx->lock, flags);

	ret = pfm_check_task_state(ctx, PFM_CMD_UNLOADED, &flags);
	if (!ret)
		ret = __pfm_load_context(ctx, task);

	spin_unlock_irqrestore(&ctx->lock, flags);

	/*
	 * in per-thread mode (not self-monitoring), we need
	 * to decrease refcount on task to monitor:
	 *   - attach successful: we have a reference in ctx->task
	 *   - attach failed    : undo the effect of pfm_get_task()
	 */
	if (task != current)
		put_task_struct(task);
error:
	pfm_release_ctx_from_fd(&cookie);
	return ret;
}
