/*
 * Ultra Wide Band
 * Life cycle of devices
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
 * FIXME: docs
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kdev_t.h>
#include <linux/random.h>
#include "uwb-internal.h"

#define D_LOCAL 1
#include <linux/uwb/debug.h>


struct uwb_devs {
	size_t count;
	struct semaphore mutex;
};

#define uwb_devs_INIT(u) {					\
	.count = 0,						\
	.mutex = __SEMAPHORE_INITIALIZER((u)->mutex, 1),	\
}

/*
 * UWB devices known to the system (either physically
 * connected or in radio range).
 */
static struct uwb_devs uwb_devs = uwb_devs_INIT(&uwb_devs);

/* We initialize addresses to 0xff (invalid, as it is bcast) */
static inline void uwb_dev_addr_init(struct uwb_dev_addr *addr)
{
	memset(&addr->data, 0xff, sizeof(addr->data));
}

static inline void uwb_mac_addr_init(struct uwb_mac_addr *addr)
{
	memset(&addr->data, 0xff, sizeof(addr->data));
}

/* @returns !0 if a device @addr is a broadcast address */
static inline int uwb_dev_addr_bcast(const struct uwb_dev_addr *addr)
{
	struct uwb_dev_addr bcast = { .data = { 0xff, 0xff }};
	return !uwb_dev_addr_cmp(addr, &bcast);
}

/*
 * Add callback @new to be called when an event occurs in @rc.
 */
int uwb_notifs_register(struct uwb_rc *rc, struct uwb_notifs_handler *new)
{
	if (down_interruptible(&rc->notifs_chain.mutex))
		return -ERESTARTSYS;
	list_add(&new->list_node, &rc->notifs_chain.list);
	up(&rc->notifs_chain.mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(uwb_notifs_register);

/*
 * Remove event handler (callback)
 */
int uwb_notifs_deregister(struct uwb_rc *rc, struct uwb_notifs_handler *entry)
{
	if (down_interruptible(&rc->notifs_chain.mutex))
		return -ERESTARTSYS;
	list_del(&entry->list_node);
	up(&rc->notifs_chain.mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(uwb_notifs_deregister);

/*
 * Notify all event handlers of a given event on @rc
 *
 * We are called with a valid reference to the device. Obtain another
 * reference before handing off to callback, release on return.
 */
static void uwb_notify(struct uwb_rc *rc, struct uwb_dev *uwb_dev,
		       enum uwb_notifs event)
{
	struct uwb_notifs_handler *handler;
	if (down_interruptible(&rc->notifs_chain.mutex))
		return;
	if (!list_empty(&rc->notifs_chain.list)) {
		uwb_dev_get(uwb_dev);
		list_for_each_entry(handler, &rc->notifs_chain.list,
				    list_node) {
			handler->cb(handler->data, uwb_dev, event);
		}
		uwb_dev_put(uwb_dev);
	}
	up(&rc->notifs_chain.mutex);
}

/*
 * Release the backing device of a uwb_dev that has been dynamically allocated.
 */
static void uwb_dev_sys_release(struct device *dev)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);

	d_fnstart(4, NULL, "(dev %p uwb_dev %p)\n", dev, uwb_dev);
	uwb_bce_put(uwb_dev->bce);
	d_printf(0, &uwb_dev->dev, "uwb_dev %p freed\n", uwb_dev);
	memset(uwb_dev, 0x69, sizeof(*uwb_dev));
	kfree(uwb_dev);
	d_fnend(4, NULL, "(dev %p uwb_dev %p) = void\n", dev, uwb_dev);
}

/*
 * Initialize a UWB device instance
 *
 * Alloc, zero and call this function.
 */
void uwb_dev_init(struct uwb_dev *uwb_dev)
{
	init_MUTEX(&uwb_dev->mutex);
	device_initialize(&uwb_dev->dev);
	uwb_dev->dev.bus = &uwb_bus;
	uwb_dev->dev.release = uwb_dev_sys_release;
	uwb_dev_addr_init(&uwb_dev->dev_addr);
	uwb_mac_addr_init(&uwb_dev->mac_addr);
	bitmap_fill(uwb_dev->streams, UWB_NUM_GLOBAL_STREAMS);
}

static ssize_t uwb_dev_EUI_48_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	char addr[UWB_ADDR_STRSIZE];

	uwb_mac_addr_print(addr, sizeof(addr), &uwb_dev->mac_addr);
	return sprintf(buf, "%s\n", addr);
}
static DEVICE_ATTR(EUI_48, S_IRUGO, uwb_dev_EUI_48_show, NULL);

static ssize_t uwb_dev_DevAddr_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	char addr[UWB_ADDR_STRSIZE];

	uwb_dev_addr_print(addr, sizeof(addr), &uwb_dev->dev_addr);
	return sprintf(buf, "%s\n", addr);
}
static DEVICE_ATTR(DevAddr, S_IRUGO, uwb_dev_DevAddr_show, NULL);

