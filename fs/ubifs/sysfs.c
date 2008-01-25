/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006, 2007 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author: Artem Bityutskiy
 *         Adrian Hunter
 */

/*
 * This file implements UBIFS sysfs tree support. This tree is placed under
 * 'fs/ubifs' directory in sysfs.
 */

#include "ubifs-priv.h"

static ssize_t fs_attr_show(struct kobject *kobj, struct attribute *attr,
			       char *buf);
static ssize_t fs_attr_store(struct kobject *kobj, struct attribute *attr,
				const char *buf, size_t len);
static void fs_release(struct kobject *kobj);

static struct attribute clean_znodes_attr =
	{.name = "clean_znodes", .mode = S_IRUGO};
static struct attribute dirty_znodes_attr =
	{.name = "dirty_znodes", .mode = S_IRUGO};
static struct attribute dirty_pages_attr =
	{.name = "dirty_pages", .mode = S_IRUGO};
static struct attribute dirty_inodes_attr =
	{.name = "dirty_inodes", .mode = S_IRUGO};
/* TODO: useful for bughunting, remove later */
static struct attribute bug_hunting_attr =
	{.name = "bug_hunting", .mode = S_IRUGO | S_IWUGO};
int bug_hunting;

static struct attribute *fs_attrs[] =
{
	&clean_znodes_attr,
	&dirty_znodes_attr,
	&dirty_pages_attr,
	&dirty_inodes_attr,
	&bug_hunting_attr,
	NULL,
};

static struct attribute_group fs_attr_group =
{
	.attrs = fs_attrs,
};

static struct sysfs_ops fs_attr_ops =
{
	.show  = fs_attr_show,
	.store = fs_attr_store,
};

static struct kobj_type fs_ktype =
{
	.release   = fs_release,
	.sysfs_ops = &fs_attr_ops,
};

struct kset ubifs_kset =
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	.kobj   = {.name = "ubifs"},
#else
	.kobj   = {.k_name = "ubifs"},
#endif
	.ktype  = &fs_ktype,
};

/**
 * ubifs_sysfs_init - initialize UBIFS sysfs support.
 * @c: UBIFS file-system description object
 *
 * This function adds file-system sysfs files under '<ubifsX_Y>/' (X:Y are IDs
 * of UBI device/volume this file-system is mounted to) directory. Returns zero
 * in case of success and a negative error code in case of failure.
 */
int ubifs_sysfs_init(struct ubifs_info *c)
{
	int err;

	c->kobj.kset = &ubifs_kset;
	c->kobj.ktype = &fs_ktype;

	err = kobject_set_name(&c->kobj, "ubifs%d_%d",
			       c->vi.ubi_num, c->vi.vol_id);
	if (err)
		goto out;

	err = kobject_register(&c->kobj);
	if (err)
		goto out;

	err = sysfs_create_group(&c->kobj, &fs_attr_group);
	if (err)
		goto out_reg;

	return 0;

out_reg:
	kobject_unregister(&c->kobj);
out:
	ubifs_err("cannot register sysfs files, error %d", err);
	c->kobj.kset = NULL;
	return err;
}

/**
 * ubifs_sysfs_close - close sysfs support for an UBIFS file-system.
 * @c: UBIFS file-system description object
 */
void ubifs_sysfs_close(struct ubifs_info *c)
{
	if (!c->kobj.kset)
		return;
	sysfs_remove_group(&c->kobj, &fs_attr_group);
	kobject_unregister(&c->kobj);
	c->kobj.kset = NULL;
}

static ssize_t fs_attr_show(struct kobject *kobj, struct attribute *attr,
			    char *buf)
{
	int n;
	struct ubifs_info *c = container_of(kobj, struct ubifs_info, kobj);

	if (attr == &clean_znodes_attr)
		n = sprintf(buf, "%ld\n", atomic_long_read(&c->clean_zn_cnt));
	else if (attr == &dirty_znodes_attr)
		n = sprintf(buf, "%ld\n", atomic_long_read(&c->dirty_zn_cnt));
	else if (attr == &dirty_pages_attr)
		n = sprintf(buf, "%ld\n", atomic_long_read(&c->dirty_pg_cnt));
	else if (attr == &dirty_inodes_attr)
		n = sprintf(buf, "%ld\n", atomic_long_read(&c->dirty_ino_cnt));
	else
		n = -EINVAL;

	return n;
}

static ssize_t fs_attr_store(struct kobject *kobj, struct attribute *attr,
			     const char *buf, size_t len)
{
	if (len > 2 || (buf[0] != '0' && buf[0] != '1'))
		return -EINVAL;

	if (attr == &bug_hunting_attr)
		bug_hunting = buf[0] - '0';
	else
		return -EINVAL;

	return len;
}

static void fs_release(struct kobject *kobj)
{
	/* TODO: Remove sysfs support altogether */
	/*struct ubifs_info *c = container_of(kobj, struct ubifs_info, kobj);

	kfree(c);*/
}
