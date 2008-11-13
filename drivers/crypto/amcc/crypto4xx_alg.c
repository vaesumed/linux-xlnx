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
 * @file crypto4xx_alg.c
 *
 * This file implements the Linux crypto algorithms.
 *
 * Changes:
 *	James Hsiao:	10/04/08
 *			crypto4xx_encrypt, crypto4xx_decrypt,
 *			crypto4xx_setkey_aes_cbc not inline.
 *			return from crypto4xx_alloc_sa now check return code
 *			instead of of check sa_in_dma_addr and sa_out_dma_addr
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mod_devicetable.h>
#include <linux/interrupt.h>
#include <linux/spinlock_types.h>
#include <linux/highmem.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <linux/hash.h>
#include <crypto/internal/hash.h>
#include <linux/pci.h>
#include <linux/rtnetlink.h>
#include <crypto/aead.h>
#include <crypto/algapi.h>
#include <crypto/des.h>
#include <crypto/authenc.h>

#include "crypto4xx_reg_def.h"
#include "crypto4xx_sa.h"
#include "crypto4xx_core.h"

static int crypto4xx_encrypt(struct ablkcipher_request *req)
{
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct crypto4xx_ctx *rctx = ablkcipher_request_ctx(req);
	int    rc;

	/*
	 * Application only provided ptr for the rctx
	 * we alloc memory for it.
	 * And along we alloc memory for the sa in it.
	 */
	ctx->use_rctx = 1;
	ctx->direction = CRYPTO_OUTBOUND;
	rc = crypto4xx_alloc_sa_rctx(ctx, rctx);
	if (rc)
		return -ENOMEM;
	memcpy(rctx->sa_out +
		get_dynamic_sa_offset_state_ptr_field(rctx),
		&rctx->state_record_dma_addr, 4);
	/* copy req->iv to state_record->iv */
	if (req->info)
		crypto4xx_memcpy_le(rctx->state_record, req->info,
				get_dynamic_sa_iv_size(rctx));
	else
		memset(rctx->state_record, 0, get_dynamic_sa_iv_size(rctx));
	rctx->hash_final = 0;
	rctx->is_hash = 0;
	rctx->pd_ctl = 0x1;
	rctx->direction = CRYPTO_OUTBOUND;

	return crypto4xx_handle_req(&req->base);
}

static int crypto4xx_decrypt(struct ablkcipher_request *req)
{
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct crypto4xx_ctx *rctx = ablkcipher_request_ctx(req);
	int    rc;

	/*
	 * Application only provided ptr for the rctx
	 * we alloc memory for it.
	 * And along we alloc memory for the sa in it
	 */
	ctx->use_rctx = 1;
	ctx->direction = CRYPTO_INBOUND;
	rc = crypto4xx_alloc_sa_rctx(ctx, rctx);
	if (rc != 0)
		return -ENOMEM;
	memcpy(rctx->sa_in +
		get_dynamic_sa_offset_state_ptr_field(rctx),
		&rctx->state_record_dma_addr, 4);
	/* copy req->iv to state_record->iv */
	if (req->info)
		crypto4xx_memcpy_le(rctx->state_record, req->info,
				get_dynamic_sa_iv_size(rctx));
	else
		memset(rctx->state_record, 0, get_dynamic_sa_iv_size(rctx));

	rctx->hash_final = 0;
	rctx->is_hash = 0;
	rctx->pd_ctl = 1;
	rctx->direction = CRYPTO_INBOUND;

	return crypto4xx_handle_req(&req->base);
}

/**
 * AES Functions
 */