/*
 * Show the BPST of this device.
 *
 * Calculated from the receive time of the device's beacon and it's
 * slot number.
 */
static ssize_t uwb_dev_BPST_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct uwb_beca_e *bce;
	struct uwb_beacon_frame *bf;
	u16 bpst;

	if (&uwb_dev->rc->uwb_dev == uwb_dev)	/* local radio controller */
		return 0;

	bce = uwb_dev->bce;
	mutex_lock(&bce->mutex);
	bf = (struct uwb_beacon_frame *)bce->be->BeaconInfo;
	bpst = bce->be->wBPSTOffset
		- (u16)(bf->Beacon_Slot_Number * UWB_BEACON_SLOT_LENGTH_US);
	mutex_unlock(&bce->mutex);

	return sprintf(buf, "%d\n", bpst);
}
static DEVICE_ATTR(BPST, S_IRUGO, uwb_dev_BPST_show, NULL);

/*
 * Show the IEs a device is beaconing
 *
 * We need to access the beacon cache, so we just lock it really
 * quick, print the IEs and unlock.
 *
 * We have a reference on the cache entry, so that should be
 * quite safe.
 */
static ssize_t uwb_dev_IEs_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t result;
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);

	if (&uwb_dev->rc->uwb_dev == uwb_dev)	/* This is a local radio controller */
		result = uwb_rc_print_IEs(uwb_dev->rc, buf, PAGE_SIZE);
	else
		result = uwb_bce_print_IEs(uwb_dev, uwb_dev->bce,
					   buf, PAGE_SIZE);
	return result;
}
static DEVICE_ATTR(IEs, S_IRUGO | S_IWUSR, uwb_dev_IEs_show, NULL);

static ssize_t uwb_dev_LQE_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct uwb_beca_e *bce = uwb_dev->bce;
	size_t result;

	/* is this a local device? */
	if (bce == NULL)
		return 0;
	mutex_lock(&bce->mutex);
	result = stats_show(&uwb_dev->bce->lqe_stats, buf);
	mutex_unlock(&bce->mutex);
	return result;
}

static ssize_t uwb_dev_LQE_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct uwb_beca_e *bce = uwb_dev->bce;
	ssize_t result;

	/* is this a local device? */
	if (bce == NULL)
		return 0;
	mutex_lock(&bce->mutex);
	result = stats_store(&uwb_dev->bce->lqe_stats, buf, size);
	mutex_unlock(&bce->mutex);
	return result;
}
static DEVICE_ATTR(LQE, S_IRUGO | S_IWUSR, uwb_dev_LQE_show, uwb_dev_LQE_store);

static ssize_t uwb_dev_RSSI_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct uwb_beca_e *bce = uwb_dev->bce;
	size_t result;

	/* is this a local device? */
	if (bce == NULL)
		return 0;
	mutex_lock(&bce->mutex);
	result = stats_show(&uwb_dev->bce->rssi_stats, buf);
	mutex_unlock(&bce->mutex);
	return result;
}

static ssize_t uwb_dev_RSSI_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct uwb_beca_e *bce = uwb_dev->bce;
	ssize_t result;

	/* is this a local device? */
	if (bce == NULL)
		return 0;
	mutex_lock(&bce->mutex);
	result = stats_store(&uwb_dev->bce->rssi_stats, buf, size);
	mutex_unlock(&bce->mutex);
	return result;
}
static DEVICE_ATTR(RSSI, S_IRUGO | S_IWUSR, uwb_dev_RSSI_show, uwb_dev_RSSI_store);


static struct attribute *dev_attrs[] = {
	&dev_attr_EUI_48.attr,
	&dev_attr_DevAddr.attr,
	&dev_attr_BPST.attr,
	&dev_attr_IEs.attr,
	&dev_attr_LQE.attr,
	&dev_attr_RSSI.attr,
	NULL,
};

static struct attribute_group dev_attr_group = {
	.name = "uwb",
	.attrs = dev_attrs,
};

static struct attribute_group *groups[] = {
	&dev_attr_group,
	NULL,
};

/**
 * Device SYSFS registration
 *
 *
 */
