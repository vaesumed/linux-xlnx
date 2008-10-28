/*
 * mkexofs.c - make an exofs file system.
 *
 * Copyright (C) 2005, 2006
 * Avishay Traeger (avishay@gmail.com) (avishay@il.ibm.com)
 * Copyright (C) 2005, 2006
 * International Business Machines
 * Copyright (C) 2008, 2009
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * Copyrights from mke2fs.c:
 *     Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
 *     2003, 2004, 2005 by Theodore Ts'o.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "exofs.h"
#include <linux/random.h>

/* #define __MKEXOFS_DEBUG_CHECKS 1 */

static int kick_it(struct osd_request *req, int timeout, uint8_t *cred_a,
		   const char *op)
{
	return exofs_sync_op(req, timeout, cred_a);
}

/* Format the LUN to the specified size */
static int format(uint64_t lun_capacity, struct osd_dev *dev, int timeout)
{
	struct osd_request *req = prepare_osd_format_lun(dev, lun_capacity);
	uint8_t cred_a[OSD_CAP_LEN];
	int ret;

	exofs_make_credential(cred_a, 0, 0);

	if (req == NULL) {
		EXOFS_ERR("ERROR: Failed to allocate request.\n");
		return -ENOMEM;
	}

	ret = kick_it(req, timeout, cred_a, "format");

	free_osd_req(req);

	return ret;
}

static int create_partition(struct osd_dev *dev, uint64_t p_id, int timeout)
{
	struct osd_request *req;
	uint8_t cred_a[OSD_CAP_LEN];
	bool try_remove = false;
	int ret;

	exofs_make_credential(cred_a, p_id, 0);

create_part:
	req = prepare_osd_create_partition(dev, p_id);
	if (!req)
		return -ENOMEM;
	ret = kick_it(req, timeout, cred_a, "create partition");
	free_osd_req(req);

	if (ret && !try_remove) {
		try_remove = true;
		req = prepare_osd_remove_partition(dev, p_id);
		if (!req)
			return -ENOMEM;
		ret = kick_it(req, timeout, cred_a, "remove partition");
		free_osd_req(req);
		if (!ret) /* Try again now */
			goto create_part;
	}

	return ret;
}

#ifdef __MKEXOFS_DEBUG_CHECKS
static int list(struct osd_dev *dev, uint64_t p_id, int timeout)
{
	struct osd_request *req;
	uint8_t cred_a[OSD_CAP_LEN];
	unsigned char *buf = NULL;
	int ret;
	uint64_t total_matches;
	uint64_t total_ret;
	uint64_t *id_list;
	int is_part, is_utd;
	uint64_t cont;
	uint32_t more;
	int i;

	buf = kzalloc(1024, GFP_KERNEL);
	if (!buf) {
		EXOFS_ERR("ERROR: Failed to allocate memory.\n");
		return -ENOMEM;
	}

	exofs_make_credential(cred_a, p_id, 0);

	req = prepare_osd_list(dev, p_id, 0, 1024, 0, 0, buf);
	if (req == NULL) {
		EXOFS_ERR("ERROR: Failed to allocate request.\n");
		return -ENOMEM;
	}

	ret = kick_it(req, timeout, cred_a, "list");
	if (ret != 0)
		goto out;

	ret = extract_list_from_req(req, &total_matches, &total_ret, &id_list,
				    &is_part, &is_utd, &cont, &more);

	EXOFS_DBGMSG("created %llu objects:\n", _LLU(total_ret));
	for (i = 0 ; i < total_ret ; i++)
		EXOFS_DBGMSG("%llu\n", _LLU(id_list[i]));

out:
	free_osd_req(req);
	kfree(buf);

	return ret;
}
#endif

static int create(struct osd_dev *dev, uint64_t p_id, uint64_t o_id,
		  int timeout)
{
	struct osd_request *req;
	uint8_t cred_a[OSD_CAP_LEN];
	int ret;

	exofs_make_credential(cred_a, p_id, o_id);
	req = prepare_osd_create(dev, p_id, o_id);

	if (req == NULL) {
		EXOFS_ERR("ERROR: Failed to allocate request.\n");
		return -ENOMEM;
	}

	ret = kick_it(req, timeout, cred_a, "create");

	free_osd_req(req);

	return ret;
}

