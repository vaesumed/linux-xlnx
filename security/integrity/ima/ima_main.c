/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 *
 * Authors:
 * Reiner Sailer <sailer@watson.ibm.com>
 * Serge Hallyn <serue@us.ibm.com>
 * Kylene Hall <kylene@us.ibm.com>
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * File: ima_main.c
 *             implements the IMA LIM hooks
 */
#include <linux/module.h>
#include <linux/integrity.h>
#include <linux/magic.h>
#include <linux/writeback.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/audit.h>
#include <linux/ima.h>
#include <linux/mman.h>

#include "ima.h"

static bool ima_initialized = false;
char *ima_hash = "sha1";
static int __init hash_setup(char *str)
{
	char *op = "setup";
	char *hash = "sha1";

	if (strncmp(str, "md5", 3) == 0) {
		op = "setup";
		hash = "md5";
		ima_hash = str;
	} else if (strncmp(str, "sha1", 4) != 0) {
		op = "hash_setup";
		hash = "invalid_hash_type";
	}
	integrity_audit_msg(AUDIT_INTEGRITY_HASH, NULL, NULL, op, hash, 0);
	return 1;
}

__setup("ima_hash=", hash_setup);

/* For use when the LSM module makes LIM API calls */
#ifdef CONFIG_IMA_BASE_HOOKS
static int ima_base_hooks = 1;
#else
static int ima_base_hooks;
#endif

/*
 * Setup the data structure used for the IMA LIM API calls.
 */
void ima_fixup_argsdata(struct ima_args_data *data,
			struct inode *inode, struct dentry *dentry,
			struct file *file, struct nameidata *nd, int mask,
			int function)
{
	data->inode = inode;
	data->dentry = dentry;
	data->file = file;
	data->nd = nd;
	data->mask = mask;
	data->function = function;

	if (file && file->f_dentry) {
		if (!dentry)
			data->dentry = dentry = file->f_dentry;
	}
	if (nd && nd->path.dentry) {
		if (!dentry)
			data->dentry = dentry = nd->path.dentry;
	}
	if (dentry && dentry->d_inode) {
		if (!inode)
			data->inode = inode = dentry->d_inode;
	}

	return;
}

/**
 * ima_file_free - called on close
 * @file: pointer to file being closed
 *
 * Flag files that changed, based on i_version.
 */
static void ima_file_free(struct file *file)
{
	struct inode *inode = NULL;
	struct ima_iint_cache *iint;

	if (!file->f_dentry)	/* can be NULL */
		return;

	inode = file->f_dentry->d_inode;
	if (S_ISDIR(inode->i_mode))
		return;
	if ((file->f_mode & FMODE_WRITE) &&
	    (atomic_read(&inode->i_writecount) == 1)) {
		iint = inode->i_integrity;
		mutex_lock(&iint->mutex);
		if (iint->version != inode->i_version)
			iint->measured = 0;
		mutex_unlock(&iint->mutex);
	}
}

/**
 * ima_alloc_integrity - allocate and attach an integrity structure
 * @inode: the inode structure
 *
 * Returns 0 on success, -ENOMEM on failure
 */
static int ima_inode_alloc_integrity(struct inode *inode)
{
	struct ima_iint_cache *iint;

	iint = kzalloc(sizeof(*iint), GFP_KERNEL);
	if (!iint)
		return -ENOMEM;

	mutex_init(&iint->mutex);
	inode->i_integrity = iint;
	iint->version = inode->i_version;
	return 0;
}

/**
 * ima_inode_free_integrity - free the integrity structure
 * @inode: the inode structure
 */
static void ima_inode_free_integrity(struct inode *inode)
{
	struct ima_iint_cache *iint = inode->i_integrity;

	if (iint) {
		inode->i_integrity = NULL;
		kfree(iint);
	}
}

/**
 * ima_inode_permission - based on policy, collect/store measurement.
 * @inode: pointer to the inode to be measured
 * @mask: contains MAY_READ, MAY_WRITE, MAY_APPEND or MAY_EXECUTE
 * @nd: pointer to a nameidata
 *
 * Measure the file associated with the inode, if the
 * file is open for read and the results of the call to
 * ima_must_measure() require the file to be measured.
 *
 * Invalidate the PCR:
 * 	- Opening a file for write when already open for read,
 *	  results in a time of measure, time of use (ToMToU) error.
 *	- Opening a file for read when already open for write,
 * 	  could result in a file measurement error.
 *
 * Return 0 on success, an error code on failure.
 * (Based on the results of appraise_measurement().)
 */
static int ima_inode_permission(struct inode *inode, int mask,
				struct nameidata *nd)
{
	struct ima_data idata;
	struct ima_args_data *data = &idata.data.args;

	if (!ima_initialized)
		return 0;

	memset(&idata, 0, sizeof idata);
	ima_fixup_argsdata(data, inode, NULL, NULL, nd, mask, INODE_PERMISSION);

	/* The file name is not required, but only a hint. */
	if (nd)
		data->filename = (!nd->path.dentry->d_name.name) ?
		    (char *)nd->path.dentry->d_iname :
		    (char *)nd->path.dentry->d_name.name;