static int __uwb_dev_sys_add(struct uwb_dev *uwb_dev, struct device *parent_dev)
{
	int result;
	struct device *dev;

	d_fnstart(4, NULL, "(uwb_dev %p parent_dev %p)\n", uwb_dev, parent_dev);
	BUG_ON(parent_dev == NULL);

	dev = &uwb_dev->dev;
	dev->groups = groups;
	dev->parent = parent_dev;
	dev_set_drvdata(dev, uwb_dev);

	result = device_add(dev);
	d_fnend(4, NULL, "(uwb_dev %p parent_dev %p) = %d\n", uwb_dev, parent_dev, result);
	return result;
}


static void __uwb_dev_sys_rm(struct uwb_dev *uwb_dev)
{
	d_fnstart(4, NULL, "(uwb_dev %p)\n", uwb_dev);
	dev_set_drvdata(&uwb_dev->dev, NULL);
	device_del(&uwb_dev->dev);
	d_fnend(4, NULL, "(uwb_dev %p) = void\n", uwb_dev);
}


/**
 * Register and initialize a new UWB device
 *
 * Did you call uwb_dev_init() on it?
 *
 * @parent_rc: is the parent radio controller who has the link to the
 *             device. When registering the UWB device that is a UWB
 *             Radio Controller, we point back to it.
 *
 * If registering the device that is part of a radio, caller has set
 * rc->uwb_dev->dev. Otherwise it is to be left NULL--a new one will
 * be allocated.
 */
int uwb_dev_add(struct uwb_dev *uwb_dev, struct device *parent_dev,
		struct uwb_rc *parent_rc)
{
	int result;
	struct device *dev;

	BUG_ON(uwb_dev == NULL);
	BUG_ON(parent_dev == NULL);
	BUG_ON(parent_rc == NULL);

	down(&uwb_devs.mutex);
	uwb_devs.count++;
	up(&uwb_devs.mutex);

	down(&uwb_dev->mutex);
	dev = &uwb_dev->dev;
	uwb_dev->rc = parent_rc;
	if (dev->bus_id[0] == 0)	/* radios print their own! */
		uwb_mac_addr_print(dev->bus_id, sizeof(dev->bus_id),
				   &uwb_dev->mac_addr);
	result = __uwb_dev_sys_add(uwb_dev, parent_dev);
	if (result < 0)
		printk(KERN_ERR "UWB: unable to register dev %s with sysfs: %d\n",
		       dev->bus_id, result);
	up(&uwb_dev->mutex);
	return result;
}


void uwb_dev_rm(struct uwb_dev *uwb_dev)
{
	struct device *parent_dev;
	d_fnstart(2, NULL, "(uwb_dev %p)\n", uwb_dev);

	parent_dev = uwb_dev->dev.parent;
	down(&uwb_dev->mutex);
	__uwb_dev_sys_rm(uwb_dev);
	up(&uwb_dev->mutex);
	down(&uwb_devs.mutex);
	uwb_devs.count--;
	up(&uwb_devs.mutex);
	d_fnend(2, NULL, "(uwb_dev %p) = void\n", uwb_dev);
}


static
int __uwb_dev_try_get(struct device *dev, void *__target_uwb_dev)
{
	struct uwb_dev *target_uwb_dev = __target_uwb_dev;
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	if (uwb_dev == target_uwb_dev) {
		uwb_dev_get(uwb_dev);
		return 1;
	}
	else
		return 0;
}


/**
 * Given a UWB device descriptor, validate and refcount it
 *
 * @returns NULL if the device does not exist or is quiescing; the ptr to
 *               it otherwise.
 */
