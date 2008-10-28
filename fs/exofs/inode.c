/*
 * Copyright (C) 2005, 2006
 * Avishay Traeger (avishay@gmail.com) (avishay@il.ibm.com)
 * Copyright (C) 2005, 2006
 * International Business Machines
 * Copyright (C) 2008, 2009
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * Copyrights for code taken from ext2:
 *     Copyright (C) 1992, 1993, 1994, 1995
 *     Remy Card (card@masi.ibp.fr)
 *     Laboratoire MASI - Institut Blaise Pascal
 *     Universite Pierre et Marie Curie (Paris VI)
 *     from
 *     linux/fs/minix/inode.c
 *     Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is part of exofs.
 *
 * exofs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.  Since it is based on ext2, and the only
 * valid version of GPL for the Linux kernel is version 2, the only valid
 * version of GPL for exofs is version 2.
 *
 * exofs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with exofs; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/writeback.h>
#include <linux/buffer_head.h>

#include "exofs.h"

static int __readpage_filler(struct page *page, bool is_async_unlock);

int exofs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags,
		struct page **pagep, void **fsdata)
{
	int ret = 0;
	struct page *page;

	page = *pagep;
	if (page == NULL) {
		ret = simple_write_begin(file, mapping, pos, len, flags, pagep,
					 fsdata);
		page = *pagep;
	}

	 /* read modify write */
	if (!PageUptodate(page) && (len != PAGE_CACHE_SIZE))
			ret = __readpage_filler(page, false);

	return ret;
}

static int exofs_write_begin_export(struct file *file,
		struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags,
		struct page **pagep, void **fsdata)
{
	*pagep = NULL;

	return exofs_write_begin(file, mapping, pos, len, flags, pagep,
					fsdata);
}

/*
 * Callback function when writepage finishes.  Check for errors, unlock, clean
 * up, etc.
 */
static void writepage_done(struct osd_request *req, void *p)
{
	int ret;
	struct page *page = p;
	struct inode *inode = page->mapping->host;
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;

	ret = exofs_check_ok(req);
	free_osd_req(req);
	atomic_dec(&sbi->s_curr_pending);

	if (ret) {
		if (ret == -ENOSPC)
			set_bit(AS_ENOSPC, &page->mapping->flags);
		else
			set_bit(AS_EIO, &page->mapping->flags);

		SetPageError(page);
	}

	end_page_writeback(page);
	unlock_page(page);
}

/*
 * Write a page to disk.  page->index gives us the page number.  The page is
 * locked before this function is called.  We write asynchronously and then the
 * callback function (writepage_done) is called.  We signify that the operation
 * has completed by unlocking the page and calling end_page_writeback().
 */
static int exofs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	struct exofs_i_info *oi = exofs_i(inode);
	loff_t i_size = i_size_read(inode);
	unsigned long end_index = i_size >> PAGE_CACHE_SHIFT;
	unsigned offset = 0;
	struct osd_request *req = NULL;
	struct exofs_sb_info *sbi;
	uint64_t start;
	uint64_t len = PAGE_CACHE_SIZE;
	int ret = 0;

	BUG_ON(!PageLocked(page));

	/* if the object has not been created, and we are not in sync mode,
	 * just return.  otherwise, wait. */
	if (!obj_created(oi)) {
		BUG_ON(!obj_2bcreated(oi));

		if (wbc->sync_mode == WB_SYNC_NONE) {
			redirty_page_for_writepage(wbc, page);
			unlock_page(page);
			ret = 0;
			goto out;
		} else
			wait_event(oi->i_wq, obj_created(oi));
	}

	/* in this case, the page is within the limits of the file */
	if (page->index < end_index)
		goto do_it;

	offset = i_size & (PAGE_CACHE_SIZE - 1);
	len = offset;

	/*in this case, the page is outside the limits (truncate in progress)*/
	if (page->index >= end_index + 1 || !offset) {
		unlock_page(page);
		goto out;
	}

do_it:
	BUG_ON(PageWriteback(page));
	set_page_writeback(page);
	start = page->index << PAGE_CACHE_SHIFT;
	sbi = inode->i_sb->s_fs_info;

	req = prepare_osd_write_pages(sbi->s_dev, sbi->s_pid,
				inode->i_ino + EXOFS_OBJ_OFF, len, start,
				&page, 1);
	if (!req) {
		EXOFS_ERR("ERROR: writepage failed.\n");
		ret = -ENOMEM;
		goto fail;
	}

	oi->i_commit_size = min_t(uint64_t, oi->i_commit_size, len + start);

	ret = exofs_async_op(req, writepage_done, page, oi->i_cred);
	if (ret) {
		free_osd_req(req);
		goto fail;
	}
	atomic_inc(&sbi->s_curr_pending);
