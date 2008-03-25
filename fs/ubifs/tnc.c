/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
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
 * Authors: Adrian Hunter
 *          Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file implements TNC (Tree Node Cache) which caches indexing nodes of
 * the UBIFS B-tree.
 *
 * At the moment the locking rules of the TNC tree are quite simple and
 * straightforward. We just have a mutex and lock it when we traverse the
 * tree. If a znode is not in memory, we read it from flash while still having
 * the mutex locked.
 */

#include <linux/crc32.h>
#include "ubifs.h"

/**
 * insert_old_idx - record an index node obsoleted since the last commit start.
 * @c: UBIFS file-system description object
 * @lnum: LEB number of obsoleted index node
 * @offs: offset of obsoleted index node
 *
 * Returns %0 on success, and a negative error code on failure.
 */
static int insert_old_idx(struct ubifs_info *c, int lnum, int offs)
{
	struct ubifs_old_idx *old_idx, *o;
	struct rb_node **p, *parent = NULL;

	ubifs_assert(lnum >= c->main_first && lnum < c->leb_cnt);
	ubifs_assert(offs >= 0 && offs < c->leb_size);

	old_idx = kmalloc(sizeof(struct ubifs_old_idx), GFP_NOFS);
	if (!old_idx)
		return -ENOMEM;
	old_idx->lnum = lnum;
	old_idx->offs = offs;

	p = &c->old_idx.rb_node;
	while (*p) {
		parent = *p;
		o = rb_entry(parent, struct ubifs_old_idx, rb);
		if (lnum < o->lnum)
			p = &(*p)->rb_left;
		else if (lnum > o->lnum)
			p = &(*p)->rb_right;
		else if (offs < o->offs)
			p = &(*p)->rb_left;
		else if (offs > o->offs)
			p = &(*p)->rb_right;
		else {
			ubifs_err("old idx added twice!");
			kfree(old_idx);
			return 0;
		}
	}
	rb_link_node(&old_idx->rb, parent, p);
	rb_insert_color(&old_idx->rb, &c->old_idx);
	return 0;
}

/**
 * insert_old_idx_znode - record a znode obsoleted since last commit start.
 * @c: UBIFS file-system description object
 * @znode: znode of obsoleted index node
 *
 * Returns %0 on success, and a negative error code on failure.
 */
int insert_old_idx_znode(struct ubifs_info *c, struct ubifs_znode *znode)
{
	if (znode->parent) {
		struct ubifs_zbranch *zbr;

		zbr = &znode->parent->zbranch[znode->iip];
		if (zbr->len)
			return insert_old_idx(c, zbr->lnum, zbr->offs);
	} else
		if (c->zroot.len)
			return insert_old_idx(c, c->zroot.lnum,
					      c->zroot.offs);
	return 0;
}

/**
 * ins_clr_old_idx_znode - record a znode obsoleted since last commit start.
 * @c: UBIFS file-system description object
 * @znode: znode of obsoleted index node
 *
 * Returns %0 on success, and a negative error code on failure.
 */
static int ins_clr_old_idx_znode(struct ubifs_info *c,
				 struct ubifs_znode *znode)
{
	int err;

	if (znode->parent) {
		struct ubifs_zbranch *zbr;

		zbr = &znode->parent->zbranch[znode->iip];
		if (zbr->len) {
			err = insert_old_idx(c, zbr->lnum, zbr->offs);
			if (err)
				return err;
			zbr->lnum = 0;
			zbr->offs = 0;
			zbr->len = 0;
		}
	} else
		if (c->zroot.len) {
			err = insert_old_idx(c, c->zroot.lnum, c->zroot.offs);
			if (err)
				return err;
			c->zroot.lnum = 0;
			c->zroot.offs = 0;
			c->zroot.len = 0;
		}
	return 0;
}

/**
 * destroy_old_idx - destroy the old_idx RB-tree.
 * @c: UBIFS file-system description object
 *
 * During start commit, the old_idx RB-tree is used to avoid overwriting index
 * nodes that were in the index last commit but have since been deleted.  This
 * is necessary for recovery i.e. the old index must be kept intact until the
 * new index is successfully written.  The old-idx RB-tree is used for the
 * in-the-gaps method of writing index nodes and is destroyed every commit.
 */
void destroy_old_idx(struct ubifs_info *c)
{
	struct rb_node *this = c->old_idx.rb_node;
	struct ubifs_old_idx *old_idx;

	while (this) {
		if (this->rb_left) {
			this = this->rb_left;
			continue;
		} else if (this->rb_right) {
			this = this->rb_right;
			continue;
		}
		old_idx = rb_entry(this, struct ubifs_old_idx, rb);
		this = rb_parent(this);
		if (this) {
			if (this->rb_left == &old_idx->rb)
				this->rb_left = NULL;
			else
				this->rb_right = NULL;
		}
		kfree(old_idx);
	}
	c->old_idx = RB_ROOT;
}

/**
 * search_zbranch - search znode branch.
 * @c: UBIFS file-system description object
 * @znode: znode to search in
 * @key: key to search for
 * @n: znode branch slot number is returned here
 *
 * This is a helper function which search branch with key @key in @znode using
 * binary search. The result of the search may be:
 *   o exact match, then %1 is returned, and the slot number of the branch is
 *     stored in @n;
 *   o no exact match, then %0 is returned and the slot number of the left
 *   closest branch is returned in @n.
 */
static int search_zbranch(const struct ubifs_info *c,
			  const struct ubifs_znode *znode,
			  const union ubifs_key *key, int *n)
{
	int beg = 0, end = znode->child_cnt, uninitialized_var(mid);
	int uninitialized_var(cmp);
	const struct ubifs_zbranch *zbr = &znode->zbranch[0];

	ubifs_assert(end > beg);

	while (end > beg) {
		mid = (beg + end) >> 1;
		cmp = keys_cmp(c, key, &zbr[mid].key);
		if (cmp > 0)
			beg = mid + 1;
		else if (cmp < 0)
			end = mid;
		else {
			*n = mid;
			return 1;
		}
	}

	*n = end - 1;

	/* The insert point is after *n */
	ubifs_assert(*n >= -1 && *n < znode->child_cnt);
	if (*n == -1)
		ubifs_assert(keys_cmp(c, key, &zbr[0].key) < 0);
	else
		ubifs_assert(keys_cmp(c, key, &zbr[*n].key) > 0);
	if (*n + 1 < znode->child_cnt)
		ubifs_assert(keys_cmp(c, key, &zbr[*n + 1].key) < 0);

	return 0;
}

/**
 * read_znode - read an indexing node from flash and fill znode.
 * @c: UBIFS file-system description object
 * @lnum: LEB of the indexing node to read
 * @offs: node offset
 * @len: node length
 * @znode: znode to read to
 *
 * This function reads an indexing node from the flash media and fills znode
 * with the read data. Returns zero in case of success and a negative error
 * code in case of failure. The read indexing node is validated and if anything
 * is wrong with it, this function prints complaint messages and returns
 * %-EINVAL.
 */
static int read_znode(struct ubifs_info *c, int lnum, int offs, int len,
		      struct ubifs_znode *znode)
{
	int i, err, type, cmp;
	struct ubifs_idx_node *idx;

	idx = kmalloc(c->max_idx_node_sz, GFP_KERNEL);
	if (!idx)
		return -ENOMEM;

	err = ubifs_read_node(c, idx, UBIFS_IDX_NODE, len, lnum, offs);
	if (err < 0)
		goto out;

	znode->child_cnt = le16_to_cpu(idx->child_cnt);
	znode->level = le16_to_cpu(idx->level);

	dbg_tnc("LEB %d:%d, level %d, %d branch",
		lnum, offs, znode->level, znode->child_cnt);

	if (znode->child_cnt > c->fanout || znode->level > UBIFS_MAX_LEVELS) {
		dbg_err("current fanout %d, branch count %d",
			c->fanout, znode->child_cnt);
		dbg_err("max levels %d, znode level %d",
			UBIFS_MAX_LEVELS, znode->level);
		goto out_dump;
	}

	for (i = 0; i < znode->child_cnt; i++) {
		const struct ubifs_branch *br = ubifs_idx_branch(c, idx, i);
		struct ubifs_zbranch *zbr = &znode->zbranch[i];

		key_read(c, &br->key, &zbr->key);
		zbr->lnum = le32_to_cpu(br->lnum);
		zbr->offs = le32_to_cpu(br->offs);
		zbr->len  = le32_to_cpu(br->len);
		zbr->znode = NULL;

		/* Validate branch */

		if (unlikely(zbr->lnum < c->main_first ||
			     zbr->lnum >= c->leb_cnt || zbr->offs < 0 ||
			     zbr->offs + zbr->len > c->leb_size ||
			     zbr->offs & 7)) {
			dbg_err("bad branch %d", i);
			goto out_dump;
		}

		switch (key_type(c, &zbr->key)) {
		case UBIFS_INO_KEY:
		case UBIFS_DATA_KEY:
		case UBIFS_DENT_KEY:
		case UBIFS_XENT_KEY:
			break;
		default:
			dbg_key(c, &zbr->key, "bad key type at slot %d: ", i);
			goto out_dump;
		}

		if (znode->level)
			continue;

		type = key_type(c, &zbr->key);
		if (c->ranges[type].max_len == 0) {
			if (unlikely(zbr->len != c->ranges[type].len)) {
				dbg_err("bad target node (type %d) length (%d)",
					type, zbr->len);
				dbg_err("have to be %d", c->ranges[type].len);
				goto out_dump;
			}
		} else if (unlikely(zbr->len < c->ranges[type].min_len ||
				    zbr->len > c->ranges[type].max_len)) {
			dbg_err("bad target node (type %d) length (%d)",
				type, zbr->len);
			dbg_err("have to be in range of %d-%d",
				c->ranges[type].min_len,
				c->ranges[type].max_len);
			goto out_dump;
		}
	}

	/*
	 * Ensure that the next key is greater or equivalent to the
	 * previous one.
	 */
	for (i = 0; i < znode->child_cnt - 1; i++) {
		const union ubifs_key *key1, *key2;

		key1 = &znode->zbranch[i].key;
		key2 = &znode->zbranch[i + 1].key;

		cmp = keys_cmp(c, key1, key2);
		if (cmp > 0) {
			dbg_err("bad key order (keys %d and %d)", i, i + 1);
			goto out_dump;
		} else if (cmp == 0 && !is_hash_key(c, key1)) {
			/* These can only be keys with colliding hash */
			dbg_err("keys %d and %d are not hashed but equivalent",
				i, i + 1);
			goto out_dump;
		}
	}

	kfree(idx);
	return 0;

out:
	kfree(idx);
	return err;

out_dump:
	ubifs_err("bad indexing node at LEB %d:%d", lnum, offs);
	dbg_dump_node(c, idx);
	kfree(idx);
	return -EINVAL;
}

/**
 * load_znode - load znode to TNC cache.
 * @c: UBIFS file-system description object
 * @zbr: znode branch
 * @parent: znode's parent
 * @iip: index in parent
 *
 * This function loads znode pointed to by @zbr into the TNC cache and
 * returns pointer to it in case of success and a negative error code in case
 * of failure.
 */
static struct ubifs_znode *load_znode(struct ubifs_info *c,
				      struct ubifs_zbranch *zbr,
				      struct ubifs_znode *parent, int iip)
{
	int err;
	struct ubifs_znode *znode;

	ubifs_assert(!zbr->znode);
	/*
	 * A slab cache is not presently used for znodes because the znode size
	 * depends on the fanout which is stored in the superblock.
	 */
	znode = kzalloc(c->max_znode_sz, GFP_NOFS);
	if (!znode)
		return ERR_PTR(-ENOMEM);

	err = read_znode(c, zbr->lnum, zbr->offs, zbr->len, znode);
	if (err)
		goto out;

	atomic_long_inc(&c->clean_zn_cnt);

	/*
	 * Increment the global clean znode counter as well. It is OK that
	 * global and per-FS clean znode counters may be inconsistent for some
	 * short time (because we might be preempted at this point), the global
	 * one is only used in shrinker.
	 */
	atomic_long_inc(&ubifs_clean_zn_cnt);

	zbr->znode = znode;
	znode->parent = parent;
	znode->time = get_seconds();
	znode->iip = iip;

	return znode;

out:
	kfree(znode);
	return ERR_PTR(err);
}

/**
 * copy_znode - copy a dirty znode.
 * @c: UBIFS file-system description object
 * @znode: znode to copy
 *
 * A dirty znode being committed may not be changed, so it is copied.
 */
static struct ubifs_znode *copy_znode(struct ubifs_info *c,
				      struct ubifs_znode *znode)
{
	struct ubifs_znode *zn;

	zn = kzalloc(c->max_znode_sz, GFP_NOFS);
	if (!zn)
		return ERR_PTR(-ENOMEM);

