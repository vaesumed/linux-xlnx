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
 * @file   crypto4xx_core.c
 *
 * This file implements AMCC crypto offload Linux device driver for use with
 * Linux CryptoAPI.
 *
 * Changes:
 *	James Hsiao:	10/04/08
 *			replace global variable lsec_core with data in
 *			in struct device.
 *			add parameter to various functions due to
 *			to remove of global variable lsec_core
 *			crypto4xx_alloc_sa now return error code.
 *			not using PVR to identify which CPU, using DTS.
 *			move this into the probe function.
 *			pass struct device pointer to dma_alloc_coherent
 *			and dma_free_coherent functions.
 *			make function static where ever is possible.
 *			remove crypto4xx_start_device.
 *			remove crypt4xx_setup_crypto.
 *			in crypto4xx_crypt_remove add kill tasklet.
 *			in crypto4xx_stop_all unmap ce_base and free core_dev.
 *			add lock to all get/put pd/sd/gd functions.
 *			change PPC4XX_SEC_VERSION_STR to "0.3"
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mod_devicetable.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/spinlock_types.h>
#include <linux/highmem.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <crypto/algapi.h>
#include <crypto/des.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <asm/dcr.h>
#include <asm/dcr-regs.h>
#include <asm/cacheflush.h>
#include <crypto/internal/hash.h>
#include "crypto4xx_reg_def.h"
#include "crypto4xx_core.h"
#include "crypto4xx_sa.h"

#define CRYPTO4XX_CRYPTO_PRIORITY	300
#define PPC4XX_SEC_VERSION_STR		"0.3"

static inline void crypto4xx_write32(struct crypto4xx_device *dev,
					u32 reg, u32 val)
{
	writel(val, dev->ce_base + reg);
}

static inline void crypto4xx_read32(struct crypto4xx_device *dev,
					u32 reg, u32 *val)
{
	*val = readl(dev->ce_base + reg);
}

/**
 * PPC4xx Crypto Engine Initialization Routine
 */
static int crypto4xx_init(struct crypto4xx_device  *dev)
{
	union ce_ring_size ring_size;
	union ce_ring_contol ring_ctrl;
	union ce_part_ring_size part_ring_size;
	union ce_io_threshold io_threshold;
	u32 rand_num;
	union ce_pe_dma_cfg pe_dma_cfg;

	crypto4xx_write32(dev, CRYPTO_ENGINE_BYTE_ORDER_CFG, 0x22222);

	/* setup pe dma, include reset sg, pdr and pe, then release reset */
	pe_dma_cfg.w = 0;

	pe_dma_cfg.bf.bo_sgpd_en = 1;
	pe_dma_cfg.bf.bo_data_en = 0;
	pe_dma_cfg.bf.bo_sa_en = 1;
	pe_dma_cfg.bf.bo_pd_en = 1;

	pe_dma_cfg.bf.dynamic_sa_en = 1;
	pe_dma_cfg.bf.reset_sg = 1;
	pe_dma_cfg.bf.reset_pdr = 1;
	pe_dma_cfg.bf.reset_pe = 1;

	crypto4xx_write32(dev, CRYPTO_ENGINE_PE_DMA_CFG, pe_dma_cfg.w);

	/* un reset pe,sg and pdr */
	pe_dma_cfg.bf.pe_mode = 0;
	pe_dma_cfg.bf.reset_sg = 0;
	pe_dma_cfg.bf.reset_pdr = 0;
	pe_dma_cfg.bf.reset_pe = 0;
	pe_dma_cfg.bf.bo_td_en = 0;

	crypto4xx_write32(dev, CRYPTO_ENGINE_PE_DMA_CFG, pe_dma_cfg.w);

	crypto4xx_write32(dev, CRYPTO_ENGINE_PDR_BASE, dev->pdr_pa);
	crypto4xx_write32(dev, CRYPTO_ENGINE_RDR_BASE, dev->pdr_pa);

	crypto4xx_write32(dev, CRYPTO_ENGINE_PRNG_CTRL, 3);
	get_random_bytes(&rand_num, sizeof(rand_num));
	crypto4xx_write32(dev, CRYPTO_ENGINE_PRNG_SEED_L, rand_num);
	get_random_bytes(&rand_num, sizeof(rand_num));
	crypto4xx_write32(dev, CRYPTO_ENGINE_PRNG_SEED_L, rand_num);

	ring_size.w = 0;
	ring_size.bf.ring_offset = PPC4XX_PD_SIZE;
	ring_size.bf.ring_size   = PPC4XX_NUM_PD;
	crypto4xx_write32(dev, CRYPTO_ENGINE_RING_SIZE, ring_size.w);

	ring_ctrl.w = 0;
	crypto4xx_write32(dev, CRYPTO_ENGINE_RING_CTRL, ring_ctrl.w);
	crypto4xx_write32(dev, CRYPTO_ENGINE_DC_CTRL, 1);

	crypto4xx_write32(dev, CRYPTO_ENGINE_GATH_RING_BASE, dev->gdr_pa);
	crypto4xx_write32(dev, CRYPTO_ENGINE_SCAT_RING_BASE, dev->sdr_pa);

	part_ring_size.w = 0;
	part_ring_size.bf.sdr_size = PPC4XX_SDR_SIZE;
	part_ring_size.bf.gdr_size = PPC4XX_GDR_SIZE;
	crypto4xx_write32(dev, CRYPTO_ENGINE_PART_RING_SIZE,
			part_ring_size.w);

	crypto4xx_write32(dev, CRYPTO_ENGINE_PART_RING_CFG,
			0x0000ffff & PPC4XX_SD_BUFFER_SIZE);
	io_threshold.w = 0;
	io_threshold.bf.output_threshold = PPC4XX_OUTPUT_THRESHOLD;
	io_threshold.bf.input_threshold  = PPC4XX_INPUT_THRESHOLD;
	crypto4xx_write32(dev, CRYPTO_ENGINE_IO_THRESHOLD, io_threshold.w);

	crypto4xx_write32(dev, CRYPTO_ENGINE_PDR_BASE_UADDR, 0x0);
	crypto4xx_write32(dev, CRYPTO_ENGINE_RDR_BASE_UADDR, 0x0);
	crypto4xx_write32(dev, CRYPTO_ENGINE_PKT_SRC_UADDR, 0x0);
	crypto4xx_write32(dev, CRYPTO_ENGINE_PKT_DEST_UADDR, 0x0);
	crypto4xx_write32(dev, CRYPTO_ENGINE_SA_UADDR, 0x0);
	crypto4xx_write32(dev, CRYPTO_ENGINE_GATH_RING_BASE_UADDR, 0x0);
	crypto4xx_write32(dev, CRYPTO_ENGINE_SCAT_RING_BASE_UADDR, 0x0);

	/* un reset pe,sg and pdr */
	pe_dma_cfg.bf.pe_mode = 1;
	pe_dma_cfg.bf.reset_sg = 0;
	pe_dma_cfg.bf.reset_pdr = 0;
	pe_dma_cfg.bf.reset_pe = 0;
	pe_dma_cfg.bf.bo_td_en = 0;

	crypto4xx_write32(dev, CRYPTO_ENGINE_PE_DMA_CFG, pe_dma_cfg.w);
	/*clear all pending interrupt*/
	crypto4xx_write32(dev, CRYPTO_ENGINE_INT_CLR, 0x3ffff);
	crypto4xx_write32(dev, CRYPTO_ENGINE_INT_DESCR_CNT,
			PPC4XX_INT_DESCR_CNT);

	crypto4xx_write32(dev, CRYPTO_ENGINE_INT_TIMEOUT_CNT,
			  PPC4XX_INT_TIMEOUT_CNT);
	crypto4xx_write32(dev, CRYPTO_ENGINE_INT_CFG, PPC4XX_INT_CFG);
	crypto4xx_write32(dev, CRYPTO_ENGINE_INT_EN, CRYPTO_PD_DONE_INT);
	return 0;
}

