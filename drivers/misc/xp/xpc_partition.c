/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2004-2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Cross Partition Communication (XPC) partition support.
 *
 *	This is the part of XPC that detects the presence/absence of
 *	other partitions. It provides a heartbeat and monitors the
 *	heartbeats of other partitions.
 *
 */

#include <linux/kernel.h>
#include <linux/sysctl.h>
#include <linux/cache.h>
#include <linux/mmzone.h>
#include <linux/nodemask.h>
#include "xpc.h"

#if defined(CONFIG_IA64)
#define xp_pa(_a)	ia64_tpa(_a)
#elif defined(CONFIG_X86_64)
#define xp_pa(_a)	__pa(_a)
#else
#error architecture is NOT supported
#endif

/* XPC is exiting flag */
int xpc_exiting;

/* this partition's reserved page pointers */
struct xpc_rsvd_page *xpc_rsvd_page;
static u64 *xpc_part_nasids;
static u64 *xpc_mach_nasids;
struct xpc_vars *xpc_vars;
struct xpc_vars_part *xpc_vars_part;

/*
 * For performance reasons, each entry of xpc_partitions[] is cacheline
 * aligned. And xpc_partitions[] is padded with an additional entry at the
 * end so that the last legitimate entry doesn't share its cacheline with
 * another variable.
 */
struct xpc_partition xpc_partitions[XP_NPARTITIONS + 1];

/*
 * Generic buffer used to store a local copy of portions of a remote
 * partition's reserved page (either its header and part_nasids mask,
 * or its vars).
 */
char *xpc_remote_copy_buffer;
void *xpc_remote_copy_buffer_base;

/*
 * Guarantee that the kmalloc'd memory is cacheline aligned.
 */
void *
xpc_kmalloc_cacheline_aligned(size_t size, gfp_t flags, void **base)
{
	/* see if kmalloc will give us cachline aligned memory by default */
	*base = kmalloc(size, flags);
	if (*base == NULL)
		return NULL;

	if ((u64)*base == L1_CACHE_ALIGN((u64)*base))
		return *base;

	kfree(*base);

	/* nope, we'll have to do it ourselves */
	*base = kmalloc(size + L1_CACHE_BYTES, flags);
	if (*base == NULL)
		return NULL;

	return (void *)L1_CACHE_ALIGN((u64)*base);
}

/*
 * Given a nasid, get the physical address of the  partition's reserved page
 * for that nasid. This function returns 0 on any error.
 */
static u64
xpc_get_rsvd_page_pa(int nasid)
{
	u64 rp_pa = nasid;	/* seed with nasid */
	enum xp_retval ret;
	u64 cookie = 0;
	size_t len = 0;
	u64 buf = buf;
	size_t buf_len = 0;
	void *buf_base = NULL;

	while (1) {

		ret = xp_get_partition_rsvd_page_pa(buf, &cookie, &rp_pa, &len);

		dev_dbg(xpc_part, "SAL returned ret=%d cookie=0x%016" U64_ELL
			"x, address=0x%016" U64_ELL "x len=0x%016lx\n", ret,
			cookie, rp_pa, len);

		if (ret != xpNeedMoreInfo)
			break;

		if (L1_CACHE_ALIGN(len) > buf_len) {
			kfree(buf_base);
			buf_len = L1_CACHE_ALIGN(len);
			buf = (u64)xpc_kmalloc_cacheline_aligned(buf_len,
								 GFP_KERNEL,
								 &buf_base);
			if (buf_base == NULL) {
				dev_err(xpc_part, "unable to kmalloc "
					"len=0x%016lx\n", buf_len);
				ret = xpNoMemory;
				break;
			}
		}

		ret = xp_remote_memcpy((void *)buf, (void *)rp_pa, buf_len);
		if (ret != xpSuccess) {
			dev_dbg(xpc_part, "xp_remote_memcpy failed %d\n", ret);
			break;
		}
	}

	kfree(buf_base);

	if (ret != xpSuccess)
		rp_pa = 0;

	dev_dbg(xpc_part, "reserved page at phys address 0x%016" U64_ELL "x\n",
		rp_pa);
	return rp_pa;
}

/*
 * Fill the partition reserved page with the information needed by
 * other partitions to discover we are alive and establish initial
 * communications.
 */
