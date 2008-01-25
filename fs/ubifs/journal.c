/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006, 2007 Nokia Corporation.
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
 * This file implements UBIFS journal.
 *
 * The journal consists of 2 parts - the log and bud LEBs. The log has fixed
 * length and position, while the bud logical eraseblock is any LEB in the main
 * area. Buds contain file system data - data nodes, inode nodes, etc. The log
 * contains only references to buds and some other stuff like commit
 * start/commit end nodes. The idea is that when we commit the journal, we do
 * not copy the data, the buds just become indexed. Since after the commit the
 * nodes in bud eraseblocks become leaf nodes of the file system index tree, we
 * use term "bud". Analogy is obvious, bud eraseblocks contain nodes which will
 * become leafs in the future.
 *
 * The journal is multi-headed because we want to write data to the journal as
 * optimally as possible. It is nice to have nodes belonging to the same inode
 * in one LEB, so we may write data owned by different inodes to different
 * journal heads.
 *
 * For recovery reasons, the base head contains all inode nodes, all directory
 * entry nodes and all truncate nodes.  This means that the other heads contain
 * only data nodes.
 *
 * Obviously, bud LEBs may be half-indexed. For example, if there is a LEB in
 * the main area with a lot of dirt, and UBIFS cleans it up using in-place
 * garbage collection, then the journal may use this half-free LEB as a bud
 * LEB.
 *
 * The journal size has to be limited, because the larger is the journal, the
 * longer it takes to mount UBIFS (scanning the journal) and the more memory it
 * takes (indexing in the TNC).
 */

#include "ubifs-priv.h"

/**
 * reserve_space - reserve space in the journal.
 * @c: UBIFS file-system description object
 * @jhead: journal head number
 * @len: node length
 *
 * This function reserves space in journal head @head. If the reservation
 * succeeded, the journal head stays locked and later has to be unlocked using
 * 'release_head()'. 'write_node()' and 'write_head()' functions also unlock
 * it. Returns zero in case of success, %-EAGAIN if commit has to be done, and
 * other negative error codes in case of other failures.
 */
static int reserve_space(struct ubifs_info *c, int jhead, int len)
{
	int err = 0, err1, retries = 0, avail, lnum, offs, free, squeeze;
	struct ubifs_wbuf *wbuf = &c->jheads[jhead].wbuf;

	/*
	 * Typically, the base head has smaller nodes written to it, so it is
	 * better to try to allocate space at the ends of eraseblocks. This is
	 * what the squeeze parameter does.
	 */
	squeeze = (jhead == BASEHD);
again:
	mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
	avail = c->leb_size - wbuf->offs - wbuf->used;

	if (wbuf->lnum != -1 && avail >= len)
		return 0;

	/*
	 * Write buffer wasn't seek'ed or there is no enough space - look for an
	 * LEB with some empty space.
	 */
	lnum = ubifs_find_free_space(c, len, &free, squeeze);
	if (lnum >= 0) {
		/* Found an LEB, add it to the journal head */
		offs = c->leb_size - free;
		err = ubifs_add_bud_to_log(c, jhead, lnum, offs);
		if (err)
			goto out_return;
		/* A new bud was successfully allocated and added to the log */
		goto out;
	}

	err = lnum;
	if (err != -ENOSPC)
		goto out_unlock;

	/*
	 * No free space, we have to run garbage collector to make
	 * some. But the write-buffer mutex has to be unlocked because
	 * GC have to sync write buffers, which may lead a deadlock.
	 */
	dbg_jrn("no free space  jhead %d, run GC", jhead);
	mutex_unlock(&wbuf->io_mutex);

	lnum = ubifs_garbage_collect(c, 0);
	if (lnum < 0) {
		err = lnum;
		if (err != -ENOSPC)
			return err;

		/*
		 * GC could not make a free LEB. But someone else may
		 * have allocated new bud for this journal head,
		 * because we dropped the 'io_mutex', so try once
		 * again.
		 */
		dbg_jrn("GC couldn't make a free LEB for jhead %d", jhead);
		if (retries++ < 2) {
			dbg_jrn("retry (%d)", retries);
			goto again;
		}

		dbg_jrn("return -ENOSPC");
		return err;
	}

	mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
	dbg_jrn("got LEB %d for jhead %d", lnum, jhead);
	avail = c->leb_size - wbuf->offs - wbuf->used;

	if (wbuf->lnum != -1 && avail >= len) {
		/*
		 * Someone else has switched the journal head and we have
		 * enough space now. This happens when more then one process is
		 * trying to write to the same journal head at the same time.
		 */
		dbg_jrn("return LEB %d back, already have LEB %d:%d",
			lnum, wbuf->lnum, wbuf->offs + wbuf->used);
		err = ubifs_return_leb(c, lnum);
		if (err)
			goto out_unlock;
		return 0;
	}

	err = ubifs_add_bud_to_log(c, jhead, lnum, 0);
	if (err)
		goto out_return;
	offs = 0;

out:
	err = ubifs_wbuf_seek_nolock(c, wbuf, lnum, offs, UBI_SHORTTERM);
	if (err)
		goto out_unlock;

	return 0;

out_unlock:
	mutex_unlock(&wbuf->io_mutex);
	return err;

out_return:
	/* An error occurred and the LEB has to be returned to lprops */
	ubifs_assert(err < 0);
	err1 = ubifs_return_leb(c, lnum);
	if (err1 && err == -EAGAIN)
		/*
		 * Return original error code 'err' only if it is not
		 * '-EAGAIN', which is not really an error. Otherwise, return
		 * the error code of 'ubifs_return_leb()'.
		 */
		err = err1;
	mutex_unlock(&wbuf->io_mutex);
	return err;
}