int crypto4xx_alloc_sa(struct crypto4xx_ctx *ctx, u32 size)
{
	ctx->sa_in = dma_alloc_coherent(ctx->dev->core_dev->device,
					size * 4,
					&ctx->sa_in_dma_addr, GFP_ATOMIC);
	if (ctx->sa_in == NULL)
		return -ENOMEM;

	ctx->sa_out = dma_alloc_coherent(ctx->dev->core_dev->device, size * 4,
					&ctx->sa_out_dma_addr, GFP_ATOMIC);
	if (ctx->sa_out == NULL) {
		dma_free_coherent(ctx->dev->core_dev->device,
				ctx->sa_len*4,
				ctx->sa_in, ctx->sa_in_dma_addr);
		return -ENOMEM;
	}

	ctx->sa_len = size;
	return 0;
}

void crypto4xx_free_sa(struct crypto4xx_ctx *ctx)
{
	if (ctx->sa_in != NULL)
		dma_free_coherent(ctx->dev->core_dev->device, ctx->sa_len*4,
				ctx->sa_in, ctx->sa_in_dma_addr);
	if (ctx->sa_out != NULL)
		dma_free_coherent(ctx->dev->core_dev->device, ctx->sa_len*4,
				ctx->sa_out, ctx->sa_out_dma_addr);

	ctx->sa_in_dma_addr = 0;
	ctx->sa_out_dma_addr = 0;
	ctx->sa_len = 0;
}

u32 crypto4xx_alloc_state_record(struct crypto4xx_ctx *ctx)
{
	ctx->state_record = dma_alloc_coherent(ctx->dev->core_dev->device,
				sizeof(struct dynamic_sa_state_record),
				&ctx->state_record_dma_addr, GFP_ATOMIC);
	if (!ctx->state_record_dma_addr)
		return -ENOMEM;
	memset(ctx->state_record, 0, sizeof(struct dynamic_sa_state_record));
	return 0;
}

void crypto4xx_free_state_record(struct crypto4xx_ctx *ctx)
{
	if (ctx->state_record != NULL)
		dma_free_coherent(ctx->dev->core_dev->device,
				    sizeof(struct dynamic_sa_state_record),
				    ctx->state_record,
				    ctx->state_record_dma_addr);
	ctx->state_record_dma_addr = 0;
}

/**
 * alloc memory for the gather ring
 * no need to alloc buf for the ring
 * gdr_tail, gdr_head and gdr_count are initialized by this function
 */
static u32 crypto4xx_build_pdr(struct crypto4xx_device  *dev)
{
	dev->pdr = dma_alloc_coherent(dev->core_dev->device,
					sizeof(struct ce_pd) * PPC4XX_NUM_PD,
					&dev->pdr_pa, GFP_ATOMIC);
	if (!dev->pdr)
		return -ENOMEM;

	dev->pdr_uinfo = kzalloc(sizeof(struct pd_uinfo) * PPC4XX_NUM_PD,
					GFP_KERNEL);
	if (!dev->pdr_uinfo) {
		dma_free_coherent(dev->core_dev->device,
				sizeof(struct ce_pd) * PPC4XX_NUM_PD,
				dev->pdr,
				dev->pdr_pa);
		return -ENOMEM;
	}
	memset(dev->pdr, 0,  sizeof(struct ce_pd) * PPC4XX_NUM_PD);
	return 0;
}

static void crypto4xx_destroy_pdr(struct crypto4xx_device  *dev)
{
	if (dev->pdr != NULL)
		dma_free_coherent(dev->core_dev->device,
				sizeof(struct ce_pd) * PPC4XX_NUM_PD,
				dev->pdr,
				dev->pdr_pa);

	if (dev->pdr_uinfo != NULL)
		kfree(dev->pdr_uinfo);
}

static u32 crypto4xx_get_pd_from_pdr_nolock(struct crypto4xx_device *dev)
{
	u32 retval;
	u32 tmp;

	retval = dev->pdr_head;
	tmp = (dev->pdr_head + 1) % PPC4XX_NUM_PD;

	if (tmp == dev->pdr_tail)
		return ERING_WAS_FULL;

	dev->pdr_head = tmp;
	return retval;
}

static u32 crypto4xx_get_pd_from_pdr(struct crypto4xx_device *dev)
{
	u32 retval;

	local_irq_disable();
	retval = crypto4xx_get_pd_from_pdr_nolock(dev);
	local_irq_enable();
	return retval;
}

static u32 crypto4xx_put_pd_to_pdr(struct crypto4xx_device *dev, u32 idx)
{
	struct pd_uinfo *pd_uinfo;

	pd_uinfo = (struct pd_uinfo *)((dev->pdr_uinfo) +
					sizeof(struct pd_uinfo)*idx);
	local_irq_disable();
	if (dev->pdr_tail != PPC4XX_LAST_PD)
		dev->pdr_tail++;
	else
		dev->pdr_tail = 0;
	pd_uinfo->state = PD_ENTRY_FREE;
	local_irq_enable();
	return 0;
}

static struct ce_pd *crypto4xx_get_pdp(struct crypto4xx_device *dev,
				dma_addr_t *pd_dma, u32 idx)
{
	*pd_dma = dev->pdr_pa + sizeof(struct ce_pd) * idx;
	return dev->pdr + sizeof(struct ce_pd)*idx;
}

/**
 * alloc memory for the gather ring
 * no need to alloc buf for the ring
 * gdr_tail, gdr_head and gdr_count are initialized by this function
 */
static u32 crypto4xx_build_gdr(struct crypto4xx_device *dev)
{
	dev->gdr = dma_alloc_coherent(dev->core_dev->device,
				      sizeof(struct ce_gd) * PPC4XX_NUM_GD,
				      &dev->gdr_pa, GFP_ATOMIC);
	if (!dev->gdr)
		return -ENOMEM;

	memset(dev->gdr, 0, sizeof(struct ce_gd) * PPC4XX_NUM_GD);
	return 0;
}