	memcpy(zn, znode, c->max_znode_sz);

	ubifs_assert(!test_bit(OBSOLETE_ZNODE, &znode->flags));
	set_bit(OBSOLETE_ZNODE, &znode->flags);

	if (znode->level != 0) {
		int i;
		const int n = zn->child_cnt;

		/* The children now have new parent */
		for (i = 0; i < n; i++) {
			struct ubifs_zbranch *zbr = &zn->zbranch[i];

			if (zbr->znode)
				zbr->znode->parent = zn;
		}
	}

	zn->cnext = NULL;
	set_bit(DIRTY_ZNODE, &zn->flags);
	clear_bit(COW_ZNODE, &zn->flags);
	atomic_long_inc(&c->dirty_zn_cnt);

	return zn;
}

/**
 * add_idx_dirt - add dirt due to a dirty znode.
 * @c: UBIFS file-system description object
 * @lnum: LEB number of index node
 * @dirt: size of index node
 *
 * This function updates lprops dirty space and the new size of the index.
 */
static int add_idx_dirt(struct ubifs_info *c, int lnum, int dirt)
{
	c->calc_idx_sz -= ALIGN(dirt, 8);
	return ubifs_add_dirt(c, lnum, dirt);
}

/**
 * dirty_cow_znode - ensure a znode is not being committed.
 * @c: UBIFS file-system description object
 * @zbr: branch of znode to check
 *
 * Returns dirtied znode on success or negative error code on failure.
 */
static struct ubifs_znode *dirty_cow_znode(struct ubifs_info *c,
					   struct ubifs_zbranch *zbr)
{
	struct ubifs_znode *znode = zbr->znode;
	struct ubifs_znode *zn;
	int err;

	if (!test_bit(COW_ZNODE, &znode->flags)) {
		/* znode is not being committed */
		if (!test_and_set_bit(DIRTY_ZNODE, &znode->flags)) {
			atomic_long_inc(&c->dirty_zn_cnt);
			atomic_long_dec(&c->clean_zn_cnt);
			atomic_long_dec(&ubifs_clean_zn_cnt);
			err = add_idx_dirt(c, zbr->lnum, zbr->len);
			if (err)
				return ERR_PTR(err);
		}
		return znode;
	}

	zn = copy_znode(c, znode);
	if (IS_ERR(zn))
		return zn;

	if (zbr->len) {
		err = insert_old_idx(c, zbr->lnum, zbr->offs);
		if (err)
			return ERR_PTR(err);

		err = add_idx_dirt(c, zbr->lnum, zbr->len);
	} else
		err = 0;

	zbr->znode = zn;
	zbr->lnum = 0;
	zbr->offs = 0;
	zbr->len = 0;

	if (err)
		return ERR_PTR(err);

	return zn;
}

/**
 * lookup_level0 - search for zero-level znode
 * @c: UBIFS file-system description object
 * @key:  key to lookup
 * @zn: znode is returned here
 * @n: znode branch slot number is returned here
 *
 * This function looks up the TNC tree and search for zero-level znode which
 * refers key @key. The found zero-level znode is returned in @zn. There are 3
 * cases:
 *   o exact match, i.e. the found zero-level znode contains key @key, then %1
 *     is returned and slot number of the matched branch is stored in @n;
 *   o not exact match, which means that zero-level znode does not contain @key
 *     then %0 is returned and slot number of the closed branch is stored in
 *     @n;
 *   o @key is so small that it is even less then the lowest key of the
 *     leftmost zero-level node, then %0 is returned and %0 is stored in @n.
 *
 * Note, when the TNC tree is traversed, some znodes may be absent, then this
 * function reads corresponding indexing nodes and inserts them to TNC.. In
 * case of failure, a negative error code is returned.
 */
static int lookup_level0(struct ubifs_info *c, const union ubifs_key *key,
			 struct ubifs_znode **zn, int *n)
{
	int exact;
	struct ubifs_znode *znode;
	unsigned long time = get_seconds();

	dbg_tnc_key(c, key, "search key");

	znode = c->zroot.znode;
	if (unlikely(!znode)) {
		znode = load_znode(c, &c->zroot, NULL, 0);
		if (IS_ERR(znode))
			return PTR_ERR(znode);
	}

	znode->time = time;

	while (1) {
		struct ubifs_zbranch *zbr;

		/*
		 * The below is a debugging hack to make UBIFS eat RAM and
		 * cause fake memory pressure. It is compiled out if it is not
		 * enabled in kernel configuration.
		 */
		dbg_eat_memory();

		exact = search_zbranch(c, znode, key, n);

		if (znode->level == 0)
			break;

		if (*n < 0)
			*n = 0;
		zbr = &znode->zbranch[*n];

		dbg_tnc_key(c, &zbr->key, "at lvl %d, next zbr %d, key",
			    znode->level, *n);

		if (zbr->znode) {
			znode->time = time;
			znode = zbr->znode;
			continue;
		}

		/* znode is not in TNC cache, load it from the media */
		znode = load_znode(c, zbr, znode, *n);
		if (IS_ERR(znode))
			return PTR_ERR(znode);
	}

	*zn = znode;
	ubifs_assert(exact >= 0 && exact < c->fanout);
	return exact;
}

/**
 * lookup_level0_dirty - search for zero-level znode dirtying
 * @c: UBIFS file-system description object
 * @key:  key to lookup
 * @zn: znode is returned here
 * @n: znode branch slot number is returned here
 *
 * This function looks up the TNC tree and search for zero-level znode which
 * refers key @key. The found zero-level znode is returned in @zn. There are 3
 * cases:
 *   o exact match, i.e. the found zero-level znode contains key @key, then %1
 *     is returned and slot number of the matched branch is stored in @n;
 *   o not exact match, which means that zero-level znode does not contain @key
 *     then %0 is returned and slot number of the closed branch is stored in
 *     @n;
 *   o @key is so small that it is even less then the lowest key of the
 *     leftmost zero-level node, then %0 is returned and %0 is stored in @n.
 *
 * Additionally all znodes in the path from the root to the located zero-level
 * znode are marked as dirty.
 *
 * Note, when the TNC tree is traversed, some znodes may be absent, then this
 * function reads corresponding indexing nodes and inserts them to TNC.. In
 * case of failure, a negative error code is returned.
 */
static int lookup_level0_dirty(struct ubifs_info *c, const union ubifs_key *key,
			       struct ubifs_znode **zn, int *n)
{
	int exact;
	struct ubifs_znode *znode;
	unsigned long time = get_seconds();

	dbg_tnc_key(c, key, "search and dirty key");

	znode = c->zroot.znode;
	if (unlikely(!znode)) {
		znode = load_znode(c, &c->zroot, NULL, 0);
		if (IS_ERR(znode))
			return PTR_ERR(znode);
	}

	znode = dirty_cow_znode(c, &c->zroot);
	if (IS_ERR(znode))
		return PTR_ERR(znode);

	znode->time = time;

	while (1) {
		struct ubifs_zbranch *zbr;

		/*
		 * The below is a debugging hack to make UBIFS eat RAM and
		 * cause fake memory pressure. It is compiled out if it is not
		 * enabled in kernel configuration.
		 */
		dbg_eat_memory();

		exact = search_zbranch(c, znode, key, n);

		if (znode->level == 0)
			break;

		if (*n < 0)
			*n = 0;
		zbr = &znode->zbranch[*n];

		dbg_tnc_key(c, &zbr->key, "at lvl %d, next zbr %d, key",
			    znode->level, *n);

		if (zbr->znode) {
			znode->time = time;
			znode = dirty_cow_znode(c, zbr);
			if (IS_ERR(znode))
				return PTR_ERR(znode);
			continue;
		}

		/* znode is not in TNC cache, load it from the media */
		znode = load_znode(c, zbr, znode, *n);
		if (IS_ERR(znode))
			return PTR_ERR(znode);
		znode = dirty_cow_znode(c, zbr);
		if (IS_ERR(znode))
			return PTR_ERR(znode);
	}

	*zn = znode;
	ubifs_assert(exact >= 0 && exact < c->fanout);
	return exact;
}

/**
 * lnc_lookup - lookup the leaf-node-cache.
 * @c: UBIFS file-system description object
 * @zbr: zbranch of leaf node
 * @node: leaf node
 *
 * Leaf nodes are non-index nodes like dent (directory entry) nodes or data
 * nodes.  The purpose of the leaf-node-cache is to save re-reading the same
 * leaf node over and over again.  Most things are cached by VFS, however the
 * file system must cache directory entries for readdir and for resolving hash
 * collisions.  The present implementation of the leaf-node-cache is extremely
 * simple, and allows for error returns that are not used but that may be needed
 * if a more complex implementation is created.
 *
 * This function returns %1 if the leaf node is in the cache, %0 if it is not,
 * and a negative error code otherwise.
 */
static int lnc_lookup(struct ubifs_info *c, struct ubifs_zbranch *zbr,
		      void *node)
{
	if (zbr->leaf == NULL)
		return 0;
	ubifs_assert(zbr->len != 0);
	memcpy(node, zbr->leaf, zbr->len);
	return 1;
}

/**
 * ubifs_validate_entry - validate directory or extended attribute entry node.
 * @c: UBIFS file-system description object
 * @dent: the node to validate
 *
 * This function validates directory or extended attribute entry node @dent.
 * Returns zero if the node is all right and a %-EINVAL if not.
 */
int ubifs_validate_entry(struct ubifs_info *c,
			 const struct ubifs_dent_node *dent)
{
	int key_type, nlen = le16_to_cpu(dent->nlen);

	if (le32_to_cpu(dent->ch.len) != nlen + UBIFS_DENT_NODE_SZ + 1 ||
	    dent->type >= UBIFS_ITYPES_CNT ||
	    nlen > UBIFS_MAX_NLEN || dent->name[nlen] != 0 ||
	    strnlen(dent->name, nlen) != nlen ||
	    le64_to_cpu(dent->inum) > MAX_INUM) {
		const char *node_type;

		if (key_type_flash(c, dent->key) == UBIFS_DENT_KEY)
			node_type = "directory entry";
		else
			node_type = "extended attribute entry";

		ubifs_err("bad %s node", node_type);
		return -EINVAL;
	}

	key_type = key_type_flash(c, dent->key);
	if (key_type_flash(c, dent->key) != UBIFS_DENT_KEY &&
	    key_type_flash(c, dent->key) != UBIFS_XENT_KEY) {
		ubifs_err("bad key type %d", key_type);
		return -EINVAL;
	}

	return 0;
}

/**
 * lnc_add - add a leaf node to the leaf-node-cache.
 * @c: UBIFS file-system description object
 * @zbr: zbranch of leaf node
 * @node: leaf node
 *
 * This function returns %0 to indicate success and a negative error code
 * otherwise.
 */
static int lnc_add(struct ubifs_info *c, struct ubifs_zbranch *zbr,
		   const void *node)
{
	int err;
	void *lnc_node;
	const struct ubifs_dent_node *dent = node;

	ubifs_assert(zbr->leaf == NULL);
	ubifs_assert(zbr->len != 0);

	/* Add all dents, but nothing else */
	if (key_type(c, &zbr->key) != UBIFS_DENT_KEY)
		return 0;

	err = ubifs_validate_entry(c, dent);
	if (err) {
		dbg_dump_node(c, dent);
		return err;
	}

	lnc_node = kmalloc(zbr->len, GFP_NOFS);
	if (!lnc_node)
		return 0; /* We don't have to have the cache, so no error */

	memcpy(lnc_node, node, zbr->len);
	zbr->leaf = lnc_node;
	return 0;
}

/**
 * lnc_free - remove a leaf node from the leaf-node-cache.
 * @zbr: zbranch of leaf node
 * @node: leaf node
 *
 * This function returns %0 to indicate success and a negative error code
 * otherwise.
 */
static void lnc_free(struct ubifs_zbranch *zbr)
{
	if (zbr->leaf == NULL)
		return;
	kfree(zbr->leaf);
	zbr->leaf = NULL;
}

/**
 * tnc_read_node - read a leaf node.
 * @c: UBIFS file-system description object
 * @zbr:  key and position of node
 * @node: node returned
 *
 * This function reads a node or returns a negative error code.
 */
