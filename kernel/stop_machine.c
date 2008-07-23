/* Copyright 2008, 2005 Rusty Russell rusty@rustcorp.com.au IBM Corporation.
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

/* This controls the threads on each CPU. */
enum stopmachine_state {
	/* Dummy starting state for thread. */
	STOPMACHINE_NONE,
	/* Awaiting everyone to be scheduled. */
	STOPMACHINE_PREPARE,
	/* Disable interrupts. */
	STOPMACHINE_DISABLE_IRQ,
	/* Run the function */
	STOPMACHINE_RUN,
	/* Exit */
	STOPMACHINE_EXIT,
};
static enum stopmachine_state state;

struct stop_machine_data {
	int (*fn)(void *);
	void *data;
	int fnret;
};

/* Like num_online_cpus(), but hotplug cpu uses us, so we need this. */
static unsigned num_threads;
static atomic_t thread_ack;
static cpumask_t prepared_cpus;
static struct completion finished;
static DEFINE_MUTEX(lock);

static unsigned long limit;
unsigned long stopmachine_timeout = 5; /* secs, arbitrary */

static void set_state(enum stopmachine_state newstate)
{
	/* Reset ack counter. */
	atomic_set(&thread_ack, num_threads);
	smp_wmb();
	state = newstate;
}

/* Last one to ack a state moves to the next state. */
static void ack_state(void)
{
	if (atomic_dec_and_test(&thread_ack)) {
		/* If we're the last one to ack the EXIT, we're finished. */
		if (state == STOPMACHINE_EXIT)
			complete(&finished);
		else
			set_state(state + 1);
	}
}

/* This is the actual thread which stops the CPU.  It exits by itself rather
 * than waiting for kthread_stop(), because it's easier for hotplug CPU. */
static int stop_cpu(struct stop_machine_data *smdata)
{
	enum stopmachine_state curstate = STOPMACHINE_NONE;
	int uninitialized_var(ret);

	/* If we've been shoved off the normal CPU, abort. */
	if (cpu_test_and_set(smp_processor_id(), prepared_cpus))
		do_exit(0);

	/* Simple state machine */
	do {
		/* Chill out and ensure we re-read stopmachine_state. */
		cpu_relax();
		if (state != curstate) {
			curstate = state;
			switch (curstate) {
			case STOPMACHINE_DISABLE_IRQ:
				local_irq_disable();
				hard_irq_disable();
				break;
			case STOPMACHINE_RUN:
				/* |= allows error detection if functions on
				 * multiple CPUs. */
				smdata->fnret |= smdata->fn(smdata->data);
				break;
			default:
				break;
			}
			ack_state();
		}
	} while (curstate != STOPMACHINE_EXIT);

	local_irq_enable();
	do_exit(0);
}

/* Callback for CPUs which aren't supposed to do anything. */
static int chill(void *unused)
{
	return 0;
}

static bool fixup_timeout(struct task_struct **threads, const cpumask_t *cpus)
{
	unsigned int i;
	bool stagger_onwards = true;

	printk(KERN_CRIT "stopmachine: Failed to stop machine in time(%lds).\n",
			stopmachine_timeout);

	for_each_online_cpu(i) {
		if (!cpu_isset(i, prepared_cpus) && i != smp_processor_id()) {
			bool ignore;

			/* If we wanted to run on a particular CPU, and that's
			 * the one which is stuck, it's a real failure. */
			ignore = !cpus || !cpu_isset(i, *cpus);
			printk(KERN_CRIT "stopmachine: cpu#%d seems to be "
			       "stuck, %s.\n",
			       i, ignore ? "ignoring" : "FAILING");
			/* Unbind thread: it will exit when it sees
			 * that prepared_cpus bit set. */
			set_cpus_allowed(threads[i], cpu_online_map);

			if (!ignore)
				stagger_onwards = false;

			/* Pretend this one doesn't exist. */
			num_threads--;
		}
	}

	if (stagger_onwards) {
		/* Force progress. */
		set_state(state + 1);
	}

	return stagger_onwards;
}

int __stop_machine(int (*fn)(void *), void *data, const cpumask_t *cpus)
{
	int i, err;
	struct stop_machine_data active, idle;
	struct task_struct **threads;

	active.fn = fn;
	active.data = data;
	active.fnret = 0;
	idle.fn = chill;
	idle.data = NULL;

	/* This could be too big for stack on large machines. */
	threads = kcalloc(NR_CPUS, sizeof(threads[0]), GFP_KERNEL);
	if (!threads)
		return -ENOMEM;

	/* Set up initial state. */
	mutex_lock(&lock);
	init_completion(&finished);
	num_threads = num_online_cpus();
	limit = jiffies + msecs_to_jiffies(stopmachine_timeout * MSEC_PER_SEC);
	set_state(STOPMACHINE_PREPARE);

	for_each_online_cpu(i) {
		struct stop_machine_data *smdata = &idle;
		struct sched_param param = { .sched_priority = MAX_RT_PRIO-1 };

		if (!cpus) {
			if (i == first_cpu(cpu_online_map))
				smdata = &active;
		} else {
			if (cpu_isset(i, *cpus))
				smdata = &active;
		}

		threads[i] = kthread_create(stop_cpu, smdata, "kstop%u", i);
		if (IS_ERR(threads[i])) {
			err = PTR_ERR(threads[i]);
			threads[i] = NULL;
			goto kill_threads;
		}

		/* Place it onto correct cpu. */
		kthread_bind(threads[i], i);

		/* Make it highest prio. */
		if (sched_setscheduler_nocheck(threads[i], SCHED_FIFO, &param))
			BUG();
	}

	/* We've created all the threads.  Wake them all: hold this CPU so one
	 * doesn't hit this CPU until we're ready. */
	cpus_clear(prepared_cpus);
	get_cpu();
	for_each_online_cpu(i)
		wake_up_process(threads[i]);

	/* Wait all others come to life */
	while (cpus_weight(prepared_cpus) != num_online_cpus() - 1) {
		if (time_is_before_jiffies(limit)) {
			if (!fixup_timeout(threads, cpus)) {
				/* Tell them all to exit. */
				set_state(STOPMACHINE_EXIT);
				active.fnret = -EIO;
			}
			break;
		}
		cpu_relax();
	}

	/* This will release the thread on our CPU. */
	put_cpu();
	wait_for_completion(&finished);
	mutex_unlock(&lock);

	kfree(threads);

	return active.fnret;

kill_threads:
	for_each_online_cpu(i)
		if (threads[i])
			kthread_stop(threads[i]);
	mutex_unlock(&lock);

	kfree(threads);
	return err;
}

int stop_machine(int (*fn)(void *), void *data, const cpumask_t *cpus)
{
	int ret;

	/* No CPUs can come up or down during this. */
	get_online_cpus();
	ret = __stop_machine(fn, data, cpus);
	put_online_cpus();

	return ret;
}
EXPORT_SYMBOL_GPL(stop_machine);
