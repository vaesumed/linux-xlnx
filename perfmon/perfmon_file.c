/*
 * perfmon_file.c: perfmon2 file input/output functions
 *
 * This file implements the perfmon2 interface which
 * provides access to the hardware performance counters
 * of the host processor.
 *
 * The initial version of perfmon.c was written by
 * Ganesh Venkitachalam, IBM Corp.
 *
 * Then it was modified for perfmon-1.x by Stephane Eranian and
 * David Mosberger, Hewlett Packard Co.
 *
 * Version Perfmon-2.x is a complete rewrite of perfmon-1.x
 * by Stephane Eranian, Hewlett Packard Co.
 *
 * Copyright (c) 1999-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *                David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * More information about perfmon available at:
 * 	http://perfmon2.sf.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/file.h>
#include <linux/vfs.h>
#include <linux/mount.h>
#include <linux/perfmon_kern.h>
#include "perfmon_priv.h"

#define PFMFS_MAGIC 0xa0b4d889	/* perfmon filesystem magic number */

struct pfm_controls pfm_controls = {
	.task_group = PFM_GROUP_PERM_ANY,
	.arg_mem_max = PAGE_SIZE,
};

static int __init enable_debug(char *str)
{
	pfm_controls.debug = 1;
	PFM_INFO("debug output enabled\n");
	return 1;
}
__setup("perfmon_debug", enable_debug);

static int pfmfs_get_sb(struct file_system_type *fs_type,
			int flags, const char *dev_name,
			void *data, struct vfsmount *mnt)
{
	return get_sb_pseudo(fs_type, "pfm:", NULL, PFMFS_MAGIC, mnt);
}

static struct file_system_type pfm_fs_type = {
	.name     = "pfmfs",
	.get_sb   = pfmfs_get_sb,
	.kill_sb  = kill_anon_super,
};

/*
 * pfmfs should _never_ be mounted by userland - too much of security hassle,
 * no real gain from having the whole whorehouse mounted. So we don't need
 * any operations on the root directory. However, we need a non-trivial
 * d_name - pfm: will go nicely and kill the special-casing in procfs.
 */
static struct vfsmount *pfmfs_mnt;

int __init pfm_init_fs(void)
{
	int err = register_filesystem(&pfm_fs_type);
	if (!err) {
		pfmfs_mnt = kern_mount(&pfm_fs_type);
		err = PTR_ERR(pfmfs_mnt);
		if (IS_ERR(pfmfs_mnt))
			unregister_filesystem(&pfm_fs_type);
		else
			err = 0;
	}
	return err;
}

/*
 * called either on explicit close() or from exit_files().
 * Only the LAST user of the file gets to this point, i.e., it is
 * called only ONCE.
 *
 * IMPORTANT: we get called ONLY when the refcnt on the file gets to zero
 * (fput()),i.e, last task to access the file. Nobody else can access the
 * file at this point.
 *
 * When called from exit_files(), the VMA has been freed because exit_mm()
 * is executed before exit_files().
 *
 * When called from exit_files(), the current task is not yet ZOMBIE but we
 * flush the PMU state to the context.
 */
static int __pfm_close(struct pfm_context *ctx, struct file *filp)
{
	unsigned long flags;
	int state;
	int can_free = 1, can_unload = 1;
	int can_release = 0;

	spin_lock_irqsave(&ctx->lock, flags);

	state = ctx->state;

	PFM_DBG("state=%d", state);

	/*
	 * check if unload is needed
	 */
	if (state == PFM_CTX_UNLOADED)
		goto doit;

#ifdef CONFIG_SMP
	if (ctx->task != current) {
		/*
		 * switch context to zombie state
		 */
		ctx->state = PFM_CTX_ZOMBIE;

		PFM_DBG("zombie ctx for [%d]", ctx->task->pid);
		/*
		 * PMU session will be released by monitored task when
		 * it notices ZOMBIE state as part of pfm_unload_context()
		 */
		can_unload = can_free = 0;
	}
#endif
	if (can_unload)
		can_release  = !__pfm_unload_context(ctx);
doit:
	spin_unlock_irqrestore(&ctx->lock, flags);

	if (can_release)
		pfm_session_release();

	if (can_free)
		pfm_free_context(ctx);

	return 0;
}

