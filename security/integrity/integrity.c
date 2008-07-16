/*
 * Copyright (C) 2006,2007,2008 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * File: integrity.c
 * 	register integrity subsystem
 * 	register integrity template
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/integrity.h>

const struct integrity_operations *integrity_ops;
EXPORT_SYMBOL(integrity_ops);

#define TEMPLATE_NAME_LEN_MAX 12
struct template_list_entry {
	struct list_head template;
	char template_name[TEMPLATE_NAME_LEN_MAX + 1];
	const struct template_operations *template_ops;
};
static LIST_HEAD(integrity_templates);
static DEFINE_MUTEX(integrity_templates_mutex);

/**
 * register_integrity - registers an integrity framework with the kernel
 * @ops: a pointer to the struct security_options that is to be registered
 *
 * Perhaps in the future integrity module stacking will be necessary, but
 * for the time being, this function permits only one integrity module to
 * register itself with the kernel integrity subsystem.
 *
 * If another integrity module is already registered, an error code is
 * returned. On success 0 is returned.
 */
int register_integrity(const struct integrity_operations *ops)
{
	if (integrity_ops != NULL)
		return -EAGAIN;
	integrity_ops = ops;
	return 0;
}

EXPORT_SYMBOL_GPL(register_integrity);

/**
 * unregister_integrity - unregisters an integrity framework from the kernel
 * @ops: a pointer to the struct security_options that is to be registered
 *
 * Returns 0 on success, -EINVAL on failure.
 */
int unregister_integrity(const struct integrity_operations *ops)
{
	if (ops != integrity_ops)
		return -EINVAL;

	integrity_ops = NULL;
	return 0;
}

EXPORT_SYMBOL_GPL(unregister_integrity);

/**
 * integrity_register_template - registers an integrity template with the kernel
 * @template_name: a pointer to a string containing the template name.
 * @template_ops: a pointer to the template functions
 *
 * Register a set of functions to collect, appraise, store, and display
 * a template measurement, and a means to decide whether to do them.
 * Unlike integrity modules, any number of templates may be registered.
 *
 * Returns 0 on success, an error code on failure.
 */
int integrity_register_template(const char *template_name,
				const struct template_operations *template_ops)
{
	int template_len;
	struct template_list_entry *entry;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	INIT_LIST_HEAD(&entry->template);

	template_len = strlen(template_name);
	if (template_len > TEMPLATE_NAME_LEN_MAX)
		return -EINVAL;
	strcpy(entry->template_name, template_name);
	entry->template_ops = template_ops;

	mutex_lock(&integrity_templates_mutex);
	list_add_rcu(&entry->template, &integrity_templates);
	mutex_unlock(&integrity_templates_mutex);
	synchronize_rcu();

	return 0;
}

EXPORT_SYMBOL_GPL(integrity_register_template);

/**
 * integrity_unregister_template: unregister a template
 * @template_name: a pointer to a string containing the template name.
 *
 * Returns 0 on success, -EINVAL on failure.
 */
int integrity_unregister_template(const char *template_name)
{
	struct template_list_entry *entry;

	mutex_lock(&integrity_templates_mutex);
	list_for_each_entry(entry, &integrity_templates, template) {
		if (strncmp(entry->template_name, template_name,
			    strlen(entry->template_name)) == 0) {
			list_del_rcu(&entry->template);
			mutex_unlock(&integrity_templates_mutex);
			synchronize_rcu();
			kfree(entry);
			return 0;
		}
	}
	mutex_unlock(&integrity_templates_mutex);
	return -EINVAL;
}

EXPORT_SYMBOL_GPL(integrity_unregister_template);

/**
 * integrity_find_template - search the integrity_templates list
 * @template_name: a pointer to a string containing the template name.
 * @template_ops: a pointer to the template functions
 *
 * Called with an rcu_read_lock
 * Returns 0 on success, -EINVAL on failure.
 */
int integrity_find_template(const char *template_name,
			    const struct template_operations **template_ops)
{
	struct template_list_entry *entry;

	list_for_each_entry_rcu(entry, &integrity_templates, template) {
		if (strncmp(entry->template_name, template_name,
			    strlen(entry->template_name)) == 0) {
			*template_ops = entry->template_ops;
			return 0;
		}
	}
	return -EINVAL;
}

EXPORT_SYMBOL_GPL(integrity_find_template);

