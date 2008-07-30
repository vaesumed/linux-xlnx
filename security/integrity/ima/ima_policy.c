/*
 * Copyright (C) 2008 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * ima_policy.c
 * 	- initialize default measure policy rules
	- load a policy ruleset
 *
 */
#include <linux/module.h>
#include <linux/list.h>
#include <linux/audit.h>
#include <linux/security.h>
#include <linux/integrity.h>
#include <linux/magic.h>

#include "ima.h"

#define security_filter_rule_init security_audit_rule_init
#define security_filter_rule_match security_audit_rule_match

struct ima_measure_rule_entry {
	struct list_head list;
	int action;
	void *lsm_obj_rule;
	void *lsm_subj_rule;
	void *lsm_type_rule;
	enum lim_hooks func;
	int mask;
	ulong fsmagic;
};

/* Without LSM specific knowledge, default policy can only
 * be written in terms of .action, .func, .mask and .fsmagic.
 */
static struct ima_measure_rule_entry default_rules[] = {
	{.action = DONT_MEASURE,.fsmagic = PROC_SUPER_MAGIC},
	{.action = DONT_MEASURE,.fsmagic = SYSFS_MAGIC},
	{.action = DONT_MEASURE,.fsmagic = DEBUGFS_MAGIC},
	{.action = DONT_MEASURE,.fsmagic = TMPFS_MAGIC},
	{.action = DONT_MEASURE,.fsmagic = SECURITYFS_MAGIC},
	{.action = MEASURE,.func = FILE_MMAP,.mask = MAY_EXEC},
	{.action = MEASURE,.func = BPRM_CHECK,.mask = MAY_EXEC},
	{.action = MEASURE,.func = INODE_PERMISSION,.mask = MAY_READ},
};

static struct list_head measure_default_rules;
static struct list_head measure_policy_rules;
static struct list_head *ima_measure;

static DEFINE_MUTEX(ima_measure_mutex);

/**
 * ima_match_rules - determine whether an inode matches the measure rule.
 * @rule: a pointer to a rule
 * @inode: a pointer to an inode
 * @func: LIM hook identifier
 * @mask: requested action (MAY_READ | MAY_WRITE | MAY_APPEND | MAY_EXEC)
 *
 * Returns true on rule match, false on failure.
 */
static bool ima_match_rules(struct ima_measure_rule_entry *rule,
			   struct inode *inode, enum lim_hooks func, int mask)
{
	if (rule->func && rule->func != func)
		return false;
	if (rule->mask && rule->mask != mask)
		return false;
	if (rule->fsmagic && rule->fsmagic != inode->i_sb->s_magic)
		return false;
	if (rule->lsm_subj_rule) {
		struct task_struct *tsk = current;
		u32 sid;
		int rc;

		security_task_getsecid(tsk, &sid);
		rc = security_filter_rule_match(sid, AUDIT_SUBJ_USER,
						AUDIT_EQUAL,
						rule->lsm_subj_rule, NULL);
		if (!rc)
			return false;
	}
	if (rule->lsm_obj_rule) {
		u32 osid;
		int rc;

		security_inode_getsecid(inode, &osid);
		rc = security_filter_rule_match(osid, AUDIT_OBJ_USER,
						AUDIT_EQUAL,
						rule->lsm_obj_rule, NULL);
		if (!rc)
			return false;
	}
	if (rule->lsm_type_rule) {
		u32 osid;
		int rc;

		security_inode_getsecid(inode, &osid);
		rc = security_filter_rule_match(osid, AUDIT_OBJ_TYPE,
						AUDIT_EQUAL,
						rule->lsm_type_rule, NULL);
		if (!rc)
			return false;
	}
	return true;
}

/**
 * ima_match_policy - decision based on LSM and other conditions
 * @inode: pointer to an inode
 * @func: IMA hook identifier
 * @mask: requested action (MAY_READ | MAY_WRITE | MAY_APPEND | MAY_EXEC)
 *
 * Measure decision based on func/mask/fsmagic and LSM(subj/obj/type)
 * conditions. Returns rule action on rule match, 0 on failure.
 */
