/*
 *  linux/fs/proc/net.c
 *
 *  Copyright (C) 2007
 *
 *  Author: Eric Biederman <ebiederm@xmission.com>
 *
 *  proc net directory handling functions
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/mount.h>
#include <linux/nsproxy.h>
#include <linux/namei.h>
#include <net/net_namespace.h>
#include <linux/seq_file.h>

#include "internal.h"

static struct file_system_type proc_net_fs_type;

static struct net *get_proc_net(const struct inode *inode)
{
	return maybe_get_net(inode->i_sb->s_fs_info);
}

int seq_open_net(struct inode *ino, struct file *f,
		 const struct seq_operations *ops, int size)
{
	struct net *net;
	struct seq_net_private *p;

	BUG_ON(size < sizeof(*p));

	net = get_proc_net(ino);
	if (net == NULL)
		return -ENXIO;

	p = __seq_open_private(f, ops, size);
	if (p == NULL) {
		put_net(net);
		return -ENOMEM;
	}
#ifdef CONFIG_NET_NS
	p->net = net;
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(seq_open_net);

int single_open_net(struct inode *inode, struct file *file,
		int (*show)(struct seq_file *, void *))
{
	int err;
	struct net *net;

	err = -ENXIO;
	net = get_proc_net(inode);
	if (net == NULL)
		goto err_net;

	err = single_open(file, show, net);
	if (err < 0)
		goto err_open;

	return 0;

err_open:
	put_net(net);
err_net:
	return err;
}
EXPORT_SYMBOL_GPL(single_open_net);

int seq_release_net(struct inode *ino, struct file *f)
{
	struct seq_file *seq;

	seq = f->private_data;

	put_net(seq_file_net(seq));
	seq_release_private(ino, f);
	return 0;
}
EXPORT_SYMBOL_GPL(seq_release_net);

int single_release_net(struct inode *ino, struct file *f)
{
	struct seq_file *seq = f->private_data;
	put_net(seq->private);
	return single_release(ino, f);
}
EXPORT_SYMBOL_GPL(single_release_net);

static struct net *get_proc_task_net(struct inode *dir)
{
	struct task_struct *task;
	struct nsproxy *ns;
	struct net *net = NULL;

	rcu_read_lock();
	task = pid_task(proc_pid(dir), PIDTYPE_PID);
	if (task != NULL) {
		ns = task_nsproxy(task);
		if (ns != NULL)
			net = get_net(ns->net_ns);
	}
	rcu_read_unlock();

	return net;
}

static void *proc_net_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	/* Follow to a mount point of the proper network namespace. */
	struct vfsmount *mnt;
	struct net *net;
	int err = -ENOENT;

	net = get_proc_task_net(dentry->d_inode);
	if (!net)
		goto out_err;

	mnt = kern_mount_data(&proc_net_fs_type, net);
	if (IS_ERR(mnt))
		goto out_err;

	dput(nd->path.dentry);
	nd->path.dentry = dget(dentry);

	err = do_add_mount(mntget(mnt), &nd->path, MNT_SHRINKABLE,
			   &proc_automounts);
	if (err < 0) {
		mntput(mnt);
		if (err == -EBUSY)
			goto out_follow;
		goto out_err;
	}
	err = 0;
	path_put(&nd->path);
	nd->path.mnt = mnt;
	nd->path.dentry = dget(mnt->mnt_root);
	put_net(net);
out:
	return ERR_PTR(err);
out_err:
	path_put(&nd->path);
	goto out;
out_follow:
	/* We raced with ourselves so just walk the mounts */
	while (d_mountpoint(nd->path.dentry) &&
		follow_down(&nd->path.mnt, &nd->path.dentry))
		;
	err = 0;
	goto out;
}

const struct inode_operations proc_net_inode_operations = {
	.follow_link	= proc_net_follow_link,
};


int proc_net_revalidate(struct task_struct *task, struct dentry *dentry,
			struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	struct dentry *tdentry;
	struct vfsmount *tmnt;
	int ret = 1;

	/* Are we talking about a proc/net mount point? */
	if (!nd || inode->i_op != &proc_net_inode_operations)
		goto out;

	/*
	 * If the wrong filesystem is mounted on /proc/<pid>/net report the
	 * dentry is invalid.
	 */
	tmnt = mntget(nd->path.mnt);
	tdentry = dget(dentry);
	if (follow_down(&tmnt, &tdentry)) {
		struct nsproxy *ns;

		rcu_read_lock();
		ns = task_nsproxy(task);
		if ((ns == NULL) ||
		     (tmnt->mnt_sb->s_magic != PROC_NET_SUPER_MAGIC) ||
		     (tmnt->mnt_sb->s_fs_info != ns->net_ns))
			ret = 0;
		rcu_read_unlock();
	}
	dput(tdentry);
	mntput(tmnt);
out:
	return ret;
}