static int crypto4xx_setkey_aes(struct crypto_ablkcipher *cipher,
				 const u8 *key,
				 unsigned int keylen,
				 unsigned char cm,
				 u8 fb)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(tfm);
	struct dynamic_sa_ctl *sa;
	int    rc;

	if ((keylen != 256/8) &&  (keylen != 128/8) &&  (keylen != 192/8)) {
		crypto_ablkcipher_set_flags(cipher,
				CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -1;
	}

	/* Create SA */
	if (ctx->sa_in_dma_addr || ctx->sa_out_dma_addr)
		crypto4xx_free_sa(ctx);

	if (keylen == 256/8)
		rc = crypto4xx_alloc_sa(ctx, SA_AES256_LEN);
	else if (keylen == 192/8)
		rc = crypto4xx_alloc_sa(ctx, SA_AES192_LEN);
	else
		rc = crypto4xx_alloc_sa(ctx, SA_AES128_LEN);

	if (rc)
		return -ENOMEM;

	if (ctx->state_record_dma_addr == 0) {
		rc = crypto4xx_alloc_state_record(ctx);
		if (rc != 0) {
			crypto4xx_free_sa(ctx);
			return -ENOMEM;
		}
	}
	/* Setup SA */
	sa = (struct dynamic_sa_ctl *)ctx->sa_in;
	ctx->hash_final = 0;
	sa->sa_command_0.bf.hash_alg = SA_HASH_ALG_NULL;
	sa->sa_command_0.bf.cipher_alg = SA_CIPHER_ALG_AES;
	sa->sa_command_0.bf.opcode = SA_OPCODE_ENCRYPT;
	sa->sa_command_0.bf.load_iv = 2;

	sa->sa_command_1.bf.sa_rev = 1;
	sa->sa_command_1.bf.copy_payload = 0;
	sa->sa_command_1.bf.crypto_mode31 = (cm & 4) >> 2;
	sa->sa_command_1.bf.crypto_mode9_8 = (cm & 3);
	sa->sa_command_1.bf.feedback_mode = fb;
	sa->sa_command_1.bf.mutable_bit_proc = 1;

	if (keylen >= 256/8) {
		crypto4xx_memcpy_le(((struct dynamic_sa_aes256 *)sa)->key,
					key, keylen);
		sa->sa_contents = SA_AES256_CONTENTS;
		sa->sa_command_1.bf.key_len = SA_AES_KEY_LEN_256;
	} else if (keylen >= 192/8) {
		crypto4xx_memcpy_le(((struct dynamic_sa_aes192 *)sa)->key,
					key, keylen);
		sa->sa_contents = SA_AES192_CONTENTS;
		sa->sa_command_1.bf.key_len = SA_AES_KEY_LEN_192;
	} else {
		crypto4xx_memcpy_le(((struct dynamic_sa_aes128 *)sa)->key,
					key, keylen);
		sa->sa_contents = SA_AES128_CONTENTS;
		sa->sa_command_1.bf.key_len = SA_AES_KEY_LEN_128;
	}
	ctx->is_hash = 0;
	ctx->direction = CRYPTO_INBOUND;
	sa->sa_command_0.bf.dir = CRYPTO_INBOUND;
	memcpy(ctx->sa_in + get_dynamic_sa_offset_state_ptr_field(ctx),
			(void *)&(ctx->state_record_dma_addr), 4);
	memcpy(ctx->sa_out, ctx->sa_in, ctx->sa_len*4);
	sa = (struct dynamic_sa_ctl *)ctx->sa_out;
	sa->sa_command_0.bf.dir = CRYPTO_OUTBOUND;

	return 0;
}

static inline int crypto4xx_setkey_aes_cbc(struct crypto_ablkcipher *cipher,
					   const u8 *key, unsigned int keylen)
{
	return crypto4xx_setkey_aes(cipher, key, keylen,
				    CRYPTO_MODE_CBC,
				    CRYPTO_FEEDBACK_MODE_NO_FB);
}

/**
 * HASH SHA1 Functions
 */
static int crypto4xx_hash_alg_init(struct crypto_tfm *tfm,
				   unsigned int sa_len,
				   unsigned char ha,
				   unsigned char hm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	struct crypto4xx_alg *my_alg = crypto_alg_to_crypto4xx_alg(alg);
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(tfm);
	struct dynamic_sa_ctl *sa;
	int rc;

	ctx->dev   = my_alg->dev;
	ctx->is_hash = 1;
	ctx->hash_final = 0;

	/* Create SA */
	if (ctx->sa_in_dma_addr || ctx->sa_out_dma_addr)
		crypto4xx_free_sa(ctx);

	rc = crypto4xx_alloc_sa(ctx, sa_len);
	if (rc)
		return -ENOMEM;

	if (ctx->state_record_dma_addr == 0) {
		crypto4xx_alloc_state_record(ctx);
		if (!ctx->state_record_dma_addr) {
			crypto4xx_free_sa(ctx);
			return -ENOMEM;
		}
	}

	tfm->crt_ahash.reqsize = sizeof(struct crypto4xx_ctx);
	sa = (struct dynamic_sa_ctl *)(ctx->sa_in);

	/* Setup hash algorithm and hash mode */
	sa->sa_command_0.w = 0;
	sa->sa_command_0.bf.hash_alg = ha;
	sa->sa_command_0.bf.gather = 0;
	sa->sa_command_0.bf.save_hash_state = 1;
	sa->sa_command_0.bf.cipher_alg = SA_CIPHER_ALG_NULL;
	sa->sa_command_0.bf.opcode = SA_OPCODE_HASH;

	/* load hash state set to no load, since we don't no init idigest  */
	sa->sa_command_0.bf.load_hash_state = 3;
	sa->sa_command_0.bf.dir = 0;
	sa->sa_command_0.bf.opcode = SA_OPCODE_HASH;
	sa->sa_command_1.w = 0;
	sa->sa_command_1.bf.hmac_muting = 0;
	/* dynamic sa, need to set it to  rev 2 */
	sa->sa_command_1.bf.sa_rev = 1;
	sa->sa_command_1.bf.copy_payload = 0;
	sa->sa_command_1.bf.mutable_bit_proc = 1;

	/* Need to zero hash digest in SA */
	if (ha == SA_HASH_ALG_SHA1) {
		struct dynamic_sa_hash160 *sa_in =
				(struct dynamic_sa_hash160 *)ctx->sa_in;
		sa->sa_contents = SA_HASH160_CONTENTS;
		memset(sa_in->inner_digest, 0, 20);
		memset(sa_in->outer_digest, 0, 20);
		sa_in->state_ptr = ctx->state_record_dma_addr;
	} else {
		printk(KERN_ERR "ERROR: invalid hash"
				" algorithm used \n");
		return -EINVAL;
	}

	return 0;
}

