/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, Red Hat, Inc.
 * Copyright 2004, 2005, Soeren Sandmann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/profile.h>
#include <linux/debugfs.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>

/*
 * This is the user-space visible ABI.
 */
#define SYSPROF_MAX_ADDRESSES 512

struct sysprof_stacktrace {
	int pid;		/* -1 if in kernel */
	int truncated;
	/*
	 * Note: n_addresses can be 1 if the process was compiled with
	 * -fomit-frame-pointer or is otherwise weird.
	 */
	int n_addresses;
	unsigned long addresses[SYSPROF_MAX_ADDRESSES];
};

#define SAMPLES_PER_SECOND (200)
#define INTERVAL ((HZ <= SAMPLES_PER_SECOND)? 1 : (HZ / SAMPLES_PER_SECOND))
#define N_TRACES 256

static struct sysprof_stacktrace stack_traces[N_TRACES];
static struct sysprof_stacktrace *head = &stack_traces[0];
static struct sysprof_stacktrace *tail = &stack_traces[0];
static DECLARE_WAIT_QUEUE_HEAD(wait_for_trace);

struct stack_frame {
	const void __user	*next_fp;
	unsigned long		return_address;
};

static int copy_stack_frame(const void __user *fp, struct stack_frame *frame)
{
	if (!access_ok(VERIFY_READ, fp, sizeof(*frame)))
		return 0;

	if (__copy_from_user_inatomic(frame, frame_pointer, sizeof(*frame)))
		return 0;

	return 1;
}

static DEFINE_PER_CPU(int, n_samples);

static int timer_notify(struct pt_regs *regs)
{
	struct sysprof_stacktrace *trace = head;
	int i;
	int is_user;
	static atomic_t in_timer_notify = ATOMIC_INIT(1);
	int n;

	n = ++get_cpu_var(n_samples);
	put_cpu_var(n_samples);

	if (n % INTERVAL != 0)
		return 0;

	/* 0: locked, 1: unlocked */

	if (!atomic_dec_and_test(&in_timer_notify))
		goto out;

	is_user = user_mode(regs);

	if (!current || current->pid == 0)
		goto out;

	if (is_user && current->state != TASK_RUNNING)
		goto out;

	if (!is_user) {
		/* kernel */
		trace->pid = current->pid;
		trace->truncated = 0;
		trace->n_addresses = 1;

		/* 0x1 is taken by sysprof to mean "in kernel" */
		trace->addresses[0] = 0x1;
	} else {
		const void __user *frame_pointer;
		struct stack_frame frame;
		memset(trace, 0, sizeof(struct sysprof_stacktrace));

		trace->pid = current->pid;
		trace->truncated = 0;

		i = 0;

		trace->addresses[i++] = regs->ip;

		frame_pointer = (void __user *)regs->bp;

		while (copy_stack_frame(frame_pointer, &frame) &&
		       i < SYSPROF_MAX_ADDRESSES &&
		       (unsigned long)frame_pointer >= regs->sp) {
			trace->addresses[i++] = frame.return_address;
			frame_pointer = frame.next_fp;
		}

		trace->n_addresses = i;

		if (i == SYSPROF_MAX_ADDRESSES)
			trace->truncated = 1;
		else
			trace->truncated = 0;
	}

	if (head++ == &stack_traces[N_TRACES - 1])
		head = &stack_traces[0];

	wake_up(&wait_for_trace);

out:
	atomic_inc(&in_timer_notify);
	return 0;
}

static ssize_t sysprof_file_read(struct file *filp, char __user *buffer,
			size_t count, loff_t *ppos)
{
	ssize_t bcount;
	if (head == tail)
		return -EWOULDBLOCK;

	BUG_ON(tail->pid == 0);
	*ppos = 0;
	bcount = simple_read_from_buffer(buffer, count, ppos,
			tail, sizeof(struct sysprof_stacktrace));

	if (tail++ == &stack_traces[N_TRACES - 1])
		tail = &stack_traces[0];

	return bcount;
}

static unsigned int sysprof_file_poll(struct file *filp, poll_table * poll_table)
{
	if (head != tail)
		return POLLIN | POLLRDNORM;

	poll_wait(filp, &wait_for_trace, poll_table);

	if (head != tail)
		return POLLIN | POLLRDNORM;

	return 0;
}

static const struct file_operations sysprof_fops = {
	.owner = THIS_MODULE,
	.read = sysprof_file_read,
	.poll = sysprof_file_poll,
};

static struct dentry *sysprof_trace_dentry;

static int sysprof_init(void)
{
	int err;

	sysprof_trace_dentry = debugfs_create_file("sysprof-trace", 0600,
						   NULL, NULL, &sysprof_fops);
	if (!sysprof_trace_dentry)
		return -ENOMEM;

	err = register_timer_hook(timer_notify);
	if (err)
		debugfs_remove(sysprof_trace_dentry);

	return err;
}

static void sysprof_exit(void)
{
	unregister_timer_hook(timer_notify);
	debugfs_remove(sysprof_trace_dentry);
}

module_init(sysprof_init);
module_exit(sysprof_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Soeren Sandmann (sandmann@daimi.au.dk)");
MODULE_DESCRIPTION("Kernel driver for the sysprof performance analysis tool");
