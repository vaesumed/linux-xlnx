/*
 *	Copyright (C) 2005 Mike Lee(eemike@gmail.com)
 *
 *	This udc driver is now under testing and code is based on pxa2xx_udc.h
 *	Please use it with your own risk!
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 */

#ifndef __LINUX_USB_GADGET_IMX_H
#define __LINUX_USB_GADGET_IMX_H

#include <linux/types.h>

/*-------------------------------------------------------------------------*/

/* IN:1 , OUT:0 */
#define EP_NO(ep)	((ep->bEndpointAddress) & ~USB_DIR_IN)
#define EP_DIR(ep)	((ep->bEndpointAddress) & USB_DIR_IN ? 1 : 0)

/*
 * not yeah finish double buffering
 * so use full fifo size be the max packet size
 */
#define EP0_MAX_SIZE	((unsigned)8)
#define BULK_MAX_SIZE	((unsigned)64)
#define ISO_MAX_SIZE	((unsigned)1023)
#define INT_MAX_SIZE	((unsigned)32)
#define IMX_USB_NB_EP	6

struct imx_request {
	struct usb_request			req;
	struct list_head			queue;
};

enum ep0_state {
	EP0_IDLE,
	EP0_IN_DATA_PHASE,
	EP0_OUT_DATA_PHASE,
	EP0_END_XFER,
	EP0_STALL,
};

struct imx_ep_struct {
	struct usb_ep				ep;
	struct imx_udc_struct			*imx_usb;
	struct list_head			queue;
	const struct usb_endpoint_descriptor	*desc;
	unsigned long				irqs;
	unsigned				stopped:1;
	unsigned				wMaxPacketSize;
	unsigned				fifosize;
	u8					bEndpointAddress;
	u8					bmAttributes;
};

struct imx_udc_struct {
	struct usb_gadget			gadget;
	struct usb_gadget_driver		*driver;
	struct device				*dev;
	struct imx_ep_struct			imx_ep[IMX_USB_NB_EP];
	struct clk				*clk;
	enum ep0_state				ep0state;
	struct resource				*res;
	void __iomem				*base;
	spinlock_t				lock;
	unsigned				got_irq:1;
	unsigned				set_config:1;
	int					dev_config;
	int					usbd_int[7];
};

#define irq_to_ep(irq)	(((irq) >= USBD_INT0) || ((irq) <= USBD_INT6) ? \
			((irq) - USBD_INT0) : (USBD_INT6)) /*should not happen*/
#define ep_to_irq(ep)	(EP_NO((ep)) + USBD_INT0)

/*
 * USB registers
 */