static int write_super(struct osd_dev *dev, uint64_t p_id, int timeout,
		       int newfile)
{
	struct osd_request *req;
	uint8_t cred_a[OSD_CAP_LEN];
	struct exofs_fscb data;
	int ret;

	exofs_make_credential(cred_a, p_id, EXOFS_SUPER_ID);

	data.s_nextid = cpu_to_le64(4);
	data.s_magic = cpu_to_le16(EXOFS_SUPER_MAGIC);
	data.s_newfs = 1;
	if (newfile)
		data.s_numfiles = cpu_to_le32(1);
	else
		data.s_numfiles = 0;

	req = prepare_osd_write(dev, p_id, EXOFS_SUPER_ID,
				sizeof(struct exofs_fscb), 0, &data);

	if (req == NULL) {
		EXOFS_ERR("ERROR: Failed to allocate request.\n");
		return -ENOMEM;
	}

	ret = kick_it(req, timeout, cred_a, "write super");

	free_osd_req(req);

	return ret;
}

#ifdef __MKEXOFS_DEBUG_CHECKS
static int read_super(struct osd_dev *dev, uint64_t p_id, int timeout)
{
	struct osd_request *req;
	uint8_t cred_a[OSD_CAP_LEN];
	struct exofs_fscb data;
	int ret;

	exofs_make_credential(cred_a, p_id, EXOFS_SUPER_ID);

	req = prepare_osd_read(dev, p_id, EXOFS_SUPER_ID,
				sizeof(struct exofs_fscb), 0, 0,
				(unsigned char *)(&data));

	if (req == NULL) {
		EXOFS_ERR("ERROR: Failed to allocate request.\n");
		return -ENOMEM;
	}

	ret = kick_it(req, timeout, cred_a, "read super");
	if (ret)
		goto out;

	EXOFS_DBGMSG("nextid:\t%u\n", le64_to_cpu(data.s_nextid));
	EXOFS_DBGMSG("magic:\t%u\n", le16_to_cpu(data.s_magic));
	EXOFS_DBGMSG("numfiles:\t%u\n", le32_to_cpu(data.s_numfiles));
out:
	free_osd_req(req);

	return ret;
}
#endif

static int write_bitmap(struct osd_dev *dev, uint64_t p_id, int timeout)
{
	struct osd_request *req;
	uint8_t cred_a[OSD_CAP_LEN];
	uint64_t off = 0;
	unsigned int id = 3;
	int ret;

	/* XXX: For now just use counter - later make bitmap */
	exofs_make_credential(cred_a, p_id, EXOFS_BM_ID);

	req = prepare_osd_write(dev, p_id, EXOFS_BM_ID, sizeof(unsigned int),
				off, &id);
	if (req == NULL) {
		EXOFS_ERR("ERROR: Failed to allocate request.\n");
		return -ENOMEM;
	}

	ret = kick_it(req, timeout, cred_a, "write bitmap");

	free_osd_req(req);

	return ret;
}

#ifdef __MKEXOFS_DEBUG_CHECKS
static int write_testfile(struct osd_dev *dev, uint64_t p_id, int timeout)
{
	struct osd_request *req;
	uint8_t cred_a[OSD_CAP_LEN];
	uint64_t off = 0;
	unsigned char buf[64];
	int ret;

	strcpy((char *)buf, "This file is a test, it is only a test.");
	exofs_make_credential(cred_a, p_id, EXOFS_TEST_ID);

	req = prepare_osd_write(dev, p_id, EXOFS_TEST_ID, 64, off, 0, buf);

	if (req == NULL) {
		EXOFS_ERR("ERROR: Failed to allocate request.\n");
		return -ENOMEM;
	}

	ret = kick_it(req, timeout, cred_a, "write bitmap");

	free_osd_req(req);

	return ret;
}

