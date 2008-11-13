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
 * @file crypto4xx_reg_def.h
 *
 * This filr defines the register set for Security Subsystem
 */

#ifndef __CRYPTO_ENGINE_REG_DEF_H__
#define __CRYPTO_ENGINE_REG_DEF_H__

/* CRYPTO_ENGINE Register offset */
#define CRYPTO_ENGINE_DESCRIPTOR			0x00000000
#define CRYPTO_ENGINE_CTRL_STAT				0x00000000
#define CRYPTO_ENGINE_SOURCE				0x00000004
#define CRYPTO_ENGINE_DEST				0x00000008
#define CRYPTO_ENGINE_SA				0x0000000C
#define CRYPTO_ENGINE_SA_LENGTH				0x00000010
#define CRYPTO_ENGINE_LENGTH				0x00000014


#define CRYPTO_ENGINE_PE_DMA_CFG			0x00000040
#define CRYPTO_ENGINE_PE_DMA_STAT			0x00000044
#define CRYPTO_ENGINE_PDR_BASE				0x00000048
#define CRYPTO_ENGINE_RDR_BASE				0x0000004c
#define CRYPTO_ENGINE_RING_SIZE				0x00000050
#define CRYPTO_ENGINE_RING_CTRL				0x00000054
#define CRYPTO_ENGINE_INT_RING_STAT			0x00000058
#define CRYPTO_ENGINE_EXT_RING_STAT			0x0000005c
#define CRYPTO_ENGINE_IO_THRESHOLD			0x00000060
#define CRYPTO_ENGINE_GATH_RING_BASE			0x00000064
#define CRYPTO_ENGINE_SCAT_RING_BASE			0x00000068
#define CRYPTO_ENGINE_PART_RING_SIZE			0x0000006c
#define CRYPTO_ENGINE_PART_RING_CFG		        0x00000070

#define CRYPTO_ENGINE_PDR_BASE_UADDR			0x00000080
#define CRYPTO_ENGINE_RDR_BASE_UADDR			0x00000084
#define CRYPTO_ENGINE_PKT_SRC_UADDR			0x00000088
#define CRYPTO_ENGINE_PKT_DEST_UADDR			0x0000008c
#define CRYPTO_ENGINE_SA_UADDR				0x00000090
#define CRYPTO_ENGINE_GATH_RING_BASE_UADDR		0x000000A0
#define CRYPTO_ENGINE_SCAT_RING_BASE_UADDR		0x000000A4

#define CRYPTO_ENGINE_SEQ_RD				0x00000408
#define CRYPTO_ENGINE_SEQ_MASK_RD			0x0000040C

#define CRYPTO_ENGINE_SA_CMD_0				0x00010600
#define CRYPTO_ENGINE_SA_CMD_1				0x00010604

#define CRYPTO_ENGINE_STATE_PTR				0x000106dc
#define CRYPTO_ENGINE_STATE_IV				0x00010700
#define CRYPTO_ENGINE_STATE_HASH_BYTE_CNT_0		0x00010710
#define CRYPTO_ENGINE_STATE_HASH_BYTE_CNT_1		0x00010714

#define CRYPTO_ENGINE_STATE_IDIGEST_0			0x00010718
#define CRYPTO_ENGINE_STATE_IDIGEST_1			0x0001071c

#define CRYPTO_ENGINE_DATA_IN				0x00018000
#define CRYPTO_ENGINE_DATA_OUT			        0x0001c000


#define CRYPTO_ENGINE_INT_UNMASK_STAT			0x000500a0
#define CRYPTO_ENGINE_INT_MASK_STAT			0x000500a4
#define CRYPTO_ENGINE_INT_CLR				0x000500a4
#define CRYPTO_ENGINE_INT_EN				0x000500a8

#define CRYPTO_ENGINE_INT_PKA				0x00000002
#define CRYPTO_ENGINE_INT_PDR_DONE			0x00008000
#define CRYPTO_ENGINE_INT_MA_WR_ERR			0x00020000
#define CRYPTO_ENGINE_INT_MA_RD_ERR			0x00010000
#define CRYPTO_ENGINE_INT_PE_ERR			0x00000200
#define CRYPTO_ENGINE_INT_USER_DMA_ERR			0x00000040
#define CRYPTO_ENGINE_INT_SLAVE_ERR			0x00000010
#define CRYPTO_ENGINE_INT_MASTER_ERR			0x00000008
#define CRYPTO_ENGINE_INT_ERROR				0x00030258

#define CRYPTO_ENGINE_INT_CFG				0x000500ac
#define CRYPTO_ENGINE_INT_DESCR_RD			0x000500b0
#define CRYPTO_ENGINE_INT_DESCR_CNT			0x000500b4
#define CRYPTO_ENGINE_INT_TIMEOUT_CNT			0x000500b8

#define CRYPTO_ENGINE_DC_CTRL				0x00060080
#define CRYPTO_ENGINE_DEVICE_ID				0x00060084
#define CRYPTO_ENGINE_DEVICE_INFO			0x00060088
#define CRYPTO_ENGINE_DMA_USER_SRC			0x00060094
#define CRYPTO_ENGINE_DMA_USER_DEST			0x00060098
#define CRYPTO_ENGINE_DMA_USER_CMD			0x0006009C

#define CRYPTO_ENGINE_DMA_CFG	        		0x000600d4
#define CRYPTO_ENGINE_BYTE_ORDER_CFG 			0x000600d8
#define CRYPTO_ENGINE_ENDIAN_CFG			0x000600d8

#define CRYPTO_ENGINE_PRNG_STAT				0x00070000
#define CRYPTO_ENGINE_PRNG_CTRL				0x00070004
#define CRYPTO_ENGINE_PRNG_SEED_L			0x00070008
#define CRYPTO_ENGINE_PRNG_SEED_H			0x0007000c