/*
 * called either on explicit close() or from exit_files().
 * Only the LAST user of the file gets to this point, i.e., it is
 * called only ONCE.
 *
 * IMPORTANT: we get called ONLY when the refcnt on the file gets to zero
 * (fput()),i.e, last task to access the file. Nobody else can access the
 * file at this point.
 *
 * When called from exit_files(), the VMA has been freed because exit_mm()
 * is executed before exit_files().
 *
 * When called from exit_files(), the current task is not yet ZOMBIE but we
 * flush the PMU state to the context.
 */
static int pfm_close(struct inode *inode, struct file *filp)
{
	struct pfm_context *ctx;

	PFM_DBG("called filp=%p", filp);

	ctx = filp->private_data;
	if (ctx == NULL) {
		PFM_ERR("no ctx");
		return -EBADF;
	}
	return __pfm_close(ctx, filp);
}

static int pfm_no_open(struct inode *irrelevant, struct file *dontcare)
{
	PFM_DBG("pfm_file_ops");

	return -ENXIO;
}

static unsigned int pfm_no_poll(struct file *filp, poll_table *wait)
{
	return 0;
}

static ssize_t pfm_read(struct file *filp, char __user *buf, size_t size,
			loff_t *ppos)
{
	PFM_DBG("pfm_read called");
	return -EINVAL;
}

static ssize_t pfm_write(struct file *file, const char __user *ubuf,
			  size_t size, loff_t *ppos)
{
	PFM_DBG("pfm_write called");
	return -EINVAL;
}

static int pfm_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		     unsigned long arg)
{
	PFM_DBG("pfm_ioctl called");
	return -EINVAL;
}

const struct file_operations pfm_file_ops = {
	.llseek = no_llseek,
	.read = pfm_read,
	.write = pfm_write,
	.ioctl = pfm_ioctl,
	.open = pfm_no_open, /* special open to disallow open via /proc */
	.release = pfm_close,
	.poll = pfm_no_poll,
};

static int pfmfs_delete_dentry(struct dentry *dentry)
{
	return 1;
}

static struct dentry_operations pfmfs_dentry_operations = {
	.d_delete = pfmfs_delete_dentry,
};

int pfm_alloc_fd(struct file **cfile)
{
	int fd, ret = 0;
	struct file *file = NULL;
	struct inode * inode;
	char name[32];
	struct qstr this;

	fd = get_unused_fd();
	if (fd < 0)
		return -ENFILE;

	ret = -ENFILE;

	file = get_empty_filp();
	if (!file)
		goto out;

	/*
	 * allocate a new inode
	 */
	inode = new_inode(pfmfs_mnt->mnt_sb);
	if (!inode)
		goto out;

	PFM_DBG("new inode ino=%ld @%p", inode->i_ino, inode);

	inode->i_sb = pfmfs_mnt->mnt_sb;
	inode->i_mode = S_IFCHR|S_IRUGO;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();

	sprintf(name, "[%lu]", inode->i_ino);
	this.name = name;
	this.hash = inode->i_ino;
	this.len = strlen(name);

	ret = -ENOMEM;

	/*
	 * allocate a new dcache entry
	 */
	file->f_dentry = d_alloc(pfmfs_mnt->mnt_sb->s_root, &this);
	if (!file->f_dentry)
		goto out;

	file->f_dentry->d_op = &pfmfs_dentry_operations;

	d_add(file->f_dentry, inode);
	file->f_vfsmnt = mntget(pfmfs_mnt);
	file->f_mapping = inode->i_mapping;

	file->f_op = &pfm_file_ops;
	file->f_mode = FMODE_READ;
	file->f_flags = O_RDONLY;
	file->f_pos  = 0;

	*cfile = file;

	return fd;
out:
	if (file)
		put_filp(file);
	put_unused_fd(fd);
	return ret;
}