/**
 * write_node - write node to a journal head.
 * @c: UBIFS file-system description object
 * @jhead: journal head
 * @node: node to write
 * @len: node length
 * @lnum: LEB number written is returned here
 * @offs: offset written is returned here
 * @ino: inode number of the node
 *
 * This function writes a node to reserved space of journal head @jhead.
 * Returns zero in case of success and a negative error code in case of
 * failure.
 */
static int write_node(struct ubifs_info *c, int jhead, void *node, int len,
		      int *lnum, int *offs, ino_t ino)
{
	int err;

	ubifs_assert(jhead != GCHD);

	*lnum = c->jheads[jhead].wbuf.lnum;
	*offs = c->jheads[jhead].wbuf.offs + c->jheads[jhead].wbuf.used;

	dbg_jrn("jhead %d, LEB %d:%d, len %d", jhead, *lnum, *offs, len);
	ubifs_prepare_node(c, node, len, 0);

	err = ubifs_wbuf_write_nolock(c, &c->jheads[jhead].wbuf, node, len);
	if (!err)
		ubifs_wbuf_add_ino_nolock(&c->jheads[jhead].wbuf, ino);
	return err;
}

/**
 * write_head - write data to a journal head.
 * @c: UBIFS file-system description object
 * @jhead: journal head
 * @buf: buffer to write
 * @len: length to write
 * @lnum: LEB number written is returned here
 * @offs: offset written is returned here
 * @ino: inode number of the first node in @buf
 * @ino2: inode number of the second node in @buf
 *
 * This function is the same as 'write_node()' but it does not assume the
 * buffer it is writing is a node, so it does not prepare it (which means
 * initializing common header and calculating CRC).
 */
static int write_head(struct ubifs_info *c, int jhead, void *buf, int len,
		      int *lnum, int *offs, ino_t ino, ino_t ino2)
{
	int err;

	ubifs_assert(jhead != GCHD);

	*lnum = c->jheads[jhead].wbuf.lnum;
	*offs = c->jheads[jhead].wbuf.offs + c->jheads[jhead].wbuf.used;
	dbg_jrn("jhead %d, LEB %d:%d, len %d", jhead, *lnum, *offs, len);

	err = ubifs_wbuf_write_nolock(c, &c->jheads[jhead].wbuf, buf, len);
	if (!err) {
		ubifs_wbuf_add_ino_nolock(&c->jheads[jhead].wbuf, ino);
		if (ino2)
			ubifs_wbuf_add_ino_nolock(&c->jheads[jhead].wbuf, ino2);
	}
	return err;
}

/**
 * make_reservation - reserve journal space.
 * @c: UBIFS file-system description object
 * @jhead: journal head
 * @len: how many bytes to reserve
 *
 * This function makes space reservation in journal head @jhead. The function
 * takes the commit lock and locks the journal head, and the caller has to
 * unlock the head and finish the reservation with 'finish_reservation()'.
 * Returns zero in case of success and a negative error code in case of
 * failure.
 *
 * Note, the journal head may be unlocked as soon as the data is written, while
 * the commit lock has to be released after the data has been added to the
 * TNC.
 */
