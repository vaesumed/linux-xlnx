/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2004-2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Cross Partition (XP) base.
 *
 *	XP provides a base from which its users can interact
 *	with XPC, yet not be dependent on XPC.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include "xp.h"

/* Define the XP debug device structures to be used with dev_dbg() et al */

struct device_driver xp_dbg_name = {
	.name = "xp"
};

struct device xp_dbg_subname = {
	.bus_id = {0},		/* set to "" */
	.driver = &xp_dbg_name
};

struct device *xp = &xp_dbg_subname;

/*
 * Target of nofault PIO read.
 */
u64 xp_nofault_PIOR_target;

short xp_partition_id;
EXPORT_SYMBOL_GPL(xp_partition_id);
u8 xp_region_size;
EXPORT_SYMBOL_GPL(xp_region_size);
unsigned long xp_rtc_cycles_per_second;
EXPORT_SYMBOL_GPL(xp_rtc_cycles_per_second);

enum xp_retval (*xp_remote_memcpy) (void *dst, const void *src, size_t len);
EXPORT_SYMBOL_GPL(xp_remote_memcpy);

enum xp_retval (*xp_register_remote_amos) (u64 paddr, size_t len);
EXPORT_SYMBOL_GPL(xp_register_remote_amos);
enum xp_retval (*xp_unregister_remote_amos) (u64 paddr, size_t len);
EXPORT_SYMBOL_GPL(xp_unregister_remote_amos);

int xp_sizeof_nasid_mask;
EXPORT_SYMBOL_GPL(xp_sizeof_nasid_mask);
int xp_sizeof_amo;
EXPORT_SYMBOL_GPL(xp_sizeof_amo);

u64 *(*xp_alloc_amos) (int n_amos);
EXPORT_SYMBOL_GPL(xp_alloc_amos);
void (*xp_free_amos) (u64 *amos_page, int n_amos);
EXPORT_SYMBOL_GPL(xp_free_amos);

enum xp_retval (*xp_set_amo) (u64 *amo_va, int op, u64 operand, int remote);
EXPORT_SYMBOL_GPL(xp_set_amo);
enum xp_retval (*xp_set_amo_with_interrupt) (u64 *amo_va, int op, u64 operand,
					     int remote, int nasid,
					     int phys_cpuid, int vector);
EXPORT_SYMBOL_GPL(xp_set_amo_with_interrupt);

enum xp_retval (*xp_get_amo) (u64 *amo_va, int op, u64 *amo_value_addr);
EXPORT_SYMBOL_GPL(xp_get_amo);

enum xp_retval (*xp_get_partition_rsvd_page_pa) (u64 buf, u64 *cookie,
						 u64 *paddr, size_t *len);
EXPORT_SYMBOL_GPL(xp_get_partition_rsvd_page_pa);

enum xp_retval (*xp_change_memprotect) (u64 paddr, size_t len, int request,
					u64 *nasid_array);
EXPORT_SYMBOL_GPL(xp_change_memprotect);
void (*xp_change_memprotect_shub_wars_1_1) (int request);
EXPORT_SYMBOL_GPL(xp_change_memprotect_shub_wars_1_1);
void (*xp_allow_IPI_ops) (void);
EXPORT_SYMBOL_GPL(xp_allow_IPI_ops);
void (*xp_disallow_IPI_ops) (void);
EXPORT_SYMBOL_GPL(xp_disallow_IPI_ops);

int (*xp_cpu_to_nasid) (int cpuid);
EXPORT_SYMBOL_GPL(xp_cpu_to_nasid);
int (*xp_node_to_nasid) (int nid);
EXPORT_SYMBOL_GPL(xp_node_to_nasid);

/*
 * Initialize the XPC interface to indicate that XPC isn't loaded.
 */
static enum xp_retval
xpc_notloaded(void)
{
	return xpNotLoaded;
}

struct xpc_interface xpc_interface = {
	(void (*)(int))xpc_notloaded,
	(void (*)(int))xpc_notloaded,
	(enum xp_retval(*)(short, int, u32, void **))xpc_notloaded,
	(enum xp_retval(*)(short, int, void *))xpc_notloaded,
	(enum xp_retval(*)(short, int, void *, xpc_notify_func, void *))
	    xpc_notloaded,
	(void (*)(short, int, void *))xpc_notloaded,
	(enum xp_retval(*)(short, void *))xpc_notloaded
};
EXPORT_SYMBOL_GPL(xpc_interface);

/*
 * XPC calls this when it (the XPC module) has been loaded.
 */
void
xpc_set_interface(void (*connect) (int),
		  void (*disconnect) (int),
		  enum xp_retval (*allocate) (short, int, u32, void **),
		  enum xp_retval (*send) (short, int, void *),
		  enum xp_retval (*send_notify) (short, int, void *,
						 xpc_notify_func, void *),
		  void (*received) (short, int, void *),
		  enum xp_retval (*partid_to_nasids) (short, void *))
{
	xpc_interface.connect = connect;
	xpc_interface.disconnect = disconnect;
	xpc_interface.allocate = allocate;
	xpc_interface.send = send;
	xpc_interface.send_notify = send_notify;
	xpc_interface.received = received;
	xpc_interface.partid_to_nasids = partid_to_nasids;
}
EXPORT_SYMBOL_GPL(xpc_set_interface);

/*
 * XPC calls this when it (the XPC module) is being unloaded.
 */
