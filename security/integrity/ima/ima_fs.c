/*
 * Copyright (C) 2005,2006,2007,2008 IBM Corporation
 *
 * Authors:
 * Kylene Hall <kjhall@us.ibm.com>
 * Reiner Sailer <sailer@us.ibm.com>
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * File: ima_fs.c
 *	implemenents security file system for reporting
 *	current measurement list and IMA statistics
 */
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/integrity.h>

#include "ima.h"

#define TMPBUFLEN 12
static ssize_t ima_show_htable_value(char __user *buf, size_t count,
				     loff_t *ppos, atomic_long_t *val)
{
	char tmpbuf[TMPBUFLEN];
	ssize_t len;

	len = scnprintf(tmpbuf, TMPBUFLEN, "%li\n", atomic_long_read(val));
	return simple_read_from_buffer(buf, count, ppos, tmpbuf, len);
}

static ssize_t ima_show_htable_violations(struct file *filp,
					  char __user *buf,
					  size_t count, loff_t *ppos)
{
	return ima_show_htable_value(buf, count, ppos, &ima_htable.violations);
}

static struct file_operations ima_htable_violations_ops = {
	.read = ima_show_htable_violations
};

static ssize_t ima_show_measurements_count(struct file *filp,
					   char __user *buf,
					   size_t count, loff_t *ppos)
{
	return ima_show_htable_value(buf, count, ppos, &ima_htable.len);

}

static struct file_operations ima_measurements_count_ops = {
	.read = ima_show_measurements_count
};

/* returns pointer to hlist_node */
static void *ima_measurements_start(struct seq_file *m, loff_t *pos)
{
	struct list_head *lpos;
	loff_t l = *pos;
	/* we need a lock since pos could point beyond last element */
	rcu_read_lock();
	list_for_each_rcu(lpos, &ima_measurements) {
		if (!l--) {
			rcu_read_unlock();
			return lpos;
		}
	}
	rcu_read_unlock();
	return NULL;
}

static void *ima_measurements_next(struct seq_file *m, void *v, loff_t *pos)
{
	/* lock protects when reading beyond last element
	 * against concurrent list-extension */
	struct list_head *lpos = (struct list_head *)v;

	rcu_read_lock();
	lpos = rcu_dereference(lpos->next);
	rcu_read_unlock();
	(*pos)++;

	return (lpos == &ima_measurements) ? NULL : lpos;
}

static void ima_measurements_stop(struct seq_file *m, void *v)
{
}

/* print format:
 *       32bit-le=pcr#
 *       char[20]=template digest
 *       32bit-le=template size
 *       32bit-le=template name size
 *       eventdata[n] = template name
 *
 */
static int ima_measurements_show(struct seq_file *m, void *v)
{
	/* the list never shrinks, so we don't need a lock here */
	struct list_head *lpos = v;
	struct ima_queue_entry *qe;
	struct ima_measure_entry *e;
	struct ima_inode_measure_entry *entry;
	const struct template_operations *template_ops;
	int templatename_len;
	int i;
	u32 pcr = CONFIG_IMA_MEASURE_PCR_IDX;
	char data[4];

	/* get entry */
	qe = list_entry(lpos, struct ima_queue_entry, later);
	e = qe->entry;
	if (e == NULL)
		return -1;

	/*
	 * 1st: PCRIndex
	 * PCR used is always the same (config option) in
	 * little-endian format
	 */
	memcpy(data, &pcr, 4);
	for (i = 0; i < 4; i++)
		seq_putc(m, data[i]);

	/* 2nd: template digest */
	for (i = 0; i < 20; i++)
		seq_putc(m, e->digest[i]);

	/* 3rd: template name size */
	templatename_len = strlen(e->template_name);
	if (templatename_len > IMA_EVENT_NAME_LEN_MAX)
		templatename_len = IMA_EVENT_NAME_LEN_MAX;

	memcpy(data, &templatename_len, 4);
	for (i = 0; i < 4; i++)
		seq_putc(m, data[i]);

	/* 4th:  template name  */
	for (i = 0; i < templatename_len; i++)
		seq_putc(m, e->template_name[i]);

	/* 5th:  template dependent */
	entry = (struct ima_inode_measure_entry *)e->template;
	if (integrity_find_template(e->template_name, &template_ops) == 0)
		template_ops->display_template(m, entry, INTEGRITY_SHOW_BINARY);
	else
		seq_printf(m, " \n");
	return 0;
}

static struct seq_operations ima_measurments_seqops = {
	.start = ima_measurements_start,
	.next = ima_measurements_next,
	.stop = ima_measurements_stop,
	.show = ima_measurements_show
};

