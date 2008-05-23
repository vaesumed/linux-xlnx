/*
 * WUSB devices
 * sysfs bindings
 *
 * Copyright (C) 2007 Intel Corporation
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
 * Get them out of the way...
 */

#include <linux/jiffies.h>
#include <linux/ctype.h>
#include <linux/workqueue.h>
#include "wusbhc.h"

#undef D_LOCAL
#define D_LOCAL 4
#include <linux/uwb/debug.h>

static ssize_t wusb_dev_disconnect_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return scnprintf(buf, PAGE_SIZE,
			 "write a non zero value to this file to "
			 "force disconnect\n");
}

static ssize_t wusb_dev_disconnect_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t size)
{
	struct usb_device *usb_dev;
	struct wusbhc *wusbhc;
	unsigned command;
	u8 port_idx;

	if (sscanf(buf, "%u\n", &command) != 1)
		return -EINVAL;
	if (command == 0)
		return size;
	usb_dev = to_usb_device(dev);
	wusbhc = wusbhc_get_by_usb_dev(usb_dev);
	if (wusbhc == NULL)
		return -ENODEV;

	mutex_lock(&wusbhc->mutex);
	port_idx = wusb_port_no_to_idx(usb_dev->portnum);
	__wusbhc_dev_disable(wusbhc, port_idx);
	mutex_unlock(&wusbhc->mutex);
	wusbhc_put(wusbhc);
	return size;
}
static DEVICE_ATTR(wusb_dev_disconnect, 0644, wusb_dev_disconnect_show,
		   wusb_dev_disconnect_store);

static ssize_t wusb_cdid_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	ssize_t result;
	struct wusb_dev *wusb_dev;

	wusb_dev = wusb_dev_get_by_usb_dev(to_usb_device(dev));
	if (wusb_dev == NULL)
		return -ENODEV;
	result = ckhdid_printf(buf, PAGE_SIZE, &wusb_dev->cdid);
	wusb_dev_put(wusb_dev);
	return result;
}
static DEVICE_ATTR(wusb_cdid, 0444, wusb_cdid_show, NULL);

static ssize_t wusb_dev_cc_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE,
			 "Write\n"
			 "\n"
			 "CHID: [16 hex digits]\n"
			 "CDID: [16 hex digits]\n"
			 "CK: [16 hex digits]\n"
			 "\n"
			 "to this file to force a 4way handshake negotiation\n"
			 "[will renew pair wise and group wise key if "
			 "succesful].\n");
}

static ssize_t wusb_dev_cc_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	int result;
	struct usb_device *usb_dev;
	struct wusbhc *wusbhc;
	struct wusb_ckhdid chid, cdid, ck;

	result = sscanf(buf,
			"CHID: %02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx\n"
			"CDID: %02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx\n"
			"CK: %02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx "
			"%02hhx %02hhx %02hhx %02hhx\n",
			&chid.data[0] , &chid.data[1],
			&chid.data[2] , &chid.data[3],
			&chid.data[4] , &chid.data[5],
			&chid.data[6] , &chid.data[7],
			&chid.data[8] , &chid.data[9],
			&chid.data[10], &chid.data[11],
			&chid.data[12], &chid.data[13],
			&chid.data[14], &chid.data[15],

			&cdid.data[0] , &cdid.data[1],
			&cdid.data[2] , &cdid.data[3],
			&cdid.data[4] , &cdid.data[5],
			&cdid.data[6] , &cdid.data[7],
			&cdid.data[8] , &cdid.data[9],
			&cdid.data[10], &cdid.data[11],
			&cdid.data[12], &cdid.data[13],
			&cdid.data[14], &cdid.data[15],

			&ck.data[0] , &ck.data[1],
			&ck.data[2] , &ck.data[3],
			&ck.data[4] , &ck.data[5],
			&ck.data[6] , &ck.data[7],
			&ck.data[8] , &ck.data[9],
			&ck.data[10], &ck.data[11],
			&ck.data[12], &ck.data[13],
			&ck.data[14], &ck.data[15]);
	if (result != 48) {
		dev_err(dev, "Unrecognized CHID/CDID/CK (need 48 8-bit "
			"hex digits, got only %d)\n", result);
		return -EINVAL;
	}

	usb_dev = to_usb_device(dev);
	wusbhc = wusbhc_get_by_usb_dev(usb_dev);
	if (wusbhc == NULL)
		return -ENODEV;
	result = wusb_dev_4way_handshake(wusbhc, usb_dev->wusb_dev,
					 &chid, &cdid, &ck);
	memset(&chid, 0, sizeof(chid));
	memset(&cdid, 0, sizeof(cdid));
	memset(&ck, 0, sizeof(ck));
	wusbhc_put(wusbhc);
	return result < 0? result : size;
}
static DEVICE_ATTR(wusb_dev_cc, 0644, wusb_dev_cc_show, wusb_dev_cc_store);

static struct attribute *wusb_dev_attrs[] = {
		&dev_attr_wusb_dev_disconnect.attr,
		&dev_attr_wusb_cdid.attr,
		&dev_attr_wusb_dev_cc.attr,
		NULL,
};

static struct attribute_group wusb_dev_attr_group = {
	.name = NULL,	/* we want them in the same directory */
	.attrs = wusb_dev_attrs,
};

int wusb_dev_sysfs_add(struct wusbhc *wusbhc, struct usb_device *usb_dev,
		       struct wusb_dev *wusb_dev)
{
	int result = sysfs_create_group(&usb_dev->dev.kobj,
					&wusb_dev_attr_group);
	struct device *dev = &usb_dev->dev;
	if (result < 0)
		dev_err(dev, "Cannot register WUSB-dev attributes: %d\n",
			result);
	return result;
}

void wusb_dev_sysfs_rm(struct wusb_dev *wusb_dev)
{
	struct usb_device *usb_dev = wusb_dev->usb_dev;
	if (usb_dev)
		sysfs_remove_group(&usb_dev->dev.kobj, &wusb_dev_attr_group);
}
