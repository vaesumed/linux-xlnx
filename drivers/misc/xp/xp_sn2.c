/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Cross Partition (XP) sn2-based functions.
 *
 *	Architecture specific implementation of common functions.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <asm/uncached.h>
#include <asm/sn/intr.h>
#include <asm/sn/bte.h>
#include <asm/sn/clksupport.h>
#include <asm/sn/mspec.h>
#include <asm/sn/sn_sal.h>
#include "xp.h"

/*
 * Register a nofault code region which performs a cross-partition PIO read.
 * If the PIO read times out, the MCA handler will consume the error and
 * return to a kernel-provided instruction to indicate an error. This PIO read
 * exists because it is guaranteed to timeout if the destination is down
 * (AMO operations do not timeout on at least some CPUs on Shubs <= v1.2,
 * which unfortunately we have to work around).
 */
static enum xp_retval
xp_register_nofault_code_sn2(void)
{
	int ret;
	u64 func_addr;
	u64 err_func_addr;

	func_addr = *(u64 *)xp_nofault_PIOR;
	err_func_addr = *(u64 *)xp_error_PIOR;
	ret = sn_register_nofault_code(func_addr, err_func_addr, err_func_addr,
				       1, 1);
	if (ret != 0) {
		dev_err(xp, "can't register nofault code, error=%d\n", ret);
		return xpSalError;
	}
	/*
	 * Setup the nofault PIO read target. (There is no special reason why
	 * SH_IPI_ACCESS was selected.)
	 */
	if (is_shub1())
		xp_nofault_PIOR_target = SH1_IPI_ACCESS;
	else if (is_shub2())
		xp_nofault_PIOR_target = SH2_IPI_ACCESS0;

	return xpSuccess;
}

void
xp_unregister_nofault_code_sn2(void)
{
	u64 func_addr = *(u64 *)xp_nofault_PIOR;
	u64 err_func_addr = *(u64 *)xp_error_PIOR;

	/* unregister the PIO read nofault code region */
	(void)sn_register_nofault_code(func_addr, err_func_addr,
				       err_func_addr, 1, 0);
}

/*
 * Wrapper for bte_copy().
 *
 *	vdst - virtual address of the destination of the transfer.
 * 	psrc - physical address of the source of the transfer.
 *	len - number of bytes to transfer from source to destination.
 *
 * Note: xp_remote_memcpy_sn2() should never be called while holding a spinlock.
 */
static enum xp_retval
xp_remote_memcpy_sn2(void *vdst, const void *psrc, size_t len)
{
	bte_result_t ret;
	u64 pdst = ia64_tpa(vdst);
	/* >>> What are the rules governing the src and dst addresses passed in?
	 * >>> Currently we're assuming that dst is a virtual address and src
	 * >>> is a physical address, is this appropriate? Can we allow them to
	 * >>> be whatever and we make the change here without damaging the
	 * >>> addresses?
	 */

	/*
	 * Ensure that the physically mapped memory is contiguous.
	 *
	 * We do this by ensuring that the memory is from region 7 only.
	 * If the need should arise to use memory from one of the other
	 * regions, then modify the BUG_ON() statement to ensure that the
	 * memory from that region is always physically contiguous.
	 */
	BUG_ON(REGION_NUMBER(vdst) != RGN_KERNEL);

	ret = bte_copy((u64)psrc, pdst, len, (BTE_NOTIFY | BTE_WACQUIRE), NULL);
	if (ret == BTE_SUCCESS)
		return xpSuccess;

	if (is_shub2())
		dev_err(xp, "bte_copy() on shub2 failed, error=0x%x\n", ret);
	else
		dev_err(xp, "bte_copy() failed, error=%d\n", ret);

	return xpBteCopyError;
}

/*
 * Register the remote partition's AMOs with SAL so it can handle and cleanup
 * errors within that address range should the remote partition go down. We
 * don't unregister this range because it is difficult to tell when outstanding
 * writes to the remote partition are finished and thus when it is safe to
 * unregister. This should not result in wasted space in the SAL xp_addr_region
 * table because we should get the same page for remote_amos_page_pa after
 * module reloads and system reboots.
 */
static enum xp_retval
xp_register_remote_amos_sn2(u64 paddr, size_t len)
{
	enum xp_retval ret = xpSuccess;

	if (sn_register_xp_addr_region(paddr, len, 1) < 0)
		ret = xpSalError;
	return ret;
}