struct xpc_rsvd_page *
xpc_rsvd_page_init(void)
{
	struct xpc_rsvd_page *rp;
	int n_amos;
	u64 *amos_page;
	u64 rp_pa;
	int i;
	u64 nasid_array = 0;
	int activate_irq_amos;
	int engaged_partitions_amos;
	int disengage_request_amos;
	int ret;

	/* get the local reserved page's address */

	preempt_disable();
	rp_pa = xpc_get_rsvd_page_pa(xp_cpu_to_nasid(smp_processor_id()));
	preempt_enable();
	if (rp_pa == 0) {
		dev_err(xpc_part, "SAL failed to locate the reserved page\n");
		return NULL;
	}
	rp = (struct xpc_rsvd_page *)__va(rp_pa);

	rp->version = XPC_RP_VERSION;

	/* establish the actual sizes of the nasid masks */
	if (rp->SAL_version == 1) {
		/* SAL_version 1 didn't set the SAL_nasids_size field */
		rp->SAL_nasids_size = 128;
	}
	xp_sizeof_nasid_mask = rp->SAL_nasids_size;

	/* setup the pointers to the various items in the reserved page */
	xpc_part_nasids = XPC_RP_PART_NASIDS(rp);
	xpc_mach_nasids = XPC_RP_MACH_NASIDS(rp);
	xpc_vars = XPC_RP_VARS(rp);
	xpc_vars_part = XPC_RP_VARS_PART(rp);

	/*
	 * Before clearing xpc_vars, see if a page (or pages) of AMOs had been
	 * previously allocated. If not we'll need to allocate one (or more)
	 * and set permissions so that cross-partition AMOs are allowed.
	 *
	 * The allocated AMO page(s) need MCA reporting to remain disabled after
	 * XPC has unloaded.  To make this work, we keep a copy of the pointer
	 * to this page (or pages) in the struct xpc_vars structure (i.e.,
	 * amos_page), which is pointed to by the reserved page, and re-use
	 * that saved copy on subsequent loads of XPC. This AMO page is never
	 * freed, and its memory protections are never restricted.
	 */
	amos_page = xpc_vars->amos_page;
	if (amos_page == NULL) {
		n_amos = xpc_number_of_amos(XP_NPARTITIONS);
		amos_page = xp_alloc_amos(n_amos);
		if (amos_page == NULL) {
			dev_err(xpc_part, "can't allocate page of AMOs\n");
			return NULL;
		}

		/*
		 * Open up AMO-R/W to cpu. This is done for Shub 1.1 systems
		 * when xp_allow_IPI_ops() is called via xpc_init().
		 */
		ret = xp_change_memprotect(xp_pa((u64)amos_page),
					   n_amos * xp_sizeof_amo,
					   XP_MEMPROT_ALLOW_CPU_AMO,
					   &nasid_array);
		if (ret != xpSuccess) {
			dev_err(xpc_part, "can't change memory protections\n");
			xp_free_amos(amos_page, n_amos);
			return NULL;
		}
	}

	/* clear xpc_vars */
	memset(xpc_vars, 0, sizeof(struct xpc_vars));

	xpc_vars->version = XPC_V_VERSION;
	xpc_vars->partid = xp_partition_id;
	xpc_vars->npartitions = XP_NPARTITIONS;
	xpc_vars->act_nasid = xp_cpu_to_nasid(0);
	xpc_vars->act_phys_cpuid = cpu_physical_id(0);
	xpc_vars->vars_part_pa = __pa(xpc_vars_part);
	xpc_vars->amos_page_pa = xp_pa((u64)amos_page);
	xpc_vars->amos_page = amos_page;	/* save for next load of XPC */

	/* clear xpc_vars_part */
	memset((u64 *)xpc_vars_part, 0, sizeof(struct xpc_vars_part) *
	       XP_NPARTITIONS);

	/* initialize the activate IRQ related AMO variables */
	activate_irq_amos = xpc_activate_irq_amos(XP_NPARTITIONS);
	for (i = 0; i < xp_nasid_mask_words(); i++)
		(void)xpc_IPI_init(activate_irq_amos + i);

	/* initialize the engaged remote partitions related AMO variables */
	engaged_partitions_amos = xpc_engaged_partitions_amos(XP_NPARTITIONS);
	disengage_request_amos = xpc_disengage_request_amos(XP_NPARTITIONS);
	for (i = 0; i < xp_partid_mask_words(XP_NPARTITIONS); i++) {
		(void)xpc_IPI_init(engaged_partitions_amos + i);
		(void)xpc_IPI_init(disengage_request_amos + i);
	}

	/* timestamp of when reserved page was setup by XPC */
	rp->stamp = CURRENT_TIME;

	/*
	 * This signifies to the remote partition that our reserved
	 * page is initialized.
	 */
	rp->vars_pa = __pa(xpc_vars);

	return rp;
}

/*
 * At periodic intervals, scan through all active partitions and ensure
 * their heartbeat is still active.  If not, the partition is deactivated.
 */
