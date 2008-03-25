/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2004-2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Cross Partition Communication (XPC) structures and macros.
 */

#ifndef _DRIVERS_MISC_XP_XPC_H
#define _DRIVERS_MISC_XP_XPC_H

#include <linux/interrupt.h>
#include <linux/sysctl.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#if defined(CONFIG_IA64)
#include <asm/sn/intr.h>
#elif defined(CONFIG_X86_64)
#define SGI_XPC_ACTIVATE 0x30
#define SGI_XPC_NOTIFY 0xe7
#else
#error architecture is NOT supported
#endif
#include "xp.h"

/*
 * XPC Version numbers consist of a major and minor number. XPC can always
 * talk to versions with same major #, and never talk to versions with a
 * different major #.
 */
#define _XPC_VERSION(_maj, _min)	(((_maj) << 4) | ((_min) & 0xf))
#define XPC_VERSION_MAJOR(_v)		((_v) >> 4)
#define XPC_VERSION_MINOR(_v)		((_v) & 0xf)

/*
 * The next macros define word or bit representations for given
 * C-brick nasid in either the SAL provided bit array representing
 * nasids in the partition/machine or the array of AMO variables used for
 * inter-partition initiation communications.
 *
 * For SN2 machines, C-Bricks are alway even numbered NASIDs.  As
 * such, some space will be saved by insisting that nasid information
 * passed from SAL always be packed for C-Bricks and the
 * cross-partition interrupts use the same packing scheme.
 */
#define XPC_NASID_IN_ARRAY(_n, _p) ((_p)[BIT_WORD((_n)/2)] & BIT_MASK((_n)/2))

#define XPC_HB_DEFAULT_INTERVAL		5	/* incr HB every x secs */
#define XPC_HB_CHECK_DEFAULT_INTERVAL	20	/* check HB every x secs */

/* define the process name of HB checker and the CPU it is pinned to */
#define XPC_HB_CHECK_THREAD_NAME	"xpc_hb"
#define XPC_HB_CHECK_CPU		0

/* define the process name of the discovery thread */
#define XPC_DISCOVERY_THREAD_NAME	"xpc_discovery"

/*
 * the reserved page
 *
 *   SAL reserves one page of memory per partition for XPC. Though a full page
 *   in length (16384 bytes), its starting address is not page aligned, but it
 *   is cacheline aligned. The reserved page consists of the following:
 *
 *   reserved page header
 *
 *     The first cacheline of the reserved page contains the header
 *     (struct xpc_rsvd_page). Before SAL initialization has completed,
 *     SAL has set up the following fields of the reserved page header:
 *     SAL_signature, SAL_version, SAL_partid, and SAL_nasids_size. The
 *     other fields are set up by XPC. (xpc_rsvd_page points to the local
 *     partition's reserved page.)
 *
 *   part_nasids mask
 *   mach_nasids mask
 *
 *     SAL also sets up two bitmaps (or masks), one that reflects the actual
 *     nasids in this partition (part_nasids), and the other that reflects
 *     the actual nasids in the entire machine (mach_nasids). We're only
 *     interested in the even numbered nasids (which contain the processors
 *     and/or memory), so we only need half as many bits to represent the
 *     nasids. The part_nasids mask is located starting at the first cacheline
 *     following the reserved page header. The mach_nasids mask follows right
 *     after the part_nasids mask. The size in bytes of each mask is reflected
 *     by the reserved page header field 'nasids_size'. (Local partition's
 *     mask pointers are xpc_part_nasids and xpc_mach_nasids.)
 *
 *   vars
 *   vars part
 *
 *     Immediately following the mach_nasids mask are the XPC variables
 *     required by other partitions. First are those that are generic to all
 *     partitions (vars), followed on the next available cacheline by those
 *     which are partition specific (vars part). These are setup by XPC.
 *     (Local partition's vars pointers are xpc_vars and xpc_vars_part.)
 *
 * Note: Until vars_pa is set, the partition XPC code has not been initialized.
 */
struct xpc_rsvd_page {
	u64 SAL_signature;	/* SAL: unique signature */
	u64 SAL_version;	/* SAL: version */
	u8 SAL_partid;		/* SAL: partition ID */
	u8 version;
	u8 pad[6];
	volatile u64 vars_pa;	/* physical address of struct xpc_vars */
	struct timespec stamp;	/* time when reserved page was setup by XPC */
	u64 pad2[9];		/* align to last u64 in cacheline */
	u64 SAL_nasids_size;	/* SAL: size of each nasid mask in bytes */
};

#define XPC_RP_VERSION _XPC_VERSION(2,0)	/* version 2.0 of the reserved page */

#define XPC_SUPPORTS_RP_STAMP(_version) \
			(_version >= _XPC_VERSION(1,1))

/*
 * compare stamps - the return value is:
 *
 *	< 0,	if stamp1 < stamp2
 *	= 0,	if stamp1 == stamp2
 *	> 0,	if stamp1 > stamp2
 */
static inline int
xpc_compare_stamps(struct timespec *stamp1, struct timespec *stamp2)
{
	int ret;

	if ((ret = stamp1->tv_sec - stamp2->tv_sec) == 0) {
		ret = stamp1->tv_nsec - stamp2->tv_nsec;
	}
	return ret;
}

/*
 * Define the structures by which XPC variables can be exported to other
 * partitions. (There are two: struct xpc_vars and struct xpc_vars_part)
 */

/*
 * The following structure describes the partition generic variables
 * needed by other partitions in order to properly initialize.
 *
 * struct xpc_vars version number also applies to struct xpc_vars_part.
 * Changes to either structure and/or related functionality should be
 * reflected by incrementing either the major or minor version numbers
 * of struct xpc_vars.
 */
