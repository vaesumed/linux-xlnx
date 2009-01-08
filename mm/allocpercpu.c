/*
 * linux/mm/allocpercpu.c
 *
 * Separated from slab.c August 11, 2006 Christoph Lameter
 * Replaced by code stolen from module.c Late 2008 Rusty Russell
 */
#include <linux/mm.h>
#include <linux/module.h>
#include <asm/sections.h>

/* Number of blocks used and allocated. */
static unsigned int pcpu_num_used, pcpu_num_allocated;
/* Size of each block.  -ve means used. */
static int *pcpu_size;

static int split_block(unsigned int i, unsigned short size)
{
	/* Reallocation required? */
	if (pcpu_num_used + 1 > pcpu_num_allocated) {
		int *new;

		new = krealloc(pcpu_size, sizeof(new[0])*pcpu_num_allocated*2,
			       GFP_KERNEL);
		if (!new)
			return 0;

		pcpu_num_allocated *= 2;
		pcpu_size = new;
	}

	/* Insert a new subblock */
	memmove(&pcpu_size[i+1], &pcpu_size[i],
		sizeof(pcpu_size[0]) * (pcpu_num_used - i));
	pcpu_num_used++;

	pcpu_size[i+1] -= size;
	pcpu_size[i] = size;
	return 1;
}

static inline unsigned int block_size(int val)
{
	if (val < 0)
		return -val;
	return val;
}

/**
 * __alloc_percpu - allocate dynamic percpu memory
 * @size: bytes to allocate
 * @align: bytes to align (< PAGE_SIZE)
 *
 * See alloc_percpu().
 */
void *__alloc_percpu(unsigned long size, unsigned long align)
{
	unsigned long extra;
	unsigned int i;
	void *ptr;

	if (WARN_ON(align > PAGE_SIZE))
		align = PAGE_SIZE;

	ptr = __per_cpu_start;
	for (i = 0; i < pcpu_num_used; ptr += block_size(pcpu_size[i]), i++) {
		/* Extra for alignment requirement. */
		extra = ALIGN((unsigned long)ptr, align) - (unsigned long)ptr;
		BUG_ON(i == 0 && extra != 0);

		if (pcpu_size[i] < 0 || pcpu_size[i] < extra + size)
			continue;

		/* Transfer extra to previous block. */
		if (pcpu_size[i-1] < 0)
			pcpu_size[i-1] -= extra;
		else
			pcpu_size[i-1] += extra;
		pcpu_size[i] -= extra;
		ptr += extra;

		/* Split block if warranted */
		if (pcpu_size[i] - size > sizeof(unsigned long))
			if (!split_block(i, size))
				return NULL;

		/* Mark allocated */
		pcpu_size[i] = -pcpu_size[i];

		/* Zero since most callers want it and it's a PITA to do. */
		for_each_possible_cpu(i)
			memset(ptr + per_cpu_offset(i), 0, size);
		return ptr;
	}

	printk(KERN_WARNING "Could not allocate %lu bytes percpu data\n",
	       size);
	return NULL;
}
EXPORT_SYMBOL_GPL(__alloc_percpu);

/**
 * free_percpu - free memory allocated with alloc_percpu.
 * @pcpuptr: the pointer returned from alloc_percpu.
 *
 * Like kfree(), the argument can be NULL.
 */
void free_percpu(void *freeme)
{
	unsigned int i;
	void *ptr = __per_cpu_start + block_size(pcpu_size[0]);

	if (!freeme)
		return;

	/* First entry is core kernel percpu data. */
	for (i = 1; i < pcpu_num_used; ptr += block_size(pcpu_size[i]), i++) {
		if (ptr == freeme) {
			pcpu_size[i] = -pcpu_size[i];
			goto free;
		}
	}
	BUG();

 free:
	/* Merge with previous? */
	if (pcpu_size[i-1] >= 0) {
		pcpu_size[i-1] += pcpu_size[i];
		pcpu_num_used--;
		memmove(&pcpu_size[i], &pcpu_size[i+1],
			(pcpu_num_used - i) * sizeof(pcpu_size[0]));
		i--;
	}
	/* Merge with next? */
	if (i+1 < pcpu_num_used && pcpu_size[i+1] >= 0) {
		pcpu_size[i] += pcpu_size[i+1];
		pcpu_num_used--;
		memmove(&pcpu_size[i+1], &pcpu_size[i+2],
			(pcpu_num_used - (i+1)) * sizeof(pcpu_size[0]));
	}
}
EXPORT_SYMBOL_GPL(free_percpu);

void __init percpu_alloc_init(void)
{
	pcpu_num_used = 2;
	pcpu_num_allocated = 2;
	pcpu_size = kmalloc(sizeof(pcpu_size[0]) * pcpu_num_allocated,
			    GFP_KERNEL);
	/* Static in-kernel percpu data (used). */
	pcpu_size[0] = -(__per_cpu_end-__per_cpu_start);
	/* Free room. */
	pcpu_size[1] = PERCPU_ENOUGH_ROOM + pcpu_size[0];
	BUG_ON(pcpu_size[1] < 0);
}

/* A heuristic based on observation.  May need to increase. */
unsigned int percpu_reserve = (sizeof(unsigned long) * 2500);

core_param(percpu, percpu_reserve, uint, 0444);

void *big_alloc_percpu(unsigned long size)
{
	unsigned int cpu;
	void **bp;

	bp = kcalloc(sizeof(void *), nr_cpu_ids, GFP_KERNEL);
	if (unlikely(!bp))
		return NULL;

	for_each_possible_cpu(cpu) {
		bp[cpu] = kzalloc_node(size, GFP_KERNEL, cpu_to_node(cpu));
		if (unlikely(!bp[cpu]))
			goto fail;
	}
	return bp;

fail:
	/* kcalloc zeroes and kfree(NULL) is safe, so this works, */
	big_free_percpu(bp);
	return NULL;
}
EXPORT_SYMBOL_GPL(big_alloc_percpu);

void big_free_percpu(const void *_bp)
{
	void *const *bp = _bp;
	unsigned int cpu;

	if (likely(bp))
		for_each_possible_cpu(cpu)
			kfree(bp[cpu]);
	kfree(bp);
}
EXPORT_SYMBOL_GPL(big_free_percpu);
