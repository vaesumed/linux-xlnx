#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/module.h>

int __first_cpu(const cpumask_t *srcp)
{
	return find_first_bit(cpumask_bits(srcp), nr_cpumask_bits);
}
EXPORT_SYMBOL(__first_cpu);

int __next_cpu(int n, const cpumask_t *srcp)
{
	return find_next_bit(cpumask_bits(srcp), nr_cpumask_bits, n+1);
}
EXPORT_SYMBOL(__next_cpu);

int cpumask_next_and(int n, const cpumask_t *srcp, const cpumask_t *andp)
{
	while ((n = next_cpu(n, *srcp)) < nr_cpu_ids)
		if (cpumask_test_cpu(n, andp))
			break;
	return n;
}
EXPORT_SYMBOL(cpumask_next_and);

int __any_online_cpu(const cpumask_t *mask)
{
	int cpu;

	for_each_cpu(cpu, mask) {
		if (cpu_online(cpu))
			break;
	}
	return cpu;
}
EXPORT_SYMBOL(__any_online_cpu);

/* These are not inline because of header tangles. */
#ifdef CONFIG_CPUMASK_OFFSTACK
bool alloc_cpumask_var(cpumask_var_t *mask, gfp_t flags)
{
	if (likely(slab_is_available()))
		*mask = kmalloc(cpumask_size(), flags);
	else {
#ifdef CONFIG_DEBUG_PER_CPU_MAPS
		printk(KERN_ERR
			"=> alloc_cpumask_var: kmalloc not available!\n");
		dump_stack();
#endif
		*mask = NULL;
	}
#ifdef CONFIG_DEBUG_PER_CPU_MAPS
	if (!*mask) {
		printk(KERN_ERR "=> alloc_cpumask_var: failed!\n");
		dump_stack();
	}
#endif
	return *mask != NULL;
}
EXPORT_SYMBOL(alloc_cpumask_var);

void free_cpumask_var(cpumask_var_t mask)
{
	kfree(mask);
}
EXPORT_SYMBOL(free_cpumask_var);
#endif