struct xpc_vars {
	u8 version;
	short partid;
	short npartitions;	/* value of XPC_NPARTITIONS */
	int act_nasid;
	int act_phys_cpuid;
	u64 vars_part_pa;
	u64 amos_page_pa;	/* paddr of first page of AMOs variables */
	u64 *amos_page;		/* vaddr of first page of AMOs variables */
	u64 heartbeat;
	u64 heartbeat_offline;	/* if 0, heartbeat should be changing */
	u64 heartbeating_to_mask[BITS_TO_LONGS(XP_MAX_NPARTITIONS)];
};

#define XPC_V_VERSION _XPC_VERSION(4,0)	/* version 4.0 of the cross vars */

#define XPC_SUPPORTS_DISENGAGE_REQUEST(_version) \
			(_version >= _XPC_VERSION(3,1))

static inline int
xpc_hb_allowed(short partid, struct xpc_vars *vars)
{
	return test_bit(partid, vars->heartbeating_to_mask);
}

static inline int
xpc_any_hbs_allowed(struct xpc_vars *vars)
{
	return !bitmap_empty((unsigned long *)vars->heartbeating_to_mask,
			     vars->npartitions);
}

static inline void
xpc_allow_hb(short partid, struct xpc_vars *vars)
{
	set_bit(partid, vars->heartbeating_to_mask);
}

static inline void
xpc_disallow_hb(short partid, struct xpc_vars *vars)
{
	clear_bit(partid, vars->heartbeating_to_mask);
}

static inline void
xpc_disallow_all_hbs(struct xpc_vars *vars)
{
	int nlongs = BITS_TO_LONGS(vars->npartitions);
	int i;

	for (i = 0; i < nlongs; i++)
		vars->heartbeating_to_mask[i] = 0;
}

/*
 * The AMOs page(s) consists of a number of AMO variables which are divided into
 * four groups, The first group consists of one AMO per partition, each of which
 * reflects state changes of up to eight channels and are accompanied by the
 * receipt of a NOTIFY IRQ. The second group represents a bitmap of nasids by
 * which to identify an ACTIVATE IRQ's sender. The last two groups, each
 * representing a bitmap of partids, are used to identify the remote partitions
 * that are currently engaged (from the viewpoint of the XPC running on the
 * remote partition).
 *
 * The following #defines reflect an AMO index into these AMOS page(s).
 */

/* get offset to beginning of notify IRQ AMOs */
static inline int
xpc_notify_irq_amos(void)
{
	return 0;
}

/* get offset to beginning of activate IRQ AMOs */
static inline int
xpc_activate_irq_amos(int npartitions)
{
	return xpc_notify_irq_amos() + npartitions;
}

/* get offset to beginning of engaged partitions AMOs */
static inline int
xpc_engaged_partitions_amos(int npartitions)
{
	return xpc_activate_irq_amos(npartitions) + xp_nasid_mask_words();
}

/* get offset to beginning of disengaged request AMOs */
static inline int
xpc_disengage_request_amos(int npartitions)
{
	return xpc_engaged_partitions_amos(npartitions) +
	    xp_partid_mask_words(npartitions);
}

/* get total number of AMOs */
static inline int
xpc_number_of_amos(int npartitions)
{
	return xpc_disengage_request_amos(npartitions) +
	    xp_partid_mask_words(npartitions);
}

/*
 * The following structure describes the per partition specific variables.
 *
 * An array of these structures, one per partition, will be defined. As a
 * partition becomes active XPC will copy the array entry corresponding to
 * itself from that partition. It is desirable that the size of this
 * structure evenly divide into a cacheline, such that none of the entries
 * in this array crosses a cacheline boundary. As it is now, each entry
 * occupies half a cacheline.
 */
struct xpc_vars_part {
	volatile u64 magic;

	u64 openclose_args_pa;	/* physical address of open and close args */
	u64 GPs_pa;		/* physical address of Get/Put values */

	u64 IPI_amo_pa;		/* physical address of IPI AMO variable */
	int IPI_nasid;		/* nasid of where to send IPIs */
	int IPI_phys_cpuid;	/* physical CPU ID of where to send IPIs */

	u8 nchannels;		/* #of defined channels supported */

	u8 reserved[23];	/* pad to a full 64 bytes */
};

/*
 * The vars_part MAGIC numbers play a part in the first contact protocol.
 *
 * MAGIC1 indicates that the per partition specific variables for a remote
 * partition have been initialized by this partition.
 *
 * MAGIC2 indicates that this partition has pulled the remote partititions
 * per partition variables that pertain to this partition.
 */
#define XPC_VP_MAGIC1	0x0053524156435058L	/* 'XPCVARS\0'L (little endian) */
#define XPC_VP_MAGIC2	0x0073726176435058L	/* 'XPCvars\0'L (little endian) */

/* the reserved page sizes and offsets */

#define XPC_RP_HEADER_SIZE	L1_CACHE_ALIGN(sizeof(struct xpc_rsvd_page))
#define XPC_RP_VARS_SIZE 	L1_CACHE_ALIGN(sizeof(struct xpc_vars))

#define XPC_RP_PART_NASIDS(_rp) (u64 *)((u8 *)(_rp) + XPC_RP_HEADER_SIZE)
#define XPC_RP_MACH_NASIDS(_rp) (XPC_RP_PART_NASIDS(_rp) + \
				 xp_nasid_mask_words())
#define XPC_RP_VARS(_rp)	(struct xpc_vars *)(XPC_RP_MACH_NASIDS(_rp) + \
	       			 xp_nasid_mask_words())
#define XPC_RP_VARS_PART(_rp)	(struct xpc_vars_part *)((u8 *)XPC_RP_VARS(_rp) + XPC_RP_VARS_SIZE)

/*
 * Functions registered by add_timer() or called by kernel_thread() only
 * allow for a single 64-bit argument. The following macros can be used to
 * pack and unpack two (32-bit, 16-bit or 8-bit) arguments into or out from
 * the passed argument.
 */
#define XPC_PACK_ARGS(_arg1, _arg2) \
			((((u64) _arg1) & 0xffffffff) | \
			((((u64) _arg2) & 0xffffffff) << 32))

#define XPC_UNPACK_ARG1(_args)	(((u64) _args) & 0xffffffff)
#define XPC_UNPACK_ARG2(_args)	((((u64) _args) >> 32) & 0xffffffff)

