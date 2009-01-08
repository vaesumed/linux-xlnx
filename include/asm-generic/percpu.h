#ifndef _ASM_GENERIC_PERCPU_H_
#define _ASM_GENERIC_PERCPU_H_
#include <linux/compiler.h>
#include <linux/threads.h>

/*
 * Determine the real variable name from the name visible in the
 * kernel sources.
 */
#define per_cpu_var(var) var

#ifdef CONFIG_SMP

/*
 * per_cpu_offset() is the offset that has to be added to a
 * percpu variable to get to the instance for a certain processor.
 *
 * Most arches use the __per_cpu_offset array for those offsets but
 * some arches have their own ways of determining the offset (x86_64, s390).
 */
#ifndef __per_cpu_offset
extern unsigned long __per_cpu_offset[NR_CPUS];

#define per_cpu_offset(x) (__per_cpu_offset[x])
#endif

/*
 * Determine the offset for the currently active processor.
 * An arch may define __my_cpu_offset to provide a more effective
 * means of obtaining the offset to the per cpu variables of the
 * current processor.
 */
#ifndef __my_cpu_offset
#define __my_cpu_offset per_cpu_offset(raw_smp_processor_id())
#endif
#ifdef CONFIG_DEBUG_PREEMPT
#define my_cpu_offset per_cpu_offset(smp_processor_id())
#else
#define my_cpu_offset __my_cpu_offset
#endif

/*
 * Add a offset to a pointer but keep the pointer as is.
 *
 * Only S390 provides its own means of moving the pointer.
 */
#ifndef SHIFT_PERCPU_PTR
/* Weird cast keeps both GCC and sparse happy. */
#define SHIFT_PERCPU_PTR(__p, __offset)	\
	((typeof(*__p) __kernel __force *)RELOC_HIDE((__p), (__offset)))
#endif

/*
 * A percpu variable may point to a discarded regions. The following are
 * established ways to produce a usable pointer from the percpu variable
 * offset.
 */
#define per_cpu(var, cpu) \
	(*SHIFT_PERCPU_PTR(&per_cpu_var(var), per_cpu_offset(cpu)))
#define __get_cpu_var(var) \
	(*SHIFT_PERCPU_PTR(&per_cpu_var(var), my_cpu_offset))
#define __raw_get_cpu_var(var) \
	(*SHIFT_PERCPU_PTR(&per_cpu_var(var), __my_cpu_offset))

#ifndef read_percpu_var
/**
 * read_percpu_var - get a copy of this cpu's percpu simple var.
 * @var: the name of the per-cpu variable.
 *
 * Like __raw_get_cpu_var(), but doesn't provide an lvalue.  Some platforms
 * can do this more efficiently (x86/32).  Only works on fundamental types.
 */
#define read_percpu_var(var) (0, __raw_get_cpu_var(var))
#endif /* read_percpu_var */

/* Use RELOC_HIDE: some arch's SHIFT_PERCPU_PTR really want an identifier. */
#define RELOC_PERCPU(addr, off) \
	((typeof(*addr) __kernel __force *)RELOC_HIDE((addr), (off)))

/**
 * per_cpu_ptr - get a pointer to a particular cpu's allocated memory
 * @ptr: the pointer returned from alloc_percpu, or &per-cpu var
 * @cpu: the cpu whose memory you want to access
 *
 * Similar to per_cpu(), except for dynamic memory.
 * cpu_possible(@cpu) must be true.
 */
#define per_cpu_ptr(ptr, cpu) \
	RELOC_PERCPU((ptr), (per_cpu_offset(cpu)))

/**
 * __get_cpu_ptr - get a pointer to this cpu's allocated memory
 * @ptr: the pointer returned from alloc_percpu
 *
 * Similar to __get_cpu_var(), except for dynamic memory.
 */
#define __get_cpu_ptr(ptr) RELOC_PERCPU(ptr, my_cpu_offset)
#define __raw_get_cpu_ptr(ptr) RELOC_PERCPU(ptr, __my_cpu_offset)

#ifndef read_percpu_ptr
/**
 * read_percpu_ptr - deref this cpu's simple percpu pointer.
 * @ptr: the address of the per-cpu variable.
 *
 * Like read_percpu_var(), but can be used on pointers returned from
 * alloc_percpu.
 */
#define read_percpu_ptr(ptr) (0, *__raw_get_cpu_ptr(ptr))
#endif /* read_percpu_ptr */

#ifdef CONFIG_HAVE_SETUP_PER_CPU_AREA
extern void setup_per_cpu_areas(void);
#endif

#else /* ! SMP */

#define per_cpu(var, cpu)			(*((void)(cpu), &per_cpu_var(var)))
#define __get_cpu_var(var)			per_cpu_var(var)
#define __raw_get_cpu_var(var)			per_cpu_var(var)
#define read_percpu_var(var)			(0, per_cpu_var(var))
#define per_cpu_ptr(ptr, cpu)			(ptr)
#define __get_cpu_ptr(ptr)			(ptr)
#define __raw_get_cpu_ptr(ptr)			(ptr)
#define read_percpu_ptr(ptr)			(0, *(ptr))

#endif	/* SMP */

#ifndef PER_CPU_ATTRIBUTES
#define PER_CPU_ATTRIBUTES
#endif

#define DECLARE_PER_CPU(type, name) \
	extern PER_CPU_ATTRIBUTES __percpu __typeof__(type) per_cpu_var(name)

#endif /* _ASM_GENERIC_PERCPU_H_ */
