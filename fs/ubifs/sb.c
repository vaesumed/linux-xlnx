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
 */

/*
 * This file implements UBIFS superblock. The superblock is stored at the first
 * LEB of the volume and is never changed by UBIFS. Only user-space tools may
 * change it. The superblock node mostly contains geometry information.
 */

#include <asm/div64.h>
#include "ubifs-priv.h"

/* TODO: Put things that affect the resizability of UBIFS into the super block */
/* TODO: Add a UBIFS version number to the super block and do not mount if it is greater than the software version number */

/*
 * Default journal size in logical eraseblocks as a percent of total
 * flash size.
 */
#define DEFAULT_JRN_PERCENT 5

/* Default maximum journal size in bytes */
#define DEFAULT_MAX_JRN (32*1024*1024)

/* Default number of LEBs for orphan information */
#ifdef CONFIG_UBIFS_FS_DEBUG
#define DEFAULT_ORPHAN_LEBS 2 /* 2 is better for testing */
#else
#define DEFAULT_ORPHAN_LEBS 1
#endif

/* Default number of journal heads */
#define DEFAULT_JHEADS_CNT 1

/* Default positions of different LEBs in the main area */
#define DEFAULT_IDX_LEB  0
#define DEFAULT_DATA_LEB 1
#define DEFAULT_GC_LEB   2

/* Default number of LEB numbers in LPT's save table */
#define DEFAULT_LSAVE_CNT 256

/**
 * create_default_filesystem - format empty UBI volume.
 * @c: UBIFS file-system description object
 *
 * This function creates default empty file-system. Returns zero in case of
 * success and a negative error code in case of failure.
 */
