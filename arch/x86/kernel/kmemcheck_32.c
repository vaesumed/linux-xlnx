/**
 * kmemcheck - a heavyweight memory checker for the linux kernel
 * Copyright (C) 2007, 2008  Vegard Nossum <vegardno@ifi.uio.no>
 * (With a lot of help from Ingo Molnar and Pekka Enberg.)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2) as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/page-flags.h>
#include <linux/stacktrace.h>
#include <linux/timer.h>

#include <asm/cacheflush.h>
#include <asm/kdebug.h>
#include <asm/kmemcheck.h>
#include <asm/pgtable.h>
#include <asm/string.h>
#include <asm/tlbflush.h>

enum shadow {
	SHADOW_UNALLOCATED,
	SHADOW_UNINITIALIZED,
	SHADOW_INITIALIZED,
	SHADOW_FREED,
};

enum kmemcheck_error_type {
	ERROR_INVALID_ACCESS,
	ERROR_BUG,
};

struct kmemcheck_error {
	enum kmemcheck_error_type type;

	union {
		/* ERROR_INVALID_ACCESS */
		struct {
			/* Kind of access that caused the error */
			enum shadow		state;
			/* Address and size of the erroneous read */
			uint32_t		address;
			unsigned int		size;
		};
	};

	struct pt_regs		regs;
	struct stack_trace	trace;
	unsigned long		trace_entries[32];
};

/*
 * Create a ring queue of errors to output. We can't call printk() directly
 * from the kmemcheck traps, since this may call the console drivers and
 * result in a recursive fault.
 */
static struct kmemcheck_error error_fifo[32];
static unsigned int error_count;
static unsigned int error_rd;
static unsigned int error_wr;

static struct timer_list kmemcheck_timer;

static struct kmemcheck_error *
error_next_wr(void)
{
	struct kmemcheck_error *e;

	if (error_count == ARRAY_SIZE(error_fifo))
		return NULL;

	e = &error_fifo[error_wr];
	if (++error_wr == ARRAY_SIZE(error_fifo))
		error_wr = 0;
	++error_count;
	return e;
}

static struct kmemcheck_error *
error_next_rd(void)
{
	struct kmemcheck_error *e;

	if (error_count == 0)
		return NULL;

	e = &error_fifo[error_rd];
	if (++error_rd == ARRAY_SIZE(error_fifo))
		error_rd = 0;
	--error_count;
	return e;
}

/*
 * Save the context of an error.
 */
static void
error_save(enum shadow state, uint32_t address, unsigned int size,
	struct pt_regs *regs)
{
	static uint32_t prev_ip;

	struct kmemcheck_error *e;

	/* Don't report several adjacent errors from the same EIP. */
	if (regs->ip == prev_ip)
		return;
	prev_ip = regs->ip;

	e = error_next_wr();
	if (!e)
		return;

	e->type = ERROR_INVALID_ACCESS;

	e->state = state;
	e->address = address;
	e->size = size;

	/* Save regs */
	memcpy(&e->regs, regs, sizeof(*regs));

	/* Save stack trace */
	e->trace.nr_entries = 0;
	e->trace.entries = e->trace_entries;
	e->trace.max_entries = ARRAY_SIZE(e->trace_entries);
	e->trace.skip = 1;
	save_stack_trace(&e->trace);
}

/*
 * Save the context of a kmemcheck bug.
 */
static void
error_save_bug(struct pt_regs *regs)
{
	struct kmemcheck_error *e;

	e = error_next_wr();
	if (!e)
		return;

	e->type = ERROR_BUG;

	memcpy(&e->regs, regs, sizeof(*regs));

	e->trace.nr_entries = 0;
	e->trace.entries = e->trace_entries;
	e->trace.max_entries = ARRAY_SIZE(e->trace_entries);
	e->trace.skip = 1;
	save_stack_trace(&e->trace);
}

static void
error_recall(void)
{
	static const char *desc[] = {
		[SHADOW_UNALLOCATED]	= "unallocated",
		[SHADOW_UNINITIALIZED]	= "uninitialized",
		[SHADOW_INITIALIZED]	= "initialized",
		[SHADOW_FREED]		= "freed",
	};

	struct kmemcheck_error *e;

	e = error_next_rd();
	if (!e)
		return;

	switch (e->type) {
	case ERROR_INVALID_ACCESS:
		printk(KERN_ERR  "kmemcheck: Caught %d-bit read "
			"from %s memory (%08x)\n",
			e->size, desc[e->state], e->address);
		break;
	case ERROR_BUG:
		printk(KERN_EMERG "kmemcheck: Fatal error\n");
		break;
	}

	__show_regs(&e->regs, 1);
	print_stack_trace(&e->trace, 0);
}