void
xpc_clear_interface(void)
{
	xpc_interface.connect = (void (*)(int))xpc_notloaded;
	xpc_interface.disconnect = (void (*)(int))xpc_notloaded;
	xpc_interface.allocate = (enum xp_retval(*)(short, int, u32,
						    void **))xpc_notloaded;
	xpc_interface.send = (enum xp_retval(*)(short, int, void *))
	    xpc_notloaded;
	xpc_interface.send_notify = (enum xp_retval(*)(short, int, void *,
						       xpc_notify_func,
						       void *))xpc_notloaded;
	xpc_interface.received = (void (*)(short, int, void *))
	    xpc_notloaded;
	xpc_interface.partid_to_nasids = (enum xp_retval(*)(short, void *))
	    xpc_notloaded;
}
EXPORT_SYMBOL_GPL(xpc_clear_interface);

/*
 * xpc_registrations[] keeps track of xpc_connect()'s done by the kernel-level
 * users of XPC.
 */
struct xpc_registration xpc_registrations[XPC_NCHANNELS];
EXPORT_SYMBOL_GPL(xpc_registrations);

/*
 * Register for automatic establishment of a channel connection whenever
 * a partition comes up.
 *
 * Arguments:
 *
 *	ch_number - channel # to register for connection.
 *	func - function to call for asynchronous notification of channel
 *	       state changes (i.e., connection, disconnection, error) and
 *	       the arrival of incoming messages.
 *      key - pointer to optional user-defined value that gets passed back
 *	      to the user on any callouts made to func.
 *	payload_size - size in bytes of the XPC message's payload area which
 *		       contains a user-defined message. The user should make
 *		       this large enough to hold their largest message.
 *	nentries - max #of XPC message entries a message queue can contain.
 *		   The actual number, which is determined when a connection
 * 		   is established and may be less then requested, will be
 *		   passed to the user via the xpConnected callout.
 *	assigned_limit - max number of kthreads allowed to be processing
 * 			 messages (per connection) at any given instant.
 *	idle_limit - max number of kthreads allowed to be idle at any given
 * 		     instant.
 */
enum xp_retval
xpc_connect(int ch_number, xpc_channel_func func, void *key, u16 payload_size,
	    u16 nentries, u32 assigned_limit, u32 idle_limit)
{
	struct xpc_registration *registration;

	DBUG_ON(ch_number < 0 || ch_number >= XPC_NCHANNELS);
	DBUG_ON(payload_size == 0 || nentries == 0);
	DBUG_ON(func == NULL);
	DBUG_ON(assigned_limit == 0 || idle_limit > assigned_limit);

	registration = &xpc_registrations[ch_number];

	if (mutex_lock_interruptible(&registration->mutex) != 0)
		return xpInterrupted;

	/* if XPC_CHANNEL_REGISTERED(ch_number) */
	if (registration->func != NULL) {
		mutex_unlock(&registration->mutex);
		return xpAlreadyRegistered;
	}

	/* register the channel for connection */
	registration->msg_size = XPC_MSG_SIZE(payload_size);
	registration->nentries = nentries;
	registration->assigned_limit = assigned_limit;
	registration->idle_limit = idle_limit;
	registration->key = key;
	registration->func = func;

	mutex_unlock(&registration->mutex);

	xpc_interface.connect(ch_number);

	return xpSuccess;
}
EXPORT_SYMBOL_GPL(xpc_connect);

/*
 * Remove the registration for automatic connection of the specified channel
 * when a partition comes up.
 *
 * Before returning this xpc_disconnect() will wait for all connections on the
 * specified channel have been closed/torndown. So the caller can be assured
 * that they will not be receiving any more callouts from XPC to their
 * function registered via xpc_connect().
 *
 * Arguments:
 *
 *	ch_number - channel # to unregister.
 */
void
xpc_disconnect(int ch_number)
{
	struct xpc_registration *registration;

	DBUG_ON(ch_number < 0 || ch_number >= XPC_NCHANNELS);

	registration = &xpc_registrations[ch_number];

	/*
	 * We've decided not to make this a down_interruptible(), since we
	 * figured XPC's users will just turn around and call xpc_disconnect()
	 * again anyways, so we might as well wait, if need be.
	 */
	mutex_lock(&registration->mutex);

	/* if !XPC_CHANNEL_REGISTERED(ch_number) */
	if (registration->func == NULL) {
		mutex_unlock(&registration->mutex);
		return;
	}

	/* remove the connection registration for the specified channel */
	registration->func = NULL;
	registration->key = NULL;
	registration->nentries = 0;
	registration->msg_size = 0;
	registration->assigned_limit = 0;
	registration->idle_limit = 0;

	xpc_interface.disconnect(ch_number);

	mutex_unlock(&registration->mutex);

	return;
}
EXPORT_SYMBOL_GPL(xpc_disconnect);

int __init
xp_init(void)
{
	enum xp_retval ret;
	int ch_number;

	if (is_shub())
		ret = xp_init_sn2();
	else if (is_uv())
		ret = xp_init_uv();
	else
		ret = xpUnsupported;

	if (ret != xpSuccess)
		return -ENODEV;

	/* initialize the connection registration mutex */
	for (ch_number = 0; ch_number < XPC_NCHANNELS; ch_number++)
		mutex_init(&xpc_registrations[ch_number].mutex);

	return 0;
}

module_init(xp_init);

void __exit
xp_exit(void)
{
	if (is_shub())
		xp_exit_sn2();
	else if (is_uv())
		xp_exit_uv();
}

module_exit(xp_exit);

MODULE_AUTHOR("Silicon Graphics, Inc.");
MODULE_DESCRIPTION("Cross Partition (XP) base");
MODULE_LICENSE("GPL");