static int tnc_read_node(struct ubifs_info *c, struct ubifs_zbranch *zbr,
			 void *node)
{
	union ubifs_key key1, *key = &zbr->key;
	int err, type = key_type(c, key);
	const struct ubifs_bud *bud;

	dbg_tnc_key(c, key, "LEB %d:%d, len %d, key",
		    zbr->lnum, zbr->offs, zbr->len);

	if (lnc_lookup(c, zbr, node))
		return 0; /* Read from the leaf-node-cache */
	/*
	 * 'zbr' has to point to on-flash node. The node may sit in a bud and
	 * may even be in a write buffer, so we have to take care about this.
	 */
	if (c->jheads)
		bud = ubifs_search_bud(c, zbr->lnum);
	else
		bud = NULL;
	if (bud)
		/* The bud can't go because we are under @c->commit_sem */
		err = ubifs_read_node_wbuf(&c->jheads[bud->jhead].wbuf,
					   node, type, zbr->len, zbr->lnum,
					   zbr->offs);
	else
		err = ubifs_read_node(c, node, type, zbr->len, zbr->lnum,
				      zbr->offs);

	if (err) {
		dbg_tnc_key(c, key, "key");
		return err;
	}

	/* Make sure the key of the read node is correct */
	key_read(c, key, &key1);
	if (memcmp(node + UBIFS_CH_SZ, &key1, c->key_len)) {
		ubifs_err("bad key in node at LEB %d:%d",
			  zbr->lnum, zbr->offs);
		dbg_tnc_key(c, key, "looked for key");
		dbg_tnc_key(c, &key1, "found node's key");
		dbg_dump_node(c, node);
		return err;
	}

	/* Consider adding the node to the leaf node cache */
	err = lnc_add(c, zbr, node);
	return err;
}

/**
 * ubifs_try_read_node - read a node if it is a node.
 * @c: UBIFS file-system description object
 * @buf: buffer to read to
 * @type: node type
 * @len: node length (not aligned)
 * @lnum: LEB number of node to read
 * @offs: offset of node to read
 *
 * This function tries to read a node of known type and length, checks it and
 * stores it in @buf. This function returns %1 if a node is present and %0 if
 * a node is not present.  A negative error code is returned for I/O errors.
 * This function performs that same function as ubifs_read_node except that
 * it does not require that there is actually a node present and instead
 * the return code indicates if a node was read.
 */
static int try_read_node(const struct ubifs_info *c, void *buf, int type,
			 int len, int lnum, int offs)
{
	int err, node_len;
	struct ubifs_ch *ch = buf;
	uint32_t crc, node_crc;

	dbg_io("LEB %d:%d, %s, length %d", lnum, offs, dbg_ntype(type), len);
	ubifs_assert(lnum >= 0 && lnum < c->leb_cnt && offs >= 0);
	ubifs_assert(len >= UBIFS_CH_SZ && offs + len <= c->leb_size);
	ubifs_assert(!(offs & 7) && offs < c->leb_size);
	ubifs_assert(type >= 0 && type < UBIFS_NODE_TYPES_CNT);

	err = ubi_read(c->ubi, lnum, buf, offs, len);
	if (err) {
		ubifs_err("cannot read node type %d from LEB %d:%d, error %d",
			  type, lnum, offs, err);
		return err;
	}

	if (le32_to_cpu(ch->magic) != UBIFS_NODE_MAGIC)
		return 0;

	if (ch->node_type != type)
		return 0;

	node_len = le32_to_cpu(ch->len);
	if (node_len != len)
		return 0;

	crc = crc32(UBIFS_CRC32_INIT, buf + 8, node_len - 8);
	node_crc = le32_to_cpu(ch->crc);
	if (crc != node_crc)
		return 0;

	return 1;
}

/**
 * fallible_read_node - try to read a leaf node.
 * @c: UBIFS file-system description object
 * @key:  key of node to read
 * @zbr:  position of node
 * @node: node returned
 *
 * This function tries to read a node and returns %1 if the node is read, %0
 * if the node is not present, and a negative error code in the case of error.
 */
static int fallible_read_node(struct ubifs_info *c, const union ubifs_key *key,
			      struct ubifs_zbranch *zbr, void *node)
{
	int ret;

	dbg_tnc_key(c, key, "key");

	if (lnc_lookup(c, zbr, node))
		return 0; /* Read from the leaf-node-cache */

	ret = try_read_node(c, node, key_type(c, key), zbr->len, zbr->lnum,
			    zbr->offs);
	if (ret == 1) {
		union ubifs_key node_key;

		/* All nodes have key in the same place */
		key_read(c, &((struct ubifs_dent_node *)node)->key, &node_key);
		if (keys_cmp(c, key, &node_key) == 0) {
			/* Consider adding the node to the leaf node cache */
			int err = lnc_add(c, zbr, node);

			if (err)
				return err;
		} else
			ret = 0;
	} else if (ret == 0)
		dbg_gc_key(c, key, "dangling branch LEB %d:%d len %d, key",
			   zbr->lnum, zbr->offs, zbr->len);
	return ret;
}

/**
 * matches_name - determine if a directory or extended attribute entry matches
 *                a given name.
 * @c: UBIFS file-system description object
 * @zt: zbranch of dent
 * @nm: name to match
 *
 * This function returns %1 if the name matches, %0 if the name does not match
 * and a negative error code otherwise.
 */
static int matches_name(struct ubifs_info *c, struct ubifs_zbranch *zt,
			const struct qstr *nm)
{
	struct ubifs_dent_node *dent;
	int nlen, err;

	/* If possible, match against the dent in the leaf-node-cache */
	dent = zt->leaf;
	if (dent) {
		nlen = le16_to_cpu(dent->nlen);

		if (nlen == nm->len && !memcmp(dent->name, nm->name, nlen))
			return 1;
		return 0;
	}

	dent = kmalloc(zt->len, GFP_NOFS);
	if (!dent)
		return -ENOMEM;
	/*
	 * In this case we end up allocating another dent object in lnc_add(),
	 * although it could have just inserted this dent.
	 */
	err = tnc_read_node(c, zt, dent);
	if (!err) {
		err = ubifs_validate_entry(c, dent);
		if (err) {
			dbg_dump_node(c, dent);
			goto out;
		}

		nlen = le16_to_cpu(dent->nlen);
		if (nlen == nm->len && !memcmp(dent->name, nm->name, nlen))
			err = 1;
	}

out:
	kfree(dent);
	return err;
}

/**
 * get_znode - get a TNC znode that may not be loaded yet.
 * @c: UBIFS file-system description object
 * @znode: parent znode
 * @n: znode branch slot number
 *
 * This function returns the znode or a negative error code.
 */
static struct ubifs_znode *get_znode(struct ubifs_info *c,
				     struct ubifs_znode *znode, int n)
{
	struct ubifs_zbranch *zbr;

	zbr = &znode->zbranch[n];
	if (zbr->znode)
		znode = zbr->znode;
	else
		znode = load_znode(c, zbr, znode, n);
	return znode;
}

/**
 * tnc_next - find next TNC entry.
 * @c: UBIFS file-system description object
 * @zn: znode is passed and returned here
 * @nn: znode branch slot number is passed and returned here
 *
 * This function returns %0 if the next TNC entry is found, %-ENOENT if there is
 * no next entry, or a negative error code otherwise.
 */
static int tnc_next(struct ubifs_info *c, struct ubifs_znode **zn, int *nn)
{
	struct ubifs_znode *znode = *zn;
	int n = *nn;

	n += 1;
	if (n < znode->child_cnt) {
		*nn = n;
		return 0;
	}
	while (1) {
		struct ubifs_znode *zp;

		zp = znode->parent;
		if (!zp)
			return -ENOENT;
		n = znode->iip + 1;
		znode = zp;
		if (n < znode->child_cnt) {
			znode = get_znode(c, znode, n);
			if (IS_ERR(znode))
				return PTR_ERR(znode);
			while (znode->level != 0) {
				znode = get_znode(c, znode, 0);
				if (IS_ERR(znode))
					return PTR_ERR(znode);
			}
			n = 0;
			break;
		}
	}
	*zn = znode;
	*nn = n;
	return 0;
}

/**
 * tnc_prev - find previous TNC entry.
 * @c: UBIFS file-system description object
 * @zn: znode is returned here
 * @nn: znode branch slot number is passed and returned here
 *
 * This function returns %0 if the previous TNC entry is found, %-ENOENT if
 * there is no next entry, or a negative error code otherwise.
 */
static int tnc_prev(struct ubifs_info *c, struct ubifs_znode **zn, int *nn)
{
	struct ubifs_znode *znode = *zn;
	int n = *nn;

	if (n > 0) {
		*nn = n - 1;
		return 0;
	}
	while (1) {
		struct ubifs_znode *zp;

		zp = znode->parent;
		if (!zp)
			return -ENOENT;
		n = znode->iip - 1;
		znode = zp;
		if (n >= 0) {
			znode = get_znode(c, znode, n);
			if (IS_ERR(znode))
				return PTR_ERR(znode);
			while (znode->level != 0) {
				n = znode->child_cnt - 1;
				znode = get_znode(c, znode, n);
				if (IS_ERR(znode))
					return PTR_ERR(znode);
			}
			n = znode->child_cnt - 1;
			break;
		}
	}
	*zn = znode;
	*nn = n;
	return 0;
}

/**
 * resolve_collision - resolve a collision.
 * @c: UBIFS file-system description object
 * @key: key of a directory or extended attribute entry
 * @zn: znode is returned here
 * @nn: znode branch slot number is passed and returned here
 * @nm: name of the entry
 *
 * This function returns %1 and sets @zn and @nn if the collision is resolved.
 * %0 is returned if @nm is not found and @zn and @nn are set to the next
 * entry. %-ENOENT is returned if there are no following entries for the same
 * inode. Otherwise a negative error code is returned.
 */
static int resolve_collision(struct ubifs_info *c, const union ubifs_key *key,
			     struct ubifs_znode **zn, int *nn,
			     const struct qstr *nm)
{
	struct ubifs_znode *znode;
	union ubifs_key *okey;
	int n, err;

	dbg_tnc_key(c, key, "key");

	znode = *zn;
	n = *nn;
	err = matches_name(c, &znode->zbranch[n], nm);
	if (err < 0)
		return err;
	if (err == 1)
		return 1;

	/* Look left */
	while (1) {
		err = tnc_prev(c, &znode, &n);
		if (err == -ENOENT)
			break;
		if (err)
			return err;
		if (keys_cmp(c, &znode->zbranch[n].key, key))
			break;
		err = matches_name(c, &znode->zbranch[n], nm);
		if (err < 0)
			return err;
		if (err == 1) {
			dbg_tnc_key(c, key, "collision resolved");
			*zn = znode;
			*nn = n;
			return 1;
		}
	}

	/* Look right */
	znode = *zn;
	n = *nn;
	while (1) {
		err = tnc_next(c, &znode, &n);
		if (err)
			return err;
		okey = &znode->zbranch[n].key;
		if (keys_cmp(c, okey, key))
			return -ENOENT;
		err = matches_name(c, &znode->zbranch[n], nm);
		if (err < 0)
			return err;
		if (err == 1) {
			dbg_tnc_key(c, key, "collision resolved");
			*zn = znode;
			*nn = n;
			return 1;
		}
	}

	return -EINVAL;
}

/**
 * fallible_matches_name - determine if a dent matches a given name.
 * @c: UBIFS file-system description object
 * @zt: zbranch of dent
 * @nm: name to match
 *
 * This function returns %1 if the name matches, %0 if the name does not match,
 * %2 if the node was not present, and a negative error code otherwise.
 */
static int fallible_matches_name(struct ubifs_info *c, struct ubifs_zbranch *zt,
				 const struct qstr *nm)
{
	struct ubifs_dent_node *dent;
	int nlen, err;

	/* If possible, match against the dent in the leaf-node-cache */
	dent = zt->leaf;
	if (dent) {
		nlen = le16_to_cpu(dent->nlen);

		if (nlen == nm->len && !memcmp(dent->name, nm->name, nlen))
			return 1;
		return 0;
	}

	dent = kmalloc(zt->len, GFP_NOFS);
	if (!dent)
		return -ENOMEM;
	/*
	 * In this case we end up allocating another dent object in lnc_add(),
	 * although it could have just inserted this dent.
	 */
	err = fallible_read_node(c, &zt->key, zt, dent);
	if (err < 0)
		goto out;
	if (err == 0) {
		err = 2; /* The node was not present */
		goto out;
	}
	if (err == 1) {
		err = ubifs_validate_entry(c, dent);
		if (err) {
			dbg_dump_node(c, dent);
			goto out;
		}

		nlen = le16_to_cpu(dent->nlen);
		if (nlen == nm->len && !memcmp(dent->name, nm->name, nlen))
			err = 1;
		else
			err = 0;
	}
out:
	kfree(dent);
	return err;
}

/**
 * fallible_resolve_collision - resolve a collision even if nodes are missing.
 * @c: UBIFS file-system description object
 * @key: key of directory entry
 * @zn: znode is returned here
 * @nn: znode branch slot number is passed and returned here
 * @nm: name of directory entry
 *
 * This function returns %1 and sets @zn and @nn if the collision is resolved.
 * %0 is returned if @nm is not found and @zn and @nn are set to the
 * next directory entry.  %-ENOENT is returned if there are no
 * following directory entries for the same inode.  Otherwise a negative error
 * code is returned.
 */