#define  USB_FRAME       (0x00)	/* USB frame */
#define  USB_SPEC        (0x04)	/* USB Spec */
#define  USB_STAT        (0x08)	/* USB Status */
#define  USB_CTRL        (0x0C)	/* USB Control */
#define  USB_DADR        (0x10)	/* USB Desc RAM addr */
#define  USB_DDAT        (0x14)	/* USB Desc RAM/EP buffer data */
#define  USB_INTR        (0x18)	/* USB interrupt */
#define  USB_MASK        (0x1C)	/* USB Mask */
#define  USB_ENAB        (0x24)	/* USB Enable */
#define  USB_EP_STAT(x)  (0x30 + (x*0x30)) /* USB status/control */
#define  USB_EP_INTR(x)  (0x34 + (x*0x30)) /* USB interrupt */
#define  USB_EP_MASK(x)  (0x38 + (x*0x30)) /* USB mask */
#define  USB_EP_FDAT(x)  (0x3C + (x*0x30)) /* USB FIFO data */
#define  USB_EP_FDAT0(x) (0x3C + (x*0x30)) /* USB FIFO data */
#define  USB_EP_FDAT1(x) (0x3D + (x*0x30)) /* USB FIFO data */
#define  USB_EP_FDAT2(x) (0x3E + (x*0x30)) /* USB FIFO data */
#define  USB_EP_FDAT3(x) (0x3F + (x*0x30)) /* USB FIFO data */
#define  USB_EP_FSTAT(x) (0x40 + (x*0x30)) /* USB FIFO status */
#define  USB_EP_FCTRL(x) (0x44 + (x*0x30)) /* USB FIFO control */
#define  USB_EP_LRFP(x)  (0x48 + (x*0x30)) /* USB last read frame pointer */
#define  USB_EP_LWFP(x)  (0x4C + (x*0x30)) /* USB last write frame pointer */
#define  USB_EP_FALRM(x) (0x50 + (x*0x30)) /* USB FIFO alarm */
#define  USB_EP_FRDP(x)  (0x54 + (x*0x30)) /* USB FIFO read pointer */
#define  USB_EP_FWRP(x)  (0x58 + (x*0x30)) /* USB FIFO write pointer */
/* USB Control Register Bit Fields.*/
#define USB_CMDOVER		(1<<6)	/* UDC status */
#define USB_CMDERROR		(1<<5)	/* UDC status */
#define USB_FE_ENA		(1<<3)	/* Enable Font End logic */
#define USB_UDC_RST		(1<<2)	/* UDC reset */
#define USB_AFE_ENA		(1<<1)	/* Analog Font end enable */
#define USB_RESUME		(1<<0)	/* UDC resume */
/* USB Descriptor Ram Bit Fields */
#define USB_CFG			(1<<31)	/* Configuration */
#define USB_BSY			(1<<30)	/* Busy status */
#define USB_DADR_DESC		(0x1FF)	/* Descriptor Ram Address */
#define USB_DDAT_DESC		(0xFF)	/* Descriptor Endpoint Buffer */
/* USB Endpoint Bit fields */
/* USB Endpoint status bit fields */
#define USB_FIFO_BCOUNT		(0x7F<<16)	/* Endpoint FIFO byte count */
#define USB_SIP			(1<<8)	/* Endpoint setup in progress */
#define USB_DIR			(1<<7)	/* Endpoint transfer direction */
#define USB_MAX			(3<<5)	/* Endpoint Max packet size */
#define USB_TYP			(3<<3)	/* Endpoint type */
#define USB_ZLPS		(1<<2)	/* Send zero length packet */
#define USB_FLUSH		(1<<1)	/* Endpoint FIFO Flush */
#define USB_STALL		(1<<0)	/* Force stall */
/* USB Endpoint FIFO status bit fields */
#define USB_FRAME_STAT		(0xF<<24)	/* Frame status bit [0-3] */
#define USB_ERR			(1<<22)	/* FIFO error */
#define USB_UF			(1<<21)	/* FIFO underflow */
#define USB_OF			(1<<20)	/* FIFO overflow */
#define USB_FR			(1<<19)	/* FIFO frame ready */
#define USB_FULL		(1<<18)	/* FIFO full */
#define USB_ALRM		(1<<17)	/* FIFO alarm */
#define USB_EMPTY		(1<<16)	/* FIFO empty */
/* USB Endpoint FIFO control bit fields */
#define USB_WFR			(1<<29)	/* Write frame end */
/* USB Endpoint FIFO interrupt bit fields */
#define USB_FIFO_FULL		(1<<8)	/* fifo full */
#define USB_FIFO_EMPTY		(1<<7)	/* fifo empty */
#define USB_FIFO_ERROR		(1<<6)	/* fifo error */
#define USB_FIFO_HIGH		(1<<5)	/* fifo high */
#define USB_FIFO_LOW		(1<<4)	/* fifo low */
#define USB_MDEVREQ		(1<<3)	/* multi Device request */
#define USB_EOT			(1<<2)	/* fifo end of transfer */
#define USB_DEVREQ		(1<<1)	/* Device request */
#define USB_EOF			(1<<0)	/* fifo end of frame */
/* USB Interrupt Bit fields */
#define USB_WAKEUP		(1<<31)	/* Wake up Interrupt */
#define USB_MSOF		(1<<7)	/* Missed Start of Frame */
#define USB_SOF			(1<<6)	/* Start of Frame */
#define USB_RESET_STOP		(1<<5)	/* Reset Signaling stop */
#define USB_RESET_START		(1<<4)	/* Reset Signaling start */
#define USB_RES			(1<<3)	/* Suspend to resume */
#define USB_SUSP		(1<<2)	/* Active to suspend */
#define USB_FRAME_MATCH		(1<<1)	/* Frame matched */
#define USB_CFG_CHG		(1<<0)	/* Configuration change occurred */
/* USB Enable Register Bit Fields.*/
#define USB_RST			(1<<31)	/* Reset USB modules */
#define USB_ENA			(1<<30)	/* Enable USB modules*/
#define USB_SUSPEND		(1<<29)	/* Suspend USB modules */
#define USB_ENDIAN		(1<<28)	/* Endian of USB modules */
#define USB_POWER		(1<<0)	/* Power mode of USB modules */

