/*
 *  linux/drivers/mmc/core/lock.h
 *
 *  Copyright 2006 Instituto Nokia de Tecnologia (INdT), All Rights Reserved.
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * MMC password key handling.
 */

#include <linux/key-type.h>

#include "lock.h"

#define MMC_KEYLEN_MAXBYTES 32

static int mmc_key_instantiate(struct key *key, const void *data, size_t datalen)
{
	struct mmc_key_payload *mpayload;
	int ret;

	ret = -EINVAL;
	if (datalen <= 0 || datalen > MMC_KEYLEN_MAXBYTES || !data) {
		pr_debug("Invalid data\n");
		goto error;
	}

	ret = key_payload_reserve(key, datalen);
	if (ret < 0) {
		pr_debug("ret = %d\n", ret);
		goto error;
	}

	ret = -ENOMEM;
	mpayload = kmalloc(sizeof(*mpayload) + datalen, GFP_KERNEL);
	if (!mpayload) {
		pr_debug("Unable to allocate mpayload structure\n");
		goto error;
	}
	mpayload->datalen = datalen;
	memcpy(mpayload->data, data, datalen);

	rcu_assign_pointer(key->payload.data, mpayload);

	/* ret = 0 if there is no error */
	ret = 0;

error:
	return ret;
}

static int mmc_key_match(const struct key *key, const void *description)
{
	return strcmp(key->description, description) == 0;
}

/*
 * dispose of the data dangling from the corpse of a mmc key
 */
static void mmc_key_destroy(struct key *key)
{
	struct mmc_key_payload *mpayload = key->payload.data;

	kfree(mpayload);
}

static struct key_type mmc_key_type = {
	.name		= "mmc",
	.def_datalen	= MMC_KEYLEN_MAXBYTES,
	.instantiate	= mmc_key_instantiate,
	.match		= mmc_key_match,
	.destroy	= mmc_key_destroy,
};

int mmc_register_key_type(void)
{
	return register_key_type(&mmc_key_type);
}

void mmc_unregister_key_type(void)
{
	unregister_key_type(&mmc_key_type);
}
