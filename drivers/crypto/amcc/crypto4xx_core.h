/**
 * AMCC SoC PPC4xx Crypto Driver
 *
 * Copyright (c) 2008 Applied Micro Circuits Corporation.
 * All rights reserved. James Hsiao <jhsiao@amcc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * @file   crypto4xx_core.h
 *
 * This is the header file for AMCC Crypto offload Linux device driver for
 * use with Linux CryptoAPI.
 *
 * Changes:
 *	James Hsiao:	10/04/08
 *			add struct device *device and struct of_device *ofdev
 *			in crypto4xx_core_device.
 *			change struct crypto4xx_device dev in
 *			crypto4xx_core_device to struct crypto4xx_device *dev.
 *			move ce_base and ce_phy_address from
 *			crypto4xx_core_device to crypto4xx_device.
 *			remove irq_cnt from crypto4xx_core_device.
 *			remove some unnecessary extern prototype declare.
 */

#ifndef __CRYPTO4XX_CORE_H__
#define __CRYPTO4XX_CORE_H__

#define CRYPTO4XX_CRYPTO_PRIORITY	300

#define PPC4XX_LAST_PD			63
#define PPC4XX_NUM_PD			64

#define PPC4XX_LAST_GD			1023
#define PPC4XX_NUM_GD			1024

#define PPC4XX_LAST_SD			63
#define PPC4XX_NUM_SD			64

#define PPC4XX_SD_BUFFER_SIZE		2048

#define PPC4XX_INT_DESCR_CNT		4
#define PPC4XX_INT_TIMEOUT_CNT		0
/* FIXme arbitory number*/
#define PPC4XX_INT_CFG			1
/*
 * These define will be used in crypto4xx_build_pd
 * AHASH don't have dst scatterlist iso u8*
 * with the type field it can destinguish what is
 */
#define ABLK				0
#define AHASH				1

#define PD_ENTRY_INUSE			1
#define PD_ENTRY_FREE			0

#define EALLOC_MEM_FAIL			0xfffffffd
#define EDOWNSEMA_FAIL			0xfffffffe
#define ERING_WAS_FULL			0xffffffff

struct crypto4xx_device;
extern struct crypto4xx_core_device lsec_core;
extern struct crypto_alg crypto4xx_basic_alg[];

struct pd_uinfo {
	struct crypto4xx_device *dev;
	u32   state;
	u32 using_sd;
	void *pd_va;                             /* offset from pdr */
	void *rd_va;                             /* offset from rdr, could be
						 same as pdr(same as pd_va)*/
	u32 first_gd;                            /* first gather discriptor
							used by this packet */
	u32 last_gd;                             /* last gather discriptor
							used by this packet */
	u32 first_sd;                            /* first scatter discriptor
							used by this packet */
	u32 last_sd;                             /* last scatter discriptor
							used by this packet */
	u32 first_done;
	u32 last_done;
	struct scatterlist *dest_va;
	struct crypto_async_request *async_req;  /* base crypto request
							for this packet */
};

struct crypto4xx_device {
	struct crypto4xx_core_device *core_dev;
	u8   dev_id;			/* Device ID - id of device to
						send request to */
	char *name;

	u64  ce_phy_address;
	void __iomem *ce_base;

	void *pdr;			/* base address of packet
						descriptor ring */
	dma_addr_t pdr_pa;		/* physical address used to
						program ce pdr_base_register */
	void *rdr;			/* result descriptor ring, maybe same
						location as pdr */
	dma_addr_t rdr_pa;             /* physical address used to
						program ce rdr_base_register */
	void *gdr;                     /* gather descriptor ring,
						for inbound packet/fragments */
					/* address of particle is
						from the request, src sg*/
	dma_addr_t gdr_pa;		/* physical address used to
						program ce gdr_base_register */
	void *sdr;			/* scatter descriptor ring,for outbound
						packet/fragments
						must be same size, so init them
						to 2k each safe for large
						packets */
	dma_addr_t sdr_pa;		/* physical address used to
						program ce sdr_base_register */
	dma_addr_t scatter_buffer_pa;
	void *scatter_buffer_va;
	u32 scatter_buffer_size;
	int pdr_tail;
	int pdr_head;
	u32 gdr_tail;
	u32 gdr_head;
	u32 sdr_tail;
	u32 sdr_head;
	void *pdr_uinfo;
	struct list_head alg_list;	/* List of algorithm supported
							by this device */
};

struct crypto4xx_core_device {
	struct device *device;
	struct of_device *ofdev;
	struct crypto4xx_device *dev;
	u32 int_status;
	u32 irq;
	struct tasklet_struct tasklet;

};

struct crypto4xx_ctx {
	struct crypto4xx_device *dev;
	void *sa_in;
	dma_addr_t sa_in_dma_addr;
	void *sa_out;
	dma_addr_t sa_out_dma_addr;
	void *state_record;
	dma_addr_t state_record_dma_addr;
	u16 sa_len;
	u32 direction;
	u32 use_rctx;
	u32 next_hdr;
	u32 save_iv;
	u32 pd_ctl_len;
	u32 pd_ctl;
	u32 bypass;
	u32 is_hash;
	u32 hash_final;
};

struct crypto4xx_req_ctx {
	struct crypto4xx_device *dev;	/* Device in which
					operation to send to */
	void  *sa;
	dma_addr_t sa_dma_addr;
	u16 sa_len;
};

struct crypto4xx_alg {
	struct list_head  entry;
	struct crypto_alg alg;
	struct crypto4xx_device *dev;
};

#define crypto_alg_to_crypto4xx_alg(x) \
		container_of(x, struct crypto4xx_alg, alg)

extern int crypto4xx_alloc_sa(struct crypto4xx_ctx *ctx, u32 size);
extern void crypto4xx_free_sa(struct crypto4xx_ctx *ctx);
extern u32 crypto4xx_alloc_sa_rctx(struct crypto4xx_ctx *ctx,
				   struct crypto4xx_ctx *rctx);
extern void crypto4xx_free_sa_rctx(struct crypto4xx_ctx *rctx);
extern void crypto4xx_free_ctx(struct crypto4xx_ctx   *ctx);
extern u32 crypto4xx_alloc_state_record(struct crypto4xx_ctx *ctx);
extern u32 get_dynamic_sa_offset_state_ptr_field(struct crypto4xx_ctx *ctx);
extern u32 get_dynamic_sa_iv_size(struct crypto4xx_ctx *ctx);
extern void crypto4xx_memcpy_le(unsigned int *dst,
				const unsigned char *buf, int len);
extern int crypto4xx_handle_req(struct crypto_async_request *req);
extern int crypto4xx_register_alg(struct crypto4xx_device *sec_dev,
			  struct crypto_alg *crypto_alg, int array_size);
extern int crypto4xx_register_basic_alg(struct crypto4xx_device *dev);
#endif