#define CRYPTO_ENGINE_PRNG_RES_0			0x00070020
#define CRYPTO_ENGINE_PRNG_RES_1			0x00070024
#define CRYPTO_ENGINE_PRNG_RES_2			0x00070028
#define CRYPTO_ENGINE_PRNG_RES_3			0x0007002C

#define CRYPTO_ENGINE_PRNG_LFSR_L			0x00070030
#define CRYPTO_ENGINE_PRNG_LFSR_H			0x00070034

/**
 * Initilize CRYPTO ENGINE registers, and memory bases.
 */

#define PPC4XX_PDR_POLL			0x3ff
#define PPC4XX_OUTPUT_THRESHOLD		2
#define PPC4XX_INPUT_THRESHOLD		2
#define PPC4XX_PD_SIZE			6
#define CRYPTO_CTX_DONE_INT		0x2000
#define CRYPTO_PD_DONE_INT		0x8000
/**
 * all follow define are ad hoc
 */
#define PPC4XX_RING_RETRY		100
#define PPC4XX_RING_POLL		100
#define PPC4XX_SDR_SIZE			PPC4XX_NUM_SD
#define PPC4XX_GDR_SIZE			PPC4XX_NUM_GD

/**
  * Generic Security Association (SA) with all possible fields. These will
 * never likely used except for reference purpose. These structure format
 * can be not changed as the hardware expects them to be layout as defined.
 * Field can be removed or reduced but ordering can not be changed.
 */

#define CRYPTO_ENGINE_DMA_CFG_OFFSET			0x40
union ce_pe_dma_cfg {
	struct {
		u32 rsv:7;
		u32 dir_host:1;
		u32 rsv1:2;
		u32 bo_td_en:1;
		u32 dis_pdr_upd:1;
		u32 bo_sgpd_en:1;
		u32 bo_data_en:1;
		u32 bo_sa_en:1;
		u32 bo_pd_en:1;
		u32 rsv2:4;
		u32 dynamic_sa_en:1;
		u32 pdr_mode:2;
		u32 pe_mode:1;
		u32 rsv3:5;
		u32 reset_sg:1;
		u32 reset_pdr:1;
		u32 reset_pe:1;
	} bf;
    u32 w;
} __attribute__((packed));

#define CRYPTO_ENGINE_PDR_BASE_OFFSET			0x48
#define CRYPTO_ENGINE_RDR_BASE_OFFSET			0x4c
#define CRYPTO_ENGINE_RING_SIZE_OFFSET			0x50
union ce_ring_size {
	struct {
		u32 ring_offset:16;
		u32 rsv:6;
		u32 ring_size:10;
	} bf;
    u32 w;
} __attribute__((packed));

#define CRYPTO_ENGINE_RING_CONTROL_OFFSET		0x54
union ce_ring_contol {
	struct {
		u32 continuous:1;
		u32 rsv:5;
		u32 ring_retry_divisor:10;
		u32 rsv1:4;
		u32 ring_poll_divisor:10;
	} bf;
    u32 w;
} __attribute__((packed));

#define CRYPTO_ENGINE_IO_THRESHOLD_OFFSET		0x60
union ce_io_threshold {
	struct {
		u32 rsv:6;
		u32 output_threshold:10;
		u32 rsv1:6;
		u32 input_threshold:10;
	} bf;
    u32 w;
} __attribute__((packed));

#define CRYPTO_ENGINE_GATHER_RING_BASE_OFFSET		0x64
#define CRYPTO_ENGINE_SCATTER_RING_BASE_OFFSET		0x68

union ce_part_ring_size  {
	struct {
		u32 sdr_size:16;
		u32 gdr_size:16;
	} bf;
    u32 w;
} __attribute__((packed));

#define MAX_BURST_SIZE_32	0
#define MAX_BURST_SIZE_64	1
#define MAX_BURST_SIZE_128	2
#define MAX_BURST_SIZE_256	3

/* gather descriptor control length */
struct gd_ctl_len {
	u32 len:16;
	u32 rsv:14;
	u32 done:1;
	u32 ready:1;
} __attribute__((packed));

struct ce_gd {
	u32 ptr;
	struct gd_ctl_len ctl_len;
} __attribute__((packed));

struct sd_ctl {
	u32 ctl:30;
	u32 done:1;
	u32 rdy:1;
} __attribute__((packed));

struct ce_sd {
    u32 ptr;
	struct sd_ctl ctl;
} __attribute__((packed));

#define PD_PAD_CTL_32	0x10
#define PD_PAD_CTL_64	0x20
#define PD_PAD_CTL_128	0x40
#define PD_PAD_CTL_256	0x80
union ce_pd_ctl {
	struct {
		u32 pd_pad_ctl:8;
		u32 status:8;
		u32 next_hdr:8;
		u32 rsv:2;
		u32 cached_sa:1;
		u32 hash_final:1;
		u32 init_arc4:1;
		u32 rsv1:1;
		u32 pe_done:1;
		u32 host_ready:1;
	} bf;
	u32 w;
} __attribute__((packed));

union ce_pd_ctl_len {
	struct {
		u32 bypass:8;
		u32 pe_done:1;
		u32 host_ready:1;
		u32 rsv:2;
		u32 pkt_len:20;
	} bf;
	u32 w;
} __attribute__((packed));

struct ce_pd {
	union ce_pd_ctl   pd_ctl;
	dma_addr_t src;
	dma_addr_t dest;
	dma_addr_t sa;                 /* get from ctx->sa_dma_addr */
	u32 sa_len;                    /* only if dynamic sa is used */
	union ce_pd_ctl_len pd_ctl_len;

} __attribute__((packed));
#endif
