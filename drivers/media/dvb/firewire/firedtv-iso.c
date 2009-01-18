/*
 * FireSAT DVB driver
 *
 * Copyright (C) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include <dvb_demux.h>

#include <dma.h>
#include <iso.h>
#include <nodemgr.h>

#include "firedtv.h"

static void rawiso_activity_cb(struct hpsb_iso *iso)
{
	struct firedtv *fdtv_iterator, *fdtv = NULL;
	unsigned int i, num, packet;
	unsigned char *buf;
	unsigned long flags;
	int count;

	spin_lock_irqsave(&fdtv_list_lock, flags);
	list_for_each_entry(fdtv_iterator, &fdtv_list, list)
		if(fdtv_iterator->iso_handle == iso) {
			fdtv = fdtv_iterator;
			break;
		}
	spin_unlock_irqrestore(&fdtv_list_lock, flags);

	packet = iso->first_packet;
	num = hpsb_iso_n_ready(iso);

	if (!fdtv) {
		dev_err(&fdtv->ud->device, "received at unknown iso channel\n");
		goto out;
	}

	for (i = 0; i < num; i++, packet = (packet + 1) % iso->buf_packets) {
		buf = dma_region_i(&iso->data_buf, unsigned char,
			iso->infos[packet].offset + sizeof(struct CIPHeader));
		count = (iso->infos[packet].len - sizeof(struct CIPHeader)) /
			(188 + sizeof(struct firewireheader));

		/* ignore empty packet */
		if (iso->infos[packet].len <= sizeof(struct CIPHeader))
			continue;

		while (count--) {
			if (buf[sizeof(struct firewireheader)] == 0x47)
				dvb_dmx_swfilter_packets(&fdtv->demux,
				    &buf[sizeof(struct firewireheader)], 1);
			else
				dev_err(&fdtv->ud->device,
					"skipping invalid packet\n");
			buf += 188 + sizeof(struct firewireheader);
		}
	}
out:
	hpsb_iso_recv_release_packets(iso, num);
}

void tear_down_iso_channel(struct firedtv *fdtv)
{
	if (fdtv->iso_handle != NULL) {
		hpsb_iso_stop(fdtv->iso_handle);
		hpsb_iso_shutdown(fdtv->iso_handle);
	}
	fdtv->iso_handle = NULL;
}

#define FDTV_ISO_BUFFER_PACKETS 256
#define FDTV_ISO_BUFFER_SIZE (FDTV_ISO_BUFFER_PACKETS * 200)

int setup_iso_channel(struct firedtv *fdtv)
{
	int ret;

	fdtv->iso_handle = hpsb_iso_recv_init(fdtv->ud->ne->host,
				FDTV_ISO_BUFFER_SIZE, FDTV_ISO_BUFFER_PACKETS,
				fdtv->isochannel, HPSB_ISO_DMA_DEFAULT,
				-1, /* stat.config.irq_interval */
				rawiso_activity_cb);
	if (fdtv->iso_handle == NULL) {
		dev_err(&fdtv->ud->device, "cannot initialize iso receive\n");
		return -ENOMEM;
	}

	ret = hpsb_iso_recv_start(fdtv->iso_handle, -1, -1, 0);
	if (ret != 0) {
		dev_err(&fdtv->ud->device, "cannot start iso receive\n");
		hpsb_iso_shutdown(fdtv->iso_handle);
		fdtv->iso_handle = NULL;
	}
	return ret;
}