void
xpc_check_remote_hb(void)
{
	struct xpc_vars *remote_vars;
	struct xpc_partition *part;
	short partid;
	enum xp_retval ret;

	remote_vars = (struct xpc_vars *)xpc_remote_copy_buffer;

	for (partid = XP_MIN_PARTID; partid <= XP_MAX_PARTID; partid++) {

		if (xpc_exiting)
			break;

		if (partid == xp_partition_id)
			continue;

		part = &xpc_partitions[partid];

		if (part->act_state == XPC_P_AS_INACTIVE ||
		    part->act_state == XPC_P_AS_DEACTIVATING) {
			continue;
		}

		/* pull the remote_hb cache line */
		ret = xp_remote_memcpy(remote_vars,
				       (void *)part->remote_vars_pa,
				       XPC_RP_VARS_SIZE);
		if (ret != xpSuccess) {
			XPC_DEACTIVATE_PARTITION(part, ret);
			continue;
		}

		dev_dbg(xpc_part, "partid = %d, heartbeat = %" U64_ELL "d, "
			"last_heartbeat = %" U64_ELL "d, heartbeat_offline = %"
			U64_ELL "d\n", partid,
			remote_vars->heartbeat, part->last_heartbeat,
			remote_vars->heartbeat_offline);

		if (((remote_vars->heartbeat == part->last_heartbeat) &&
		     (remote_vars->heartbeat_offline == 0)) ||
		    !xpc_hb_allowed(xp_partition_id, remote_vars)) {

			XPC_DEACTIVATE_PARTITION(part, xpNoHeartbeat);
			continue;
		}

		part->last_heartbeat = remote_vars->heartbeat;
	}
}

/*
 * Get a copy of a portion of the remote partition's rsvd page.
 *
 * remote_rp points to a buffer that is cacheline aligned for BTE copies and
 * is large enough to contain a copy of their reserved page header and
 * part_nasids mask.
 */
static enum xp_retval
xpc_get_remote_rp(int nasid, u64 *discovered_nasids,
		  struct xpc_rsvd_page *remote_rp, u64 *remote_rp_pa)
{
	int i;
	enum xp_retval ret;

	/* get the reserved page's physical address */

	*remote_rp_pa = xpc_get_rsvd_page_pa(nasid);
	if (*remote_rp_pa == 0)
		return xpNoRsvdPageAddr;

	/* pull over the reserved page header and part_nasids mask */
	ret = xp_remote_memcpy(remote_rp, (void *)*remote_rp_pa,
			       XPC_RP_HEADER_SIZE + xp_sizeof_nasid_mask);
	if (ret != xpSuccess)
		return ret;

	if (discovered_nasids != NULL) {
		u64 *remote_part_nasids = XPC_RP_PART_NASIDS(remote_rp);

		for (i = 0; i < xp_nasid_mask_words(); i++)
			discovered_nasids[i] |= remote_part_nasids[i];
	}

	if (XPC_VERSION_MAJOR(remote_rp->version) !=
	    XPC_VERSION_MAJOR(XPC_RP_VERSION)) {
		return xpBadVersion;
	}

	return xpSuccess;
}

/*
 * Get a copy of the remote partition's XPC variables from the reserved page.
 *
 * remote_vars points to a buffer that is cacheline aligned for BTE copies and
 * assumed to be of size XPC_RP_VARS_SIZE.
 */
static enum xp_retval
xpc_get_remote_vars(u64 remote_vars_pa, struct xpc_vars *remote_vars)
{
	enum xp_retval ret;

	if (remote_vars_pa == 0)
		return xpVarsNotSet;

	/* pull over the cross partition variables */
	ret = xp_remote_memcpy(remote_vars, (void *)remote_vars_pa,
			       XPC_RP_VARS_SIZE);
	if (ret != xpSuccess)
		return ret;

	if (XPC_VERSION_MAJOR(remote_vars->version) !=
	    XPC_VERSION_MAJOR(XPC_V_VERSION)) {
		return xpBadVersion;
	}

	/* check that the partid is for another partition */
	if (remote_vars->partid < XP_MIN_PARTID ||
	    remote_vars->partid > XP_MAX_PARTID) {
		return xpInvalidPartid;
	}
	if (remote_vars->partid == xp_partition_id)
		return xpLocalPartid;

	return xpSuccess;
}

/*
 * Update the remote partition's info.
 */