static enum xp_retval
xp_unregister_remote_amos_sn2(u64 paddr, size_t len)
{
	return xpSuccess;	/* we don't unregister AMOs on sn2 */
}

/*
 * Allocate the required number of contiguous physical pages to hold the
 * specified number of AMOs.
 */
static u64 *
xp_alloc_amos_sn2(int n_amos)
{
	int n_pages = DIV_ROUND_UP(n_amos * xp_sizeof_amo, PAGE_SIZE);

	return (u64 *)TO_AMO(uncached_alloc_page(0, n_pages));
}

static void
xp_free_amos_sn2(u64 *amos_page, int n_amos)
{
	int n_pages = DIV_ROUND_UP(n_amos * xp_sizeof_amo, PAGE_SIZE);

	uncached_free_page(__IA64_UNCACHED_OFFSET | TO_PHYS((u64)amos_page),
			   n_pages);
}

static enum xp_retval
xp_set_amo_sn2(u64 *amo_va, int op, u64 operand, int remote)
{
	unsigned long irq_flags = irq_flags;	/* eliminate compiler warning */
	int ret = xpSuccess;
	/* >>> eliminate remote arg and xp_nofault_PIOR() call */

	if (op == XP_AMO_AND)
		op = FETCHOP_AND;
	else if (op == XP_AMO_OR)
		op = FETCHOP_OR;
	else
		BUG();

	if (remote)
		local_irq_save(irq_flags);

	FETCHOP_STORE_OP(TO_AMO((u64)amo_va), op, operand);

	if (remote) {
		/*
		 * We must always use the nofault function regardless of
		 * whether we are on a Shub 1.1 system or a Shub 1.2 slice
		 * 0xc processor. If we didn't, we'd never know that the other
		 * partition is down and would keep sending IPIs and AMOs to
		 * it until the heartbeat times out.
		 */
		if (xp_nofault_PIOR((u64 *)GLOBAL_MMR_ADDR(NASID_GET(amo_va),
							xp_nofault_PIOR_target))
		    != 0) {
			ret = xpPioReadError;
		}
		local_irq_restore(irq_flags);
	}

	return ret;
}

static enum xp_retval
xp_set_amo_with_interrupt_sn2(u64 *amo_va, int op, u64 operand, int remote,
			      int nasid, int phys_cpuid, int vector)
{
	unsigned long irq_flags = irq_flags;	/* eliminate compiler warning */
	int ret = xpSuccess;

	if (op == XP_AMO_AND)
		op = FETCHOP_AND;
	else if (op == XP_AMO_OR)
		op = FETCHOP_OR;
	else
		BUG();

	if (remote)
		local_irq_save(irq_flags);

	FETCHOP_STORE_OP(TO_AMO((u64)amo_va), op, operand);
	sn_send_IPI_phys(nasid, phys_cpuid, vector, 0);

	if (remote) {
		/*
		 * We must always use the nofault function regardless of
		 * whether we are on a Shub 1.1 system or a Shub 1.2 slice
		 * 0xc processor. If we didn't, we'd never know that the other
		 * partition is down and would keep sending IPIs and AMOs to
		 * it until the heartbeat times out.
		 */
		if (xp_nofault_PIOR((u64 *)GLOBAL_MMR_ADDR(NASID_GET(amo_va),
							xp_nofault_PIOR_target))
		    != 0) {
			ret = xpPioReadError;
		}
		local_irq_restore(irq_flags);
	}

	return ret;
}

static enum xp_retval
xp_get_amo_sn2(u64 *amo_va, int op, u64 *amo_value_addr)
{
	u64 amo_value;

	if (op == XP_AMO_LOAD)
		op = FETCHOP_LOAD;
	else if (op == XP_AMO_CLEAR)
		op = FETCHOP_CLEAR;
	else
		BUG();

	amo_value = FETCHOP_LOAD_OP(TO_AMO((u64)amo_va), op);
	if (amo_value_addr != NULL)
		*amo_value_addr = amo_value;
	return xpSuccess;
}

static enum xp_retval
xp_get_partition_rsvd_page_pa_sn2(u64 buf, u64 *cookie, u64 *paddr, size_t *len)
{
	s64 status;
	enum xp_retval ret;

	status = sn_partition_reserved_page_pa(buf, cookie, paddr, len);
	if (status == SALRET_OK)
		ret = xpSuccess;
	else if (status == SALRET_MORE_PASSES)
		ret = xpNeedMoreInfo;
	else
		ret = xpSalError;

	return ret;
}