/*
 * Define a Get/Put value pair (pointers) used with a message queue.
 */
struct xpc_gp {
	volatile s64 get;	/* Get value */
	volatile s64 put;	/* Put value */
};

#define XPC_GP_SIZE \
		L1_CACHE_ALIGN(sizeof(struct xpc_gp) * XPC_NCHANNELS)

/*
 * Define a structure that contains arguments associated with opening and
 * closing a channel.
 */
struct xpc_openclose_args {
	u16 reason;		/* reason why channel is closing */
	u16 msg_size;		/* sizeof each message entry */
	u16 remote_nentries;	/* #of message entries in remote msg queue */
	u16 local_nentries;	/* #of message entries in local msg queue */
	u64 local_msgqueue_pa;	/* physical address of local message queue */
};

#define XPC_OPENCLOSE_ARGS_SIZE \
	      L1_CACHE_ALIGN(sizeof(struct xpc_openclose_args) * XPC_NCHANNELS)

/* struct xpc_msg flags */

#define	XPC_M_DONE		0x01	/* msg has been received/consumed */
#define	XPC_M_READY		0x02	/* msg is ready to be sent */
#define	XPC_M_INTERRUPT		0x04	/* send interrupt when msg consumed */

#define XPC_MSG_ADDRESS(_payload) \
		((struct xpc_msg *)((u8 *)(_payload) - XPC_MSG_PAYLOAD_OFFSET))

/*
 * Defines notify entry.
 *
 * This is used to notify a message's sender that their message was received
 * and consumed by the intended recipient.
 */
struct xpc_notify {
	volatile u8 type;	/* type of notification */

	/* the following two fields are only used if type == XPC_N_CALL */
	xpc_notify_func func;	/* user's notify function */
	void *key;		/* pointer to user's key */
};

/* struct xpc_notify type of notification */

#define	XPC_N_CALL		0x01	/* notify function provided by user */

/*
 * Define the structure that manages all the stuff required by a channel. In
 * particular, they are used to manage the messages sent across the channel.
 *
 * This structure is private to a partition, and is NOT shared across the
 * partition boundary.
 *
 * There is an array of these structures for each remote partition. It is
 * allocated at the time a partition becomes active. The array contains one
 * of these structures for each potential channel connection to that partition.
 *
 * Each of these structures manages two message queues (circular buffers).
 * They are allocated at the time a channel connection is made. One of
 * these message queues (local_msgqueue) holds the locally created messages
 * that are destined for the remote partition. The other of these message
 * queues (remote_msgqueue) is a locally cached copy of the remote partition's
 * own local_msgqueue.
 *
 * The following is a description of the Get/Put pointers used to manage these
 * two message queues. Consider the local_msgqueue to be on one partition
 * and the remote_msgqueue to be its cached copy on another partition. A
 * description of what each of the lettered areas contains is included.
 *
 *
 *                     local_msgqueue      remote_msgqueue
 *
 *                        |/////////|      |/////////|
 *    w_remote_GP.get --> +---------+      |/////////|
 *                        |    F    |      |/////////|
 *     remote_GP.get  --> +---------+      +---------+ <-- local_GP->get
 *                        |         |      |         |
 *                        |         |      |    E    |
 *                        |         |      |         |
 *                        |         |      +---------+ <-- w_local_GP.get
 *                        |    B    |      |/////////|
 *                        |         |      |////D////|
 *                        |         |      |/////////|
 *                        |         |      +---------+ <-- w_remote_GP.put
 *                        |         |      |////C////|
 *      local_GP->put --> +---------+      +---------+ <-- remote_GP.put
 *                        |         |      |/////////|
 *                        |    A    |      |/////////|
 *                        |         |      |/////////|
 *     w_local_GP.put --> +---------+      |/////////|
 *                        |/////////|      |/////////|
 *
 *
 *	    ( remote_GP.[get|put] are cached copies of the remote
 *	      partition's local_GP->[get|put], and thus their values can
 *	      lag behind their counterparts on the remote partition. )
 *
 *
 *  A - Messages that have been allocated, but have not yet been sent to the
 *	remote partition.
 *
 *  B - Messages that have been sent, but have not yet been acknowledged by the
 *      remote partition as having been received.
 *
 *  C - Area that needs to be prepared for the copying of sent messages, by
 *	the clearing of the message flags of any previously received messages.
 *
 *  D - Area into which sent messages are to be copied from the remote
 *	partition's local_msgqueue and then delivered to their intended
 *	recipients. [ To allow for a multi-message copy, another pointer
 *	(next_msg_to_pull) has been added to keep track of the next message
 *	number needing to be copied (pulled). It chases after w_remote_GP.put.
 *	Any messages lying between w_local_GP.get and next_msg_to_pull have
 *	been copied and are ready to be delivered. ]
 *
 *  E - Messages that have been copied and delivered, but have not yet been
 *	acknowledged by the recipient as having been received.
 *
 *  F - Messages that have been acknowledged, but XPC has not yet notified the
 *	sender that the message was received by its intended recipient.
 *	This is also an area that needs to be prepared for the allocating of
 *	new messages, by the clearing of the message flags of the acknowledged
 *	messages.
 */
struct xpc_channel {
	short partid;		/* ID of remote partition connected */
	spinlock_t lock;	/* lock for updating this structure */
	u32 flags;		/* general flags */

	enum xp_retval reason;	/* reason why channel is disconnect'g */
	int reason_line;	/* line# disconnect initiated from */

	u16 number;		/* channel # */

	u16 msg_size;		/* sizeof each msg entry */
	u16 local_nentries;	/* #of msg entries in local msg queue */
	u16 remote_nentries;	/* #of msg entries in remote msg queue */

	void *local_msgqueue_base;	/* base address of kmalloc'd space */
	struct xpc_msg *local_msgqueue;	/* local message queue */
	void *remote_msgqueue_base;	/* base address of kmalloc'd space */
	struct xpc_msg *remote_msgqueue;	/* cached copy of remote partition's */
	/* local message queue */
	u64 remote_msgqueue_pa;	/* phys addr of remote partition's */
	/* local message queue */