static void
xpc_update_partition_info(struct xpc_partition *part, u8 remote_rp_version,
			  struct timespec *remote_rp_stamp, u64 remote_rp_pa,
			  u64 remote_vars_pa, struct xpc_vars *remote_vars)
{
	part->remote_rp_version = remote_rp_version;
	dev_dbg(xpc_part, "  remote_rp_version = 0x%016x\n",
		part->remote_rp_version);

	part->remote_rp_stamp = *remote_rp_stamp;
	dev_dbg(xpc_part, "  remote_rp_stamp (tv_sec = 0x%lx tv_nsec = 0x%lx\n",
		part->remote_rp_stamp.tv_sec, part->remote_rp_stamp.tv_nsec);

	part->remote_rp_pa = remote_rp_pa;
	dev_dbg(xpc_part, "  remote_rp_pa = 0x%016" U64_ELL "x\n",
		part->remote_rp_pa);

	part->remote_npartitions = remote_vars->npartitions;
	dev_dbg(xpc_part, "  remote_npartitions = %d\n",
		part->remote_npartitions);

	part->remote_vars_pa = remote_vars_pa;
	dev_dbg(xpc_part, "  remote_vars_pa = 0x%016" U64_ELL "x\n",
		part->remote_vars_pa);

	part->last_heartbeat = remote_vars->heartbeat;
	dev_dbg(xpc_part, "  last_heartbeat = 0x%016" U64_ELL "x\n",
		part->last_heartbeat);

	part->remote_vars_part_pa = remote_vars->vars_part_pa;
	dev_dbg(xpc_part, "  remote_vars_part_pa = 0x%016" U64_ELL "x\n",
		part->remote_vars_part_pa);

	part->remote_act_nasid = remote_vars->act_nasid;
	dev_dbg(xpc_part, "  remote_act_nasid = 0x%x\n",
		part->remote_act_nasid);

	part->remote_act_phys_cpuid = remote_vars->act_phys_cpuid;
	dev_dbg(xpc_part, "  remote_act_phys_cpuid = 0x%x\n",
		part->remote_act_phys_cpuid);

	part->remote_amos_page_pa = remote_vars->amos_page_pa;
	dev_dbg(xpc_part, "  remote_amos_page_pa = 0x%" U64_ELL "x\n",
		part->remote_amos_page_pa);

	part->remote_vars_version = remote_vars->version;
	dev_dbg(xpc_part, "  remote_vars_version = 0x%x\n",
		part->remote_vars_version);
}

/*
 * Prior code has determined the nasid which generated an IPI.  Inspect
 * that nasid to determine if its partition needs to be activated or
 * deactivated.
 *
 * A partition is consider "awaiting activation" if our partition
 * flags indicate it is not active and it has a heartbeat.  A
 * partition is considered "awaiting deactivation" if our partition
 * flags indicate it is active but it has no heartbeat or it is not
 * sending its heartbeat to us.
 *
 * To determine the heartbeat, the remote nasid must have a properly
 * initialized reserved page.
 */
