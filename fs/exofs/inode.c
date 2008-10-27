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

#ifdef CONFIG_EXOFS_DEBUG
#  define EXOFS_DEBUG_OBJ_ISIZE 1
#endif

/*
 * Callback for readpage
 */
static int __readpage_done(struct osd_request *or, void *p, int unlock)
{
	struct page *page = p;
	struct inode *inode = page->mapping->host;
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;
	int ret;

	ret = exofs_check_ok(or);
	osd_end_request(or);

	EXOFS_DBGMSG("ret=>%d unlock=%d page=%p\n", ret, unlock, page);

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

	atomic_dec(&sbi->s_curr_pending);
	if (unlock)
		unlock_page(page);

	return ret;
}

static void readpage_done(struct osd_request *or, void *p)
{
	__readpage_done(or, p, true);
}

/*
 * Read a page from the OSD
 */
static int __readpage_filler(struct page *page, bool is_async)
{
	struct osd_request *or = NULL;
	struct inode *inode = page->mapping->host;
	struct exofs_i_info *oi = exofs_i(inode);
	ino_t ino = inode->i_ino;
	loff_t i_size = i_size_read(inode);
	loff_t i_start = page->index << PAGE_CACHE_SHIFT;
	pgoff_t end_index = i_size >> PAGE_CACHE_SHIFT;
	struct super_block *sb = inode->i_sb;
	struct exofs_sb_info *sbi = sb->s_fs_info;
	struct osd_obj_id obj = {sbi->s_pid, ino + EXOFS_OBJ_OFF};
	uint64_t amount;
	int ret = 0;

	BUG_ON(!PageLocked(page));

	if (PageUptodate(page))
		goto unlock;

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
		goto unlock;
	}

	if (amount != PAGE_CACHE_SIZE)
		zero_user(page, amount, PAGE_CACHE_SIZE - amount);

	or = osd_start_request(sbi->s_dev, GFP_KERNEL);
	if (unlikely(!or)) {
		ret = -ENOMEM;
		goto err;
	}

	ret = osd_req_read_pages(or, &obj, i_start, amount, &page, 1);
	if (unlikely(ret))
		goto err;

	atomic_inc(&sbi->s_curr_pending);
	if (is_async) {
		ret = exofs_async_op(or, readpage_done, page, oi->i_cred);
		if (unlikely(ret)) {
			atomic_dec(&sbi->s_curr_pending);
			goto err;
		}
	} else {
		exofs_sync_op(or, sbi->s_timeout, oi->i_cred);
		ret = __readpage_done(or, page, false);
	}

	EXOFS_DBGMSG("ret=>%d unlock=%d page=%p\n", ret, is_async, page);
	return ret;

err:
	if (or)
		osd_end_request(or);
	SetPageError(page);
	EXOFS_DBGMSG("@err\n");
unlock:
	if (is_async)
		unlock_page(page);
	EXOFS_DBGMSG("@unlock is_async=%d\n", is_async);
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

/*
 * Callback function when writepage finishes.  Check for errors, unlock, clean
 * up, etc.
 */
static void writepage_done(struct osd_request *or, void *p)
{
	int ret;
	struct page *page = p;
	struct inode *inode = page->mapping->host;
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;

	ret = exofs_check_ok(or);
	osd_end_request(or);
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
	struct osd_obj_id obj;
	loff_t i_size = i_size_read(inode);
	unsigned long end_index = i_size >> PAGE_CACHE_SHIFT;
	unsigned offset = 0;
	struct osd_request *or;
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
	oi->i_commit_size = min_t(uint64_t, oi->i_commit_size, len + start);

	or = osd_start_request(sbi->s_dev, GFP_KERNEL);
	if (unlikely(!or)) {
		EXOFS_ERR("ERROR: writepage failed.\n");
		ret = -ENOMEM;
		goto fail;
	}

	obj.partition = sbi->s_pid;
	obj.id = inode->i_ino + EXOFS_OBJ_OFF;
	ret = osd_req_write_pages(or, &obj, start, len, &page, 1);
	if (ret)
		goto fail;

	ret = exofs_async_op(or, writepage_done, page, oi->i_cred);
	if (ret)
		goto fail;

	atomic_inc(&sbi->s_curr_pending);
out:
	return ret;
fail:
	if (or)
		osd_end_request(or);
	set_bit(AS_EIO, &page->mapping->flags);
	end_page_writeback(page);
	unlock_page(page);
	goto out;
}

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
		if (ret) {
			EXOFS_DBGMSG("simple_write_begin faild\n");
			return ret;
		}

		page = *pagep;
	}

	 /* read modify write */
	if (!PageUptodate(page) && (len != PAGE_CACHE_SIZE)) {
		ret = __readpage_filler(page, false);
		if (ret) {
			/*SetPageError was done by readpage_filler. Is it ok?*/
			unlock_page(page);
			EXOFS_DBGMSG("__readpage_filler faild\n");
		}
	}

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

const struct osd_attr g_attr_logical_length = ATTR_DEF(
	OSD_APAGE_OBJECT_INFORMATION, OSD_ATTR_OI_LOGICAL_LENGTH, 8);

/*
 * Truncate a file to the specified size - all we have to do is set the size
 * attribute.  We make sure the object exists first.
 */
void exofs_truncate(struct inode *inode)
{
	struct exofs_sb_info *sbi = inode->i_sb->s_fs_info;
	struct exofs_i_info *oi = exofs_i(inode);
	struct osd_obj_id obj = {sbi->s_pid, inode->i_ino + EXOFS_OBJ_OFF};
	struct osd_request *or;
	struct osd_attr attr;
	loff_t isize = i_size_read(inode);
	__be64 newsize;
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

	or = osd_start_request(sbi->s_dev, GFP_KERNEL);
	if (unlikely(!or)) {
		EXOFS_ERR("ERROR: exofs_truncate: osd_start_request failed\n");
		goto fail;
	}

	osd_req_set_attributes(or, &obj);

	newsize = cpu_to_be64((u64)isize);
	attr = g_attr_logical_length;
	attr.val_ptr = &newsize;
	osd_req_add_set_attr_list(or, &attr, 1);

	/* if we are about to truncate an object, and it hasn't been
	 * created yet, wait
	 */
	if (unlikely(wait_obj_created(oi)))
		goto fail;

	ret = exofs_sync_op(or, sbi->s_timeout, oi->i_cred);
	osd_end_request(or);
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