static int create_default_filesystem(struct ubifs_info *c)
{
	struct ubifs_sb_node *sup;
	struct ubifs_mst_node *mst;
	struct ubifs_idx_node *idx;
	struct ubifs_ino_node *ino;
	struct ubifs_cs_node *cs;
	int err, tmp, jrn_lebs, log_lebs, max_buds, main_lebs, main_first;
	int lpt_lebs, lpt_first, orph_lebs, big_lpt, ino_waste;
	long long tmp64;

	/*
	 * First of all, we have to calculate default file-system geometry -
	 * log size, journal size, etc.
	 */
	c->max_leb_cnt = c->leb_cnt;
	if (c->leb_cnt < 0x7FFFFFFF / DEFAULT_JRN_PERCENT)
		/* We can first multiply then divide and have no overflow */
		jrn_lebs = c->leb_cnt * DEFAULT_JRN_PERCENT / 100;
	else
		jrn_lebs = (c->leb_cnt / 100) * DEFAULT_JRN_PERCENT;

	if (jrn_lebs < UBIFS_MIN_JRN_LEBS)
		jrn_lebs = UBIFS_MIN_JRN_LEBS;
	if (jrn_lebs * c->leb_size > DEFAULT_MAX_JRN)
		jrn_lebs = DEFAULT_MAX_JRN / c->leb_size;

	/*
	 * The log should be large enough to fit reference nodes for all bud
	 * LEBs. Because buds do not have to start from the beginning of LEBs
	 * (half of the LEB may contain committed data), the log should
	 * generally be larger, make it twice as large.
	 */
	tmp = 2 * (c->ref_node_alsz * jrn_lebs) + c->leb_size - 1;
	log_lebs = tmp / c->leb_size;
	/* Plus one LEB reserved for commit */
	log_lebs += 1;
	/* And some extra space to allow writes while committing */
	log_lebs += 1;

	max_buds = jrn_lebs - log_lebs;
	if (max_buds < UBIFS_MIN_BUD_LEBS)
		max_buds = UBIFS_MIN_BUD_LEBS;

	/*
	 * Orphan nodes are stored in a separate area. One node can store a lot
	 * of orphan inode numbers, but when new orphan comes we just add a new
	 * orphan node. At some point the nodes are consolidated into one
	 * orphan node.
	 */
	orph_lebs = DEFAULT_ORPHAN_LEBS;

	main_lebs = c->leb_cnt - UBIFS_SB_LEBS - UBIFS_MST_LEBS - log_lebs;
	main_lebs -= orph_lebs;

	lpt_first = UBIFS_LOG_LNUM + log_lebs;
	c->lsave_cnt = DEFAULT_LSAVE_CNT;
	err = ubifs_create_dflt_lpt(c, &main_lebs, lpt_first, &lpt_lebs,
				    &big_lpt);
	if (err)
		return err;

	dbg_gen("LEB Properties Tree created (LEBs %d-%d)", lpt_first,
		lpt_first + lpt_lebs - 1);

	main_first = c->leb_cnt - main_lebs;

	/* Create default superblock */
	tmp = ALIGN(UBIFS_SB_NODE_SZ, c->min_io_size);
	sup = kzalloc(tmp, GFP_KERNEL);
	if (!sup)
		return -ENOMEM;

	tmp64 = (long long)max_buds * c->leb_size;
	sup->ch.node_type  = UBIFS_SB_NODE;
	sup->key_hash      = c->key_hash_type;
	sup->big_lpt       = big_lpt;
	sup->min_io_size   = cpu_to_be32(c->min_io_size);
	sup->leb_size      = cpu_to_be32(c->leb_size);
	sup->leb_cnt       = cpu_to_be32(c->leb_cnt);
	sup->max_leb_cnt   = cpu_to_be32(c->max_leb_cnt);
	sup->max_bud_bytes = cpu_to_be64(tmp64);
	sup->log_lebs      = cpu_to_be32(log_lebs);
	sup->lpt_lebs      = cpu_to_be32(lpt_lebs);
	sup->orph_lebs     = cpu_to_be32(orph_lebs);
	sup->jhead_cnt     = cpu_to_be32(DEFAULT_JHEADS_CNT);
	sup->fanout        = cpu_to_be32(c->fanout);
	sup->lsave_cnt     = cpu_to_be32(c->lsave_cnt);
	sup->default_compr = cpu_to_be16(c->default_compr);

	err = ubifs_write_node(c, sup, UBIFS_SB_NODE_SZ, 0, 0, UBI_LONGTERM);
	kfree(sup);
	if (err)
		return err;

	dbg_gen("default superblock created at LEB 0:0");

	/* Create default master node */
	mst = kzalloc(c->mst_node_alsz, GFP_KERNEL);
	if (!mst)
		return -ENOMEM;

	mst->ch.node_type = UBIFS_MST_NODE;
	mst->log_lnum     = cpu_to_be32(UBIFS_LOG_LNUM);
	mst->highest_inum = cpu_to_be64(UBIFS_FIRST_INO);
	mst->cmt_no       = cpu_to_be64(0);
	mst->root_lnum    = cpu_to_be32(main_first + DEFAULT_IDX_LEB);
	mst->root_offs    = cpu_to_be32(0);
	tmp = UBIFS_IDX_NODE_SZ + UBIFS_BRANCH_SZ;
	mst->root_len     = cpu_to_be32(tmp);
	mst->gc_lnum      = cpu_to_be32(main_first + DEFAULT_GC_LEB);
	mst->ihead_lnum   = cpu_to_be32(main_first + DEFAULT_IDX_LEB);
	mst->ihead_offs   = cpu_to_be32(ALIGN(tmp, c->max_align));
	mst->index_size   = cpu_to_be64(MIN_IDX_NODE_SZ);
	mst->lpt_lnum     = cpu_to_be32(c->lpt_lnum);
	mst->lpt_offs     = cpu_to_be32(c->lpt_offs);
	mst->nhead_lnum   = cpu_to_be32(c->nhead_lnum);
	mst->nhead_offs   = cpu_to_be32(c->nhead_offs);
	mst->ltab_lnum    = cpu_to_be32(c->ltab_lnum);
	mst->ltab_offs    = cpu_to_be32(c->ltab_offs);
	mst->lsave_lnum   = cpu_to_be32(c->lsave_lnum);
	mst->lsave_offs   = cpu_to_be32(c->lsave_offs);
	mst->lscan_lnum   = cpu_to_be32(main_first);
	mst->empty_lebs   = cpu_to_be32(main_lebs - 2);
	mst->idx_lebs     = cpu_to_be32(1);
	mst->leb_cnt      = cpu_to_be32(c->leb_cnt);

	/* Calculate lprops statistics */
	tmp64 = (long long)main_lebs * c->leb_size;
	tmp64 -= ALIGN(UBIFS_IDX_NODE_SZ + UBIFS_BRANCH_SZ, c->max_align);
	tmp64 -= ALIGN(UBIFS_INO_NODE_SZ, c->min_io_size);
	mst->total_free = cpu_to_be64(tmp64);

	tmp64 = ALIGN(UBIFS_IDX_NODE_SZ + UBIFS_BRANCH_SZ, c->max_align);
	ino_waste = ALIGN(UBIFS_INO_NODE_SZ, c->min_io_size) -
			  UBIFS_INO_NODE_SZ;
	tmp64 += ino_waste;
	tmp64 -= MIN_IDX_NODE_SZ;
	mst->total_dirty = cpu_to_be64(tmp64);

	/*  The indexing LEB does not contribute to dark space */
	tmp64 = (c->main_lebs - 1) * c->dark_wm;
	mst->total_dark = cpu_to_be64(tmp64);

	mst->total_used = cpu_to_be64(UBIFS_INO_NODE_SZ);

	err = ubifs_write_node(c, mst, UBIFS_MST_NODE_SZ, UBIFS_MST_LNUM, 0,
			       UBI_UNKNOWN);
	if (err) {
		kfree(mst);
		return err;
	}
	err = ubifs_write_node(c, mst, UBIFS_MST_NODE_SZ, UBIFS_MST_LNUM + 1, 0,
			       UBI_UNKNOWN);
	kfree(mst);
	if (err)
		return err;

	dbg_gen("default master node created at LEB %d:0", UBIFS_MST_LNUM);

	/* Create the root indexing node */
	tmp = UBIFS_IDX_NODE_SZ + UBIFS_BRANCH_SZ;
	idx = kzalloc(ALIGN(tmp, c->min_io_size), GFP_KERNEL);
	if (!idx)
		return -ENOMEM;

	c->key_fmt = UBIFS_SIMPLE_KEY_FMT;
	c->key_hash = key_r5_hash;

	idx->ch.node_type = UBIFS_IDX_NODE;
	idx->child_cnt = cpu_to_be16(1);
	ino_key_init_flash(c, &idx->branch[0].key, UBIFS_ROOT_INO);
	idx->branch[0].lnum = cpu_to_be32(main_first + DEFAULT_DATA_LEB);
	idx->branch[0].len = cpu_to_be32(UBIFS_INO_NODE_SZ);
	err = ubifs_write_node(c, idx, tmp, main_first + DEFAULT_IDX_LEB, 0,
			       UBI_UNKNOWN);
	kfree(idx);
	if (err)
		return err;

	dbg_gen("default root indexing node created LEB %d:0",
		main_first + DEFAULT_IDX_LEB);

	/* Create default root inode */
	tmp = ALIGN(UBIFS_INO_NODE_SZ, c->min_io_size);
	ino = kzalloc(tmp, GFP_KERNEL);
	if (!ino)
		return -ENOMEM;

	ino_key_init_flash(c, &ino->key, UBIFS_ROOT_INO);
	ino->ch.node_type = UBIFS_INO_NODE;
	ino->nlink = cpu_to_be32(2);
	ino->atime = ino->ctime = ino->mtime =
				cpu_to_be32(CURRENT_TIME_SEC.tv_sec);
	ino->mode = cpu_to_be32(S_IFDIR | S_IRUGO | S_IWUSR | S_IXUGO);

	/* Set compression enabled by default */
	ino->flags = cpu_to_be32(UBIFS_COMPR_FL);

	err = ubifs_write_node(c, ino, UBIFS_INO_NODE_SZ,
			       main_first + DEFAULT_DATA_LEB, 0,
			       UBI_UNKNOWN);
	kfree(ino);
	if (err)
		return err;

	dbg_gen("root inode created at LEB %d:0",
		main_first + DEFAULT_DATA_LEB);

	/*
	 * The first node in the log has to be the commit start node. This is
	 * always the case during normal file-system operation. Write a fake
	 * commit start node to the log.
	 */
	tmp = ALIGN(UBIFS_CS_NODE_SZ, c->min_io_size);
	cs = kzalloc(tmp, GFP_KERNEL);
	if (!cs)
		return -ENOMEM;

	cs->ch.node_type = UBIFS_CS_NODE;
	err = ubifs_write_node(c, cs, UBIFS_CS_NODE_SZ, UBIFS_LOG_LNUM,
			       0, UBI_UNKNOWN);
	kfree(cs);

	ubifs_msg("default file-system created");
	return 0;
}

