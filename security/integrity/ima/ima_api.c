/*
 * Copyright (C) 2008 IBM Corporation
 *
 * Author: Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * File: ima_api.c
 *            - implements the LIM API
 */
#include <linux/module.h>
#include <linux/integrity.h>
#include <linux/magic.h>
#include <linux/writeback.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/audit.h>
#include <linux/ima.h>

#include "ima.h"

const struct template_operations ima_template_ops = {
	.must_measure = ima_must_measure,
	.collect_measurement = ima_collect_measurement,
	.store_measurement = ima_store_measurement,
	.display_template = ima_template_show
};

/**
 * mode_setup - for compatability with non-template IMA versions
 * @str: is pointer to a string
 */
int ima_template_mode = 1;
static int __init mode_setup(char *str)
{
	if (strncmp(str, "ima", 3) == 0)
		ima_template_mode = 0;
	if (strncmp(str, "template", 7) == 0)
		ima_template_mode = 1;
	ima_info("template_mode %s \n",
		  ima_template_mode ? "template" : "ima");
	return 1;
}

__setup("ima_mode=", mode_setup);

/**
 * ima_digest_cpy - copy the hash in the IMA template structure to a digest
 * @template_name: string containing the name of the template (i.e. "ima")
 * @template: pointer to template structure
 * @digest: pointer to the digest
 *
 * Returns 0 on success, error code otherwise
 */
static int ima_digest_cpy(char *template_name, void *template, u8 *digest)
{
	int rc, result = 0;
	struct ima_inode_measure_entry *inode_template =
	    (struct ima_inode_measure_entry *)template;

	rc = strcmp(template_name, "ima");
	if (rc == 0)
		memcpy(digest, inode_template->digest,
		       sizeof inode_template->digest);
	else
		result = -ENODATA;
	return result;
}

/**
 * ima_store_template_measure - collect and protect template measurements
 * @template_name: string containing the name of the template (i.e. "ima")
 * @template_len: length of the template data
 * @template: actual template data
 * @violation: invalidate pcr measurement indication
 * @audit_cause: string containing the audit failure cause
 *
 * Calculate the hash of a template entry, add the template entry
 * to an ordered list of measurement entries maintained inside the kernel,
 * and also update the aggregate integrity value (maintained inside the
 * configured TPM PCR) over the hashes of the current list of measurement
 * entries.
 *
 * Applications retrieve the current kernel-held measurement list through
 * the securityfs entries in /sys/kernel/security/ima. The signed aggregate
 * TPM PCR (called quote) can be retrieved using a TPM user space library
 * and is used to validate the measurement list.
 *
 * Returns 0 on success, error code otherwise
 */
static int ima_store_template_measure(char *template_name, int template_len,
				      char *template, int violation,
				      char **audit_cause)
{
	struct ima_measure_entry *entry;
	u8 digest[IMA_DIGEST_SIZE];
	struct ima_queue_entry *qe;
	int count, result = 0;

	memset(digest, 0, IMA_DIGEST_SIZE);
	if (!violation) {
		int rc = -ENODATA;

		if (!ima_template_mode)
			rc = ima_digest_cpy(template_name, template, digest);
		if (rc < 0)
			result = ima_calc_template_hash(template_len, template,
							digest);

		/* hash exists already? */
		qe = ima_lookup_digest_entry(digest);
		if (qe) {
			*audit_cause = "hash_exists";
			result = -EEXIST;
			goto out;
		}
	}

	/* create new entry and add to measurement list */
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		*audit_cause = "ENOMEM";
		result = -ENOMEM;
		goto out;
	}

	entry->template = kzalloc(template_len, GFP_KERNEL);
	if (!entry->template) {
		*audit_cause = "ENOMEM";
		result = -ENOMEM;
		goto out;
	}
	if (!template_name) {
		*audit_cause = "null_template_name";
		count = 1;
	} else {
		count = strlen(template_name);
		if (count > IMA_EVENT_NAME_LEN_MAX)
			count = IMA_EVENT_NAME_LEN_MAX;
		memcpy(entry->template_name, template_name, count);
	}
	entry->template_name[count] = '\0';
	entry->template_len = template_len;
	memcpy(entry->template, template, template_len);
	memcpy(entry->digest, digest, IMA_DIGEST_SIZE);

	result = ima_add_measure_entry(entry, violation);
	if (result < 0)
		kfree(entry);
out:
	return result;
}

/**
 * ima_store_inode_measure - create and store an inode template measurement
 * @name: ascii file name associated with the measurement hash
 * @hash_len: length of hash value in bytes (16 for MD5, 20 for SHA1)
 * @hash: actual hash value pre-calculated
 *
 * Returns 0 on success, error code otherwise
 */
static int ima_store_inode_measure(struct inode *inode,
				   const unsigned char *name,
				   int hash_len, char *hash, int violation)
{
	struct ima_inode_measure_entry measure_entry, *entry = &measure_entry;
	int result;
	int namelen;
	char *op = "add_measure";
	char *cause = " ";

	memset(entry, 0, sizeof *entry);
	if (!violation)
		memcpy(entry->digest, hash, hash_len > IMA_DIGEST_SIZE ?
		       IMA_DIGEST_SIZE : hash_len);
	if (name) {
		namelen = strlen(name);
		memcpy(entry->file_name, name, namelen > IMA_EVENT_NAME_LEN_MAX
		       ? IMA_EVENT_NAME_LEN_MAX : namelen);
		entry->file_name[namelen] = '\0';
	}
	result = ima_store_template_measure("ima", sizeof *entry, (char *)entry,
					    violation, &cause);
	if (result < 0)
		integrity_audit_msg(AUDIT_INTEGRITY_PCR, inode,
				    name, op, cause, result);
	return result;
}