	/* Invalidate PCR, if a measured file is already open for read */
	if ((mask == MAY_WRITE) || (mask == MAY_APPEND)) {
		int mask_sav = data->mask;
		int rc;

		data->mask = MAY_READ;
		rc = ima_must_measure(&idata);
		if (!rc) {
			if (atomic_read(&(data->dentry->d_count)) - 1 >
			    atomic_read(&(inode->i_writecount)))
				ima_add_violation(inode, data->filename,
						  "invalid_pcr", "ToMToU");
		}
		data->mask = mask_sav;
		goto out;
	}

	/* measure executables later */
	if (mask & MAY_READ) {
		int rc;

		rc = ima_must_measure(&idata);
		if (!rc) {
			/* Invalidate PCR, if a measured file is
			 * already open for write.
			 */
			if (atomic_read(&(inode->i_writecount)) > 0)
				ima_add_violation(inode, data->filename,
						  "invalid_pcr",
						  "open_writers");

			idata.type = IMA_DATA;
			rc = ima_collect_measurement(&idata);
			if (!rc)
				ima_store_measurement(&idata);
		}
	}
out:
	return 0;
}

/**
 * ima_file_mmap - based on policy, collect/store measurement.
 * @inode: pointer to the inode to be measured
 * @mask: contains MAY_READ, MAY_WRITE, MAY_APPEND or MAY_EXECUTE
 * @nd: pointer to a nameidata
 *
 * Measure files being mmapped executable based on the ima_must_measure()
 * policy decision.
 *
 * Return 0 on success, an error code on failure.
 * (Based on the results of appraise_measurement().)
 */
static int ima_file_mmap(struct file *file, unsigned long reqprot,
			 unsigned long prot, unsigned long flags,
			 unsigned long addr, unsigned long addr_only)
{
	struct ima_data idata;
	struct ima_args_data *data = &idata.data.args;
	int rc = 0;

	if (!ima_initialized)
		return 0;
	if (!file || !file->f_dentry)
		return rc;
	if (!(prot & VM_EXEC))
		return rc;

	ima_fixup_argsdata(data, NULL, NULL, file, NULL, MAY_EXEC, FILE_MMAP);
	data->filename = (file->f_dentry->d_name.name) ?
	    (char *)file->f_dentry->d_iname :
	    (char *)file->f_dentry->d_name.name;

	rc = ima_must_measure(&idata);
	if (!rc) {
		idata.type = IMA_DATA;
		rc = ima_collect_measurement(&idata);
		if (!rc)
			ima_store_measurement(&idata);
	}
	return 0;
}

/**
 * ima_bprm_check_integrity - based on policy, collect/store measurement.
 * @bprm: contains the linux_binprm structure
 *
 * The OS protects against an executable file, already open for write,
 * from being executed in deny_write_access() and an executable file,
 * already open for execute, from being modified in get_write_access().
 * So we can be certain that what we verify and measure here is actually
 * what is being executed.
 *
 * Return 0 on success, an error code on failure.
 * (Based on the results of appraise_measurement().)
 */
static int ima_bprm_check_integrity(struct linux_binprm *bprm)
{
	struct ima_data idata;
	struct ima_args_data *data = &idata.data.args;
	int rc = 0;

	if (!ima_initialized)
		return 0;
	ima_fixup_argsdata(data, NULL, NULL, bprm->file, NULL, MAY_EXEC,
			   BPRM_CHECK);
	data->filename = bprm->filename;

	rc = ima_must_measure(&idata);
	if (!rc) {
		idata.type = IMA_DATA;
		rc = ima_collect_measurement(&idata);
		if (!rc)
			ima_store_measurement(&idata);
	}
	return 0;
}

static const struct integrity_operations ima_integrity_ops = {
	.bprm_check_integrity = ima_bprm_check_integrity,
	.inode_permission = ima_inode_permission,
	.inode_alloc_integrity = ima_inode_alloc_integrity,
	.inode_free_integrity = ima_inode_free_integrity,
	.file_free_integrity = ima_file_free,
	.file_mmap = ima_file_mmap,
};

static const struct integrity_operations ima_base_ops = {
	.inode_alloc_integrity = ima_inode_alloc_integrity,
	.inode_free_integrity = ima_inode_free_integrity,
	.file_free_integrity = ima_file_free,
};

/* Register the integrity ops early so that i_integrity is
 * allocated at inode initialization.
 */
static int __init init_ops(void)
{
	int error;

	if (ima_base_hooks)
		error = register_integrity(&ima_base_ops);
	else
		error = register_integrity(&ima_integrity_ops);
	return error;
}

/* After the TPM is available, start IMA
 */
static int __init init_ima(void)
{
	int error;

	error = ima_init();
	if (error)
		goto out;
	ima_initialized = true;
	integrity_register_template("ima", &ima_template_ops);
out:
	return error;
}

static void __exit cleanup_ima(void)
{
	integrity_unregister_template("ima");
	unregister_integrity(&ima_integrity_ops);
	ima_cleanup();
}

security_initcall(init_ops);	/* Register the integrity ops early */
late_initcall(init_ima);	/* Start IMA after the TPM is available */
module_exit(cleanup_ima);

MODULE_DESCRIPTION("Integrity Measurement Architecture");
MODULE_LICENSE("GPL");
