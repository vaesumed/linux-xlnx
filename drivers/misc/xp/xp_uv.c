/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Cross Partition (XP) uv-based functions.
 *
 *	Architecture specific implementation of common functions.
 *
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/device.h>
#include "xp.h"

extern struct device *xp;

static enum xp_retval
xp_register_nofault_code_uv(void)
{
	return xpSuccess;
}

static void
xp_unregister_nofault_code_uv(void)
{
}

static enum xp_retval
xp_remote_memcpy_uv(void *vdst, const void *psrc, size_t len)
{
	/* >>> this function needs fleshing out */
	return xpUnsupported;
}

static enum xp_retval
xp_register_remote_amos_uv(u64 paddr, size_t len)
{
	/* >>> this function needs fleshing out */
	return xpUnsupported;
}

static enum xp_retval
xp_unregister_remote_amos_uv(u64 paddr, size_t len)
{
	/* >>> this function needs fleshing out */
	return xpUnsupported;
}

/*
 * Allocate the required number of contiguous physical pages to hold the
 * specified number of AMOs.
 */
static u64 *
xp_alloc_amos_uv(int n_amos)
{
	size_t n_bytes = roundup(n_amos * xp_sizeof_amo, PAGE_SIZE);
	struct page *page;
	u64 *amos_page = NULL;

	page = alloc_pages_node(0, GFP_KERNEL | __GFP_ZERO | GFP_THISNODE,
				get_order(n_bytes));
	if (page)
		amos_page = (u64 *)page_address(page);

	return amos_page;
}

static void
xp_free_amos_uv(u64 *amos_page, int n_amos)
{
	size_t n_bytes = roundup(n_amos * xp_sizeof_amo, PAGE_SIZE);

	free_pages((u64)amos_page, get_order(n_bytes));
}

static enum xp_retval
xp_set_amo_uv(u64 *amo_va, int op, u64 operand, int remote)
{
	/* >>> this function needs fleshing out */
	return xpUnsupported;
}

static enum xp_retval
xp_set_amo_with_interrupt_uv(u64 *amo_va, int op, u64 operand, int remote,
			     int nasid, int phys_cpuid, int vector)
{
	/* >>> this function needs fleshing out */
	return xpUnsupported;
}

static enum xp_retval
xp_get_amo_uv(u64 *amo_va, int op, u64 *amo_value_addr)
{
	/* >>> this function needs fleshing out */
	return xpUnsupported;
}

static enum xp_retval
xp_get_partition_rsvd_page_pa_uv(u64 buf, u64 *cookie, u64 *paddr, size_t *len)
{
	/* >>> this function needs fleshing out */
	return xpUnsupported;
}

static enum xp_retval
xp_change_memprotect_uv(u64 paddr, size_t len, int request, u64 *nasid_array)
{
	/* >>> this function needs fleshing out */
	return xpUnsupported;
}

static void
xp_change_memprotect_shub_wars_1_1_uv(int request)
{
	return;
}

static void
xp_allow_IPI_ops_uv(void)
{
	/* >>> this function needs fleshing out */
	return;
}

static void
xp_disallow_IPI_ops_uv(void)
{
	/* >>> this function needs fleshing out */
	return;
}

static int
xp_cpu_to_nasid_uv(int cpuid)
{
	/* >>> this function needs fleshing out */
	return -1;
}

static int
xp_node_to_nasid_uv(int nid)
{
	/* >>> this function needs fleshing out */
	return -1;
}

enum xp_retval
xp_init_uv(void)
{
	BUG_ON(!is_uv());

	xp_partition_id = 0;	/* >>> not correct value */
	xp_region_size = 0;	/* >>> not correct value */
	xp_rtc_cycles_per_second = 0;	/* >>> not correct value */

	xp_remote_memcpy = xp_remote_memcpy_uv;

	xp_register_remote_amos = xp_register_remote_amos_uv;
	xp_unregister_remote_amos = xp_unregister_remote_amos_uv;

	xp_sizeof_amo = sizeof(u64);
	xp_alloc_amos = xp_alloc_amos_uv;
	xp_free_amos = xp_free_amos_uv;
	xp_set_amo = xp_set_amo_uv;
	xp_set_amo_with_interrupt = xp_set_amo_with_interrupt_uv;
	xp_get_amo = xp_get_amo_uv;

	xp_get_partition_rsvd_page_pa = xp_get_partition_rsvd_page_pa_uv;

	xp_change_memprotect = xp_change_memprotect_uv;
	xp_change_memprotect_shub_wars_1_1 =
	    xp_change_memprotect_shub_wars_1_1_uv;
	xp_allow_IPI_ops = xp_allow_IPI_ops_uv;
	xp_disallow_IPI_ops = xp_disallow_IPI_ops_uv;

	xp_cpu_to_nasid = xp_cpu_to_nasid_uv;
	xp_node_to_nasid = xp_node_to_nasid_uv;

	return xp_register_nofault_code_uv();
}

void
xp_exit_uv(void)
{
	BUG_ON(!is_uv());

	xp_unregister_nofault_code_uv();
}