	atomic_t references;	/* #of external references to queues */

	atomic_t n_on_msg_allocate_wq;	/* #on msg allocation wait queue */
	wait_queue_head_t msg_allocate_wq;	/* msg allocation wait queue */

	u8 delayed_IPI_flags;	/* IPI flags received, but delayed */
	/* action until channel disconnected */

	/* queue of msg senders who want to be notified when msg received */

	atomic_t n_to_notify;	/* #of msg senders to notify */
	struct xpc_notify *notify_queue;	/* notify queue for messages sent */

	xpc_channel_func func;	/* user's channel function */
	void *key;		/* pointer to user's key */

	struct mutex msg_to_pull_mutex;	/* next msg to pull serialization */
	struct completion wdisconnect_wait;	/* wait for channel disconnect */

	struct xpc_openclose_args *local_openclose_args;	/* args passed on */
	/* opening or closing of channel */

	/* various flavors of local and remote Get/Put values */

	struct xpc_gp *local_GP;	/* local Get/Put values */
	struct xpc_gp remote_GP;	/* remote Get/Put values */
	struct xpc_gp w_local_GP;	/* working local Get/Put values */
	struct xpc_gp w_remote_GP;	/* working remote Get/Put values */
	s64 next_msg_to_pull;	/* Put value of next msg to pull */

	/* kthread management related fields */

	atomic_t kthreads_assigned;	/* #of kthreads assigned to channel */
	u32 kthreads_assigned_limit;	/* limit on #of kthreads assigned */
	atomic_t kthreads_idle;	/* #of kthreads idle waiting for work */
	u32 kthreads_idle_limit;	/* limit on #of kthreads idle */
	atomic_t kthreads_active;	/* #of kthreads actively working */

	wait_queue_head_t idle_wq;	/* idle kthread wait queue */

} ____cacheline_aligned;

/* struct xpc_channel flags */

#define	XPC_C_WASCONNECTED	0x00000001	/* channel was connected */

#define	XPC_C_ROPENREPLY	0x00000002	/* remote open channel reply */
#define	XPC_C_OPENREPLY		0x00000004	/* local open channel reply */
#define	XPC_C_ROPENREQUEST	0x00000008	/* remote open channel request */
#define	XPC_C_OPENREQUEST	0x00000010	/* local open channel request */

#define	XPC_C_SETUP		0x00000020	/* channel's msgqueues are alloc'd */
#define	XPC_C_CONNECTEDCALLOUT	0x00000040	/* connected callout initiated */
#define	XPC_C_CONNECTEDCALLOUT_MADE \
				0x00000080	/* connected callout completed */
#define	XPC_C_CONNECTED		0x00000100	/* local channel is connected */
#define	XPC_C_CONNECTING	0x00000200	/* channel is being connected */

#define	XPC_C_RCLOSEREPLY	0x00000400	/* remote close channel reply */
#define	XPC_C_CLOSEREPLY	0x00000800	/* local close channel reply */
#define	XPC_C_RCLOSEREQUEST	0x00001000	/* remote close channel request */
#define	XPC_C_CLOSEREQUEST	0x00002000	/* local close channel request */

#define	XPC_C_DISCONNECTED	0x00004000	/* channel is disconnected */
#define	XPC_C_DISCONNECTING	0x00008000	/* channel is being disconnected */
#define	XPC_C_DISCONNECTINGCALLOUT \
				0x00010000	/* disconnecting callout initiated */
#define	XPC_C_DISCONNECTINGCALLOUT_MADE \
				0x00020000	/* disconnecting callout completed */
#define	XPC_C_WDISCONNECT	0x00040000	/* waiting for channel disconnect */

/*
 * Manages channels on a partition basis. There is one of these structures
 * for each partition (a partition will never utilize the structure that
 * represents itself).
 */
struct xpc_partition {

	/* XPC HB infrastructure */

	u8 remote_rp_version;	/* version# of partition's rsvd pg */
	short remote_npartitions;	/* value of XPC_NPARTITIONS */
	u32 flags;		/* general flags */
	struct timespec remote_rp_stamp;	/* time when rsvd pg was initialized */
	u64 remote_rp_pa;	/* phys addr of partition's rsvd pg */
	u64 remote_vars_pa;	/* phys addr of partition's vars */
	u64 remote_vars_part_pa;	/* phys addr of partition's vars part */
	u64 last_heartbeat;	/* HB at last read */
	u64 remote_amos_page_pa;	/* phys addr of partition's amos page */
	int remote_act_nasid;	/* active part's act/deact nasid */
	int remote_act_phys_cpuid;	/* active part's act/deact phys cpuid */
	u32 act_IRQ_rcvd;	/* IRQs since activation */
	spinlock_t lock;	/* protect updating of act_state and */
	/* the general flags */
	u8 act_state;		/* from XPC HB viewpoint */
	u8 remote_vars_version;	/* version# of partition's vars */
	enum xp_retval reason;	/* reason partition is deactivating */
	int reason_line;	/* line# deactivation initiated from */
	int reactivate_nasid;	/* nasid in partition to reactivate */

	unsigned long disengage_request_timeout;	/* timeout in jiffies */
	struct timer_list disengage_request_timer;

	/* XPC infrastructure referencing and teardown control */

	volatile u8 setup_state;	/* infrastructure setup state */
	wait_queue_head_t teardown_wq;	/* kthread waiting to teardown infra */
	atomic_t references;	/* #of references to infrastructure */

	/*
	 * NONE OF THE PRECEDING FIELDS OF THIS STRUCTURE WILL BE CLEARED WHEN
	 * XPC SETS UP THE NECESSARY INFRASTRUCTURE TO SUPPORT CROSS PARTITION
	 * COMMUNICATION. ALL OF THE FOLLOWING FIELDS WILL BE CLEARED. (THE
	 * 'nchannels' FIELD MUST BE THE FIRST OF THE FIELDS TO BE CLEARED.)
	 */

