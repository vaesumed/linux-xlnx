/*
 * integrity.h
 *
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 */

#ifndef _LINUX_INTEGRITY_H
#define _LINUX_INTEGRITY_H

#include <linux/fs.h>
#include <linux/audit.h>

#ifdef CONFIG_INTEGRITY
void integrity_audit_msg(int audit_msgno, struct inode *inode,
			const unsigned char *fname, char *op,
			char *cause, int result);

/*
 * Integrity API calls:
 *
 * @collect_measurement:
 *	Collect template specific measurement data.
 *	@data contains template specific data used for collecting the
 *	measurement.
 * 	Return 0 if operation was successful.
 *
 * @appraise_measurement:
 *	Appraise the integrity of the template specific measurement data.
 *	@data contains template specific data used for appraising the
 *	measurement.
 * 	Return 0 if operation was successful.
 *
 * @store_measurement:
 *	Store the template specific data.
 *	@data contains template specific data used for storing the
 *	measurement.
 *
 * @must_measure:
 *	Measurement decision based on an integrity policy.
 *	@data contains template specific data used for making policy
 * 	decision.
 * 	Return 0 if operation was successful.
 *
 * @display_template:
 *	Display template specific data.
 *
 */

enum integrity_show_type { INTEGRITY_SHOW_BINARY, INTEGRITY_SHOW_ASCII};

struct template_operations {
	int (*collect_measurement)(void *);
	int (*appraise_measurement)(void *);
	void (*store_measurement)(void *);
	int (*must_measure)(void *);
	void (*display_template)(struct seq_file *m, void *,
				 enum integrity_show_type);
};
extern int integrity_register_template(const char *template_name,
					const struct template_operations *ops);
extern int integrity_unregister_template(const char *template_name);
extern int integrity_find_template(const char *,
				   const struct template_operations **ops);

/*
 * Integrity hooks:
 *
 * @bprm_check_integrity:
 * 	This hook mediates the point when a search for a binary handler	will
 * 	begin.  At this point, the OS protects against an executable file,
 * 	already open for write, from being executed; and an executable file
 * 	already open for execute, from being modified. So we can be certain
 *	that any measurements(collect, appraise, store) done here are of
 * 	the file being executed.
 * 	@bprm contains the linux_binprm structure.
 *	Return 0 if the hook is successful and permission is granted.
 *
 * @inode_alloc_integrity:
 *	Allocate and attach an integrity structure to @inode->i_integrity.  The
 * 	i_integrity field is initialized to NULL when the inode structure is
 * 	allocated.
 * 	@inode contains the inode structure.
 * 	Return 0 if operation was successful.
 *
 * @inode_free_integrity:
 *	@inode contains the inode structure.
 * 	Deallocate the inode integrity structure and set @inode->i_integrity to
 * 	NULL.
 *
 * @inode_permission:
 *	This hook is called by the existing Linux permission function, when
 * 	a file is opened (as well as many other operations).  At this point,
 *	measurements of files open for read(collect, appraise, store) can
 * 	be made.
 *	@inode contains the inode structure to check.
 *	@mask contains the permission mask.
 *      @nd contains the nameidata (may be NULL).
 *
 * @file_free_integrity:
 *	Update the integrity xattr value as necessary.
 * 	*file contains the file structure being closed.
 *
 * @file_mmap :
 *	Measurement(collect, appraise, store) of files mmaped for EXEC,
 *	could be measured at this point.
 *	@file contains the file structure for file to map (may be NULL).
 *	@reqprot contains the protection requested by the application.
 *	@prot contains the protection that will be applied by the kernel.
 *	@flags contains the operational flags.
 *	Return 0 if permission is granted.
 */

enum lim_hooks {INODE_PERMISSION = 1, FILE_MMAP, BPRM_CHECK };

struct integrity_operations {
	int (*bprm_check_integrity) (struct linux_binprm *bprm);
	int (*inode_alloc_integrity) (struct inode *inode);
	void (*inode_free_integrity) (struct inode *inode);
	int (*inode_permission) (struct inode *inode, int mask,
				struct nameidata *nd);
	void (*file_free_integrity) (struct file *file);
	int (*file_mmap) (struct file *file,
			  unsigned long reqprot, unsigned long prot,
			  unsigned long flags, unsigned long addr,
			  unsigned long addr_only);
};
extern int register_integrity(const struct integrity_operations *ops);
extern int unregister_integrity(const struct integrity_operations *ops);

/* global variables */
extern const struct integrity_operations *integrity_ops;


int integrity_collect_measurement(const char *template_name, void *data);
int integrity_appraise_measurement(const char *template_name, void *data);
int integrity_must_measure(const char *template_name, void *data);
int integrity_store_measurement(const char *template_name, void *data);

int integrity_bprm_check(struct linux_binprm *bprm);
int integrity_inode_alloc(struct inode *inode);
void integrity_inode_free(struct inode *inode);
int integrity_inode_permission(struct inode *inode, int mask,
				struct nameidata *nd);
void integrity_file_free(struct file *file);
int integrity_file_mmap(struct file *file,
			  unsigned long reqprot, unsigned long prot,
			  unsigned long flags, unsigned long addr,
			  unsigned long addr_only);
#else

static inline int integrity_bprm_check(struct linux_binprm *bprm)
{
	return 0;
}

static inline int integrity_inode_alloc(struct inode *inode)
{
	return 0;
}

static inline void integrity_inode_free(struct inode *inode)
{
	return;
}

static inline int integrity_inode_permission(struct inode *inode, int mask,
				struct nameidata *nd)
{
	return 0;
}

static inline int integrity_file_permission(struct file *file, int mask)
{
	return 0;
}

static inline void integrity_file_free(struct file *file)
{
	return;
}

static inline int integrity_file_mmap(struct file *file,
			  unsigned long reqprot, unsigned long prot,
			  unsigned long flags, unsigned long addr,
			  unsigned long addr_only)
{
	return 0;
}
#endif
#endif