out:
	return ret;
fail:
	set_bit(AS_EIO, &page->mapping->flags);
	end_page_writeback(page);
	unlock_page(page);
	goto out;
}

/*
 * Callback for readpage
 */
static int __readpage_done(struct osd_request *req, void *p, int unlock)
{
	struct page *page = p;
	struct inode *inode = page->mapping->host;
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;
	int ret;

	ret = exofs_check_ok(req);
	free_osd_req(req);
	atomic_dec(&sbi->s_curr_pending);

	if (ret == 0) {
		/* Everything is OK */
		SetPageUptodate(page);
		if (PageError(page))
			ClearPageError(page);
	} else if (ret == -EFAULT) {
		/* In this case we were trying to read something that wasn't on
		 * disk yet - return a page full of zeroes.  This should be OK,
		 * because the object should be empty (if there was a write
		 * before this read, the read would be waiting with the page
		 * locked */
		clear_highpage(page);

		SetPageUptodate(page);
		if (PageError(page))
			ClearPageError(page);
	} else /* Error */
		SetPageError(page);

	if (unlock)
		unlock_page(page);

	return ret;
}

static void readpage_done(struct osd_request *req, void *p)
{
	__readpage_done(req, p, true);
}

/*
 * Read a page from the OSD
 */
static int __readpage_filler(struct page *page, bool is_async_unlock)
{
	struct osd_request *req = NULL;
	struct inode *inode = page->mapping->host;
	struct exofs_i_info *oi = exofs_i(inode);
	ino_t ino = inode->i_ino;
	loff_t i_size = i_size_read(inode);
	loff_t i_start = page->index << PAGE_CACHE_SHIFT;
	pgoff_t end_index = i_size >> PAGE_CACHE_SHIFT;
	struct super_block *sb = inode->i_sb;
	struct exofs_sb_info *sbi = sb->s_fs_info;
	uint64_t amount;
	int ret = 0;

	BUG_ON(!PageLocked(page));

	if (PageUptodate(page))
		goto out;

	if (page->index < end_index)
		amount = PAGE_CACHE_SIZE;
	else
		amount = i_size & (PAGE_CACHE_SIZE - 1);

	/* this will be out of bounds, or doesn't exist yet */
	if ((page->index >= end_index + 1) || !obj_created(oi) || !amount
	    /*|| (i_start >= oi->i_commit_size)*/) {
		clear_highpage(page);

		SetPageUptodate(page);
		if (PageError(page))
			ClearPageError(page);
		if (is_async_unlock)
			unlock_page(page);
		goto out;
	}

	if (amount != PAGE_CACHE_SIZE)
		zero_user(page, amount, PAGE_CACHE_SIZE - amount);


	req = prepare_osd_read_pages(sbi->s_dev, sbi->s_pid,
				     ino + EXOFS_OBJ_OFF, amount, i_start,
				     &page, 1);
	if (!req) {
		EXOFS_ERR("ERROR: readpage failed.\n");
		ret = -ENOMEM;
		unlock_page(page);
		goto out;
	}

	atomic_inc(&sbi->s_curr_pending);
	if (!is_async_unlock) {
		exofs_sync_op(req, sbi->s_timeout, oi->i_cred);
		ret = __readpage_done(req, page, false);
	} else {
		ret = exofs_async_op(req, readpage_done, page, oi->i_cred);
		if (ret) {
			free_osd_req(req);
			unlock_page(page);
			atomic_dec(&sbi->s_curr_pending);
		}
	}

out:
	return ret;
}

static int readpage_filler(struct page *page)
{
	int ret = __readpage_filler(page, true);

	return ret;
}

/*
 * We don't need the file
 */
static int exofs_readpage(struct file *file, struct page *page)
{
	return readpage_filler(page);
}

/*
 * We don't need the data
 */
static int readpage_strip(void *data, struct page *page)
{
	return readpage_filler(page);
}

/*
 * read a bunch of pages - usually for readahead
 */
static int exofs_readpages(struct file *file, struct address_space *mapping,
			   struct list_head *pages, unsigned nr_pages)
{
	return read_cache_pages(mapping, pages, readpage_strip, NULL);
}