int ima_match_policy(struct inode *inode, enum lim_hooks func, int mask)
{
	struct ima_measure_rule_entry *entry;

	rcu_read_lock();
	list_for_each_entry_rcu(entry, ima_measure, list) {
		bool rc;

		rc = ima_match_rules(entry, inode, func, mask);
		if (rc) {
			rcu_read_unlock();
			return entry->action;
		}
	}
	rcu_read_unlock();
	return 0;
}

/**
 * ima_init_policy - initialize the default and policy measure rules.
 */
void ima_init_policy(void)
{
	int i;

	INIT_LIST_HEAD(&measure_default_rules);
	for (i = 0; i < ARRAY_SIZE(default_rules); i++)
		list_add_tail(&default_rules[i].list, &measure_default_rules);
	ima_measure = &measure_default_rules;

	INIT_LIST_HEAD(&measure_policy_rules);
}

/**
 * ima_update_policy - update default_rules with new measure rules
 *
 * Wait to update the default rules with a complete new set of measure rules.
 */
void ima_update_policy(void)
{
	char *op = "policy_update";
	char *cause = "already exists";
	int result = 1;

	if (ima_measure == &measure_default_rules) {
		ima_measure = &measure_policy_rules;
		cause = "complete";
		result = 0;
	}
	integrity_audit_msg(AUDIT_INTEGRITY_STATUS, NULL,
			    NULL, op, cause, result);
}

/**
 * ima_add_rule - add ima measure rules
 * @action: integer 1 indicating MEASURE, 0 indicating DONT_MEASURE
 * @subj: pointer to an LSM subject value
 * @obj:  pointer to an LSM object value
 * @type:  pointer to an LSM object type value
 * @func: LIM hook identifier
 * @mask: requested action (MAY_READ | MAY_WRITE | MAY_APPEND | MAY_EXEC)
 * @fsmagic: fs magic hex value string
 *
 * Returns 0 on success, an error code on failure.
 */
int ima_add_rule(int action, char *subj, char *obj, char *type,
		 char *func, char *mask, char *fsmagic)
{
	struct ima_measure_rule_entry *entry;
	int result = 0;

	/* Prevent installed policy from changing */
	if (ima_measure != &measure_default_rules) {
		integrity_audit_msg(AUDIT_INTEGRITY_STATUS, NULL,
				    NULL, "policy_update", "already exists", 1);
		return -EACCES;
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	INIT_LIST_HEAD(&entry->list);
	if (action < 0 || action > 1)
		result = -EINVAL;
	else
		entry->action = action;
	if (!result && subj)
		result = security_filter_rule_init(AUDIT_SUBJ_USER, AUDIT_EQUAL,
						   subj, &entry->lsm_subj_rule);
	if (!result && obj)
		result = security_filter_rule_init(AUDIT_OBJ_USER, AUDIT_EQUAL,
						   obj, &entry->lsm_obj_rule);
	if (!result && type)
		result = security_filter_rule_init(AUDIT_OBJ_TYPE, AUDIT_EQUAL,
						   obj, &entry->lsm_obj_rule);
	if (!result && func) {
		if (strcmp(func, "INODE_PERMISSION") == 0)
			entry->func = INODE_PERMISSION;
		else if (strcmp(func, "FILE_MMAP") == 0)
			entry->func = FILE_MMAP;
		else if (strcmp(func, "BPRM_CHECK") == 0)
			entry->func = BPRM_CHECK;
		else
			result = -EINVAL;
	}
	if (!result && mask) {
		if (strcmp(mask, "MAY_EXEC") == 0)
			entry->mask = MAY_EXEC;
		else if (strcmp(mask, "MAY_WRITE") == 0)
			entry->mask = MAY_WRITE;
		else if (strcmp(mask, "MAY_READ") == 0)
			entry->mask = MAY_READ;
		else if (strcmp(mask, "MAY_APPEND") == 0)
			entry->mask = MAY_APPEND;
		else
			result = -EINVAL;
	}
	if (!result && fsmagic) {
		int rc;

		rc = strict_strtoul(fsmagic, 16, &entry->fsmagic);
		if (rc)
			result = -EINVAL;
	}
	if (!result) {
		mutex_lock(&ima_measure_mutex);
		list_add_tail(&entry->list, &measure_policy_rules);
		mutex_unlock(&ima_measure_mutex);
	}
	return result;
}