static inline void crypto4xx_destroy_gdr(struct crypto4xx_device *dev)
{
	dma_free_coherent(dev->core_dev->device,
			  sizeof(struct ce_gd) * PPC4XX_NUM_GD,
			  dev->gdr, dev->gdr_pa);
}

/* Note: caller of this function should already disable irq */
static u32 crypto4xx_get_gd_from_gdr(struct crypto4xx_device *dev)
{
	u32 retval;
	u32 tmp;

	retval = dev->gdr_head;
	tmp = (dev->gdr_head+1) % PPC4XX_NUM_GD;

	if (tmp == dev->gdr_tail)
		return ERING_WAS_FULL;

	dev->gdr_head = tmp;

	return retval;
}

static u32 crypto4xx_put_gd_to_gdr(struct crypto4xx_device *dev)
{
	local_irq_disable();
	if (dev->gdr_tail == dev->gdr_head) {
		local_irq_enable();
		return 0;
	}

	if (dev->gdr_tail != PPC4XX_LAST_GD)
		dev->gdr_tail++;
	else
		dev->gdr_tail = 0;
	local_irq_enable();
	return 0;
}

static inline struct ce_gd *crypto4xx_get_gdp(struct crypto4xx_device *dev,
				dma_addr_t *gd_dma, u32 idx)
{
	*gd_dma = dev->gdr_pa + sizeof(struct ce_gd)*idx;
	return (struct ce_gd *) (dev->gdr + sizeof(struct ce_gd) * idx);
}

/**
 * alloc memory for the scatter ring
 * need to alloc buf for the ring
 * sdr_tail, sdr_head and sdr_count are initialized by this function
 */
static u32 crypto4xx_build_sdr(struct crypto4xx_device  *dev)
{
	int i;
	struct ce_sd *sd_array;
	/* alloc memory for scatter descriptor ring */
	dev->sdr = dma_alloc_coherent(dev->core_dev->device,
				      sizeof(struct ce_sd) * PPC4XX_NUM_SD,
				      &dev->sdr_pa, GFP_ATOMIC);
	if (!dev->sdr)
		return -ENOMEM;

	dev->scatter_buffer_size = PPC4XX_SD_BUFFER_SIZE;
	dev->scatter_buffer_va =
		dma_alloc_coherent(dev->core_dev->device,
			dev->scatter_buffer_size * PPC4XX_NUM_SD,
			&dev->scatter_buffer_pa, GFP_ATOMIC);
	if (!dev->scatter_buffer_va) {
		dma_free_coherent(dev->core_dev->device,
				sizeof(struct ce_sd) * PPC4XX_NUM_SD,
				dev->sdr,
				dev->sdr_pa);
		return -ENOMEM;
	}

	sd_array = dev->sdr;

	for (i = 0; i < PPC4XX_NUM_SD; i++) {
		sd_array[i].ptr = dev->scatter_buffer_pa +
		dev->scatter_buffer_size * i;
	}

	return 0;
}

static void crypto4xx_destroy_sdr(struct crypto4xx_device  *dev)
{
	if (dev->sdr != NULL)
		dma_free_coherent(dev->core_dev->device,
				sizeof(struct ce_sd) * PPC4XX_NUM_SD,
				dev->sdr,
				dev->sdr_pa);

	if (dev->scatter_buffer_va != NULL)
		dma_free_coherent(dev->core_dev->device,
				dev->scatter_buffer_size * PPC4XX_NUM_SD,
				dev->scatter_buffer_va,
				dev->scatter_buffer_pa);
}

/* Note: caller of this function should already disable irq */
static u32 crypto4xx_get_sd_from_sdr(struct crypto4xx_device *dev)
{
	u32 retval;
	u32 tmp;

	retval = dev->sdr_head;
	tmp  = (dev->sdr_head+1) % PPC4XX_NUM_SD;

	if (tmp == dev->sdr_tail)
		return ERING_WAS_FULL;

	dev->sdr_head = tmp;
	return retval;
}

static u32 crypto4xx_put_sd_to_sdr(struct crypto4xx_device *dev)
{
	local_irq_disable();
	if (dev->sdr_tail == dev->sdr_head) {
		local_irq_enable();
		return 0;
	}
	if (dev->sdr_tail != PPC4XX_LAST_SD)
		dev->sdr_tail++;
	else
		dev->sdr_tail = 0;
	local_irq_enable();
	return 0;
}

static inline struct ce_sd *crypto4xx_get_sdp(struct crypto4xx_device *dev,
					dma_addr_t *sd_dma, u32 idx)
{
	*sd_dma = dev->sdr_pa + sizeof(struct ce_sd) * idx;
	return  (struct ce_sd *)(dev->sdr + sizeof(struct ce_sd) * idx);
}

static u32 crypto4xx_fill_one_page(struct crypto4xx_device *dev,
				dma_addr_t *addr, u32 *length,
				u32 *idx, u32 *offset, u32 *nbytes)
{
	u32 len;
	if ((*length) > dev->scatter_buffer_size) {
		memcpy(phys_to_virt(*addr),
			dev->scatter_buffer_va +
			(*idx)*dev->scatter_buffer_size + (*offset),
			dev->scatter_buffer_size);
		*offset = 0;
		*length -= dev->scatter_buffer_size;
		*nbytes -= dev->scatter_buffer_size;
		if (*idx == PPC4XX_LAST_SD)
			*idx = 0;
		else
			(*idx)++;
		*addr = *addr +  dev->scatter_buffer_size;
		return 1;
	} else if ((*length) < dev->scatter_buffer_size) {
		memcpy(phys_to_virt(*addr),
		dev->scatter_buffer_va +
		(*idx)*dev->scatter_buffer_size + (*offset),
		*length);
		if ((*offset + *length) == dev->scatter_buffer_size) {
			if (*idx == PPC4XX_LAST_SD)
				*idx = 0;
			else
				(*idx)++;
			*nbytes -= *length;
			*offset = 0;
		} else {
			*nbytes -= *length;
			*offset += *length;
		}

		return 0;
	} else {
		len = (*nbytes <=
		dev->scatter_buffer_size) ?
		(*nbytes) : dev->scatter_buffer_size;
		memcpy(phys_to_virt(*addr),
			dev->scatter_buffer_va +
			(*idx) * dev->scatter_buffer_size + (*offset),
			len);
		*offset = 0;
		*nbytes -= len;

		if (*idx == PPC4XX_LAST_SD)
			*idx = 0;
		else
			(*idx)++;

		return 0;
    }
}