static void
do_wakeup(unsigned long data)
{
	while (error_count > 0)
		error_recall();
	mod_timer(&kmemcheck_timer, kmemcheck_timer.expires + HZ);
}

void __init
kmemcheck_init(void)
{
	printk(KERN_INFO "kmemcheck: \"Bugs, beware!\"\n");

#ifdef CONFIG_SMP
	/* Limit SMP to use a single CPU. We rely on the fact that this code
	 * runs before SMP is set up. */
	if (setup_max_cpus > 1) {
		printk(KERN_INFO
			"kmemcheck: Limiting number of CPUs to 1.\n");
		setup_max_cpus = 1;
	}
#endif

	setup_timer(&kmemcheck_timer, &do_wakeup, 0);
	mod_timer(&kmemcheck_timer, jiffies + HZ);
}

#ifdef CONFIG_KMEMCHECK_ENABLED_BY_DEFAULT
int kmemcheck_enabled = 1;
#else
int kmemcheck_enabled = 0;
#endif

static int __init
param_kmemcheck(char *str)
{
	if (!str)
		return -EINVAL;

	switch (str[0]) {
	case '0':
		kmemcheck_enabled = 0;
		return 0;
	case '1':
		kmemcheck_enabled = 1;
		return 0;
	}

	return -EINVAL;
}

early_param("kmemcheck", param_kmemcheck);

/*
 * Return the shadow address for the given address. Returns NULL if the
 * address is not tracked.
 */
static void *
address_get_shadow(unsigned long address)
{
	struct page *page;
	struct page *head;

	if (address < PAGE_OFFSET)
		return NULL;
	page = virt_to_page(address);
	if (!page)
		return NULL;
	head = compound_head(page);
	if (!PageHead(head))
		return NULL;
	if (!PageSlab(head))
		return NULL;
	if (!PageTracked(head))
		return NULL;

	return (void *) address + (PAGE_SIZE << (compound_order(head) - 1));
}

static int
show_addr(uint32_t addr)
{
	pte_t *pte;
	int level;

	if (!address_get_shadow(addr))
		return 0;

	pte = lookup_address(addr, &level);
	BUG_ON(!pte);
	BUG_ON(level != PG_LEVEL_4K);

	set_pte(pte, __pte(pte_val(*pte) | _PAGE_PRESENT));
	__flush_tlb_one(addr);
	return 1;
}

/*
 * In case there's something seriously wrong with kmemcheck (like a recursive
 * or looping page fault), we should disable tracking for the page as a last
 * attempt to not hang the machine.
 */
static void
emergency_show_addr(uint32_t address)
{
	pte_t *pte;
	int level;

	pte = lookup_address(address, &level);
	if (!pte)
		return;
	if (level != PG_LEVEL_4K)
		return;

	/* Don't change pages that weren't hidden in the first place -- they
	 * aren't ours to modify. */
	if (!(pte_val(*pte) & _PAGE_HIDDEN))
		return;

	set_pte(pte, __pte(pte_val(*pte) | _PAGE_PRESENT));
	__flush_tlb_one(address);
}

static int
hide_addr(uint32_t addr)
{
	pte_t *pte;
	int level;

	if (!address_get_shadow(addr))
		return 0;

	pte = lookup_address(addr, &level);
	BUG_ON(!pte);
	BUG_ON(level != PG_LEVEL_4K);

	set_pte(pte, __pte(pte_val(*pte) & ~_PAGE_PRESENT));
	__flush_tlb_one(addr);
	return 1;
}

struct kmemcheck_context {
	bool busy;
	int balance;

	uint32_t addr1;
	uint32_t addr2;
	uint32_t flags;
};

DEFINE_PER_CPU(struct kmemcheck_context, kmemcheck_context);

/*
 * Called from the #PF handler.
 */