static void
xpc_identify_act_IRQ_req(int nasid)
{
	struct xpc_rsvd_page *remote_rp;
	struct xpc_vars *remote_vars;
	u64 remote_rp_pa;
	u64 remote_vars_pa;
	int remote_rp_version;
	int reactivate = 0;
	int stamp_diff;
	struct timespec remote_rp_stamp = { 0, 0 };
	short partid;
	struct xpc_partition *part;
	enum xp_retval ret;

	/* pull over the reserved page structure */

	remote_rp = (struct xpc_rsvd_page *)xpc_remote_copy_buffer;

	ret = xpc_get_remote_rp(nasid, NULL, remote_rp, &remote_rp_pa);
	if (ret != xpSuccess) {
		dev_warn(xpc_part, "unable to get reserved page from nasid %d, "
			 "which sent interrupt, reason=%d\n", nasid, ret);
		return;
	}

	remote_vars_pa = remote_rp->vars_pa;
	remote_rp_version = remote_rp->version;
	if (XPC_SUPPORTS_RP_STAMP(remote_rp_version))
		remote_rp_stamp = remote_rp->stamp;

	/* pull over the cross partition variables */

	remote_vars = (struct xpc_vars *)xpc_remote_copy_buffer;

	ret = xpc_get_remote_vars(remote_vars_pa, remote_vars);
	if (ret != xpSuccess) {
		dev_warn(xpc_part, "unable to get XPC variables from nasid %d, "
			 "which sent interrupt, reason=%d\n", nasid, ret);
		return;
	}

	partid = remote_vars->partid;
	part = &xpc_partitions[partid];

	part->act_IRQ_rcvd++;

	dev_dbg(xpc_part, "partid for nasid %d is %d; IRQs = %d; HB = "
		"%" U64_ELL "d\n", (int)nasid, (int)partid,
		part->act_IRQ_rcvd, remote_vars->heartbeat);

	if (xpc_partition_disengaged(part) &&
	    part->act_state == XPC_P_AS_INACTIVE) {

		xpc_update_partition_info(part, remote_rp_version,
					  &remote_rp_stamp, remote_rp_pa,
					  remote_vars_pa, remote_vars);

		if (XPC_SUPPORTS_DISENGAGE_REQUEST(part->remote_vars_version)) {
			if (xpc_partition_disengage_requested(partid)) {
				/*
				 * Other side is waiting on us to disengage,
				 * even though we already have.
				 */
				return;
			}
		} else {
			/* other side doesn't support disengage requests */
			xpc_clear_partition_disengage_request(partid);
		}

		xpc_activate_partition(part);
		return;
	}

	DBUG_ON(part->remote_rp_version == 0);
	DBUG_ON(part->remote_vars_version == 0);

	if (!XPC_SUPPORTS_RP_STAMP(part->remote_rp_version)) {
		DBUG_ON(XPC_SUPPORTS_DISENGAGE_REQUEST(part->
						       remote_vars_version));

		if (!XPC_SUPPORTS_RP_STAMP(remote_rp_version)) {
			DBUG_ON(XPC_SUPPORTS_DISENGAGE_REQUEST(remote_vars->
							       version));
			/* see if the other side rebooted */
			if (part->remote_amos_page_pa ==
			    remote_vars->amos_page_pa &&
			    xpc_hb_allowed(xp_partition_id, remote_vars)) {
				/* doesn't look that way, so ignore the IPI */
				return;
			}
		}

		/*
		 * Other side rebooted and previous XPC didn't support the
		 * disengage request, so we don't need to do anything special.
		 */

		xpc_update_partition_info(part, remote_rp_version,
					  &remote_rp_stamp, remote_rp_pa,
					  remote_vars_pa, remote_vars);
		part->reactivate_nasid = nasid;
		XPC_DEACTIVATE_PARTITION(part, xpReactivating);
		return;
	}

	DBUG_ON(!XPC_SUPPORTS_DISENGAGE_REQUEST(part->remote_vars_version));

	if (!XPC_SUPPORTS_RP_STAMP(remote_rp_version)) {
		DBUG_ON(!XPC_SUPPORTS_DISENGAGE_REQUEST(remote_vars->version));

		/*
		 * Other side rebooted and previous XPC did support the
		 * disengage request, but the new one doesn't.
		 */

		xpc_clear_partition_engaged(partid);
		xpc_clear_partition_disengage_request(partid);

		xpc_update_partition_info(part, remote_rp_version,
					  &remote_rp_stamp, remote_rp_pa,
					  remote_vars_pa, remote_vars);
		reactivate = 1;

	} else {
		DBUG_ON(!XPC_SUPPORTS_DISENGAGE_REQUEST(remote_vars->version));

		stamp_diff = xpc_compare_stamps(&part->remote_rp_stamp,
						&remote_rp_stamp);
		if (stamp_diff != 0) {
			DBUG_ON(stamp_diff >= 0);

			/*
			 * Other side rebooted and the previous XPC did support
			 * the disengage request, as does the new one.
			 */

			DBUG_ON(xpc_partition_engaged(partid));
			DBUG_ON(xpc_partition_disengage_requested(partid));

			xpc_update_partition_info(part, remote_rp_version,
						  &remote_rp_stamp,
						  remote_rp_pa, remote_vars_pa,
						  remote_vars);
			reactivate = 1;
		}
	}

	if (part->disengage_request_timeout > 0 &&
	    !xpc_partition_disengaged(part)) {
		/* still waiting on other side to disengage from us */
		return;
	}

	if (reactivate) {
		part->reactivate_nasid = nasid;
		XPC_DEACTIVATE_PARTITION(part, xpReactivating);

	} else if (XPC_SUPPORTS_DISENGAGE_REQUEST(part->remote_vars_version) &&
		   xpc_partition_disengage_requested(partid)) {
		XPC_DEACTIVATE_PARTITION(part, xpOtherGoingDown);
	}
}

/*
 * Loop through the activation AMO variables and process any bits
 * which are set.  Each bit indicates a nasid sending a partition
 * activation or deactivation request.
 *
 * Return #of IRQs detected.
 */
