/*
 * FireDTV driver (formerly known as FireSAT)
 *
 * Copyright (C) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (C) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include <ieee1394.h>
#include <nodemgr.h>

#include "avc_api.h"
#include "cmp.h"
#include "firesat.h"

#define CMP_OUTPUT_PLUG_CONTROL_REG_0	0xfffff0000904ULL

typedef struct _OPCR
{
	__u8 PTPConnCount    : 6 ; // Point to point connect. counter
	__u8 BrConnCount     : 1 ; // Broadcast connection counter
	__u8 OnLine          : 1 ; // On Line

	__u8 ChNr            : 6 ; // Channel number
	__u8 Res             : 2 ; // Reserved

	__u8 PayloadHi       : 2 ; // Payoad high bits
	__u8 OvhdID          : 4 ; // Overhead ID
	__u8 DataRate        : 2 ; // Data Rate

	__u8 PayloadLo           ; // Payoad low byte
} OPCR ;

static int cmp_read(struct firesat *firesat, void *buf, u64 addr, size_t len)
{
	int ret;

	if (mutex_lock_interruptible(&firesat->avc_mutex))
		return -EINTR;

	ret = hpsb_node_read(firesat->ud->ne, addr, buf, len);
	if (ret < 0)
		dev_err(&firesat->ud->device, "CMP: read I/O error\n");

	mutex_unlock(&firesat->avc_mutex);
	return ret;
}

static int cmp_lock(struct firesat *firesat, quadlet_t *data, u64 addr,
		quadlet_t arg, int ext_tcode)
{
	int ret;

	if (mutex_lock_interruptible(&firesat->avc_mutex))
		return -EINTR;

	ret = hpsb_node_lock(firesat->ud->ne, addr, ext_tcode, data, arg);
	if (ret < 0)
		dev_err(&firesat->ud->device, "CMP: lock I/O error\n");

	mutex_unlock(&firesat->avc_mutex);
	return ret;
}

int cmp_establish_pp_connection(struct firesat *firesat, int plug, int channel)
{
	quadlet_t old_opcr, opcr;
	OPCR *hilf = (OPCR *)&opcr;
	u64 opcr_address = CMP_OUTPUT_PLUG_CONTROL_REG_0 + (plug << 2);
	int ret;

	ret = cmp_read(firesat, &opcr, opcr_address, 4);
	if (ret < 0)
		return ret;

repeat:
	if (!hilf->OnLine) {
		dev_err(&firesat->ud->device, "CMP: output offline\n");
		return -EBUSY;
	}

	old_opcr = opcr;

	if (hilf->PTPConnCount) {
		if (hilf->ChNr != channel) {
			dev_err(&firesat->ud->device,
				"CMP: cannot change channel\n");
			return -EBUSY;
		}
		dev_info(&firesat->ud->device,
			 "CMP: overlaying existing connection\n");

		/* We don't allocate isochronous resources. */
	} else {
		hilf->ChNr = channel;
		hilf->DataRate = IEEE1394_SPEED_400;

		/* FIXME: this is for the worst case - optimize */
		hilf->OvhdID = 0;

		/* FIXME: allocate isochronous channel and bandwidth at IRM */
	}

	hilf->PTPConnCount++;

	ret = cmp_lock(firesat, &opcr, opcr_address, old_opcr, 2);
	if (ret < 0)
		return ret;

	if (old_opcr != opcr) {
		/*
		 * FIXME: if old_opcr.P2P_Connections > 0,
		 * deallocate isochronous channel and bandwidth at IRM
		 */

		goto repeat;
	}

	return 0;
}

void cmp_break_pp_connection(struct firesat *firesat, int plug, int channel)
{
	quadlet_t old_opcr, opcr;
	OPCR *hilf = (OPCR *)&opcr;
	u64 opcr_address = CMP_OUTPUT_PLUG_CONTROL_REG_0 + (plug << 2);

	if (cmp_read(firesat, &opcr, opcr_address, 4) < 0)
		return;

repeat:
	if (!hilf->OnLine || !hilf->PTPConnCount || hilf->ChNr != channel) {
		dev_err(&firesat->ud->device, "CMP: no connection to break\n");
		return;
	}

	old_opcr = opcr;
	hilf->PTPConnCount--;

	if (cmp_lock(firesat, &opcr, opcr_address, old_opcr, 2) < 0)
		return;

	if (old_opcr != opcr) {
		/*
		 * FIXME: if old_opcr.P2P_Connections == 1, i.e. we were last
		 * owner, deallocate isochronous channel and bandwidth at IRM
		 */

		goto repeat;
	}
}
