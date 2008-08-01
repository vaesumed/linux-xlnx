/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 *
 * Authors:
 * Reiner Sailer      <sailer@watson.ibm.com>
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Mimi Zohar         <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * File: ima_init.c
 *             initialization and cleanup functions
 */
#include <linux/module.h>
#include <linux/scatterlist.h>
#include "ima.h"

/* name for boot aggregate entry */
static char *boot_aggregate_name = "boot_aggregate";
static const char version[] = "v7.6 02/27/2007";

int ima_used_chip;

static void ima_add_boot_aggregate(void)
{
	/* cumulative sha1 over tpm registers 0-7 */
	struct ima_measure_entry *entry;
	size_t count;
	int err;

	/* create new entry for boot aggregate */
	entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (entry == NULL) {
		ima_add_violation(NULL, boot_aggregate_name,
				  "add_measure", "ENOMEM");
		return;
	}
	count = strlen(boot_aggregate_name);
	if (count > IMA_EVENT_NAME_LEN_MAX)
		count = IMA_EVENT_NAME_LEN_MAX;
	memcpy(entry->template_name, boot_aggregate_name, count);
	entry->template_name[count] = '\0';
	if (ima_used_chip) {
		int i;
		u8 pcr_i[20];
		struct hash_desc desc;
		struct crypto_hash *tfm;
		struct scatterlist sg;

		tfm = crypto_alloc_hash("sha1", 0, CRYPTO_ALG_ASYNC);
		if (!tfm || IS_ERR(tfm)) {
			kfree(entry);
			ima_error("error initializing digest.\n");
			return;
		}
		desc.tfm = tfm;
		desc.flags = 0;
		crypto_hash_init(&desc);

		for (i = 0; i < 8; i++) {
			ima_pcrread(i, pcr_i, sizeof(pcr_i));
			/* now accumulate with current aggregate */
			sg_init_one(&sg, (u8 *) pcr_i, 20);
			crypto_hash_update(&desc, &sg, 20);
		}
		crypto_hash_final(&desc, entry->digest);
		crypto_free_hash(tfm);
	} else
		memset(entry->digest, 0xff, 20);

	/* now add measurement; if TPM bypassed, we have a ff..ff entry */
	err = ima_add_measure_entry(entry, 0);
	if (err < 0) {
		kfree(entry);
		ima_add_violation(NULL, boot_aggregate_name,
				  "add_measure", " ");
	}
}

int ima_init(void)
{
	int rc;

	ima_used_chip = 0;
	rc = tpm_pcr_read(IMA_TPM, 0, NULL);
	if (rc == 0)
		ima_used_chip = 1;

	if (!ima_used_chip)
		ima_info("No TPM chip found(rc = %d), activating TPM-bypass!\n",
			 rc);

	ima_create_htable();	/* for measurements */
	ima_add_boot_aggregate();	/* boot aggregate must be first entry */

	return ima_fs_init();
}

void __exit ima_cleanup(void)
{
	ima_fs_cleanup();
}