static void crypto4xx_copy_pkt_to_dst(struct crypto4xx_device *dev,
				      struct ce_pd *pd,
				      struct pd_uinfo *pd_uinfo,
				      u32 nbytes,
				      struct scatterlist *dst,
				      u8 type)
{
	dma_addr_t addr;
	u32 this_sd;
	u32 offset;
	u32 len;
	u32 i;
	u32 sg_len;
	struct scatterlist *sg;
	this_sd = pd_uinfo->first_sd;
	offset = 0;
	i = 0;

	while (nbytes) {
		sg = &dst[i];
		sg_len = sg->length;
		addr = dma_map_page(dev->core_dev->device, sg_page(sg),
				sg->offset, sg->length, DMA_TO_DEVICE);

		if (offset == 0) {
			len = (nbytes <= sg->length) ? nbytes : sg->length;
			while (crypto4xx_fill_one_page(dev, &addr, &len,
				&this_sd, &offset, &nbytes))
				;
		if (!nbytes)
			return;
		i++;
		} else {
			len = (nbytes <= (dev->scatter_buffer_size - offset)) ?
				nbytes : (dev->scatter_buffer_size - offset);
			len = (sg->length < len) ? sg->length : len;
			while (crypto4xx_fill_one_page(dev, &addr, &len,
					       &this_sd, &offset, &nbytes))
				;
			if (!nbytes)
				return;
			sg_len -= len;
			if (sg_len) {
				addr += len;
				while (crypto4xx_fill_one_page(dev, &addr,
					&sg_len, &this_sd, &offset, &nbytes))
					;
			}
			i++;
		}
	}
}

static u32 crypto4xx_copy_digest_to_dst(struct pd_uinfo *pd_uinfo,
				 struct crypto4xx_ctx *ctx)
{
	struct dynamic_sa_ctl *sa = (struct dynamic_sa_ctl *)ctx->sa_in;
	struct dynamic_sa_state_record *state_record =
			(struct dynamic_sa_state_record *)ctx->state_record;

	if (sa->sa_command_0.bf.hash_alg == SA_HASH_ALG_SHA1) {
		memcpy((void *)pd_uinfo->dest_va, state_record->save_digest,
				SA_HASH_ALG_SHA1_DIGEST_SIZE);
	}
	return 0;
}

static void crypto4xx_ret_sg_desc(struct crypto4xx_device *dev,
				  struct pd_uinfo *pd_uinfo)
{
	int i;
	struct ce_sd *sd = NULL;

	if (pd_uinfo->first_gd != 0xffffffff) {
		if (pd_uinfo->first_gd  <= pd_uinfo->last_gd) {
			for (i = pd_uinfo->first_gd;
				i <= pd_uinfo->last_gd; i++)
				crypto4xx_put_gd_to_gdr(dev);

		} else {
			for (i = pd_uinfo->first_gd;
				i < PPC4XX_NUM_GD; i++)
				crypto4xx_put_gd_to_gdr(dev);
			for (i = 0; i <= pd_uinfo->last_gd; i++)
				crypto4xx_put_gd_to_gdr(dev);
		}
	}

	if (pd_uinfo->first_sd != 0xffffffff) {
		if (pd_uinfo->first_sd  <= pd_uinfo->last_sd) {
			for (i = pd_uinfo->first_sd;
				i <= pd_uinfo->last_sd; i++) {
				sd = (struct ce_sd *)(dev->sdr +
						sizeof(struct ce_sd)*i);
				sd->ctl.done = 0;
				sd->ctl.rdy = 0;
				crypto4xx_put_sd_to_sdr(dev);
			}
		} else {
			for (i = pd_uinfo->first_sd; i < PPC4XX_NUM_SD; i++) {
				sd = (struct ce_sd *)(dev->sdr +
						sizeof(struct ce_sd)*i);
				sd->ctl.done = 0;
				sd->ctl.rdy = 0;
				crypto4xx_put_sd_to_sdr(dev);
			}
			for (i = 0; i <= pd_uinfo->last_sd; i++) {
				sd = (struct ce_sd *)(dev->sdr +
						sizeof(struct ce_sd)*i);
				sd->ctl.done = 0;
				sd->ctl.rdy = 0;
				crypto4xx_put_sd_to_sdr(dev);
			}
		}
	}

	pd_uinfo->first_gd = pd_uinfo->last_gd = 0xffffffff;
	pd_uinfo->first_sd = pd_uinfo->last_sd = 0xffffffff;
}

static u32 crypto4xx_ablkcipher_done(struct crypto4xx_device *dev,
				struct pd_uinfo *pd_uinfo,
				struct ce_pd *pd)
{
	struct crypto4xx_ctx *ctx;
	struct crypto4xx_ctx *rctx = NULL;
	struct ablkcipher_request *ablk_req;
	struct scatterlist *dst;
	dma_addr_t addr;

	ablk_req = ablkcipher_request_cast(pd_uinfo->async_req);
	ctx  = crypto_tfm_ctx(ablk_req->base.tfm);

	if (ctx->use_rctx == 1)
		rctx = ablkcipher_request_ctx(ablk_req);

	if (pd_uinfo->using_sd) {
		crypto4xx_copy_pkt_to_dst(dev,
					pd,
					pd_uinfo,
					ablk_req->nbytes,
					ablk_req->dst,
					CRYPTO_ALG_TYPE_ABLKCIPHER);
	} else {
		dst = pd_uinfo->dest_va;
		addr = dma_map_page(dev->core_dev->device,
				sg_page(dst), dst->offset,
				dst->length, DMA_FROM_DEVICE);
	}
	crypto4xx_ret_sg_desc(dev, pd_uinfo);
	if (rctx != NULL)
		crypto4xx_free_sa_rctx(rctx);
	if (ablk_req->base.complete != NULL)
		ablk_req->base.complete(&ablk_req->base, 0);
	return 0;
}

static u32 crypto4xx_ahash_done(struct crypto4xx_device *dev,
				struct pd_uinfo *pd_uinfo)
{
	struct crypto4xx_ctx *ctx;
	struct crypto4xx_ctx *rctx = NULL;
	struct ahash_request *ahash_req;

	ahash_req = ahash_request_cast(pd_uinfo->async_req);
	ctx  = crypto_tfm_ctx(ahash_req->base.tfm);

	crypto4xx_copy_digest_to_dst(pd_uinfo,
				     crypto_tfm_ctx(ahash_req->base.tfm));
	crypto4xx_ret_sg_desc(dev, pd_uinfo);

	if (ctx->use_rctx == 1) {
		rctx = ahash_request_ctx(ahash_req);
		if (rctx != NULL) {
			if (rctx->sa_in_dma_addr)
				dma_free_coherent(dev->core_dev->device,
						rctx->sa_len * 4,
						rctx->sa_in,
						rctx->sa_in_dma_addr);
			if (rctx->sa_out_dma_addr)
				dma_free_coherent(dev->core_dev->device,
						rctx->sa_len * 4,
						rctx->sa_out,
						rctx->sa_out_dma_addr);
		}
	}
	/* call user provided callback function x */
	if (ahash_req->base.complete != NULL)
		ahash_req->base.complete(&ahash_req->base, 0);
	return 0;
}

static u32 crypto4xx_pd_done(struct crypto4xx_device *dev, u32 idx)
{
	struct ce_pd *pd;
	struct pd_uinfo *pd_uinfo;

	pd =  dev->pdr + sizeof(struct ce_pd)*idx;
	pd_uinfo = dev->pdr_uinfo + sizeof(struct pd_uinfo)*idx;
	if (crypto_tfm_alg_type(pd_uinfo->async_req->tfm) ==
			CRYPTO_ALG_TYPE_ABLKCIPHER)
		return crypto4xx_ablkcipher_done(dev, pd_uinfo, pd);
	else
		return crypto4xx_ahash_done(dev, pd_uinfo);
}

