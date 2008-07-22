/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 *
 * Authors:
 * Reiner Sailer <sailer@watson.ibm.com>
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * File: ima.h
 *	internal ima definitions
 */

#ifndef __LINUX_IMA_H
#define __LINUX_IMA_H

#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/security.h>
#include <linux/integrity.h>
#include <linux/hash.h>
#include <linux/tpm.h>

#define ima_printk(level, format, arg...)		\
	printk(level "ima (%s): " format, __func__, ## arg)

#define ima_error(format, arg...)	\
	ima_printk(KERN_ERR, format, ## arg)

#define ima_info(format, arg...)	\
	ima_printk(KERN_INFO, format, ## arg)

/* digest size for IMA, fits SHA1 or MD5 */
#define IMA_DIGEST_SIZE		20
#define IMA_EVENT_NAME_LEN_MAX	255

#define IMA_HASH_BITS 9
#define IMA_MEASURE_HTABLE_SIZE (1 << IMA_HASH_BITS)

/* set during initialization */
extern int ima_used_chip;
extern char *ima_hash;

struct ima_measure_entry {
	u8 digest[IMA_DIGEST_SIZE];	/* sha1 or md5 measurement hash */
	char template_name[IMA_EVENT_NAME_LEN_MAX + 1];	/* name + \0 */
	int template_len;
	char *template;
};

struct ima_queue_entry {
	struct hlist_node hnext;	/* place in hash collision list */
	struct list_head later;		/* place in ima_measurements list */
	struct ima_measure_entry *entry;
};
extern struct list_head ima_measurements;	/* list of all measurements */

/* declarations */
extern int ima_template_mode;
extern const struct template_operations ima_template_ops;

/* Internal IMA function definitions */
int ima_init(void);
void ima_cleanup(void);
int ima_fs_init(void);
void ima_fs_cleanup(void);
void ima_create_htable(void);
int ima_add_measure_entry(struct ima_measure_entry *entry, int violation);
struct ima_queue_entry *ima_lookup_digest_entry(u8 *digest);
int ima_calc_hash(struct dentry *dentry, struct file *file,
			struct nameidata *, char *digest);
int ima_calc_template_hash(int template_len, char *template, char *digest);
void ima_add_violation(struct inode *inode, const unsigned char *fname,
			char *op, char *cause);

enum ima_action {DONT_MEASURE, MEASURE};
int ima_match_policy(struct inode *inode, enum lim_hooks func, int mask);
int ima_add_rule(int, char *, char *, char *, char *, char *, char *);
void ima_init_policy(void);
void ima_update_policy(void);


/* LIM API function definitions */
int ima_must_measure(void *d);
int ima_collect_measurement(void *d);
int ima_appraise_measurement(void *d);
void ima_store_measurement(void *d);
void ima_template_show(struct seq_file *m, void *e,
			     enum integrity_show_type show);


/*
 * used to protect h_table and sha_table
 */
extern spinlock_t ima_queue_lock;

struct ima_h_table {
	atomic_long_t len;	/* number of stored measurements in the list */
	atomic_long_t violations;
	unsigned int max_htable_size;
	struct hlist_head queue[IMA_MEASURE_HTABLE_SIZE];
	atomic_t queue_len[IMA_MEASURE_HTABLE_SIZE];
};
extern struct ima_h_table ima_htable;

static inline unsigned long IMA_HASH_KEY(u8 *digest)
{
	 return(hash_ptr(digest, IMA_HASH_BITS));
}

/* TPM "Glue" definitions */

#define IMA_TPM ((((u32)TPM_ANY_TYPE)<<16) | (u32)TPM_ANY_NUM)
static inline void ima_extend(const u8 *hash)
{
	if (!ima_used_chip)
		return;

	if (tpm_pcr_extend(IMA_TPM, CONFIG_IMA_MEASURE_PCR_IDX, hash) != 0)
		ima_error("Error Communicating to TPM chip\n");
}

static inline void ima_pcrread(int idx, u8 *pcr, int pcr_size)
{
	if (!ima_used_chip)
		return;

	if (tpm_pcr_read(IMA_TPM, idx, pcr) != 0)
		ima_error("Error Communicating to TPM chip\n");
}

struct ima_inode_measure_entry {
	u8 digest[IMA_DIGEST_SIZE];	/* sha1/md5 measurement hash */
	char file_name[IMA_EVENT_NAME_LEN_MAX + 1];	/* name + \0 */
};

/* inode integrity data */
struct ima_iint_cache {
	u64 		version;
	int 		measured;
	u8 		hmac[IMA_DIGEST_SIZE];
	u8 		digest[IMA_DIGEST_SIZE];
	struct mutex mutex;
};
#endif
