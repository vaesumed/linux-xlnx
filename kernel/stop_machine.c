/* Copyright 2005 Rusty Russell rusty@rustcorp.com.au IBM Corporation.
 * GPL v2 and any later version.
 */
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/stop_machine.h>
#include <linux/syscalls.h>
#include <linux/interrupt.h>

#include <asm/atomic.h>
#include <asm/uaccess.h>

/* Since we effect priority and affinity (both of which are visible
 * to, and settable by outside processes) we do indirection via a
 * kthread. */

/* Thread to stop each CPU in user context. */
enum stopmachine_state {
	STOPMACHINE_WAIT,
	STOPMACHINE_DEPLOY,
	STOPMACHINE_PREPARE,
	STOPMACHINE_DISABLE_IRQ,
	STOPMACHINE_RUN,
	STOPMACHINE_EXIT,
};

struct stop_machine_data {
	int (*fn)(void *);
	void *data;
	struct completion done;
	int run_all;
} smdata;

static enum stopmachine_state stopmachine_state;
static unsigned int stopmachine_num_threads;
static atomic_t stopmachine_thread_ack;
static atomic_t stopmachine_busy_exit;

unsigned long stopmachine_timeout = 5; /* secs, arbitrary */

static int stopmachine(void *cpu)
{
	int irqs_disabled = 0;
	int prepared = 0;
	int ran = 0;

	/* Wait sisters */
	while (stopmachine_state == STOPMACHINE_WAIT)
		yield();
	/* short path for cancel */
	if (stopmachine_state == STOPMACHINE_EXIT)
		goto exit;

	/* If target cpu is on fire, this call can stuck */
	set_cpus_allowed_ptr(current, &cpumask_of_cpu((int)(long)cpu));

	/* Ack: we arrived */
	smp_mb(); /* Theoretically the ack = 0 might not be on this CPU yet. */
	atomic_inc(&stopmachine_thread_ack);

	/* Simple state machine */
	while (stopmachine_state != STOPMACHINE_EXIT) {
		if (stopmachine_state == STOPMACHINE_DISABLE_IRQ 
		    && !irqs_disabled) {
			local_irq_disable();
			hard_irq_disable();
			irqs_disabled = 1;
			/* Ack: irqs disabled. */
			smp_mb(); /* Must read state first. */
			atomic_inc(&stopmachine_thread_ack);
		} else if (stopmachine_state == STOPMACHINE_PREPARE
			   && !prepared) {
			/* Everyone is in place, hold CPU. */
			preempt_disable();
			prepared = 1;
			smp_mb(); /* Must read state first. */
			atomic_inc(&stopmachine_thread_ack);
		} else if (stopmachine_state == STOPMACHINE_RUN && !ran) {
			smdata.fn(smdata.data);
			ran = 1;
			smp_mb(); /* Must read state first. */
			atomic_inc(&stopmachine_thread_ack);
		}
		/* Yield in first stage: migration threads need to
		 * help our sisters onto their CPUs. */
		if (!prepared && !irqs_disabled)
			yield();
		else
			cpu_relax();
	}
exit:
	/* Ack: we are exiting. */
	smp_mb(); /* Must read state first. */
	atomic_inc(&stopmachine_thread_ack);

	if (irqs_disabled)
		local_irq_enable();
	if (prepared)
		preempt_enable();

	if (atomic_read(&stopmachine_busy_exit)) {
		atomic_dec(&stopmachine_busy_exit);
		printk(KERN_INFO "stopmachine: cpu#%d is not busy now.\n",
			(int)(long)cpu);
	}

	return 0;
}

/* Change the thread state */
static void stopmachine_set_state(enum stopmachine_state state)
{
	atomic_set(&stopmachine_thread_ack, 0);
	smp_wmb();
	stopmachine_state = state;
	while (atomic_read(&stopmachine_thread_ack) != stopmachine_num_threads)
		cpu_relax();
}

