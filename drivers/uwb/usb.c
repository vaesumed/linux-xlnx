/*
 * @lket@ignore-file -- We actually don't want this file for upstream :)
 *
 * Ultra Wide Band
 * Misc USB ops
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

#include <linux/usb.h>
#include <linux/device.h>
#include <linux/uwb/compat.h>

#ifdef NEED_USB_DEV_RESET_DELAYED

struct usb_dev_reset_ctx {
	struct work_struct ws;
	struct usb_device  *usb_dev;
};


/*
 * We need to lock the device before resetting, but we may already have the
 * lock. So we try to lock the device, if it fails (returns < 0) then we
 * cannot proceed. If it returned 1 then we acquired the lock here and need
 * to release the lock. If it returned 0 then we already have the lock and
 * we leave it to the piece that acquired the lock to release it.
 */
static
void usb_dev_reset_delayed_task(struct work_struct *ws)
{
	struct usb_dev_reset_ctx *reset_ctx =
		container_of(ws, struct usb_dev_reset_ctx, ws);
	struct device *dev = &reset_ctx->usb_dev->dev;
	int had_to_lock;
	int result = 0;

	WARN_ON(reset_ctx->usb_dev == NULL);
	had_to_lock = usb_lock_device_for_reset(reset_ctx->usb_dev, NULL);
	if (had_to_lock < 0) {
		if (had_to_lock != -ENODEV)	// ignore dissapearance
			dev_err(dev, "Cannot lock device for reset: %d\n",
				had_to_lock);
	} else {
		result = usb_reset_device(reset_ctx->usb_dev);
		if (result < 0 && result != -ENODEV)
			dev_err(dev, "Unable to reset device: %d\n", result);
		if (had_to_lock)
			usb_unlock_device(reset_ctx->usb_dev);
	}
	usb_put_dev(reset_ctx->usb_dev);
	kfree(reset_ctx);
	module_put(THIS_MODULE);
}


/**
 * Schedule a delayed USB device reset
 *
 * @usb_dev: USB device that needs to be reset. We assume you have a
 *           valid reference on it (so it won't dissapear) until we
 *           take another one that we'll own.
 *
 * Allocates a context structure containing a workqueue struct with
 * all the pertinent info; gets a reference to @usb_dev and schedules
 * the call that will be executed later on.
 *
 * NOTE: for use in atomic contexts
 */
static void usb_dev_reset_delayed(struct usb_device *usb_dev)
{
	struct usb_dev_reset_ctx *reset_ctx;
	struct device *dev = &usb_dev->dev;
	reset_ctx = kmalloc(sizeof(*reset_ctx), GFP_ATOMIC);
	if (reset_ctx == NULL) {
		if (printk_ratelimit())
			dev_err(dev, "USB: cannot allocate memory for "
				"delayed device reset\n");
		return;
	}
	if (try_module_get(THIS_MODULE) == 0) {
		kfree(reset_ctx);
		return;
	}
	reset_ctx->usb_dev = usb_dev;
	usb_get_dev(reset_ctx->usb_dev);
	INIT_WORK(&reset_ctx->ws, usb_dev_reset_delayed_task);
	schedule_work(&reset_ctx->ws);
}
EXPORT_SYMBOL_GPL(usb_dev_reset_delayed);

#endif /* #ifdef NEED_USB_DEV_RESET_DELAYED */
