/*
 * Ultra Wide Band
 * Driver initialization, etc
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
 *
 * Life cycle: FIXME: explain
 *
 *  UWB radio controller:
 *
 *    1. alloc a uwb_rc, zero it
 *    2. call uwb_rc_init() on it to set it up + ops (won't do any
 *       kind of allocation)
 *    3. register (now it is owned by the UWB stack--deregister before
 *       freeing/destroying).
 *    4. It lives on it's own now (UWB stack handles)--when it
 *       disconnects, call unregister()
 *    5. free it.
 *
 *    Make sure you have a reference to the uwb_rc before calling
 *    any of the UWB API functions.
 *
 * TODO:
 *
 * 1. Locking and life cycle management is crappy still. All entry
 *    points to the UWB HCD API assume you have a reference on the
 *    uwb_rc structure and that it won't go away. They mutex lock it
 *    before doing anything.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kdev_t.h>
#include <linux/random.h>
#include <linux/uwb/debug.h>
#include "uwb-internal.h"


/* FIXME: complete these */
struct bus_type uwb_bus = {
	.name = "uwb",
	.match = NULL,		/* match a UWB driver w/ a UWB device */
	.suspend = NULL,
	.resume = NULL,
};


/*
 * The UWB dev driver doesn't do much other than bind the
 * rc->uwb_dev.dev device to a driver (for now).
 *
 * FIXME: this docs suck, make up your mind and fix them
 *
 * We don't really attach to the device beacuse the real device data
 * is the 'struct uwb_dev' that has been created when the device
 * popped up in the radio and was created somewhere else.
 *
 * Note Radio Controllers have another driver here, although they will
 * point also to the uwb_dev.
 */
static int uwb_dev_drv_probe(struct device *dev)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);

	if (&uwb_dev->rc->uwb_dev == uwb_dev) {
		/* This is a RC device, ignore */
		return -ENODEV;
	}
	dev_set_drvdata(dev, uwb_dev);
	return 0;
}

static int uwb_dev_drv_remove(struct device *dev)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);

	if (&uwb_dev->rc->uwb_dev == uwb_dev) {
		/* This is a RC device, ignore */
		WARN_ON(1);
		return -ENODEV;
	}
	dev_set_drvdata(dev, NULL);
	return 0;
}

/*
 * The UWB RC driver doesn't do much other than bind the
 * rc->uwb_dev.dev device to a driver (for now).
 */
static int uwb_rc_drv_probe(struct device *dev)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);

	if (&uwb_dev->rc->uwb_dev != uwb_dev) {
		/* This is not a RC, ignore */
		return -ENODEV;
	}
	dev_set_drvdata(dev, uwb_dev);
	return 0;
}

static int uwb_rc_drv_remove(struct device *dev)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);

	if (&uwb_dev->rc->uwb_dev != uwb_dev) {
		/* This is not a RC, ignore */
		WARN_ON(1);
		return -ENODEV;
	}
	dev_set_drvdata(dev, NULL);
	return 0;
}

static void uwb_gen_drv_shutdown(struct device *dev)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);

	dev_err(dev, "%s: (uwb_dev %p) FIXME: FINISH ME\n", __func__, uwb_dev);
}

static int uwb_gen_drv_suspend(struct device *dev, pm_message_t state)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);

	dev_err(dev, "%s: (uwb_dev %p) FIXME: FINISH ME\n", __func__, uwb_dev);
	return -ENOSYS;
}

static int uwb_gen_drv_resume(struct device *dev)
{
	struct uwb_dev *uwb_dev = to_uwb_dev(dev);

	dev_err(dev, "%s: (uwb_dev %p) FIXME: FINISH ME\n", __func__, uwb_dev);
	return -ENOSYS;
}

static struct device_driver uwb_rc_drv = {
	.name = "uwb-rc",
	.bus = &uwb_bus,
	.owner = THIS_MODULE,
	.probe = uwb_rc_drv_probe,
	.remove = uwb_rc_drv_remove,
	.shutdown = uwb_gen_drv_shutdown,
	.suspend = uwb_gen_drv_suspend,
	.resume = uwb_gen_drv_resume,
};

