/*
 * Ultra Wide Band
 * Neighborhood Management Daemon
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * This daemon takes care of maintaing information that describes the
 * UWB neighborhood that the radios in this machine can see. It also
 * keeps a tab of which devices are visible, makes sure each HC sits
 * on a different channel to avoid interfering, etc.
 *
 * Different drivers (radio controller, device, any API in general)
 * communicate with this daemon through an event queue. Daemon wakes
 * up, takes a list of events and handles them one by one; handling
 * function is extracted from a table based on the event's type and
 * subtype. Events are freed only if the handling function says so.
 *
 *   . Lock protecting the event list has to be an spinlock and locked
 *     with IRQSAVE because it might be called from an interrupt
 *     context (ie: when events arrive and the notification drops
 *     down from the ISR).
 *
 *   . UWB radio controller drivers queue events to the daemon using
 *     uwbd_event_queue(). They just get the event, chew it to make it
 *     look like UWBD likes it and pass it in a buffer allocated with
 *     uwb_event_alloc().
 *
 * EVENTS
 *
 * Events have a type, a subtype, a lenght, some other stuff and the
 * data blob, which depends on the event. The header is 'struct
 * uwb_event'; for payloads, see 'struct uwbd_evt_*'.
 *
 * EVENT HANDLER TABLES
 *
 * To find a handling function for an event, the type is used to index
 * a subtype-table in the type-table. The subtype-table is indexed
 * with the subtype to get the function that handles the event. Start
 * with the main type-table 'uwbd_evt_type_handler'.
 *
 * DEVICES
 *
 * Devices are created when a bunch of beacons have been received and
 * it is stablished that the device has stable radio presence. CREATED
 * only, not configured. Devices are ONLY configured when an
 * Application-Specific IE Probe is receieved, in which the device
 * declares which Protocol ID it groks. Then the device is CONFIGURED
 * (and the driver->probe() stuff of the device model is invoked).
 *
 * Devices are considered disconnected when a certain number of
 * beacons are not received in an amount of time.
 *
 * Handler functions are called normally uwbd_evt_handle_*().
 */

#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/freezer.h>
#include "uwb-internal.h"

#define D_LOCAL 1
#include <linux/uwb/debug.h>


/**
 * UWBD Event handler function signature
 *
 * Return !0 if the event needs not to be freed (ie the handler
 * takes/took care of it). 0 means the daemon code will free the
 * event.
 *
 * @evt->rc is already referenced and guaranteed to exist. See
 * uwb_evt_handle().
 */
typedef int (*uwbd_evt_handler_f)(struct uwb_event *);

/**
 * Properties of a UWBD event
 *
 * @handler:    the function that will handle this event
 * @count:      number of instances since startup that this event has been
 *              seen
 * @name:       text name of event
 */
struct uwbd_event {
	uwbd_evt_handler_f handler;
	unsigned long count;
	const char *name;
};

/** Table of handlers for and properties of the UWBD Radio Control Events */
static
struct uwbd_event uwbd_events[] = {
	[UWB_RC_EVT_BEACON] = {
		.handler = uwbd_evt_handle_rc_beacon,
		.count = 0,
		.name = "BEACON_RECEIVED"
	},
	[UWB_RC_EVT_BEACON_SIZE] = {
		.handler = uwbd_evt_handle_rc_beacon_size,
		.count = 0,
		.name = "BEACON_SIZE_CHANGE"
	},
	[UWB_RC_EVT_BPOIE_CHANGE] = {
		.handler = uwbd_evt_handle_rc_bpoie_change,
		.count = 0,
		.name = "BPOIE_CHANGE"
	},
	[UWB_RC_EVT_BP_SLOT_CHANGE] = {
		.handler = uwbd_evt_handle_rc_bp_slot_change,
		.count = 0,
		.name = "BP_SLOT_CHANGE"
	},
	[UWB_RC_EVT_DRP_AVAIL] = {
		.handler = uwbd_evt_handle_rc_drp_avail,
		.count = 0,
		.name = "DRP_AVAILABILITY_CHANGE"
	},
	[UWB_RC_EVT_DRP] = {
		.handler = uwbd_evt_handle_rc_drp,
		.count = 0,
		.name = "DRP"
	},
	[UWB_RC_EVT_DEV_ADDR_CONFLICT] = {
		.handler = uwbd_evt_handle_rc_dev_addr_conflict,
		.count = 0,
		.name = "DEV_ADDR_CONFLICT",
	},
};



struct uwbd_evt_type_handler {
	const char *name;
	struct uwbd_event *uwbd_events;
	size_t size;
};