static int make_reservation(struct ubifs_info *c, int jhead, int len)
{
	int err, cmt_retries = 0, nospc_retries = 0;

	ubifs_assert(len <= c->dark_wm);

again:
	down_read(&c->commit_sem);
	err = reserve_space(c, jhead, len);
	if (!err)
		return 0;
	up_read(&c->commit_sem);

	if (err == -ENOSPC) {
		/*
		 * GC could not make any progress. We should try to commit
		 * once because it could make some dirty space and GC would
		 * make progress, so make the error -EAGAIN so that the below
		 * will commit and re-try.
		 */
		if (nospc_retries++ < 2) {
			dbg_jrn("no space, retry");
			err = -EAGAIN;
		}

		/*
		 * This means that the budgeting is incorrect. We always have
		 * to be able to write to the media, because all operations are
		 * budgeted. Deletions are not budgeted, though, but we reserve
		 * an extra LEB for them.
		 */
	}

	if (err != -EAGAIN)
		goto out;

	/*
	 * -EAGAIN means that the journal is full or too large, or the above
	 * code wants to do one commit. Do this and re-try.
	 */
	if (cmt_retries > 128) {
		/*
		 * This should not happen unless the journal size limitations
		 * are too tough.
		 */
		ubifs_err("stuck in space allocation");
		err = -ENOSPC;
		goto out;
	} else if (cmt_retries > 32)
		ubifs_warn("too many space allocation re-tries (%d)",
			   cmt_retries);

	dbg_jrn("-EAGAIN, commit and retry (retried %d times)",
		cmt_retries);
	cmt_retries += 1;

	err = ubifs_run_commit(c);
	if (err)
		return err;
	goto again;

out:
	ubifs_err("cannot reserve %d bytes in jhead %d, error %d",
		  len, jhead, err);
	if (err == -ENOSPC) {
		/* This are some budgeting problems, print useful information */
		down_write(&c->commit_sem);
		spin_lock(&c->space_lock);
		dbg_dump_stack();
		dbg_dump_budg(c);
		spin_unlock(&c->space_lock);
		dbg_dump_lprops(c);
		dbg_check_lprops(c);
		up_write(&c->commit_sem);
	}

	return err;
}

/**
 * release_head - release a journal head.
 * @c: UBIFS file-system description object
 * @jhead: journal head
 *
 * This function releases journal head @jhead which was locked by
 * the 'make_reservation()' function. It has to be called after each successful
 * 'make_reservation()' infocation.
 */
static inline void release_head(struct ubifs_info *c, int jhead)
{
	mutex_unlock(&c->jheads[jhead].wbuf.io_mutex);
}

/**
 * finish_reservation - finish a reservation.
 * @c: UBIFS file-system description object
 *
 * This function finishes journal space reservation. It must be called after
 * 'make_reservation()'.
 */
static void finish_reservation(struct ubifs_info *c)
{
	up_read(&c->commit_sem);
}

/**
 * get_dent_type - translate VFS inode mode to UBIFS dentry type.
 * @mode: inode mode
 */
static int get_dent_type(int mode)
{
	switch (mode & S_IFMT) {
	case S_IFREG:
		return UBIFS_ITYPE_REG;
	case S_IFDIR:
		return UBIFS_ITYPE_DIR;
	case S_IFLNK:
		return UBIFS_ITYPE_LNK;
	case S_IFBLK:
		return UBIFS_ITYPE_BLK;
	case S_IFCHR:
		return UBIFS_ITYPE_CHR;
	case S_IFIFO:
		return UBIFS_ITYPE_FIFO;
	case S_IFSOCK:
		return UBIFS_ITYPE_SOCK;
	default:
		BUG();
	}
	return 0;
}

/**
 * pack_inode - pack an inode node.
 * @c: UBIFS file-system description object
 * @ino: buffer in which to pack inode node
 * @inode: inode to pack
 * @last: indicates the last node of the group
 */
static void pack_inode(struct ubifs_info *c, struct ubifs_ino_node *ino,
		       struct inode *inode, int last)
{
	struct ubifs_inode *ui = ubifs_inode(inode);