static int read_testfile(struct osd_dev *dev, uint64_t p_id, int timeout)
{
	struct osd_request *req;
	uint8_t cred_a[OSD_CAP_LEN];
	unsigned char data[64];
	int ret;

	exofs_make_credential(cred_a, p_id, EXOFS_TEST_ID);

	req = prepare_osd_read(dev, p_id, EXOFS_TEST_ID, 64, 0, 0, data);

	if (req == NULL) {
		EXOFS_ERR("ERROR: Failed to allocate request.\n");
		return -ENOMEM;
	}

	ret = kick_it(req, timeout, cred_a, "read test file");
	if (ret)
		goto out;

	EXOFS_DBGMSG("test file: %s\n", data);

out:
	free_osd_req(req);

	return ret;
}
#endif

static int write_rootdir(struct osd_dev *dev, uint64_t p_id, int timeout,
			 int newfile)
{
	struct osd_request *req;
	uint8_t cred_a[OSD_CAP_LEN];
	struct exofs_dir_entry *dir;
	uint64_t off = 0;
	unsigned char *buf = NULL;
	int filetype = EXOFS_FT_DIR << 8;
	int filetype2 = EXOFS_FT_REG_FILE << 8;
	int rec_len;
	int done;
	int ret;

	buf = kzalloc(EXOFS_BLKSIZE, GFP_KERNEL);
	if (!buf) {
		EXOFS_ERR("ERROR: Failed to allocate memory.\n");
		return -ENOMEM;
	}
	dir = (struct exofs_dir_entry *)buf;

	/* create entry for '.' */
	dir->name[0] = '.';
	dir->name_len = 1 | filetype;
	dir->inode_no = cpu_to_le64(EXOFS_ROOT_ID - EXOFS_OBJ_OFF);
	dir->rec_len = cpu_to_le16(EXOFS_DIR_REC_LEN(1));
	rec_len = EXOFS_BLKSIZE - EXOFS_DIR_REC_LEN(1);

	/* create entry for '..' */
	dir = (struct exofs_dir_entry *) (buf + le16_to_cpu(dir->rec_len));
	dir->name[0] = '.';
	dir->name[1] = '.';
	dir->name_len = 2 | filetype;
	dir->inode_no = cpu_to_le64(EXOFS_ROOT_ID - EXOFS_OBJ_OFF);
	if (newfile) {
		rec_len -= EXOFS_DIR_REC_LEN(2);
		dir->rec_len = cpu_to_le16(EXOFS_DIR_REC_LEN(2));
	} else
		dir->rec_len = cpu_to_le16(rec_len);
	done = EXOFS_DIR_REC_LEN(1) + le16_to_cpu(dir->rec_len);

	/* create entry for 'test', if specified */
	if (newfile) {
		dir = (struct exofs_dir_entry *) (buf + done);
		dir->inode_no = cpu_to_le64(EXOFS_TEST_ID - EXOFS_OBJ_OFF);
		dir->name_len = 4 | filetype2;
		dir->name[0] = 't';
		dir->name[1] = 'e';
		dir->name[2] = 's';
		dir->name[3] = 't';
		dir->rec_len = cpu_to_le16(rec_len);
	}

	exofs_make_credential(cred_a, p_id, EXOFS_ROOT_ID);

	req = prepare_osd_write(dev, p_id, EXOFS_ROOT_ID, EXOFS_BLKSIZE, off,
				buf);

	if (req == NULL) {
		EXOFS_ERR("ERROR: Failed to allocate request.\n");
		return -ENOMEM;
	}

	ret = kick_it(req, timeout, cred_a, "write rootdir");

	free_osd_req(req);

	return ret;
}

