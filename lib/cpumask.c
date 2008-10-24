#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/module.h>

int cpumask_first(const cpumask_t *srcp)
{
	return find_first_bit(cpumask_bits(srcp), nr_cpumask_bits);
}
EXPORT_SYMBOL(cpumask_first);

int cpumask_next(int n, const cpumask_t *srcp)
{
	/* -1 is a legal arg here. */
	if (n != -1)
		cpumask_check(n);
	return find_next_bit(cpumask_bits(srcp), nr_cpumask_bits, n+1);
}
EXPORT_SYMBOL(cpumask_next);

int cpumask_next_and(int n, const cpumask_t *srcp, const cpumask_t *andp)
{
	while ((n = cpumask_next(n, srcp)) < nr_cpu_ids)
		if (cpumask_test_cpu(n, andp))
			break;
	return n;
}
EXPORT_SYMBOL(cpumask_next_and);

int cpumask_any_but(const struct cpumask *mask, unsigned int cpu)
{
	unsigned int i;

	for_each_cpu(i, mask)
		if (i != cpu)
			break;
	return i;
}

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
