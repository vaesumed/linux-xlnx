#ifndef __LINUX_CPUMASK_H
#define __LINUX_CPUMASK_H

/*
 * Cpumasks provide a bitmap suitable for representing the
 * set of CPU's in a system, one bit position per CPU number up to
 * nr_cpu_ids (<= NR_CPUS).
 *
 * Old-style uses "cpumask_t", but new ops are "struct cpumask *";
 * don't put "struct cpumask"s on the stack.
 *
 * See detailed comments in the file linux/bitmap.h describing the
 * data type on which these cpumasks are based.
 *
 * For details of cpumask_scnprintf() and cpumask_parse_user(),
 *     see bitmap_scnprintf() and bitmap_parse_user() in lib/bitmap.c.
 * For details of cpulist_scnprintf() and cpulist_parse(),
 *     see bitmap_scnlistprintf() and bitmap_parselist(), in lib/bitmap.c.
 * For details of cpumask_cpuremap(), see bitmap_bitremap in lib/bitmap.c
 * For details of cpumask_remap(), see bitmap_remap in lib/bitmap.c.
 * For details of cpumask_onto(), see bitmap_onto in lib/bitmap.c.
 * For details of cpumask_fold(), see bitmap_fold in lib/bitmap.c.
 *
 * The available cpumask operations are:
 *
 * void cpumask_set_cpu(cpu, mask)	turn on bit 'cpu' in mask
 * void cpumask_clear_cpu(cpu, mask)	turn off bit 'cpu' in mask
 * int cpumask_test_and_set_cpu(cpu, mask) test and set bit 'cpu' in mask
 * int cpumask_test_cpu(cpu, mask)	true iff bit 'cpu' set in mask
 * void cpumask_setall(mask)		set all bits
 * void cpumask_clear(mask)		clear all bits
 *
 * void cpumask_and(dst, src1, src2)	dst = src1 & src2  [intersection]
 * void cpumask_or(dst, src1, src2)	dst = src1 | src2  [union]
 * void cpumask_xor(dst, src1, src2)	dst = src1 ^ src2
 * void cpumask_andnot(dst, src1, src2)	dst = src1 & ~src2
 * void cpumask_complement(dst, src)	dst = ~src
 *
 * int cpumask_equal(mask1, mask2)	Does mask1 == mask2?
 * int cpumask_intersects(mask1, mask2)	Do mask1 and mask2 intersect?
 * int cpumask_subset(mask1, mask2)	Is mask1 a subset of mask2?
 * int cpumask_empty(mask)		Is mask empty (no bits sets)?
 * int cpumask_full(mask)		Is mask full (all bits sets)?
 * int cpumask_weight(mask)		Hamming weigh - number of set bits
 *
 * void cpumask_shift_right(dst, src, n) Shift right
 * void cpumask_shift_left(dst, src, n)	Shift left
 *
 * int cpumask_first(mask)		Number lowest set bit, or >= nr_cpu_ids
 * int cpumask_next(cpu, mask)		Next cpu past 'cpu', or >= nr_cpu_ids
 *
 * void cpumask_copy(dmask, smask)	dmask = smask
 *
 * size_t cpumask_size()		Length of cpumask in bytes.
 * const struct cpumask *cpumask_of(cpu) Return cpumask with bit 'cpu' set
 * CPU_BITS_ALL				Initializer - all bits set
 * CPU_BITS_NONE			Initializer - no bits set
 * CPU_BITS_CPU0			Initializer - first bit set
 * unsigned long *cpumask_bits(mask)	Array of unsigned long's in mask
 *
 * struct cpumask *to_cpumask(const unsigned long[])
 *					Convert a bitmap to a cpumask.
 *
 * ------------------------------------------------------------------------
 *
 * int cpumask_scnprintf(buf, len, mask) Format cpumask for printing
 * int cpumask_parse_user(ubuf, ulen, mask)	Parse ascii string as cpumask
 * int cpumask_scnprintf(buf, len, mask) Format cpumask as list for printing
 * int cpumask_parse(buf, map)		Parse ascii string as cpumask
 * int cpu_remap(oldbit, old, new)	newbit = map(old, new)(oldbit)
 * void cpumask_remap(dst, src, old, new)	*dst = map(old, new)(src)
 * void cpumask_onto(dst, orig, relmap)	*dst = orig relative to relmap
 * void cpumask_fold(dst, orig, sz)	dst bits = orig bits mod sz
 *
 * for_each_cpu(cpu, mask)		for-loop cpu over mask, <= nr_cpu_ids
 * for_each_cpu_and(cpu, mask, and)	for-loop cpu over (mask & and).
 *
 * int num_online_cpus()		Number of online CPUs
 * int num_possible_cpus()		Number of all possible CPUs
 * int num_present_cpus()		Number of present CPUs
 *
 * int cpu_online(cpu)			Is some cpu online?
 * int cpu_possible(cpu)		Is some cpu possible?
 * int cpu_present(cpu)			Is some cpu present (can schedule)?
 *
 * int cpumask_any(mask)		Any cpu in mask
 * int cpumask_any_and(mask1,mask2)	Any cpu in both masks
 * int cpumask_any_but(mask,cpu)	Any cpu in mask except cpu
 *
 * for_each_possible_cpu(cpu)		for-loop cpu over cpu_possible_mask
 * for_each_online_cpu(cpu)		for-loop cpu over cpu_online_mask
 * for_each_present_cpu(cpu)		for-loop cpu over cpu_present_mask
 *
 * Subtlety:
 * 1) The 'type-checked' form of cpu_isset() causes gcc (3.3.2, anyway)
 *    to generate slightly worse code.  Note for example the additional
 *    40 lines of assembly code compiling the "for each possible cpu"
 *    loops buried in the disk_stat_read() macros calls when compiling
 *    drivers/block/genhd.c (arch i386, CONFIG_SMP=y).  So use a simple
 *    one-line #define for cpu_isset(), instead of wrapping an inline
 *    inside a macro, the way we do the other calls.
 */

