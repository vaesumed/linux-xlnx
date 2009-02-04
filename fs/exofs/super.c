/*
 * Copyright (C) 2005, 2006
 * Avishay Traeger (avishay@gmail.com) (avishay@il.ibm.com)
 * Copyright (C) 2005, 2006
 * International Business Machines
 * Copyright (C) 2008, 2009
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * Copyrights for code taken from ext2:
 *     Copyright (C) 1992, 1993, 1994, 1995
 *     Remy Card (card@masi.ibp.fr)
 *     Laboratoire MASI - Institut Blaise Pascal
 *     Universite Pierre et Marie Curie (Paris VI)
 *     from
 *     linux/fs/minix/inode.c
 *     Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is part of exofs.
 *
 * exofs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.  Since it is based on ext2, and the only
 * valid version of GPL for the Linux kernel is version 2, the only valid
 * version of GPL for exofs is version 2.
 *
 * exofs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with exofs; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/string.h>
#include <linux/parser.h>
#include <linux/vfs.h>
#include <linux/random.h>

#include "exofs.h"

/******************************************************************************
 * MOUNT OPTIONS
 *****************************************************************************/

/*
 * exofs-specific mount-time options.
 */
enum { Opt_pid, Opt_to, Opt_mkfs, Opt_format, Opt_err };

/*
 * Our mount-time options.  These should ideally be 64-bit unsigned, but the
 * kernel's parsing functions do not currently support that.  32-bit should be
 * sufficient for most applications now.
 */
static match_table_t tokens = {
	{Opt_pid, "pid=%u"},
	{Opt_to, "to=%u"},
	{Opt_mkfs, "mkfs=%u"},
	{Opt_format, "format=%u"},
	{Opt_err, NULL}
};

/*
 * The main option parsing method.  Also makes sure that all of the mandatory
 * mount options were set.
 */
static int parse_options(char *options, struct exofs_mountopt *opts)
{
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;
	int s_pid = 0;

	EXOFS_DBGMSG("parse_options %s\n", options);
	/* defaults */
	memset(opts, 0, sizeof(*opts));
	opts->timeout = BLK_DEFAULT_SG_TIMEOUT;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_pid:
			if (match_int(&args[0], &option))
				return -EINVAL;
			if (option < 65536) {
				EXOFS_ERR("Partition ID must be >= 65536");
				return -EINVAL;
			}
			opts->pid = option;
			s_pid = 1;
			break;
		case Opt_to:
			if (match_int(&args[0], &option))
				return -EINVAL;
			if (option <= 0) {
				EXOFS_ERR("Timout must be > 0");
				return -EINVAL;
			}
			opts->timeout = option * HZ;
			break;
		case Opt_mkfs:
			if (match_int(&args[0], &option))
				return -EINVAL;
			opts->mkfs = option != 0;
			break;
		case Opt_format:
			if (match_int(&args[0], &option))
				return -EINVAL;
			opts->format = option;
			break;
		}
	}

	if (!s_pid) {
		EXOFS_ERR("Need to specify the following options:\n");
		EXOFS_ERR("    -o pid=pid_no_to_use\n");
		return -EINVAL;
	}

	return 0;
}

/******************************************************************************
 * INODE CACHE
 *****************************************************************************/

/*
 * Our inode cache.  Isn't it pretty?
 */
static struct kmem_cache *exofs_inode_cachep;

/*
 * Allocate an inode in the cache
 */
static struct inode *exofs_alloc_inode(struct super_block *sb)
{
	struct exofs_i_info *oi;

	oi = kmem_cache_alloc(exofs_inode_cachep, GFP_KERNEL);
	if (!oi)
		return NULL;

	oi->vfs_inode.i_version = 1;
	return &oi->vfs_inode;
}

/*
 * Remove an inode from the cache
 */
static void exofs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(exofs_inode_cachep, exofs_i(inode));
}

/*
 * Initialize the inode
 */
static void exofs_init_once(void *foo)
{
	struct exofs_i_info *oi = foo;

	inode_init_once(&oi->vfs_inode);
}

/*
 * Create and initialize the inode cache
 */