const struct address_space_operations exofs_aops = {
	.readpage	= exofs_readpage,
	.readpages	= exofs_readpages,
	.writepage	= exofs_writepage,
	.write_begin	= exofs_write_begin_export,
	.write_end	= simple_write_end,
	.writepages	= generic_writepages,
};

/******************************************************************************
 * INODE OPERATIONS
 *****************************************************************************/

/*
 * Test whether an inode is a fast symlink.
 */
static inline int exofs_inode_is_fast_symlink(struct inode *inode)
{
	struct exofs_i_info *oi = exofs_i(inode);

	return S_ISLNK(inode->i_mode) && (oi->i_data[0] != 0);
}

/*
 * get_block_t - Fill in a buffer_head
 * An OSD takes care of block allocation so we just fake an allocation by
 * putting in the inode's sector_t in the buffer_head.
 * TODO: What about the case of create==0 and @iblock does not exist in the
 * object?
 */
static int exofs_get_block(struct inode *inode, sector_t iblock,
		    struct buffer_head *bh_result, int create)
{
	map_bh(bh_result, inode->i_sb, iblock);
	return 0;
}

/*
 * Truncate a file to the specified size - all we have to do is set the size
 * attribute.  We make sure the object exists first.
 */
void exofs_truncate(struct inode *inode)
{
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;
	struct exofs_i_info *oi = exofs_i(inode);
	struct osd_request *req = NULL;
	loff_t isize = i_size_read(inode);
	uint64_t newsize;
	int ret;

	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)
	     || S_ISLNK(inode->i_mode)))
		return;
	if (exofs_inode_is_fast_symlink(inode))
		return;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return;
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;

	nobh_truncate_page(inode->i_mapping, isize, exofs_get_block);

	req = prepare_osd_set_attr(sbi->s_dev, sbi->s_pid,
				 inode->i_ino + EXOFS_OBJ_OFF);
	if (!req) {
		EXOFS_ERR("ERROR: prepare set_attr failed.\n");
		goto fail;
	}

	newsize = cpu_to_le64((uint64_t) isize);
	prepare_set_attr_list_add_entry(req, OSD_APAGE_OBJECT_INFORMATION,
					OSD_ATTR_OI_LOGICAL_LENGTH, 8,
					(unsigned char *)(&newsize));

	/* if we are about to truncate an object, and it hasn't been
	 * created yet, wait
	 */
	if (!obj_created(oi)) {
		BUG_ON(!obj_2bcreated(oi));
		wait_event(oi->i_wq, obj_created(oi));
	}

	ret = exofs_sync_op(req, sbi->s_timeout, oi->i_cred);
	free_osd_req(req);
	if (ret)
		goto fail;

out:
	mark_inode_dirty(inode);
	return;
fail:
	make_bad_inode(inode);
	goto out;
}

/*
 * Set inode attributes - just call generic functions.
 */
int exofs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = dentry->d_inode;
	int error;

	error = inode_change_ok(inode, iattr);
	if (error)
		return error;

	error = inode_setattr(inode, iattr);
	return error;
}

/*
 * Read an inode from the OSD, and return it as is.  We also return the size
 * attribute in the 'sanity' argument if we got compiled with debugging turned
 * on.
 */
static int exofs_get_inode(struct super_block *sb, struct exofs_i_info *oi,
		    struct exofs_fcb *inode, uint64_t *sanity)
{
	struct exofs_sb_info *sbi = sb->s_fs_info;
	struct osd_request *req = NULL;
	uint32_t attr_page;
	uint32_t attr_id;
	uint16_t expected;
	uint8_t *buf;
	uint64_t o_id;
	int ret;

	o_id = oi->vfs_inode.i_ino + EXOFS_OBJ_OFF;

	exofs_make_credential(oi->i_cred, sbi->s_pid, o_id);

	req = prepare_osd_get_attr(sbi->s_dev, sbi->s_pid, o_id);
	if (!req) {
		EXOFS_ERR("ERROR: prepare get_attr failed.\n");
		return -ENOMEM;
	}

	/* we need the inode attribute */
	prepare_get_attr_list_add_entry(req,
					OSD_PAGE_NUM_IBM_UOBJ_FS_DATA,
					OSD_ATTR_NUM_IBM_UOBJ_FS_DATA_INODE,
					EXOFS_INO_ATTR_SIZE);

#ifdef EXOFS_DEBUG
	/* we get the size attributes to do a sanity check */
	prepare_get_attr_list_add_entry(req,
					OSD_APAGE_OBJECT_INFORMATION,
					OSD_ATTR_OI_LOGICAL_LENGTH, 8);
#endif