/**
 * ima_add_violation - add violation to measurement list.
 * @inode: inode associated with the violation
 * @fname: name associated with the inode
 * @op: string pointer to audit operation (i.e. "invalid_pcr", "add_measure")
 * @cause: string pointer to reason for violation (i.e. "ToMToU")
 *
 * Violations are flagged in the measurement list with zero hash values.
 * By extending the PCR with 0xFF's instead of with zeroes, the PCR
 * value is invalidated.
 */
void ima_add_violation(struct inode *inode, const unsigned char *fname,
		       char *op, char *cause)
{
	int result;

	/* can overflow, only indicator */
	atomic_long_inc(&ima_htable.violations);

	result = ima_store_inode_measure(inode, fname, 0, NULL, 1);
	integrity_audit_msg(AUDIT_INTEGRITY_PCR, inode, fname, op,
			    cause, result);
}

/**
 * skip_measurement - measure only regular files, skip everything else.
 * @inode: inode being measured
 * @mask: contains the permission mask
 *
 * Quick sanity check to make sure that only regular files opened
 * for read-only or execute are measured.
 *
 * Return 1 to skip measure, 0 to measure
 */
static int skip_measurement(struct inode *inode, int mask)
{
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		return 1;	/* can't measure */

	if (special_file(inode->i_mode) || S_ISLNK(inode->i_mode))
		return 1;	/* don't measure */

	if (S_ISREG(inode->i_mode))
		return 0;	/* measure */
	return 1;		/* don't measure */
}

/**
 * ima_must_measure - measure decision based on policy.
 * @template_data: pointer to struct ima_data containing ima_args_data
 *
 * The policy is defined in terms of keypairs:
 * 		subj=, obj=, type=, func=, mask=, fsmagic=
 *	subj,obj, and type: are LSM specific.
 * 	func: INODE_PERMISSION | BPRM_CHECK | FILE_MMAP
 * 	mask: contains the permission mask
 *	fsmagic: hex value
 *
 * Return 0 to measure. For matching a DONT_MEASURE policy, no policy,
 * or other error, return an error code.
*/
int ima_must_measure(void *template_data)
{
	struct ima_data *idata = (struct ima_data *)template_data;
	struct ima_args_data *data = &idata->data.args;
	int rc;

	if ((data->mask & MAY_WRITE) || (data->mask & MAY_APPEND))
		return -EPERM;

	if (skip_measurement(data->inode, data->mask))
		return -EPERM;

	rc = ima_match_policy(data->inode, data->function, data->mask);
	if (rc)
		return 0;
	return -EACCES;
}

/**
 * ima_collect_measurement - collect file measurements and store in the inode
 * @template_data: pointer to struct ima_data containing ima_args_data
 *
 * Return 0 on success, error code otherwise
 */
int ima_collect_measurement(void *template_data)
{
	struct ima_iint_cache *iint;
	struct ima_data *idata = (struct ima_data *)template_data;
	struct ima_args_data *data = &idata->data.args;
	struct inode *inode = data->inode;
	struct dentry *dentry = data->dentry;
	struct nameidata *nd = data->nd;
	struct file *file = data->file;
	int result = 0;

	if (idata->type != IMA_DATA)
		return -EPERM;

	if (!inode || !dentry)
		return -EINVAL;

	iint = inode->i_integrity;
	mutex_lock(&iint->mutex);
	if (!iint->measured) {
		memset(iint->digest, 0, IMA_DIGEST_SIZE);
		result = ima_calc_hash(dentry, file, nd, iint->digest);
	} else
		result = -EEXIST;
	mutex_unlock(&iint->mutex);
	return result;
}

/**
 * ima_store_measurement - store file and template measurements
 * @template_data: pointer to struct ima_data containing ima_args_data,
 * used to create an IMA template, or a template.
 *
 * For file measurements, first create an IMA template and then store it.
 * For all other types of template measurements, just store it.
 */
void ima_store_measurement(void *template_data)
{
	struct ima_data *idata = (struct ima_data *)template_data;
	int result;
	char *op = "add_template_measure";
	char *cause = "";

	if (idata->type == IMA_DATA) {
		struct ima_args_data *data = &idata->data.args;
		struct ima_iint_cache *iint;

		iint = data->inode->i_integrity;
		mutex_lock(&iint->mutex);
		if (iint->measured) {
			mutex_unlock(&iint->mutex);
			return;
		}
		result = ima_store_inode_measure(data->inode, data->filename,
						 IMA_DIGEST_SIZE, iint->digest,
						 0);
		if (!result || result == -EEXIST) {
			iint->measured = 1;
			iint->version = data->inode->i_version;
		}
		mutex_unlock(&iint->mutex);
	} else if (idata->type == IMA_TEMPLATE) {
		struct ima_store_data *template = (struct ima_store_data *)
		    &idata->data.template;

		result = ima_store_template_measure(template->name,
						    template->len,
						    template->data, 0, &cause);
		if (result < 0)
			integrity_audit_msg(AUDIT_INTEGRITY_PCR, NULL,
					    template->name, op, cause, result);
	}
}
