/*
 * devtmpfs - tmpfs based device node filesystem
 *
 * Copyright (C) 2009, Kay Sievers <kay.sievers@vrfy.org>
 *
 * During bootup, before any driver core device is registered, a tmpfs
 * filesystem is created. Every device which requests a devno, will
 * create a device node in this filesystem. The node is named after the
 * the nameof the device, or the susbsytem can provide a custom name
 * for the node.
 *
 * All devices are owned by root. This is intended to simplify bootup, and
 * make it possible to delay the initial coldplug done by udev in userspace.
 *
 * It should also provide a simpler way for rescue systems to bring up a
 * kernel with dynamic major/minor numbers.
 */

#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/mount.h>
#include <linux/device.h>
#include <linux/genhd.h>
#include <linux/namei.h>
#include <linux/fs.h>
#include <linux/cred.h>

static DECLARE_MUTEX(dev_sb_sem);
static struct vfsmount *dev_mnt;
static struct file_system_type dev_fs_type;
static struct cred *kern_cred;

#if defined CONFIG_DEVTMPFS_MOUNT
static int dev_mount = 1;
#else
static int dev_mount;
#endif

static int __init mount_param(char *str)
{
	dev_mount = simple_strtoul(str, NULL, 0);
	return 1;
}
__setup("devtmpfs.mount=", mount_param);

static int dev_get_sb(struct file_system_type *fs_type, int flags,
		      const char *dev_name, void *data, struct vfsmount *mnt)
{
	int ret = 0;

	down(&dev_sb_sem);
	if (!dev_mnt) {
		static struct file_system_type *tmpfs;

		tmpfs = get_fs_type("tmpfs");
		if (!tmpfs) {
			printk(KERN_ERR "dev_get_sb: unable get tmpfs "
			       "fstype\n");
			ret = -ENOENT;
			goto out;
		}
		ret = tmpfs->get_sb(fs_type, flags, dev_name, data, mnt);
		if (ret) {
			printk(KERN_ERR "dev_get_sb: unable to init "
			       "tmpfs superblock\n");
			goto out;
		}
		dev_mnt = mnt;
	} else {
		/* we use a tmpfs superblock, fill_super() is not called */
		get_sb_single(fs_type, flags, data, NULL, mnt);
	}
out:
	up(&dev_sb_sem);
	return ret;
}

static struct file_system_type dev_fs_type = {
	.name = "devtmpfs",
	.get_sb = dev_get_sb,
	.kill_sb = kill_litter_super,
};

#ifdef CONFIG_BLOCK
static inline int is_blockdev(struct device *dev)
{
	return dev->class == &block_class;
}
#else
static inline int is_blockdev(struct device *dev) { return 0; }
#endif

static int dev_mkdir(const char *name, mode_t mode)
{
	struct nameidata nd;
	struct dentry *dentry;
	int err;

	err = vfs_path_lookup(dev_mnt->mnt_root, dev_mnt,
			      name, LOOKUP_PARENT, &nd);
	if (err)
		return err;

	dentry = lookup_create(&nd, 1);
	if (!IS_ERR(dentry)) {
		err = vfs_mkdir(nd.path.dentry->d_inode, dentry, mode);
		dput(dentry);
	} else {
		err = PTR_ERR(dentry);
	}
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);

	path_put(&nd.path);
	return err;
}

static int create_path(const char *nodepath)
{
	char *path;
	struct nameidata nd;
	int err = 0;

	path = kstrdup(nodepath, GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	err = vfs_path_lookup(dev_mnt->mnt_root, dev_mnt,
			      path, LOOKUP_PARENT, &nd);
	if (err == 0) {
		struct dentry *dentry;

		/* create directory right away */
		dentry = lookup_create(&nd, 1);
		if (!IS_ERR(dentry)) {
			err = vfs_mkdir(nd.path.dentry->d_inode,
					dentry, 0775);
			dput(dentry);
		}
		mutex_unlock(&nd.path.dentry->d_inode->i_mutex);

		path_put(&nd.path);
	} else if (err == -ENOENT) {
		char *s;

		/* parent directories do not exist, create them */
		s = path;
		while (1) {
			s = strchr(s, '/');
			if (!s)
				break;
			s[0] = '\0';
			err = dev_mkdir(path, 0755);
			if (err && err != -EEXIST)
				break;
			s[0] = '/';
			s++;
		}
	}

	kfree(path);
	return err;
}

int devtmpfs_create_node(struct device *dev)
{
	const char *tmp = NULL;
	const char *nodename;
	mode_t mode;
	struct nameidata nd;
	struct dentry *dentry;
	int err;

	if (!dev_mnt)
		return 0;

	nodename = device_get_nodename(dev, &tmp);
	if (!nodename)
		return -ENOMEM;

	if (is_blockdev(dev))
		mode = S_IFBLK|0600;
	else
		mode = S_IFCHR|0600;

	err = vfs_path_lookup(dev_mnt->mnt_root, dev_mnt,
			      nodename, LOOKUP_PARENT, &nd);
	if (err == -ENOENT) {
		/* create missing parent directories */
		create_path(nodename);
		err = vfs_path_lookup(dev_mnt->mnt_root, dev_mnt,
				      nodename, LOOKUP_PARENT, &nd);
		if (err)
			goto out_name;
	}

	dentry = lookup_create(&nd, 0);
	if (!IS_ERR(dentry)) {
		const struct cred *curr_cred;

		curr_cred = override_creds(kern_cred);
		err = vfs_mknod(nd.path.dentry->d_inode,
				dentry, mode, dev->devt);
		revert_creds(curr_cred);
		/* mark as kernel created inode */
		if (!err)
			dentry->d_inode->i_private = &dev_mnt;
		dput(dentry);
	} else {
		err = PTR_ERR(dentry);
	}
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);

	path_put(&nd.path);
out_name:
	kfree(tmp);
	return err;
}