struct uwb_dev *uwb_dev_try_get(struct uwb_dev *uwb_dev)
{
	if (bus_for_each_dev(&uwb_bus, NULL, uwb_dev, __uwb_dev_try_get))
		return uwb_dev;
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(uwb_dev_try_get);


/**
 * Remove a device from the system [grunt for other functions]
 */
int __uwb_dev_offair(struct uwb_dev *uwb_dev, struct uwb_rc *rc)
{
	struct device *dev = &uwb_dev->dev;
	char macbuf[UWB_ADDR_STRSIZE], devbuf[UWB_ADDR_STRSIZE];

	d_fnstart(3, NULL, "(dev %p [uwb_dev %p], uwb_rc %p)\n", dev, uwb_dev, rc);
	uwb_mac_addr_print(macbuf, sizeof(macbuf), &uwb_dev->mac_addr);
	uwb_dev_addr_print(devbuf, sizeof(devbuf), &uwb_dev->dev_addr);
	dev_info(dev, "uwb device (mac %s dev %s) disconnected from %s %s\n",
		 macbuf, devbuf,
		 rc? rc->uwb_dev.dev.parent->bus->name : "n/a",
		 rc? rc->uwb_dev.dev.parent->bus_id : "");
	uwb_dev_rm(uwb_dev);
	uwb_dev_put(uwb_dev);	/* for the creation in _onair() */
	d_fnend(3, NULL, "(dev %p [uwb_dev %p], uwb_rc %p) = 0\n", dev, uwb_dev, rc);
	return 0;
}


/**
 * A device went off the air, clean up after it!
 *
 * This is called by the UWB Daemon (through the beacon purge function
 * uwb_bcn_cache_purge) when it is detected that a device has been in
 * radio silence for a while.
 *
 * If this device is actually a local radio controller we don't need
 * to go through the offair process, as it is not registered as that.
 *
 * NOTE: uwb_bcn_cache.mutex is held!
 */
void uwbd_dev_offair(struct uwb_beca_e *bce)
{
	struct uwb_rc *rc, *lrc;
	struct uwb_dev *uwb_dev;

	uwb_dev = uwb_dev_try_get(bce->uwb_dev);
	if (uwb_dev == NULL)		/* Already gone :) */
		return;
	rc = __uwb_rc_try_get(uwb_dev->rc);
	bce->uwb_dev = NULL;
	if (rc)
		uwb_notify(rc, uwb_dev, UWB_NOTIF_OFFAIR);
	lrc = uwb_rc_get_by_dev(&uwb_dev->dev_addr);
	if (lrc != NULL)		// This device address is a local
		uwb_rc_put(lrc);	// radio controller
	else
		__uwb_dev_offair(uwb_dev, rc);
	if (rc) {
		if (!uwb_bg_joined(rc)) //only us left
			uwb_notify(rc, uwb_dev, UWB_NOTIF_BG_LEAVE);
		__uwb_rc_put(rc);
	}
	uwb_dev_put(uwb_dev);	/* once for us doing a try_get() */
}


/**
 * A device went on the air, start it up!
 *
 * This is called by the UWB Daemon when it is detected that a device
 * has popped up in the radio range of the radio controller.
 *
 * It will just create the freaking device, register the beacon and
 * stuff and yatla, done.
 *
 *
 * NOTE: uwb_beca.mutex is held, bce->mutex is held
 */
void uwbd_dev_onair(struct uwb_rc *rc, struct uwb_beca_e *bce)
{
	int result;
	struct device *dev = &rc->uwb_dev.dev;
	struct uwb_dev *uwb_dev;
	char macbuf[UWB_ADDR_STRSIZE], devbuf[UWB_ADDR_STRSIZE];

	uwb_mac_addr_print(macbuf, sizeof(macbuf), bce->mac_addr);
	uwb_dev_addr_print(devbuf, sizeof(devbuf), &bce->dev_addr);
	uwb_dev = kcalloc(1, sizeof(*uwb_dev), GFP_KERNEL);
	if (uwb_dev == NULL) {
		dev_err(dev, "new device %s: Cannot allocate memory\n",
			macbuf);
		return;
	}
	uwb_dev_init(uwb_dev);		/* This sets refcnt to one, we own it */
	uwb_dev->mac_addr = *bce->mac_addr;
	uwb_dev->dev_addr = bce->dev_addr;
	result = uwb_dev_add(uwb_dev, &rc->uwb_dev.dev, rc);
	if (result < 0) {
		dev_err(dev, "new device %s: cannot instantiate device\n",
			macbuf);
		goto error_dev_add;
	}
	/* plug the beacon cache */
	bce->uwb_dev = uwb_dev;
	uwb_dev->bce = bce;
	uwb_bce_get(bce);		// released in uwb_dev_sys_release()
	dev_info(dev, "uwb device (mac %s dev %s) connected to %s %s\n",
		 macbuf, devbuf, rc->uwb_dev.dev.parent->bus->name,
		 rc->uwb_dev.dev.parent->bus_id);
	if (uwb_bg_joined(rc)) //see other devices
		uwb_notify(rc, uwb_dev, UWB_NOTIF_BG_JOIN);
	uwb_notify(rc, uwb_dev, UWB_NOTIF_ONAIR);
	return;

error_dev_add:
	kfree(uwb_dev);
	return;
}


struct get_by_rc {
	struct uwb_rc *rc;
	struct uwb_dev *found_dev;
};

struct for_each_by_rc {
	struct uwb_rc *rc;
	uwb_dev_for_each_by_rc_f func;
	void *priv;
};

static
int __uwb_dev_get_by_rc(struct device *dev, void *_get_by_rc)
{
	int result;
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct get_by_rc *get_by_rc = _get_by_rc;
	struct uwb_rc *rc = get_by_rc->rc;

	d_fnstart(4, NULL, "(dev %p [uwb_dev %p], uwb_rc %p)\n", dev, uwb_dev, rc);
	if (uwb_dev->rc == rc && &rc->uwb_dev != uwb_dev) {
		uwb_dev_get(uwb_dev);
		get_by_rc->found_dev = uwb_dev;
		result = 1;
	}
	else
		result = 0;
	d_fnend(4, NULL, "(dev %p [uwb_dev %p], uwb_rc %p) = %d\n", dev, uwb_dev, rc, result);
	return result;
}


/**
 * Look up and return validate and refcount a device connected to rc
 *
 * Skips the device that is the radio controller as well.
 *
 * @uwb_dev:   Where to start looking in the device list. If NULL, the
 *             beginning.
 * @rc:        UWB radio controller the device must be child of
 *             [assumed to be properly referenced].
 * @returns NULL if the device does not exist or is quiescing; the ptr to
 *               it otherwise.
 */
struct uwb_dev *uwb_dev_get_by_rc(struct uwb_dev *uwb_dev, struct uwb_rc *rc)
{
	struct get_by_rc get_by_rc = {
		.rc = rc,
	};
	if (bus_for_each_dev(&uwb_bus, uwb_dev? &uwb_dev->dev : NULL,
			     &get_by_rc,__uwb_dev_get_by_rc))
		return get_by_rc.found_dev;
	else
		return NULL;
}
EXPORT_SYMBOL_GPL(uwb_dev_get_by_rc);


/**
 * Iterate over the list of UWB devices, calling a @function on each
 *
 * See docs for bus_for_each()....
 *
 * @start:    device at which to start looking--if NULL, start from the
 *            beginning.
 * @function: function to call.
 * @priv:     data to pass to @function.
 * @returns:  0 if no invocation of function() returned a value
 *            different to zero. That value otherwise.
 */
int uwb_dev_for_each(struct uwb_dev *start, uwb_dev_for_each_f function,
		     void *priv)
{
	return bus_for_each_dev(&uwb_bus, start? &start->dev : NULL,
				priv, function);
}
EXPORT_SYMBOL_GPL(uwb_dev_for_each);

static int __uwb_dev_for_each_by_rc(struct device *dev, void *_for_each_by_rc)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);
	struct for_each_by_rc *for_each_by_rc = _for_each_by_rc;
	struct uwb_rc *rc = for_each_by_rc->rc;
	int result = 0;

	d_fnstart(4, NULL, "(dev %p [uwb_dev %p], uwb_rc %p)\n",
		  dev, uwb_dev, rc);
	if (uwb_dev->rc == rc && &rc->uwb_dev != uwb_dev) {

		result = (*for_each_by_rc->func)(rc, uwb_dev,
				for_each_by_rc->priv);

	}
	d_fnend(4, NULL, "(dev %p [uwb_dev %p], uwb_rc %p) = %d\n",
		dev, uwb_dev, rc, result);
	return result;
}