static enum xp_retval
xp_change_memprotect_sn2(u64 paddr, size_t len, int request, u64 *nasid_array)
{
	u64 perms;
	int status;

	/*
	 * Since the BIST collides with memory operations on
	 * SHUB 1.1, sn_change_memprotect() cannot be used. See
	 * xp_change_memprotect_shub_wars_1_1() for WAR.
	 */
	if (enable_shub_wars_1_1())
		return xpSuccess;

	if (request == XP_MEMPROT_DISALLOW_ALL)
		perms = SN_MEMPROT_ACCESS_CLASS_0;
	else if (request == XP_MEMPROT_ALLOW_CPU_AMO)
		perms = SN_MEMPROT_ACCESS_CLASS_1;
	else if (request == XP_MEMPROT_ALLOW_CPU_MEM)
		perms = SN_MEMPROT_ACCESS_CLASS_2;
	else
		BUG();

	status = sn_change_memprotect(paddr, len, perms, nasid_array);
	return (status == 0) ? xpSuccess : xpSalError;
}

/* original protection values for each node */
static u64 xpc_prot_vec[MAX_NUMNODES];

/*
 * Change protections to allow/disallow all operations on Shub 1.1 systems.
 */
static void
xp_change_memprotect_shub_wars_1_1_sn2(int request)
{
	int node;
	int nasid;

	/*
	 * Since the BIST collides with memory operations on SHUB 1.1
	 * sn_change_memprotect() cannot be used.
	 */
	if (!enable_shub_wars_1_1())
		return;

	if (request == XP_MEMPROT_ALLOW_ALL) {
		for_each_online_node(node) {
			nasid = cnodeid_to_nasid(node);
			/* save current protection values */
			xpc_prot_vec[node] =
			    (u64)HUB_L((u64 *)GLOBAL_MMR_ADDR(nasid,
						  SH1_MD_DQLP_MMR_DIR_PRIVEC0));
			/* open up everything */
			HUB_S((u64 *)GLOBAL_MMR_ADDR(nasid,
						   SH1_MD_DQLP_MMR_DIR_PRIVEC0),
			      -1UL);
			HUB_S((u64 *)
			      GLOBAL_MMR_ADDR(nasid,
					      SH1_MD_DQRP_MMR_DIR_PRIVEC0),
			      -1UL);
		}
	} else if (request == XP_MEMPROT_DISALLOW_ALL) {
		for_each_online_node(node) {
			nasid = cnodeid_to_nasid(node);
			/* restore original protection values */
			HUB_S((u64 *)GLOBAL_MMR_ADDR(nasid,
						   SH1_MD_DQLP_MMR_DIR_PRIVEC0),
			      xpc_prot_vec[node]);
			HUB_S((u64 *)
			      GLOBAL_MMR_ADDR(nasid,
					      SH1_MD_DQRP_MMR_DIR_PRIVEC0),
			      xpc_prot_vec[node]);
		}
	} else {
		BUG();
	}
}

/* SH_IPI_ACCESS shub register value on startup */
static u64 xpc_sh1_IPI_access;
static u64 xpc_sh2_IPI_access0;
static u64 xpc_sh2_IPI_access1;
static u64 xpc_sh2_IPI_access2;
static u64 xpc_sh2_IPI_access3;

/*
 * Change protections to allow IPI operations.
 */
static void
xp_allow_IPI_ops_sn2(void)
{
	int node;
	int nasid;

	/*  >>> The following should get moved into SAL. */
	if (is_shub2()) {
		xpc_sh2_IPI_access0 =
		    (u64)HUB_L((u64 *)LOCAL_MMR_ADDR(SH2_IPI_ACCESS0));
		xpc_sh2_IPI_access1 =
		    (u64)HUB_L((u64 *)LOCAL_MMR_ADDR(SH2_IPI_ACCESS1));
		xpc_sh2_IPI_access2 =
		    (u64)HUB_L((u64 *)LOCAL_MMR_ADDR(SH2_IPI_ACCESS2));
		xpc_sh2_IPI_access3 =
		    (u64)HUB_L((u64 *)LOCAL_MMR_ADDR(SH2_IPI_ACCESS3));

		for_each_online_node(node) {
			nasid = cnodeid_to_nasid(node);
			HUB_S((u64 *)GLOBAL_MMR_ADDR(nasid, SH2_IPI_ACCESS0),
			      -1UL);
			HUB_S((u64 *)GLOBAL_MMR_ADDR(nasid, SH2_IPI_ACCESS1),
			      -1UL);
			HUB_S((u64 *)GLOBAL_MMR_ADDR(nasid, SH2_IPI_ACCESS2),
			      -1UL);
			HUB_S((u64 *)GLOBAL_MMR_ADDR(nasid, SH2_IPI_ACCESS3),
			      -1UL);
		}
	} else {
		xpc_sh1_IPI_access =
		    (u64)HUB_L((u64 *)LOCAL_MMR_ADDR(SH1_IPI_ACCESS));

		for_each_online_node(node) {
			nasid = cnodeid_to_nasid(node);
			HUB_S((u64 *)GLOBAL_MMR_ADDR(nasid, SH1_IPI_ACCESS),
			      -1UL);
		}
	}
}