static int dev_rmdir(const char *name)
{
	struct nameidata nd;
	struct dentry *dentry;
	int err;

	err = vfs_path_lookup(dev_mnt->mnt_root, dev_mnt,
			      name, LOOKUP_PARENT, &nd);
	if (err)
		return err;

	mutex_lock_nested(&nd.path.dentry->d_inode->i_mutex, I_MUTEX_PARENT);
	dentry = lookup_one_len(nd.last.name, nd.path.dentry, nd.last.len);
	if (!IS_ERR(dentry)) {
		if (dentry->d_inode)
			err = vfs_rmdir(nd.path.dentry->d_inode, dentry);
		else
			err = -ENOENT;
		dput(dentry);
	} else {
		err = PTR_ERR(dentry);
	}
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);

	path_put(&nd.path);
	return err;
}

static int delete_path(const char *nodepath)
{
	const char *path;
	int err = 0;

	path = kstrdup(nodepath, GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	while (1) {
		char *base;

		base = strrchr(path, '/');
		if (!base)
			break;
		base[0] = '\0';
		err = dev_rmdir(path);
		if (err)
			break;
	}

	kfree(path);
	return err;
}

static int dev_mynode(struct device *dev, struct inode *inode, struct kstat *stat)
{
	/* did we create it */
	if (inode->i_private != &dev_mnt)
		return 0;

	/* does the dev_t match */
	if (is_blockdev(dev)) {
		if (!S_ISBLK(stat->mode))
			return 0;
	} else {
		if (!S_ISCHR(stat->mode))
			return 0;
	}
	if (stat->rdev != dev->devt)
		return 0;

	/* ours */
	return 1;
}

int devtmpfs_delete_node(struct device *dev)
{
	const char *tmp = NULL;
	const char *nodename;
	struct nameidata nd;
	struct dentry *dentry;
	struct kstat stat;
	int deleted = 1;
	int err;

	if (!dev_mnt)
		return 0;

	nodename = device_get_nodename(dev, &tmp);
	if (!nodename)
		return -ENOMEM;

	err = vfs_path_lookup(dev_mnt->mnt_root, dev_mnt,
			      nodename, LOOKUP_PARENT, &nd);
	if (err)
		goto out_name;

	mutex_lock_nested(&nd.path.dentry->d_inode->i_mutex, I_MUTEX_PARENT);
	dentry = lookup_one_len(nd.last.name, nd.path.dentry, nd.last.len);
	if (!IS_ERR(dentry)) {
		if (dentry->d_inode) {
			err = vfs_getattr(nd.path.mnt, dentry, &stat);
			if (!err && dev_mynode(dev, dentry->d_inode, &stat)) {
				err = vfs_unlink(nd.path.dentry->d_inode,
						 dentry);
				if (!err || err == -ENOENT)
					deleted = 1;
			}
		} else {
			err = -ENOENT;
		}
		dput(dentry);
	} else {
		err = PTR_ERR(dentry);
	}
	mutex_unlock(&nd.path.dentry->d_inode->i_mutex);

	path_put(&nd.path);
	if (deleted && strchr(nodename, '/'))
		delete_path(nodename);
out_name:
	kfree(tmp);
	return err;
}

/* After the root filesystem is mounted by the kernel at /root, or the
 * initramfs in extracted at /root, this tmpfs will be mounted at /root/dev.
 */
int devtmpfs_mount(const char *mountpoint)
{
	struct path path;
	int err;

	if (!dev_mount)
		return 0;

	if (!dev_mnt)
		return 0;

	err = kern_path(mountpoint, LOOKUP_FOLLOW, &path);
	if (err)
		return err;
	err = do_add_mount(dev_mnt, &path, 0, NULL);
	if (err)
		printk(KERN_INFO "devtmpfs: error mounting %i\n", err);
	else
		printk(KERN_INFO "devtmpfs: mounted\n");
	path_put(&path);
	return err;
}

/*
 * Create tmpfs mount, created core devices will add their device device
 * nodes here.
 */
int __init devtmpfs_init(void)
{
	int err;
	struct vfsmount *mnt;

	kern_cred = prepare_kernel_cred(NULL);
	if (!kern_cred) {
		printk(KERN_ERR "devtmpfs: unable to create credentials\n");
		return -EINVAL;
	}

	err = register_filesystem(&dev_fs_type);
	if (err) {
		printk(KERN_ERR "devtmpfs: unable to register devtmpfs "
		       "type %i\n", err);
		put_cred(kern_cred);
		return err;
	}

	mnt = kern_mount(&dev_fs_type);
	if (IS_ERR(mnt)) {
		err = PTR_ERR(mnt);
		printk(KERN_ERR "devtmpfs: unable to create devtmpfs %i\n", err);
		unregister_filesystem(&dev_fs_type);
		put_cred(kern_cred);
		return err;
	}

	dev_mkdir("shm", 01755);

	printk(KERN_INFO "devtmpfs: initialized\n");
	return 0;
}