u32 crypto4xx_alloc_sa_rctx(struct crypto4xx_ctx *ctx,
				struct crypto4xx_ctx *rctx)
{
	int    rc;
	struct dynamic_sa_ctl *sa = NULL;

	if (ctx->direction == CRYPTO_INBOUND) {
		sa = (struct dynamic_sa_ctl *)(ctx->sa_in);
		rctx->sa_in = dma_alloc_coherent(ctx->dev->core_dev->device,
				ctx->sa_len*4,
				&rctx->sa_in_dma_addr, GFP_ATOMIC);
		if (rctx->sa_in == NULL)
			return -ENOMEM;
		memcpy(rctx->sa_in, ctx->sa_in, ctx->sa_len*4);
		rctx->sa_out = NULL;
		rctx->sa_out_dma_addr = 0;
	} else {
		sa = (struct dynamic_sa_ctl *)(ctx->sa_out);
		rctx->sa_out = dma_alloc_coherent(ctx->dev->core_dev->device,
				ctx->sa_len*4,
				&rctx->sa_out_dma_addr, GFP_ATOMIC);
		if (rctx->sa_out == NULL)
			return -ENOMEM;

		memcpy(rctx->sa_out, ctx->sa_out, ctx->sa_len*4);
		rctx->sa_in = NULL;
		rctx->sa_in_dma_addr = 0;
	}

	if (sa->sa_contents & 0x20000000) {
		rc = crypto4xx_alloc_state_record(rctx);
		if (rc != 0) {
			if (rctx->sa_in != NULL)
				dma_free_coherent(rctx->dev->core_dev->device,
						  rctx->sa_len * 4,
						  rctx->sa_in,
						  rctx->sa_in_dma_addr);

			if (rctx->sa_out != NULL)
				dma_free_coherent(rctx->dev->core_dev->device,
						  rctx->sa_len * 4,
						  rctx->sa_out,
						  rctx->sa_out_dma_addr);
			return -ENOMEM;
		}
		memcpy(rctx->state_record, ctx->state_record, 16);
	} else {
		rctx->state_record = NULL;
	}

	rctx->direction = ctx->direction;
	rctx->sa_len = ctx->sa_len;
	rctx->bypass = ctx->bypass;

	return 0;
}

void crypto4xx_free_sa_rctx(struct crypto4xx_ctx *rctx)
{
	if (rctx->sa_in != NULL)
		dma_free_coherent(rctx->dev->core_dev->device,
				  rctx->sa_len * 4,
				  rctx->sa_in,
				  rctx->sa_in_dma_addr);

	if (rctx->sa_out != NULL)
		dma_free_coherent(rctx->dev->core_dev->device,
				rctx->sa_len * 4,
				rctx->sa_out,
				rctx->sa_out_dma_addr);

	crypto4xx_free_state_record(rctx);
	rctx->sa_len = 0;
	rctx->state_record = NULL;
	rctx->state_record_dma_addr = 0;
}

/**
 * Note: Only use this function to copy items that is word aligned.
 */
void crypto4xx_memcpy_le(unsigned int *dst,
			 const unsigned char *buf,
			 int len)
{
	u8 *tmp;
	for (; len >= 4; buf += 4, len -= 4)
		*dst++ = cpu_to_le32(*(unsigned int *) buf);

	tmp = (u8 *)dst;
	switch (len) {
	case 3:
		*tmp++ = 0;
		*tmp++ = *(buf+2);
		*tmp++ = *(buf+1);
		*tmp++ = *buf;
		break;
	case 2:
		*tmp++ = 0;
		*tmp++ = 0;
		*tmp++ = *(buf+1);
		*tmp++ = *(buf);
		break;
	case 1:
		*tmp++ = 0;
		*tmp++ = 0;
		*tmp++ = 0;
		*tmp++ = *(buf);
		break;
	default:
		break;
	}
}

static void crypto4xx_stop_all(struct crypto4xx_core_device *core_dev)
{
	crypto4xx_destroy_pdr(core_dev->dev);
	crypto4xx_destroy_gdr(core_dev->dev);
	crypto4xx_destroy_sdr(core_dev->dev);
	dev_set_drvdata(core_dev->device, NULL);
	iounmap(core_dev->dev->ce_base);
	kfree(core_dev->dev);
	kfree(core_dev);
}

u32 crypto4xx_build_pd_normal(struct crypto4xx_device  *dev,
			      struct crypto_async_request *req,
			      struct crypto4xx_ctx *ctx,
			      struct scatterlist *src,
			      struct scatterlist *dst,
			      u16 datalen,
			      u8 type)
{
	dma_addr_t pd_dma;
	struct dynamic_sa_ctl *sa;
	struct ce_pd *pd;
	u32 pd_entry;
	struct pd_uinfo *pd_uinfo = NULL;

	pd_entry = crypto4xx_get_pd_from_pdr(dev);  /* index to the entry */
	if (pd_entry == ERING_WAS_FULL)
		return -EAGAIN;

	pd_uinfo = (struct pd_uinfo *)((dev->pdr_uinfo) +
				       sizeof(struct pd_uinfo) * pd_entry);
	pd = crypto4xx_get_pdp(dev, &pd_dma, pd_entry);
	pd_uinfo->async_req = req;

	if (ctx->direction == CRYPTO_INBOUND) {
		pd->sa = ctx->sa_in_dma_addr;
		sa = (struct dynamic_sa_ctl *)ctx->sa_in;
	} else {
		pd->sa = ctx->sa_out_dma_addr;
		sa = (struct dynamic_sa_ctl *)ctx->sa_out;
	}

	pd->sa_len = ctx->sa_len;
	pd->src = dma_map_page(dev->core_dev->device,
				sg_page(src), src->offset,
				src->length, DMA_TO_DEVICE);

	/* Disable gather in sa command */
	sa->sa_command_0.bf.gather = 0;

	/* Indicate gather array is not used */
	pd_uinfo->first_gd = pd_uinfo->last_gd = 0xffffffff;
	pd_uinfo->using_sd = 0;
	pd_uinfo->first_sd = pd_uinfo->last_sd = 0xffffffff;
	pd_uinfo->dest_va = dst;
	sa->sa_command_0.bf.scatter = 0;

	if (ctx->is_hash)
		pd->dest = virt_to_phys((void *)dst);
	else
		pd->dest = dma_map_page(dev->core_dev->device,
					sg_page(dst), dst->offset,
					dst->length, DMA_TO_DEVICE);

	pd->pd_ctl.w = ctx->pd_ctl;
	pd->pd_ctl_len.w = 0x00400000 | (ctx->bypass<<24) | datalen ;
	pd_uinfo->state = PD_ENTRY_INUSE;

	crypto4xx_write32(dev, CRYPTO_ENGINE_INT_DESCR_RD, 1);

	return -EINPROGRESS;
}