#define UWBD_EVT_TYPE_HANDLER(n,a) {		\
	.name = (n),				\
	.uwbd_events = (a),			\
	.size = sizeof(a)/sizeof((a)[0])	\
}


/** Table of handlers for each UWBD Event type. */
static
struct uwbd_evt_type_handler uwbd_evt_type_handlers[] = {
	[UWB_RC_CET_GENERAL] = UWBD_EVT_TYPE_HANDLER("RC", uwbd_events)
};

static const
size_t uwbd_evt_type_handlers_len =
	sizeof(uwbd_evt_type_handlers) / sizeof(uwbd_evt_type_handlers[0]);


/**
 * Handle an event passed to the UWB Daemon
 *
 * @evt: the event to handle
 * @returns: 0 if the event can be kfreed, !0 on the contrary
 *           (somebody else took ownership) [coincidentally, returning
 *           a <0 errno code will free it :)].
 *
 * Looks up the two indirection tables (one for the type, one for the
 * subtype) to decide which function handles it and then calls the
 * handler.
 *
 * The event structure passed to the event handler has the radio
 * controller in @evt->rc referenced. The reference will be dropped
 * once the handler returns, so if it needs it for longer (async),
 * it'll need to take another one.
 */
static
int uwbd_event_handle(struct uwb_event *evt)
{
	int result;
	struct uwbd_evt_type_handler *type_table;
	uwbd_evt_handler_f handler;
	struct uwb_rc *rc;
	u8 type, context;
	u16 event;

	rc = evt->rc;
	if (rc == NULL) {
		if (printk_ratelimit())
			printk(KERN_ERR "UWBD: BUG: received event "
			       "with NULL rc\n");
		return -EINVAL;
	}
	result = 0;
	if (!rc->ready)			// Not ready to process stuff, drop it
		goto out_put;
	type = evt->rceb->bEventType;
	event = le16_to_cpu(evt->rceb->wEvent);
	context = evt->rceb->bEventContext;
	result = -EINVAL;
	if (type > uwbd_evt_type_handlers_len) {
		if (printk_ratelimit())
			printk(KERN_ERR "UWBD: event type %u: unknown "
			       "(too high)\n", type);
		goto out_put;
	}
	type_table = &uwbd_evt_type_handlers[type];
	if (type_table->uwbd_events == NULL) {
		if (printk_ratelimit())
			printk(KERN_ERR "UWBD: event type %u: unknown\n", type);
		goto out_put;
	}
	if (event > type_table->size) {
		if (printk_ratelimit())
			printk(KERN_ERR "UWBD: event %s[%u]: "
			       "unknown (too high)\n", type_table->name, event);
		goto out_put;
	}
	handler = type_table->uwbd_events[event].handler;
	if (handler == NULL) {
		if (printk_ratelimit())
			printk(KERN_ERR "UWBD: event %s[%u]: unknown\n",
			       type_table->name, event);
		goto out_put;
	}
	d_printf(3, NULL, "processing 0x%02x/%04x/%02x, %zu bytes\n",
		 type, event, context, evt->size);
	type_table->uwbd_events[event].count++;
	result = (*handler)(evt);
	if (result < 0) {
		if (printk_ratelimit())
			printk(KERN_ERR "UWBD: event 0x%02x/%04x/%02x, "
			       "table %s[%u]: handling failed: %d\n",
			       type, event, context, type_table->name,
			       event, result);
		result = 0;	// make sure uwbd() frees the evt buffer
	}
out_put:
	__uwb_rc_put(rc);	// for the __uwb_rc_get() in uwb_rc_notif_cb()
	return result;
}


/* The UWB Daemon */


/** Daemon's PID: used to decide if we can queue or not */
static int uwbd_pid;
/** Daemon's task struct for managing the kthread */
static struct task_struct * uwbd_task;
/** Daemon's waitqueue for waiting for new events */
static DECLARE_WAIT_QUEUE_HEAD(uwbd_wq);
/** Daemon's list of events; we queue/dequeue here */
static struct list_head uwbd_event_list = LIST_HEAD_INIT(uwbd_event_list);
/** Daemon's list lock to protect concurent access */
static spinlock_t uwbd_event_list_lock = SPIN_LOCK_UNLOCKED;


/**
 * UWB Daemon
 *
 * Listens to all UWB notifications and takes care to track the state
 * of the UWB neighboorhood for the kernel. When we do a run, we
 * spinlock, move the list to a private copy and release the
 * lock. Hold it as little as possible. Not a conflict: it is
 * guaranteed we own the events in the private list.
 *
 * FIXME: should change so we don't have a 1HZ timer all the time, but
 *        only if there are devices.
 */