/*------------------------ D E B U G -----------------------------------------*/

#ifdef DEBUG
#define LV4
#define LV3
#define LV2
#define LV1
#define LV0

#ifdef LV0
#define D(label, fmt, args...) \
	printk(KERN_INFO "udc (%20s) " fmt, label, ## args)
#else /* LV0 */
#define D(fmt, args...) do {} while (0)
#endif /* LV0 */

#ifdef LV1
#define D1(fmt, args...) \
	printk(KERN_INFO "udc lv1(%20s) " fmt, __func__, ## args)
#else /* LV1 */
#define D1(fmt, args...) do {} while (0)
#endif /* LV1 */

#ifdef LV2
#define D2(fmt, args...) \
	printk(KERN_INFO "udc lv2(%20s) " fmt, __func__, ## args)
#else /* LV2 */
#define D2(fmt, args...) do {} while (0)
#endif /* LV2 */

#ifdef LV3
#define D3(fmt, args...) \
	printk(KERN_INFO "udc lv3(%20s) " fmt, __func__, ## args)
#else /* LV3 */
#define D3(fmt, args...) do {} while (0)
#endif /* LV3 */

#ifdef LV4
#define D4(fmt, args...) \
	printk(KERN_INFO "udc lv4(%20s) " fmt, __func__, ## args)
#else /* LV4 */
#define D4(fmt, args...) do {} while (0)
#endif /* LV4 */

static const char *state_name[] = {
	"EP0_IDLE",
	"EP0_IN_DATA_PHASE", "EP0_OUT_DATA_PHASE",
	"EP0_END_XFER", "EP0_STALL"
};

static void __attribute__ ((__unused__))
dump_ep_stat(const char *label, struct imx_ep_struct *imx_ep)
{
	int nb = EP_NO(imx_ep);
	D(label, "ep0[%s] ep%d_stat<%08x>=[%s%s%s%s%s]\n",
		state_name[imx_ep->imx_usb->ep0state], nb,
		USB_EP_STAT(nb),
		(USB_EP_STAT(nb) & USB_SIP) ? " sip" : "",
		(USB_EP_STAT(nb) & USB_DIR) ? " in" : "",
		(USB_EP_STAT(nb) & USB_ZLPS) ? " zlp" : "",
		(USB_EP_STAT(nb) & USB_FLUSH) ? " fsh" : "",
		(USB_EP_STAT(nb) & USB_STALL) ? " stall" : "");
}
static void __attribute__ ((__unused__))
dump_ep_intr(const char *label, struct imx_ep_struct *imx_ep)
{
	int nb = EP_NO(imx_ep);
	D(label, "ep%d_intr<%08x>=[%s%s%s%s%s%s%s%s%s]\n",
		nb, USB_EP_INTR(nb),
		(USB_EP_INTR(nb) & USB_FIFO_FULL) ? " full" : "",
		(USB_EP_INTR(nb) & USB_FIFO_EMPTY) ? " fempty" : "",
		(USB_EP_INTR(nb) & USB_FIFO_ERROR) ? " ferr" : "",
		(USB_EP_INTR(nb) & USB_FIFO_HIGH) ? " fhigh" : "",
		(USB_EP_INTR(nb) & USB_FIFO_LOW) ? " flow" : "",
		(USB_EP_INTR(nb) & USB_MDEVREQ) ? " mreq" : "",
		(USB_EP_INTR(nb) & USB_EOF) ? " eof" : "",
		(USB_EP_INTR(nb) & USB_DEVREQ) ? " req" : "",
		(USB_EP_INTR(nb) & USB_EOT) ? " eot" : ""
		);
}
static void __attribute__ ((__unused__))
dump_intr(const char *label)
{
	D(label, "usb_intr<%08x>=[%s%s%s%s%s%s%s%s%s]\n",
		USB_INTR,
		(USB_INTR & USB_WAKEUP) ? " wak" : "",
		(USB_INTR & USB_MSOF) ? " msof" : "",
		(USB_INTR & USB_SOF) ? " sof" : "",
		(USB_INTR & USB_RES) ? " res" : "",
		(USB_INTR & USB_SUSP) ? " sus" : "",
		(USB_INTR & USB_RESET_STOP) ? " res_stop" : "",
		(USB_INTR & USB_RESET_START) ? " res_start" : "",
		(USB_INTR & USB_FRAME_MATCH) ? " f_match" : "",
		(USB_INTR & USB_CFG_CHG) ? " cfg" : ""
		);
}

static void __attribute__ ((__unused__))
dump_ep_fstat(const char *label, struct imx_ep_struct *imx_ep)
{
	int nb = EP_NO(imx_ep);
	D(label, "%s %08X =framebit[%04x],[%s%s%s%s%s%s%s]\n",
		state_name[imx_ep->imx_usb->ep0state], USB_EP_FSTAT(nb),
		(USB_EP_FSTAT(nb) & USB_FRAME_STAT) >> 24,
		(USB_EP_FSTAT(nb) & USB_ERR) ? " err" : "",
		(USB_EP_FSTAT(nb) & USB_UF) ? " uf" : "",
		(USB_EP_FSTAT(nb) & USB_OF) ? " of" : "",
		(USB_EP_FSTAT(nb) & USB_FR) ? " fr" : "",
		(USB_EP_FSTAT(nb) & USB_FULL) ? " full" : "",
		(USB_EP_FSTAT(nb) & USB_ALRM) ? " alrm" : "",
		(USB_EP_FSTAT(nb) & USB_EMPTY) ? " empty" : "");
}

static void __attribute__ ((__unused__))
dump_req(struct usb_request *req) {
#ifdef LV4
	int i = 0;

	if (!req || !req->buf) {
		D(__func__, "req or req buf is free\n");
		return;
	}

	printk("dump req <");
	for (; i < req->length; i++)
		printk("%02x-", *((u8 *)req->buf + i));

	printk(">\n");
#endif /* LV4 */
}


#else /* DEBUG */

#define D(label, fmt, args...) do {} while (0)
#define D1(fmt, args...) do {} while (0)
#define D2(fmt, args...) do {} while (0)
#define D3(fmt, args...) do {} while (0)
#define D4(fmt, args...) do {} while (0)

#define	dump_ep_stat(x, y) 	do {} while (0)
#define	dump_ep_fstat(x, y)	do {} while (0)
#define dump_ep_intr(x, y)	do {} while (0)
#define dump_intr(x)		do {} while (0)
#define dump_ep_fstat(x, y)	do {} while (0)
#define dump_req(req)		do {} while (0)

#endif /* DEBUG */

#endif /* __LINUX_USB_GADGET_IMX_H */