static int stop_machine(void)
{
	int i, ret = 0;
	unsigned long limit;

	if (atomic_read(&stopmachine_busy_exit)) {
		/*
		 * previous try was timeout, and still there is a unreachable
		 * cpu and abandoned child.
		 */
		return -EBUSY;
	}

	atomic_set(&stopmachine_thread_ack, 0);
	stopmachine_num_threads = 0;
	stopmachine_state = STOPMACHINE_WAIT;

	for_each_online_cpu(i) {
		if (i == raw_smp_processor_id())
			continue;
		ret = kernel_thread(stopmachine, (void *)(long)i,CLONE_KERNEL);
		if (ret < 0)
			goto exit_threads;
		stopmachine_num_threads++;
	}

	limit = jiffies + msecs_to_jiffies(stopmachine_timeout * MSEC_PER_SEC);

	/* Wait for them all to come to life on the target. */
	stopmachine_state = STOPMACHINE_DEPLOY;
	while (atomic_read(&stopmachine_thread_ack) != stopmachine_num_threads)
		if (time_is_after_jiffies(limit))
			yield();
		else
			goto deploy_timeout;

	/* Now they are all started, make them hold the CPUs, ready. */
	preempt_disable();
	stopmachine_set_state(STOPMACHINE_PREPARE);

	/* Make them disable irqs. */
	local_irq_disable();
	hard_irq_disable();
	stopmachine_set_state(STOPMACHINE_DISABLE_IRQ);

	return 0;

deploy_timeout:
	printk(KERN_CRIT "stopmachine: Failed to stop machine in time(%lds). "
		"Are there any CPUs on file?\n", stopmachine_timeout);

	/* defer exit check to the beginning of next try. */
	atomic_set(&stopmachine_busy_exit, stopmachine_num_threads);

	printk(KERN_INFO "stopmachine: cpu#%d is initiator of failed stop.\n",
			raw_smp_processor_id());
	smp_wmb();
	stopmachine_state = STOPMACHINE_EXIT;

	return -EBUSY;

exit_threads:
	/* Wait for them all to exit, since stop is canceled */
	stopmachine_set_state(STOPMACHINE_EXIT);

	return ret;
}

static void restart_machine(void)
{
	stopmachine_set_state(STOPMACHINE_EXIT);
	local_irq_enable();
	preempt_enable_no_resched();
}

static void run_other_cpus(void)
{
	stopmachine_set_state(STOPMACHINE_RUN);
}

static int do_stop(void *_smdata)
{
	struct stop_machine_data *smdata = _smdata;
	int ret;

	ret = stop_machine();
	if (ret == 0) {
		ret = smdata->fn(smdata->data);
		if (smdata->run_all)
			run_other_cpus();
		restart_machine();
	}

	/* We're done: you can kthread_stop us now */
	complete(&smdata->done);

	/* Wait for kthread_stop */
	set_current_state(TASK_INTERRUPTIBLE);
	while (!kthread_should_stop()) {
		schedule();
		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);
	return ret;
}

struct task_struct *__stop_machine_run(int (*fn)(void *), void *data,
				       unsigned int cpu)
{
	static DEFINE_MUTEX(stopmachine_mutex);
	struct stop_machine_data smdata;
	struct task_struct *p;

	mutex_lock(&stopmachine_mutex);

	smdata.fn = fn;
	smdata.data = data;
	smdata.run_all = (cpu == ALL_CPUS) ? 1 : 0;
	init_completion(&smdata.done);

	smp_wmb(); /* make sure other cpus see smdata updates */

	/* If they don't care which CPU fn runs on, bind to any online one. */
	if (cpu == NR_CPUS || cpu == ALL_CPUS)
		cpu = raw_smp_processor_id();

	p = kthread_create(do_stop, &smdata, "kstopmachine");
	if (!IS_ERR(p)) {
		struct sched_param param = { .sched_priority = MAX_RT_PRIO-1 };

		/* One high-prio thread per cpu.  We'll do this one. */
		sched_setscheduler(p, SCHED_FIFO, &param);
		kthread_bind(p, cpu);
		wake_up_process(p);
		wait_for_completion(&smdata.done);
	}
	mutex_unlock(&stopmachine_mutex);
	return p;
}

int stop_machine_run_notype(int (*fn)(void *), void *data, unsigned int cpu)
{
	struct task_struct *p;
	int ret;

	/* No CPUs can come up or down during this. */
	get_online_cpus();
	p = __stop_machine_run(fn, data, cpu);
	if (!IS_ERR(p))
		ret = kthread_stop(p);
	else
		ret = PTR_ERR(p);
	put_online_cpus();

	return ret;
}
EXPORT_SYMBOL_GPL(stop_machine_run_notype);