static int set_inode(struct osd_dev *dev, uint64_t p_id, int timeout,
		     uint64_t o_id, uint16_t mode)
{
	struct osd_request *req;
	uint8_t cred_a[OSD_CAP_LEN];
	struct exofs_fcb in = {0};
	struct exofs_fcb *inode = &in;
	uint32_t i_generation;
	int ret;

	inode->i_mode = cpu_to_le16(mode);
	inode->i_uid = inode->i_gid = 0;
	inode->i_links_count = cpu_to_le16(2);
	inode->i_ctime = inode->i_atime = inode->i_mtime =
				       (signed)cpu_to_le32(CURRENT_TIME.tv_sec);
	inode->i_size = cpu_to_le64(EXOFS_BLKSIZE);
	if (o_id != EXOFS_ROOT_ID)
		inode->i_size = cpu_to_le64(64);

	get_random_bytes(&i_generation, sizeof(i_generation));
	inode->i_generation = cpu_to_le32(i_generation);

	exofs_make_credential(cred_a, p_id, o_id);

	req = prepare_osd_set_attr(dev, p_id, o_id);

	if (req == NULL) {
		EXOFS_ERR("ERROR: Failed to allocate request.\n");
		return -ENOMEM;
	}

	prepare_set_attr_list_add_entry(req,
			OSD_PAGE_NUM_IBM_UOBJ_FS_DATA,
			OSD_ATTR_NUM_IBM_UOBJ_FS_DATA_INODE,
			EXOFS_INO_ATTR_SIZE, (u8 *)inode);

	ret = kick_it(req, timeout, cred_a, "set inode");

	free_osd_req(req);

	return ret;
}

#ifdef __MKEXOFS_DEBUG_CHECKS
static int get_root_attr(struct osd_dev *dev, uint64_t p_id, int timeout)
{
	struct osd_request *req;
	uint8_t cred_a[OSD_CAP_LEN];
	struct exofs_fcb in = {0};
	struct exofs_fcb *inode = &in;
	uint32_t attr_page = OSD_PAGE_NUM_IBM_UOBJ_FS_DATA;
	uint32_t attr_id = OSD_ATTR_NUM_IBM_UOBJ_FS_DATA_INODE;
	uint16_t expected = EXOFS_INO_ATTR_SIZE;
	uint8_t *buf;
	int ret;

	exofs_make_credential(cred_a, p_id, EXOFS_ROOT_ID);

	req = prepare_osd_get_attr(dev, p_id, EXOFS_ROOT_ID);
	if (req == NULL) {
		EXOFS_ERR("ERROR: Failed to allocate request.\n");
		return -ENOMEM;
	}

	prepare_get_attr_list_add_entry(req,
			OSD_PAGE_NUM_IBM_UOBJ_FS_DATA,
			OSD_ATTR_NUM_IBM_UOBJ_FS_DATA_INODE,
			EXOFS_INO_ATTR_SIZE);

	ret = kick_it(req, timeout, cred_a, "get root inode");
	if (ret)
		goto out;

	ret = extract_next_attr_from_req(req, &attr_page, &attr_id, &expected,
					 &buf);
	if (ret) {
		EXOFS_ERR("ERROR: extract attr from req failed\n");
		goto out;
	}

	memcpy(inode, buf, sizeof(struct exofs_fcb));

	EXOFS_DBGMSG("mode: %u\n", le16_to_cpu(inode->i_mode));
	EXOFS_DBGMSG("uid: %u\n", le32_to_cpu(inode->i_uid));
	EXOFS_DBGMSG("gid: %u\n", le32_to_cpu(inode->i_gid));
	EXOFS_DBGMSG("links: %u\n", le16_to_cpu(inode->i_links_count));
	EXOFS_DBGMSG("ctime: %u\n", le32_to_cpu(inode->i_ctime));
	EXOFS_DBGMSG("atime: %u\n", le32_to_cpu(inode->i_atime));
	EXOFS_DBGMSG("mtime: %u\n", le32_to_cpu(inode->i_mtime));
	EXOFS_DBGMSG("gen: %u\n", le32_to_cpu(inode->i_generation));
	EXOFS_DBGMSG("size: %llu\n", _LLU(le64_to_cpu(inode->i_size)));

out:
	free_osd_req(req);

	return ret;
}
#endif

/*
 * This function creates an exofs file system on the specified OSD partition.
 */