#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/bitmap.h>

struct cpumask {
	DECLARE_BITMAP(bits, NR_CPUS);
};
#define cpumask_bits(maskp) ((maskp)->bits)

/* Deprecated: use struct cpumask *, or cpumask_var_t. */
typedef struct cpumask cpumask_t;

#if CONFIG_NR_CPUS == 1
/* Uniprocesor. */
#define cpumask_first(src)		({ (void)(src); 0; })
#define cpumask_next(n, src)		({ (void)(src); 1; })
#define cpumask_next_and(n, srcp, andp)	({ (void)(srcp), (void)(andp); 1; })
#define cpumask_any_but(mask, cpu)	({ (void)(mask); (void)(cpu); 0; })

#define for_each_cpu(cpu, mask)			\
	for ((cpu) = 0; (cpu) < 1; (cpu)++, (void)mask)
#define for_each_cpu_and(cpu, mask, and)	\
	for ((cpu) = 0; (cpu) < 1; (cpu)++, (void)mask, (void)and)

#define num_online_cpus()	1
#define num_possible_cpus()	1
#define num_present_cpus()	1
#define cpu_online(cpu)		((cpu) == 0)
#define cpu_possible(cpu)	((cpu) == 0)
#define cpu_present(cpu)	((cpu) == 0)
#define cpu_active(cpu)		((cpu) == 0)
#define nr_cpu_ids		1
#else
/* SMP */
extern int nr_cpu_ids;

int cpumask_first(const cpumask_t *srcp);
int cpumask_next(int n, const cpumask_t *srcp);
int cpumask_next_and(int n, const cpumask_t *srcp, const cpumask_t *andp);
int cpumask_any_but(const struct cpumask *mask, unsigned int cpu);

#define for_each_cpu(cpu, mask)				\
	for ((cpu) = -1;				\
		(cpu) = cpumask_next((cpu), (mask)),	\
		(cpu) < nr_cpu_ids;)
