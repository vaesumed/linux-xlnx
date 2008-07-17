/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 *
 * Authors:
 * Serge Hallyn <serue@us.ibm.com>
 * Reiner Sailer <sailer@watson.ibm.com>
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * File: ima_queue.c
 *       implements queues that store IMA measurements and
 *       maintains aggregate over the stored measurements
 *       in the pre-configured TPM PCR (if available)
 *       The measurement list is append-only. No entry is
 *       ever removed or changed during the boot-cycle.
 */
#include <linux/module.h>

#include "ima.h"

struct list_head ima_measurements;	/* list of all measurements */
struct ima_h_table ima_htable;	/* key: inode (before secure-hashing a file) */

/* mutex protects atomicity of extending measurement list
 * and extending the TPM PCR aggregate. Since tpm_extend can take
 * long (and the tpm driver uses a mutex), we can't use the spinlock.
 */
static DEFINE_MUTEX(ima_extend_list_mutex);

void ima_create_htable(void)
{
	int i;

	INIT_LIST_HEAD(&ima_measurements);
	atomic_set(&ima_htable.len, 0);
	atomic_long_set(&ima_htable.violations, 0);
	ima_htable.max_htable_size = IMA_MEASURE_HTABLE_SIZE;

	for (i = 0; i < ima_htable.max_htable_size; i++) {
		INIT_HLIST_HEAD(&ima_htable.queue[i]);
		atomic_set(&ima_htable.queue_len[i], 0);
	}
}

struct ima_queue_entry *ima_lookup_digest_entry(u8 *digest_value)
{
	struct ima_queue_entry *qe, *ret = NULL;
	unsigned int key;
	struct hlist_node *pos;

	key = IMA_HASH_KEY(digest_value);
	rcu_read_lock();
	hlist_for_each_entry_rcu(qe, pos, &ima_htable.queue[key], hnext) {
		if (memcmp(qe->entry->digest, digest_value, 20) == 0) {
			ret = qe;
			break;
		}
	}
	rcu_read_unlock();
	return ret;
}

/* Called with mutex held */
static int ima_add_digest_entry(struct ima_measure_entry *entry)
{
	struct ima_queue_entry *qe;
	unsigned int key;

	key = IMA_HASH_KEY(entry->digest);
	qe = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (qe == NULL) {
		ima_error("OUT OF MEMORY ERROR creating queue entry.\n");
		return -ENOMEM;
	}
	qe->entry = entry;

	hlist_add_head_rcu(&qe->hnext, &ima_htable.queue[key]);
	atomic_inc(&ima_htable.queue_len[key]);
	return 0;
}

int ima_add_measure_entry(struct ima_measure_entry *entry, int violation)
{
	struct ima_queue_entry *qe;
	int error = 0;

	mutex_lock(&ima_extend_list_mutex);
	if (!violation) {
		if (ima_lookup_digest_entry(entry->digest)) {
			error = -EEXIST;
			goto out;
		}
	}
	qe = kmalloc(sizeof(struct ima_queue_entry), GFP_KERNEL);
	if (qe == NULL) {
		ima_error("OUT OF MEMORY in %s.\n", __func__);
		error = -ENOMEM;
		goto out;
	}
	qe->entry = entry;

	INIT_LIST_HEAD(&qe->later);
	list_add_tail_rcu(&qe->later, &ima_measurements);

	atomic_inc(&ima_htable.len);
	if (ima_add_digest_entry(entry)) {
		error = -ENOMEM;
		goto out;
	}
	if (violation) {	/* Replace 0x00 with 0xFF */
		u8 digest[IMA_DIGEST_SIZE];

		memset(digest, 0xff, sizeof digest);
		ima_extend(digest);
	} else
		ima_extend(entry->digest);
out:
	mutex_unlock(&ima_extend_list_mutex);
	return error;
}
