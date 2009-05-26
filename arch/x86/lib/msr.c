#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/smp.h>
#include <asm/msr.h>

struct msr_info {
	u32 msr_no;
	u32 l, h;
	int err;
};

static void __rdmsr_on_cpu(void *info)
{
	struct msr_info *rv = info;

	rdmsr(rv->msr_no, rv->l, rv->h);
}

static void __wrmsr_on_cpu(void *info)
{
	struct msr_info *rv = info;

	wrmsr(rv->msr_no, rv->l, rv->h);
}

int rdmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h)
{
	int err;
	struct msr_info rv;

	rv.msr_no = msr_no;
	err = smp_call_function_single(cpu, __rdmsr_on_cpu, &rv, 1);
	*l = rv.l;
	*h = rv.h;

	return err;
}
EXPORT_SYMBOL(rdmsr_on_cpu);

int wrmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h)
{
	int err;
	struct msr_info rv;

	rv.msr_no = msr_no;
	rv.l = l;
	rv.h = h;
	err = smp_call_function_single(cpu, __wrmsr_on_cpu, &rv, 1);

	return err;
}
EXPORT_SYMBOL(wrmsr_on_cpu);

/* rdmsr on a bunch of CPUs
 *
 * @mask:	which CPUs
 * @msr_no:	which MSR
 * @msrs:	array of MSR values
 *
 * Returns:
 * 0 - success
 * <0 - read failed on at least one CPU (latter in the mask)
 */
int rdmsr_on_cpus(const cpumask_t *mask, u32 msr_no, struct msr *msrs)
{
	struct msr *reg;
	int cpu, tmp, err = 0;
	int off = cpumask_first(mask);

	for_each_cpu(cpu, mask) {
		reg = &msrs[cpu - off];

		tmp = rdmsr_on_cpu(cpu, msr_no, &reg->l, &reg->h);
		if (tmp)
			err = tmp;
	}
	return err;
}
EXPORT_SYMBOL(rdmsr_on_cpus);

/*
 * wrmsr of a bunch of CPUs
 *
 * @mask:	which CPUs
 * @msr_no:	which MSR
 * @msrs:	array of MSR values
  *
 * Returns:
 * 0 - success
 * <0 - write failed on at least one CPU (latter in the mask)
 */
int wrmsr_on_cpus(const cpumask_t *mask, u32 msr_no, struct msr *msrs)
{
	struct msr reg;
	int cpu, tmp, err = 0;
	int off = cpumask_first(mask);

	for_each_cpu(cpu, mask) {
		reg = msrs[cpu - off];

		tmp = wrmsr_on_cpu(cpu, msr_no, reg.l, reg.h);
		if (tmp)
			err = tmp;
	}
	return err;
}
EXPORT_SYMBOL(wrmsr_on_cpus);

/* These "safe" variants are slower and should be used when the target MSR
   may not actually exist. */
static void __rdmsr_safe_on_cpu(void *info)
{
	struct msr_info *rv = info;

	rv->err = rdmsr_safe(rv->msr_no, &rv->l, &rv->h);
}

static void __wrmsr_safe_on_cpu(void *info)
{
	struct msr_info *rv = info;

	rv->err = wrmsr_safe(rv->msr_no, rv->l, rv->h);
}

int rdmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h)
{
	int err;
	struct msr_info rv;

	rv.msr_no = msr_no;
	err = smp_call_function_single(cpu, __rdmsr_safe_on_cpu, &rv, 1);
	*l = rv.l;
	*h = rv.h;

	return err ? err : rv.err;
}
EXPORT_SYMBOL(rdmsr_safe_on_cpu);

int wrmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h)
{
	int err;
	struct msr_info rv;

	rv.msr_no = msr_no;
	rv.l = l;
	rv.h = h;
	err = smp_call_function_single(cpu, __wrmsr_safe_on_cpu, &rv, 1);

	return err ? err : rv.err;
}
EXPORT_SYMBOL(wrmsr_safe_on_cpu);