int
xpc_identify_act_IRQ_sender(void)
{
	enum xp_retval ret;
	int w_index, b_index;
	u64 *amo_va;
	u64 nasid_mask;
	u64 nasid;		/* remote nasid */
	int n_IRQs_detected = 0;

	amo_va = (u64 *)((u64)xpc_vars->amos_page +
			 xpc_activate_irq_amos(xpc_vars->npartitions) *
			 xp_sizeof_amo);

	/* scan through activation AMO variables looking for non-zero entries */
	for (w_index = 0; w_index < xp_nasid_mask_words(); w_index++) {

		if (xpc_exiting)
			break;

		ret = xp_get_amo(amo_va, XP_AMO_CLEAR, &nasid_mask);
		BUG_ON(ret != xpSuccess);	/* should never happen */
		amo_va = (u64 *)((u64)amo_va + xp_sizeof_amo);	/* next amo */
		if (nasid_mask == 0) {
			/* no IRQs from nasids in this variable */
			continue;
		}

		dev_dbg(xpc_part, "AMO[%d] gave back 0x%" U64_ELL "x\n",
			w_index, nasid_mask);

		/*
		 * If any nasid(s) in mask have been added to the machine
		 * since our partition was reset, this will retain the
		 * remote nasid(s) in our reserved pages machine mask.
		 * This is used in the event of module reload.
		 */
		xpc_mach_nasids[w_index] |= nasid_mask;

		/* locate the nasid(s) which sent interrupts */

		for (b_index = 0; b_index < BITS_PER_LONG; b_index++) {
			if (nasid_mask & (1UL << b_index)) {
				n_IRQs_detected++;
				nasid = (w_index * BITS_PER_LONG + b_index) * 2;
				dev_dbg(xpc_part, "interrupt from nasid %"
					U64_ELL "d\n", nasid);
				xpc_identify_act_IRQ_req(nasid);
			}
		}
	}
	return n_IRQs_detected;
}

/*
 * See if the other side has responded to a partition disengage request
 * from us.
 */
int
xpc_partition_disengaged(struct xpc_partition *part)
{
	short partid = XPC_PARTID(part);
	int disengaged;

	disengaged = (xpc_partition_engaged(partid) == 0);
	if (part->disengage_request_timeout) {
		if (!disengaged) {
			if (time_before(jiffies, part->disengage_request_timeout)) {
				/* timelimit hasn't been reached yet */
				return 0;
			}

			/*
			 * Other side hasn't responded to our disengage
			 * request in a timely fashion, so assume it's dead.
			 */

			dev_info(xpc_part, "disengage from remote partition %d "
				 "timed out\n", partid);
			xpc_disengage_request_timedout = 1;
			xpc_clear_partition_engaged(partid);
			disengaged = 1;
		}
		part->disengage_request_timeout = 0;

		/* cancel the timer function, provided it's not us */
		if (!in_interrupt()) {
			del_singleshot_timer_sync(&part->
						  disengage_request_timer);
		}

		DBUG_ON(part->act_state != XPC_P_AS_DEACTIVATING &&
			part->act_state != XPC_P_AS_INACTIVE);
		if (part->act_state != XPC_P_AS_INACTIVE)
			xpc_wakeup_channel_mgr(part);

		if (XPC_SUPPORTS_DISENGAGE_REQUEST(part->remote_vars_version))
			xpc_cancel_partition_disengage_request(part);
	}
	return disengaged;
}

/*
 * Mark specified partition as active.
 */
enum xp_retval
xpc_mark_partition_active(struct xpc_partition *part)
{
	unsigned long irq_flags;
	enum xp_retval ret;

	dev_dbg(xpc_part, "setting partition %d to ACTIVE\n", XPC_PARTID(part));

	spin_lock_irqsave(&part->lock, irq_flags);
	if (part->act_state == XPC_P_AS_ACTIVATING) {
		part->act_state = XPC_P_AS_ACTIVE;
		ret = xpSuccess;
	} else {
		DBUG_ON(part->reason == xpSuccess);
		ret = part->reason;
	}
	spin_unlock_irqrestore(&part->lock, irq_flags);

	return ret;
}

/*
 * Notify XPC that the partition is down.
 */