void
kmemcheck_show(struct pt_regs *regs)
{
	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);
	int n;

	BUG_ON(!irqs_disabled());

	if (unlikely(data->balance != 0)) {
		emergency_show_addr(data->addr1);
		emergency_show_addr(data->addr2);
		error_save_bug(regs);
		data->balance = 0;
		return;
	}

	n = 0;
	n += show_addr(data->addr1);
	n += show_addr(data->addr2);

	/* None of the addresses actually belonged to kmemcheck. Note that
	 * this is not an error. */
	if (n == 0)
		return;

	++data->balance;

	/*
	 * The IF needs to be cleared as well, so that the faulting
	 * instruction can run "uninterrupted". Otherwise, we might take
	 * an interrupt and start executing that before we've had a chance
	 * to hide the page again.
	 *
	 * NOTE: In the rare case of multiple faults, we must not override
	 * the original flags:
	 */
	if (!(regs->flags & TF_MASK))
		data->flags = regs->flags;

	regs->flags |= TF_MASK;
	regs->flags &= ~IF_MASK;
}

/*
 * Called from the #DB handler.
 */
void
kmemcheck_hide(struct pt_regs *regs)
{
	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);

	BUG_ON(!irqs_disabled());

	--data->balance;
	if (unlikely(data->balance != 0)) {
		emergency_show_addr(data->addr1);
		emergency_show_addr(data->addr2);
		error_save_bug(regs);
		data->addr1 = 0;
		data->addr2 = 0;
		data->balance = 0;

		if (!(data->flags & TF_MASK))
			regs->flags &= ~TF_MASK;
		if (data->flags & IF_MASK)
			regs->flags |= IF_MASK;
		return;
	}

	if (kmemcheck_enabled) {
		hide_addr(data->addr1);
		hide_addr(data->addr2);
	}

	data->addr1 = 0;
	data->addr2 = 0;

	if (!(data->flags & TF_MASK))
		regs->flags &= ~TF_MASK;
	if (data->flags & IF_MASK)
		regs->flags |= IF_MASK;
}

void
kmemcheck_prepare(struct pt_regs *regs)
{
	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);

	/*
	 * Detect and handle recursive page faults. This can happen, for
	 * example, when we have an instruction that will cause a page fault
	 * for both a tracked kernel page and a userspace page in the same
	 * instruction.
	 */
	if (data->balance > 0) {
		/*
		 * We can have multi-address faults from accesses like:
		 *
		 *          rep movsb %ds:(%esi),%es:(%edi)
		 *
		 * So in this case, we hide the current in-progress fault
		 * and handle it after the second fault has been handled.
		 */
		kmemcheck_hide(regs);
	}
}

void
kmemcheck_show_pages(struct page *p, unsigned int n)
{
	unsigned int i;
	struct page *head;

	head = compound_head(p);
	BUG_ON(!PageHead(head));

	ClearPageTracked(head);

	for (i = 0; i < n; ++i) {
		unsigned long address;
		pte_t *pte;
		int level;

		address = (unsigned long) page_address(&p[i]);
		pte = lookup_address(address, &level);
		BUG_ON(!pte);
		BUG_ON(level != PG_LEVEL_4K);

		set_pte(pte, __pte(pte_val(*pte) | _PAGE_PRESENT));
		set_pte(pte, __pte(pte_val(*pte) & ~_PAGE_HIDDEN));
		__flush_tlb_one(address);
	}
}

void
kmemcheck_hide_pages(struct page *p, unsigned int n)
{
	unsigned int i;
	struct page *head;

	head = compound_head(p);
	BUG_ON(!PageHead(head));

	SetPageTracked(head);

	for (i = 0; i < n; ++i) {
		unsigned long address;
		pte_t *pte;
		int level;

		address = (unsigned long) page_address(&p[i]);
		pte = lookup_address(address, &level);
		BUG_ON(!pte);
		BUG_ON(level != PG_LEVEL_4K);

		set_pte(pte, __pte(pte_val(*pte) & ~_PAGE_PRESENT));
		set_pte(pte, __pte(pte_val(*pte) | _PAGE_HIDDEN));
		__flush_tlb_one(address);
	}
}

static void
mark_shadow(void *address, unsigned int n, enum shadow status)
{
	void *shadow;

	shadow = address_get_shadow((unsigned long) address);
	if (!shadow)
		return;
	__memset(shadow, status, n);
}

void
kmemcheck_mark_unallocated(void *address, unsigned int n)
{
	mark_shadow(address, n, SHADOW_UNALLOCATED);
}

void
kmemcheck_mark_uninitialized(void *address, unsigned int n)
{
	mark_shadow(address, n, SHADOW_UNINITIALIZED);
}

/*
 * Fill the shadow memory of the given address such that the memory at that
 * address is marked as being initialized.
 */
void
kmemcheck_mark_initialized(void *address, unsigned int n)
{
	mark_shadow(address, n, SHADOW_INITIALIZED);
}