	ret = exofs_sync_op(req, sbi->s_timeout, oi->i_cred);
	if (ret)
		goto out;

	attr_page = OSD_PAGE_NUM_IBM_UOBJ_FS_DATA;
	attr_id = OSD_ATTR_NUM_IBM_UOBJ_FS_DATA_INODE;
	expected = EXOFS_INO_ATTR_SIZE;
	ret = extract_next_attr_from_req(req, &attr_page, &attr_id, &expected,
					 &buf);
	if (ret) {
		EXOFS_ERR("ERROR: extract attr from req failed\n");
		goto out;
	}
	memcpy(inode, buf, sizeof(struct exofs_fcb));

#ifdef EXOFS_DEBUG
	attr_page = OSD_APAGE_OBJECT_INFORMATION;
	attr_id = OSD_ATTR_OI_LOGICAL_LENGTH;
	expected = 8;
	ret = extract_next_attr_from_req(req, &attr_page, &attr_id, &expected,
					 &buf);
	if (ret) {
		EXOFS_ERR("ERROR: extract attr from req failed\n");
		goto out;
	}
	*sanity = le64_to_cpu(*((uint64_t *) buf));
#endif

out:
	free_osd_req(req);
	return ret;
}

/*
 * Fill in an inode read from the OSD and set it up for use
 */
struct inode *exofs_iget(struct super_block *sb, unsigned long ino)
{
	struct exofs_i_info *oi;
	struct exofs_fcb fcb;
	struct inode *inode;
	uint64_t sanity;
	int ret;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))
		return inode;
	oi = exofs_i(inode);

	/* read the inode from the osd */
	ret = exofs_get_inode(sb, oi, &fcb, &sanity);
	if (ret)
		goto bad_inode;

	init_waitqueue_head(&oi->i_wq);
	set_obj_created(oi);

	/* copy stuff from on-disk struct to in-memory struct */
	inode->i_mode = le16_to_cpu(fcb.i_mode);
	inode->i_uid = le32_to_cpu(fcb.i_uid);
	inode->i_gid = le32_to_cpu(fcb.i_gid);
	inode->i_nlink = le16_to_cpu(fcb.i_links_count);
	inode->i_ctime.tv_sec = (signed)le32_to_cpu(fcb.i_ctime);
	inode->i_atime.tv_sec = (signed)le32_to_cpu(fcb.i_atime);
	inode->i_mtime.tv_sec = (signed)le32_to_cpu(fcb.i_mtime);
	inode->i_ctime.tv_nsec =
		inode->i_atime.tv_nsec = inode->i_mtime.tv_nsec = 0;
	oi->i_commit_size = le64_to_cpu(fcb.i_size);
	i_size_write(inode, oi->i_commit_size);
	inode->i_blkbits = EXOFS_BLKSHIFT;
	inode->i_generation = le32_to_cpu(fcb.i_generation);

#ifdef EXOFS_DEBUG
	if ((inode->i_size != sanity) &&
		(!exofs_inode_is_fast_symlink(inode))) {
		EXOFS_ERR("WARNING: Size of object from inode and "
			  "attributes differ (%lld != %llu)\n",
			  inode->i_size, _LLU(sanity));
	}
#endif

	oi->i_dir_start_lookup = 0;

	if ((inode->i_nlink == 0) && (inode->i_mode == 0)) {
		ret = -ESTALE;
		goto bad_inode;
	}

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		if (fcb.i_data[0])
			inode->i_rdev =
				old_decode_dev(le32_to_cpu(fcb.i_data[0]));
		else
			inode->i_rdev =
				new_decode_dev(le32_to_cpu(fcb.i_data[1]));
	} else {
		memcpy(oi->i_data, fcb.i_data, sizeof(fcb.i_data));
	}

	if (S_ISREG(inode->i_mode)) {
		inode->i_op = &exofs_file_inode_operations;
		inode->i_fop = &exofs_file_operations;
		inode->i_mapping->a_ops = &exofs_aops;
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &exofs_dir_inode_operations;
		inode->i_fop = &exofs_dir_operations;
		inode->i_mapping->a_ops = &exofs_aops;
	} else if (S_ISLNK(inode->i_mode)) {
		if (exofs_inode_is_fast_symlink(inode))
			inode->i_op = &exofs_fast_symlink_inode_operations;
		else {
			inode->i_op = &exofs_symlink_inode_operations;
			inode->i_mapping->a_ops = &exofs_aops;
		}
	} else {
		inode->i_op = &exofs_special_inode_operations;
		if (fcb.i_data[0])
			init_special_inode(inode, inode->i_mode,
			   old_decode_dev(le32_to_cpu(fcb.i_data[0])));
		else
			init_special_inode(inode, inode->i_mode,
			   new_decode_dev(le32_to_cpu(fcb.i_data[1])));
	}

	unlock_new_inode(inode);
	return inode;