/**
 * validate_sb - validate superblock node.
 * @c: UBIFS file-system description object
 * @sup: superblock node
 *
 * This function validates superblock node @sup. Since most of data was read
 * from the superblock and stored in @c, the function validates fields in @c
 * instead. Returns zero in case of success and %-EINVAL in case of validation
 * failure.
 */
static int validate_sb(struct ubifs_info *c, struct ubifs_sb_node *sup)
{
	if (be32_to_cpu(sup->flags))
		goto failed;

	if (!c->key_hash)
		goto failed;

	if (sup->key_fmt != UBIFS_SIMPLE_KEY_FMT)
		goto failed;

	if (be32_to_cpu(sup->min_io_size) != c->min_io_size) {
		ubifs_err("min. I/O unit mismatch: %d in superblock, %d real",
			  be32_to_cpu(sup->min_io_size), c->min_io_size);
		goto failed;
	}

	if (be32_to_cpu(sup->leb_size) != c->leb_size) {
		ubifs_err("LEB size mismatch: %d in superblock, %d real",
			  be32_to_cpu(sup->leb_size), c->leb_size);
		goto failed;
	}

	if (c->leb_cnt < UBIFS_MIN_LEB_CNT || c->leb_cnt > c->vi.size) {
		ubifs_err("bad LEB count: %d in superblock, %d on UBI volume, "
			  "%d minimum required", c->leb_cnt, c->vi.size,
			  UBIFS_MIN_LEB_CNT);
		goto failed;
	}

	if (c->max_leb_cnt < c->leb_cnt) {
		ubifs_err("max. LEB count %d less than LEB count %d",
			  c->max_leb_cnt, c->leb_cnt);
		goto failed;
	}

	if (c->log_lebs < UBIFS_MIN_LOG_LEBS ||
	    c->lpt_lebs < UBIFS_MIN_LPT_LEBS ||
	    c->orph_lebs < UBIFS_MIN_ORPH_LEBS ||
	    c->main_lebs < UBIFS_MIN_MAIN_LEBS)
		goto failed;

	if (c->main_lebs < UBIFS_MIN_MAIN_LEBS) {
		dbg_err("bad main_lebs");
		goto failed;
	}

	if (c->max_bud_bytes < (long long)c->leb_size * UBIFS_MIN_BUD_LEBS ||
	    c->max_bud_bytes > (long long)c->leb_size * c->main_lebs) {
		dbg_err("bad max_bud_bytes");
		goto failed;
	}

	if (c->jhead_cnt < NONDATA_JHEADS_CNT + 1 ||
	    c->jhead_cnt > NONDATA_JHEADS_CNT + UBIFS_MAX_JHEADS) {
		dbg_err("bad jhead_cnt");
		goto failed;
	}

	if (c->fanout < UBIFS_MIN_FANOUT ||
	    UBIFS_IDX_NODE_SZ + c->fanout * UBIFS_BRANCH_SZ > c->leb_size) {
		dbg_err("bad fanout");
		goto failed;
	}

	if (c->lsave_cnt < 0 || c->lsave_cnt > c->max_leb_cnt - UBIFS_SB_LEBS -
	    UBIFS_MST_LEBS - c->log_lebs - c->lpt_lebs - c->orph_lebs) {
		dbg_err("bad lsave_cnt");
		goto failed;
	}

	if (UBIFS_SB_LEBS + UBIFS_MST_LEBS + c->log_lebs + c->lpt_lebs +
	    c->orph_lebs + c->main_lebs != c->leb_cnt) {
		dbg_err("LEBs don't add up");
		goto failed;
	}

	if (c->default_compr < 0 || c->default_compr >= UBIFS_COMPR_TYPES_CNT) {
		dbg_err("bad compression type");
		goto failed;
	}

	return 0;

failed:
	ubifs_err("bad superblock");
	dbg_dump_node(c, sup);
	return -EINVAL;
}