static int init_inodecache(void)
{
	exofs_inode_cachep = kmem_cache_create("exofs_inode_cache",
				sizeof(struct exofs_i_info), 0,
				SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
				exofs_init_once);
	if (exofs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

/*
 * Destroy the inode cache
 */
static void destroy_inodecache(void)
{
	kmem_cache_destroy(exofs_inode_cachep);
}

/******************************************************************************
 * SUPERBLOCK FUNCTIONS
 *****************************************************************************/

/*
 * Write the superblock to the OSD
 */
static void exofs_write_super(struct super_block *sb)
{
	struct exofs_sb_info *sbi;
	struct exofs_fscb *fscb = NULL;
	struct osd_request *req = NULL;

	fscb = kzalloc(sizeof(struct exofs_fscb), GFP_KERNEL);
	if (!fscb)
		return;

	lock_kernel();
	sbi = sb->s_fs_info;
	fscb->s_nextid = cpu_to_le64(sbi->s_nextid);
	fscb->s_numfiles = cpu_to_le32(sbi->s_numfiles);
	fscb->s_magic = cpu_to_le16(sb->s_magic);
	fscb->s_newfs = 0;

	req = prepare_osd_write(sbi->s_dev, sbi->s_pid, EXOFS_SUPER_ID,
				sizeof(struct exofs_fscb), 0, fscb);
	if (!req) {
		EXOFS_ERR("ERROR: write super failed.\n");
		goto out;
	}

	exofs_sync_op(req, sbi->s_timeout, sbi->s_cred);
	free_osd_req(req);
	sb->s_dirt = 0;

out:
	unlock_kernel();
	kfree(fscb);
}

/*
 * This function is called when the vfs is freeing the superblock.  We just
 * need to free our own part.
 */
static void exofs_put_super(struct super_block *sb)
{
	int num_pend;
	struct exofs_sb_info *sbi = sb->s_fs_info;

	/* make sure there are no pending commands */
	for (num_pend = atomic_read(&sbi->s_curr_pending); num_pend > 0;
	     num_pend = atomic_read(&sbi->s_curr_pending)) {
		wait_queue_head_t wq;
		init_waitqueue_head(&wq);
		wait_event_timeout(wq,
				  (atomic_read(&sbi->s_curr_pending) == 0),
				  msecs_to_jiffies(100));
	}

	osduld_put_device(sbi->s_dev);
	kfree(sb->s_fs_info);
	sb->s_fs_info = NULL;
}

/*
 * Read the superblock from the OSD and fill in the fields
 */
static int exofs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root;
	struct exofs_mountopt *opts = data;
	struct exofs_sb_info *sbi = NULL;    /*extended info                  */
	struct exofs_fscb fscb;		     /*on-disk superblock info        */
	struct osd_request *req = NULL;
	int ret;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	sb->s_fs_info = sbi;

	/* use mount options to fill superblock */
	sbi->s_dev = osduld_path_lookup(opts->dev_name);
	if (IS_ERR(sbi->s_dev)) {
		ret = PTR_ERR(sbi->s_dev);
		sbi->s_dev = NULL;
		goto free_sbi;
	}

	sbi->s_pid = opts->pid;
	sbi->s_timeout = opts->timeout;

	/* fill in some other data by hand */
	memset(sb->s_id, 0, sizeof(sb->s_id));
	strcpy(sb->s_id, "exofs");
	sb->s_blocksize = EXOFS_BLKSIZE;
	sb->s_blocksize_bits = EXOFS_BLKSHIFT;
	atomic_set(&sbi->s_curr_pending, 0);
	sb->s_bdev = NULL;
	sb->s_dev = 0;

	/* see if we need to make the file system on the obsd */
	if (opts->mkfs) {
		EXOFS_DBGMSG("exofs_mkfs %p\n", sbi->s_dev);
		exofs_mkfs(sbi->s_dev, sbi->s_pid, opts->format);
	}

	/* read data from on-disk superblock object */
	exofs_make_credential(sbi->s_cred, sbi->s_pid, EXOFS_SUPER_ID);

	req = prepare_osd_read(sbi->s_dev, sbi->s_pid, EXOFS_SUPER_ID,
			       sizeof(struct exofs_fscb), 0, &fscb);
	if (!req) {
		if (!silent)
			EXOFS_ERR("ERROR: could not prepare read request.\n");
		ret = -ENOMEM;
		goto free_sbi;
	}

	ret = exofs_sync_op(req, sbi->s_timeout, sbi->s_cred);
	if (ret != 0) {
		if (!silent)
			EXOFS_ERR("ERROR: read super failed.\n");
		ret = -EIO;
		goto free_sbi;
	}

	sb->s_magic = le16_to_cpu(fscb.s_magic);
	sbi->s_nextid = le64_to_cpu(fscb.s_nextid);
	sbi->s_numfiles = le32_to_cpu(fscb.s_numfiles);

	/* make sure what we read from the object store is correct */
	if (sb->s_magic != EXOFS_SUPER_MAGIC) {
		if (!silent)
			EXOFS_ERR("ERROR: Bad magic value\n");
		ret = -EINVAL;
		goto free_sbi;
	}

	/* start generation numbers from a random point */
	get_random_bytes(&sbi->s_next_generation, sizeof(u32));
	spin_lock_init(&sbi->s_next_gen_lock);

	/* set up operation vectors */
	sb->s_op = &exofs_sops;
	root = exofs_iget(sb, EXOFS_ROOT_ID - EXOFS_OBJ_OFF);
	if (IS_ERR(root)) {
		EXOFS_ERR("ERROR: exofs_iget failed\n");
		ret = PTR_ERR(root);
		goto free_sbi;
	}
	sb->s_root = d_alloc_root(root);
	if (!sb->s_root) {
		iput(root);
		EXOFS_ERR("ERROR: get root inode failed\n");
		ret = -ENOMEM;
		goto free_sbi;
	}

	if (!S_ISDIR(root->i_mode)) {
		dput(sb->s_root);
		sb->s_root = NULL;
		EXOFS_ERR("ERROR: corrupt root inode (mode = %hd)\n",
		       root->i_mode);
		ret = -EINVAL;
		goto free_sbi;
	}

	ret = 0;
out:
	if (req)
		free_osd_req(req);
	return ret;

free_sbi:
	osduld_put_device(sbi->s_dev); /* NULL safe */
	kfree(sbi);
	goto out;
}

/*
 * Set up the superblock (calls exofs_fill_super eventually)
 */
static int exofs_get_sb(struct file_system_type *type,
			  int flags, const char *dev_name,
			  void *data, struct vfsmount *mnt)
{
	struct exofs_mountopt opts;
	int ret;

	ret = parse_options(data, &opts);
	if (ret)
		return ret;

	opts.dev_name = dev_name;
	return get_sb_nodev(type, flags, &opts, exofs_fill_super, mnt);
}

/*
 * Return information about the file system state in the buffer.  This is used
 * by the 'df' command, for example.
 */
static int exofs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct exofs_sb_info *sbi = sb->s_fs_info;
	uint8_t cred_a[OSD_CAP_LEN];
	struct osd_request *req = NULL;
	uint32_t attr_page;
	uint32_t attr_id;
	uint16_t expected;
	uint64_t capacity;
	uint64_t used;
	uint8_t *data;
	int ret;

	/* get used/capacity attributes */
	exofs_make_credential(cred_a, sbi->s_pid, 0);

	req = prepare_osd_get_attr(sbi->s_dev, sbi->s_pid, 0);
	if (!req) {
		EXOFS_ERR("ERROR: prepare get_attr failed.\n");
		return -1;
	}

	prepare_get_attr_list_add_entry(req,
					OSD_APAGE_PARTITION_QUOTAS,
					OSD_ATTR_PQ_CAPACITY_QUOTA, 8);

	prepare_get_attr_list_add_entry(req,
					OSD_APAGE_PARTITION_INFORMATION,
					OSD_ATTR_PI_USED_CAPACITY, 8);

	ret = exofs_sync_op(req, sbi->s_timeout, cred_a);
	if (ret)
		goto out;

	attr_page = OSD_APAGE_PARTITION_QUOTAS;
	attr_id = OSD_ATTR_PQ_CAPACITY_QUOTA;
	expected = 8;
	ret = extract_next_attr_from_req(req, &attr_page, &attr_id, &expected,
					 &data);
	if (ret) {
		EXOFS_ERR("ERROR: extract attr from req failed\n");
		goto out;
	}
	capacity = get_unaligned_le64(data);

	attr_page = OSD_APAGE_PARTITION_INFORMATION;
	attr_id = OSD_ATTR_PI_USED_CAPACITY;
	expected = 8;
	ret = extract_next_attr_from_req(req, &attr_page, &attr_id, &expected,
					 &data);
	if (ret) {
		EXOFS_ERR("ERROR: extract attr from req failed\n");
		goto out;
	}
	used = get_unaligned_le64(data);

	/* fill in the stats buffer */
	buf->f_type = EXOFS_SUPER_MAGIC;
	buf->f_bsize = EXOFS_BLKSIZE;
	buf->f_blocks = (capacity >> EXOFS_BLKSHIFT);
	buf->f_bfree = ((capacity - used) >> EXOFS_BLKSHIFT);
	buf->f_bavail = buf->f_bfree;
	buf->f_files = sbi->s_numfiles;
	buf->f_ffree = EXOFS_MAX_ID - sbi->s_numfiles;
	buf->f_namelen = EXOFS_NAME_LEN;
out:
	free_osd_req(req);

	return ret;
}