struct proc_dir_entry *proc_net_fops_create(struct net *net,
	const char *name, mode_t mode, const struct file_operations *fops)
{
	return proc_create(name, mode, net->proc_net, fops);
}
EXPORT_SYMBOL_GPL(proc_net_fops_create);

struct proc_dir_entry *proc_net_mkdir(struct net *net, const char *name,
		struct proc_dir_entry *parent)
{
	if (!parent)
		parent = net->proc_net;
	return proc_mkdir(name, parent);
}
EXPORT_SYMBOL_GPL(proc_net_mkdir);

void proc_net_remove(struct net *net, const char *name)
{
	remove_proc_entry(name, net->proc_net);
}
EXPORT_SYMBOL_GPL(proc_net_remove);

static int proc_net_fill_super(struct super_block *sb)
{
	struct net *net = sb->s_fs_info;
	struct proc_dir_entry *netd = net->proc_net;
	struct inode *root_inode = NULL;

	sb->s_flags |= MS_NODIRATIME | MS_NOSUID | MS_NOEXEC;
	sb->s_blocksize = PAGE_SIZE;
	sb->s_blocksize_bits = PAGE_SHIFT;
	sb->s_magic = PROC_NET_SUPER_MAGIC;
	sb->s_op = &proc_sops;
	sb->s_time_gran = 1;

	de_get(netd);
	root_inode = proc_get_inode(sb, netd->low_ino, netd);
	if (!root_inode)
		goto out_no_root;
	root_inode->i_uid = 0;
	root_inode->i_gid = 0;
	sb->s_root = d_alloc_root(root_inode);
	if (!sb->s_root)
		goto out_no_root;
	return 0;

out_no_root:
	printk("%s: get root inode failed\n", __func__);
	iput(root_inode);
	de_put(netd);
	return -ENOMEM;
}

static int proc_net_test_super(struct super_block *sb, void *data)
{
	return sb->s_fs_info == data;
}

static int proc_net_set_super(struct super_block *sb, void *data)
{
	sb->s_fs_info = data;
	return set_anon_super(sb, NULL);
}

static int proc_net_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	struct super_block *sb;

	if (!(flags & MS_KERNMOUNT))
		data = current->nsproxy->net_ns;

	sb = sget(fs_type, proc_net_test_super, proc_net_set_super, data);
	if (IS_ERR(sb))
		return PTR_ERR(sb);

	if (!sb->s_root) {
		int err;
		sb->s_flags = flags;
		err = proc_net_fill_super(sb);
		if (err) {
			up_write(&sb->s_umount);
			deactivate_super(sb);
			return err;
		}

		sb->s_flags |= MS_ACTIVE;
	}

	simple_set_mnt(mnt, sb);
	return 0;
}

static struct file_system_type proc_net_fs_type = {
	.name		= "proc/net",
	.get_sb		= proc_net_get_sb,
	.kill_sb	= kill_litter_super,
};

static __net_init int proc_net_ns_init(struct net *net)
{
	struct proc_dir_entry *netd, *net_statd;
	struct vfsmount *mnt;
	int err;

	err = -ENOMEM;
	netd = proc_create_root();
	if (!netd)
		goto out;

	err = -EEXIST;
	net_statd = proc_net_mkdir(net, "stat", netd);
	if (!net_statd)
		goto free_net;

	net->proc_net = netd;
	net->proc_net_stat = net_statd;

	mnt = kern_mount_data(&proc_net_fs_type, net);
	if (IS_ERR(mnt))
		goto free_stat;

	net->proc_mnt = mnt;

	return 0;

free_stat:
	remove_proc_entry("stat", netd);
free_net:
	kfree(netd);
out:
	return err;
}

static __net_exit void proc_net_ns_exit(struct net *net)
{
	remove_proc_entry("stat", net->proc_net);
	release_proc_entry(net->proc_net);
	/*
	 * We won't be looking up this super block any more so set s_fs_info to
	 * NULL to ensure it doesn't conflict with network namespaces allocated
	 * in the future at the same address.
	 */
	net->proc_mnt->mnt_sb->s_fs_info = NULL;
	mntput(net->proc_mnt);
}

static struct pernet_operations __net_initdata proc_net_ns_ops = {
	.init = proc_net_ns_init,
	.exit = proc_net_ns_exit,
};

int __init proc_net_init(void)
{
	proc_symlink("net", NULL, "self/net");
	register_filesystem(&proc_net_fs_type);
	return register_pernet_subsys(&proc_net_ns_ops);
}