/**
 * ubifs_read_sb_node - read superblock node.
 * @c: UBIFS file-system description object
 *
 * This function returns a pointer to the superblock node or a negative error
 * code.
 */
struct ubifs_sb_node *ubifs_read_sb_node(struct ubifs_info *c)
{
	struct ubifs_sb_node *sup;
	int err;

	sup = kmalloc(ALIGN(UBIFS_SB_NODE_SZ, c->min_io_size), GFP_NOFS);
	if (!sup)
		return ERR_PTR(-ENOMEM);

	err = ubifs_read_node(c, sup, UBIFS_SB_NODE, UBIFS_SB_NODE_SZ,
			      UBIFS_SB_LNUM, 0);
	if (err) {
		kfree(sup);
		return ERR_PTR(err);
	}

	return sup;
}

/**
 * ubifs_write_sb_node - write superblock node.
 * @c: UBIFS file-system description object
 * @sup: superblock node read with 'ubifs_read_sb_node()'
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_write_sb_node(struct ubifs_info *c, struct ubifs_sb_node *sup)
{
	int len = ALIGN(UBIFS_SB_NODE_SZ, c->min_io_size);

	ubifs_prepare_node(c, sup, UBIFS_SB_NODE_SZ, 1);
	return ubi_leb_change(c->ubi, UBIFS_SB_LNUM, sup, len, UBI_LONGTERM);
}

/**
 * ubifs_read_superblock - read superblock.
 * @c: UBIFS file-system description object
 *
 * This function finds, reads and checks the superblock. If an empty UBI volume
 * is being mounted, this function creates default superblock. Returns zero in
 * case of success, and a negative error code in case of failure.
 */