void
xpc_deactivate_partition(const int line, struct xpc_partition *part,
			 enum xp_retval reason)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&part->lock, irq_flags);

	if (part->act_state == XPC_P_AS_INACTIVE) {
		XPC_SET_REASON(part, reason, line);
		spin_unlock_irqrestore(&part->lock, irq_flags);
		if (reason == xpReactivating) {
			/* we interrupt ourselves to reactivate partition */
			xpc_IPI_send_reactivate(part);
		}
		return;
	}
	if (part->act_state == XPC_P_AS_DEACTIVATING) {
		if ((part->reason == xpUnloading && reason != xpUnloading) ||
		    reason == xpReactivating) {
			XPC_SET_REASON(part, reason, line);
		}
		spin_unlock_irqrestore(&part->lock, irq_flags);
		return;
	}

	part->act_state = XPC_P_AS_DEACTIVATING;
	XPC_SET_REASON(part, reason, line);

	spin_unlock_irqrestore(&part->lock, irq_flags);

	if (XPC_SUPPORTS_DISENGAGE_REQUEST(part->remote_vars_version)) {
		xpc_request_partition_disengage(part);
		xpc_IPI_send_disengage(part);

		/* set a timelimit on the disengage request */
		part->disengage_request_timeout = jiffies +
		    (xpc_disengage_request_timelimit * HZ);
		part->disengage_request_timer.expires =
		    part->disengage_request_timeout;
		add_timer(&part->disengage_request_timer);
	}

	dev_dbg(xpc_part, "bringing partition %d down, reason = %d\n",
		XPC_PARTID(part), reason);

	xpc_partition_going_down(part, reason);
}

/*
 * Mark specified partition as inactive.
 */
void
xpc_mark_partition_inactive(struct xpc_partition *part)
{
	unsigned long irq_flags;

	dev_dbg(xpc_part, "setting partition %d to INACTIVE\n",
		XPC_PARTID(part));

	spin_lock_irqsave(&part->lock, irq_flags);
	part->act_state = XPC_P_AS_INACTIVE;
	spin_unlock_irqrestore(&part->lock, irq_flags);
	part->remote_rp_pa = 0;
}

/*
 * Register the remote partition's AMOs so any errors within that address
 * range can be handled and cleaned up should the remote partition go down.
 */
enum xp_retval
xpc_register_remote_amos(struct xpc_partition *part)
{
	unsigned long irq_flags;
	size_t len;
	enum xp_retval ret;

	if (part->flags & XPC_P_RAMOSREGISTERED)
		return xpSuccess;

	len = xpc_number_of_amos(part->remote_npartitions) * xp_sizeof_amo;
	ret = xp_register_remote_amos(part->remote_amos_page_pa, len);
	if (ret == xpSuccess) {
		spin_lock_irqsave(&part->lock, irq_flags);
		part->flags |= XPC_P_RAMOSREGISTERED;
		spin_unlock_irqrestore(&part->lock, irq_flags);
	}
	return ret;
}

void
xpc_unregister_remote_amos(struct xpc_partition *part)
{
	unsigned long irq_flags;
	size_t len;
	enum xp_retval ret;

	if (!(part->flags & XPC_P_RAMOSREGISTERED))
		return;

	len = xpc_number_of_amos(part->remote_npartitions) * xp_sizeof_amo;
	ret = xp_unregister_remote_amos(part->remote_amos_page_pa, len);
	if (ret != xpSuccess)
		dev_warn(xpc_part, "failed to unregister remote AMOs for "
			 "partition %d, ret=%d\n", XPC_PARTID(part), ret);

	spin_lock_irqsave(&part->lock, irq_flags);
	part->flags &= ~XPC_P_RAMOSREGISTERED;
	spin_unlock_irqrestore(&part->lock, irq_flags);
}

/*
 * SAL has provided a partition and machine mask.  The partition mask
 * contains a bit for each even nasid in our partition.  The machine
 * mask contains a bit for each even nasid in the entire machine.
 *
 * Using those two bit arrays, we can determine which nasids are
 * known in the machine.  Each should also have a reserved page
 * initialized if they are available for partitioning.
 */