	u8 nchannels;		/* #of defined channels supported */
	atomic_t nchannels_active;	/* #of channels that are not DISCONNECTED */
	atomic_t nchannels_engaged;	/* #of channels engaged with remote part */
	struct xpc_channel *channels;	/* array of channel structures */

	void *local_GPs_base;	/* base address of kmalloc'd space */
	struct xpc_gp *local_GPs;	/* local Get/Put values */
	void *remote_GPs_base;	/* base address of kmalloc'd space */
	struct xpc_gp *remote_GPs;	/* copy of remote partition's local Get/Put */
	/* values */
	u64 remote_GPs_pa;	/* phys address of remote partition's local */
	/* Get/Put values */

	/* fields used to pass args when opening or closing a channel */

	void *local_openclose_args_base;	/* base address of kmalloc'd space */
	struct xpc_openclose_args *local_openclose_args;	/* local's args */
	void *remote_openclose_args_base;	/* base address of kmalloc'd space */
	struct xpc_openclose_args *remote_openclose_args;	/* copy of remote's */
	/* args */
	u64 remote_openclose_args_pa;	/* phys addr of remote's args */

	/* IPI sending, receiving and handling related fields */

	int remote_IPI_nasid;	/* nasid of where to send IPIs */
	int remote_IPI_phys_cpuid;	/* phys CPU ID of where to send IPIs */
	u64 *remote_IPI_amo_va;	/* address of remote IPI AMO variable */

	u64 *local_IPI_amo_va;	/* address of IPI AMO variable */
	u64 local_IPI_amo;	/* IPI amo flags yet to be handled */
	char IPI_owner[8];	/* IPI owner's name */
	struct timer_list dropped_IPI_timer;	/* dropped IPI timer */

	spinlock_t IPI_lock;	/* IPI handler lock */

	/* channel manager related fields */

	atomic_t channel_mgr_requests;	/* #of requests to activate chan mgr */
	wait_queue_head_t channel_mgr_wq;	/* channel mgr's wait queue */

} ____cacheline_aligned;

/* struct xpc_partition flags */

#define	XPC_P_RAMOSREGISTERED	0x00000001	/* remote AMOs were registered */

/* struct xpc_partition act_state values (for XPC HB) */

#define	XPC_P_AS_INACTIVE	0x00	/* partition is not active */
#define XPC_P_AS_ACTIVATION_REQ	0x01	/* created thread to activate */
#define XPC_P_AS_ACTIVATING	0x02	/* activation thread started */
#define XPC_P_AS_ACTIVE		0x03	/* xpc_partition_up() was called */
#define XPC_P_AS_DEACTIVATING	0x04	/* partition deactivation initiated */

#define XPC_DEACTIVATE_PARTITION(_p, _reason) \
			xpc_deactivate_partition(__LINE__, (_p), (_reason))

/* struct xpc_partition setup_state values */

#define XPC_P_SS_UNSET		0x00	/* infrastructure was never setup */
#define XPC_P_SS_SETUP		0x01	/* infrastructure is setup */
#define XPC_P_SS_WTEARDOWN	0x02	/* waiting to teardown infrastructure */
#define XPC_P_SS_TORNDOWN	0x03	/* infrastructure is torndown */

/*
 * struct xpc_partition IPI_timer #of seconds to wait before checking for
 * dropped IPIs. These occur whenever an IPI amo write doesn't complete until
 * after the IPI was received.
 */
#define XPC_DROPPED_IPI_WAIT_INTERVAL	(0.25 * HZ)

/* number of seconds to wait for other partitions to disengage */
#define XPC_DISENGAGE_REQUEST_DEFAULT_TIMELIMIT	90

/* interval in seconds to print 'waiting disengagement' messages */
#define XPC_DISENGAGE_PRINTMSG_INTERVAL		10

#define XPC_PARTID(_p)	((short) ((_p) - &xpc_partitions[0]))

/* found in xp_main.c */
extern struct xpc_registration xpc_registrations[];

/* found in xpc_main.c */
extern struct device *xpc_part;
extern struct device *xpc_chan;
extern int xpc_disengage_request_timelimit;
extern int xpc_disengage_request_timedout;
extern irqreturn_t xpc_notify_IRQ_handler(int, void *);
extern void xpc_dropped_IPI_check(struct xpc_partition *);
extern void xpc_activate_partition(struct xpc_partition *);
extern void xpc_activate_kthreads(struct xpc_channel *, int);
extern void xpc_create_kthreads(struct xpc_channel *, int, int);
extern void xpc_disconnect_wait(int);

/* found in xpc_partition.c */
extern int xpc_exiting;
extern struct xpc_vars *xpc_vars;
extern struct xpc_rsvd_page *xpc_rsvd_page;
extern struct xpc_vars_part *xpc_vars_part;
extern struct xpc_partition xpc_partitions[XP_NPARTITIONS + 1];
extern char *xpc_remote_copy_buffer;
extern void *xpc_remote_copy_buffer_base;
extern void *xpc_kmalloc_cacheline_aligned(size_t, gfp_t, void **);
extern struct xpc_rsvd_page *xpc_rsvd_page_init(void);
extern int xpc_identify_act_IRQ_sender(void);
extern int xpc_partition_disengaged(struct xpc_partition *);
extern enum xp_retval xpc_mark_partition_active(struct xpc_partition *);
extern void xpc_deactivate_partition(const int, struct xpc_partition *,
				     enum xp_retval);
extern void xpc_mark_partition_inactive(struct xpc_partition *);
extern enum xp_retval xpc_register_remote_amos(struct xpc_partition *);
extern void xpc_unregister_remote_amos(struct xpc_partition *);
extern void xpc_discovery(void);
extern void xpc_check_remote_hb(void);
extern enum xp_retval xpc_initiate_partid_to_nasids(short, void *);

