/*
 * UWB Stack
 * Compatibility with older Linux kernels
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * NOTE: These copyright notices apply only to code which was
 *       originally created, not the ones that were copied from the
 *       Linux kernel to provide compatibility. Those retain their
 *       originating license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 Samyoung Electronics. All rights reserved.
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 *
 * BSD LICENSE
 *
 * Copyright(c) 2007 Samyoung Electronics. All rights reserved.
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation, nor Samyoung
 *     Electronics, nor the names of its contributors may be used to
 *     endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Compatibility layer for Linux UWB vs. other Linux kernel
 * versions. We try to keep this down to a minimum. Much of this is
 * stolen from the newer kernels we try to emulate.
 *
 *
 * (C) 2007 JaeHee Kim <max27@samyoung.co.kr>
 *
 *     First implementation
 *
 * (C) 2007 Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 *     Cleanups and breakups in compat blocks per each kernel version
 *     (approximated) and other dirty hacks and additions to make it
 *     happen.
 *
 * (C) Others
 *
 *     Snippets from the Linux kernel.
 *
 *
 * @lket@ignore-file -- We actually don't want this file for upstream :)
 */
#include <linux/version.h>
#include <linux/device.h>
#include <linux/uwb/compat.h>
#include "uwb-internal.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,13)
#include <stdarg.h>

struct class_device * class_device_create(
	struct class_simple *cs, void *dummy, dev_t dev,
	struct device *device, const char *fmt, ...)
{
	struct class_device *csd;
	char name[64];
	va_list vargs;

	va_start(vargs, fmt);
	vsnprintf(name, sizeof(name), fmt, vargs);
	va_end(vargs);
	down(&uwb_rc_class_sem);
	csd = class_simple_device_add(cs, dev, device, fmt);
	up(&uwb_rc_class_sem);
	return csd;
}
EXPORT_SYMBOL_GPL(class_device_create);
#endif	/* Linux kernel < 2.6.12 */


#ifdef NEED_BITMAP_COPY_LE
/**
 * bitmap_copy_le - copy a bitmap, putting the bits into little-endian order.
 * @dst:   destination bitmap.
 * @src:   bitmap to copy.
 * @nbits: number of bits in the bitmap.
 *
 * Require nbits % BITS_PER_LONG == 0.
 */
void bitmap_copy_le(unsigned long *dst, const unsigned long *src, int nbits)
{
	int i;

	for (i = 0; i < nbits/BITS_PER_LONG; i++)
		if (BITS_PER_LONG == 64)
			dst[i] = cpu_to_le64(src[i]);
		else
			dst[i] = cpu_to_le32(src[i]);
}
EXPORT_SYMBOL(bitmap_copy_le);
#endif /* #ifdef NEED_BITMAP_COPY_LE */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,13)

/*
 * Stolen from 2.6.13-rcsomething, where this is not exported; later
 * on, (before 2.6.13) a EXPORT_SYMBOL_GPL() is added, so ONLY GPL
 * code can link against these two functions, usb_get_intf() and
 * usb_put_intf().
 */

struct usb_interface *usb_get_intf(struct usb_interface *iface)
{
	if (iface)
		get_device(&iface->dev);
	return iface;
}
EXPORT_SYMBOL_GPL(usb_get_intf);

void usb_put_intf(struct usb_interface *iface);
{
	if (iface)
		put_device(&iface->dev);
}
EXPORT_SYMBOL_GPL(usb_put_intf);

#endif	/* Linux kernel < 2.6.13 */