#define for_each_cpu_and(cpu, mask, and)				\
	for ((cpu) = -1;						\
		(cpu) = cpumask_next_and((cpu), (mask), (and)),		\
		(cpu) < nr_cpu_ids;)

#define num_online_cpus()	cpus_weight(cpu_online_map)
#define num_possible_cpus()	cpus_weight(cpu_possible_map)
#define num_present_cpus()	cpus_weight(cpu_present_map)
#define cpu_online(cpu)		cpu_isset((cpu), cpu_online_map)
#define cpu_possible(cpu)	cpu_isset((cpu), cpu_possible_map)
#define cpu_present(cpu)	cpu_isset((cpu), cpu_present_map)
#define cpu_active(cpu)		cpu_isset((cpu), cpu_active_map)
#endif /* SMP */

#if CONFIG_NR_CPUS <= BITS_PER_LONG
#define CPU_BITS_ALL						\
{								\
	[BITS_TO_LONGS(CONFIG_NR_CPUS)-1] = CPU_MASK_LAST_WORD	\
}

/* This produces more efficient code. */
#define nr_cpumask_bits	NR_CPUS

#else /* CONFIG_NR_CPUS > BITS_PER_LONG */

#define CPU_BITS_ALL						\
{								\
	[0 ... BITS_TO_LONGS(CONFIG_NR_CPUS)-2] = ~0UL,		\
	[BITS_TO_LONGS(CONFIG_NR_CPUS)-1] = CPU_MASK_LAST_WORD	\
}

#define nr_cpumask_bits	nr_cpu_ids
#endif /* CONFIG_NR_CPUS > BITS_PER_LONG */

static inline size_t cpumask_size(void)
{
	/* FIXME: Use nr_cpumask_bits once all cpumask_t assignments banished */
	return BITS_TO_LONGS(NR_CPUS) * sizeof(long);
}

/* Deprecated. */
extern cpumask_t _unused_cpumask_arg_;

#define CPU_MASK_ALL_PTR	(cpu_all_mask)
#define CPU_MASK_ALL		((cpumask_t){ CPU_BITS_ALL })
#define CPU_MASK_NONE		((cpumask_t){ CPU_BITS_NONE })
#define CPU_MASK_CPU0		((cpumask_t){ CPU_BITS_CPU0 })
#define cpu_set(cpu, dst) cpumask_set_cpu((cpu), &(dst))
#define cpu_clear(cpu, dst) cpumask_clear_cpu((cpu), &(dst))
#define cpu_test_and_set(cpu, mask) cpumask_test_and_set_cpu((cpu), &(mask))
/* No static inline type checking - see Subtlety (1) above. */
#define cpu_isset(cpu, cpumask) test_bit((cpu), (cpumask).bits)
#define cpus_setall(dst) cpumask_setall(&(dst))
#define cpus_clear(dst) cpumask_clear(&(dst))
#define cpus_and(dst, src1, src2) cpumask_and(&(dst), &(src1), &(src2))
#define cpus_or(dst, src1, src2) cpumask_or(&(dst), &(src1), &(src2))
#define cpus_xor(dst, src1, src2) cpumask_xor(&(dst), &(src1), &(src2))
#define cpus_andnot(dst, src1, src2) \
				cpumask_andnot(&(dst), &(src1), &(src2))
#define cpus_complement(dst, src) cpumask_complement(&(dst), &(src))
#define cpus_equal(src1, src2) cpumask_equal(&(src1), &(src2))
#define cpus_intersects(src1, src2) cpumask_intersects(&(src1), &(src2))
#define cpus_subset(src1, src2) cpumask_subset(&(src1), &(src2))
#define cpus_empty(src) cpumask_empty(&(src))
#define cpus_full(cpumask) cpumask_full(&(cpumask))
#define cpus_weight(cpumask) cpumask_weight(&(cpumask))
#define cpus_shift_right(dst, src, n) \
			cpumask_shift_right(&(dst), &(src), (n))