/**
 * Call @function on all devices connected to given RC, excluding RC self.
 *
 * Iterate over all UWB devices connected to given RC, calling
 * @function on each. The function will not be run on the device that is
 * the RC.
 *
 * @rc:       RC to which uwb device has to be connected
 * @function: function to call
 * @priv:     data to pass to @function
 * @returns:  0 if no invocation of function() returned a value
 *            different to zero. That value otherwise.
 */
int uwb_dev_for_each_by_rc(struct uwb_rc *rc, uwb_dev_for_each_by_rc_f function,
		     void *priv)
{
	int result;
	struct for_each_by_rc for_each_by_rc = {
		.rc = rc,
		.func = function,
		.priv = priv,
	};
	d_fnstart(4, NULL, "(uwb_rc %p, bwa %p)\n", rc, priv);
	result = bus_for_each_dev(&uwb_bus, NULL, &for_each_by_rc,
				  __uwb_dev_for_each_by_rc);
	d_fnend(4, NULL, "(uwb_rc %p bwa %p)\n", rc, priv);
	return result;
}
EXPORT_SYMBOL_GPL(uwb_dev_for_each_by_rc);



/**
 * @returns the number of known UWB devices
 *
 * Non-locking version
 */
int __uwb_dev_get_count(void)
{
	return uwb_devs.count;
}
EXPORT_SYMBOL_GPL(__uwb_dev_get_count);

/**
 * @returns the number of known UWB devices
 *
 * Locking version
 */
int uwb_dev_get_count(void)
{
	int result;
	down(&uwb_devs.mutex);
	result = uwb_devs.count;
	up(&uwb_devs.mutex);
	return result;
}
EXPORT_SYMBOL_GPL(uwb_dev_get_count);