/* Start of the integrity API calls */

/**
 * integrity_collect_measurement - collect template specific measurement
 * @template_name: a pointer to a string containing the template name.
 * @data: pointer to template specific data
 *
 * Returns 0 on success, an error code on failure.
 */
int integrity_collect_measurement(const char *template_name, void *data)
{
	const struct template_operations *template_ops;
	int rc;

	rcu_read_lock();
	rc = integrity_find_template(template_name, &template_ops);
	if (rc == 0)
		rc = template_ops->collect_measurement(data);
	rcu_read_unlock();
	return rc;
}

EXPORT_SYMBOL_GPL(integrity_collect_measurement);

/**
 * integrity_appraise_measurement - appraise template specific measurement
 * @template_name: a pointer to a string containing the template name.
 * @data: pointer to template specific data
 *
 * Returns 0 on success, an error code on failure
 */
int integrity_appraise_measurement(const char *template_name, void *data)
{
	const struct template_operations *template_ops;
	int rc;

	rcu_read_lock();
	rc = integrity_find_template(template_name, &template_ops);
	if (rc == 0)
		rc = template_ops->appraise_measurement(data);
	rcu_read_unlock();
	return rc;
}

EXPORT_SYMBOL_GPL(integrity_appraise_measurement);

/**
 * integrity_store_measurement - store template specific measurement
 * @template_name: a pointer to a string containing the template name.
 * @data: pointer to template specific data
 *
 * Store template specific integrity measurement.
 */
int integrity_store_measurement(const char *template_name, void *data)
{
	const struct template_operations *template_ops;
	int rc;

	rcu_read_lock();
	rc = integrity_find_template(template_name, &template_ops);
	if (rc == 0)
		template_ops->store_measurement(data);
	rcu_read_unlock();
	return rc;
}

EXPORT_SYMBOL_GPL(integrity_store_measurement);

/**
 * integrity_must_measure - measure decision based on template policy
 * @template_name: a pointer to a string containing the template name.
 * @data: pointer to template specific data
 *
 * Returns 0 on success, an error code on failure.
 */
int integrity_must_measure(const char *template_name, void *data)
{
	const struct template_operations *template_ops;
	int rc;

	rcu_read_lock();
	rc = integrity_find_template(template_name, &template_ops);
	if (rc == 0)
		rc = template_ops->must_measure(data);
	rcu_read_unlock();
	return rc;
}

EXPORT_SYMBOL_GPL(integrity_must_measure);

/* Start of the integrity Hooks */

/* Hook used to measure executable file integrity. */
int integrity_bprm_check(struct linux_binprm *bprm)
{
	int rc = 0;

	if (integrity_ops && integrity_ops->bprm_check_integrity)
		rc = integrity_ops->bprm_check_integrity(bprm);
	return rc;
}

/* Allocate, attach and initialize an inode's i_integrity. */
int integrity_inode_alloc(struct inode *inode)
{
	int rc = 0;

	if (integrity_ops && integrity_ops->inode_alloc_integrity)
		rc = integrity_ops->inode_alloc_integrity(inode);
	return rc;
}

/* Hook used to free an inode's i_integrity structure. */
void integrity_inode_free(struct inode *inode)
{
	if (integrity_ops && integrity_ops->inode_free_integrity)
		integrity_ops->inode_free_integrity(inode);
}

/* Hook used to measure a file's integrity. */
int integrity_inode_permission(struct inode *inode, int mask,
			       struct nameidata *nd)
{
	int rc = 0;

	if (integrity_ops && integrity_ops->inode_permission)
		rc = integrity_ops->inode_permission(inode, mask, nd);
	return rc;
}

/* Hook used to update i_integrity data and integrity xattr values
 * as necessary.
 */
void integrity_file_free(struct file *file)
{
	if (integrity_ops && integrity_ops->file_free_integrity)
		integrity_ops->file_free_integrity(file);
}

/* Hook used to measure integrity of an mmapped file */
int integrity_file_mmap(struct file *file, unsigned long reqprot,
			unsigned long prot, unsigned long flags,
			unsigned long addr, unsigned long addr_only)
{
	int rc = 0;

	if (integrity_ops && integrity_ops->file_mmap)
		rc = integrity_ops->file_mmap(file, reqprot, prot,
					      flags, addr, addr_only);
	return rc;
}