static int fallible_resolve_collision(struct ubifs_info *c,
				      const union ubifs_key *key,
				      struct ubifs_znode **zn, int *nn,
				      const struct qstr *nm)
{
	struct ubifs_znode *znode, *o_znode = NULL;
	union ubifs_key *okey;
	int n, o_n = 0, err;

	dbg_tnc_key(c, key, "key");
	znode = *zn;
	n = *nn;
	err = fallible_matches_name(c, &znode->zbranch[n], nm);
	if (err < 0)
		return err;
	if (err == 1)
		return 1;
	if (err == 2) {
		o_znode = znode;
		o_n = n;
	}

	/* Look left */
	while (1) {
		err = tnc_prev(c, &znode, &n);
		if (err == -ENOENT)
			break;
		if (err)
			return err;
		if (keys_cmp(c, &znode->zbranch[n].key, key))
			break;
		err = fallible_matches_name(c, &znode->zbranch[n], nm);
		if (err < 0)
			return err;
		if (err == 1) {
			dbg_tnc_key(c, key, "collision resolved");
			*zn = znode;
			*nn = n;
			return 1;
		}
		if (err == 2) {
			o_znode = znode;
			o_n = n;
		}
	}
	/* Look right */
	znode = *zn;
	n = *nn;
	while (1) {
		err = tnc_next(c, &znode, &n);
		if (err == -ENOENT && o_znode) {
			dbg_tnc_key(c, key, "collision resolved by default");
			dbg_gc_key(c, key, "dangling match LEB %d:%d len %d ",
				   o_znode->zbranch[o_n].lnum,
				   o_znode->zbranch[o_n].offs,
				   o_znode->zbranch[o_n].len);
			*zn = o_znode;
			*nn = o_n;
			return 1;
		}
		if (err)
			return err;
		okey = &znode->zbranch[n].key;
		if (keys_cmp(c, okey, key)) {
			if (!o_znode)
				return -ENOENT;
			dbg_tnc_key(c, key, "collision resolved by default");
			dbg_gc_key(c, key, "dangling match LEB %d:%d len %d ",
				   o_znode->zbranch[o_n].lnum,
				   o_znode->zbranch[o_n].offs,
				   o_znode->zbranch[o_n].len);
			*zn = o_znode;
			*nn = o_n;
			return 1;
		}
		err = fallible_matches_name(c, &znode->zbranch[n], nm);
		if (err < 0)
			return err;
		if (err == 1) {
			dbg_tnc_key(c, key, "collision resolved");
			*zn = znode;
			*nn = n;
			return 1;
		}
		if (err == 2) {
			o_znode = znode;
			o_n = n;
		}
	}
	return -EINVAL;
}

/**
 * matches_position - determine if a zbranch matches a given position.
 * @zt: zbranch of dent
 * @lnum: LEB number of dent to match
 * @offs: offset of dent to match
 *
 * This function returns %1 if @lnum:@offs matches, and %0 otherwise.
 */
static int matches_position(struct ubifs_zbranch *zt, int lnum, int offs)
{
	if (zt->lnum == lnum && zt->offs == offs)
		return 1;
	else
		return 0;
}

/**
 * resolve_collision_directly - resolve a collision directly.
 * @c: UBIFS file-system description object
 * @key: key of directory entry
 * @zn: znode is passed and returned here
 * @nn: znode branch slot number is passed and returned here
 * @lnum: LEB number of dent node to match
 * @offs: offset of dent node to match
 *
 * This function returns %1 and sets @zn and @nn if the collision is resolved.
 * %0 is returned if @lnum:@offs is not found and @zn and @nn are set to the
 * next directory entry.  %-ENOENT is returned if there are no
 * following directory entries for the same inode.  Otherwise a negative error
 * code is returned.
 */
static int resolve_collision_directly(struct ubifs_info *c,
				      const union ubifs_key *key,
				      struct ubifs_znode **zn, int *nn,
				      int lnum, int offs)
{
	struct ubifs_znode *znode;
	union ubifs_key *okey;
	int n, err;

	dbg_tnc_key(c, key, "key");
	dbg_mnt_key(c, key, "LEB %d:%d", lnum, offs);
	znode = *zn;
	n = *nn;
	if (matches_position(&znode->zbranch[n], lnum, offs))
		return 1;

	/* Look left */
	while (1) {
		err = tnc_prev(c, &znode, &n);
		if (err == -ENOENT)
			break;
		if (err)
			return err;
		if (keys_cmp(c, &znode->zbranch[n].key, key))
			break;
		if (matches_position(&znode->zbranch[n], lnum, offs)) {
			dbg_tnc_key(c, key, "collision resolved");
			dbg_mnt_key(c, key, "LEB %d:%d collision resolved",
				    lnum, offs);
			*zn = znode;
			*nn = n;
			return 1;
		}
	}

	/* Look right */
	znode = *zn;
	n = *nn;
	while (1) {
		err = tnc_next(c, &znode, &n);
		if (err)
			return err;
		okey = &znode->zbranch[n].key;
		if (keys_cmp(c, okey, key))
			return 0;
		if (matches_position(&znode->zbranch[n], lnum, offs)) {
			dbg_tnc_key(c, key, "collision resolved");
			dbg_mnt_key(c, key, "LEB %d:%d collision resolved",
				    lnum, offs);
			*zn = znode;
			*nn = n;
			return 1;
		}
	}
}

/**
 * ubifs_tnc_lookup - look up a file-system node.
 * @c: UBIFS file-system description object
 * @key: node key to lookup
 * @node: the node is returned here
 *
 * This function look up and reads node with key @key. The caller has to make
 * sure the @node buffer is large enough to fit the node. Returns zero in case
 * of success, %-ENOENT if the node was not found, and a negative error code in
 * case of failure.
 */
