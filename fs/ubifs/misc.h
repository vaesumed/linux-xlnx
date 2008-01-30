/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006, 2007 Nokia Corporation
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
 * This file contains miscelanious helper finctions.
 */

#ifndef __UBIFS_MISC_H__
#define __UBIFS_MISC_H__

/**
 * ubifs_set_i_bytes - set inode size for VFS.
 * @inode: the inode to set
 *
 * This is a helper function which sets @inode->i_bytes and @inode->i_blopcks.
 * VFS expects the blocks size in this case to be 512 bytes, no matter what is
 * the FS's I/O block size (ours is 4KiB).
 */
static inline void ubifs_set_i_bytes(struct inode *inode)
{
	loff_t size = i_size_read(inode);

	inode->i_bytes = size & 0x1FF;

	/* First align inode size up to UBIFS block size boundary */
	size = (size + UBIFS_BLOCK_SIZE - 1) & ~UBIFS_BLOCK_MASK;
	/* Then calculate amount of 512 byte blocks */
	inode->i_blocks = size >> 9;
}

/**
 * ubifs_zn_dirty - check if znode is dirty.
 * @znode: znode to check
 *
 * This helper function returns %1 if @znode is dirty and %0 otherwise.
 */
static inline int ubifs_zn_dirty(const struct ubifs_znode *znode)
{
	return !!test_bit(DIRTY_ZNODE, &znode->flags);
}

/**
 * ubifs_wake_up_bgt - wake up background thread.
 * @c: UBIFS file-system description object
 */
static inline void ubifs_wake_up_bgt(struct ubifs_info *c)
{
	if (c->bgt && !c->need_bgt) {
		c->need_bgt = 1;
		wake_up_process(c->bgt);
	}
}

/**
 * ubifs_tnc_find_child - find next child in znode.
 * @znode: znode to search at
 * @start: the zbranch index to start at
 *
 * This helper function looks for znode child starting at index @start. Returns
 * the child or %NULL if no children were found.
 */
static inline struct ubifs_znode *
ubifs_tnc_find_child(struct ubifs_znode *znode, int start)
{
	while (start < znode->child_cnt) {
		if (znode->zbranch[start].znode)
			return znode->zbranch[start].znode;
		start += 1;
	}

	return NULL;
}

/**
 * ubifs_inode - get UBIFS inode information by VFS 'struct inode' object.
 * @inode: the VFS 'struct inode' pointer
 */
static inline struct ubifs_inode *ubifs_inode(const struct inode *inode)
{
	return container_of(inode, struct ubifs_inode, vfs_inode);
}

/**
 * ubifs_ro_mode - switch UBIFS to read read-only mode.
 * @c: UBIFS file-system description object
 */
static inline void ubifs_ro_mode(struct ubifs_info *c)
{
	if (!c->ro_media) {
		c->ro_media = 1;
		ubifs_warn("switched to read-only mode");
	}
}

/**
 * ubifs_compr_present - check if compressor was compiled in.
 * @compr_type: compressor type to check
 *
 * This function returns %1 of compressor of type @compr_type is present, and
 * %0 if not.
 */
static inline int ubifs_compr_present(int compr_type)
{
	ubifs_assert(compr_type >= 0 && compr_type < UBIFS_COMPR_TYPES_CNT);
	return !!ubifs_compressors[compr_type]->capi_name;
}

/**
 * ubifs_compr_name - get compressor name string by its type.
 * @compr_type: compressor type
 *
 * This function returns compressor type string.
 */
static inline const char *ubifs_compr_name(int compr_type)
{
	ubifs_assert(compr_type >= 0 && compr_type < UBIFS_COMPR_TYPES_CNT);
	return ubifs_compressors[compr_type]->name;
}

/**
 * ubifs_wbuf_sync - synchronize write-buffer.
 *
 * This is the same as as 'ubifs_wbuf_sync_nolock()' but it does not assume
 * that the write-buffer is already locked.
 */
static inline int ubifs_wbuf_sync(struct ubifs_info *c, struct ubifs_wbuf *wbuf)
{
	int err;

	mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
	err = ubifs_wbuf_sync_nolock(c, wbuf);
	mutex_unlock(&wbuf->io_mutex);
	return err;
}

/**
 * ubifs_encode_dev - encode device node IDs.
 * @dev: UBIFS device node information
 * @rdev: device IDs to encode
 *
 * This is a helper function which encodes major/minor numbers of a device node
 * into UBIFS device node description. We use standard Linux "new" and "huge"
 * encodings.
 */
static inline int ubifs_encode_dev(union ubifs_dev_desc *dev, dev_t rdev)
{
	if (new_valid_dev(rdev)) {
		dev->new = cpu_to_le32(new_encode_dev(rdev));
		return sizeof(dev->new);
	} else {
		dev->huge = cpu_to_le64(huge_encode_dev(rdev));
		return sizeof(dev->huge);
	}
}

/**
 * ubifs_add_dirt - add dirty space to LEB properties.
 * @c: the UBIFS file-system description object
 * @lnum: LEB to add dirty space for
 * @dirty: dirty space to add
 *
 * This is a helper function which increased amount of dirty LEB space. Returns
 * zero in case of success and a negative error code in case of failure.
 */
static inline int ubifs_add_dirt(struct ubifs_info *c, int lnum, int dirty)
{
	return ubifs_update_one_lp(c, lnum, -1, dirty, 0, 0);
}

/**
 * ubifs_return_leb - return LEB to lprops.
 * @c: the UBIFS file-system description object
 * @lnum: LEB to return
 *
 * This helper function cleans the "taken" flag of a logical eraseblock in the
 * lprops. Returns zero in case of success and a negative error code in case of
 * failure.
 */
static inline int ubifs_return_leb(struct ubifs_info *c, int lnum)
{
	return ubifs_change_one_lp(c, lnum, -1, -1, 0, LPROPS_TAKEN, 0);
}

#endif /* __UBIFS_MISC_H__ */