static struct device_driver uwb_dev_drv = {
	.name = "uwb-dev",
	.bus = &uwb_bus,
	.owner = THIS_MODULE,
	.probe = uwb_dev_drv_probe,
	.remove = uwb_dev_drv_remove,
	.shutdown = uwb_gen_drv_shutdown,
	.suspend = uwb_gen_drv_suspend,
	.resume = uwb_gen_drv_resume,
};


/* UWB stack attributes (or 'global' constants) */


/**
 * If a beacon dissapears for longer than this, then we consider the
 * device who was represented by that beacon to be gone.
 *
 * ECMA-368[17.2.3, last para] establishes that a device must not
 * consider a device to be its neighbour if he doesn't receive a beacon
 * for more than mMaxLostBeacons. mMaxLostBeacons is defined in
 * ECMA-368[17.16] as 3; because we can get only one beacon per
 * superframe, that'd be 3 * 65ms = 195 ~ 200 ms. Let's give it time
 * for jitter and stuff and make it 500 ms.
 */
unsigned long beacon_timeout_ms = 500;

static
ssize_t beacon_timeout_ms_show(struct bus_type *bus, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%lu\n", beacon_timeout_ms);
}

static
ssize_t beacon_timeout_ms_store(struct bus_type *drv,
				const char *buf, size_t size)
{
	unsigned long bt;
	ssize_t result;
	result = sscanf(buf, "%lu", &bt);
	if (result != 1)
		return -EINVAL;
	beacon_timeout_ms = bt;
	return size;
}

static BUS_ATTR(beacon_timeout_ms, 0644,
	 beacon_timeout_ms_show, beacon_timeout_ms_store);


static
struct attribute *uwb_bus_attrs[] = {
	&bus_attr_beacon_timeout_ms.attr,
	NULL,
};

static struct attribute_group uwb_bus_attr_group = {
	.name = NULL,	/* we want them in the same directory */
	.attrs = uwb_bus_attrs,
};


/** Device model classes */
struct class *uwb_rc_class;


static int __init uwb_subsys_init(void)
{
	struct kset *subsys;
	int result = 0;

	if (UWB_BUG_COUNT)
		printk(KERN_INFO "UWB: workarounds enabled for bugs:%s\n",
		       UWB_BUGS_ENABLED);
	result = uwb_est_create();
	if (result < 0) {
		printk(KERN_ERR "uwb: Can't initialize EST subsystem\n");
		goto error_est_init;
	}
	result = bus_register(&uwb_bus);
	if (result < 0)
		goto error_uwb_bus_register;

	subsys = bus_get_kset(&uwb_bus);
	result = sysfs_create_group(&subsys->kobj, &uwb_bus_attr_group);
	if (result < 0) {
		printk(KERN_ERR "uwb: cannot initialize sysfs attributes: %d\n",
		       result);
		goto error_sysfs_init;
	}
	result = driver_register(&uwb_dev_drv);
	if (result < 0)
		goto error_dev_drv_register;
	result = driver_register(&uwb_rc_drv);
	if (result < 0)
		goto error_rc_drv_register;
	uwb_rc_class = class_create(THIS_MODULE, "uwb_rc");	/* RCs */
	if (IS_ERR(uwb_rc_class)) {
		result = PTR_ERR(uwb_rc_class);
		goto error_uwb_rc_class_create;
	}
	uwbd_start();
	return 0;

error_uwb_rc_class_create:
	driver_unregister(&uwb_rc_drv);
error_rc_drv_register:
	driver_unregister(&uwb_dev_drv);
error_dev_drv_register:

	sysfs_remove_group(&subsys->kobj, &uwb_bus_attr_group);
error_sysfs_init:
	bus_unregister(&uwb_bus);
error_uwb_bus_register:
	uwb_est_destroy();
error_est_init:
	return result;
}
module_init(uwb_subsys_init);

static void __exit uwb_subsys_exit(void)
{
	struct kset *subsys;
	uwbd_stop();
	class_destroy(uwb_rc_class);
	subsys = bus_get_kset(&uwb_bus);
	sysfs_remove_group(&subsys->kobj, &uwb_bus_attr_group);
	driver_unregister(&uwb_rc_drv);
	driver_unregister(&uwb_dev_drv);
	bus_unregister(&uwb_bus);
	uwb_est_destroy();
	return;
}
module_exit(uwb_subsys_exit);

MODULE_AUTHOR("Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>");
MODULE_DESCRIPTION("Ultra Wide Band core");
MODULE_LICENSE("GPL");