int exofs_mkfs(struct osd_dev *dev, uint64_t p_id, uint64_t format_size_meg)
{
	int err;
	const int to_format = (4 * 60 * HZ);
	const int to_gen = (60 * HZ);
	bool newfile = false;

	/* Get a handle */
	EXOFS_DBGMSG("setting up exofs on partition %llu:\n", _LLU(p_id));

	/* Format LUN if requested */
	if (format_size_meg > 0) {
		EXOFS_DBGMSG("formatting %llu Mgb...\n", _LLU(format_size_meg));
		err = format(format_size_meg * 1024 * 1024, dev, to_format);
		if (err)
			goto out;
		EXOFS_DBGMSG(" OK\n");
	}

	/* Create partition */
	EXOFS_DBGMSG("creating partition...\n");
	err = create_partition(dev, p_id, to_gen);
	if (err)
		goto out;
	EXOFS_DBGMSG(" OK\n");

	/* Create object with known ID for superblock info */
	EXOFS_DBGMSG("creating superblock...\n");
	err = create(dev, p_id, EXOFS_SUPER_ID, to_gen);
	if (err)
		goto out;
	EXOFS_DBGMSG(" OK\n");

	/* Create root directory object */
	EXOFS_DBGMSG("creating root directory...\n");
	err = create(dev, p_id, EXOFS_ROOT_ID, to_gen);
	if (err)
		goto out;
	EXOFS_DBGMSG(" OK\n");

	/* Create bitmap object */
	EXOFS_DBGMSG("creating free ID bitmap...\n");
	err = create(dev, p_id, EXOFS_BM_ID, to_gen);
	if (err)
		goto out;
	EXOFS_DBGMSG(" OK\n");

#ifdef __MKEXOFS_DEBUG_CHECKS
	/* Create a test file, if specified by options */
	if (newfile) {
		EXOFS_DBGMSG("creating test file...\n");
		err = create(dev, p_id, EXOFS_TEST_ID, to_gen);
		if (err)
			goto out;
		EXOFS_DBGMSG(" OK\n");
	}
#endif

	/* Write superblock */
	EXOFS_DBGMSG("writing superblock...\n");
	err = write_super(dev, p_id, to_gen, newfile);
	if (err)
		goto out;
	EXOFS_DBGMSG(" OK\n");

	/* Write root directory */
	EXOFS_DBGMSG("writing root directory...\n");
	err = write_rootdir(dev, p_id, to_gen, newfile);
	if (err)
		goto out;
	EXOFS_DBGMSG(" OK\n");

	/* Set root partition inode attribute */
	EXOFS_DBGMSG("writing root inode...\n");
	err = set_inode(dev, p_id, to_gen, EXOFS_ROOT_ID,
			0040000 | (0777 & ~022));
	if (err)
		goto out;
	EXOFS_DBGMSG(" OK\n");

#ifdef __MKEXOFS_DEBUG_CHECKS
	/* Set test file inode attribute */
	if (newfile) {
		EXOFS_DBGMSG("writing test inode...\n");
		err = set_inode(dev, p_id, to_gen, EXOFS_TEST_ID,
				0100000 | (0777 & ~022));
		if (err)
			goto out;
		EXOFS_DBGMSG(" OK\n");
	}
#endif
	/* Write bitmap */
	EXOFS_DBGMSG("writing free ID bitmap...\n");
	err = write_bitmap(dev, p_id, to_gen);
	if (err)
		goto out;
	EXOFS_DBGMSG(" OK\n");

#ifdef __MKEXOFS_DEBUG_CHECKS
	/* Write test file */
	if (newfile) {
		EXOFS_DBGMSG("writing test file...\n");
		err = write_testfile(dev, p_id, to_gen);
		if (err)
			goto out;
		EXOFS_DBGMSG(" OK\n");
	}

	/* some debug info */
	{
		EXOFS_DBGMSG("listing:\n");
		list(dev, p_id, to_gen);
		EXOFS_DBGMSG("contents of superblock:\n");
		read_super(dev, p_id, to_gen);
		EXOFS_DBGMSG("contents of root inode:\n");
		get_root_attr(dev, p_id, to_gen);
		if (newfile) {
			EXOFS_DBGMSG("contents of test file:\n");
			read_testfile(dev, p_id, to_gen);
		}
	}
#endif
	EXOFS_DBGMSG("\nsetup complete: enjoy your shiny new exofs!\n");

out:
	return err;
}