static int uwbd(void *unused)
{
	unsigned long flags;
	struct list_head list = LIST_HEAD_INIT(list);
	struct uwb_event *evt, *nxt;
	int should_stop = 0, should_keep;
	while (1) {
		wait_event_interruptible_timeout(
			uwbd_wq,
			!list_empty(&uwbd_event_list)
			  || (should_stop = kthread_should_stop()),
			HZ);
		if (should_stop)	// uwbd_stop() cleans up
			break;
		try_to_freeze();
		spin_lock_irqsave(&uwbd_event_list_lock, flags);
		list_splice_init(&uwbd_event_list, &list);
		spin_unlock_irqrestore(&uwbd_event_list_lock, flags);
		list_for_each_entry_safe(evt, nxt, &list, list_node) {
			list_del(&evt->list_node);
			should_keep = uwbd_event_handle(evt);
			if (!should_keep)
				kfree(evt->rceb);
			kfree(evt);
		}
		uwb_beca_purge();	/* Purge devices that left */
	}
	return 0;
}


/** Start the UWB daemon */
void uwbd_start(void)
{
	uwbd_task = kthread_run(uwbd, NULL, "uwbd");
	if (uwbd_task == NULL)
		printk(KERN_ERR "UWB: Cannot start management daemon; "
		       "UWB won't work\n");
	else
		uwbd_pid = uwbd_task->pid;
}

/* Stop the UWB daemon and free any unprocessed events */
void uwbd_stop(void)
{
	unsigned long flags;
	struct uwb_event *evt, *nxt;
	kthread_stop(uwbd_task);
	spin_lock_irqsave(&uwbd_event_list_lock, flags);
	uwbd_pid = 0;
	list_for_each_entry_safe(evt, nxt, &uwbd_event_list, list_node) {
		if (evt->rceb)
			kfree(evt->rceb);
		kfree(evt);
	}
	spin_unlock_irqrestore(&uwbd_event_list_lock, flags);
	uwb_beca_release();
}

/*
 * Queue an event for the management daemon
 *
 * When some lower layer receives an event, it uses this function to
 * push it forward to the UWB daemon.
 *
 * Once you pass the event, you don't own it any more, but the daemon
 * does. It will uwb_event_free() it when done, so make sure you
 * uwb_event_alloc()ed it or bad things will happen.
 *
 * If the daemon is not running, we just free the event.
 */
void uwbd_event_queue(struct uwb_event *evt)
{
	unsigned long flags;
	spin_lock_irqsave(&uwbd_event_list_lock, flags);
	if (uwbd_pid != 0) {
		list_add(&evt->list_node, &uwbd_event_list);
		wake_up_all(&uwbd_wq);
	}
	else {
		if (evt->rceb)
			kfree(evt->rceb);
		kfree(evt);
	}
	spin_unlock_irqrestore(&uwbd_event_list_lock, flags);
	return;
}

/**
 * Print the statistics for all events in a table
 *
 * @returns: the number of characters printed to buffer, 0 if buffer is
 *           full
 */
static size_t uwbd_print_evt_stats(struct uwbd_evt_type_handler *type_table,
				   char *buf, size_t buf_size)
{
	struct uwbd_event *events;
	size_t evtmax;
	unsigned cnt;
	ssize_t result;
	ssize_t org_buf_size = buf_size;

	events = type_table->uwbd_events;
	evtmax = type_table->size;
	for (cnt = 0; cnt < evtmax; cnt++) {
		if (events[cnt].handler != NULL) {
			result = scnprintf(buf, buf_size,
					   "%-5s \t %-25s\t %lu \n",
					   type_table->name,
					   events[cnt].name,
					   events[cnt].count);
			buf_size = result >= buf_size ? 0 :
				buf_size - result;
			if (buf_size > 0)
				buf += result;
		}
	}
	return org_buf_size - buf_size;
}

/**
 * Print the statistics of all notifications from the device.
 *
 * The tables of all event types are traversed to get statistics of every
 * notification handled by the driver.
 */
static ssize_t uwbd_evt_stats_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	unsigned cnt;
	struct uwbd_evt_type_handler *type_table;
	size_t consumed;

	consumed = sprintf(buf, "#EVENT_TYPE EVENT NUM_RECEIVED\n\n");
	for (cnt = 0; cnt < uwbd_evt_type_handlers_len; cnt++) {
		type_table = &uwbd_evt_type_handlers[cnt];
		if (type_table->uwbd_events != NULL && consumed < PAGE_SIZE)
			consumed += uwbd_print_evt_stats(type_table,
							 buf + consumed,
							 PAGE_SIZE - consumed);
	}
	return consumed;
}
DEVICE_ATTR(event_stats, S_IRUGO, uwbd_evt_stats_show, NULL);
