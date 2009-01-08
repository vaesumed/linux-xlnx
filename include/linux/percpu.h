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
	PER_CPU_ATTRIBUTES __typeof__(type) __percpu name

#ifdef MODULE
#define SHARED_ALIGNED_SECTION ".data.percpu"
#else
#define SHARED_ALIGNED_SECTION ".data.percpu.shared_aligned"
#endif

#define DEFINE_PER_CPU_SHARED_ALIGNED(type, name)			\
	__attribute__((__section__(SHARED_ALIGNED_SECTION)))		\
	PER_CPU_ATTRIBUTES __typeof__(type) __percpu name		\
	____cacheline_aligned_in_smp

#define DEFINE_PER_CPU_PAGE_ALIGNED(type, name)			\
	__attribute__((__section__(".data.percpu.page_aligned")))	\
	PER_CPU_ATTRIBUTES __typeof__(type) __percpu name
#else
#define DEFINE_PER_CPU(type, name)					\
	PER_CPU_ATTRIBUTES __typeof__(type) __percpu name

#define DEFINE_PER_CPU_SHARED_ALIGNED(type, name)		      \
	DEFINE_PER_CPU(type, name)

#define DEFINE_PER_CPU_PAGE_ALIGNED(type, name)		      \
	DEFINE_PER_CPU(type, name)
#endif

#define EXPORT_PER_CPU_SYMBOL(var) EXPORT_SYMBOL(var)
#define EXPORT_PER_CPU_SYMBOL_GPL(var) EXPORT_SYMBOL_GPL(var)

#ifndef PERCPU_ENOUGH_ROOM
extern unsigned int percpu_reserve;

#define PERCPU_ENOUGH_ROOM (__per_cpu_end - __per_cpu_start + percpu_reserve)
#endif	/* PERCPU_ENOUGH_ROOM */

/*
 * Must be an lvalue. Since @var must be a simple identifier,
 * we force a syntax error here if it isn't.
 */
#define get_cpu_var(var) (*({				\
	extern int simple_identifier_##var(void);	\
	preempt_disable();				\
	&__get_cpu_var(var); }))
#define put_cpu_var(var) preempt_enable()

/**
 * put_cpu_ptr - return a pointer to this cpu's allocated memory
 * @ptr: the pointer passed to get_cpu_ptr().
 *
 * Counterpart to get_cpu_ptr(): re-enables preemption
 */
#define put_cpu_ptr(ptr) preempt_enable()

/**
 * get_cpu_ptr - hold a pointer to this cpu's allocated memory
 * @ptr: the pointer returned from alloc_percpu
 *
 * Similar to get_cpu_var(), except for dynamic memory.  Disables preemption.
 */
#define get_cpu_ptr(ptr) ({ preempt_disable(); __get_cpu_ptr(ptr); })

#ifdef CONFIG_SMP
void *__alloc_percpu(unsigned long size, unsigned long align);
void free_percpu(void *pcpuptr);
void percpu_alloc_init(void);
#else
static inline void *__alloc_percpu(unsigned long size, unsigned long align)
{
	return kzalloc(size, GFP_KERNEL);
}

static inline void free_percpu(void *pcpuptr)
{
	kfree(pcpuptr);
}

static inline void percpu_alloc_init(void)
{
}
#endif /* CONFIG_SMP */

/**
 * alloc_percpu - allocate memory on every possible cpu.
 * @type: the type to allocate
 *
 * Allocates memory for use with per_cpu_ptr/get_cpu_ptr/__get_cpu_ptr.
 * The memory is always zeroed.  Returns NULL on failure.
 *
 * Note that percpu memory is a limited resource; it's usually used for small
 * allocations.  Use big_alloc_percpu/big_cpu_ptr if that's not the case.
 */
#define alloc_percpu(type) \
	(type *)__alloc_percpu(sizeof(type), __alignof__(type))

/* Big per-cpu allocations.  Less efficient, but if you're doing an unbounded
 * number of allocations or large ones, you need these until we implement
 * growing percpu regions. */
#ifdef CONFIG_SMP
extern void *big_alloc_percpu(unsigned long size);
extern void big_free_percpu(const void *);

#define big_per_cpu_ptr(bptr, cpu) ({		\
	void **__bp = (void **)(bptr);		\
	(__typeof__(bptr))__bp[(cpu)];		\
})
#else
static inline void *big_alloc_percpu(unsigned long size)
{
	return kzalloc(size, GFP_KERNEL);
}
static inline void big_free_percpu(const void *bp)
{
	kfree(bp);
}

#define big_per_cpu_ptr(ptr, cpu) ({ (void)(cpu); (ptr); })
#endif /* !CONFIG_SMP */

#endif /* __LINUX_PERCPU_H */