#define cpus_shift_left(dst, src, n) \
			cpumask_shift_left(&(dst), &(src), (n))
#define cpu_remap(oldbit, old, new) \
		cpumask_cpuremap((oldbit), &(old), &(new))
#define cpus_remap(dst, src, old, new) \
		cpumask_remap(&(dst), &(src), &(old), &(new))
#define cpus_onto(dst, orig, relmap) \
		cpumask_onto(&(dst), &(orig), &(relmap))
#define cpus_fold(dst, orig, sz) \
		cpumask_fold(&(dst), &(orig), sz)
#define cpus_addr(src) ((src).bits)
#define next_cpu_nr(n, src)		next_cpu(n, src)
#define cpus_weight_nr(cpumask)		cpus_weight(cpumask)
#define for_each_cpu_mask_nr(cpu, mask)	for_each_cpu_mask(cpu, mask)
#define cpumask_of_cpu(cpu) (*cpumask_of(cpu))
#define for_each_cpu_mask(cpu, mask)	for_each_cpu(cpu, &(mask))
#define for_each_cpu_mask_and(cpu, mask, and)	\
		for_each_cpu_and(cpu, &(mask), &(and))
#define first_cpu(src)		cpumask_first(&(src))
#define next_cpu(n, src)	cpumask_next((n), &(src))
#define any_online_cpu(mask)	cpumask_any_and(&(mask), cpu_online_mask)
#if NR_CPUS > BITS_PER_LONG
#define	CPUMASK_ALLOC(m)	struct m *m = kmalloc(sizeof(*m), GFP_KERNEL)
#define	CPUMASK_FREE(m)		kfree(m)
#else
#define	CPUMASK_ALLOC(m)	struct m _m, *m = &_m
#define	CPUMASK_FREE(m)
#endif
#define	CPUMASK_PTR(v, m) 	cpumask_t *v = &(m->v)
/* These strip const, as traditionally they weren't const. */
#define cpu_possible_map	(*(cpumask_t *)cpu_possible_mask)
#define cpu_online_map		(*(cpumask_t *)cpu_online_mask)
#define cpu_present_map		(*(cpumask_t *)cpu_present_mask)
#define cpu_active_map		(*(cpumask_t *)cpu_active_mask)
#define cpu_mask_all		(*(cpumask_t *)cpu_all_mask)
/* End deprecated region. */

/* verify cpu argument to cpumask_* operators */
static inline unsigned int cpumask_check(unsigned int cpu)
{
#ifdef CONFIG_DEBUG_PER_CPU_MAPS
	/* This breaks at runtime. */
	BUG_ON(cpu >= nr_cpumask_bits);
#endif /* CONFIG_DEBUG_PER_CPU_MAPS */
	return cpu;
}

/* cpumask_* operators */
static inline void cpumask_set_cpu(int cpu, volatile struct cpumask *dstp)
{
	set_bit(cpumask_check(cpu), cpumask_bits(dstp));
}

static inline void cpumask_clear_cpu(int cpu, volatile struct cpumask *dstp)
{
	clear_bit(cpumask_check(cpu), cpumask_bits(dstp));
}

/* No static inline type checking - see Subtlety (1) above. */
#define cpumask_test_cpu(cpu, cpumask) \
	test_bit(cpumask_check(cpu), (cpumask)->bits)

static inline int cpumask_test_and_set_cpu(int cpu, struct cpumask *addr)
{
	return test_and_set_bit(cpumask_check(cpu), cpumask_bits(addr));
}

static inline void cpumask_setall(struct cpumask *dstp)
{
	bitmap_fill(cpumask_bits(dstp), nr_cpumask_bits);
}

static inline void cpumask_clear(struct cpumask *dstp)
{
	bitmap_zero(cpumask_bits(dstp), nr_cpumask_bits);
}