	ino->ch.node_type = UBIFS_INO_NODE;
	ino_key_init_flash(c, &ino->key, inode->i_ino);
	ino->size  = cpu_to_be64(i_size_read(inode));
	ino->nlink = cpu_to_be32(inode->i_nlink);
	ino->atime = cpu_to_be32(inode->i_atime.tv_sec);
	ino->ctime = cpu_to_be32(inode->i_ctime.tv_sec);
	ino->mtime = cpu_to_be32(inode->i_mtime.tv_sec);
	ino->uid   = cpu_to_be32(inode->i_uid);
	ino->gid   = cpu_to_be32(inode->i_gid);
	ino->mode  = cpu_to_be32(inode->i_mode);
	ino->flags = cpu_to_be32(ui->flags);
	ino->compr_type = cpu_to_be16(ui->compr_type);

	if ((inode->i_mode & S_IFMT) == S_IFCHR ||
	    (inode->i_mode & S_IFMT) == S_IFBLK) {
		union ubifs_dev_desc *dev = ui->data;

		ui->data_len = ubifs_encode_dev(dev, inode->i_rdev);
	}

	ino->data_len = cpu_to_be32(ui->data_len);
	if (ui->data_len)
		memcpy(ino->data, ui->data, ui->data_len);

	ubifs_prep_grp_node(c, ino, UBIFS_INO_NODE_SZ + ui->data_len, last);
}

/**
 * ubifs_jrn_update - update inode.
 * @c: UBIFS file-system description object
 * @dir: parent inode
 * @dentry: directory entry
 * @inode: inode
 * @del: indicates a directory entry deletion i.e unlink or rmdir
 *
 * This function updates an inode by writing a directory entry, the inode and
 * the parent directory inode to the journal.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_jrn_update(struct ubifs_info *c, struct inode *dir,
		     struct dentry *dentry, struct inode *inode, int del)
{
	int err, dlen, ilen, len, lnum, ino_offs, dent_offs, aligned_dlen;
	int aligned_ilen, plen = UBIFS_INO_NODE_SZ;
	struct ubifs_dent_node *dent;
	struct ubifs_ino_node *ino;
	union ubifs_key dent_key, ino_key;

	dbg_jrn("ino %lu, dent '%.*s', data len %d in dir ino %lu",
		inode->i_ino, dentry->d_name.len, dentry->d_name.name,
		ubifs_inode(inode)->data_len, dir->i_ino);

	dlen = UBIFS_DENT_NODE_SZ + dentry->d_name.len + 1;
	ilen = UBIFS_INO_NODE_SZ + ubifs_inode(inode)->data_len;

	ubifs_assert(ubifs_inode(dir)->data_len == 0);

	aligned_dlen = ALIGN(dlen, 8);
	aligned_ilen = ALIGN(ilen, 8);

	len = aligned_dlen + aligned_ilen + plen;

	dent = kmalloc(len, GFP_KERNEL);
	if (!dent)
		return -ENOMEM;

	dent->ch.node_type = UBIFS_DENT_NODE;
	dent_key_init(c, &dent_key, dir->i_ino, &dentry->d_name);
	key_write(c, &dent_key, dent->key);
	if (del)
		dent->inum = 0;
	else
		dent->inum = cpu_to_be64(inode->i_ino);
	dent->padding = 0;
	dent->type = get_dent_type(inode->i_mode);
	dent->nlen = cpu_to_be16(dentry->d_name.len);
	memcpy(dent->name, dentry->d_name.name, dentry->d_name.len);
	dent->name[dentry->d_name.len] = '\0';
	ubifs_prep_grp_node(c, dent, dlen, 0);

	ino = (void *)dent + aligned_dlen;
	pack_inode(c, ino, inode, 0);

	ino = (void *)ino + aligned_ilen;
	pack_inode(c, ino, dir, 1);

	err = make_reservation(c, BASEHD, len);
	if (err)
		goto out_free;

	if (del && inode->i_nlink == 0) {
		err = ubifs_add_orphan(c, inode->i_ino);
		if (err) {
			release_head(c, BASEHD);
			goto out_finish;
		}
	}

	err = write_head(c, BASEHD, dent, len, &lnum, &dent_offs, inode->i_ino,
			 dir->i_ino);
	release_head(c, BASEHD);
	if (err)
		goto out_orph;

	kfree(dent);

	if (del) {
		err = ubifs_tnc_remove_nm(c, &dent_key, &dentry->d_name);
		if (err)
			goto out_ro;
		err = ubifs_add_dirt(c, lnum, dlen);
	} else
		err = ubifs_tnc_add_nm(c, &dent_key, lnum, dent_offs, dlen,
				       &dentry->d_name);
	if (err)
		goto out_ro;

	ino_key_init(c, &ino_key, inode->i_ino);
	ino_offs = dent_offs + aligned_dlen;
	err = ubifs_tnc_add(c, &ino_key, lnum, ino_offs, ilen);
	if (err)
		goto out_ro;

	ino_key_init(c, &ino_key, dir->i_ino);
	ino_offs += aligned_ilen;
	err = ubifs_tnc_add(c, &ino_key, lnum, ino_offs, plen);
	if (err)
		goto out_ro;

	finish_reservation(c);
	return 0;

out_orph:
	if (del && inode->i_nlink == 0)
		ubifs_delete_orphan(c, inode->i_ino);
out_finish:
	finish_reservation(c);
out_free:
	kfree(dent);
	return err;

out_ro:
	ubifs_ro_mode(c);
	if (del && inode->i_nlink == 0)
		ubifs_delete_orphan(c, inode->i_ino);
	finish_reservation(c);
	return err;
}

/**
 * ubifs_jrn_write_data - write a data node to the journal.
 * @c: UBIFS file-system description object
 * @inode: inode the data node belongs to
 * @key: node key
 * @buf: buffer to write
 * @len: data length (must not exceed %UBIFS_BLOCK_SIZE)
 *
 * This function writes a data node to the journal. Returns %0 if the data node
 * was successfully written, and a negative error code in case of failure.
 */