void crypto4xx_return_pd(struct crypto4xx_device *dev,
			 u32 pd_entry,
			 struct ce_pd *pd,
			 struct pd_uinfo *pd_uinfo)
{
	/* irq should be already disabled */
	dev->pdr_head = pd_entry;
	pd->pd_ctl.w = 0;
	pd->pd_ctl_len.w = 0;
	pd_uinfo->state = PD_ENTRY_FREE;
}

void crypto4xx_return_gather_descriptors(struct crypto4xx_device *dev,
					 struct pd_uinfo *pd_uinfo)
{
	int i;
	struct ce_gd *gd = NULL;
	dma_addr_t gd_dma;

	if (pd_uinfo->first_gd <= pd_uinfo->last_gd) {
		for (i = pd_uinfo->first_gd; i < pd_uinfo->last_gd; i++) {
			gd = crypto4xx_get_gdp(dev, &gd_dma, i);
			gd->ctl_len.ready = 0;
		}
	} else {
		for (i = pd_uinfo->first_gd; i < PPC4XX_NUM_GD; i++) {
			gd = crypto4xx_get_gdp(dev, &gd_dma, i);
			gd->ctl_len.ready = 0;
		}

		for (i = 0; i <= pd_uinfo->last_gd; i++) {
			gd = crypto4xx_get_gdp(dev, &gd_dma, i);
			gd->ctl_len.ready = 0;
		}
	}
	dev->gdr_head = pd_uinfo->first_gd;
}

void crypto4xx_return_scatter_descriptors(struct crypto4xx_device *dev,
					  struct pd_uinfo *pd_uinfo)
{
	int i;
	struct ce_sd *sd = NULL;
	dma_addr_t sd_dma;

	if (pd_uinfo->first_sd <= pd_uinfo->last_sd) {
		for (i = pd_uinfo->first_gd; i < pd_uinfo->last_sd; i++) {
			sd = crypto4xx_get_sdp(dev, &sd_dma, i);
			sd->ctl.rdy = 0;
		}
	} else {
		for (i = pd_uinfo->first_sd; i < PPC4XX_NUM_SD; i++) {
			sd = crypto4xx_get_sdp(dev, &sd_dma, i);
			sd->ctl.rdy = 0;
		}
		for (i = 0; i <= pd_uinfo->last_sd; i++) {
			sd = crypto4xx_get_sdp(dev, &sd_dma, i);
			sd->ctl.rdy = 0;
		}
	}
	dev->sdr_head = pd_uinfo->first_sd;
}

static u32 crypto4xx_build_pd(struct crypto4xx_device  *dev,
		       struct crypto_async_request *req,
		       struct crypto4xx_ctx *ctx,
		       struct scatterlist *src,
		       struct scatterlist *dst,
		       u16 datalen,
		       u8 type)
{
	dma_addr_t addr, pd_dma, sd_dma, gd_dma;
	struct dynamic_sa_ctl *sa;
	struct scatterlist *sg;
	u32 pd_entry;
	struct ce_pd *pd;
	struct pd_uinfo *pd_uinfo;
	unsigned int nbytes = datalen, idx;
	struct ce_gd *gd;
	u32 gd_idx = 0;
	struct ce_sd *sd;
	u32 sd_idx = 0;

	if (sg_is_last(src) && (sg_is_last(dst) || ctx->is_hash))
		return crypto4xx_build_pd_normal(dev,
						req,
						ctx,
						src,
						dst,
						datalen,
						type);

	/*
	 * We need to use scatter/gather array
	 * Crypto Engine require consecutive descriptors
	 * disable irq to make sure not to preempted here
	 */
	local_irq_disable();
	pd_entry = crypto4xx_get_pd_from_pdr_nolock(dev);
	if (pd_entry == ERING_WAS_FULL) {
		local_irq_enable();
		return -EAGAIN;
	}
	pd = crypto4xx_get_pdp(dev, &pd_dma, pd_entry);
	pd_uinfo = (struct pd_uinfo *)((dev->pdr_uinfo) +
			sizeof(struct pd_uinfo)*pd_entry);
	pd_uinfo->async_req = req;

	if (ctx->direction == CRYPTO_INBOUND) {
		pd->sa = ctx->sa_in_dma_addr;
		sa = (struct dynamic_sa_ctl *)ctx->sa_in;
	} else {
		pd->sa = ctx->sa_out_dma_addr;
		sa = (struct dynamic_sa_ctl *)ctx->sa_out;
	}

	pd->sa_len = ctx->sa_len;

	/* If first is last then we are single */
	if (sg_is_last(src)) {
		pd->src = dma_map_page(dev->core_dev->device, sg_page(src),
				    src->offset, src->length,
				    DMA_TO_DEVICE);
		/* Disable gather in sa command */
		sa->sa_command_0.bf.gather = 0;
		/* Indicate gather array is not used */
		pd_uinfo->first_gd = pd_uinfo->last_gd = 0xffffffff;
	} else {
		src = &src[0];
		/* get first gd we are going to use */
		gd_idx = crypto4xx_get_gd_from_gdr(dev);
		if (gd_idx == ERING_WAS_FULL)
			goto err_get_first_gd;

		pd_uinfo->first_gd = gd_idx;
		gd = crypto4xx_get_gdp(dev, &gd_dma, gd_idx);
		pd->src = gd_dma;
		/* Enable gather */
		sa->sa_command_0.bf.gather = 1;
		idx = 0;

		/*
		 * walk the sg, and setup gather array
		 * CRYPTO ENGINE DMA is byte align,
		 * so we can use ptr directly from sg
		 */
		while (nbytes != 0) {
			sg = &src[idx];
			addr = dma_map_page(dev->core_dev->device, sg_page(sg),
					    sg->offset, sg->length,
					    DMA_TO_DEVICE);
			gd->ptr = addr;
			gd->ctl_len.len = sg->length;
			gd->ctl_len.done = 0;
			gd->ctl_len.ready = 1;
			/* when using tcrypt, sum of sg->lenght maybe > nbytps*/
			if (sg->length >= nbytes)
				break;
			nbytes -= sg->length;
			/* Get first gd we are going to use */
			gd_idx = crypto4xx_get_gd_from_gdr(dev);
			if (gd_idx == ERING_WAS_FULL)
				goto err_get_gd;

			gd = crypto4xx_get_gdp(dev, &gd_dma, gd_idx);
			pd_uinfo->last_gd = gd_idx;
			idx++;
		}
	}

	if (ctx->is_hash || sg_is_last(dst)) {
		/*
		 * we know application give us dst a whole piece of memory
		 * no need to use scatter ring
		 */
		pd_uinfo->using_sd = 0;
		pd_uinfo->first_sd = pd_uinfo->last_sd = 0xffffffff;
		pd_uinfo->dest_va = dst;
		sa->sa_command_0.bf.scatter = 0;
		if (ctx->is_hash)
			pd->dest = virt_to_phys((void *)dst);
		else
			pd->dest = dma_map_page(dev->core_dev->device,
						sg_page(dst),
						dst->offset, dst->length,
						DMA_TO_DEVICE);
	} else {
		nbytes = datalen;
		sa->sa_command_0.bf.scatter = 1;
		pd_uinfo->using_sd = 1;

		sd_idx = crypto4xx_get_sd_from_sdr(dev);
		if (sd_idx == ERING_WAS_FULL)
			goto err_get_first_sd;

		pd_uinfo->first_sd = pd_uinfo->last_sd = sd_idx;
		sd = crypto4xx_get_sdp(dev, &sd_dma, sd_idx);
		pd->dest = sd_dma;
		wmb();
		/* setup scatter descriptor */
		sd->ctl.done = 0;
		sd->ctl.rdy = 1;
		/* sd->ptr should be setup by sd_init routine*/
		if (nbytes >= PPC4XX_SD_BUFFER_SIZE)
			nbytes -= PPC4XX_SD_BUFFER_SIZE;
		else if (nbytes < PPC4XX_SD_BUFFER_SIZE)
			nbytes = 0;
		while (nbytes) {
			sd_idx = crypto4xx_get_sd_from_sdr(dev);
			if (sd_idx == ERING_WAS_FULL)
				goto err_get_sd;

			sd = crypto4xx_get_sdp(dev, &sd_dma, sd_idx);
			pd_uinfo->last_sd = sd_idx;
			/* setup scatter descriptor */
			sd->ctl.done = 0;
			sd->ctl.rdy = 1;
			if (nbytes >= PPC4XX_SD_BUFFER_SIZE)
				nbytes -= PPC4XX_SD_BUFFER_SIZE;
			else
				nbytes = 0;
		}
	}
	pd->pd_ctl.w = ctx->pd_ctl;
	pd->pd_ctl_len.w = 0x00400000 | (ctx->bypass<<24) | datalen;
	pd_uinfo->state = PD_ENTRY_INUSE;
	crypto4xx_write32(dev, CRYPTO_ENGINE_INT_DESCR_RD, 1);
	local_irq_enable();
	return -EINPROGRESS;

err_get_sd:
	/* return all scatter descriptor just got */
	crypto4xx_return_scatter_descriptors(dev, pd_uinfo);
err_get_gd:
err_get_first_sd:
	/* return all gather descriptors just got */
	if (pd_uinfo->first_gd != 0xffffffff)
		crypto4xx_return_gather_descriptors(dev, pd_uinfo);
err_get_first_gd:
	/* return the packet descriptor just got */
	crypto4xx_return_pd(dev, pd_entry, pd, pd_uinfo);
	local_irq_enable();
	return -EAGAIN;
}