static inline void cpumask_and(struct cpumask *dstp,
			       const struct cpumask *src1p,
			       const struct cpumask *src2p)
{
	bitmap_and(cpumask_bits(dstp), cpumask_bits(src1p),
				       cpumask_bits(src2p), nr_cpumask_bits);
}

static inline void cpumask_or(struct cpumask *dstp, const struct cpumask *src1p,
			      const struct cpumask *src2p)
{
	bitmap_or(cpumask_bits(dstp), cpumask_bits(src1p),
				      cpumask_bits(src2p), nr_cpumask_bits);
}

static inline void cpumask_xor(struct cpumask *dstp,
			       const struct cpumask *src1p,
			       const struct cpumask *src2p)
{
	bitmap_xor(cpumask_bits(dstp), cpumask_bits(src1p),
				       cpumask_bits(src2p), nr_cpumask_bits);
}

static inline void cpumask_andnot(struct cpumask *dstp,
				  const struct cpumask *src1p,
				  const struct cpumask *src2p)
{
	bitmap_andnot(cpumask_bits(dstp), cpumask_bits(src1p),
					  cpumask_bits(src2p), nr_cpumask_bits);
}

static inline void cpumask_complement(struct cpumask *dstp,
				      const struct cpumask *srcp)
{
	bitmap_complement(cpumask_bits(dstp), cpumask_bits(srcp),
					      nr_cpumask_bits);
}

static inline int cpumask_equal(const struct cpumask *src1p,
				const struct cpumask *src2p)
{
	return bitmap_equal(cpumask_bits(src1p), cpumask_bits(src2p),
						 nr_cpumask_bits);
}

static inline int cpumask_intersects(const struct cpumask *src1p,
				     const struct cpumask *src2p)
{
	return bitmap_intersects(cpumask_bits(src1p), cpumask_bits(src2p),
						      nr_cpumask_bits);
}

static inline int cpumask_subset(const struct cpumask *src1p,
				 const struct cpumask *src2p)
{
	return bitmap_subset(cpumask_bits(src1p), cpumask_bits(src2p),
						  nr_cpumask_bits);
}

static inline int cpumask_empty(const struct cpumask *srcp)
{
	return bitmap_empty(cpumask_bits(srcp), nr_cpumask_bits);
}

static inline int cpumask_full(const struct cpumask *srcp)
{
	return bitmap_full(cpumask_bits(srcp), nr_cpumask_bits);
}

static inline int __cpus_weight(const cpumask_t *srcp, int nbits)
{
	return bitmap_weight(cpumask_bits(srcp), nbits);
}

static inline int cpumask_weight(const struct cpumask *srcp)
{
	return bitmap_weight(cpumask_bits(srcp), nr_cpumask_bits);
}

static inline void cpumask_shift_right(struct cpumask *dstp,
				       const struct cpumask *srcp, int n)
{
	bitmap_shift_right(cpumask_bits(dstp), cpumask_bits(srcp), n,
					       nr_cpumask_bits);
}

static inline void cpumask_shift_left(struct cpumask *dstp,
				      const struct cpumask *srcp, int n)
{
	bitmap_shift_left(cpumask_bits(dstp), cpumask_bits(srcp), n,
					      nr_cpumask_bits);
}

static inline int cpumask_scnprintf(char *buf, int len,
				    const struct cpumask *srcp)
{
	return bitmap_scnprintf(buf, len, cpumask_bits(srcp), nr_cpumask_bits);
}

static inline int cpumask_parse_user(const char __user *buf, int len,
				     struct cpumask *dstp)
{
	return bitmap_parse_user(buf, len, cpumask_bits(dstp), nr_cpumask_bits);
}

static inline int cpulist_scnprintf(char *buf, int len,
				    const struct cpumask *srcp)
{
	return bitmap_scnlistprintf(buf, len, cpumask_bits(srcp),
					      nr_cpumask_bits);
}

static inline int cpulist_parse(const char *buf, struct cpumask *dstp)
{
	return bitmap_parselist(buf, cpumask_bits(dstp), nr_cpumask_bits);
}

