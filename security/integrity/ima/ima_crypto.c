/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 *
 * Authors:
 * Mimi Zohar <zohar@us.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * File: ima_crypto.c
 * 	Calculate a file's or a template's hash.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/crypto.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/scatterlist.h>
#include "ima.h"

/*
 * Calculate the file hash, using an open file descriptor if available.
 */
static int update_file_hash(struct dentry *dentry, struct file *f,
			    struct nameidata *nd, struct hash_desc *desc)
{
	struct file *file = f;
	struct scatterlist sg[1];
	loff_t i_size;
	int rc = 0;
	char *rbuf;
	int offset = 0;

	if (!file) {
		struct dentry *de = dget(dentry);
		struct vfsmount *mnt = mntget(nd->path.mnt);
		if (!de || !mnt) {
			rc = -EINVAL;
			goto err_out;
		}
		file = dentry_open(de, mnt, O_RDONLY);
		if (IS_ERR(file)) {
			ima_info("%s dentry_open failed\n", de->d_name.name);
			rc = PTR_ERR(file);
			file = NULL;
		}
err_out:
		if (!file) {
			dput(de);
			mntput(mnt);
			goto out;
		}
	}

	if (!file->f_dentry || !file->f_dentry->d_inode) {
		rc = -EINVAL;
		goto out;
	}

	rbuf = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!rbuf) {
		rc = -ENOMEM;
		goto out;
	}
	i_size = i_size_read(file->f_dentry->d_inode);
	while (offset < i_size) {
		int rbuf_len;

		rbuf_len = kernel_read(file, offset, rbuf, PAGE_SIZE);
		if (rbuf_len < 0) {
			rc = rbuf_len;
			break;
		}
		offset += rbuf_len;
		sg_set_buf(sg, rbuf, rbuf_len);

		rc = crypto_hash_update(desc, sg, rbuf_len);
		if (rc)
			break;
	}
	kfree(rbuf);
out:
	if (file && !f)
		fput(file);	/* clean up dentry_open() */
	return rc;
}

/*
 * Calculate the MD5/SHA1 digest
 */
int ima_calc_hash(struct dentry *dentry, struct file *file,
		  struct nameidata *nd, char *digest)
{
	struct hash_desc desc;
	int rc;

	if (!dentry && !file)
		return -EINVAL;

	desc.tfm = crypto_alloc_hash(ima_hash, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(desc.tfm)) {
		ima_info("failed to load %s transform: %ld\n",
			 ima_hash, PTR_ERR(desc.tfm));
		rc = PTR_ERR(desc.tfm);
		return rc;
	}
	desc.flags = 0;
	rc = crypto_hash_init(&desc);
	if (rc)
		goto out;

	rc = update_file_hash(dentry, file, nd, &desc);
	if (!rc)
		rc = crypto_hash_final(&desc, digest);
out:
	crypto_free_hash(desc.tfm);
	return rc;
}

/*
 * Calculate the hash of a given template
 */
int ima_calc_template_hash(int template_len, char *template, char *digest)
{
	struct hash_desc desc;
	struct scatterlist sg[1];
	int rc;

	desc.tfm = crypto_alloc_hash(ima_hash, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(desc.tfm)) {
		ima_info("failed to load %s transform: %ld\n",
			 ima_hash, PTR_ERR(desc.tfm));
		rc = PTR_ERR(desc.tfm);
		return rc;
	}
	desc.flags = 0;
	rc = crypto_hash_init(&desc);
	if (rc)
		goto out;

	sg_set_buf(sg, template, template_len);
	rc = crypto_hash_update(&desc, sg, template_len);
	if (!rc)
		rc = crypto_hash_final(&desc, digest);
out:
	crypto_free_hash(desc.tfm);
	return rc;
}