int ubifs_jrn_write_data(struct ubifs_info *c, const struct inode *inode,
			 const union ubifs_key *key, const void *buf, int len)
{
	int err, lnum, offs, compr_type, out_len;
	int dlen = UBIFS_DATA_NODE_SZ + len * WORST_COMPR_FACTOR;
	const struct ubifs_inode *ui = ubifs_inode(inode);
	struct ubifs_data_node *data;

	dbg_jrn_key(c, key, "ino %lu, blk %u, len %d, key ",
		    key_ino(c, key), key_block(c, key), len);
	ubifs_assert(len <= UBIFS_BLOCK_SIZE);

	data = kmalloc(dlen, GFP_NOFS);
	if (!data)
		return -ENOMEM;

	data->ch.node_type = UBIFS_DATA_NODE;
	key_write(c, key, &data->key);
	data->size = cpu_to_be32(len);

	if (!(ui->flags && UBIFS_COMPR_FL))
		/* Compression is disabled for this inode */
		compr_type = UBIFS_COMPR_NONE;
	else
		compr_type = ui->compr_type;

	out_len = dlen - UBIFS_DATA_NODE_SZ;
	ubifs_compress(buf, len, &data->data, &out_len, &compr_type);
	ubifs_assert(out_len <= UBIFS_BLOCK_SIZE);

	dlen = UBIFS_DATA_NODE_SZ + out_len;
	data->compr_type = cpu_to_be16(compr_type);

	err = make_reservation(c, DATAHD, dlen);
	if (err)
		goto out_free;

	err = write_node(c, DATAHD, data, dlen, &lnum, &offs, key_ino(c, key));
	release_head(c, DATAHD);
	if (err)
		goto out_finish;

	err = ubifs_tnc_add(c, key, lnum, offs, dlen);

out_finish:
	finish_reservation(c);
out_free:
	kfree(data);
	return err;
}

/**
 * ubifs_jrn_write_inode - flush inode to the journal.
 * @c: UBIFS file-system description object
 * @inode: inode to flush
 * @deletion: inode has been deleted
 *
 * This function writes inode @inode to the journal (to the base head). Returns
 * zero in case of success and a negative error code in case of failure.
 */