void
kmemcheck_mark_freed(void *address, unsigned int n)
{
	mark_shadow(address, n, SHADOW_FREED);
}

void
kmemcheck_mark_unallocated_pages(struct page *p, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; ++i)
		kmemcheck_mark_unallocated(page_address(&p[i]), PAGE_SIZE);
}

void
kmemcheck_mark_uninitialized_pages(struct page *p, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; ++i)
		kmemcheck_mark_uninitialized(page_address(&p[i]), PAGE_SIZE);
}

static bool
opcode_is_prefix(uint8_t b)
{
	return
		/* Group 1 */
		b == 0xf0 || b == 0xf2 || b == 0xf3
		/* Group 2 */
		|| b == 0x2e || b == 0x36 || b == 0x3e || b == 0x26
		|| b == 0x64 || b == 0x65 || b == 0x2e || b == 0x3e
		/* Group 3 */
		|| b == 0x66
		/* Group 4 */
		|| b == 0x67;
}

/* This is a VERY crude opcode decoder. We only need to find the size of the
 * load/store that caused our #PF and this should work for all the opcodes
 * that we care about. Moreover, the ones who invented this instruction set
 * should be shot. */
static unsigned int
opcode_get_size(const uint8_t *op)
{
	/* Default operand size */
	int operand_size_override = 32;

	/* prefixes */
	for (; opcode_is_prefix(*op); ++op) {
		if (*op == 0x66)
			operand_size_override = 16;
	}

	/* escape opcode */
	if (*op == 0x0f) {
		++op;

		if (*op == 0xb6)
			return operand_size_override >> 1;
		if (*op == 0xb7)
			return 16;
	}

	return (*op & 1) ? operand_size_override : 8;
}

static const uint8_t *
opcode_get_primary(const uint8_t *op)
{
	/* skip prefixes */
	for (; opcode_is_prefix(*op); ++op);
	return op;
}

static inline enum shadow
test(void *shadow, unsigned int size)
{
	uint8_t *x;

	x = shadow;

#ifdef CONFIG_KMEMCHECK_PARTIAL_OK
	/*
	 * Make sure _some_ bytes are initialized. Gcc frequently generates
	 * code to access neighboring bytes.
	 */
	switch (size) {
	case 32:
		if (x[3] == SHADOW_INITIALIZED)
			return x[3];
		if (x[2] == SHADOW_INITIALIZED)
			return x[2];
	case 16:
		if (x[1] == SHADOW_INITIALIZED)
			return x[1];
	case 8:
		if (x[0] == SHADOW_INITIALIZED)
			return x[0];
	}
#else
	switch (size) {
	case 32:
		if (x[3] != SHADOW_INITIALIZED)
			return x[3];
		if (x[2] != SHADOW_INITIALIZED)
			return x[2];
	case 16:
		if (x[1] != SHADOW_INITIALIZED)
			return x[1];
	case 8:
		if (x[0] != SHADOW_INITIALIZED)
			return x[0];
	}
#endif

	return x[0];
}

static inline void
set(void *shadow, unsigned int size)
{
	uint8_t *x;

	x = shadow;

	switch (size) {
	case 32:
		x[3] = SHADOW_INITIALIZED;
		x[2] = SHADOW_INITIALIZED;
	case 16:
		x[1] = SHADOW_INITIALIZED;
	case 8:
		x[0] = SHADOW_INITIALIZED;
	}

	return;
}

static void
kmemcheck_read(struct pt_regs *regs, uint32_t address, unsigned int size)
{
	void *shadow;
	enum shadow status;

	shadow = address_get_shadow(address);
	if (!shadow)
		return;

	status = test(shadow, size);
	if (status == SHADOW_INITIALIZED)
		return;

	/* Don't warn about it again. */
	set(shadow, size);

	error_save(status, address, size, regs);
}

static void
kmemcheck_write(struct pt_regs *regs, uint32_t address, unsigned int size)
{
	void *shadow;

	shadow = address_get_shadow(address);
	if (!shadow)
		return;
	set(shadow, size);
}

