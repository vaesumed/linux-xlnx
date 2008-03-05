/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
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
 * Authors: Adrian Hunter
 *          Artem Bityutskiy
 */

/*
 * This file implements the extended attributes support.
 */

#include <linux/xattr.h>
#include "ubifs.h"

int ubifs_setxattr(struct dentry *dentry, const char *name,
		   const void *value, size_t size, int flags)
{
	dbg_xattr("xattr '%s' of dent '%.*s'",
		  name, dentry->d_name.len, dentry->d_name.name);
	return -ENOTSUPP;
}

ssize_t ubifs_getxattr(struct dentry *dentry, const char *name, void *buf,
		       size_t size)
{
	dbg_xattr("xattr '%s' of dent '%.*s'",
		  name, dentry->d_name.len, dentry->d_name.name);
	return -ENOTSUPP;
}

ssize_t ubifs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	dbg_xattr("dent '%.*s'", dentry->d_name.len, dentry->d_name.name);
	return -ENOTSUPP;
}

int ubifs_removexattr(struct dentry *dentry, const char *name)
{
	dbg_xattr("xattr '%s' of dent '%.*s'",
		  name, dentry->d_name.len, dentry->d_name.name);
	return -ENOTSUPP;
}