static inline int cpumask_cpuremap(int oldbit,
				   const struct cpumask *oldp,
				   const struct cpumask *newp)
{
	return bitmap_bitremap(cpumask_check(oldbit), cpumask_bits(oldp),
				cpumask_bits(newp), nr_cpumask_bits);
}

static inline void cpumask_remap(struct cpumask *dstp,
				 const struct cpumask *srcp,
				 const struct cpumask *oldp,
				 const struct cpumask *newp)
{
	bitmap_remap(cpumask_bits(dstp), cpumask_bits(srcp),
		     cpumask_bits(oldp), cpumask_bits(newp), nr_cpumask_bits);
}

static inline void cpumask_onto(struct cpumask *dstp,
				const struct cpumask *origp,
				const struct cpumask *relmapp)
{
	bitmap_onto(cpumask_bits(dstp), cpumask_bits(origp),
					cpumask_bits(relmapp), nr_cpumask_bits);
}

static inline void cpumask_fold(struct cpumask *dstp,
				const struct cpumask *origp, int sz)
{
	bitmap_fold(cpumask_bits(dstp), cpumask_bits(origp), sz,
					nr_cpumask_bits);
}

static inline void cpumask_copy(struct cpumask *dstp,
				const struct cpumask *srcp)
{
	bitmap_copy(cpumask_bits(dstp), cpumask_bits(srcp), nr_cpumask_bits);
}

#define cpumask_any(srcp)		cpumask_first(srcp)
#define cpumask_any_and(mask1, mask2)	cpumask_first_and((mask1), (mask2))

/* Used for static bitmaps of CONFIG_NR_CPUS bits.  Must be a constant to use
 * as an initializer. */
#define to_cpumask(bitmap)						\
	((struct cpumask *)(1 ? (bitmap)				\
			    : (void *)sizeof(__check_is_bitmap(bitmap))))

static inline int __check_is_bitmap(const unsigned long *bitmap)
{
	return 1;
}

/*
 * Special-case data structure for "single bit set only" constant CPU masks.
 *
 * We pre-generate all the 64 (or 32) possible bit positions, with enough
 * padding to the left and the right, and return the constant pointer
 * appropriately offset.
 */
extern const unsigned long
	cpu_bit_bitmap[BITS_PER_LONG+1][BITS_TO_LONGS(NR_CPUS)];

static inline const struct cpumask *cpumask_of(unsigned int cpu)
{
	const unsigned long *p = cpu_bit_bitmap[1 + cpu % BITS_PER_LONG];
	p -= cpu / BITS_PER_LONG;
	return (const struct cpumask *)p;
}

#define CPU_MASK_LAST_WORD BITMAP_LAST_WORD_MASK(CONFIG_NR_CPUS)

#define CPU_BITS_NONE						\
{								\
	[0 ... BITS_TO_LONGS(CONFIG_NR_CPUS)-1] = 0UL		\
}

#define CPU_BITS_CPU0						\
{								\
	[0] =  1UL						\
}

#define cpumask_first_and(mask, and) cpumask_next_and(-1, (mask), (and))

/*
 * cpumask_var_t: struct cpumask for stack usage.
 *
 * Oh, the wicked games we play!  In order to make kernel coding a
 * little more difficult, we typedef cpumask_var_t to an array or a
 * pointer: doing &mask on an array is a noop, so it still works.
 *
 * ie.
 *	cpumask_var_t tmpmask;
 *	if (!alloc_cpumask_var(&tmpmask, GFP_KERNEL))
 *		return -ENOMEM;
 *
 *	  ... use 'tmpmask' like a normal struct cpumask * ...
 *
 *	free_cpumask_var(tmpmask);
 */
#ifdef CONFIG_CPUMASK_OFFSTACK
typedef struct cpumask *cpumask_var_t;

bool alloc_cpumask_var(cpumask_var_t *mask, gfp_t flags);
void free_cpumask_var(cpumask_var_t mask);