static int ima_measurements_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ima_measurments_seqops);
}

static struct file_operations ima_measurements_ops = {
	.open = ima_measurements_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

void ima_template_show(struct seq_file *m, void *e,
		       enum integrity_show_type show)
{
	struct ima_inode_measure_entry *entry =
	    (struct ima_inode_measure_entry *)e;
	int filename_len;
	char data[4];
	int i;

	/* Display file digest */
	if (ima_template_mode)
		for (i = 0; i < 20; i++) {
			switch (show) {
			case INTEGRITY_SHOW_ASCII:
				seq_printf(m, "%02x", entry->digest[i]);
				break;
			case INTEGRITY_SHOW_BINARY:
				seq_putc(m, entry->digest[i]);
			default:
				break;
			}
		}

	switch (show) {
	case INTEGRITY_SHOW_ASCII:
		seq_printf(m, " %s\n", entry->file_name);
		break;
	case INTEGRITY_SHOW_BINARY:
		filename_len = strlen(entry->file_name);
		if (filename_len > IMA_EVENT_NAME_LEN_MAX)
			filename_len = IMA_EVENT_NAME_LEN_MAX;

		memcpy(data, &filename_len, 4);
		for (i = 0; i < 4; i++)
			seq_putc(m, data[i]);
		for (i = 0; i < filename_len; i++)
			seq_putc(m, entry->file_name[i]);
	default:
		break;
	}
}

/* print in ascii */
static int ima_ascii_measurements_show(struct seq_file *m, void *v)
{
	/* the list never shrinks, so we don't need a lock here */
	struct list_head *lpos = v;
	struct ima_queue_entry *qe;
	struct ima_measure_entry *e;
	struct ima_inode_measure_entry *entry;
	const struct template_operations *template_ops;
	int i;

	/* get entry */
	qe = list_entry(lpos, struct ima_queue_entry, later);
	e = qe->entry;
	if (e == NULL)
		return -1;

	/* 1st: PCR used (config option) */
	seq_printf(m, "%2d ", CONFIG_IMA_MEASURE_PCR_IDX);

	/* 2nd: SHA1 template hash */
	for (i = 0; i < 20; i++)
		seq_printf(m, "%02x", e->digest[i]);

	/* 3th:  template name */
	seq_printf(m, " %s ", e->template_name);

	/* 4th:  filename <= max + \'0' delimiter */
	entry = (struct ima_inode_measure_entry *)e->template;
	if (integrity_find_template(e->template_name, &template_ops) == 0)
		template_ops->display_template(m, entry, INTEGRITY_SHOW_ASCII);
	else
		seq_printf(m, " \n");

	return 0;
}

static struct seq_operations ima_ascii_measurements_seqops = {
	.start = ima_measurements_start,
	.next = ima_measurements_next,
	.stop = ima_measurements_stop,
	.show = ima_ascii_measurements_show
};

static int ima_ascii_measurements_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ima_ascii_measurements_seqops);
}