static int crypto4xx_hash_init(struct ahash_request *req)
{
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	int ds;
	struct dynamic_sa_ctl *sa;

	ctx->use_rctx = 0;
	sa = (struct dynamic_sa_ctl *)ctx->sa_in;
	ds = crypto_ahash_digestsize(
			__crypto_ahash_cast(req->base.tfm));
	sa->sa_command_0.bf.digest_len = ds>>2;
	sa->sa_command_0.bf.load_hash_state = SA_LOAD_HASH_FROM_SA;
	ctx->is_hash = 1;
	ctx->direction = CRYPTO_INBOUND;

	return 0;
}

static int crypto4xx_hash_update(struct ahash_request *req)
{
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(req->base.tfm);

	ctx->is_hash = 1;
	ctx->hash_final = 0;
	ctx->use_rctx = 0;
	ctx->pd_ctl = 0x11;
	ctx->direction = CRYPTO_INBOUND;
	return crypto4xx_handle_req(&req->base);
}

static int crypto4xx_hash_final(struct ahash_request *req)
{
	struct crypto4xx_ctx *rctx = ahash_request_ctx(req);

	crypto4xx_free_sa_rctx(rctx);
	return 0;
}

static int crypto4xx_hash_digest(struct ahash_request *req)
{
	struct crypto4xx_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	ctx->use_rctx = 0;
	ctx->hash_final = 1;
	ctx->pd_ctl = 0x11;
	ctx->direction = CRYPTO_INBOUND;
	return crypto4xx_handle_req(&req->base);
}

/**
 * SHA1 and SHA2 Algorithm
 */
static int crypto4xx_sha1_alg_init(struct crypto_tfm *tfm)
{
	return crypto4xx_hash_alg_init(tfm,
				       SA_HASH160_LEN,
				       SA_HASH_ALG_SHA1,
				       SA_HASH_MODE_HASH);
}

/**
 * Support Crypto Algorithms
 */
struct crypto_alg crypto4xx_basic_alg[] = {

	/* Crypto AES modes */
	{.cra_name 		= "cbc(aes)",
	 .cra_driver_name 	= "cbc-aes-ppc4xx",
	 .cra_priority 		= CRYPTO4XX_CRYPTO_PRIORITY,
	 .cra_flags 		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	 .cra_blocksize 	= 16,	/* 128-bits block */
	 .cra_ctxsize 		= sizeof(struct crypto4xx_ctx),
	 .cra_alignmask 	= 0,
	 .cra_type 		= &crypto_ablkcipher_type,
	 .cra_module 		= THIS_MODULE,
	 .cra_u 		= {.ablkcipher = {
	 .min_keysize 		= 16,	/* AES min key size is 128-bits */
	 .max_keysize 		= 32,	/* AES max key size is 256-bits */
	 .ivsize 		= 16,	/* IV size is 16 bytes */
	 .setkey 		= crypto4xx_setkey_aes_cbc,
	 .encrypt 		= crypto4xx_encrypt,
	 .decrypt 		= crypto4xx_decrypt,
	 } }
	},
	/* Hash SHA1, SHA2 */
	{.cra_name 		= "sha1",
	 .cra_driver_name 	= "sha1-ppc4xx",
	 .cra_priority 		= CRYPTO4XX_CRYPTO_PRIORITY,
	 .cra_flags 		= CRYPTO_ALG_TYPE_AHASH | CRYPTO_ALG_ASYNC,
	 .cra_blocksize 	= 64,	/* SHA1 block size is 512-bits */
	 .cra_ctxsize 		= sizeof(struct crypto4xx_ctx),
	 .cra_alignmask 	= 0,
	 .cra_type   		= &crypto_ahash_type,
	 .cra_init 		= crypto4xx_sha1_alg_init,
	 .cra_module 		= THIS_MODULE,
	 .cra_u  		= {.ahash = {
	 .digestsize 		= 20,	/* Disgest is 160-bits */
	 .init   		= crypto4xx_hash_init,
	 .update 		= crypto4xx_hash_update,
	 .final  		= crypto4xx_hash_final,
	 .digest 		= crypto4xx_hash_digest,
	 } }
	},
};

int crypto4xx_register_basic_alg(struct crypto4xx_device *dev)
{
	return crypto4xx_register_alg(dev,
				      crypto4xx_basic_alg,
				      ARRAY_SIZE(crypto4xx_basic_alg));
}