void
xpc_discovery(void)
{
	void *remote_rp_base;
	struct xpc_rsvd_page *remote_rp;
	struct xpc_vars *remote_vars;
	u64 remote_rp_pa;
	u64 remote_vars_pa;
	int region;
	int region_size;
	int max_regions;
	int nasid;
	struct xpc_rsvd_page *rp;
	short partid;
	struct xpc_partition *part;
	u64 *discovered_nasids;
	enum xp_retval ret;

	remote_rp = xpc_kmalloc_cacheline_aligned(XPC_RP_HEADER_SIZE +
						  xp_sizeof_nasid_mask,
						  GFP_KERNEL, &remote_rp_base);
	if (remote_rp == NULL)
		return;

	remote_vars = (struct xpc_vars *)remote_rp;

	discovered_nasids = kzalloc(sizeof(u64) * xp_nasid_mask_words(),
				    GFP_KERNEL);
	if (discovered_nasids == NULL) {
		kfree(remote_rp_base);
		return;
	}

	rp = (struct xpc_rsvd_page *)xpc_rsvd_page;

	/*
	 * The term 'region' in this context refers to the minimum number of
	 * nodes that can comprise an access protection grouping. The access
	 * protection is in regards to memory, IOI and IPI.
	 */
	max_regions = 64;
	region_size = xp_region_size;

	switch (region_size) {
	case 128:
		max_regions *= 2;
	case 64:
		max_regions *= 2;
	case 32:
		max_regions *= 2;
		region_size = 16;
		DBUG_ON(!is_shub2());
	}

	for (region = 0; region < max_regions; region++) {

		if (xpc_exiting)
			break;

		dev_dbg(xpc_part, "searching region %d\n", region);

		for (nasid = (region * region_size * 2);
		     nasid < ((region + 1) * region_size * 2); nasid += 2) {

			if (xpc_exiting)
				break;

			dev_dbg(xpc_part, "checking nasid %d\n", nasid);

			if (XPC_NASID_IN_ARRAY(nasid, xpc_part_nasids)) {
				dev_dbg(xpc_part, "PROM indicates Nasid %d is "
					"part of the local partition; skipping "
					"region\n", nasid);
				break;
			}

			if (!(XPC_NASID_IN_ARRAY(nasid, xpc_mach_nasids))) {
				dev_dbg(xpc_part, "PROM indicates Nasid %d was "
					"not on Numa-Link network at reset\n",
					nasid);
				continue;
			}

			if (XPC_NASID_IN_ARRAY(nasid, discovered_nasids)) {
				dev_dbg(xpc_part, "Nasid %d is part of a "
					"partition which was previously "
					"discovered\n", nasid);
				continue;
			}

			/* pull over the reserved page structure */

			ret = xpc_get_remote_rp(nasid, discovered_nasids,
						remote_rp, &remote_rp_pa);
			if (ret != xpSuccess) {
				dev_dbg(xpc_part, "unable to get reserved page "
					"from nasid %d, reason=%d\n", nasid,
					ret);
				continue;
			}

			remote_vars_pa = remote_rp->vars_pa;

			/* pull over the cross partition variables */

			ret = xpc_get_remote_vars(remote_vars_pa, remote_vars);
			if (ret != xpSuccess) {
				dev_dbg(xpc_part, "unable to get XPC variables "
					"from nasid %d, reason=%d\n", nasid,
					ret);
				if (ret == xpLocalPartid)
					break;
				continue;
			}

			partid = remote_vars->partid;
			part = &xpc_partitions[partid];

			if (part->act_state != XPC_P_AS_INACTIVE) {
				dev_dbg(xpc_part, "partition %d on nasid %d is "
					"already activating\n", partid, nasid);
				break;
			}

			/*
			 * Register the remote partition's AMOs so any errors
			 * within that address range can be handled and
			 * cleaned up should the remote partition go down.
			 */
			part->remote_npartitions = remote_vars->npartitions;
			part->remote_amos_page_pa = remote_vars->amos_page_pa;
			ret = xpc_register_remote_amos(part);
			if (ret != xpSuccess) {
				dev_warn(xpc_part, "xpc_discovery() failed to "
					 "register remote AMOs for partition %d"
					 ", ret=%d\n", partid, ret);

				XPC_SET_REASON(part, xpPhysAddrRegFailed,
					       __LINE__);
				break;
			}

			/*
			 * The remote nasid is valid and available.
			 * Send an interrupt to that nasid to notify
			 * it that we are ready to begin activation.
			 */
			dev_dbg(xpc_part, "sending an interrupt to AMO 0x%"
				U64_ELL "x, nasid %d, phys_cpuid 0x%x\n",
				remote_vars->amos_page_pa,
				remote_vars->act_nasid,
				remote_vars->act_phys_cpuid);

			if (XPC_SUPPORTS_DISENGAGE_REQUEST(remote_vars->
							   version)) {
				part->remote_amos_page_pa =
				    remote_vars->amos_page_pa;
				xpc_mark_partition_disengaged(part);
				xpc_cancel_partition_disengage_request(part);
			}
			xpc_IPI_send_activate(remote_vars);
		}
	}

	kfree(discovered_nasids);
	kfree(remote_rp_base);
}

/*
 * Given a partid, get the nasids owned by that partition from the
 * remote partition's reserved page.
 */
enum xp_retval
xpc_initiate_partid_to_nasids(short partid, void *nasid_mask)
{
	struct xpc_partition *part;
	u64 part_nasid_pa;

	part = &xpc_partitions[partid];
	if (part->remote_rp_pa == 0)
		return xpPartitionDown;

	memset(nasid_mask, 0, xp_sizeof_nasid_mask);

	part_nasid_pa = (u64)XPC_RP_PART_NASIDS(part->remote_rp_pa);

	return xp_remote_memcpy(nasid_mask, (void *)part_nasid_pa,
				xp_sizeof_nasid_mask);
}