void
kmemcheck_access(struct pt_regs *regs,
	unsigned long fallback_address, enum kmemcheck_method fallback_method)
{
	const uint8_t *insn;
	const uint8_t *insn_primary;
	unsigned int size;

	struct kmemcheck_context *data = &__get_cpu_var(kmemcheck_context);

	/* Recursive fault -- ouch. */
	if (data->busy) {
		emergency_show_addr(fallback_address);
		error_save_bug(regs);
		return;
	}

	data->busy = true;

	insn = (const uint8_t *) regs->ip;
	insn_primary = opcode_get_primary(insn);

	size = opcode_get_size(insn);

	switch (insn_primary[0]) {
#ifdef CONFIG_KMEMCHECK_BITOPS_OK
		/* AND, OR, XOR */
		/*
		 * Unfortunately, these instructions have to be excluded from
		 * our regular checking since they access only some (and not
		 * all) bits. This clears out "bogus" bitfield-access warnings.
		 */
	case 0x80:
	case 0x81:
	case 0x82:
	case 0x83:
		switch ((insn_primary[1] >> 3) & 7) {
			/* OR */
		case 1:
			/* AND */
		case 4:
			/* XOR */
		case 6:
			kmemcheck_write(regs, fallback_address, size);
			data->addr1 = fallback_address;
			data->addr2 = 0;
			data->busy = false;
			return;

			/* ADD */
		case 0:
			/* ADC */
		case 2:
			/* SBB */
		case 3:
			/* SUB */
		case 5:
			/* CMP */
		case 7:
			break;
		}
		break;
#endif

		/* MOVS, MOVSB, MOVSW, MOVSD */
	case 0xa4:
	case 0xa5:
		/* These instructions are special because they take two
		 * addresses, but we only get one page fault. */
		kmemcheck_read(regs, regs->si, size);
		kmemcheck_write(regs, regs->di, size);
		data->addr1 = regs->si;
		data->addr2 = regs->di;
		data->busy = false;
		return;

		/* CMPS, CMPSB, CMPSW, CMPSD */
	case 0xa6:
	case 0xa7:
		kmemcheck_read(regs, regs->si, size);
		kmemcheck_read(regs, regs->di, size);
		data->addr1 = regs->si;
		data->addr2 = regs->di;
		data->busy = false;
		return;
	}

	/* If the opcode isn't special in any way, we use the data from the
	 * page fault handler to determine the address and type of memory
	 * access. */
	switch (fallback_method) {
	case KMEMCHECK_READ:
		kmemcheck_read(regs, fallback_address, size);
		data->addr1 = fallback_address;
		data->addr2 = 0;
		data->busy = false;
		return;
	case KMEMCHECK_WRITE:
		kmemcheck_write(regs, fallback_address, size);
		data->addr1 = fallback_address;
		data->addr2 = 0;
		data->busy = false;
		return;
	}
}

/*
 * A faster implementation of memset() when tracking is enabled where the
 * whole memory area is within a single page.
 */
static void
memset_one_page(unsigned long s, int c, size_t n)
{
	void *x;
	unsigned long flags;

	x = address_get_shadow(s);
	if (!x) {
		/* The page isn't being tracked. */
		__memset((void *) s, c, n);
		return;
	}

	/* While we are not guarding the page in question, nobody else
	 * should be able to change them. */
	local_irq_save(flags);

	show_addr(s);
	__memset((void *) s, c, n);
	__memset((void *) x, SHADOW_INITIALIZED, n);
	if (kmemcheck_enabled)
		hide_addr(s);

	local_irq_restore(flags);
}

/*
 * A faster implementation of memset() when tracking is enabled. We cannot
 * assume that all pages within the range are tracked, so copying has to be
 * split into page-sized (or smaller, for the ends) chunks.
 */
void *
kmemcheck_memset(unsigned long s, int c, size_t n)
{
	unsigned long start_page, start_offset;
	unsigned long end_page, end_offset;
	unsigned long i;

	if (!n)
		return s;

	if (!slab_is_available()) {
		__memset((void *) s, c, n);
		return s;
	}

	start_page = s & PAGE_MASK;
	end_page = (s + n) & PAGE_MASK;

	if (start_page == end_page) {
		/* The entire area is within the same page. Good, we only
		 * need one memset(). */
		memset_one_page(s, c, n);
		return s;
	}

	start_offset = s & ~PAGE_MASK;
	end_offset = (s + n) & ~PAGE_MASK;

	/* Clear the head, body, and tail of the memory area. */
	if (start_offset < PAGE_SIZE)
		memset_one_page(s, c, PAGE_SIZE - start_offset);
	for (i = start_page + PAGE_SIZE; i < end_page; i += PAGE_SIZE)
		memset_one_page(i, c, PAGE_SIZE);
	if (end_offset > 0)
		memset_one_page(end_page, c, end_offset);

	return s;
}

EXPORT_SYMBOL(kmemcheck_memset);