bad_inode:
	iget_failed(inode);
	return ERR_PTR(ret);
}

/*
 * Callback function from exofs_new_inode().  The important thing is that we
 * set the obj_created flag so that other methods know that the object exists on
 * the OSD.
 */
static void create_done(struct osd_request *req, void *p)
{
	struct inode *inode = p;
	struct exofs_i_info *oi = exofs_i(inode);
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;
	int ret;

	ret = exofs_check_ok(req);
	free_osd_req(req);
	atomic_dec(&sbi->s_curr_pending);

	if (ret)
		make_bad_inode(inode);
	else
		set_obj_created(oi);

	atomic_dec(&inode->i_count);
}

/*
 * Set up a new inode and create an object for it on the OSD
 */
struct inode *exofs_new_inode(struct inode *dir, int mode)
{
	struct super_block *sb;
	struct inode *inode;
	struct exofs_i_info *oi;
	struct exofs_sb_info *sbi;
	struct osd_request *req = NULL;
	int ret;

	sb = dir->i_sb;
	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	oi = exofs_i(inode);

	init_waitqueue_head(&oi->i_wq);
	set_obj_2bcreated(oi);

	sbi = sb->s_fs_info;

	sb->s_dirt = 1;
	inode->i_uid = current->cred->fsuid;
	if (dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else {
		inode->i_gid = current->cred->fsgid;
	}
	inode->i_mode = mode;

	inode->i_ino = sbi->s_nextid++;
	inode->i_blkbits = EXOFS_BLKSHIFT;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	oi->i_commit_size = inode->i_size = 0;
	spin_lock(&sbi->s_next_gen_lock);
	inode->i_generation = sbi->s_next_generation++;
	spin_unlock(&sbi->s_next_gen_lock);
	insert_inode_hash(inode);

	mark_inode_dirty(inode);

	req = prepare_osd_create(sbi->s_dev, sbi->s_pid,
				 inode->i_ino + EXOFS_OBJ_OFF);
	if (!req) {
		EXOFS_ERR("ERROR: prepare_osd_create failed\n");
		return ERR_PTR(-EIO);
	}

	exofs_make_credential(oi->i_cred, sbi->s_pid,
			      inode->i_ino + EXOFS_OBJ_OFF);

	/* increment the refcount so that the inode will still be around when we
	 * reach the callback
	 */
	atomic_inc(&inode->i_count);

	ret = exofs_async_op(req, create_done, inode, oi->i_cred);
	if (ret) {
		atomic_dec(&inode->i_count);
		free_osd_req(req);
		return ERR_PTR(-EIO);
	}
	atomic_inc(&sbi->s_curr_pending);

	return inode;
}

/*
 * Callback function from exofs_update_inode().
 */
static void updatei_done(struct osd_request *req, void *p)
{
	struct updatei_args *args = p;

	free_osd_req(req);

	atomic_dec(&args->sbi->s_curr_pending);

	kfree(args);
}

/*
 * Write the inode to the OSD.  Just fill up the struct, and set the attribute
 * synchronously or asynchronously depending on the do_sync flag.
 */
static int exofs_update_inode(struct inode *inode, int do_sync)
{
	struct exofs_i_info *oi = exofs_i(inode);
	struct super_block *sb = inode->i_sb;
	struct exofs_sb_info *sbi = sb->s_fs_info;
	struct osd_request *req;
	struct exofs_fcb *fcb;
	struct updatei_args *args;
	int ret;

	args = kzalloc(sizeof(*args), GFP_KERNEL);
	if (!args)
		return -ENOMEM;

	fcb = &args->fcb;

	fcb->i_mode = cpu_to_le16(inode->i_mode);
	fcb->i_uid = cpu_to_le32(inode->i_uid);
	fcb->i_gid = cpu_to_le32(inode->i_gid);
	fcb->i_links_count = cpu_to_le16(inode->i_nlink);
	fcb->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
	fcb->i_atime = cpu_to_le32(inode->i_atime.tv_sec);
	fcb->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
	oi->i_commit_size = i_size_read(inode);
	fcb->i_size = cpu_to_le64(oi->i_commit_size);
	fcb->i_generation = cpu_to_le32(inode->i_generation);

	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
		if (old_valid_dev(inode->i_rdev)) {
			fcb->i_data[0] =
				cpu_to_le32(old_encode_dev(inode->i_rdev));
			fcb->i_data[1] = 0;
		} else {
			fcb->i_data[0] = 0;
			fcb->i_data[1] =
				cpu_to_le32(new_encode_dev(inode->i_rdev));
			fcb->i_data[2] = 0;
		}
	} else
		memcpy(fcb->i_data, oi->i_data, sizeof(fcb->i_data));

