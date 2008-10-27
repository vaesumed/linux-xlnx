/*
 * Cpu allocator - Manage objects allocated for each processor
 *
 * (C) 2008 SGI, Christoph Lameter <cl@linux-foundation.org>
 * 	Basic implementation with allocation and free from a dedicated per
 * 	cpu area.
 *
 * The per cpu allocator allows a dynamic allocation of a piece of memory on
 * every processor. A bitmap is used to track used areas.
 * The allocator implements tight packing to reduce the cache footprint
 * and increase speed since cacheline contention is typically not a concern
 * for memory mainly used by a single cpu. Small objects will fill up gaps
 * left by larger allocations that required alignments.
 */
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/bitmap.h>
#include <asm/sections.h>
#include <linux/bootmem.h>

/*
 * Basic allocation unit. A bit map is created to track the use of each
 * UNIT_SIZE element in the cpu area.
 */
#define UNIT_TYPE int
#define UNIT_SIZE sizeof(UNIT_TYPE)

/*
 * How many units are needed for an object of a given size
 */
static int size_to_units(unsigned long size)
{
	return DIV_ROUND_UP(size, UNIT_SIZE);
}

/*
 * Lock to protect the bitmap and the meta data for the cpu allocator.
 */
static DEFINE_SPINLOCK(cpu_alloc_map_lock);
static unsigned long *cpu_alloc_map;
static int nr_units;		/* Number of available units */
static int first_free;		/* First known free unit */
static int base_percpu_in_units; /* Size of base percpu area in units */

/*
 * Mark an object as used in the cpu_alloc_map
 *
 * Must hold cpu_alloc_map_lock
 */
static void set_map(int start, int length)
{
	while (length-- > 0)
		__set_bit(start++, cpu_alloc_map);
}

/*
 * Mark an area as freed.
 *
 * Must hold cpu_alloc_map_lock
 */
static void clear_map(int start, int length)
{
	while (length-- > 0)
		__clear_bit(start++, cpu_alloc_map);
}

/*
 * Allocate an object of a certain size
 *
 * Returns a special pointer that can be used with CPU_PTR to find the
 * address of the object for a certain cpu.
 */
void *cpu_alloc(unsigned long size, gfp_t gfpflags, unsigned long align)
{
	unsigned long start;
	int units = size_to_units(size);
	void *ptr;
	int first;
	unsigned long flags;

	if (!size)
		return ZERO_SIZE_PTR;

	WARN_ON(align > PAGE_SIZE);

	if (align < UNIT_SIZE)
		align = UNIT_SIZE;

	spin_lock_irqsave(&cpu_alloc_map_lock, flags);

	first = 1;
	start = first_free;

	for ( ; ; ) {

		start = find_next_zero_bit(cpu_alloc_map, nr_units, start);
		if (start >= nr_units)
			goto out_of_memory;

		if (first)
			first_free = start;

		/*
		 * Check alignment and that there is enough space after
		 * the starting unit.
		 */
		if ((base_percpu_in_units + start) %
					(align / UNIT_SIZE) == 0 &&
			find_next_bit(cpu_alloc_map, nr_units, start + 1)
					>= start + units)
				break;
		start++;
		first = 0;
	}

	if (first)
		first_free = start + units;

	if (start + units > nr_units)
		goto out_of_memory;

	set_map(start, units);
	__count_vm_events(CPU_BYTES, units * UNIT_SIZE);

	spin_unlock_irqrestore(&cpu_alloc_map_lock, flags);

	ptr = (int *)__per_cpu_end + start;

	if (gfpflags & __GFP_ZERO) {
		int cpu;

		for_each_possible_cpu(cpu)
			memset(CPU_PTR(ptr, cpu), 0, size);
	}

	return ptr;

out_of_memory:
	spin_unlock_irqrestore(&cpu_alloc_map_lock, flags);
	return NULL;
}
EXPORT_SYMBOL(cpu_alloc);

/*
 * Free an object. The pointer must be a cpu pointer allocated
 * via cpu_alloc.
 */
void cpu_free(void *start, unsigned long size)
{
	unsigned long units = size_to_units(size);
	unsigned long index = (int *)start - (int *)__per_cpu_end;
	unsigned long flags;

	if (!start || start == ZERO_SIZE_PTR)
		return;

	if (WARN_ON(index >= nr_units))
		return;

	if (WARN_ON(!test_bit(index, cpu_alloc_map) ||
		!test_bit(index + units - 1, cpu_alloc_map)))
			return;

	spin_lock_irqsave(&cpu_alloc_map_lock, flags);

	clear_map(index, units);
	__count_vm_events(CPU_BYTES, -units * UNIT_SIZE);

	if (index < first_free)
		first_free = index;

	spin_unlock_irqrestore(&cpu_alloc_map_lock, flags);
}
EXPORT_SYMBOL(cpu_free);


void __init cpu_alloc_init(void)
{
	base_percpu_in_units = (__per_cpu_end - __per_cpu_start
					+ UNIT_SIZE - 1) / UNIT_SIZE;

	nr_units = PERCPU_AREA_SIZE / UNIT_SIZE - base_percpu_in_units;

	cpu_alloc_map = alloc_bootmem(BITS_TO_LONGS(nr_units));
}