int ubifs_jrn_write_inode(struct ubifs_info *c, struct inode *inode,
			  int deletion)
{
	int err, len, lnum, offs;
	struct ubifs_ino_node *ino;

	dbg_jrn("ino %lu%s", inode->i_ino, deletion ? " (deletion)" : "");
	if (deletion)
		ubifs_assert(inode->i_nlink == 0);

	len = UBIFS_INO_NODE_SZ + ubifs_inode(inode)->data_len;
	ino = kmalloc(len, GFP_NOFS);
	if (!ino)
		return -ENOMEM;
	pack_inode(c, ino, inode, 1);

	err = make_reservation(c, BASEHD, len);
	if (err)
		goto out_free;

	err = write_head(c, BASEHD, ino, len, &lnum, &offs, inode->i_ino, 0);
	release_head(c, BASEHD);
	if (err)
		goto out_finish;

	if (deletion) {
		union ubifs_key min_key, max_key;

		min_inum_key(c, &min_key, inode->i_ino);
		max_inum_key(c, &max_key, inode->i_ino);
		err = ubifs_tnc_remove_range(c, &min_key, &max_key);
		if (err)
			goto out_finish;
		ubifs_delete_orphan(c, inode->i_ino);
		err = ubifs_add_dirt(c, lnum, len);
	} else {
		union ubifs_key key;

		ino_key_init(c, &key, inode->i_ino);
		err = ubifs_tnc_add(c, &key, lnum, offs, len);
	}

out_finish:
	finish_reservation(c);
out_free:
	kfree(ino);
	return err;
}

/**
 * ubifs_jrn_rename - rename a directory entry.
 * @c: UBIFS file-system description object
 * @old_dir: parent inode of directory entry to rename
 * @old_dentry: directory entry to rename
 * @new_dir: parent inode of directory entry to rename
 * @new_dentry: new directory entry (or directory entry to replace)
 *
 * Returns zero in case of success and a negative error code in case of failure.
 */
int ubifs_jrn_rename(struct ubifs_info *c, struct inode *old_dir,
		     struct dentry *old_dentry, struct inode *new_dir,
		     struct dentry *new_dentry)
{
	struct inode *old_inode = old_dentry->d_inode;
	struct inode *new_inode = new_dentry->d_inode;
	int err, dlen1, dlen2, ilen, lnum, offs, len;
	int aligned_dlen1, aligned_dlen2, plen = UBIFS_INO_NODE_SZ;
	struct ubifs_dent_node *dent, *dent2;
	void *p;
	union ubifs_key key;

	dbg_jrn("dent '%.*s' in dir ino %lu to dent '%.*s' in dir ino %lu",
		old_dentry->d_name.len, old_dentry->d_name.name,
		old_dir->i_ino, new_dentry->d_name.len,
		new_dentry->d_name.name, new_dir->i_ino);

	ubifs_assert(ubifs_inode(old_dir)->data_len == 0);
	ubifs_assert(ubifs_inode(new_dir)->data_len == 0);

	dlen1 = UBIFS_DENT_NODE_SZ + new_dentry->d_name.len + 1;
	dlen2 = UBIFS_DENT_NODE_SZ + old_dentry->d_name.len + 1;
	if (new_inode)
		ilen = UBIFS_INO_NODE_SZ + ubifs_inode(new_inode)->data_len;
	else
		ilen = 0;

	aligned_dlen1 = ALIGN(dlen1, 8);
	aligned_dlen2 = ALIGN(dlen2, 8);

	len = aligned_dlen1 + aligned_dlen2 + ALIGN(ilen, 8) + ALIGN(plen, 8);
	if (old_dir != new_dir)
		len += plen;

	dent = kmalloc(len, GFP_NOFS);
	if (!dent)
		return -ENOMEM;

	/* Make new dent */
	dent->ch.node_type = UBIFS_DENT_NODE;
	dent_key_init_flash(c, &dent->key, new_dir->i_ino, &new_dentry->d_name);
	dent->inum = cpu_to_be64(old_inode->i_ino);
	dent->padding = 0;
	dent->type = get_dent_type(old_inode->i_mode);
	dent->nlen = cpu_to_be16(new_dentry->d_name.len);
	memcpy(dent->name, new_dentry->d_name.name, new_dentry->d_name.len);
	dent->name[new_dentry->d_name.len] = '\0';
	ubifs_prep_grp_node(c, dent, dlen1, 0);

	dent2 = (void *)dent + aligned_dlen1;

