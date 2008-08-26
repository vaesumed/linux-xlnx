/*
 * fs/sysfs/symlink.c - operations for initializing and mounting sysfs
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#define DEBUG 

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/module.h>

#include "sysfs.h"

/* Random magic number */
#define SYSFS_MAGIC 0x62656572

static struct vfsmount *sysfs_mount;
struct super_block * sysfs_sb = NULL;
struct kmem_cache *sysfs_dir_cachep;

static const struct super_operations sysfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
};

struct sysfs_dirent sysfs_root = {
	.s_name		= "",
	.s_count	= ATOMIC_INIT(1),
	.s_flags	= SYSFS_DIR,
	.s_mode		= S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO,
	.s_ino		= 1,
};

static int sysfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct sysfs_super_info *info = NULL;
	struct inode *inode = NULL;
	struct dentry *root = NULL;
	int error;

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = SYSFS_MAGIC;
	sb->s_op = &sysfs_ops;
	sb->s_time_gran = 1;
	if (!sysfs_sb)
		sysfs_sb = sb;

	error = -ENOMEM;
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		goto out_err;

	/* get root inode, initialize and unlock it */
	error = -ENOMEM;
	inode = sysfs_get_inode(&sysfs_root);
	if (!inode) {
		pr_debug("sysfs: could not get root inode\n");
		goto out_err;
	}

	/* instantiate and link root dentry */
	error = -ENOMEM;
	root = d_alloc_root(inode);
	if (!root) {
		pr_debug("%s: could not get root dentry!\n",__func__);
		goto out_err;
	}
	root->d_fsdata = &sysfs_root;
	sb->s_root = root;
	sb->s_fs_info = info;
	return 0;

out_err:
	dput(root);
	iput(inode);
	kfree(info);
	if (sysfs_sb == sb)
		sysfs_sb = NULL;
	return error;
}

static int sysfs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	int rc;
	mutex_lock(&sysfs_rename_mutex);
	rc = get_sb_single(fs_type, flags, data, sysfs_fill_super, mnt);
	mutex_unlock(&sysfs_rename_mutex);
	return rc;
}

struct file_system_type sysfs_fs_type = {
	.name		= "sysfs",
	.get_sb		= sysfs_get_sb,
	.kill_sb	= kill_anon_super,
};

void sysfs_grab_supers(void)
{
	/* must hold sysfs_rename_mutex */
	struct super_block *sb;
	/* Loop until I have taken s_umount on all sysfs superblocks */
restart:
	spin_lock(&sb_lock);
	list_for_each_entry(sb, &sysfs_fs_type.fs_supers, s_instances) {
		if (sysfs_info(sb)->grabbed)
			continue;
		/* Wait for unmount activity to complete. */
		if (sb->s_count < S_BIAS) {
			sb->s_count += 1;
			spin_unlock(&sb_lock);
			down_read(&sb->s_umount);
			drop_super(sb);
			goto restart;
		}
		atomic_inc(&sb->s_active);
		sysfs_info(sb)->grabbed = 1;
	}
	spin_unlock(&sb_lock);
}

void sysfs_release_supers(void)
{
	/* must hold sysfs_rename_mutex */
	struct super_block *sb;
restart:
	spin_lock(&sb_lock);
	list_for_each_entry(sb, &sysfs_fs_type.fs_supers, s_instances) {
		if (!sysfs_info(sb)->grabbed)
			continue;
		sysfs_info(sb)->grabbed = 0;
		spin_unlock(&sb_lock);
		deactivate_super(sb);
		goto restart;
	}
	spin_unlock(&sb_lock);
}

int __init sysfs_init(void)
{
	int err = -ENOMEM;

	sysfs_dir_cachep = kmem_cache_create("sysfs_dir_cache",
					      sizeof(struct sysfs_dirent),
					      0, 0, NULL);
	if (!sysfs_dir_cachep)
		goto out;

	err = sysfs_inode_init();
	if (err)
		goto out_err;

	err = register_filesystem(&sysfs_fs_type);
	if (!err) {
		sysfs_mount = kern_mount(&sysfs_fs_type);
		if (IS_ERR(sysfs_mount)) {
			printk(KERN_ERR "sysfs: could not mount!\n");
			err = PTR_ERR(sysfs_mount);
			sysfs_mount = NULL;
			unregister_filesystem(&sysfs_fs_type);
			goto out_err;
		}
	} else
		goto out_err;
out:
	return err;
out_err:
	kmem_cache_destroy(sysfs_dir_cachep);
	sysfs_dir_cachep = NULL;
	goto out;
}

#undef sysfs_get
struct sysfs_dirent *sysfs_get(struct sysfs_dirent *sd)
{
	return __sysfs_get(sd);
}
EXPORT_SYMBOL_GPL(sysfs_get);

#undef sysfs_put
void sysfs_put(struct sysfs_dirent *sd)
{
	__sysfs_put(sd);
}
EXPORT_SYMBOL_GPL(sysfs_put);