/* found in xpc_channel.c */
extern void xpc_initiate_connect(int);
extern void xpc_initiate_disconnect(int);
extern enum xp_retval xpc_initiate_allocate(short, int, u32, void **);
extern enum xp_retval xpc_initiate_send(short, int, void *);
extern enum xp_retval xpc_initiate_send_notify(short, int, void *,
					       xpc_notify_func, void *);
extern void xpc_initiate_received(short, int, void *);
extern enum xp_retval xpc_setup_infrastructure(struct xpc_partition *);
extern enum xp_retval xpc_pull_remote_vars_part(struct xpc_partition *);
extern void xpc_process_channel_activity(struct xpc_partition *);
extern void xpc_connected_callout(struct xpc_channel *);
extern void xpc_deliver_msg(struct xpc_channel *);
extern void xpc_disconnect_channel(const int, struct xpc_channel *,
				   enum xp_retval, unsigned long *);
extern void xpc_disconnect_callout(struct xpc_channel *, enum xp_retval);
extern void xpc_partition_going_down(struct xpc_partition *, enum xp_retval);
extern void xpc_teardown_infrastructure(struct xpc_partition *);

static inline void
xpc_wakeup_channel_mgr(struct xpc_partition *part)
{
	if (atomic_inc_return(&part->channel_mgr_requests) == 1) {
		wake_up(&part->channel_mgr_wq);
	}
}

/*
 * These next two inlines are used to keep us from tearing down a channel's
 * msg queues while a thread may be referencing them.
 */
static inline void
xpc_msgqueue_ref(struct xpc_channel *ch)
{
	atomic_inc(&ch->references);
}

static inline void
xpc_msgqueue_deref(struct xpc_channel *ch)
{
	s32 refs = atomic_dec_return(&ch->references);

	DBUG_ON(refs < 0);
	if (refs == 0) {
		xpc_wakeup_channel_mgr(&xpc_partitions[ch->partid]);
	}
}

#define XPC_DISCONNECT_CHANNEL(_ch, _reason, _irqflgs) \
		xpc_disconnect_channel(__LINE__, _ch, _reason, _irqflgs)

/*
 * These two inlines are used to keep us from tearing down a partition's
 * setup infrastructure while a thread may be referencing it.
 */
static inline void
xpc_part_deref(struct xpc_partition *part)
{
	s32 refs = atomic_dec_return(&part->references);

	DBUG_ON(refs < 0);
	if (refs == 0 && part->setup_state == XPC_P_SS_WTEARDOWN) {
		wake_up(&part->teardown_wq);
	}
}

static inline int
xpc_part_ref(struct xpc_partition *part)
{
	int setup;

	atomic_inc(&part->references);
	setup = (part->setup_state == XPC_P_SS_SETUP);
	if (!setup) {
		xpc_part_deref(part);
	}
	return setup;
}

/*
 * The following macro is to be used for the setting of the reason and
 * reason_line fields in both the struct xpc_channel and struct xpc_partition
 * structures.
 */
#define XPC_SET_REASON(_p, _reason, _line) \
	{ \
		(_p)->reason = _reason; \
		(_p)->reason_line = _line; \
	}

/*
 * This next set of inlines are used to keep track of when a partition is
 * potentially engaged in accessing memory belonging to another partition.
 */

static inline void
xpc_mark_partition_engaged(struct xpc_partition *part)
{
	u64 *amo_va = __va(part->remote_amos_page_pa +
			   (xpc_engaged_partitions_amos
			    (part->remote_npartitions) +
			    BIT_WORD(xp_partition_id)) * xp_sizeof_amo);

	/* set bit corresponding to our partid in remote partition's AMO */
	(void)xp_set_amo(amo_va, XP_AMO_OR, BIT_MASK(xp_partition_id), 1);
}

static inline void
xpc_mark_partition_disengaged(struct xpc_partition *part)
{
	u64 *amo_va = __va(part->remote_amos_page_pa +
			   (xpc_engaged_partitions_amos
			    (part->remote_npartitions) +
			    BIT_WORD(xp_partition_id)) * xp_sizeof_amo);

	/* clear bit corresponding to our partid in remote partition's AMO */
	(void)xp_set_amo(amo_va, XP_AMO_AND, ~BIT_MASK(xp_partition_id), 1);
}

static inline void
xpc_request_partition_disengage(struct xpc_partition *part)
{
	u64 *amo_va = __va(part->remote_amos_page_pa +
			   (xpc_disengage_request_amos(part->remote_npartitions)
			    + BIT_WORD(xp_partition_id)) * xp_sizeof_amo);

	/* set bit corresponding to our partid in remote partition's AMO */
	(void)xp_set_amo(amo_va, XP_AMO_OR, BIT_MASK(xp_partition_id), 1);
}

static inline void
xpc_cancel_partition_disengage_request(struct xpc_partition *part)
{
	u64 *amo_va = __va(part->remote_amos_page_pa +
			   (xpc_disengage_request_amos(part->remote_npartitions)
			    + BIT_WORD(xp_partition_id)) * xp_sizeof_amo);

	/* clear bit corresponding to our partid in remote partition's AMO */
	(void)xp_set_amo(amo_va, XP_AMO_AND, ~BIT_MASK(xp_partition_id), 1);
}

static inline int
xpc_any_partition_engaged(void)
{
	enum xp_retval ret;
	int w_index;
	u64 *amo_va = (u64 *)((u64)xpc_vars->amos_page +
			      xpc_engaged_partitions_amos(xpc_vars->
							  npartitions) *
			      xp_sizeof_amo);
	u64 amo;

	for (w_index = 0; w_index < xp_partid_mask_words(xpc_vars->npartitions);
	     w_index++) {
		ret = xp_get_amo(amo_va, XP_AMO_LOAD, &amo);
		BUG_ON(ret != xpSuccess);	/* should never happen */
		if (amo != 0)
			return 1;

		amo_va = (u64 *)((u64)amo_va + xp_sizeof_amo);
	}
	return 0;
}