	/* Make deletion dent */
	dent2->ch.node_type = UBIFS_DENT_NODE;
	dent_key_init_flash(c, &dent2->key, old_dir->i_ino, &old_dentry->d_name);
	dent2->inum = cpu_to_be64(0);
	dent2->padding = 0;
	dent2->type = DT_UNKNOWN;
	dent2->nlen = cpu_to_be16(old_dentry->d_name.len);
	memcpy(dent2->name, old_dentry->d_name.name, old_dentry->d_name.len);
	dent2->name[old_dentry->d_name.len] = '\0';
	ubifs_prep_grp_node(c, dent2, dlen2, 0);

	p = (void *)dent2 + aligned_dlen2;
	if (new_inode) {
		pack_inode(c, p, new_inode, 0);
		p += ALIGN(ilen, 8);
	}

	if (old_dir == new_dir)
		pack_inode(c, p, old_dir, 1);
	else {
		pack_inode(c, p, old_dir, 0);
		p += ALIGN(plen, 8);
		pack_inode(c, p, new_dir, 1);
	}

	err = make_reservation(c, BASEHD, len);
	if (err)
		goto out_free;

	if (new_inode && new_inode->i_nlink == 0) {
		err = ubifs_add_orphan(c, new_inode->i_ino);
		if (err) {
			release_head(c, BASEHD);
			goto out_finish;
		}
	}

	err = write_head(c, BASEHD, dent, len, &lnum, &offs,
	                 new_dir->i_ino, old_dir->i_ino);
	release_head(c, BASEHD);
	if (err) {
		if (new_inode && new_inode->i_nlink == 0)
			ubifs_delete_orphan(c, new_inode->i_ino);
		goto out_finish;
	}
	if (new_inode)
		ubifs_wbuf_add_ino_nolock(&c->jheads[BASEHD].wbuf,
					  new_inode->i_ino);

	dent_key_init(c, &key, new_dir->i_ino, &new_dentry->d_name);
	err = ubifs_tnc_add_nm(c, &key, lnum, offs, dlen1, &new_dentry->d_name);
	if (err)
		goto out_ro;

	err = ubifs_add_dirt(c, lnum, dlen2);
	if (err)
		goto out_ro;

	dent_key_init(c, &key, old_dir->i_ino, &old_dentry->d_name);
	err = ubifs_tnc_remove_nm(c, &key, &old_dentry->d_name);
	if (err)
		goto out_ro;

	offs += aligned_dlen1 + aligned_dlen2;
	if (new_inode) {
		ino_key_init(c, &key, new_inode->i_ino);
		err = ubifs_tnc_add(c, &key, lnum, offs, ilen);
		if (err)
			goto out_ro;
		offs += ALIGN(ilen, 8);
	}

	ino_key_init(c, &key, old_dir->i_ino);
	err = ubifs_tnc_add(c, &key, lnum, offs, plen);
	if (err)
		goto out_ro;

	if (old_dir != new_dir) {
		offs += ALIGN(plen, 8);
		ino_key_init(c, &key, new_dir->i_ino);
		err = ubifs_tnc_add(c, &key, lnum, offs, plen);
		if (err)
			goto out_ro;
	}

out_finish:
	finish_reservation(c);
out_free:
	kfree(dent);
	return err;

out_ro:
	ubifs_ro_mode(c);
	if (new_inode && new_inode->i_nlink == 0)
		ubifs_delete_orphan(c, new_inode->i_ino);
	finish_reservation(c);
	kfree(dent);
	return err;
}

/**
 * recomp_data_node - recompress a truncated data node.
 * @dn: data node to recompress
 * @new_len: new length
 *
 * This function is used when an inode is truncated and the last data node of
 * the inode has to be re-compressed and re-written.
 */
static int recomp_data_node(struct ubifs_data_node *dn, int *new_len)
{
	void *buf;
	int err, len, compr_type, out_len;

	out_len = be32_to_cpu(dn->size);
	buf = kmalloc(out_len * WORST_COMPR_FACTOR, GFP_NOFS);
	if (!buf)
		return -ENOMEM;

	len = be32_to_cpu(dn->ch.len) - UBIFS_DATA_NODE_SZ;
	compr_type = be16_to_cpu(dn->compr_type);
	err = ubifs_decompress(&dn->data, len, buf, &out_len, compr_type);
	if (err)
		goto out;

	ubifs_compress(buf, *new_len, &dn->data, &out_len, &compr_type);
	ubifs_assert(out_len <= UBIFS_BLOCK_SIZE);
	dn->compr_type = cpu_to_be16(compr_type);
	dn->size = cpu_to_be32(*new_len);
	*new_len = UBIFS_DATA_NODE_SZ + out_len;

out:
	kfree(buf);
	return err;
}