int ubifs_read_superblock(struct ubifs_info *c)
{
	int err;
	struct ubifs_sb_node *sup;

	if (c->empty) {
		err = create_default_filesystem(c);
		if (err)
			return err;
	}

	sup = ubifs_read_sb_node(c);
	if (IS_ERR(sup))
		return PTR_ERR(sup);

	switch(sup->key_hash) {
		case UBIFS_KEY_HASH_R5:
			c->key_hash = key_r5_hash;
			c->key_hash_type = UBIFS_KEY_HASH_R5;
			break;

		case UBIFS_KEY_HASH_TEST:
			c->key_hash = key_test_hash;
			c->key_hash_type = UBIFS_KEY_HASH_TEST;
			break;
	};

	c->key_fmt = sup->key_fmt;
	c->key_len = UBIFS_SK_LEN;

	c->big_lpt = sup->big_lpt;

	c->leb_cnt       = be32_to_cpu(sup->leb_cnt);
	c->max_leb_cnt   = be32_to_cpu(sup->max_leb_cnt);
	c->max_bud_bytes = be64_to_cpu(sup->max_bud_bytes);
	c->log_lebs      = be32_to_cpu(sup->log_lebs);
	c->lpt_lebs      = be32_to_cpu(sup->lpt_lebs);
	c->orph_lebs     = be32_to_cpu(sup->orph_lebs);
	c->jhead_cnt     = be32_to_cpu(sup->jhead_cnt) + NONDATA_JHEADS_CNT;
	c->fanout        = be32_to_cpu(sup->fanout);
	c->lsave_cnt     = be32_to_cpu(sup->lsave_cnt);
	c->default_compr = be16_to_cpu(sup->default_compr);

	/* Automatically increase file system size to the maximum size */
	c->old_leb_cnt = c->leb_cnt;
	if (c->leb_cnt < c->vi.size && c->leb_cnt < c->max_leb_cnt) {
		c->leb_cnt = min_t(int, c->max_leb_cnt, c->vi.size);
		if (c->vfs_sb->s_flags & MS_RDONLY)
			dbg_mnt("Auto resizing (ro) from %d LEBs to %d LEBs",
				c->old_leb_cnt,	c->leb_cnt);
		else {
			dbg_mnt("Auto resizing (sb) from %d LEBs to %d LEBs",
				c->old_leb_cnt, c->leb_cnt);
			sup->leb_cnt = cpu_to_be32(c->leb_cnt);
			err = ubifs_write_sb_node(c, sup);
			if (err)
				goto out;
			c->old_leb_cnt = c->leb_cnt;
		}
	}

	c->log_bytes = (long long)c->log_lebs * c->leb_size;
	c->log_last = UBIFS_LOG_LNUM + c->log_lebs - 1;
	c->lpt_first = UBIFS_LOG_LNUM + c->log_lebs;
	c->lpt_last = c->lpt_first + c->lpt_lebs - 1;
	c->orph_first = c->lpt_last + 1;
	c->orph_last = c->orph_first + c->orph_lebs - 1;
	c->main_lebs = c->leb_cnt - UBIFS_SB_LEBS - UBIFS_MST_LEBS;
	c->main_lebs -= c->log_lebs + c->lpt_lebs + c->orph_lebs;
	c->main_first = c->leb_cnt - c->main_lebs;

	err = validate_sb(c, sup);
out:
	kfree(sup);
	return err;
}