static inline u64
xpc_partition_engaged(short partid)
{
	enum xp_retval ret;
	u64 *amo_va = (u64 *)((u64)xpc_vars->amos_page +
			      (xpc_engaged_partitions_amos
			       (xpc_vars->npartitions) +
			       BIT_WORD(partid)) * xp_sizeof_amo);
	u64 amo;

	/* return our partition's AMO variable ANDed with partid mask */
	ret = xp_get_amo(amo_va, XP_AMO_LOAD, &amo);
	BUG_ON(ret != xpSuccess);	/* should never happen */
	return (amo & BIT_MASK(partid));
}

static inline u64
xpc_partition_disengage_requested(short partid)
{
	enum xp_retval ret;
	u64 *amo_va = (u64 *)((u64)xpc_vars->amos_page +
			      (xpc_disengage_request_amos
			       (xpc_vars->npartitions) +
			       BIT_WORD(partid)) * xp_sizeof_amo);
	u64 amo;

	/* return our partition's AMO variable ANDed with partid mask */
	ret = xp_get_amo(amo_va, XP_AMO_LOAD, &amo);
	BUG_ON(ret != xpSuccess);	/* should never happen */
	return (amo & BIT_MASK(partid));
}

static inline void
xpc_clear_partition_engaged(short partid)
{
	enum xp_retval ret;
	u64 *amo_va = (u64 *)((u64)xpc_vars->amos_page +
			      (xpc_engaged_partitions_amos
			       (xpc_vars->npartitions) +
			       BIT_WORD(partid)) * xp_sizeof_amo);

	/* clear bit corresponding to partid in our partition's AMO */
	ret = xp_set_amo(amo_va, XP_AMO_AND, ~BIT_MASK(partid), 0);
	BUG_ON(ret != xpSuccess);	/* should never happen */
}

static inline void
xpc_clear_partition_disengage_request(short partid)
{
	enum xp_retval ret;
	u64 *amo_va = (u64 *)((u64)xpc_vars->amos_page +
			      (xpc_disengage_request_amos
			       (xpc_vars->npartitions) +
			       BIT_WORD(partid)) * xp_sizeof_amo);

	/* clear bit corresponding to partid in our partition's AMO */
	ret = xp_set_amo(amo_va, XP_AMO_AND, ~BIT_MASK(partid), 0);
	BUG_ON(ret != xpSuccess);	/* should never happen */
}

/*
 * The following set of macros and inlines are used for the sending and
 * receiving of IPIs (also known as IRQs). There are two flavors of IPIs,
 * one that is associated with partition activity (SGI_XPC_ACTIVATE) and
 * the other that is associated with channel activity (SGI_XPC_NOTIFY).
 */

/*
 * IPIs associated with SGI_XPC_ACTIVATE IRQ.
 */

/*
 * Flag the appropriate AMO variable and send an IPI to the specified node.
 */
static inline void
xpc_activate_IRQ_send(u64 amos_page_pa, int from_nasid, int to_nasid,
		      int to_phys_cpuid, int remote_amo, int npartitions)
{
	enum xp_retval ret;
	/* SN nodes are always even numbered nasids */
	u64 *amo_va = (u64 *)__va(amos_page_pa +
				  (xpc_activate_irq_amos(npartitions) +
				   BIT_WORD(from_nasid / 2)) * xp_sizeof_amo);

	ret = xp_set_amo_with_interrupt(amo_va, XP_AMO_OR,
					BIT_MASK(from_nasid / 2),
					remote_amo, to_nasid,
					to_phys_cpuid, SGI_XPC_ACTIVATE);
	BUG_ON(!remote_amo && ret != xpSuccess);	/* should never happen */
}

static inline void
xpc_IPI_send_activate(struct xpc_vars *vars)
{
	xpc_activate_IRQ_send(vars->amos_page_pa, xp_node_to_nasid(0),
			      vars->act_nasid, vars->act_phys_cpuid, 1,
			      vars->npartitions);
}

static inline void
xpc_IPI_send_activated(struct xpc_partition *part)
{
	xpc_activate_IRQ_send(part->remote_amos_page_pa, xp_node_to_nasid(0),
			      part->remote_act_nasid,
			      part->remote_act_phys_cpuid, 1,
			      part->remote_npartitions);
}

static inline void
xpc_IPI_send_reactivate(struct xpc_partition *part)
{
	xpc_activate_IRQ_send(xpc_vars->amos_page_pa, part->reactivate_nasid,
			      xpc_vars->act_nasid, xpc_vars->act_phys_cpuid, 0,
			      xpc_vars->npartitions);
}

static inline void
xpc_IPI_send_disengage(struct xpc_partition *part)
{
	xpc_activate_IRQ_send(part->remote_amos_page_pa, xp_node_to_nasid(0),
			      part->remote_act_nasid,
			      part->remote_act_phys_cpuid, 1,
			      part->remote_npartitions);
}

/*
 * IPIs associated with SGI_XPC_NOTIFY IRQ.
 */

/*
 * Send an IPI to the remote partition that is associated with the
 * specified channel.
 */