/**
 * ubifs_jrn_truncate - update the journal for a truncation.
 * @c: UBIFS file-system description object
 * @ino: inode number of inode being truncated
 * @old_size: old size
 * @new_size: new size
 *
 * When the size of a file decreases due to truncation, a truncation node is
 * written, the journal tree is updated, and the last data block is re-written
 * if it has been affected.
 *
 * This function returns %0 in the case of success, and a negative error code in
 * case of failure.
 */
int ubifs_jrn_truncate(struct ubifs_info *c, ino_t ino,
		       loff_t old_size, loff_t new_size)
{
	union ubifs_key key, to_key;
	struct ubifs_trun_node *trun;
	struct ubifs_data_node *dn;
	int err, dlen, len, lnum, offs, bit, sz;
	unsigned int blk;

	dbg_jrn("ino %lu, size %lld -> %lld", ino, old_size, new_size);

	sz = UBIFS_TRUN_NODE_SZ + UBIFS_MAX_DATA_NODE_SZ * WORST_COMPR_FACTOR;
	trun = kmalloc(sz, GFP_NOFS);
	if (!trun)
		return -ENOMEM;

	trun->ch.node_type = UBIFS_TRUN_NODE;
	trun_key_init_flash(c, &trun->key, ino);
	trun->old_size = cpu_to_be64(old_size);
	trun->new_size = cpu_to_be64(new_size);
	ubifs_prepare_node(c, trun, UBIFS_TRUN_NODE_SZ, 0);

	dlen = new_size & (UBIFS_BLOCK_SIZE - 1);

	if (dlen) {
		/* Get last data block so it can be truncated */
		dn = (void *)trun + ALIGN(UBIFS_TRUN_NODE_SZ, 8);
		blk = new_size / UBIFS_BLOCK_SIZE;
		data_key_init(c, &key, ino, blk);
		dbg_jrn_key(c, &key, "key ");
		err = ubifs_tnc_lookup(c, &key, dn);
		if (err == -ENOENT)
			dlen = 0; /* Not found (so it is a hole) */
		else if (err)
			goto out_free;
		else {
			if (be32_to_cpu(dn->size) <= dlen)
				dlen = 0; /* Nothing to do */
			else {
				int compr_type = be16_to_cpu(dn->compr_type);

				if (compr_type != UBIFS_COMPR_NONE) {
					err = recomp_data_node(dn, &dlen);
					if (err)
						goto out_free;
				} else {
					dn->size = cpu_to_be32(dlen);
					dlen += UBIFS_DATA_NODE_SZ;
				}
				ubifs_prepare_node(c, dn, dlen, 0);
			}
		}
	}

	if (dlen)
		len = ALIGN(UBIFS_TRUN_NODE_SZ, 8) + dlen;
	else
		len = UBIFS_TRUN_NODE_SZ;

	err = make_reservation(c, BASEHD, len);
	if (err)
		goto out_free;

	err = write_head(c, BASEHD, trun, len, &lnum, &offs, ino, 0);
	release_head(c, BASEHD);
	if (err)
		goto out;

	if (dlen) {
		offs += ALIGN(UBIFS_TRUN_NODE_SZ, 8);
		err = ubifs_tnc_add(c, &key, lnum, offs, dlen);
		if (err)
			goto out;
	}

	err = ubifs_add_dirt(c, lnum, UBIFS_TRUN_NODE_SZ);
	if (err)
		goto out;

	bit = new_size & (UBIFS_BLOCK_SIZE - 1);

	blk = new_size / UBIFS_BLOCK_SIZE + (bit ? 1 : 0);
	data_key_init(c, &key, ino, blk);

	bit = old_size & (UBIFS_BLOCK_SIZE - 1);

	blk = old_size / UBIFS_BLOCK_SIZE - (bit ? 0: 1);
	data_key_init(c, &to_key, ino, blk);

	err = ubifs_tnc_remove_range(c, &key, &to_key);

out:
	finish_reservation(c);
out_free:
	kfree(trun);
	return err;
}