static struct file_operations ima_ascii_measurements_ops = {
	.open = ima_ascii_measurements_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static char *get_tag(char *bufStart, char *bufEnd, char delimiter, int *taglen)
{
	char *bufp = bufStart;
	char *tag;

	/* Get start of tag */
	while (bufp < bufEnd) {
		if (*bufp == ' ')	/* skip blanks */
			while ((*bufp == ' ') && (bufp++ < bufEnd)) ;
		else if (*bufp == '#') {	/* skip comment */
			while ((*bufp != '\n') && (bufp++ < bufEnd)) ;
			bufp++;
		} else if (*bufp == '\n')	/* skip newline */
			bufp++;
		else if (*bufp == '\t')	/* skip tabs */
			bufp++;
		else
			break;
	}
	if (bufp < bufEnd)
		tag = bufp;
	else
		return NULL;

	/* Get tag */
	*taglen = 0;
	while ((bufp < bufEnd) && (*taglen == 0)) {
		if ((*bufp == delimiter) || (*bufp == '\n')) {
			*taglen = bufp - tag;
			*bufp = '\0';
		}
		bufp++;
	}
	if (*taglen == 0)	/* Didn't find end delimiter */
		tag = NULL;
	return tag;
}

static ssize_t ima_write_policy(struct file *file, const char __user *buf,
				size_t buflen, loff_t *ppos)
{
	size_t rc = 0, datalen;
	int action = 0;
	char *data, *datap, *dataend;
	char *subj = NULL, *obj = NULL, *type = NULL;
	char *func = NULL, *mask = NULL, *fsmagic = NULL;
	int err = 0;
	char *tag;
	int taglen, i;

	datalen = buflen > 4095 ? 4095 : buflen;
	data = kmalloc(datalen + 1, GFP_KERNEL);
	if (!data)
		rc = -ENOMEM;

	if (copy_from_user(data, buf, datalen)) {
		kfree(data);
		return -EFAULT;
	}

	rc = datalen;
	*(data + datalen) = ' ';

	datap = data;
	dataend = data + datalen;

	if (strncmp(datap, "measure", 7) == 0) {
		datap += 8;
		action = 1;
	} else if (strncmp(datap, "dont_measure", 12) == 0)
		datap += 13;
	else			/* bad format */
		goto out;

	for (i = 0; i < 6; i++) {
		tag = get_tag(datap, dataend, ' ', &taglen);
		if (!tag)
			break;
		if (strncmp(tag, "obj=", 4) == 0)
			obj = tag + 4;
		else if (strncmp(tag, "subj=", 5) == 0)
			subj = tag + 5;
		else if (strncmp(tag, "type=", 5) == 0)
			type = tag + 5;
		else if (strncmp(tag, "func=", 5) == 0)
			func = tag + 5;
		else if (strncmp(tag, "mask=", 5) == 0)
			mask = tag + 5;
		else if (strncmp(tag, "fsmagic=", 8) == 0)
			fsmagic = tag + 8;
		else {		/* bad format */
			err = 1;
			break;
		}
		datap += taglen + 1;
	}

	if (!err) {
		ima_info("%s %s %s %s %s %s %s\n",
			 action ? "measure " : "dont_measure",
			 subj, obj, type, func, mask, fsmagic);
		ima_add_rule(action, subj, obj, type, func, mask, fsmagic);
	}
out:
	if (!data)
		kfree(data);
	return rc;
}

static struct dentry *ima_dir;
static struct dentry *binary_runtime_measurements;
static struct dentry *ascii_runtime_measurements;
static struct dentry *runtime_measurements_count;
static struct dentry *violations;
static struct dentry *ima_policy;

static int ima_release_policy(struct inode *inode, struct file *file)
{
	ima_update_policy();
	securityfs_remove(ima_policy);
	ima_policy = NULL;
	return 0;
}

static struct file_operations ima_measure_policy_ops = {
	.write = ima_write_policy,
	.release = ima_release_policy
};

int ima_fs_init(void)
{
	ima_dir = securityfs_create_dir("ima", NULL);
	if (!ima_dir || IS_ERR(ima_dir))
		return -1;

	binary_runtime_measurements =
	    securityfs_create_file("binary_runtime_measurements",
				   S_IRUSR | S_IRGRP, ima_dir, NULL,
				   &ima_measurements_ops);
	if (!binary_runtime_measurements || IS_ERR(binary_runtime_measurements))
		goto out;

	ascii_runtime_measurements =
	    securityfs_create_file("ascii_runtime_measurements",
				   S_IRUSR | S_IRGRP, ima_dir, NULL,
				   &ima_ascii_measurements_ops);
	if (!ascii_runtime_measurements || IS_ERR(ascii_runtime_measurements))
		goto out;

	runtime_measurements_count =
	    securityfs_create_file("runtime_measurements_count",
				   S_IRUSR | S_IRGRP, ima_dir, NULL,
				   &ima_measurements_count_ops);
	if (!runtime_measurements_count || IS_ERR(runtime_measurements_count))
		goto out;

	violations =
	    securityfs_create_file("violations", S_IRUSR | S_IRGRP,
				   ima_dir, NULL, &ima_htable_violations_ops);
	if (!violations || IS_ERR(violations))
		goto out;

	ima_policy = securityfs_create_file("policy",
					    S_IRUSR | S_IRGRP | S_IWUSR,
					    ima_dir, NULL,
					    &ima_measure_policy_ops);
	ima_init_policy();
	return 0;

out:
	securityfs_remove(runtime_measurements_count);
	securityfs_remove(ascii_runtime_measurements);
	securityfs_remove(binary_runtime_measurements);
	securityfs_remove(ima_dir);
	securityfs_remove(ima_policy);
	return -1;
}

void __exit ima_fs_cleanup(void)
{
	securityfs_remove(violations);
	securityfs_remove(runtime_measurements_count);
	securityfs_remove(ascii_runtime_measurements);
	securityfs_remove(binary_runtime_measurements);
	securityfs_remove(ima_dir);
	securityfs_remove(ima_policy);
}
