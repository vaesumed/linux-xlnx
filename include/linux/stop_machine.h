#ifndef _LINUX_STOP_MACHINE
#define _LINUX_STOP_MACHINE
/* "Bogolock": stop the entire machine, disable interrupts.  This is a
   very heavy lock, which is equivalent to grabbing every spinlock
   (and more).  So the "read" side to such a lock is anything which
   diables preeempt. */
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/compiler.h>
#include <asm/system.h>

/* Deprecated, but useful for transition. */
#define ALL_CPUS CPU_MASK_ALL_PTR

/**
 * stop_machine_run: freeze the machine on all CPUs and run this function
 * @fn: the function to run
 * @data: the data ptr for the @fn()
 * @cpus: the cpus to run the @fn() on (NULL = any online cpu)
 *
 * Description: This causes a thread to be scheduled on every cpu,
 * each of which disables interrupts.  The result is that noone is
 * holding a spinlock or inside any other preempt-disabled region when
 * @fn() runs.
 *
 * This can be thought of as a very heavy write lock, equivalent to
 * grabbing every spinlock in the kernel. */
#define stop_machine_run(fn, data, cpus)				\
	stop_machine_run_notype(typesafe_cb(int, (fn), (data)), (data), (cpus))

#if defined(CONFIG_STOP_MACHINE) && defined(CONFIG_SMP)
int stop_machine_run_notype(int (*fn)(void *), void *, const cpumask_t *cpus);

/**
 * __stop_machine_run: freeze the machine on all CPUs and run this function
 * @fn: the function to run
 * @data: the data ptr for the @fn
 * @cpus: the cpus to run the @fn() on (NULL = any online cpu)
 *
 * Description: This is a special version of the above, which assumes cpus
 * won't come or go while it's being called.  Used by hotplug cpu.
 */
int __stop_machine_run(int (*fn)(void *), void *data, const cpumask_t *cpus);
#else

static inline int stop_machine_run_notype(int (*fn)(void *), void *data,
					  const cpumask_t *cpus)
{
	int ret;
	local_irq_disable();
	ret = fn(data);
	local_irq_enable();
	return ret;
}
#endif /* CONFIG_SMP */
#endif /* _LINUX_STOP_MACHINE */