	req = prepare_osd_set_attr(sbi->s_dev, sbi->s_pid,
				 (uint64_t) (inode->i_ino + EXOFS_OBJ_OFF));
	if (!req) {
		EXOFS_ERR("ERROR: prepare set_attr failed.\n");
		ret = -ENOMEM;
		goto free_args;
	}

	prepare_set_attr_list_add_entry(req,
					OSD_PAGE_NUM_IBM_UOBJ_FS_DATA,
					OSD_ATTR_NUM_IBM_UOBJ_FS_DATA_INODE,
					EXOFS_INO_ATTR_SIZE,
					(unsigned char *)fcb);

	if (!obj_created(oi)) {
		BUG_ON(!obj_2bcreated(oi));
		wait_event(oi->i_wq, obj_created(oi));
	}

	if (do_sync) {
		ret = exofs_sync_op(req, sbi->s_timeout, oi->i_cred);
		free_osd_req(req);
		goto free_args;
	} else {
		args->sbi = sbi;

		ret = exofs_async_op(req, updatei_done, args, oi->i_cred);
		if (ret) {
			free_osd_req(req);
			goto free_args;
		}
		atomic_inc(&sbi->s_curr_pending);
		goto out; /* deallocation in updatei_done */
	}

free_args:
	kfree(args);
out:
	return ret;
}

int exofs_write_inode(struct inode *inode, int wait)
{
	return exofs_update_inode(inode, wait);
}

int exofs_sync_inode(struct inode *inode)
{
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 0,	/* sys_fsync did this */
	};

	return sync_inode(inode, &wbc);
}

/*
 * Callback function from exofs_delete_inode() - don't have much cleaning up to
 * do.
 */
static void delete_done(struct osd_request *req, void *p)
{
	struct exofs_sb_info *sbi;
	free_osd_req(req);
	sbi = p;
	atomic_dec(&sbi->s_curr_pending);
}

/*
 * Called when the refcount of an inode reaches zero.  We remove the object
 * from the OSD here.  We make sure the object was created before we try and
 * delete it.
 */
void exofs_delete_inode(struct inode *inode)
{
	struct exofs_i_info *oi = exofs_i(inode);
	struct osd_request *req = NULL;
	struct super_block *sb = inode->i_sb;
	struct exofs_sb_info *sbi = sb->s_fs_info;
	int ret;

	truncate_inode_pages(&inode->i_data, 0);

	if (is_bad_inode(inode))
		goto no_delete;
	mark_inode_dirty(inode);
	exofs_update_inode(inode, inode_needs_sync(inode));

	inode->i_size = 0;
	if (inode->i_blocks)
		exofs_truncate(inode);

	clear_inode(inode);

	req = prepare_osd_remove(sbi->s_dev, sbi->s_pid,
				 inode->i_ino + EXOFS_OBJ_OFF);
	if (!req) {
		EXOFS_ERR("ERROR: prepare_osd_remove failed\n");
		return;
	}

	/* if we are deleting an obj that hasn't been created yet, wait */
	if (!obj_created(oi)) {
		BUG_ON(!obj_2bcreated(oi));
		wait_event(oi->i_wq, obj_created(oi));
	}

	ret = exofs_async_op(req, delete_done, sbi, oi->i_cred);
	if (ret) {
		EXOFS_ERR(
		       "ERROR: @exofs_delete_inode exofs_async_op failed\n");
		free_osd_req(req);
		return;
	}
	atomic_inc(&sbi->s_curr_pending);

	return;

no_delete:
	clear_inode(inode);
}
