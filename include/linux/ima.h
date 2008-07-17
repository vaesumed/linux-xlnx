/*
 * ima.h
 *
 * Copyright (C) 2008 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 */

#ifndef _LINUX_IMA_H
#define _LINUX_IMA_H

/* IMA LIM Data */
enum ima_type { IMA_DATA, IMA_METADATA, IMA_TEMPLATE };

struct ima_args_data {
	const char	*filename;
	struct inode 	*inode;
	struct dentry 	*dentry;
	struct nameidata 	*nd;
	struct file 	*file;
	enum lim_hooks	function;
	u32		osid;
	int 		mask;
};

struct ima_store_data {
	char 		*name;
	int 		len;
	char 		*data;
	int  		violation;
};

struct ima_data {
	enum ima_type 	type;
	union {
		struct ima_args_data 	args;
		struct ima_store_data	template;
	} data;
};

void ima_fixup_argsdata(struct ima_args_data *data,
			struct inode *inode, struct dentry *dentry,
			struct file *file, struct nameidata *nd, int mask,
			int function);
#endif