int crypto4xx_handle_req(struct crypto_async_request *req)
{
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(req->tfm);
	struct crypto4xx_device *dev = ctx->dev;
	struct crypto4xx_ctx *rctx;
	int ret = -EAGAIN;

	if (crypto_tfm_alg_type(req->tfm) == CRYPTO_ALG_TYPE_ABLKCIPHER) {
		struct ablkcipher_request *ablk_req;
		ablk_req = ablkcipher_request_cast(req);
		if (ctx->use_rctx) {
			rctx = ablkcipher_request_ctx(ablk_req);
			return crypto4xx_build_pd(dev, req, rctx,
						  ablk_req->src, ablk_req->dst,
						  ablk_req->nbytes, ABLK);
		} else {
			return crypto4xx_build_pd(dev, req, ctx,
						  ablk_req->src, ablk_req->dst,
						  ablk_req->nbytes,
						  ABLK);
		}
	} else {
		struct ahash_request *ahash_req;
		ahash_req = ahash_request_cast(req);
		if (ctx->use_rctx) {
			rctx = ahash_request_ctx(ahash_req);
			return crypto4xx_build_pd(dev, req, rctx,
				ahash_req->src,
				(struct scatterlist *) ahash_req->result,
				ahash_req->nbytes,
				AHASH);
		} else {
			return crypto4xx_build_pd(dev, req, ctx,
				ahash_req->src,
				(struct scatterlist *) ahash_req->result,
				ahash_req->nbytes,
				AHASH);
		}
	}
	return ret;
}

/**
 * Algorithm Registration Functions
 */
static int crypto4xx_alg_init(struct crypto_tfm *tfm)
{
	struct crypto_alg    *alg = tfm->__crt_alg;
	struct crypto4xx_alg *amcc_alg = crypto_alg_to_crypto4xx_alg(alg);
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->dev = amcc_alg->dev;
	ctx->sa_in = NULL;
	ctx->sa_out = NULL;
	ctx->sa_in_dma_addr = 0;
	ctx->sa_out_dma_addr = 0;
	ctx->sa_len = 0;

	if (alg->cra_type == &crypto_ablkcipher_type)
		tfm->crt_ablkcipher.reqsize = sizeof(struct crypto4xx_ctx);
	else if (alg->cra_type == &crypto_ahash_type)
		tfm->crt_ahash.reqsize = sizeof(struct crypto4xx_ctx);
	return 0;
}

static void crypto4xx_alg_exit(struct crypto_tfm *tfm)
{
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(tfm);
	crypto4xx_free_sa(ctx);
	crypto4xx_free_state_record(ctx);
}

int crypto4xx_register_alg(struct crypto4xx_device *sec_dev,
			   struct crypto_alg *crypto_alg, int array_size)
{
	struct crypto4xx_alg *alg;
	int i;
	int rc = 0;

	for (i = 0; i < array_size; i++) {
		alg = kzalloc(sizeof(struct crypto4xx_alg), GFP_KERNEL);
		if (!alg)
			return -ENOMEM;

		alg->alg = crypto_alg[i];
		INIT_LIST_HEAD(&alg->alg.cra_list);
		if (alg->alg.cra_init == NULL)
			alg->alg.cra_init = crypto4xx_alg_init;
		if (alg->alg.cra_exit == NULL)
			alg->alg.cra_exit = crypto4xx_alg_exit;
		alg->dev = sec_dev;
		list_add_tail(&alg->entry, &sec_dev->alg_list);
		rc = crypto_register_alg(&alg->alg);
		if (rc) {
			list_del(&alg->entry);
			kfree(alg);
			return rc;
		}
	}
	return rc;
}

static void crypto4xx_unregister_alg(struct crypto4xx_device *sec_dev)
{
	struct crypto4xx_alg *alg, *tmp;

	list_for_each_entry_safe(alg, tmp, &sec_dev->alg_list, entry) {
		list_del(&alg->entry);
		crypto_unregister_alg(&alg->alg);
		kfree(alg);
	}
}