int ubifs_tnc_lookup(struct ubifs_info *c, const union ubifs_key *key,
		     void *node)
{
	int found, n, err;
	struct ubifs_znode *znode;
	struct ubifs_zbranch zbr, *zt;

	mutex_lock(&c->tnc_mutex);
	found = lookup_level0(c, key, &znode, &n);
	if (!found) {
		err = -ENOENT;
		goto out;
	} else if (found < 0) {
		err = found;
		goto out;
	}
	zt = &znode->zbranch[n];
	if (is_hash_key(c, key)) {
		/*
		 * In this case the leaf-node-cache gets used, so we pass the
		 * address of the zbranch and keep the mutex locked
		 */
		err = tnc_read_node(c, zt, node);
		goto out;
	}
	zbr = znode->zbranch[n];
	mutex_unlock(&c->tnc_mutex);

	err = tnc_read_node(c, &zbr, node);
	return err;

out:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_locate - look up a file-system node and return it and its location.
 * @c: UBIFS file-system description object
 * @key: node key to lookup
 * @node: the node is returned here
 * @lnum: LEB number is returned here
 * @offs: offset is returned here
 *
 * This function is the same as 'ubifs_tnc_lookup()' but it returns the node
 * location also. See 'ubifs_tnc_lookup()'.
 */
int ubifs_tnc_locate(struct ubifs_info *c, const union ubifs_key *key,
		     void *node, int *lnum, int *offs)
{
	int found, n, err;
	struct ubifs_znode *znode;
	struct ubifs_zbranch zbr, *zt;

	mutex_lock(&c->tnc_mutex);
	found = lookup_level0(c, key, &znode, &n);
	if (!found) {
		err = -ENOENT;
		goto out;
	} else if (found < 0) {
		err = found;
		goto out;
	}
	zt = &znode->zbranch[n];
	if (is_hash_key(c, key)) {
		/*
		 * In this case the leaf-node-cache gets used, so we pass the
		 * address of the zbranch and keep the mutex locked
		 */
		*lnum = zt->lnum;
		*offs = zt->offs;
		err = tnc_read_node(c, zt, node);
		goto out;
	}
	zbr = znode->zbranch[n];
	mutex_unlock(&c->tnc_mutex);

	*lnum = zbr.lnum;
	*offs = zbr.offs;

	err = tnc_read_node(c, &zbr, node);
	return err;

out:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * do_lookup_nm- look up a "hashed" node.
 * directory entry file-system node.
 * @c: UBIFS file-system description object
 * @key: node key to lookup
 * @node: the node is returned here
 * @nm: node name
 *
 * This function look up and reads a node which contains name hash in the key.
 * Since the hash may have collisions, there may be many nodes with the same
 * key, so we have to sequentially look to all of them until the needed one is
 * found. This function returns zero in case of success, %-ENOENT if the node
 * was not found, and a negative error code in case of failure.
 */
static int do_lookup_nm(struct ubifs_info *c, const union ubifs_key *key,
			void *node, const struct qstr *nm)
{
	int found, n, err;
	struct ubifs_znode *znode;
	struct ubifs_zbranch zbr;

	dbg_tnc_key(c, key, "key");
	mutex_lock(&c->tnc_mutex);
	found = lookup_level0(c, key, &znode, &n);
	if (!found) {
		err = -ENOENT;
		goto out;
	} else if (found < 0) {
		err = found;
		goto out;
	}

	ubifs_assert(n >= 0);

	err = resolve_collision(c, key, &znode, &n, nm);
	if (err < 0)
		goto out;
	if (err == 0) {
		err = -ENOENT;
		goto out;
	}

	zbr = znode->zbranch[n];
	mutex_unlock(&c->tnc_mutex);

	err = tnc_read_node(c, &zbr, node);

	return err;

out:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_lookup_nm- look up a "hashed" node.
 * directory entry file-system node.
 * @c: UBIFS file-system description object
 * @key: node key to lookup
 * @node: the node is returned here
 * @nm: node name
 *
 * This function look up and reads a node which contains name hash in the key.
 * Since the hash may have collisions, there may be many nodes with the same
 * key, so we have to sequentially look to all of them until the needed one is
 * found. This function returns zero in case of success, %-ENOENT if the node
 * was not found, and a negative error code in case of failure.
 */
int ubifs_tnc_lookup_nm(struct ubifs_info *c, const union ubifs_key *key,
			void *node, const struct qstr *nm)
{
	int err, len;
	const struct ubifs_dent_node *dent = node;

	/*
	 * We assume that in most of the cases there are no name collisions and
	 * 'ubifs_tnc_lookup()' returns us the right direntry.
	 */
	err = ubifs_tnc_lookup(c, key, node);
	if (err)
		return err;

	len = le16_to_cpu(dent->nlen);
	if (nm->len == len && !memcmp(dent->name, nm->name, len))
		return 0;

	/*
	 * Unluckily, there are hash collisions and we have to iterate over
	 * them look at each direntry with colliding name hash sequentially.
	 */
	return do_lookup_nm(c, key, node, nm);
}

/**
 * correct_parent_keys - correct parent znodes' keys.
 * @c: UBIFS file-system description object
 * @znode: znode to correct parent znodes for
 *
 * This is a helper function for 'tnc_insert()'. When the key of the leftmost
 * zbranch changes, keys of parent znodes have to be corrected. This helper
 * function is called in such situations and corrects the keys if needed.
 */
static void correct_parent_keys(const struct ubifs_info *c,
				struct ubifs_znode *znode)
{
	union ubifs_key *key, *key1;

	ubifs_assert(znode->parent);
	ubifs_assert(znode->iip == 0);

	key = &znode->zbranch[0].key;
	key1 = &znode->parent->zbranch[0].key;

	while (keys_cmp(c, key, key1) < 0) {
		key_copy(c, key, key1);
		znode = znode->parent;
		if (!znode->parent || znode->iip)
			break;
		key1 = &znode->parent->zbranch[0].key;
	}
}

/**
 * insert_zbranch - insert a zbranch into a znode.
 * @znode: znode into which to insert
 * @zbr: zbranch to insert
 * @n: slot number to insert to
 *
 * This is a helper function for 'tnc_insert()'. UBIFS does not allow "gaps" in
 * znode's array of zbranches and keeps zbranches consolidated, so when a new
 * zbranch has to be inserted to the @znode->zbranches[]' array at the @n-th
 * slot, zbranches starting from @n have to be moved right.
 */
static void insert_zbranch(struct ubifs_znode *znode,
			   const struct ubifs_zbranch *zbr, int n)
{
	int i;

	ubifs_assert(ubifs_zn_dirty(znode));

	if (znode->level) {
		for (i = znode->child_cnt; i > n; i--) {
			znode->zbranch[i] = znode->zbranch[i - 1];
			if (znode->zbranch[i].znode)
				znode->zbranch[i].znode->iip = i;
		}
		if (zbr->znode)
			zbr->znode->iip = n;
	} else
		for (i = znode->child_cnt; i > n; i--)
			znode->zbranch[i] = znode->zbranch[i - 1];

	znode->zbranch[n] = *zbr;
	znode->child_cnt += 1;
	/*
	 * After inserting at slot zero, the lower bound of the key range of
	 * this znode may have changed. If this znode is subsequently split
	 * then the upper bound of the key range may change, and furthermore
	 * it could change to be lower than the original lower bound. If that
	 * happens, then it will no longer be possible to find this znode in the
	 * TNC using the key from the index node on flash. That is bad because
	 * if it is not found, we will assume it is obsolete and may overwrite
	 * it. Then if there is an unclean unmount, we will start using the
	 * old index which will be broken.
	 *
	 * So we first mark znodes that have insertions at slot zero, and then
	 * if they are split we add their lnum/offs to the old_idx tree.
	 */
	if (n == 0)
		znode->alt = 1;
}

/**
 * tnc_insert - insert a node into TNC.
 * @c: UBIFS file-system description object
 * @znode: znode to insert into
 * @zbr: branch to insert
 * @n: slot number to insert new zbranch to
 *
 * This function inserts a new node described by @zbr into znode @znode. If
 * znode does not have a free slot for new zbranch, it is split. Parent znodes
 * are splat as well if needed. Returns zero in case of success or a negative
 * error code in case of failure.
 */
static int tnc_insert(struct ubifs_info *c, struct ubifs_znode *znode,
		      struct ubifs_zbranch *zbr, int n)
{
	struct ubifs_znode *zn, *zi, *zp;
	int i, keep, move, appending = 0;
	union ubifs_key *key = &zbr->key;

	ubifs_assert(n >= 0 && n <= c->fanout);

	/* Implement naive insert for now */
again:
	zp = znode->parent;
	if (znode->child_cnt < c->fanout) {
		ubifs_assert(n != c->fanout);
		dbg_tnc_key(c, key, "inserted at %d level %d, key ", n,
			    znode->level);

		insert_zbranch(znode, zbr, n);

		/* Ensure parent's key is correct */
		if (n == 0 && zp && znode->iip == 0)
			correct_parent_keys(c, znode);

		return 0;
	}

	/*
	 * Unfortunately, @znode does not have more empty slots and we have to
	 * split it.
	 */
	dbg_tnc_key(c, key, "splitting level %d, key ", znode->level);

	if (znode->alt)
		/*
		 * We can no longer be sure of finding this znode by key, so we
		 * record it in the old_idx tree.
		 */
		ins_clr_old_idx_znode(c, znode);

	zn = kzalloc(c->max_znode_sz, GFP_NOFS);
	if (!zn)
		return -ENOMEM;
	zn->parent = zp;
	zn->level = znode->level;

	/* Decide where to split */
	if (znode->level == 0 && n == c->fanout &&
	    key_type(c, key) == UBIFS_DATA_KEY) {
		union ubifs_key *key1;

		/*
		 * If this is an inode which is being appended - do not split
		 * it because no other zbranches can be inserted between
		 * zbranches of consecutive data nodes anyway.
		 */
		key1 = &znode->zbranch[n - 1].key;
		if (key_ino(c, key1) == key_ino(c, key) &&
		    key_type(c, key1) == UBIFS_DATA_KEY &&
		    key_block(c, key1) == key_block(c, key) - 1)
			appending = 1;
	}

	if (appending) {
		keep = c->fanout;
		move = 0;
	} else {
		keep = (c->fanout + 1) / 2;
		move = c->fanout - keep;
	}

	/*
	 * Although we don't at present, we could look at the neighbors and see
	 * if we can move some zbranches there.
	 */

	if (n < keep) {
		/* Insert into existing znode */
		zi = znode;
		move += 1;
		keep -= 1;
	} else {
		/* Insert into new znode */
		zi = zn;
		n -= keep;
		/* Re-parent */
		if (zn->level != 0)
			zbr->znode->parent = zn;
	}

	set_bit(DIRTY_ZNODE, &zn->flags);
	atomic_long_inc(&c->dirty_zn_cnt);

	zn->child_cnt = move;
	znode->child_cnt = keep;

	dbg_tnc("moving %d, keeping %d", move, keep);

	/* Move zbranch */
	for (i = 0; i < move; i++) {
		zn->zbranch[i] = znode->zbranch[keep + i];
		/* Re-parent */
		if (zn->level != 0)
			if (zn->zbranch[i].znode) {
				zn->zbranch[i].znode->parent = zn;
				zn->zbranch[i].znode->iip = i;
			}
	}

	/* Insert new key and branch */
	dbg_tnc_key(c, key, "inserting at %d level %d, key ", n,
		    zn->level);

	insert_zbranch(zi, zbr, n);

	/* Insert new znode (produced by spitting) into the parent */
	if (zp) {
		i = n;
		/* Locate insertion point */
		n = znode->iip + 1;
		if (appending && n != c->fanout)
			appending = 0;

		if (i == 0 && zi == znode && znode->iip == 0)
			correct_parent_keys(c, znode);

		/* Tail recursion */
		zbr->key = zn->zbranch[0].key;
		zbr->znode = zn;
		zbr->lnum = 0;
		zbr->offs = 0;
		zbr->len = 0;
		znode = zp;

		goto again;
	}

	/* We have to split root znode */
	dbg_tnc("creating new zroot at level %d", znode->level + 1);

	zi = kzalloc(c->max_znode_sz, GFP_NOFS);
	if (!zi)
		return -ENOMEM;

	zi->child_cnt = 2;
	zi->level = znode->level + 1;

	set_bit(DIRTY_ZNODE, &zi->flags);
	atomic_long_inc(&c->dirty_zn_cnt);

	zi->zbranch[0].key = znode->zbranch[0].key;
	zi->zbranch[0].znode = znode;
	zi->zbranch[0].lnum = c->zroot.lnum;
	zi->zbranch[0].offs = c->zroot.offs;
	zi->zbranch[0].len = c->zroot.len;
	zi->zbranch[1].key = zn->zbranch[0].key;
	zi->zbranch[1].znode = zn;

	c->zroot.lnum = 0;
	c->zroot.offs = 0;
	c->zroot.len = 0;
	c->zroot.znode = zi;

	zn->parent = zi;
	zn->iip = 1;
	znode->parent = zi;
	znode->iip = 0;

	return 0;
}

/**
 * ubifs_tnc_add - add a node to TNC.
 * @c: UBIFS file-system description object
 * @key: key to add
 * @lnum: LEB number of node
 * @offs: node offset
 * @len: node length
 *
 * This function adds a node with key @key to TNC. The node may be new or it may
 * obsolete some existing one. Returns %0 on success or negative error code on
 * failure.
 */
int ubifs_tnc_add(struct ubifs_info *c, const union ubifs_key *key, int lnum,
		  int offs, int len)
{
	int found, n, err = 0;
	struct ubifs_znode *znode;

	mutex_lock(&c->tnc_mutex);
	found = lookup_level0_dirty(c, key, &znode, &n);
	if (!found) {
		struct ubifs_zbranch zbr;

		zbr.znode = NULL;
		zbr.lnum = lnum;
		zbr.offs = offs;
		zbr.len = len;
		zbr.key = *key;
		err = tnc_insert(c, znode, &zbr, n + 1);
	} else if (found == 1) {
		struct ubifs_zbranch *zbr = &znode->zbranch[n];

		lnc_free(zbr);
		err = ubifs_add_dirt(c, zbr->lnum, zbr->len);
		zbr->lnum = lnum;
		zbr->offs = offs;
		zbr->len = len;
	} else
		err = found;
	if (!err)
		err = dbg_check_tnc(c, 0);
	mutex_unlock(&c->tnc_mutex);

	return err;
}

/**
 * dirty_cow_bottom_up - dirty a znode and its ancestors.
 * @c: UBIFS file-system description object
 * @znode: znode to dirty
 *
 * If we do not have a unique key that resides in a znode, then we cannot
 * dirty that znode from the top down (i.e. by using lookup_level0_dirty)
 * This function records the path back to the last dirty ancestor, and then
 * dirties the znodes on that path.
 */
static struct ubifs_znode *dirty_cow_bottom_up(struct ubifs_info *c,
					       struct ubifs_znode *znode)
{
	struct ubifs_znode *zp;
	int *path = NULL, h, p = 0;

	ubifs_assert(c->zroot.znode != NULL);
	ubifs_assert(znode != NULL);
	h = c->zroot.znode->level;
	if (h) {
		path = kmalloc(sizeof(int) * h, GFP_NOFS);
		if (!path)
			return ERR_PTR(-ENOMEM);
		/* Go up until parent is dirty */
		while (1) {
			int n;

			zp = znode->parent;
			if (!zp)
				break;
			n = znode->iip;
			ubifs_assert(p < h);
			path[p++] = n;
			if (!zp->cnext && ubifs_zn_dirty(znode))
				break;
			znode = zp;
		}
	}
	/* Come back down, dirtying as we go */
	while (1) {
		struct ubifs_zbranch *zbr;

		zp = znode->parent;
		if (zp) {
			ubifs_assert(path[p - 1] >= 0);
			ubifs_assert(path[p - 1] < zp->child_cnt);
			zbr = &zp->zbranch[path[--p]];
			znode = dirty_cow_znode(c, zbr);
		} else {
			ubifs_assert(znode == c->zroot.znode);
			znode = dirty_cow_znode(c, &c->zroot);
		}
		if (IS_ERR(znode) || !p)
			break;
		ubifs_assert(path[p - 1] >= 0);
		ubifs_assert(path[p - 1] < znode->child_cnt);
		znode = znode->zbranch[path[p - 1]].znode;
	}
	kfree(path);
	return znode;
}

/**
 * ubifs_tnc_replace - replace a node in the TNC only if the old node is found.
 * @c: UBIFS file-system description object
 * @key: key to add
 * @old_lnum: LEB number of old node
 * @old_offs: old node offset
 * @lnum: LEB number of node
 * @offs: node offset
 * @len: node length
 *
 * This function replaces a node with key @key in the TNC only if the old node
 * is found.  This function is called by garbage collection when node are moved.
 * Returns %0 on success or negative error code on failure.
 */
int ubifs_tnc_replace(struct ubifs_info *c, const union ubifs_key *key,
		      int old_lnum, int old_offs, int lnum, int offs, int len)
{
	int found, n, err = 0;
	struct ubifs_znode *znode;

	mutex_lock(&c->tnc_mutex);
	found = lookup_level0_dirty(c, key, &znode, &n);
	if (found < 0) {
		err = found;
		goto out;
	} else if (found == 1) {
		struct ubifs_zbranch *zbr = &znode->zbranch[n];

		found = 0;
		if (zbr->lnum == old_lnum && zbr->offs == old_offs) {
			lnc_free(zbr);
			err = ubifs_add_dirt(c, zbr->lnum, zbr->len);
			if (err)
				goto out;
			zbr->lnum = lnum;
			zbr->offs = offs;
			zbr->len = len;
			found = 1;
		} else if (is_hash_key(c, key)) {
			found = resolve_collision_directly(c, key, &znode, &n,
							   old_lnum, old_offs);
			if (found == -ENOENT)
				found = 0;
			if (found < 0) {
				err = found;
				goto out;
			} else if (found) {
				/* Ensure the znode is dirtied */
				if (znode->cnext || !ubifs_zn_dirty(znode)) {
					    znode = dirty_cow_bottom_up(c,
									znode);
					    if (IS_ERR(znode)) {
						    err = PTR_ERR(znode);
						    goto out;
					    }
				}
				zbr = &znode->zbranch[n];
				lnc_free(zbr);
				err = ubifs_add_dirt(c, zbr->lnum,
						     zbr->len);
				if (err)
					goto out;
				zbr->lnum = lnum;
				zbr->offs = offs;
				zbr->len = len;
			}
		}
	}

	if (found == 0) {
		err = ubifs_add_dirt(c, lnum, len);
		if (err)
			goto out;
	}

	err = dbg_check_tnc(c, 0);

out:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_add_nm - add a "hashed" node to TNC.
 * @c: UBIFS file-system description object
 * @key: key to add
 * @lnum: LEB number of node
 * @offs: node offset
 * @len: node length
 * @nm: node name
 *
 * This is the same as 'ubifs_tnc_add()' but it should be used with keys which
 * may have collisions, like directory entry keys.
 */
int ubifs_tnc_add_nm(struct ubifs_info *c, const union ubifs_key *key,
		     int lnum, int offs, int len, const struct qstr *nm)
{
	int found, n, err = 0;
	struct ubifs_znode *znode;

	mutex_lock(&c->tnc_mutex);
	found = lookup_level0_dirty(c, key, &znode, &n);
	if (found < 0) {
		err = found;
		goto out;
	}
	if (found == 1) {
		if (c->replaying)
			found = fallible_resolve_collision(c, key, &znode, &n,
							   nm);
		else
			found = resolve_collision(c, key, &znode, &n, nm);
		if (found < 0 && found != -ENOENT) {
			err = found;
			goto out;
		}
		/* Ensure the znode is dirtied */
		if (znode->cnext || !ubifs_zn_dirty(znode)) {
			    znode = dirty_cow_bottom_up(c, znode);
			    if (IS_ERR(znode)) {
				    err = PTR_ERR(znode);
				    goto out;
			    }
		}
		if (found == 0)
			n -= 1;
		else if (found == -ENOENT)
			found = 0;
		else if (found == 1) {
			struct ubifs_zbranch *zbr = &znode->zbranch[n];

			lnc_free(zbr);
			err = ubifs_add_dirt(c, zbr->lnum, zbr->len);
			zbr->lnum = lnum;
			zbr->offs = offs;
			zbr->len = len;
			goto out;
		}
	}
	if (!found) {
		struct ubifs_zbranch zbr;

		zbr.znode = NULL;
		zbr.lnum = lnum;
		zbr.offs = offs;
		zbr.len = len;
		zbr.key = *key;
		err = tnc_insert(c, znode, &zbr, n + 1);
	}

out:
	if (!err)
		err = dbg_check_tnc(c, 0);
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * tnc_delete - delete a znode form TNC.
 * @c: UBIFS file-system description object
 * @znode: znode to delete from
 * @n: zbranch slot number to delete
 *
 * This function deletes a leaf node from @n-th slot of @znode. Returns zero in
 * case of success and a negative error code in case of failure.
 */
static int tnc_delete(struct ubifs_info *c, struct ubifs_znode *znode, int n)
{
	struct ubifs_zbranch *zbr;
	struct ubifs_znode *zp;
	int i, err;

	/* Delete without merge for now */
	ubifs_assert(znode->level == 0);
	ubifs_assert(n >= 0 && n < c->fanout);
	dbg_tnc_key(c, &znode->zbranch[n].key, "deleting");

	zbr = &znode->zbranch[n];
	lnc_free(zbr);

	err = ubifs_add_dirt(c, zbr->lnum, zbr->len);
	if (err) {
		dbg_dump_znode(c, znode);
		return err;
	}

	/* We do not "gap" zbranch slots */
	for (i = n; i < znode->child_cnt - 1; i++)
		znode->zbranch[i] = znode->zbranch[i + 1];
	znode->child_cnt -= 1;

	if (znode->child_cnt > 0)
		return 0;

	/*
	 * This was the last zbranch, we have to delete this znode from the
	 * parent.
	 */

	do {
		ubifs_assert(!test_bit(OBSOLETE_ZNODE, &znode->flags));
		ubifs_assert(ubifs_zn_dirty(znode));

		zp = znode->parent;
		n = znode->iip;

		atomic_long_dec(&c->dirty_zn_cnt);

		err = insert_old_idx_znode(c, znode);
		if (err)
			return err;

		if (znode->cnext) {
			set_bit(OBSOLETE_ZNODE, &znode->flags);
			atomic_long_inc(&c->clean_zn_cnt);
			atomic_long_inc(&ubifs_clean_zn_cnt);
		} else
			kfree(znode);
		znode = zp;
	} while (znode->child_cnt == 1); /* while removing last child */

	/* Remove from znode, entry n - 1 */
	znode->child_cnt -= 1;
	ubifs_assert(znode->level != 0);
	for (i = n; i < znode->child_cnt; i++) {
		znode->zbranch[i] = znode->zbranch[i + 1];
		if (znode->zbranch[i].znode)
			znode->zbranch[i].znode->iip = i;
	}

	/*
	 * If this is the root and it has only 1 child then
	 * collapse the tree.
	 */
	if (znode->parent == NULL) {
		while (znode->child_cnt == 1 && znode->level != 0) {
			zp = znode;
			zbr = &znode->zbranch[0];
			znode = get_znode(c, znode, 0);
			if (IS_ERR(znode))
				return PTR_ERR(znode);
			znode = dirty_cow_znode(c, zbr);
			if (IS_ERR(znode))
				return PTR_ERR(znode);
			znode->parent = NULL;
			znode->iip = 0;
			if (c->zroot.len) {
				err = insert_old_idx(c, c->zroot.lnum,
						     c->zroot.offs);
				if (err)
					return err;
			}
			c->zroot.lnum = zbr->lnum;
			c->zroot.offs = zbr->offs;
			c->zroot.len = zbr->len;
			c->zroot.znode = znode;
			ubifs_assert(!test_bit(OBSOLETE_ZNODE,
				     &zp->flags));
			ubifs_assert(test_bit(DIRTY_ZNODE, &zp->flags));
			atomic_long_dec(&c->dirty_zn_cnt);

			if (zp->cnext) {
				set_bit(OBSOLETE_ZNODE, &zp->flags);
				atomic_long_inc(&c->clean_zn_cnt);
				atomic_long_inc(&ubifs_clean_zn_cnt);
			} else
				kfree(zp);
		}
	}

	return 0;
}

/**
 * ubifs_tnc_remove - remove an index entry of a node.
 * @c: UBIFS file-system description object
 * @key: key of node
 *
 * Returns %0 on success or negative error code on failure.
 */
int ubifs_tnc_remove(struct ubifs_info *c, const union ubifs_key *key)
{
	int found, n, err = 0;
	struct ubifs_znode *znode;

	mutex_lock(&c->tnc_mutex);
	found = lookup_level0_dirty(c, key, &znode, &n);
	if (found == 1)
		err = tnc_delete(c, znode, n);
	else if (found < 0)
		err = found;
	if (!err)
		err = dbg_check_tnc(c, 0);
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_remove_nm - remove an index entry for a "hashed" node.
 * @c: UBIFS file-system description object
 * @key: key of node
 * @nm: directory entry name
 *
 * Returns %0 on success or negative error code on failure.
 */
int ubifs_tnc_remove_nm(struct ubifs_info *c, const union ubifs_key *key,
			const struct qstr *nm)
{
	int found, n, err = 0;
	struct ubifs_znode *znode;

	mutex_lock(&c->tnc_mutex);
	found = lookup_level0_dirty(c, key, &znode, &n);
	if (found < 0) {
		err = found;
		goto out;
	}
	if (found) {
		if (c->replaying)
			found = fallible_resolve_collision(c, key, &znode, &n,
							   nm);
		else
			found = resolve_collision(c, key, &znode, &n, nm);
		if (found == -ENOENT)
			found = 0;
		if (found < 0) {
			err = found;
			goto out;
		}
		if (found) {
			/* Ensure the znode is dirtied */
			if (znode->cnext || !ubifs_zn_dirty(znode)) {
				    znode = dirty_cow_bottom_up(c, znode);
				    if (IS_ERR(znode)) {
					    err = PTR_ERR(znode);
					    goto out;
				    }
			}
			err = tnc_delete(c, znode, n);
		}
	}
out:
	if (!err)
		err = dbg_check_tnc(c, 0);
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * key_in_range - determine if a key falls within a range of keys.
 * @c: UBIFS file-system description object
 * @key: key to check
 * @from_key: lowest key in range
 * @to_key: highest key in range
 *
 * This function returns %1 if the key is in range and %0 otherwise.
 */
static int key_in_range(struct ubifs_info *c, union ubifs_key *key,
			union ubifs_key *from_key, union ubifs_key *to_key)
{
	if (keys_cmp(c, key, from_key) < 0)
		return 0;
	if (keys_cmp(c, key, to_key) > 0)
		return 0;
	return 1;
}

/**
 * ubifs_tnc_remove_range - remove index entries in range.
 * @c: UBIFS file-system description object
 * @from_key: lowest key to remove
 * @to_key: highest key to remove
 *
 * This function removes index entries starting at @from_key and ending at
 * @to_key.  This function returns zero in case of success and a negative error
 * code in case of failure.
 */
int ubifs_tnc_remove_range(struct ubifs_info *c, union ubifs_key *from_key,
			   union ubifs_key *to_key)
{
	int found, i, n, k, err = 0;
	struct ubifs_znode *znode;
	union ubifs_key *key;

	mutex_lock(&c->tnc_mutex);
	while (1) {
		/* Find first level 0 znode that contains keys to remove */
		found = lookup_level0(c, from_key, &znode, &n);
		if (found < 0) {
			err = found;
			goto out;
		}
		if (found)
			key = from_key;
		else {
			err = tnc_next(c, &znode, &n);
			if (err == -ENOENT) {
				err = 0;
				goto out;
			}
			if (err < 0)
				goto out;
			key = &znode->zbranch[n].key;
			if (!key_in_range(c, key, from_key, to_key)) {
				err = 0;
				goto out;
			}
		}
		/* Ensure the znode is dirtied */
		if (znode->cnext || !ubifs_zn_dirty(znode)) {
			    znode = dirty_cow_bottom_up(c, znode);
			    if (IS_ERR(znode)) {
				    err = PTR_ERR(znode);
				    goto out;
			    }
		}
		/* Remove all keys in range except the first */
		for (i = n + 1, k = 0; i < znode->child_cnt; i++, k++) {
			key = &znode->zbranch[i].key;
			if (!key_in_range(c, key, from_key, to_key))
				break;
			lnc_free(&znode->zbranch[i]);
			err = ubifs_add_dirt(c, znode->zbranch[i].lnum,
					     znode->zbranch[i].len);
			if (err) {
				dbg_dump_znode(c, znode);
				goto out;
			}
			dbg_tnc_key(c, key, "removing");
		}
		if (k) {
			for (i = n + 1 + k; i < znode->child_cnt; i++)
				znode->zbranch[i - k] = znode->zbranch[i];
			znode->child_cnt -= k;
		}
		/* Now delete the first */
		err = tnc_delete(c, znode, n);
		if (err)
			goto out;
	}
out:
	if (!err)
		err = dbg_check_tnc(c, 0);
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * ubifs_tnc_remove_ino - remove an inode from TNC.
 * @c: UBIFS file-system description object
 * @inum: inode number to remove
 *
 * This function remove inode @inum and all the extended attributes associated
 * with the anode from TNC and returns zero in case of success or a negative
 * error code in case of failure.
 */
int ubifs_tnc_remove_ino(struct ubifs_info *c, ino_t inum)
{
	union ubifs_key key1, key2;
	struct ubifs_dent_node *xent, *pxent = NULL;
	struct qstr nm = { .name = NULL };

	dbg_tnc("ino %lu", inum);

	/*
	 * Walk all extended attribute entries and remove them together with
	 * corresponding extended attribute inodes.
	 */
	lowest_xent_key(c, &key1, inum);
	while (1) {
		ino_t xattr_inum;
		int err;

		xent = ubifs_tnc_next_ent(c, &key1, &nm);
		if (IS_ERR(xent)) {
			err = PTR_ERR(xent);
			if (err == -ENOENT)
				break;
			return err;
		}

		xattr_inum = le64_to_cpu(xent->inum);
		dbg_tnc("xent '%s', ino %lu", xent->name, xattr_inum);

		nm.name = xent->name;
		nm.len = le16_to_cpu(xent->nlen);
		err = ubifs_tnc_remove_nm(c, &key1, &nm);
		if (err) {
			kfree(xent);
			return err;
		}

		lowest_ino_key(c, &key1, xattr_inum);
		highest_ino_key(c, &key2, xattr_inum);
		err = ubifs_tnc_remove_range(c, &key1, &key2);
		if (err) {
			kfree(xent);
			return err;
		}

		kfree(pxent);
		pxent = xent;
		key_read(c, &xent->key, &key1);
	}

	kfree(pxent);
	lowest_ino_key(c, &key1, inum);
	highest_ino_key(c, &key2, inum);

	return ubifs_tnc_remove_range(c, &key1, &key2);
}

/**
 * ubifs_tnc_next_ent - walk directory or extended attribute entries.
 * @c: UBIFS file-system description object
 * @key: key of last entry
 * @nm: name of last entry found or %NULL
 *
 * This function finds and reads the next directory or extended attribute entry
 * after the given key (@key) if there is one. @name is used to resolve
 * collisions. If the fist entry has to be found, @key has to contain the
 * lowest possible key value for this inode and @name has to be %NULL.
 *
 * This function returns the found directory or extended attribute entry node
 * in case of success, %-ENOENT is returned if no entry is found, or a negative
 * error code in case of failure.
 */
struct ubifs_dent_node *ubifs_tnc_next_ent(struct ubifs_info *c,
					   union ubifs_key *key,
					   const struct qstr *nm)
{
	int found, n, err, type = key_type(c, key), dlen = 0;
	struct ubifs_znode *znode;
	struct ubifs_dent_node *dent = NULL;
	struct ubifs_zbranch *zbr;
	union ubifs_key *dkey;

	dbg_tnc_key(c, key, "%s",
		    ((nm && nm->name) ? (char *)nm->name : "(lowest)"));
	ubifs_assert(type == UBIFS_DENT_KEY || type == UBIFS_XENT_KEY);

	mutex_lock(&c->tnc_mutex);
	found = lookup_level0(c, key, &znode, &n);
	if (found < 0) {
		err = found;
		goto out;
	}

	/* Handle collisions */
	if (found && nm && nm->name) {
		err = resolve_collision(c, key, &znode, &n, nm);
		if (err < 0)
			goto out;
		if (err == 0)
			goto name_not_found;
	}

again:
	/* Now find next entry */
	err = tnc_next(c, &znode, &n);
	if (err)
		goto out;

name_not_found:
	dkey = &znode->zbranch[n].key;
	zbr = &znode->zbranch[n];

	if (key_ino(c, dkey) != key_ino(c, key) ||
	    key_type(c, dkey) != type) {
		err = -ENOENT;
		goto out;
	}

	if (!dent || dlen < zbr->len) {
		kfree(dent);
		dlen = zbr->len;
		dent = kmalloc(dlen, GFP_NOFS);
		if (!dent) {
			err = -ENOMEM;
			goto out;
		}
	}

	err = tnc_read_node(c, zbr, dent);
	if (err)
		goto out;

	if (dent->inum == 0)
		goto again;

	mutex_unlock(&c->tnc_mutex);
	return dent;

out:
	kfree(dent);
	mutex_unlock(&c->tnc_mutex);
	return ERR_PTR(err);
}

/**
 * tnc_postorder_first - find first znode to do postorder tree traversal.
 * @znode: znode to start at (root of the sub-tree to traverse)
 *
 * Find the lowest leftmost znode in a subtree of the TNC tree. The LNC is
 * ignored.
 */
static struct ubifs_znode *tnc_postorder_first(struct ubifs_znode *znode)
{
	if (unlikely(!znode))
		return NULL;

	while (znode->level > 0) {
		struct ubifs_znode *child;

		child = ubifs_tnc_find_child(znode, 0);
		if (!child)
			return znode;
		znode = child;
	}

	return znode;
}

/**
 * tnc_postorder_next - next TNC tree element in postorder traversal.
 * @znode: previous znode
 *
 * This function implements postorder TNC traversal. The LNC is ignored.
 * Returns the next element or %NULL if @znode is already the last one.
 */
static struct ubifs_znode *tnc_postorder_next(struct ubifs_znode *znode)
{
	struct ubifs_znode *zn;

	ubifs_assert(znode);
	if (unlikely(!znode->parent))
		return NULL;

	/* Switch to the next index in the parent */
	zn = ubifs_tnc_find_child(znode->parent, znode->iip + 1);
	if (!zn)
		/* This is in fact the last child, return parent */
		return znode->parent;

	/* Go to the first znode in this new subtree */
	return tnc_postorder_first(zn);
}

/**
 * ubifs_destroy_tnc_subtree - destroy all znodes connected to a subtree.
 * @znode: znode defining subtree to destroy
 *
 * This function destroys subtree of the TNC tree. Returns number of clean
 * znodes in the subtree.
 */
long ubifs_destroy_tnc_subtree(struct ubifs_znode *znode)
{
	struct ubifs_znode *zn = tnc_postorder_first(znode);
	long clean_freed = 0;
	int n;

	ubifs_assert(zn);
	while (1) {
		for (n = 0; n < zn->child_cnt; n++) {
			if (!zn->zbranch[n].znode)
				continue;

			if (zn->level > 0 &&
			    !ubifs_zn_dirty(zn->zbranch[n].znode))
				clean_freed += 1;

			cond_resched();
			kfree(zn->zbranch[n].znode);
		}

		if (zn == znode) {
			if (!ubifs_zn_dirty(zn))
				clean_freed += 1;
			kfree(zn);
			return clean_freed;
		}

		zn = tnc_postorder_next(zn);
	}
}

/**
 * tnc_destroy_cnext - destroy left-over obsolete znodes from a failed commit.
 * @c: UBIFS file-system description object
 *
 * Destroy left-over obsolete znodes from a failed commit.
 */
static void tnc_destroy_cnext(struct ubifs_info *c)
{
	struct ubifs_znode *cnext;

	if (!c->cnext)
		return;
	ubifs_assert(c->cmt_state == COMMIT_BROKEN);
	cnext = c->cnext;
	do {
		struct ubifs_znode *znode = cnext;

		cnext = cnext->cnext;
		if (test_bit(OBSOLETE_ZNODE, &znode->flags))
			kfree(znode);
	} while (cnext != NULL && cnext != c->cnext);
}

/**
 * ubifs_tnc_close - close TNC subsystem and free all related resources.
 * @c: UBIFS file-system description object
 */
void ubifs_tnc_close(struct ubifs_info *c)
{
	long clean_freed;

	tnc_destroy_cnext(c);
	if (c->zroot.znode) {
		clean_freed = ubifs_destroy_tnc_subtree(c->zroot.znode);
		atomic_long_sub(clean_freed, &ubifs_clean_zn_cnt);
	}
	kfree(c->cbuf);
	kfree(c->gap_lebs);
	kfree(c->ilebs);
	destroy_old_idx(c);
}

/**
 * left_znode - get the znode to the left.
 * @c: UBIFS file-system description object
 * @znode: znode
 *
 * This function returns a pointer to the znode to the left of @znode or NULL if
 * there is not one. A negative error code is returned on failure.
 */
static struct ubifs_znode *left_znode(struct ubifs_info *c,
				      struct ubifs_znode *znode)
{
	int level = znode->level;

	while (1) {
		int n = znode->iip - 1;

		/* Go up until we can go left */
		znode = znode->parent;
		if (!znode)
			return NULL;
		if (n >= 0) {
			/* Now go down the rightmost branch to 'level' */
			znode = get_znode(c, znode, n);
			if (IS_ERR(znode))
				return znode;
			while (znode->level != level) {
				n = znode->child_cnt - 1;
				znode = get_znode(c, znode, n);
				if (IS_ERR(znode))
					return znode;
			}
			break;
		}
	}
	return znode;
}

/**
 * right_znode - get the znode to the right.
 * @c: UBIFS file-system description object
 * @znode: znode
 *
 * This function returns a pointer to the znode to the right of @znode or NULL
 * if there is not one. A negative error code is returned on failure.
 */
static struct ubifs_znode *right_znode(struct ubifs_info *c,
				       struct ubifs_znode *znode)
{
	int level = znode->level;

	while (1) {
		int n = znode->iip + 1;

		/* Go up until we can go right */
		znode = znode->parent;
		if (!znode)
			return NULL;
		if (n < znode->child_cnt) {
			/* Now go down the leftmost branch to 'level' */
			znode = get_znode(c, znode, n);
			if (IS_ERR(znode))
				return znode;
			while (znode->level != level) {
				znode = get_znode(c, znode, 0);
				if (IS_ERR(znode))
					return znode;
			}
			break;
		}
	}
	return znode;
}

/**
 * lookup_znode - find a particular znode.
 * @c: UBIFS file-system description object
 * @key: index node key
 * @level: index node level
 * @lnum: index node LEB number
 * @offs: index node offset
 *
 * This function returns a pointer to the znode found or NULL if it is not
 * found. A negative error code is returned on failure.
 */
static struct ubifs_znode *lookup_znode(struct ubifs_info *c,
					union ubifs_key *key, int level,
					int lnum, int offs)
{
	struct ubifs_znode *znode, *zn;
	int n, nn;

	/*
	 * The arguments have probably been read off flash, so don't assume
	 * they are valid.
	 */
	if (level < 0)
		return ERR_PTR(-EINVAL);

	/* Get the root znode */
	znode = c->zroot.znode;
	if (!znode) {
		znode = load_znode(c, &c->zroot, NULL, 0);
		if (IS_ERR(znode))
			return znode;
	}
	/* Check if it is the one we are looking for */
	if (c->zroot.lnum == lnum && c->zroot.offs == offs)
		return znode;
	/* Descend to the parent level i.e. (level + 1) */
	if (level >= znode->level)
		return NULL;
	while (1) {
		search_zbranch(c, znode, key, &n);
		if (n < 0)
			return NULL;
		if (znode->level == level + 1)
			break;
		znode = get_znode(c, znode, n);
		if (IS_ERR(znode))
			return znode;
	}
	/* Check if the child is the one we are looking for */
	if (znode->zbranch[n].lnum == lnum && znode->zbranch[n].offs == offs)
		return get_znode(c, znode, n);
	/* If the key is unique, there is nowhere else to look */
	if (!is_hash_key(c, key))
		return NULL;
	/*
	 * The key is not unique and so may be also in the znodes to either
	 * side.
	 */
	zn = znode;
	nn = n;
	/* Look left */
	while (1) {
		/* Move one branch to the left */
		if (n)
			n -= 1;
		else {
			znode = left_znode(c, znode);
			if (znode == NULL)
				break;
			if (IS_ERR(znode))
				return znode;
			n = znode->child_cnt - 1;
		}
		/* Check it */
		if (znode->zbranch[n].lnum == lnum &&
		    znode->zbranch[n].offs == offs)
			return get_znode(c, znode, n);
		/* Stop if the key is less than the one we are looking for */
		if (keys_cmp(c, &znode->zbranch[n].key, key) < 0)
			break;
	}
	/* Back to the middle */
	znode = zn;
	n = nn;
	/* Look right */
	while (1) {
		/* Move one branch to the right */
		if (++n >= znode->child_cnt) {
			znode = right_znode(c, znode);
			if (znode == NULL)
				break;
			if (IS_ERR(znode))
				return znode;
			n = 0;
		}
		/* Check it */
		if (znode->zbranch[n].lnum == lnum &&
		    znode->zbranch[n].offs == offs)
			return get_znode(c, znode, n);
		/* Stop if the key is greater than the one we are looking for */
		if (keys_cmp(c, &znode->zbranch[n].key, key) > 0)
			break;
	}
	return NULL;
}

/**
 * is_idx_node_in_tnc - determine if an index node is in the TNC.
 * @c: UBIFS file-system description object
 * @key: key of index node
 * @level: index node level
 * @lnum: LEB number of index node
 * @offs: offset of index node
 *
 * This function returns %0 if the index node is not referred to in the TNC.
 * This function returns %1 if the index node is referred to in the TNC and the
 * corresponding znode is dirty.
 * This function returns %2 if an index node is referred to in the TNC and the
 * corresponding znode is clean.
 * Otherwise, this function returns a negative error code.
 *
 * For index nodes, the key is the key of the first child.
 *
 * This function relies on the fact that 0:0 is never a valid LEB number and
 * offset for a main-area node.
 */
int is_idx_node_in_tnc(struct ubifs_info *c, union ubifs_key *key, int level,
		       int lnum, int offs)
{
	struct ubifs_znode *znode;

	znode = lookup_znode(c, key, level, lnum, offs);
	if (znode == NULL)
		return 0;
	if (IS_ERR(znode))
		return PTR_ERR(znode);
	if (ubifs_zn_dirty(znode))
		return 1;
	else
		return 2;
}

/**
 * is_node_clean - determine if a node is clean.
 * @c: UBIFS file-system description object
 * @key: node key
 * @lnum: node LEB number
 * @offs: node offset
 *
 * This function returns %1 if a node is referred to in the TNC and %0
 * if it is not.  Otherwise a negative error code is returned.
 *
 * This function relies on the fact that 0:0 is never a valid LEB number and
 * offset for a main-area node.
 */
static int is_node_clean(struct ubifs_info *c, union ubifs_key *key,
			     int lnum, int offs)
{
	struct ubifs_zbranch *zbr;
	struct ubifs_znode *znode, *zn;
	int n, found, err, nn;
	const int unique = !is_hash_key(c, key);

	found = lookup_level0(c, key, &znode, &n);
	if (found < 0)
		return found; /* Error code */
	if (!found)
		return 0;
	zbr = &znode->zbranch[n];
	if (lnum == zbr->lnum && offs == zbr->offs)
		return 1; /* Found it */
	if (unique)
		return 0;
	/*
	 * Because the key is not unique, we have to look left
	 * and right as well
	 */
	zn = znode;
	nn = n;
	/* Look left */
	while (1) {
		err = tnc_prev(c, &znode, &n);
		if (err == -ENOENT)
			break;
		if (err)
			return err;
		if (keys_cmp(c, key, &znode->zbranch[n].key))
			break;
		zbr = &znode->zbranch[n];
		if (lnum == zbr->lnum && offs == zbr->offs)
			return 1; /* Found it */
	}
	/* Look right */
	znode = zn;
	n = nn;
	while (1) {
		err = tnc_next(c, &znode, &n);
		if (err) {
			if (err == -ENOENT)
				return 0;
			return err;
		}
		if (keys_cmp(c, key, &znode->zbranch[n].key))
			break;
		zbr = &znode->zbranch[n];
		if (lnum == zbr->lnum && offs == zbr->offs)
			return 1; /* Found it */
	}
	return 0;
}

/**
 * ubifs_tnc_has_node - determine whether a node is in the TNC.
 * @c: UBIFS file-system description object
 * @key: node key
 * @level: index node level (if it is an index node)
 * @lnum: node LEB number
 * @offs: node offset
 * @is_idx: non-zero if the node is an index node
 *
 * This function returns %1 if a node is in the TNC and %0 if it is not.
 * Otherwise a negative error code is returned.
 * For index nodes, the key is the key of the first child.
 * An index node is considered to be in the TNC only if the corresponding znode
 * is clean or has not been loaded.
 */
int ubifs_tnc_has_node(struct ubifs_info *c, union ubifs_key *key, int level,
		       int lnum, int offs, int is_idx)
{
	int ret;

	mutex_lock(&c->tnc_mutex);
	if (is_idx) {
		ret = is_idx_node_in_tnc(c, key, level, lnum, offs);
		if (ret < 0)
			goto out; /* Error code */
		if (ret == 1)
			/* The index node was found but it was dirty */
			ret = 0;
		else if (ret == 2)
			/* The index node was found and it was clean */
			ret = 1;
		else if (ret != 0)
			BUG();
	} else
		ret = is_node_clean(c, key, lnum, offs);
out:
	mutex_unlock(&c->tnc_mutex);
	return ret;
}

/**
 * ubifs_dirty_idx_node - dirty an index node.
 * @c: UBIFS file-system description object
 * @key: index node key
 * @level: index node level
 * @lnum: index node LEB number
 * @offs: index node offset
 *
 * This function loads and dirties an index node so that it can be garbage
 * collected.
 *
 * For index nodes, the key is the key of the first child.
 *
 * This function relies on the fact that 0:0 is never a valid LEB number and
 * offset for a main-area node.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_dirty_idx_node(struct ubifs_info *c, union ubifs_key *key, int level,
			 int lnum, int offs)
{
	struct ubifs_znode *znode;
	int err = 0;

	mutex_lock(&c->tnc_mutex);
	znode = lookup_znode(c, key, level, lnum, offs);
	if (!znode)
		goto out;
	if (IS_ERR(znode)) {
		err = PTR_ERR(znode);
		goto out;
	}
	znode = dirty_cow_bottom_up(c, znode);
	if (IS_ERR(znode)) {
		err = PTR_ERR(znode);
		goto out;
	}
out:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

#ifdef CONFIG_UBIFS_FS_DEBUG_CHK_TNC

/**
 * dbg_check_znode - check if znode is all right.
 * @c: UBIFS file-system description object
 * @zbr: zbranch which points to this znode
 *
 * This function makes sure that znode referred to by @zbr is all right.
 * Returns zero if it is, and %-EINVAL if it is not.
 */
static int dbg_check_znode(const struct ubifs_info *c,
			   const struct ubifs_zbranch *zbr)
{
	const struct ubifs_znode *znode = zbr->znode;
	const struct ubifs_znode *zp = znode->parent;
	int n, err, cmp;

	if (znode->child_cnt <= 0 || znode->child_cnt > c->fanout) {
		err = 1;
		goto out;
	}
	if (znode->level < 0) {
		err = 2;
		goto out;
	}
	if (znode->iip < 0 || znode->iip >= c->fanout) {
		err = 3;
		goto out;
	}

	if (zbr->len == 0)
		/* Only dirty zbranch may have no on-flash nodes */
		if (!ubifs_zn_dirty(znode)) {
			err = 4;
			goto out;
		}

	if (ubifs_zn_dirty(znode))
		/* If znode is dirty, its parent has to be dirty as well */
		if (zp && !ubifs_zn_dirty(zp))
			/*
			 * The dirty flag is atomic and is cleared outside the
			 * TNC mutex, so znode's dirty flag may now have
			 * been cleared. The child is always cleared before the
			 * parent, so we just need to check again.
			 */
			if (ubifs_zn_dirty(znode)) {
				err = 5;
				goto out;
			}

	if (zp) {
		const union ubifs_key *min, *max;

		if (znode->level != zp->level - 1) {
			err = 6;
			goto out;
		}

		/* Make sure the 'parent' pointer in our znode is correct */
		err = search_zbranch(c, zp, &zbr->key, &n);
		if (!err) {
			/* This zbranch does not exist in the parent */
			err = 7;
			goto out;
		}

		if (znode->iip != n) {
			err = 8;
			goto out;
		}

		/*
		 * Make sure that the first key in our znode is greater than or
		 * equal to the key in the pointing zbranch.
		 */
		min = &zbr->key;
		cmp = keys_cmp(c, min, &znode->zbranch[0].key);
		if (cmp == 1) {
			err = 9;
			goto out;
		}

		if (n + 1 < zp->child_cnt) {
			max = &zp->zbranch[n + 1].key;

			/*
			 * Make sure the last key in our znode is less than the
			 * the key in zbranch which goes after our pointing
			 * zbranch.
			 */
			cmp = keys_cmp(c, max,
				&znode->zbranch[znode->child_cnt - 1].key);
			if (cmp == -1) {
				err = 10;
				goto out;
			}
		}
	} else {
		/* This may only be root znode */
		if (zbr != &c->zroot) {
			err = 11;
			goto out;
		}
	}

	/*
	 * Make sure that next key is greater or equivalent then the previous
	 * one.
	 */
	for (n = 1; n < znode->child_cnt; n++) {
		cmp = keys_cmp(c, &znode->zbranch[n].key,
			       &znode->zbranch[n - 1].key);
		if (cmp < 0) {
			err = 12;
			goto out;
		}
		if (cmp == 0)
			/* This can only be keys with colliding hash */
			if (!is_hash_key(c, &znode->zbranch[n].key)) {
				err = 13;
				goto out;
			}
	}

	for (n = 0; n < znode->child_cnt; n++) {
		if (znode->zbranch[n].znode == NULL &&
		    (znode->zbranch[n].lnum == 0 ||
		     znode->zbranch[n].len == 0)) {
			err = 14;
			goto out;
		}

		if (znode->zbranch[n].lnum != 0 &&
		    znode->zbranch[n].len == 0) {
			err = 15;
			goto out;
		}

		if (znode->zbranch[n].lnum == 0 &&
		    znode->zbranch[n].len != 0) {
			err = 16;
			goto out;
		}

		if (znode->zbranch[n].lnum == 0 &&
		    znode->zbranch[n].offs != 0) {
			err = 17;
			goto out;
		}

		if (znode->level != 0 && znode->zbranch[n].znode)
			if (znode->zbranch[n].znode->parent != znode) {
				err = 18;
				goto out;
			}
	}

	return 0;

out:
	ubifs_err("failed, error %d", err);
	ubifs_msg("dump of the znode");
	dbg_dump_znode(c, znode);
	if (zp) {
		ubifs_msg("dump of the parent znode");
		dbg_dump_znode(c, zp);
	}
	dump_stack();
	return -EINVAL;
}

/**
 * dbg_check_tnc - check TNC tree.
 * @c: UBIFS file-system description object
 * @extra: do extra checks that are possible at start commit
 *
 * This function traverses whole TNC tree and checks every znode. Returns zero
 * if everything is all right and %-EINVAL if something is wrong with TNC.
 */
int dbg_check_tnc(struct ubifs_info *c, int extra)
{
	struct ubifs_znode *znode;
	long clean_cnt = 0, dirty_cnt = 0;
	int err;

	ubifs_assert(mutex_is_locked(&c->tnc_mutex));
	if (!c->zroot.znode)
		return 0;

	znode = tnc_postorder_first(c->zroot.znode);
	while (znode) {
		const struct ubifs_zbranch *zbr;

		if (!znode->parent)
			zbr = &c->zroot;
		else
			zbr = &znode->parent->zbranch[znode->iip];

		err = dbg_check_znode(c, zbr);
		if (err)
			return err;

		if (extra) {
			if (ubifs_zn_dirty(znode))
				dirty_cnt += 1;
			else
				clean_cnt += 1;
		}

		znode = tnc_postorder_next(znode);
	}

	if (extra) {
		if (clean_cnt != atomic_long_read(&c->clean_zn_cnt)) {
			ubifs_err("incorrect clean_zn_cnt %ld, calculated %ld",
				  atomic_long_read(&c->clean_zn_cnt),
				  clean_cnt);
			return -EINVAL;
		}
		if (dirty_cnt != atomic_long_read(&c->dirty_zn_cnt)) {
			ubifs_err("incorrect dirty_zn_cnt %ld, calculated %ld",
				  atomic_long_read(&c->dirty_zn_cnt),
				  dirty_cnt);
			return -EINVAL;
		}
	}

	return 0;
}

#endif /* CONFIG_UBIFS_FS_DEBUG_CHK_TNC */

#ifdef CONFIG_UBIFS_FS_DEBUG

/**
 * dbg_walk_sub_tree - walk index subtree.
 * @c: UBIFS file-system description object
 * @znode: root znode of the subtree to walk
 * @leaf_cb: called for each leaf node
 * @znode_cb: called for each indexing node
 * @priv: private date which is passed to callbacks
 *
 * This is a helper function which recursively walks the UBIFS index, reading
 * each indexing node from the media if needed. Returns zero in case of success
 * and a negative error code in case of failure.
 */
static int dbg_walk_sub_tree(struct ubifs_info *c, struct ubifs_znode *znode,
			     dbg_leaf_callback leaf_cb,
			     dbg_znode_callback znode_cb, void *priv)
{
	int n, err;

	cond_resched();

	if (znode_cb) {
		err = znode_cb(c, znode, priv);
		if (err)
			return err;
	}

	if (znode->level == 0) {
		if (!leaf_cb)
			return 0;

		for (n = 0; n < znode->child_cnt; n++) {
			struct ubifs_zbranch *zbr = &znode->zbranch[n];

			err = leaf_cb(c, zbr, priv);
			if (err)
				return err;
		}
	} else
		for (n = 0; n < znode->child_cnt; n++) {
			struct ubifs_znode *zn;

			zn = get_znode(c, znode, n);
			if (IS_ERR(zn))
				return PTR_ERR(zn);
			err = dbg_walk_sub_tree(c, zn, leaf_cb, znode_cb, priv);
			if (err)
				return err;
		}

	return 0;
}

/**
 * dbg_walk_index - walk the on-flash index.
 * @c: UBIFS file-system description object
 * @leaf_cb: called for each leaf node
 * @znode_cb: called for each indexing node
 * @priv: private date which is passed to callbacks
 *
 * This function walks the UBIFS index and calls the @leaf_cb for each leaf
 * node and @znode_cb for each indexing node. Returns zero in case of success
 * and a negative error code in case of failure.
 *
 * Because 'dbg_walk_sub_tree()' is recursive, it runs the risk of exceeding the
 * stack space.
 *
 * It would be better if this function removed every znode it pulled to into
 * the TNC, so that the behaviour more closely matched the non-debugging
 * behaviour.
 */
int dbg_walk_index(struct ubifs_info *c, dbg_leaf_callback leaf_cb,
		   dbg_znode_callback znode_cb, void *priv)
{
	int err = 0;

	mutex_lock(&c->tnc_mutex);
	if (!c->zroot.znode) {
		c->zroot.znode = load_znode(c, &c->zroot, NULL, 0);
		if (IS_ERR(c->zroot.znode)) {
			err = PTR_ERR(c->zroot.znode);
			c->zroot.znode = NULL;
			goto out;
		}
	}

	err = dbg_walk_sub_tree(c, c->zroot.znode, leaf_cb, znode_cb, priv);

out:
	mutex_unlock(&c->tnc_mutex);
	return err;
}

/**
 * dbg_read_leaf_nolock - read a leaf node.
 * @c: UBIFS file-system description object
 * @zbr:  key and position of node
 * @node: node returned
 *
 * This function reads leaf defined node by @zbr and returns zero in case of
 * success or a negative negative error code in case of failure.
 */
int dbg_read_leaf_nolock(struct ubifs_info *c, struct ubifs_zbranch *zbr,
			 void *node)
{
	return tnc_read_node(c, zbr, node);
}

#endif /* CONFIG_UBIFS_FS_DEBUG */

#ifdef CONFIG_UBIFS_FS_DEBUG_CHK_IDX_SZ

static int dbg_add_size(struct ubifs_info *c, struct ubifs_znode *znode,
			void *priv)
{
	long long *idx_size = priv;
	int add;

	add = ubifs_idx_node_sz(c, znode->child_cnt);
	add = ALIGN(add, 8);
	*idx_size += add;
	return 0;
}

int dbg_check_idx_size(struct ubifs_info *c, long long idx_size)
{
	int err;
	long long calc = 0;


	err = dbg_walk_index(c, NULL, dbg_add_size, &calc);
	if (err) {
		ubifs_err("error %d while walking the index", err);
		return err;
	}

	if (calc != idx_size) {
		ubifs_err("index size check failed");
		ubifs_err("calculated size is %lld, should be %lld",
			  calc, idx_size);
		dump_stack();
		return -EINVAL;
	}

	return 0;
}

#endif /* CONFIG_UBIFS_FS_DEBUG_CHK_IDX_SZ */
