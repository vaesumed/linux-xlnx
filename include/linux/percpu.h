#ifndef __LINUX_PERCPU_H
#define __LINUX_PERCPU_H

#include <linux/preempt.h>
#include <linux/slab.h> /* For kmalloc() */
#include <linux/smp.h>
#include <linux/cpumask.h>

#include <asm/percpu.h>

#ifdef CONFIG_SMP
#define DEFINE_PER_CPU(type, name)					\
	__attribute__((__section__(".data.percpu")))			\
	PER_CPU_ATTRIBUTES __typeof__(type) per_cpu__##name

#ifdef MODULE
#define SHARED_ALIGNED_SECTION ".data.percpu"
#else
#define SHARED_ALIGNED_SECTION ".data.percpu.shared_aligned"
#endif

#define DEFINE_PER_CPU_SHARED_ALIGNED(type, name)			\
	__attribute__((__section__(SHARED_ALIGNED_SECTION)))		\
	PER_CPU_ATTRIBUTES __typeof__(type) per_cpu__##name		\
	____cacheline_aligned_in_smp

#define DEFINE_PER_CPU_PAGE_ALIGNED(type, name)			\
	__attribute__((__section__(".data.percpu.page_aligned")))	\
	PER_CPU_ATTRIBUTES __typeof__(type) per_cpu__##name

#ifdef CONFIG_HAVE_ZERO_BASED_PER_CPU
#define DEFINE_PER_CPU_FIRST(type, name)				\
	__attribute__((__section__(".data.percpu.first")))		\
	PER_CPU_ATTRIBUTES __typeof__(type) per_cpu__##name
#else
#define DEFINE_PER_CPU_FIRST(type, name)				\
	DEFINE_PER_CPU(type, name)
#endif

#else /* !CONFIG_SMP */

#define DEFINE_PER_CPU(type, name)					\
	PER_CPU_ATTRIBUTES __typeof__(type) per_cpu__##name

#define DEFINE_PER_CPU_SHARED_ALIGNED(type, name)		      \
	DEFINE_PER_CPU(type, name)

#define DEFINE_PER_CPU_PAGE_ALIGNED(type, name)		      \
	DEFINE_PER_CPU(type, name)

#define DEFINE_PER_CPU_FIRST(type, name)				\
	DEFINE_PER_CPU(type, name)

#endif /* !CONFIG_SMP */

#define EXPORT_PER_CPU_SYMBOL(var) EXPORT_SYMBOL(per_cpu__##var)
#define EXPORT_PER_CPU_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(per_cpu__##var)

extern unsigned int percpu_reserve;
/* Enough to cover all DEFINE_PER_CPUs in kernel, including modules. */
#ifndef PERCPU_AREA_SIZE
#ifdef CONFIG_MODULES
#define PERCPU_RESERVE_SIZE	8192
#else
#define PERCPU_RESERVE_SIZE	0
#endif

#define PERCPU_AREA_SIZE						\
	(__per_cpu_end - __per_cpu_start + percpu_reserve)
#endif	/* PERCPU_AREA_SIZE */

/*
 * Must be an lvalue. Since @var must be a simple identifier,
 * we force a syntax error here if it isn't.
 */
#define get_cpu_var(var) (*({				\
	extern int simple_identifier_##var(void);	\
	preempt_disable();				\
	&__get_cpu_var(var); }))
#define put_cpu_var(var) preempt_enable()

#ifdef CONFIG_SMP

struct percpu_data {
	void *ptrs[1];
};

#define __percpu_disguise(pdata) (struct percpu_data *)~(unsigned long)(pdata)
/* 
 * Use this to get to a cpu's version of the per-cpu object dynamically
 * allocated. Non-atomic access to the current CPU's version should
 * probably be combined with get_cpu()/put_cpu().
 */ 
#define percpu_ptr(ptr, cpu)                              \
({                                                        \
        struct percpu_data *__p = __percpu_disguise(ptr); \
        (__typeof__(ptr))__p->ptrs[(cpu)];	          \
})

extern void *__percpu_alloc_mask(size_t size, gfp_t gfp, cpumask_t *mask);
extern void percpu_free(void *__pdata);

#else /* CONFIG_SMP */

#define percpu_ptr(ptr, cpu) ({ (void)(cpu); (ptr); })

static __always_inline void *__percpu_alloc_mask(size_t size, gfp_t gfp, cpumask_t *mask)
{
	return kzalloc(size, gfp);
}

static inline void percpu_free(void *__pdata)
{
	kfree(__pdata);
}

#endif /* CONFIG_SMP */

#define percpu_alloc_mask(size, gfp, mask) \
	__percpu_alloc_mask((size), (gfp), &(mask))

#define percpu_alloc(size, gfp) percpu_alloc_mask((size), (gfp), cpu_online_map)

/* (legacy) interface for use without CPU hotplug handling */

#define __alloc_percpu(size)	percpu_alloc_mask((size), GFP_KERNEL, \
						  cpu_possible_map)
#define alloc_percpu(type)	(type *)__alloc_percpu(sizeof(type))
#define free_percpu(ptr)	percpu_free((ptr))
#define per_cpu_ptr(ptr, cpu)	percpu_ptr((ptr), (cpu))


/*
 * cpu allocator definitions
 *
 * The cpu allocator allows allocating an instance of an object for each
 * processor and the use of a single pointer to access all instances
 * of the object. cpu_alloc provides optimized means for accessing the
 * instance of the object belonging to the currently executing processor
 * as well as special atomic operations on fields of objects of the
 * currently executing processor.
 *
 * Cpu objects are typically small. The allocator packs them tightly
 * to increase the chance on each access that a per cpu object is already
 * cached. Alignments may be specified but the intent is to align the data
 * properly due to cpu alignment constraints and not to avoid cacheline
 * contention. Any holes left by aligning objects are filled up with smaller
 * objects that are allocated later.
 *
 * Cpu data can be allocated using CPU_ALLOC. The resulting pointer is
 * pointing to the instance of the variable in the per cpu area provided
 * by the loader. It is generally an error to use the pointer directly
 * unless we are booting the system.
 *
 * __GFP_ZERO may be passed as a flag to zero the allocated memory.
 */

/*
 * Raw calls
 */
#ifdef CONFIG_SMP
void *cpu_alloc(unsigned long size, gfp_t flags, unsigned long align);
void cpu_free(void *cpu_pointer, unsigned long size);

#else
static inline void *cpu_alloc(unsigned long size, gfp_t flags, unsigned long align)
{
	return kmalloc(size, flags);
}

static inline void cpu_free(void *cpu_pointer, unsigned long size)
{
	kfree(cpu_pointer);
}

#define SHIFT_PERCPU_PTR(__p, __offset)	(__p)
#endif

/* Return a pointer to the instance of a object for a particular processor */
#define CPU_PTR(__p, __cpu)	SHIFT_PERCPU_PTR((__p), per_cpu_offset(__cpu))

/*
 * Return a pointer to the instance of the object belonging to the processor
 * running the current code.
 */
#define THIS_CPU(__p)	SHIFT_PERCPU_PTR((__p), my_cpu_offset)
#define __THIS_CPU(__p)	SHIFT_PERCPU_PTR((__p), __my_cpu_offset)

#define CPU_ALLOC(type, flags)	((typeof(type) *)cpu_alloc(sizeof(type), (flags), \
							__alignof__(type)))
#define CPU_FREE(pointer)	cpu_free((pointer), sizeof(*(pointer)))


#endif /* __LINUX_PERCPU_H */