static void crypto4xx_bh_tasklet_cb(unsigned long data)
{
	struct device *dev = (struct device *)data;
	struct crypto4xx_core_device *core_dev = dev_get_drvdata(dev);
	struct pd_uinfo *pd_uinfo;
	struct ce_pd *pd;
	u32 tail;

	while (core_dev->dev->pdr_head != core_dev->dev->pdr_tail) {
		tail = core_dev->dev->pdr_tail;
		pd_uinfo = core_dev->dev->pdr_uinfo +
			sizeof(struct pd_uinfo)*tail;
		pd =  core_dev->dev->pdr + sizeof(struct ce_pd) * tail;
		if ((pd_uinfo->state == PD_ENTRY_INUSE) &&
				   pd->pd_ctl.bf.pe_done &&
				   !pd->pd_ctl.bf.host_ready) {
			pd->pd_ctl.bf.pe_done = 0;
			crypto4xx_pd_done(core_dev->dev, tail);
			crypto4xx_put_pd_to_pdr(core_dev->dev, tail);
			pd_uinfo->state = PD_ENTRY_FREE;
		} else {
			/* if tail not done, break */
			break;
		}
	}
}

/**
 * Top Half of isr.
 */
static irqreturn_t crypto4xx_ce_interrupt_handler(int irq, void *data)
{
	struct device *dev = (struct device *)data;
	struct crypto4xx_core_device *core_dev = dev_get_drvdata(dev);

	if (core_dev->dev->ce_base == 0)
		return 0;

	crypto4xx_write32(core_dev->dev,
			CRYPTO_ENGINE_INT_CLR, 0x3ffff);

	tasklet_schedule(&core_dev->tasklet);

	return IRQ_HANDLED;
}

/**
 * Module Initialization Routine
 */
static int __init crypto4xx_crypto_probe(struct of_device *ofdev,
					 const struct of_device_id *match)
{
	int rc;
	struct resource res;
	struct device *dev = &ofdev->dev;
	struct crypto4xx_core_device *core_dev;

	rc = of_address_to_resource(ofdev->node, 0, &res);
	if (rc)
		return -ENODEV;

	if (of_find_compatible_node(NULL, NULL, "amcc,crypto-460ex")) {
		mtdcri(SDR0, 0x201, mfdcri(SDR0, 0x201) | 0x08000000);
		mtdcri(SDR0, 0x201, mfdcri(SDR0, 0x201) & ~0x08000000);
	} else if (of_find_compatible_node(NULL, NULL, "amcc,crypto-405ex")) {
		mtdcri(SDR0, 0x200, mfdcri(SDR0, 0x200) | 0x00000008);
		mtdcri(SDR0, 0x200, mfdcri(SDR0, 0x200) & ~0x00000008);
	} else if (of_find_compatible_node(NULL, NULL, "amcc,crypto-460sx")) {
		mtdcri(SDR0, 0x201, mfdcri(SDR0, 0x201) | 0x20000000);
		mtdcri(SDR0, 0x201, mfdcri(SDR0, 0x201) & ~0x20000000);
	} else {
		printk(KERN_ERR "Crypto Function Not supported!\n");
		return -EINVAL;
	}

	core_dev = kzalloc(sizeof(struct crypto4xx_core_device), GFP_KERNEL);
	if (!core_dev)
		return -ENOMEM;

	dev_set_drvdata(dev, core_dev);
	core_dev->ofdev = ofdev;
	core_dev->dev = kzalloc(sizeof(struct crypto4xx_device), GFP_KERNEL);
	if (!core_dev->dev)
		goto err_alloc_dev;

	core_dev->dev->core_dev = core_dev;
	core_dev->device = dev;
	INIT_LIST_HEAD(&core_dev->dev->alg_list);
	rc = crypto4xx_build_pdr(core_dev->dev);
	if (rc)
		goto err_build_pdr;

	rc = crypto4xx_build_gdr(core_dev->dev);
	if (rc)
		goto err_build_gdr;

	rc = crypto4xx_build_sdr(core_dev->dev);
	if (rc)
		goto err_build_sdr;

	/* Init tasklet for bottom half processing */
	tasklet_init(&core_dev->tasklet, crypto4xx_bh_tasklet_cb,
			(unsigned long)dev);

	/* Register for Crypto isr, Crypto Engine IRQ */
	core_dev->irq = irq_of_parse_and_map(ofdev->node, 0);
	rc = request_irq(core_dev->irq, crypto4xx_ce_interrupt_handler, 0,
			 core_dev->dev->name, dev);
	if (rc)
		goto err_request_irq;

	core_dev->dev->ce_base = of_iomap(ofdev->node, 0);
	if (!core_dev->dev->ce_base) {
		dev_err(dev, "failed to of_iomap\n");
		goto err_start_dev;
	}

	/* need to setup pdr, rdr, gdr and sdr */
	rc = crypto4xx_init(core_dev->dev);
	if (rc)
		goto err_start_dev;

	/* Register security algorithms with Linux CryptoAPI */
	rc = crypto4xx_register_basic_alg(core_dev->dev);
	if (rc)
		goto err_register_alg;

	printk(KERN_INFO "Loaded AMCC PPC4xx crypto "
	       "accelerator driver v%s\n", PPC4XX_SEC_VERSION_STR);

	return rc;

err_start_dev:
err_register_alg:
	iounmap(core_dev->dev->ce_base);
	free_irq(core_dev->irq, dev);
	irq_dispose_mapping(core_dev->irq);
err_request_irq:
err_build_sdr:
	crypto4xx_destroy_gdr(core_dev->dev);
err_build_gdr:
	crypto4xx_destroy_pdr(core_dev->dev);
err_build_pdr:
	kfree(core_dev->dev);
err_alloc_dev:
	kfree(core_dev);
	return rc;
}

static int __exit crypto4xx_crypto_remove(struct of_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct crypto4xx_core_device *core_dev = dev_get_drvdata(dev);

	free_irq(core_dev->irq, dev);
	irq_dispose_mapping(core_dev->irq);

	tasklet_kill(&core_dev->tasklet);
	/* Un-register with Linux CryptoAPI */
	crypto4xx_unregister_alg(core_dev->dev);
	/* Free all allocated memory */
	crypto4xx_stop_all(core_dev);

	printk(KERN_INFO "Unloaded AMCC PPC4xx crypto "
	       "accelerator driver v%s\n", PPC4XX_SEC_VERSION_STR);

	return 0;
}

static struct of_device_id crypto4xx_crypto_match[] = {
	{ .compatible      = "amcc,ppc4xx-crypto",},
	{ },
};

static struct of_platform_driver crypto4xx_crypto_driver = {
	.name		= "crypto4xx-crypto",
	.match_table	= crypto4xx_crypto_match,
	.probe		= crypto4xx_crypto_probe,
	.remove		= crypto4xx_crypto_remove,
};

static int __init crypto4xx_lsec_init(void)
{
	return of_register_platform_driver(&crypto4xx_crypto_driver);
}

static void __exit crypto4xx_lsec_exit(void)
{
	of_unregister_platform_driver(&crypto4xx_crypto_driver);
}

module_init(crypto4xx_lsec_init);
module_exit(crypto4xx_lsec_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("James Hsiao <jhsiao@amcc.com>");
MODULE_DESCRIPTION("Driver for AMCC PPC4xx crypto accelerator");

