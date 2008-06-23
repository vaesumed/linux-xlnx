/*
 * Intel 1480 Wireless UWB Link
 * WLP specific definitions
 *
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

#ifndef __i1480_wlp_h__
#define __i1480_wlp_h__

#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/uwb.h>
#include <linux/if_ether.h>

/* New simplified header format? */
#undef WLP_HDR_FMT_2 		/* FIXME: rename */

/**
 * Values of the Delivery ID & Type field when PCA or DRP
 *
 * The Delivery ID & Type field in the WLP TX header indicates whether
 * the frame is PCA or DRP. This is done based on the high level bit of
 * this field.
 * We use this constant to test if the traffic is PCA or DRP as follows:
 * if (wlp_tx_hdr->delivery_id_type & WLP_DRP)
 * 	this is DRP traffic
 * else
 * 	this is PCA traffic
 */
enum deliver_id_type_bit {
	WLP_DRP = 8,
};

/**
 * WLP TX header
 *
 * Indicates UWB/WLP-specific transmission parameters for a network
 * packet.
 */
struct wlp_tx_hdr {
	/* dword 0 */
	struct uwb_dev_addr		dstaddr;
	u8		    		key_index;
	DECL_BF_LE3(
		u8			delivery_id_type:4,
		enum uwb_ack_pol	ack_pol:3,
		u8			rts_cts:1
	) __attribute__((packed));
	/* dword 1 */
	DECL_BF_LE2(
		enum uwb_phy_rate	phy_rate:4,
		s8			tx_power_ctl:4
	) __attribute__((packed));
#ifndef WLP_HDR_FMT_2
	u8		    reserved;
	__le16		    oui01;		/* FIXME: not so sure if __le16 or u8[2] */
	/* dword 2 */
	u8		    oui2;		/*        if all LE, it could be merged */
	__le16		    prid;
#endif
} __attribute__((packed));


/**
 * WLP RX header
 *
 * Provides UWB/WLP-specific transmission data for a received
 * network packet.
 */
struct wlp_rx_hdr {
	/* dword 0 */
	struct uwb_dev_addr dstaddr;
	struct uwb_dev_addr srcaddr;
	/* dword 1 */
	u8 		    LQI;
	s8		    RSSI;
	u8		    reserved3;
#ifndef WLP_HDR_FMT_2
	u8 		    oui0;
	/* dword 2 */
	__le16		    oui12;
	__le16		    prid;
#endif
} __attribute__((packed));


/** User configurable options for WLP */
struct wlp_options {
	struct mutex mutex; /* access to user configurable options*/
	struct wlp_tx_hdr def_tx_hdr;	/* default tx hdr */
	u8 pca_base_priority;
	u8 bw_alloc; /*index into bw_allocs[] for PCA/DRP reservations*/
};


static inline
void wlp_options_init(struct wlp_options *options)
{
	mutex_init(&options->mutex);
	options->def_tx_hdr.ack_pol = UWB_ACK_INM;
	options->def_tx_hdr.rts_cts = 1;
	/* FIXME: default to phy caps */
	options->def_tx_hdr.phy_rate = UWB_PHY_RATE_480;
#ifndef WLP_HDR_FMT_2
	options->def_tx_hdr.prid = cpu_to_le16(0x0000);
#endif
}


/* sysfs helpers */

extern ssize_t uwb_pca_base_priority_store(struct wlp_options *,
					   const char *, size_t);
extern ssize_t uwb_pca_base_priority_show(const struct wlp_options *, char *);
extern ssize_t uwb_bw_alloc_store(struct wlp_options *, const char *, size_t);
extern ssize_t uwb_bw_alloc_show(const struct wlp_options *, char *);
extern ssize_t uwb_ack_policy_store(struct wlp_options *,
				    const char *, size_t);
extern ssize_t uwb_ack_policy_show(const struct wlp_options *, char *);
extern ssize_t uwb_rts_cts_store(struct wlp_options *, const char *, size_t);
extern ssize_t uwb_rts_cts_show(const struct wlp_options *, char *);
extern ssize_t uwb_phy_rate_store(struct wlp_options *, const char *, size_t);
extern ssize_t uwb_phy_rate_show(const struct wlp_options *, char *);


/** Simple bandwidth allocation (temporary and too simple) */
struct wlp_bw_allocs {
	const char *name;
	struct {
		u8 mask, stream;
	} tx, rx;
};


#endif /* #ifndef __i1480_wlp_h__ */