const struct super_operations exofs_sops = {
	.alloc_inode    = exofs_alloc_inode,
	.destroy_inode  = exofs_destroy_inode,
	.write_inode    = exofs_write_inode,
	.delete_inode   = exofs_delete_inode,
	.put_super      = exofs_put_super,
	.write_super    = exofs_write_super,
	.statfs         = exofs_statfs,
};

/******************************************************************************
 * INSMOD/RMMOD
 *****************************************************************************/

/*
 * struct that describes this file system
 */
static struct file_system_type exofs_type = {
	.owner          = THIS_MODULE,
	.name           = "exofs",
	.get_sb         = exofs_get_sb,
	.kill_sb        = generic_shutdown_super,
};

static int __init init_exofs(void)
{
	int err;

	err = init_inodecache();
	if (err)
		goto out;

	err = register_filesystem(&exofs_type);
	if (err)
		goto out_d;

	return 0;
out_d:
	destroy_inodecache();
out:
	return err;
}

static void __exit exit_exofs(void)
{
	unregister_filesystem(&exofs_type);
	destroy_inodecache();
}

MODULE_AUTHOR("Avishay Traeger <avishay@gmail.com>");
MODULE_DESCRIPTION("exofs");
MODULE_LICENSE("GPL");

module_init(init_exofs)
module_exit(exit_exofs)