#else
typedef struct cpumask cpumask_var_t[1];

static inline bool alloc_cpumask_var(cpumask_var_t *mask, gfp_t flags)
{
	return true;
}

static inline void free_cpumask_var(cpumask_var_t mask)
{
}

#endif /* CONFIG_CPUMASK_OFFSTACK */

/*
 * The following particular system cpumasks and operations manage
 * possible, present, active and online cpus.
 *
 *     cpu_possible_mask- has bit 'cpu' set iff cpu is populatable
 *     cpu_present_mask - has bit 'cpu' set iff cpu is populated
 *     cpu_online_mask  - has bit 'cpu' set iff cpu available to scheduler
 *     cpu_active_mask  - has bit 'cpu' set iff cpu available to migration
 *
 *  If !CONFIG_HOTPLUG_CPU, present == possible, and active == online.
 *
 *  The cpu_possible_mask is fixed at boot time, as the set of CPU id's
 *  that it is possible might ever be plugged in at anytime during the
 *  life of that system boot.  The cpu_present_mask is dynamic(*),
 *  representing which CPUs are currently plugged in.  And
 *  cpu_online_mask is the dynamic subset of cpu_present_mask,
 *  indicating those CPUs available for scheduling.
 *
 *  If HOTPLUG is enabled, then cpu_possible_mask is forced to have
 *  all NR_CPUS bits set, otherwise it is just the set of CPUs that
 *  ACPI reports present at boot.
 *
 *  If HOTPLUG is enabled, then cpu_present_mask varies dynamically,
 *  depending on what ACPI reports as currently plugged in, otherwise
 *  cpu_present_mask is just a copy of cpu_possible_mask.
 *
 *  (*) Well, cpu_present_mask is dynamic in the hotplug case.  If not
 *      hotplug, it's a copy of cpu_possible_mask, hence fixed at boot.
 *
 * Subtleties:
 * 1) UP arch's (NR_CPUS == 1, CONFIG_SMP not defined) hardcode
 *    assumption that their single CPU is online.  The UP
 *    cpu_{online,possible,present}_masks are placebos.  Changing them
 *    will have no useful affect on the following num_*_cpus()
 *    and cpu_*() macros in the UP case.  This ugliness is a UP
 *    optimization - don't waste any instructions or memory references
 *    asking if you're online or how many CPUs there are if there is
 *    only one CPU.
 */

extern const struct cpumask *const cpu_possible_mask;
extern const struct cpumask *const cpu_online_mask;
extern const struct cpumask *const cpu_present_mask;
extern const struct cpumask *const cpu_active_mask;

/*
 * It's common to want to use cpu_all_mask in struct member initializers,
 * so it has to refer to an address rather than a pointer:
 */
extern const DECLARE_BITMAP(cpu_all_bits, CONFIG_NR_CPUS);
#define cpu_all_mask to_cpumask(cpu_all_bits)

/* First bits of cpu_bit_bitmap are in fact unset. */
#define cpu_none_mask to_cpumask(cpu_bit_bitmap[0])

/* Wrappers to manipulate otherwise-constant masks. */
void set_cpu_possible(unsigned int cpu, bool possible);
void set_cpu_present(unsigned int cpu, bool present);
void set_cpu_online(unsigned int cpu, bool online);
void set_cpu_active(unsigned int cpu, bool active);
void init_cpu_present(const struct cpumask *src);
void init_cpu_possible(const struct cpumask *src);
void init_cpu_online(const struct cpumask *src);

#define cpu_is_offline(cpu)	unlikely(!cpu_online(cpu))

#define for_each_possible_cpu(cpu) for_each_cpu((cpu), cpu_possible_mask)
#define for_each_online_cpu(cpu)   for_each_cpu((cpu), cpu_online_mask)
#define for_each_present_cpu(cpu)  for_each_cpu((cpu), cpu_present_mask)

#endif /* __LINUX_CPUMASK_H */