/*
 * Restrict protections to disallow IPI operations.
 */
static void
xp_disallow_IPI_ops_sn2(void)
{
	int node;
	int nasid;

	/*  >>> The following should get moved into SAL. */
	if (is_shub2()) {
		for_each_online_node(node) {
			nasid = cnodeid_to_nasid(node);
			HUB_S((u64 *)GLOBAL_MMR_ADDR(nasid, SH2_IPI_ACCESS0),
			      xpc_sh2_IPI_access0);
			HUB_S((u64 *)GLOBAL_MMR_ADDR(nasid, SH2_IPI_ACCESS1),
			      xpc_sh2_IPI_access1);
			HUB_S((u64 *)GLOBAL_MMR_ADDR(nasid, SH2_IPI_ACCESS2),
			      xpc_sh2_IPI_access2);
			HUB_S((u64 *)GLOBAL_MMR_ADDR(nasid, SH2_IPI_ACCESS3),
			      xpc_sh2_IPI_access3);
		}
	} else {
		for_each_online_node(node) {
			nasid = cnodeid_to_nasid(node);
			HUB_S((u64 *)GLOBAL_MMR_ADDR(nasid, SH1_IPI_ACCESS),
			      xpc_sh1_IPI_access);
		}
	}
}

static int
xp_cpu_to_nasid_sn2(int cpuid)
{
	return cpuid_to_nasid(cpuid);
}

static int
xp_node_to_nasid_sn2(int nid)
{
	return cnodeid_to_nasid(nid);
}

enum xp_retval
xp_init_sn2(void)
{
	BUG_ON(!is_shub());

	xp_partition_id = sn_partition_id;
	xp_region_size = sn_region_size;
	xp_rtc_cycles_per_second = sn_rtc_cycles_per_second;

	xp_remote_memcpy = xp_remote_memcpy_sn2;

	xp_register_remote_amos = xp_register_remote_amos_sn2;
	xp_unregister_remote_amos = xp_unregister_remote_amos_sn2;

	/*
	 * MSPEC based AMOs are assumed to have the important bits in only the
	 * first 64. The remainder is ignored other than xp_sizeof_amo must
	 * reflect its existence.
	 */
	BUG_ON(offsetof(AMO_t, variable) != 0);
	BUG_ON(sizeof(((AMO_t *) NULL)->variable) != sizeof(u64));
	xp_sizeof_amo = sizeof(AMO_t);
	xp_alloc_amos = xp_alloc_amos_sn2;
	xp_free_amos = xp_free_amos_sn2;
	xp_set_amo = xp_set_amo_sn2;
	xp_set_amo_with_interrupt = xp_set_amo_with_interrupt_sn2;
	xp_get_amo = xp_get_amo_sn2;

	xp_get_partition_rsvd_page_pa = xp_get_partition_rsvd_page_pa_sn2;

	xp_change_memprotect = xp_change_memprotect_sn2;
	xp_change_memprotect_shub_wars_1_1 =
	    xp_change_memprotect_shub_wars_1_1_sn2;
	xp_allow_IPI_ops = xp_allow_IPI_ops_sn2;
	xp_disallow_IPI_ops = xp_disallow_IPI_ops_sn2;

	xp_cpu_to_nasid = xp_cpu_to_nasid_sn2;
	xp_node_to_nasid = xp_node_to_nasid_sn2;

	return xp_register_nofault_code_sn2();
}

void
xp_exit_sn2(void)
{
	BUG_ON(!is_shub());

	xp_unregister_nofault_code_sn2();
}