#define XPC_NOTIFY_IRQ_SEND(_ch, _ipi_f, _irq_f) \
		xpc_notify_IRQ_send(_ch, _ipi_f, #_ipi_f, _irq_f)

static inline void
xpc_notify_IRQ_send(struct xpc_channel *ch, u8 ipi_flag, char *ipi_flag_string,
		    unsigned long *irq_flags)
{
	struct xpc_partition *part = &xpc_partitions[ch->partid];
	enum xp_retval ret;

	if (unlikely(part->act_state == XPC_P_AS_DEACTIVATING))
		return;

	ret = xp_set_amo_with_interrupt(part->remote_IPI_amo_va, XP_AMO_OR,
					((u64)ipi_flag << (ch->number * 8)), 1,
					part->remote_IPI_nasid,
					part->remote_IPI_phys_cpuid,
					SGI_XPC_NOTIFY);
	dev_dbg(xpc_chan, "%s sent to partid=%d, channel=%d, ret=%d\n",
		ipi_flag_string, ch->partid, ch->number, ret);
	if (unlikely(ret != xpSuccess)) {
		if (irq_flags != NULL)
			spin_unlock_irqrestore(&ch->lock, *irq_flags);
		XPC_DEACTIVATE_PARTITION(part, ret);
		if (irq_flags != NULL)
			spin_lock_irqsave(&ch->lock, *irq_flags);
	}
}

/*
 * Make it look like the remote partition, which is associated with the
 * specified channel, sent us an IPI. This faked IPI will be handled
 * by xpc_dropped_IPI_check().
 */
#define XPC_NOTIFY_IRQ_SEND_LOCAL(_ch, _ipi_f) \
		xpc_notify_IRQ_send_local(_ch, _ipi_f, #_ipi_f)

static inline void
xpc_notify_IRQ_send_local(struct xpc_channel *ch, u8 ipi_flag,
			  char *ipi_flag_string)
{
	enum xp_retval ret;
	u64 *amo_va = xpc_partitions[ch->partid].local_IPI_amo_va;

	/* set IPI flag corresponding to channel in partition's local AMO */
	ret =
	    xp_set_amo(amo_va, XP_AMO_OR, ((u64)ipi_flag << (ch->number * 8)),
		       0);
	BUG_ON(ret != xpSuccess);	/* should never happen */

	dev_dbg(xpc_chan, "%s sent local from partid=%d, channel=%d\n",
		ipi_flag_string, ch->partid, ch->number);
}

/*
 * The sending and receiving of IPIs includes the setting of an AMO variable
 * to indicate the reason the IPI was sent. The 64-bit variable is divided
 * up into eight bytes, ordered from right to left. Byte zero pertains to
 * channel 0, byte one to channel 1, and so on. Each byte is described by
 * the following IPI flags.
 */

#define	XPC_IPI_CLOSEREQUEST	0x01
#define	XPC_IPI_CLOSEREPLY	0x02
#define	XPC_IPI_OPENREQUEST	0x04
#define	XPC_IPI_OPENREPLY	0x08
#define	XPC_IPI_MSGREQUEST	0x10

/* given an AMO variable and a channel#, get its associated IPI flags */
#define XPC_GET_IPI_FLAGS(_amo, _c)	((u8) (((_amo) >> ((_c) * 8)) & 0xff))
#define XPC_SET_IPI_FLAGS(_amo, _c, _f)	(_amo) |= ((u64) (_f) << ((_c) * 8))

#define	XPC_ANY_OPENCLOSE_IPI_FLAGS_SET(_amo) ((_amo) & 0x0f0f0f0f0f0f0f0fUL)
#define XPC_ANY_MSG_IPI_FLAGS_SET(_amo)       ((_amo) & 0x1010101010101010UL)

static inline void
xpc_IPI_send_closerequest(struct xpc_channel *ch, unsigned long *irq_flags)
{
	struct xpc_openclose_args *args = ch->local_openclose_args;

	args->reason = ch->reason;

	XPC_NOTIFY_IRQ_SEND(ch, XPC_IPI_CLOSEREQUEST, irq_flags);
}

static inline void
xpc_IPI_send_closereply(struct xpc_channel *ch, unsigned long *irq_flags)
{
	XPC_NOTIFY_IRQ_SEND(ch, XPC_IPI_CLOSEREPLY, irq_flags);
}

static inline void
xpc_IPI_send_openrequest(struct xpc_channel *ch, unsigned long *irq_flags)
{
	struct xpc_openclose_args *args = ch->local_openclose_args;

	args->msg_size = ch->msg_size;
	args->local_nentries = ch->local_nentries;

	XPC_NOTIFY_IRQ_SEND(ch, XPC_IPI_OPENREQUEST, irq_flags);
}

static inline void
xpc_IPI_send_openreply(struct xpc_channel *ch, unsigned long *irq_flags)
{
	struct xpc_openclose_args *args = ch->local_openclose_args;

	args->remote_nentries = ch->remote_nentries;
	args->local_nentries = ch->local_nentries;
	args->local_msgqueue_pa = __pa(ch->local_msgqueue);

	XPC_NOTIFY_IRQ_SEND(ch, XPC_IPI_OPENREPLY, irq_flags);
}

static inline void
xpc_IPI_send_msgrequest(struct xpc_channel *ch)
{
	XPC_NOTIFY_IRQ_SEND(ch, XPC_IPI_MSGREQUEST, NULL);
}

static inline void
xpc_IPI_send_local_msgrequest(struct xpc_channel *ch)
{
	XPC_NOTIFY_IRQ_SEND_LOCAL(ch, XPC_IPI_MSGREQUEST);
}

static inline u64 *
xpc_IPI_init(int index)
{
	enum xp_retval ret;
	u64 *amo_va = (u64 *)((u64)xpc_vars->amos_page + index * xp_sizeof_amo);

	ret = xp_get_amo(amo_va, XP_AMO_CLEAR, NULL);
	BUG_ON(ret != xpSuccess);	/* should never happen */
	return amo_va;
}

/*
 * Check to see if there is any channel activity to/from the specified
 * partition.
 */
static inline void
xpc_check_for_channel_activity(struct xpc_partition *part)
{
	enum xp_retval ret;
	u64 IPI_amo;
	unsigned long irq_flags;

	ret = xp_get_amo(part->local_IPI_amo_va, XP_AMO_CLEAR, &IPI_amo);
	BUG_ON(ret != xpSuccess);	/* should never happen */
	if (IPI_amo == 0) {
		return;
	}

	spin_lock_irqsave(&part->IPI_lock, irq_flags);
	part->local_IPI_amo |= IPI_amo;
	spin_unlock_irqrestore(&part->IPI_lock, irq_flags);

	dev_dbg(xpc_chan, "received IPI from partid=%d, IPI_amo=0x%" U64_ELL
		"x\n", XPC_PARTID(part), IPI_amo);

	xpc_wakeup_channel_mgr(part);
}

#endif /* _DRIVERS_MISC_XP_XPC_H */
