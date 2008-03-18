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
 *          Artem Bityutskiy
 */

#include <linux/crc16.h>
#include "ubifs.h"

#ifdef CONFIG_UBIFS_FS_DEBUG_CHK_LPROPS
static int dbg_check_ltab(struct ubifs_info *c);
#else
#define dbg_check_ltab(c) 0
#endif

#if defined(CONFIG_UBIFS_FS_DEBUG_CHK_LPROPS) || \
    defined(CONFIG_UBIFS_FS_DEBUG_CHK_OTHER)
static int dbg_chk_nodes(struct ubifs_info *c, struct ubifs_cnode *cnode,
			 int row, int col);
#else
#define dbg_chk_nodes(c, cnode, row, col) 0
#endif

#define dbg_lpt dbg_lp

/**
 * do_calc_lpt_geom - calculate sizes for the LPT area.
 * @c: the UBIFS file-system description object
 *
 * Calculate the sizes of LPT bit fields, nodes, and tree, based on the
 * properties of the flash and whether LPT is "big" (c->big_lpt).
 */
static void do_calc_lpt_geom(struct ubifs_info *c)
{
	int n, bits, per_leb_wastage;
	long long sz, tot_wastage;

	c->pnode_cnt = DIV_ROUND_UP(c->main_lebs, UBIFS_LPT_FANOUT);

	n = DIV_ROUND_UP(c->pnode_cnt, UBIFS_LPT_FANOUT);
	c->nnode_cnt = n;
	while (n > 1) {
		n = DIV_ROUND_UP(n, UBIFS_LPT_FANOUT);
		c->nnode_cnt += n;
	}

	c->lpt_hght = 1;
	n = UBIFS_LPT_FANOUT;
	while (n < c->pnode_cnt) {
		c->lpt_hght += 1;
		n <<= UBIFS_LPT_FANOUT_SHIFT;
	}

	c->space_bits = fls(c->leb_size) - 3;
	c->lpt_lnum_bits = fls(c->lpt_lebs);
	c->lpt_offs_bits = fls(c->leb_size - 1);
	c->lpt_spc_bits = fls(c->leb_size);

	n = DIV_ROUND_UP(c->max_leb_cnt, UBIFS_LPT_FANOUT);
	c->pcnt_bits = fls(n - 1);

	c->lnum_bits = fls(c->max_leb_cnt - 1);

	bits = UBIFS_LPT_CRC_BITS + UBIFS_LPT_TYPE_BITS +
	       (c->big_lpt ? c->pcnt_bits : 0) +
	       (c->space_bits * 2 + 1) * UBIFS_LPT_FANOUT;
	c->pnode_sz = (bits + 7) / 8;

	bits = UBIFS_LPT_CRC_BITS + UBIFS_LPT_TYPE_BITS +
	       (c->big_lpt ? c->pcnt_bits : 0) +
	       (c->lpt_lnum_bits + c->lpt_offs_bits) * UBIFS_LPT_FANOUT;
	c->nnode_sz = (bits + 7) / 8;

	bits = UBIFS_LPT_CRC_BITS + UBIFS_LPT_TYPE_BITS +
	       c->lpt_lebs * c->lpt_spc_bits * 2;
	c->ltab_sz = (bits + 7) / 8;

	bits = UBIFS_LPT_CRC_BITS + UBIFS_LPT_TYPE_BITS +
	       c->lnum_bits * c->lsave_cnt;
	c->lsave_sz = (bits + 7) / 8;

	/* Calculate the minimum LPT size */
	c->lpt_sz = (long long)c->pnode_cnt * c->pnode_sz;
	c->lpt_sz += (long long)c->nnode_cnt * c->nnode_sz;
	c->lpt_sz += c->ltab_sz;
	c->lpt_sz += c->lsave_sz;

	/* Add wastage */
	sz = c->lpt_sz;
	per_leb_wastage = max_t(int, c->pnode_sz, c->nnode_sz);
	sz += per_leb_wastage;
	tot_wastage = per_leb_wastage;
	while (sz > c->leb_size) {
		sz += per_leb_wastage;
		sz -= c->leb_size;
		tot_wastage += per_leb_wastage;
	}
	tot_wastage += ALIGN(sz, c->min_io_size) - sz;
	c->lpt_sz += tot_wastage;
}

/**
 * ubifs_calc_lpt_geom - calculate and check sizes for the LPT area.
 * @c: the UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_calc_lpt_geom(struct ubifs_info *c)
{
	int lebs_needed;
	long long sz;

	do_calc_lpt_geom(c);

	/* Verify that lpt_lebs is big enough */
	sz = c->lpt_sz * 2; /* Must have at least 2 times the size */
	sz += c->leb_size - 1;
	do_div(sz, c->leb_size);
	lebs_needed = sz;
	if (lebs_needed > c->lpt_lebs) {
		ubifs_err("too few LPT LEBs");
		return -EINVAL;
	}

	/* Verify that ltab fits in a single LEB (since ltab is a single node */
	if (c->ltab_sz > c->leb_size) {
		ubifs_err("LPT ltab too big");
		return -EINVAL;
	}

	return 0;
}

/**
 * calc_dflt_lpt_geom - calculate default LPT geometry.
 * @c: the UBIFS file-system description object
 * @main_lebs: number of main area LEBs is passed and returned here
 * @big_lpt: whether the LPT area is "big" is returned here
 *
 * The size of the LPT area depends on parameters that themselves are dependent
 * on the size of the LPT area. This function, successively recalculates the LPT
 * area geometry until the parameters and resultant geometry are consistent.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int calc_dflt_lpt_geom(struct ubifs_info *c, int *main_lebs,
			      int *big_lpt)
{
	int i, lebs_needed;
	long long sz;

	/* Start by assuming the minimum number of LPT LEBs */
	c->lpt_lebs = UBIFS_MIN_LPT_LEBS;
	c->main_lebs = *main_lebs - c->lpt_lebs;
	if (c->main_lebs <= 0)
		return -EINVAL;

	/* And assume we will use the small LPT model */
	c->big_lpt = 0;

	/*
	 * Calculate the geometry based on assumptions above and then see if it
	 * makes sense
	 */
	do_calc_lpt_geom(c);

	/* Small LPT model must have lpt_sz < leb_size */
	if (c->lpt_sz > c->leb_size) {
		/* Nope, so try again using big LPT model */
		c->big_lpt = 1;
		do_calc_lpt_geom(c);
	}

	/* Now check there are enough LPT LEBs */
	for (i = 0; i < 64 ; i++) {
		sz = c->lpt_sz * 4; /* Allow 4 times the size */
		sz += c->leb_size - 1;
		do_div(sz, c->leb_size);
		lebs_needed = sz;
		if (lebs_needed > c->lpt_lebs) {
			/* Not enough LPT LEBs so try again with more */
			c->lpt_lebs = lebs_needed;
			c->main_lebs = *main_lebs - c->lpt_lebs;
			if (c->main_lebs <= 0)
				return -EINVAL;
			do_calc_lpt_geom(c);
			continue;
		}
		if (c->ltab_sz > c->leb_size) {
			ubifs_err("LPT ltab too big");
			return -EINVAL;
		}
		*main_lebs = c->main_lebs;
		*big_lpt = c->big_lpt;
		return 0;
	}
	return -EINVAL;
}

/**
 * unmap_leb - unmap a LEB.
 * @c: UBIFS file-system description object
 * @lnum: LEB number to unmap
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int unmap_leb(struct ubifs_info *c, int lnum)
{
	int err;

	err = ubi_leb_unmap(c->ubi, lnum);
	if (err) {
		ubifs_err("unmap LEB %d failed, error %d", lnum, err);
		return err;
	}
	return 0;
}

/**
 * write_leb - write to a LEB.
 * @c: UBIFS file-system description object
 * @lnum: LEB number to write
 * @buf: buffer to write from
 * @offs: offset within buffer and within LEB to write to
 * @len: length to write
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int write_leb(struct ubifs_info *c, int lnum, void *buf, int offs,
		     int len)
{
	int err;

	err = ubi_leb_write(c->ubi, lnum, buf + offs, offs, len, UBI_SHORTTERM);
	if (err) {
		ubifs_err("writing %d bytes at %d:%d, error %d",
			  len, lnum, offs, err);
		return err;
	}
	dbg_lpt("LPT wrote %d bytes at %d:%d", len, lnum, offs);
	return 0;
}

/**
 * pack_bits - pack bit fields end-to-end.
 * @addr: address at which to pack (passed and next address returned)
 * @pos: bit position at which to pack (passed and next position returned)
 * @val: value to pack
 * @nrbits: number of bits of value to pack (1-32)
 */
static void pack_bits(uint8_t **addr, int *pos, uint32_t val, int nrbits)
{
	uint8_t *p = *addr;
	int b = *pos;

	ubifs_assert(nrbits > 0);
	ubifs_assert(nrbits <= 32);
	ubifs_assert(*pos >= 0);
	ubifs_assert(*pos < 8);
	ubifs_assert((val >> nrbits) == 0 || nrbits == 32);
	if (b) {
		*p |= ((uint8_t)val) << b;
		nrbits += b;
		if (nrbits > 8) {
			*++p = (uint8_t)(val >>= (8 - b));
			if (nrbits > 16) {
				*++p = (uint8_t)(val >>= 8);
				if (nrbits > 24) {
					*++p = (uint8_t)(val >>= 8);
					if (nrbits > 32)
						*++p = (uint8_t)(val >>= 8);
				}
			}
		}
	} else {
		*p = (uint8_t)val;
		if (nrbits > 8) {
			*++p = (uint8_t)(val >>= 8);
			if (nrbits > 16) {
				*++p = (uint8_t)(val >>= 8);
				if (nrbits > 24)
					*++p = (uint8_t)(val >>= 8);
			}
		}
	}
	b = nrbits & 7;
	if (b == 0)
		p++;
	*addr = p;
	*pos = b;
}

/**
 * unpack_bits - unpack bit fields.
 * @addr: address at which to unpack (passed and next address returned)
 * @pos: bit position at which to unpack (passed and next position returned)
 * @nrbits: number of bits of value to unpack (1-32)
 *
 * This functions returns the value unpacked.
 */
static uint32_t unpack_bits(uint8_t **addr, int *pos, int nrbits)
{
	const int k = 32 - nrbits;
	uint8_t *p = *addr;
	int b = *pos;
	uint32_t val;

	ubifs_assert(nrbits > 0);
	ubifs_assert(nrbits <= 32);
	ubifs_assert(*pos >= 0);
	ubifs_assert(*pos < 8);
	if (b) {
		val = p[1] | ((uint32_t)p[2] << 8) | ((uint32_t)p[3] << 16) |
		      ((uint32_t)p[4] << 24);
		val <<= (8 - b);
		val |= *p >> b;
		nrbits += b;
	} else
		val = p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
		      ((uint32_t)p[3] << 24);
	val <<= k;
	val >>= k;
	b = nrbits & 7;
	p += nrbits / 8;
	*addr = p;
	*pos = b;
	ubifs_assert((val >> nrbits) == 0 || nrbits - b == 32);
	return val;
}

/**
 * first_nnode - find the first nnode in memory.
 * @c: UBIFS file-system description object
 * @hght: height of tree where nnode found is returned here
 *
 * This function returns a pointer to the nnode found or %NULL if no nnode is
 * found. This function is a helper to 'ubifs_lpt_free()'.
 */
static struct ubifs_nnode *first_nnode(struct ubifs_info *c, int *hght)
{
	struct ubifs_nnode *nnode;
	int h, i, found;

	nnode = c->nroot;
	*hght = 0;
	if (!nnode)
		return NULL;
	for (h = 1; h < c->lpt_hght; h++) {
		found = 0;
		for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
			if (nnode->nbranch[i].nnode) {
				found = 1;
				nnode = nnode->nbranch[i].nnode;
				*hght = h;
				break;
			}
		}
		if (!found)
			break;
	}
	return nnode;
}

/**
 * next_nnode - find the next nnode in memory.
 * @c: UBIFS file-system description object
 * @nnode: nnode from which to start.
 * @hght: height of tree where nnode is, is passed and returned here
 *
 * This function returns a pointer to the nnode found or %NULL if no nnode is
 * found. This function is a helper to 'ubifs_lpt_free()'.
 */
static struct ubifs_nnode *next_nnode(struct ubifs_info *c,
				      struct ubifs_nnode *nnode, int *hght)
{
	struct ubifs_nnode *parent;
	int iip, h, i, found;

	parent = nnode->parent;
	if (!parent)
		return NULL;
	if (nnode->iip == UBIFS_LPT_FANOUT - 1) {
		*hght -= 1;
		return parent;
	}
	for (iip = nnode->iip + 1; iip < UBIFS_LPT_FANOUT; iip++) {
		nnode = parent->nbranch[iip].nnode;
		if (nnode)
			break;
	}
	if (!nnode) {
		*hght -= 1;
		return parent;
	}
	for (h = *hght + 1; h < c->lpt_hght; h++) {
		found = 0;
		for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
			if (nnode->nbranch[i].nnode) {
				found = 1;
				nnode = nnode->nbranch[i].nnode;
				*hght = h;
				break;
			}
		}
		if (!found)
			break;
	}
	return nnode;
}

/**
 * free_obsolete_cnodes - free obsolete cnodes for commit end.
 * @c: UBIFS file-system description object
 */
static void free_obsolete_cnodes(struct ubifs_info *c)
{
	struct ubifs_cnode *cnode, *cnext;

	cnext = c->lpt_cnext;
	if (!cnext)
		return;
	do {
		cnode = cnext;
		cnext = cnode->cnext;
		if (test_bit(OBSOLETE_CNODE, &cnode->flags))
			kfree(cnode);
		else
			cnode->cnext = NULL;
	} while (cnext != c->lpt_cnext);
	c->lpt_cnext = NULL;
}

/**
 * ubifs_lpt_free - free resources owned by the LPT.
 * @c: UBIFS file-system description object
 * @wr_only: free only resources used for writing
 */
void ubifs_lpt_free(struct ubifs_info *c, int wr_only)
{
	struct ubifs_nnode *nnode;
	int i, hght;

	/* Free write-only things first */

	free_obsolete_cnodes(c); /* Leftover from a failed commit */

	vfree(c->ltab_cmt);
	c->ltab_cmt = NULL;
	vfree(c->lpt_buf);
	c->lpt_buf = NULL;
	kfree(c->lsave);
	c->lsave = NULL;

	if (wr_only)
		return;

	/* Now free the rest */

	nnode = first_nnode(c, &hght);
	while (nnode) {
		for (i = 0; i < UBIFS_LPT_FANOUT; i++)
			kfree(nnode->nbranch[i].nnode);
		nnode = next_nnode(c, nnode, &hght);
	}
	for (i = 0; i < LPROPS_HEAP_CNT; i++)
		kfree(c->lpt_heap[i].arr);
	kfree(c->dirty_idx.arr);
	kfree(c->nroot);
	vfree(c->ltab);
	kfree(c->lpt_nod_buf);
}

/**
 * pack_pnode - pack all the bit fields of a pnode.
 * @c: UBIFS file-system description object
 * @buf: buffer into which to pack
 * @pnode: pnode to pack
 */
static void pack_pnode(struct ubifs_info *c, void *buf,
		       struct ubifs_pnode *pnode)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0;
	uint16_t crc;

	pack_bits(&addr, &pos, UBIFS_LPT_PNODE, UBIFS_LPT_TYPE_BITS);
	if (c->big_lpt)
		pack_bits(&addr, &pos, pnode->num, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		pack_bits(&addr, &pos, pnode->lprops[i].free >> 3,
			  c->space_bits);
		pack_bits(&addr, &pos, pnode->lprops[i].dirty >> 3,
			  c->space_bits);
		if (pnode->lprops[i].flags & LPROPS_INDEX)
			pack_bits(&addr, &pos, 1, 1);
		else
			pack_bits(&addr, &pos, 0, 1);
	}
	crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
		    c->pnode_sz - UBIFS_LPT_CRC_BYTES);
	addr = buf;
	pos = 0;
	pack_bits(&addr, &pos, crc, UBIFS_LPT_CRC_BITS);
}

/**
 * pack_nnode - pack all the bit fields of a nnode.
 * @c: UBIFS file-system description object
 * @buf: buffer into which to pack
 * @nnode: nnode to pack
 */
static void pack_nnode(struct ubifs_info *c, void *buf,
		       struct ubifs_nnode *nnode)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0;
	uint16_t crc;

	pack_bits(&addr, &pos, UBIFS_LPT_NNODE, UBIFS_LPT_TYPE_BITS);
	if (c->big_lpt)
		pack_bits(&addr, &pos, nnode->num, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		int lnum = nnode->nbranch[i].lnum;

		if (lnum == 0)
			lnum = c->lpt_last + 1;
		pack_bits(&addr, &pos, lnum - c->lpt_first, c->lpt_lnum_bits);
		pack_bits(&addr, &pos, nnode->nbranch[i].offs,
			  c->lpt_offs_bits);
	}
	crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
		    c->nnode_sz - UBIFS_LPT_CRC_BYTES);
	addr = buf;
	pos = 0;
	pack_bits(&addr, &pos, crc, UBIFS_LPT_CRC_BITS);
}

/**
 * pack_ltab - pack the LPT's own lprops table.
 * @c: UBIFS file-system description object
 * @buf: buffer into which to pack
 * @ltab: LPT's own lprops table to pack
 */
static void pack_ltab(struct ubifs_info *c, void *buf,
			 struct ubifs_lpt_lprops *ltab)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0;
	uint16_t crc;

	pack_bits(&addr, &pos, UBIFS_LPT_LTAB, UBIFS_LPT_TYPE_BITS);
	for (i = 0; i < c->lpt_lebs; i++) {
		pack_bits(&addr, &pos, ltab[i].free, c->lpt_spc_bits);
		pack_bits(&addr, &pos, ltab[i].dirty, c->lpt_spc_bits);
	}
	crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
		    c->ltab_sz - UBIFS_LPT_CRC_BYTES);
	addr = buf;
	pos = 0;
	pack_bits(&addr, &pos, crc, UBIFS_LPT_CRC_BITS);
}

/**
 * pack_lsave - pack the LPT's save table.
 * @c: UBIFS file-system description object
 * @buf: buffer into which to pack
 * @lsave: LPT's save table to pack
 */
static void pack_lsave(struct ubifs_info *c, void *buf, int *lsave)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0;
	uint16_t crc;

	pack_bits(&addr, &pos, UBIFS_LPT_LSAVE, UBIFS_LPT_TYPE_BITS);
	for (i = 0; i < c->lsave_cnt; i++)
		pack_bits(&addr, &pos, lsave[i], c->lnum_bits);
	crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
		    c->lsave_sz - UBIFS_LPT_CRC_BYTES);
	addr = buf;
	pos = 0;
	pack_bits(&addr, &pos, crc, UBIFS_LPT_CRC_BITS);
}

/**
 * add_lpt_dirt - add dirty space to LPT LEB properties.
 * @c: UBIFS file-system description object
 * @lnum: LEB number to which to add dirty space
 * @dirty: amount of dirty space to add
 */
static void add_lpt_dirt(struct ubifs_info *c, int lnum, int dirty)
{
	if (!dirty || !lnum)
		return;
	dbg_lp("LEB %d add %d to %d",
	       lnum, dirty, c->ltab[lnum - c->lpt_first].dirty);
	ubifs_assert(lnum >= c->lpt_first && lnum <= c->lpt_last);
	c->ltab[lnum - c->lpt_first].dirty += dirty;
}

/**
 * set_ltab - set LPT LEB properties.
 * @c: UBIFS file-system description object
 * @lnum: LEB number
 * @free: amount of free space
 * @dirty: amount of dirty space
 */
static void set_ltab(struct ubifs_info *c, int lnum, int free, int dirty)
{
	dbg_lpt("LEB %d free %d dirty %d to %d %d",
		lnum, c->ltab[lnum - c->lpt_first].free,
		c->ltab[lnum - c->lpt_first].dirty, free, dirty);
	ubifs_assert(lnum >= c->lpt_first && lnum <= c->lpt_last);
	c->ltab[lnum - c->lpt_first].free = free;
	c->ltab[lnum - c->lpt_first].dirty = dirty;
}

/**
 * upd_ltab - update LPT LEB properties.
 * @c: UBIFS file-system description object
 * @lnum: LEB number
 * @free: amount of free space
 * @dirty: amount of dirty space to add
 */
static void upd_ltab(struct ubifs_info *c, int lnum, int free, int dirty)
{
	dbg_lpt("LEB %d free %d dirty %d to %d +%d",
		lnum, c->ltab[lnum - c->lpt_first].free,
		c->ltab[lnum - c->lpt_first].dirty, free, dirty);
	ubifs_assert(lnum >= c->lpt_first && lnum <= c->lpt_last);
	c->ltab[lnum - c->lpt_first].free = free;
	c->ltab[lnum - c->lpt_first].dirty += dirty;
}

/**
 * add_nnode_dirt - add dirty space to LPT LEB properties.
 * @c: UBIFS file-system description object
 * @nnode: nnode for which to add dirt
 */
static void add_nnode_dirt(struct ubifs_info *c, struct ubifs_nnode *nnode)
{
	struct ubifs_nnode *np = nnode->parent;

	if (np)
		add_lpt_dirt(c, np->nbranch[nnode->iip].lnum, c->nnode_sz);
	else {
		add_lpt_dirt(c, c->lpt_lnum, c->nnode_sz);
		if (!(c->lpt_drty_flgs & LTAB_DIRTY)) {
			c->lpt_drty_flgs |= LTAB_DIRTY;
			add_lpt_dirt(c, c->ltab_lnum, c->ltab_sz);
		}
	}
}

/**
 * add_pnode_dirt - add dirty space to LPT LEB properties.
 * @c: UBIFS file-system description object
 * @pnode: pnode for which to add dirt
 */
static void add_pnode_dirt(struct ubifs_info *c, struct ubifs_pnode *pnode)
{
	add_lpt_dirt(c, pnode->parent->nbranch[pnode->iip].lnum, c->pnode_sz);
}

/**
 * alloc_lpt_leb - allocate an LPT LEB that is empty.
 * @c: UBIFS file-system description object
 * @lnum: LEB number is passed and returned here
 *
 * This function finds the next empty LEB in the ltab starting from @lnum. If a
 * an empty LEB is found it is returned in @lnum and the function returns %0.
 * Otherwise the function returns -ENOSPC.  Note however, that LPT is designed
 * never to run out of space.
 */
static int alloc_lpt_leb(struct ubifs_info *c, int *lnum)
{
	int i, n;

	n = *lnum - c->lpt_first + 1;
	for (i = n; i < c->lpt_lebs; i++) {
		if (c->ltab[i].tgc || c->ltab[i].cmt)
			continue;
		if (c->ltab[i].free == c->leb_size) {
			c->ltab[i].cmt = 1;
			*lnum = i + c->lpt_first;
			return 0;
		}
	}
	for (i = 0; i < n; i++) {
		if (c->ltab[i].tgc || c->ltab[i].cmt)
			continue;
		if (c->ltab[i].free == c->leb_size) {
			c->ltab[i].cmt = 1;
			*lnum = i + c->lpt_first;
			return 0;
		}
	}
	dbg_err("last LEB %d", *lnum);
	dump_stack();
	return -ENOSPC;
}

/**
 * realloc_lpt_leb - allocate an LPT LEB that is empty.
 * @c: UBIFS file-system description object
 * @lnum: LEB number is passed and returned here
 *
 * This function duplicates exactly the results of the function alloc_lpt_leb.
 * It is used during end commit to reallocate the same LEB numbers that were
 * allocated by alloc_lpt_leb during start commit.
 *
 * This function finds the next LEB that was allocated by the alloc_lpt_leb
 * function starting from @lnum. If a LEB is found it is returned in @lnum and
 * the function returns %0. Otherwise the function returns -ENOSPC.
 * Note however, that LPT is designed never to run out of space.
 */
static int realloc_lpt_leb(struct ubifs_info *c, int *lnum)
{
	int i, n;

	n = *lnum - c->lpt_first + 1;
	for (i = n; i < c->lpt_lebs; i++)
		if (c->ltab[i].cmt) {
			c->ltab[i].cmt = 0;
			*lnum = i + c->lpt_first;
			return 0;
		}
	for (i = 0; i < n; i++)
		if (c->ltab[i].cmt) {
			c->ltab[i].cmt = 0;
			*lnum = i + c->lpt_first;
			return 0;
		}
	dbg_err("last LEB %d", *lnum);
	dump_stack();
	return -ENOSPC;
}

/**
 * calc_nnode_num - calculate nnode number.
 * @row: the row in the tree (root is zero)
 * @col: the column in the row (leftmost is zero)
 *
 * The nnode number is a number that uniquely identifies a nnode and can be used
 * easily to traverse the tree from the root to that nnode.
 *
 * This function calculates and returns the nnode number for the nnode at @row
 * and @col.
 */
static int calc_nnode_num(int row, int col)
{
	int num, bits;

	num = 1;
	while (row--) {
		bits = (col & (UBIFS_LPT_FANOUT - 1));
		col >>= UBIFS_LPT_FANOUT_SHIFT;
		num <<= UBIFS_LPT_FANOUT_SHIFT;
		num |= bits;
	}
	return num;
}

/**
 * calc_nnode_num_from_parent - calculate nnode number.
 * @c: UBIFS file-system description object
 * @parent: parent nnode
 * @iip: index in parent
 *
 * The nnode number is a number that uniquely identifies a nnode and can be used
 * easily to traverse the tree from the root to that nnode.
 *
 * This function calculates and returns the nnode number based on the parent's
 * nnode number and the index in parent.
 */
static int calc_nnode_num_from_parent(struct ubifs_info *c,
				      struct ubifs_nnode *parent, int iip)
{
	int num, shft;

	if (!parent)
		return 1;
	shft = (c->lpt_hght - parent->level) * UBIFS_LPT_FANOUT_SHIFT;
	num = parent->num ^ (1 << shft);
	num |= (UBIFS_LPT_FANOUT + iip) << shft;
	return num;
}

/**
 * calc_pnode_num_from_parent - calculate pnode number.
 * @c: UBIFS file-system description object
 * @parent: parent nnode
 * @iip: index in parent
 *
 * The pnode number is a number that uniquely identifies a pnode and can be used
 * easily to traverse the tree from the root to that pnode.
 *
 * This function calculates and returns the pnode number based on the parent's
 * nnode number and the index in parent.
 */
static int calc_pnode_num_from_parent(struct ubifs_info *c,
				      struct ubifs_nnode *parent, int iip)
{
	int i, n = c->lpt_hght - 1, pnum = parent->num, num = 0;

	for (i = 0; i < n; i++) {
		num <<= UBIFS_LPT_FANOUT_SHIFT;
		num |= pnum & (UBIFS_LPT_FANOUT - 1);
		pnum >>= UBIFS_LPT_FANOUT_SHIFT;
	}
	num <<= UBIFS_LPT_FANOUT_SHIFT;
	num |= iip;
	return num;
}

/**
 * ubifs_create_dflt_lpt - create default LPT.
 * @c: UBIFS file-system description object
 * @main_lebs: number of main area LEBs is passed and returned here
 * @lpt_first: LEB number of first LPT LEB
 * @lpt_lebs: number of LEBs for LPT is passed and returned here
 * @big_lpt: use big LPT model is passed and returned here
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_create_dflt_lpt(struct ubifs_info *c, int *main_lebs, int lpt_first,
			  int *lpt_lebs, int *big_lpt)
{
	int lnum, err = 0, node_sz, iopos, i, j, cnt, len, alen, row;
	int blnum, boffs, bsz, bcnt;
	struct ubifs_pnode *pnode = NULL;
	struct ubifs_nnode *nnode = NULL;
	void *buf = NULL, *p;
	struct ubifs_lpt_lprops *ltab = NULL;
	int *lsave = NULL;

	err = calc_dflt_lpt_geom(c, main_lebs, big_lpt);
	if (err)
		return err;
	*lpt_lebs = c->lpt_lebs;

	c->lpt_first = lpt_first; /* Needed by pack_nnode and set_ltab */
	c->lpt_last = lpt_first + c->lpt_lebs - 1; /* Needed by set_ltab */
	c->main_first = c->leb_cnt - *main_lebs; /* Needed by pack_lsave */

	pnode = kzalloc(sizeof(struct ubifs_pnode), GFP_KERNEL);
	nnode = kzalloc(sizeof(struct ubifs_nnode), GFP_KERNEL);
	buf = vmalloc(c->leb_size);
	ltab = vmalloc(sizeof(struct ubifs_lpt_lprops) * c->lpt_lebs);
	lsave = kmalloc(sizeof(int) * c->lsave_cnt, GFP_KERNEL);
	if (!pnode || !nnode || !buf || !ltab || !lsave) {
		err = -ENOMEM;
		goto out;
	}

	ubifs_assert(c->ltab == NULL);
	c->ltab = ltab; /* Needed by set_ltab */

	/* Initialize LPT's own lprops */
	for (i = 0; i < c->lpt_lebs; i++) {
		ltab[i].free = c->leb_size;
		ltab[i].dirty = 0;
		ltab[i].tgc = 0;
		ltab[i].cmt = 0;
	}

	lnum = lpt_first;
	p = buf;
	/* Number of leaf nodes (pnodes) */
	cnt = c->pnode_cnt;

	/*
	 * The first pnode contains the LEB properties for the LEBs that contain
	 * the root inode node and the root index node of the index tree.
	 */
	node_sz = ALIGN(ubifs_idx_node_sz(c, 1), 8);
	iopos = ALIGN(node_sz, c->min_io_size);
	pnode->lprops[0].free = c->leb_size - iopos;
	pnode->lprops[0].dirty = iopos - node_sz;
	pnode->lprops[0].flags = LPROPS_INDEX;

	node_sz = UBIFS_INO_NODE_SZ;
	iopos = ALIGN(node_sz, c->min_io_size);
	pnode->lprops[1].free = c->leb_size - iopos;
	pnode->lprops[1].dirty = iopos - node_sz;

	for (i = 2; i < UBIFS_LPT_FANOUT; i++)
		pnode->lprops[i].free = c->leb_size;

	/* Add first pnode */
	pack_pnode(c, p, pnode);
	p += c->pnode_sz;
	len = c->pnode_sz;
	pnode->num += 1;

	/* Reset pnode values for remaining pnodes */
	pnode->lprops[0].free = c->leb_size;
	pnode->lprops[0].dirty = 0;
	pnode->lprops[0].flags = 0;

	pnode->lprops[1].free = c->leb_size;
	pnode->lprops[1].dirty = 0;

	/*
	 * To calculate the internal node branches, we keep information about
	 * the level below.
	 */
	blnum = lnum; /* LEB number of level below */
	boffs = 0; /* Offset of level below */
	bcnt = cnt; /* Number of nodes in level below */
	bsz = c->pnode_sz; /* Size of nodes in level below */

	/* Add all remaining pnodes */
	for (i = 1; i < cnt; i++) {
		if (len + c->pnode_sz > c->leb_size) {
			alen = ALIGN(len, c->min_io_size);
			set_ltab(c, lnum, c->leb_size - alen, alen - len);
			memset(p, 0xff, alen - len);
			err = ubi_leb_change(c->ubi, lnum++, buf, alen,
					     UBI_SHORTTERM);
			if (err)
				goto out;
			p = buf;
			len = 0;
		}
		pack_pnode(c, p, pnode);
		p += c->pnode_sz;
		len += c->pnode_sz;
		/*
		 * pnodes are simply numbered left to right starting at zero,
		 * which means the pnode number can be used easily to traverse
		 * down the tree to the corresponding pnode.
		 */
		pnode->num += 1;
	}

	row = 0;
	for (i = UBIFS_LPT_FANOUT; cnt > i; i <<= UBIFS_LPT_FANOUT_SHIFT)
		row += 1;
	/* Add all nnodes, one level at a time */
	while (1) {
		/* Number of internal nodes (nnodes) at next level */
		cnt = DIV_ROUND_UP(cnt, UBIFS_LPT_FANOUT);
		for (i = 0; i < cnt; i++) {
			if (len + c->nnode_sz > c->leb_size) {
				alen = ALIGN(len, c->min_io_size);
				set_ltab(c, lnum, c->leb_size - alen,
					    alen - len);
				memset(p, 0xff, alen - len);
				err = ubi_leb_change(c->ubi, lnum++, buf, alen,
						     UBI_SHORTTERM);
				if (err)
					goto out;
				p = buf;
				len = 0;
			}
			/* Only 1 nnode at this level, so it is the root */
			if (cnt == 1) {
				c->lpt_lnum = lnum;
				c->lpt_offs = len;
			}
			/* Set branches to the level below */
			for (j = 0; j < UBIFS_LPT_FANOUT; j++) {
				if (bcnt) {
					if (boffs + bsz > c->leb_size) {
						blnum += 1;
						boffs = 0;
					}
					nnode->nbranch[j].lnum = blnum;
					nnode->nbranch[j].offs = boffs;
					boffs += bsz;
					bcnt--;
				} else {
					nnode->nbranch[j].lnum = 0;
					nnode->nbranch[j].offs = 0;
				}
			}
			nnode->num = calc_nnode_num(row, i);
			pack_nnode(c, p, nnode);
			p += c->nnode_sz;
			len += c->nnode_sz;
		}
		/* Only 1 nnode at this level, so it is the root */
		if (cnt == 1)
			break;
		/* Update the information about the level below */
		bcnt = cnt;
		bsz = c->nnode_sz;
		row -= 1;
	}

	if (*big_lpt) {
		/* Need to add LPT's save table */
		if (len + c->lsave_sz > c->leb_size) {
			alen = ALIGN(len, c->min_io_size);
			set_ltab(c, lnum, c->leb_size - alen, alen - len);
			memset(p, 0xff, alen - len);
			err = ubi_leb_change(c->ubi, lnum++, buf, alen,
					     UBI_SHORTTERM);
			if (err)
				goto out;
			p = buf;
			len = 0;
		}

		c->lsave_lnum = lnum;
		c->lsave_offs = len;

		for (i = 0; i < c->lsave_cnt && i < *main_lebs; i++)
			lsave[i] = c->main_first + i;
		for (; i < c->lsave_cnt; i++)
			lsave[i] = c->main_first;

		pack_lsave(c, p, lsave);
		p += c->lsave_sz;
		len += c->lsave_sz;
	}

	/* Need to add LPT's own LEB properties table */
	if (len + c->ltab_sz > c->leb_size) {
		alen = ALIGN(len, c->min_io_size);
		set_ltab(c, lnum, c->leb_size - alen, alen - len);
		memset(p, 0xff, alen - len);
		err = ubi_leb_change(c->ubi, lnum++, buf, alen, UBI_SHORTTERM);
		if (err)
			goto out;
		p = buf;
		len = 0;
	}

	c->ltab_lnum = lnum;
	c->ltab_offs = len;

	/* Update ltab before packing it */
	len += c->ltab_sz;
	alen = ALIGN(len, c->min_io_size);
	set_ltab(c, lnum, c->leb_size - alen, alen - len);

	pack_ltab(c, p, ltab);
	p += c->ltab_sz;

	/* Write remaining buffer */
	memset(p, 0xff, alen - len);
	err = ubi_leb_change(c->ubi, lnum, buf, alen, UBI_SHORTTERM);
	if (err)
		goto out;

	c->nhead_lnum = lnum;
	c->nhead_offs = ALIGN(len, c->min_io_size);

	dbg_lpt("space_bits %d", c->space_bits);
	dbg_lpt("lpt_lnum_bits %d", c->lpt_lnum_bits);
	dbg_lpt("lpt_offs_bits %d", c->lpt_offs_bits);
	dbg_lpt("lpt_spc_bits %d", c->lpt_spc_bits);
	dbg_lpt("pcnt_bits %d", c->pcnt_bits);
	dbg_lpt("lnum_bits %d", c->lnum_bits);
	dbg_lpt("pnode_sz %d", c->pnode_sz);
	dbg_lpt("nnode_sz %d", c->nnode_sz);
	dbg_lpt("ltab_sz %d", c->ltab_sz);
	dbg_lpt("lsave_sz %d", c->lsave_sz);
	dbg_lpt("lpt_hght %d", c->lpt_hght);
	dbg_lpt("big_lpt %d", c->big_lpt);
	dbg_lpt("LPT root is at %d:%d", c->lpt_lnum, c->lpt_offs);
	dbg_lpt("LPT head is at %d:%d", c->nhead_lnum, c->nhead_offs);
	dbg_lpt("LPT ltab is at %d:%d", c->ltab_lnum, c->ltab_offs);
	if (c->big_lpt)
		dbg_lpt("LPT lsave is at %d:%d", c->lsave_lnum, c->lsave_offs);
out:
	c->ltab = NULL;
	kfree(lsave);
	vfree(ltab);
	vfree(buf);
	kfree(nnode);
	kfree(pnode);
	return err;
}

/**
 * update_cats - add LEB properties of a pnode to LEB category lists and heaps.
 * @c: UBIFS file-system description object
 * @pnode: pnode
 *
 * When a pnode is loaded into memory, the LEB properties it contains are added,
 * by this function, to the LEB category lists and heaps.
 */
static void update_cats(struct ubifs_info *c, struct ubifs_pnode *pnode)
{
	int i;

	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		int cat = pnode->lprops[i].flags & LPROPS_CAT_MASK;
		int lnum = pnode->lprops[i].lnum;

		if (!lnum)
			return;
		ubifs_add_to_cat(c, &pnode->lprops[i], cat);
	}
}

/**
 * replace_cats - add LEB properties of a pnode to LEB category lists and heaps.
 * @c: UBIFS file-system description object
 * @old_pnode: pnode copied
 * @new_pnode: pnode copy
 *
 * During commit it is sometimes necessary to copy a pnode
 * (see dirty_cow_pnode).  When that happens, references in
 * category lists and heaps must be replaced.  This function does that.
 */
static void replace_cats(struct ubifs_info *c, struct ubifs_pnode *old_pnode,
			 struct ubifs_pnode *new_pnode)
{
	int i;

	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		if (!new_pnode->lprops[i].lnum)
			return;
		ubifs_replace_cat(c, &old_pnode->lprops[i], &new_pnode->lprops[i]);
	}
}

/**
 * check_lpt_crc - check LPT node crc is correct.
 * @c: UBIFS file-system description object
 * @buf: buffer containing node
 * @len: length of node
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int check_lpt_crc(void *buf, int len)
{
	int pos = 0;
	uint8_t *addr = buf;
	uint16_t crc, calc_crc;

	crc = unpack_bits(&addr, &pos, UBIFS_LPT_CRC_BITS);
	calc_crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
			 len - UBIFS_LPT_CRC_BYTES);
	if (crc != calc_crc) {
		ubifs_err("invalid crc in LPT node: crc %hx calc %hx", crc,
			  calc_crc);
		dbg_dump_stack();
		return -EINVAL;
	}
	return 0;
}

/**
 * check_lpt_type - check LPT node type is correct.
 * @c: UBIFS file-system description object
 * @addr: address of type bit field is passed and returned updated here
 * @pos: position of type bit field is passed and returned updated here
 * @type: expected type
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int check_lpt_type(uint8_t **addr, int *pos, int type)
{
	int node_type;

	node_type = unpack_bits(addr, pos, UBIFS_LPT_TYPE_BITS);
	if (node_type != type) {
		ubifs_err("invalid type (%d) in LPT node type %d", node_type,
			  type);
		dbg_dump_stack();
		return -EINVAL;
	}
	return 0;
}

/**
 * unpack_pnode - unpack a pnode.
 * @c: UBIFS file-system description object
 * @buf: buffer containing packed pnode to unpack
 * @pnode: pnode structure to fill
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int unpack_pnode(struct ubifs_info *c, void *buf,
			struct ubifs_pnode *pnode)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0, err;

	err = check_lpt_type(&addr, &pos, UBIFS_LPT_PNODE);
	if (err)
		return err;
	if (c->big_lpt)
		pnode->num = unpack_bits(&addr, &pos, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		struct ubifs_lprops * const lprops = &pnode->lprops[i];

		lprops->free = unpack_bits(&addr, &pos, c->space_bits) << 3;
		lprops->dirty = unpack_bits(&addr, &pos, c->space_bits) << 3;
		if (unpack_bits(&addr, &pos, 1))
			lprops->flags = LPROPS_INDEX;
		else
			lprops->flags = 0;
		lprops->flags |= ubifs_categorize_lprops(c, lprops);
	}
	err = check_lpt_crc(buf, c->pnode_sz);
	return err;
}

/**
 * unpack_nnode - unpack a nnode.
 * @c: UBIFS file-system description object
 * @buf: buffer containing packed nnode to unpack
 * @nnode: nnode structure to fill
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int unpack_nnode(struct ubifs_info *c, void *buf,
			struct ubifs_nnode *nnode)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0, err;

	err = check_lpt_type(&addr, &pos, UBIFS_LPT_NNODE);
	if (err)
		return err;
	if (c->big_lpt)
		nnode->num = unpack_bits(&addr, &pos, c->pcnt_bits);
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		int lnum;

		lnum = unpack_bits(&addr, &pos, c->lpt_lnum_bits) +
		       c->lpt_first;
		if (lnum == c->lpt_last + 1)
			lnum = 0;
		nnode->nbranch[i].lnum = lnum;
		nnode->nbranch[i].offs = unpack_bits(&addr, &pos,
						     c->lpt_offs_bits);
	}
	err = check_lpt_crc(buf, c->nnode_sz);
	return err;
}

/**
 * unpack_ltab - unpack the LPT's own lprops table.
 * @c: UBIFS file-system description object
 * @buf: buffer from which to unpack
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int unpack_ltab(struct ubifs_info *c, void *buf)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0, err;

	err = check_lpt_type(&addr, &pos, UBIFS_LPT_LTAB);
	if (err)
		return err;
	for (i = 0; i < c->lpt_lebs; i++) {
		int free = unpack_bits(&addr, &pos, c->lpt_spc_bits);
		int dirty = unpack_bits(&addr, &pos, c->lpt_spc_bits);

		if (free < 0 || free > c->leb_size || dirty < 0 ||
		    dirty > c->leb_size || free + dirty > c->leb_size)
			return -EINVAL;

		c->ltab[i].free = free;
		c->ltab[i].dirty = dirty;
		c->ltab[i].tgc = 0;
		c->ltab[i].cmt = 0;
	}
	err = check_lpt_crc(buf, c->ltab_sz);
	return err;
}

/**
 * unpack_lsave - unpack the LPT's save table.
 * @c: UBIFS file-system description object
 * @buf: buffer from which to unpack
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int unpack_lsave(struct ubifs_info *c, void *buf)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int i, pos = 0, err;

	err = check_lpt_type(&addr, &pos, UBIFS_LPT_LSAVE);
	if (err)
		return err;
	for (i = 0; i < c->lsave_cnt; i++) {
		int lnum = unpack_bits(&addr, &pos, c->lnum_bits);

		if (lnum < c->main_first || lnum >= c->leb_cnt)
			return -EINVAL;
		c->lsave[i] = lnum;
	}
	err = check_lpt_crc(buf, c->lsave_sz);
	return err;
}

/**
 * validate_nnode - validate a nnode.
 * @c: UBIFS file-system description object
 * @nnode: nnode to validate
 * @parent: parent nnode (or NULL for the root nnode)
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int validate_nnode(struct ubifs_info *c, struct ubifs_nnode *nnode,
			  struct ubifs_nnode *parent, int iip)
{
	int i, lvl, max_offs;

	if (c->big_lpt) {
		int num = calc_nnode_num_from_parent(c, parent, iip);

		if (nnode->num != num)
			return -EINVAL;
	}
	lvl = parent ? parent->level - 1 : c->lpt_hght;
	if (lvl < 1)
		return -EINVAL;
	if (lvl == 1)
		max_offs = c->leb_size - c->pnode_sz;
	else
		max_offs = c->leb_size - c->nnode_sz;
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		int lnum = nnode->nbranch[i].lnum;
		int offs = nnode->nbranch[i].offs;

		if (lnum == 0) {
			if (offs != 0)
				return -EINVAL;
			continue;
		}
		if (lnum < c->lpt_first || lnum > c->lpt_last)
			return -EINVAL;
		if (offs < 0 || offs > max_offs)
			return -EINVAL;
	}
	return 0;
}

/**
 * validate_pnode - validate a pnode.
 * @c: UBIFS file-system description object
 * @pnode: pnode to validate
 * @parent: parent nnode
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int validate_pnode(struct ubifs_info *c, struct ubifs_pnode *pnode,
			  struct ubifs_nnode *parent, int iip)
{
	int i;

	if (c->big_lpt) {
		int num = calc_pnode_num_from_parent(c, parent, iip);

		if (pnode->num != num)
			return -EINVAL;
	}
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		int free = pnode->lprops[i].free;
		int dirty = pnode->lprops[i].dirty;

		if (free < 0 || free > c->leb_size || free % c->min_io_size ||
		    (free & 7))
			return -EINVAL;
		if (dirty < 0 || dirty > c->leb_size || (dirty & 7))
			return -EINVAL;
		if (dirty + free > c->leb_size)
			return -EINVAL;
	}
	return 0;
}

/**
 * set_pnode_lnum - set LEB numbers on a pnode.
 * @c: UBIFS file-system description object
 * @pnode: pnode to update
 *
 * This function calculates the LEB numbers for the LEB properties it contains
 * based on the pnode number.
 */
static void set_pnode_lnum(struct ubifs_info *c, struct ubifs_pnode *pnode)
{
	int i, lnum;

	lnum = (pnode->num << UBIFS_LPT_FANOUT_SHIFT) + c->main_first;
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		if (lnum >= c->leb_cnt)
			return;
		pnode->lprops[i].lnum = lnum++;
	}
}

/**
 * read_nnode - read a nnode from flash and link it to the tree in memory.
 * @c: UBIFS file-system description object
 * @parent: parent nnode (or NULL for the root)
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int read_nnode(struct ubifs_info *c, struct ubifs_nnode *parent, int iip)
{
	struct ubifs_nbranch *branch = NULL;
	struct ubifs_nnode *nnode = NULL;
	void *buf = c->lpt_nod_buf;
	int err, lnum, offs;

	if (parent) {
		branch = &parent->nbranch[iip];
		lnum = branch->lnum;
		offs = branch->offs;
	} else {
		lnum = c->lpt_lnum;
		offs = c->lpt_offs;
	}
	nnode = kzalloc(sizeof(struct ubifs_nnode), GFP_NOFS);
	if (!nnode) {
		err = -ENOMEM;
		goto out;
	}
	if (lnum == 0) {
		/*
		 * This nnode was not written which just means that the LEB
		 * properties in the subtree below it describe empty LEBs. We
		 * make the nnode as though we had read it, which in fact means
		 * doing almost nothing.
		 */
		if (c->big_lpt)
			nnode->num = calc_nnode_num_from_parent(c, parent, iip);
	} else {
		err = ubi_read(c->ubi, lnum, buf, offs, c->nnode_sz);
		if (err)
			goto out;
		err = unpack_nnode(c, buf, nnode);
		if (err)
			goto out;
	}
	err = validate_nnode(c, nnode, parent, iip);
	if (err)
		goto out;
	if (!c->big_lpt)
		nnode->num = calc_nnode_num_from_parent(c, parent, iip);
	if (parent) {
		branch->nnode = nnode;
		nnode->level = parent->level - 1;
	} else {
		c->nroot = nnode;
		nnode->level = c->lpt_hght;
	}
	nnode->parent = parent;
	nnode->iip = iip;
	return 0;

out:
	ubifs_err("error %d reading nnode at %d:%d", err, lnum, offs);
	kfree(nnode);
	return err;
}

/**
 * read_pnode - read a pnode from flash and link it to the tree in memory.
 * @c: UBIFS file-system description object
 * @parent: parent nnode
 * @iip: index in parent
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int read_pnode(struct ubifs_info *c, struct ubifs_nnode *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_pnode *pnode = NULL;
	void *buf = c->lpt_nod_buf;
	int err, lnum, offs;

	branch = &parent->nbranch[iip];
	lnum = branch->lnum;
	offs = branch->offs;
	pnode = kzalloc(sizeof(struct ubifs_pnode), GFP_NOFS);
	if (!pnode) {
		err = -ENOMEM;
		goto out;
	}
	if (lnum == 0) {
		/*
		 * This pnode was not written which just means that the LEB
		 * properties in it describe empty LEBs. We make the pnode as
		 * though we had read it.
		 */
		int i;

		if (c->big_lpt)
			pnode->num = calc_pnode_num_from_parent(c, parent, iip);
		for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
			struct ubifs_lprops * const lprops = &pnode->lprops[i];

			lprops->free = c->leb_size;
			lprops->flags = ubifs_categorize_lprops(c, lprops);
		}
	} else {
		err = ubi_read(c->ubi, lnum, buf, offs, c->pnode_sz);
		if (err)
			goto out;
		err = unpack_pnode(c, buf, pnode);
		if (err)
			goto out;
	}
	err = validate_pnode(c, pnode, parent, iip);
	if (err)
		goto out;
	if (!c->big_lpt)
		pnode->num = calc_pnode_num_from_parent(c, parent, iip);
	branch->pnode = pnode;
	pnode->parent = parent;
	pnode->iip = iip;
	set_pnode_lnum(c, pnode);
	c->pnodes_have += 1;
	return 0;

out:
	ubifs_err("error %d reading pnode at %d:%d", err, lnum, offs);
	kfree(pnode);
	return err;
}

/**
 * read_ltab - read LPT's own lprops table.
 * @c: UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int read_ltab(struct ubifs_info *c)
{
	int err;
	void *buf;

	buf = vmalloc(c->ltab_sz);
	if (!buf)
		return -ENOMEM;
	err = ubi_read(c->ubi, c->ltab_lnum, buf, c->ltab_offs, c->ltab_sz);
	if (err)
		goto out;
	err = unpack_ltab(c, buf);
out:
	vfree(buf);
	return err;
}

/**
 * read_lsave - read LPT's save table.
 * @c: UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int read_lsave(struct ubifs_info *c)
{
	int err, i;
	void *buf;

	buf = vmalloc(c->lsave_sz);
	if (!buf)
		return -ENOMEM;
	err = ubi_read(c->ubi, c->lsave_lnum, buf, c->lsave_offs, c->lsave_sz);
	if (err)
		goto out;
	err = unpack_lsave(c, buf);
	if (err)
		goto out;
	for (i = 0; i < c->lsave_cnt; i++) {
		int lnum = c->lsave[i];

		/*
		 * Due to automatic resizing, the values in the lsave table
		 * could be beyond the volume size - just ignore them.
		 */
		if (lnum >= c->leb_cnt)
			continue;
		ubifs_lpt_lookup(c, lnum);
	}
out:
	vfree(buf);
	return err;
}

/**
 * get_nnode - get a nnode.
 * @c: UBIFS file-system description object
 * @parent: parent nnode (or NULL for the root)
 * @iip: index in parent
 *
 * This function returns a pointer to the nnode on success or a negative error
 * code on failure.
 */
static struct ubifs_nnode *get_nnode(struct ubifs_info *c,
				     struct ubifs_nnode *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_nnode *nnode;
	int err;

	branch = &parent->nbranch[iip];
	nnode = branch->nnode;
	if (nnode)
		return nnode;
	err = read_nnode(c, parent, iip);
	if (err)
		return ERR_PTR(err);
	return branch->nnode;
}

/**
 * get_pnode - get a pnode.
 * @c: UBIFS file-system description object
 * @parent: parent nnode
 * @iip: index in parent
 *
 * This function returns a pointer to the pnode on success or a negative error
 * code on failure.
 */
static struct ubifs_pnode *get_pnode(struct ubifs_info *c,
				     struct ubifs_nnode *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_pnode *pnode;
	int err;

	branch = &parent->nbranch[iip];
	pnode = branch->pnode;
	if (pnode)
		return pnode;
	err = read_pnode(c, parent, iip);
	if (err)
		return ERR_PTR(err);
	update_cats(c, branch->pnode);
	return branch->pnode;
}

/**
 * ubifs_lpt_lookup - lookup LEB properties in the LPT.
 * @c: UBIFS file-system description object
 * @lnum: LEB number to lookup
 *
 * This function returns a pointer to the LEB properties on success or a
 * negative error code on failure.
 */
struct ubifs_lprops *ubifs_lpt_lookup(struct ubifs_info *c, int lnum)
{
	int err, i, h, iip, shft;
	struct ubifs_nnode *nnode;
	struct ubifs_pnode *pnode;

	if (!c->nroot) {
		err = read_nnode(c, NULL, 0);
		if (err)
			return ERR_PTR(err);
	}
	nnode = c->nroot;
	i = lnum - c->main_first;
	shft = c->lpt_hght * UBIFS_LPT_FANOUT_SHIFT;
	for (h = 1; h < c->lpt_hght; h++) {
		iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
		shft -= UBIFS_LPT_FANOUT_SHIFT;
		nnode = get_nnode(c, nnode, iip);
		if (IS_ERR(nnode))
			return ERR_PTR(PTR_ERR(nnode));
	}
	iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
	shft -= UBIFS_LPT_FANOUT_SHIFT;
	pnode = get_pnode(c, nnode, iip);
	if (IS_ERR(pnode))
		return ERR_PTR(PTR_ERR(pnode));
	iip = (i & (UBIFS_LPT_FANOUT - 1));
	dbg_lp("LEB %d, free %d, dirty %d, flags %d", lnum,
	       pnode->lprops[iip].free, pnode->lprops[iip].dirty,
	       pnode->lprops[iip].flags);
	return &pnode->lprops[iip];
}

/**
 * nnode_lookup - lookup a nnode in the LPT.
 * @c: UBIFS file-system description object
 * @i: nnode number
 *
 * This function returns a pointer to the nnode on success or a negative
 * error code on failure.
 */
static struct ubifs_nnode *nnode_lookup(struct ubifs_info *c, int i)
{
	int err, iip;
	struct ubifs_nnode *nnode;

	if (!c->nroot) {
		err = read_nnode(c, NULL, 0);
		if (err)
			return ERR_PTR(err);
	}
	nnode = c->nroot;
	while (1) {
		iip = i & (UBIFS_LPT_FANOUT - 1);
		i >>= UBIFS_LPT_FANOUT_SHIFT;
		if (!i)
			break;
		nnode = get_nnode(c, nnode, iip);
		if (IS_ERR(nnode))
			return nnode;
	}
	return nnode;
}

/**
 * pnode_lookup - lookup a pnode in the LPT.
 * @c: UBIFS file-system description object
 * @i: pnode number (0 to main_lebs - 1)
 *
 * This function returns a pointer to the pnode on success or a negative
 * error code on failure.
 */
static struct ubifs_pnode *pnode_lookup(struct ubifs_info *c, int i)
{
	int err, h, iip, shft;
	struct ubifs_nnode *nnode;

	if (!c->nroot) {
		err = read_nnode(c, NULL, 0);
		if (err)
			return ERR_PTR(err);
	}
	i <<= UBIFS_LPT_FANOUT_SHIFT;
	nnode = c->nroot;
	shft = c->lpt_hght * UBIFS_LPT_FANOUT_SHIFT;
	for (h = 1; h < c->lpt_hght; h++) {
		iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
		shft -= UBIFS_LPT_FANOUT_SHIFT;
		nnode = get_nnode(c, nnode, iip);
		if (IS_ERR(nnode))
			return ERR_PTR(PTR_ERR(nnode));
	}
	iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
	return get_pnode(c, nnode, iip);
}

/**
 * dirty_cow_nnode - ensure a nnode is not being committed.
 * @c: UBIFS file-system description object
 * @nnode: nnode to check
 *
 * Returns dirtied nnode on success or negative error code on failure.
 */
static struct ubifs_nnode *dirty_cow_nnode(struct ubifs_info *c,
					   struct ubifs_nnode *nnode)
{
	struct ubifs_nnode *n;
	int i;

	if (!test_bit(COW_ZNODE, &nnode->flags)) {
		/* nnode is not being committed */
		if (!test_and_set_bit(DIRTY_CNODE, &nnode->flags)) {
			c->dirty_nn_cnt += 1;
			add_nnode_dirt(c, nnode);
		}
		return nnode;
	}

	/* nnode is being committed, so copy it */
	n = kzalloc(sizeof(struct ubifs_nnode), GFP_NOFS);
	if (!n)
		return ERR_PTR(-ENOMEM);

	memcpy(n, nnode, sizeof(struct ubifs_nnode));

	/* The children now have new parent */
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		struct ubifs_nbranch *branch = &n->nbranch[i];

		if (branch->cnode)
			branch->cnode->parent = n;
	}

	ubifs_assert(!test_bit(OBSOLETE_CNODE, &nnode->flags));
	set_bit(OBSOLETE_CNODE, &nnode->flags);

	n->cnext = NULL;
	set_bit(DIRTY_CNODE, &n->flags);
	clear_bit(COW_CNODE, &n->flags);
	c->dirty_nn_cnt += 1;
	add_nnode_dirt(c, nnode);
	if (nnode->parent)
		nnode->parent->nbranch[n->iip].nnode = n;
	else
		c->nroot = n;

	return n;
}

/**
 * dirty_cow_pnode - ensure a pnode is not being committed.
 * @c: UBIFS file-system description object
 * @pnode: pnode to check
 *
 * Returns dirtied pnode on success or negative error code on failure.
 */
static struct ubifs_pnode *dirty_cow_pnode(struct ubifs_info *c,
					   struct ubifs_pnode *pnode)
{
	struct ubifs_pnode *p;

	if (!test_bit(COW_ZNODE, &pnode->flags)) {
		/* pnode is not being committed */
		if (!test_and_set_bit(DIRTY_CNODE, &pnode->flags)) {
			c->dirty_pn_cnt += 1;
			add_pnode_dirt(c, pnode);
		}
		return pnode;
	}

	/* pnode is being committed, so copy it */
	p = kzalloc(sizeof(struct ubifs_pnode), GFP_NOFS);
	if (!p)
		return ERR_PTR(-ENOMEM);

	memcpy(p, pnode, sizeof(struct ubifs_pnode));
	replace_cats(c, pnode, p);

	ubifs_assert(!test_bit(OBSOLETE_CNODE, &pnode->flags));
	set_bit(OBSOLETE_CNODE, &pnode->flags);

	p->cnext = NULL;
	set_bit(DIRTY_CNODE, &p->flags);
	clear_bit(COW_CNODE, &p->flags);
	c->dirty_pn_cnt += 1;
	add_pnode_dirt(c, pnode);
	pnode->parent->nbranch[p->iip].pnode = p;

	return p;
}

/**
 * ubifs_lpt_lookup_dirty - lookup LEB properties in the LPT.
 * @c: UBIFS file-system description object
 * @lnum: LEB number to lookup
 *
 * This function returns a pointer to the LEB properties on success or a
 * negative error code on failure.
 */
struct ubifs_lprops *ubifs_lpt_lookup_dirty(struct ubifs_info *c, int lnum)
{
	int err, i, h, iip, shft;
	struct ubifs_nnode *nnode;
	struct ubifs_pnode *pnode;

	if (!c->nroot) {
		err = read_nnode(c, NULL, 0);
		if (err)
			return ERR_PTR(err);
	}
	nnode = c->nroot;
	nnode = dirty_cow_nnode(c, nnode);
	if (IS_ERR(nnode))
		return ERR_PTR(PTR_ERR(nnode));
	i = lnum - c->main_first;
	shft = c->lpt_hght * UBIFS_LPT_FANOUT_SHIFT;
	for (h = 1; h < c->lpt_hght; h++) {
		iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
		shft -= UBIFS_LPT_FANOUT_SHIFT;
		nnode = get_nnode(c, nnode, iip);
		if (IS_ERR(nnode))
			return ERR_PTR(PTR_ERR(nnode));
		nnode = dirty_cow_nnode(c, nnode);
		if (IS_ERR(nnode))
			return ERR_PTR(PTR_ERR(nnode));
	}
	iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
	shft -= UBIFS_LPT_FANOUT_SHIFT;
	pnode = get_pnode(c, nnode, iip);
	if (IS_ERR(pnode))
		return ERR_PTR(PTR_ERR(pnode));
	pnode = dirty_cow_pnode(c, pnode);
	if (IS_ERR(pnode))
		return ERR_PTR(PTR_ERR(pnode));
	iip = (i & (UBIFS_LPT_FANOUT - 1));
	dbg_lp("LEB %d, free %d, dirty %d, flags %d", lnum,
	       pnode->lprops[iip].free, pnode->lprops[iip].dirty,
	       pnode->lprops[iip].flags);
	ubifs_assert(test_bit(DIRTY_CNODE, &pnode->flags));
	return &pnode->lprops[iip];
}

/**
 * first_dirty_cnode - find first dirty cnode.
 * @c: UBIFS file-system description object
 * @nnode: nnode at which to start
 *
 * This function returns the first dirty cnode or %NULL if there is not one.
 */
static struct ubifs_cnode *first_dirty_cnode(struct ubifs_nnode *nnode)
{
	ubifs_assert(nnode);
	while (1) {
		int i, cont = 0;

		for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
			struct ubifs_cnode *cnode;

			cnode = nnode->nbranch[i].cnode;
			if (cnode &&
			    test_bit(DIRTY_CNODE, &cnode->flags)) {
				if (cnode->level == 0)
					return cnode;
				nnode = (struct ubifs_nnode *)cnode;
				cont = 1;
				break;
			}
		}
		if (!cont)
			return (struct ubifs_cnode *)nnode;
	}
}

/**
 * next_dirty_cnode - find next dirty cnode.
 * @cnode: cnode from which to begin searching
 *
 * This function returns the next dirty cnode or %NULL if there is not one.
 */
static struct ubifs_cnode *next_dirty_cnode(struct ubifs_cnode *cnode)
{
	struct ubifs_nnode *nnode;
	int i;

	ubifs_assert(cnode);
	nnode = cnode->parent;
	if (!nnode)
		return NULL;
	for (i = cnode->iip + 1; i < UBIFS_LPT_FANOUT; i++) {
		cnode = nnode->nbranch[i].cnode;
		if (cnode && test_bit(DIRTY_CNODE, &cnode->flags))
		{
			if (cnode->level == 0)
				return cnode; /* cnode is a pnode */
			/* cnode is a nnode */
			return first_dirty_cnode((struct ubifs_nnode *)cnode);
		}
	}
	return (struct ubifs_cnode *)nnode;
}

/**
 * get_cnodes_to_commit - create list of dirty cnodes to commit.
 * @c: UBIFS file-system description object
 *
 * This function returns the number of cnodes to commit.
 */
static int get_cnodes_to_commit(struct ubifs_info *c)
{
	struct ubifs_cnode *cnode, *cnext;
	int cnt = 0;

	if (!c->nroot)
		return 0;

	if (!test_bit(DIRTY_CNODE, &c->nroot->flags))
		return 0;

	c->lpt_cnext = first_dirty_cnode(c->nroot);
	cnode = c->lpt_cnext;
	if (!cnode)
		return 0;
	cnt += 1;
	while (1) {
		ubifs_assert(!test_bit(COW_ZNODE, &cnode->flags));
		set_bit(COW_ZNODE, &cnode->flags);
		cnext = next_dirty_cnode(cnode);
		if (!cnext) {
			cnode->cnext = c->lpt_cnext;
			break;
		}
		cnode->cnext = cnext;
		cnode = cnext;
		cnt += 1;
	}
	dbg_cmt("committing %d cnodes", cnt);
	dbg_lpt("committing %d cnodes", cnt);
	ubifs_assert(cnt == c->dirty_nn_cnt + c->dirty_pn_cnt);
	return cnt;
}

/**
 * layout_cnodes - layout cnodes for commit.
 * @c: UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int layout_cnodes(struct ubifs_info *c)
{
	int lnum, offs, len, alen, done_lsave, done_ltab, err;
	struct ubifs_cnode *cnode;

	cnode = c->lpt_cnext;
	if (!cnode)
		return 0;
	lnum = c->nhead_lnum;
	offs = c->nhead_offs;
	/* Try to place lsave and ltab nicely */
	done_lsave = !c->big_lpt;
	done_ltab = 0;
	if (!done_lsave && offs + c->lsave_sz <= c->leb_size) {
		done_lsave = 1;
		c->lsave_lnum = lnum;
		c->lsave_offs = offs;
		offs += c->lsave_sz;
	}
	if (offs + c->ltab_sz <= c->leb_size) {
		done_ltab = 1;
		c->ltab_lnum = lnum;
		c->ltab_offs = offs;
		offs += c->ltab_sz;
	}
	do {
		if (cnode->level) {
			len = c->nnode_sz;
			c->dirty_nn_cnt -= 1;
		} else {
			len = c->pnode_sz;
			c->dirty_pn_cnt -= 1;
		}
		while (offs + len > c->leb_size) {
			alen = ALIGN(offs, c->min_io_size);
			upd_ltab(c, lnum, c->leb_size - alen, alen - offs);
			err = alloc_lpt_leb(c, &lnum);
			if (err)
				return err;
			offs = 0;
			ubifs_assert(lnum >= c->lpt_first &&
				     lnum <= c->lpt_last);
			/* Try to place lsave and ltab nicely */
			if (!done_lsave) {
				done_lsave = 1;
				c->lsave_lnum = lnum;
				c->lsave_offs = offs;
				offs += c->lsave_sz;
				continue;
			}
			if (!done_ltab) {
				done_ltab = 1;
				c->ltab_lnum = lnum;
				c->ltab_offs = offs;
				offs += c->ltab_sz;
				continue;
			}
			break;
		}
		if (cnode->parent) {
			cnode->parent->nbranch[cnode->iip].lnum = lnum;
			cnode->parent->nbranch[cnode->iip].offs = offs;
		} else {
			c->lpt_lnum = lnum;
			c->lpt_offs = offs;
		}
		offs += len;
		cnode = cnode->cnext;
	} while (cnode && cnode != c->lpt_cnext);
	/* Make sure to place LPT's save table */
	if (!done_lsave) {
		if (offs + c->lsave_sz > c->leb_size) {
			alen = ALIGN(offs, c->min_io_size);
			upd_ltab(c, lnum, c->leb_size - alen, alen - offs);
			err = alloc_lpt_leb(c, &lnum);
			if (err)
				return err;
			offs = 0;
			ubifs_assert(lnum >= c->lpt_first &&
				     lnum <= c->lpt_last);
		}
		done_lsave = 1;
		c->lsave_lnum = lnum;
		c->lsave_offs = offs;
		offs += c->lsave_sz;
	}
	/* Make sure to place LPT's own lprops table */
	if (!done_ltab) {
		if (offs + c->ltab_sz > c->leb_size) {
			alen = ALIGN(offs, c->min_io_size);
			upd_ltab(c, lnum, c->leb_size - alen, alen - offs);
			err = alloc_lpt_leb(c, &lnum);
			if (err)
				return err;
			offs = 0;
			ubifs_assert(lnum >= c->lpt_first &&
				     lnum <= c->lpt_last);
		}
		done_ltab = 1;
		c->ltab_lnum = lnum;
		c->ltab_offs = offs;
		offs += c->ltab_sz;
	}
	alen = ALIGN(offs, c->min_io_size);
	upd_ltab(c, lnum, c->leb_size - alen, alen - offs);
	return 0;
}

/**
 * write_cnodes - write cnodes for commit.
 * @c: UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int write_cnodes(struct ubifs_info *c)
{
	int lnum, offs, len, from, err, wlen, alen, done_ltab, done_lsave;
	struct ubifs_cnode *cnode;
	void *buf = c->lpt_buf;

	cnode = c->lpt_cnext;
	if (!cnode)
		return 0;
	lnum = c->nhead_lnum;
	offs = c->nhead_offs;
	from = offs;
	/* Ensure empty LEB is unmapped */
	if (offs == 0) {
		err = unmap_leb(c, lnum);
		if (err)
			return err;
	}
	/* Try to place lsave and ltab nicely */
	done_lsave = !c->big_lpt;
	done_ltab = 0;
	if (!done_lsave && offs + c->lsave_sz <= c->leb_size) {
		done_lsave = 1;
		pack_lsave(c, buf + offs, c->lsave);
		offs += c->lsave_sz;
	}
	if (offs + c->ltab_sz <= c->leb_size) {
		done_ltab = 1;
		pack_ltab(c, buf + offs, c->ltab_cmt);
		offs += c->ltab_sz;
	}
	/* Loop for each cnode */
	do {
		if (cnode->level)
			len = c->nnode_sz;
		else
			len = c->pnode_sz;
		while (offs + len > c->leb_size) {
			wlen = offs - from;
			if (wlen) {
				alen = ALIGN(wlen, c->min_io_size);
				memset(buf + offs, 0xff, alen - wlen);
				err = write_leb(c, lnum, buf, from, alen);
				if (err)
					return err;
			}
			err = realloc_lpt_leb(c, &lnum);
			if (err)
				return err;
			offs = 0;
			from = 0;
			ubifs_assert(lnum >= c->lpt_first &&
				     lnum <= c->lpt_last);
			err = unmap_leb(c, lnum);
			if (err)
				return err;
			/* Try to place lsave and ltab nicely */
			if (!done_lsave) {
				done_lsave = 1;
				pack_lsave(c, buf + offs, c->lsave);
				offs += c->lsave_sz;
				continue;
			}
			if (!done_ltab) {
				done_ltab = 1;
				pack_ltab(c, buf + offs, c->ltab_cmt);
				offs += c->ltab_sz;
				continue;
			}
			break;
		}
		if (cnode->level)
			pack_nnode(c, buf + offs, (struct ubifs_nnode *)cnode);
		else
			pack_pnode(c, buf + offs, (struct ubifs_pnode *)cnode);
		clear_bit(DIRTY_CNODE, &cnode->flags);
		smp_mb__before_clear_bit();
		clear_bit(COW_ZNODE, &cnode->flags);
		smp_mb__after_clear_bit();
		offs += len;
		cnode = cnode->cnext;
	} while (cnode && cnode != c->lpt_cnext);
	/* Make sure to place LPT's save table */
	if (!done_lsave) {
		if (offs + c->lsave_sz > c->leb_size) {
			wlen = offs - from;
			alen = ALIGN(wlen, c->min_io_size);
			memset(buf + offs, 0xff, alen - wlen);
			err = write_leb(c, lnum, buf, from, alen);
			if (err)
				return err;
			err = realloc_lpt_leb(c, &lnum);
			if (err)
				return err;
			offs = 0;
			ubifs_assert(lnum >= c->lpt_first &&
				     lnum <= c->lpt_last);
			err = unmap_leb(c, lnum);
			if (err)
				return err;
		}
		done_lsave = 1;
		pack_lsave(c, buf + offs, c->lsave);
		offs += c->lsave_sz;
	}
	/* Make sure to place LPT's own lprops table */
	if (!done_ltab) {
		if (offs + c->ltab_sz > c->leb_size) {
			wlen = offs - from;
			alen = ALIGN(wlen, c->min_io_size);
			memset(buf + offs, 0xff, alen - wlen);
			err = write_leb(c, lnum, buf, from, alen);
			if (err)
				return err;
			err = realloc_lpt_leb(c, &lnum);
			if (err)
				return err;
			offs = 0;
			ubifs_assert(lnum >= c->lpt_first &&
				     lnum <= c->lpt_last);
			err = unmap_leb(c, lnum);
			if (err)
				return err;
		}
		done_ltab = 1;
		pack_ltab(c, buf + offs, c->ltab_cmt);
		offs += c->ltab_sz;
	}
	/* Write remaining data in buffer */
	wlen = offs - from;
	alen = ALIGN(wlen, c->min_io_size);
	memset(buf + offs, 0xff, alen - wlen);
	err = write_leb(c, lnum, buf, from, alen);
	if (err)
		return err;
	c->nhead_lnum = lnum;
	c->nhead_offs = ALIGN(offs, c->min_io_size);
	dbg_lpt("LPT root is at %d:%d", c->lpt_lnum, c->lpt_offs);
	dbg_lpt("LPT head is at %d:%d", c->nhead_lnum, c->nhead_offs);
	dbg_lpt("LPT ltab is at %d:%d", c->ltab_lnum, c->ltab_offs);
	if (c->big_lpt)
		dbg_lpt("LPT lsave is at %d:%d", c->lsave_lnum, c->lsave_offs);
	return 0;
}

/**
 * lpt_init_rd - initialize the LPT for reading.
 * @c: UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int lpt_init_rd(struct ubifs_info *c)
{
	int err, i;

	c->ltab = vmalloc(sizeof(struct ubifs_lpt_lprops) * c->lpt_lebs);
	if (!c->ltab)
		return -ENOMEM;

	i = max_t(int, c->nnode_sz, c->pnode_sz);
	c->lpt_nod_buf = kmalloc(i, GFP_KERNEL);
	if (!c->lpt_nod_buf)
		return -ENOMEM;

	for (i = 0; i < LPROPS_HEAP_CNT; i++) {
		c->lpt_heap[i].arr = kmalloc(sizeof(void *) * LPT_HEAP_SZ,
					     GFP_KERNEL);
		if (!c->lpt_heap[i].arr)
			return -ENOMEM;
		c->lpt_heap[i].cnt = 0;
		c->lpt_heap[i].max_cnt = LPT_HEAP_SZ;
	}

	c->dirty_idx.arr = kmalloc(sizeof(void *) * LPT_HEAP_SZ, GFP_KERNEL);
	if (!c->dirty_idx.arr)
		return -ENOMEM;
	c->dirty_idx.cnt = 0;
	c->dirty_idx.max_cnt = LPT_HEAP_SZ;

	err = read_ltab(c);
	if (err)
		return err;

	dbg_lpt("space_bits %d", c->space_bits);
	dbg_lpt("lpt_lnum_bits %d", c->lpt_lnum_bits);
	dbg_lpt("lpt_offs_bits %d", c->lpt_offs_bits);
	dbg_lpt("lpt_spc_bits %d", c->lpt_spc_bits);
	dbg_lpt("pcnt_bits %d", c->pcnt_bits);
	dbg_lpt("lnum_bits %d", c->lnum_bits);
	dbg_lpt("pnode_sz %d", c->pnode_sz);
	dbg_lpt("nnode_sz %d", c->nnode_sz);
	dbg_lpt("ltab_sz %d", c->ltab_sz);
	dbg_lpt("lsave_sz %d", c->lsave_sz);
	dbg_lpt("lsave_cnt %d", c->lsave_cnt);
	dbg_lpt("lpt_hght %d", c->lpt_hght);
	dbg_lpt("big_lpt %d", c->big_lpt);
	dbg_lpt("LPT root is at %d:%d", c->lpt_lnum, c->lpt_offs);
	dbg_lpt("LPT head is at %d:%d", c->nhead_lnum, c->nhead_offs);
	dbg_lpt("LPT ltab is at %d:%d", c->ltab_lnum, c->ltab_offs);
	if (c->big_lpt)
		dbg_lpt("LPT lsave is at %d:%d", c->lsave_lnum, c->lsave_offs);

	return 0;
}

/**
 * lpt_init_wr - initialize the LPT for writing.
 * @c: UBIFS file-system description object
 *
 * 'lpt_init_rd()' must have been called already.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int lpt_init_wr(struct ubifs_info *c)
{
	int err, i;

	c->ltab_cmt = vmalloc(sizeof(struct ubifs_lpt_lprops) * c->lpt_lebs);
	if (!c->ltab_cmt)
		return -ENOMEM;

	c->lpt_buf = vmalloc(c->leb_size);
	if (!c->lpt_buf)
		return -ENOMEM;

	if (c->big_lpt) {
		c->lsave = kmalloc(sizeof(int) * c->lsave_cnt, GFP_NOFS);
		if (!c->lsave)
			return -ENOMEM;
		err = read_lsave(c);
		if (err)
			return err;
	}

	for (i = 0; i < c->lpt_lebs; i++)
		if (c->ltab[i].free == c->leb_size) {
			err = unmap_leb(c, i + c->lpt_first);
			if (err)
				return err;
		}

	return 0;
}

/**
 * ubifs_lpt_init - initialize the LPT.
 * @c: UBIFS file-system description object
 * @rd: whether to initialize lpt for reading
 * @wr: whether to initialize lpt for writing
 *
 * For mounting 'rw', @rd and @wr are both true. For mounting 'ro', @rd is true
 * and @wr is false. For mounting from 'ro' to 'rw', @rd is false and @wr is
 * true.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_lpt_init(struct ubifs_info *c, int rd, int wr)
{
	int err;

	if (rd) {
		err = lpt_init_rd(c);
		if (err)
			return err;
	}

	if (wr) {
		err = lpt_init_wr(c);
		if (err)
			return err;
	}

	return 0;
}

/**
 * get_lpt_node_len - return the length of a node based on its type.
 * @c: UBIFS file-system description object
 * @node_type: LPT node type
 */
static int get_lpt_node_len(struct ubifs_info *c, int node_type)
{
	switch (node_type)
	{
	case UBIFS_LPT_NNODE:
		return c->nnode_sz;
	case UBIFS_LPT_PNODE:
		return c->pnode_sz;
	case UBIFS_LPT_LTAB:
		return c->ltab_sz;
	case UBIFS_LPT_LSAVE:
		return c->lsave_sz;
	}
	return 0;
}

/**
 * is_a_node - determine if a buffer contains a node.
 * @c: UBIFS file-system description object
 * @buf: buffer
 * @len: length of buffer
 *
 * This function returns %1 if the buffer contains a node or %0 if it does not.
 */
static int is_a_node(struct ubifs_info *c, uint8_t *buf, int len)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int pos = 0, node_type, node_len;
	uint16_t crc, calc_crc;

	node_type = unpack_bits(&addr, &pos, UBIFS_LPT_TYPE_BITS);
	if (node_type == UBIFS_LPT_NOT_A_NODE)
		return 0;
	node_len = get_lpt_node_len(c, node_type);
	if (!node_len || node_len > len)
		return 0;
	pos = 0;
	addr = buf;
	crc = unpack_bits(&addr, &pos, UBIFS_LPT_CRC_BITS);
	calc_crc = crc16(-1, buf + UBIFS_LPT_CRC_BYTES,
			 node_len - UBIFS_LPT_CRC_BYTES);
	if (crc != calc_crc)
		return 0;
	return 1;
}

/**
 * get_pad_len - return the length of padding in a buffer.
 * @c: UBIFS file-system description object
 * @buf: buffer
 * @len: length of buffer
 */
static int get_pad_len(struct ubifs_info *c, uint8_t *buf, int len)
{
	int offs, pad_len;

	if (c->min_io_size == 1)
		return 0;
	offs = c->leb_size - len;
	pad_len = ALIGN(offs, c->min_io_size) - offs;
	return pad_len;
}

/**
 * get_lpt_node_type - return type (and node number) of a node in a buffer.
 * @c: UBIFS file-system description object
 * @buf: buffer
 * @node_num: node number is returned here
 */
static int get_lpt_node_type(struct ubifs_info *c, uint8_t *buf, int *node_num)
{
	uint8_t *addr = buf + UBIFS_LPT_CRC_BYTES;
	int pos = 0, node_type;

	node_type = unpack_bits(&addr, &pos, UBIFS_LPT_TYPE_BITS);
	*node_num = unpack_bits(&addr, &pos, c->pcnt_bits);
	return node_type;
}

/**
 * make_nnode_dirty - find a nnode and, if found, make it dirty.
 * @c: UBIFS file-system description object
 * @node_num: nnode number of nnode to make dirty
 * @lnum: LEB number where nnode was written
 * @offs: offset where nnode was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the nodes in the LEB being garbage-collected as
 * dirty.  The dirty nodes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_nnode_dirty(struct ubifs_info *c, int node_num, int lnum,
			    int offs)
{
	struct ubifs_nnode *nnode;

	nnode = nnode_lookup(c, node_num);
	if (IS_ERR(nnode))
		return PTR_ERR(nnode);
	if (nnode->parent) {
		struct ubifs_nbranch *branch;

		branch = &nnode->parent->nbranch[nnode->iip];
		if (branch->lnum != lnum || branch->offs != offs)
			return 0; /* nnode is obsolete */
	} else if (c->lpt_lnum != lnum || c->lpt_offs != offs)
			return 0; /* nnode is obsolete */
	/* Assumes cnext list is empty i.e. not called during commit */
	if (!test_and_set_bit(DIRTY_CNODE, &nnode->flags)) {
		c->dirty_nn_cnt += 1;
		add_nnode_dirt(c, nnode);
		/* Mark parent and ancestors dirty too */
		nnode = nnode->parent;
		while (nnode) {
			if (!test_and_set_bit(DIRTY_CNODE, &nnode->flags)) {
				c->dirty_nn_cnt += 1;
				add_nnode_dirt(c, nnode);
				nnode = nnode->parent;
			} else
				break;
		}
	}
	return 0;
}

/**
 * do_make_pnode_dirty - mark a pnode dirty.
 * @c: UBIFS file-system description object
 * @pnode: pnode to mark dirty
 */
static void do_make_pnode_dirty(struct ubifs_info *c, struct ubifs_pnode *pnode)
{
	/* Assumes cnext list is empty i.e. not called during commit */
	if (!test_and_set_bit(DIRTY_CNODE, &pnode->flags)) {
		struct ubifs_nnode *nnode;

		c->dirty_pn_cnt += 1;
		add_pnode_dirt(c, pnode);
		/* Mark parent and ancestors dirty too */
		nnode = pnode->parent;
		while (nnode) {
			if (!test_and_set_bit(DIRTY_CNODE, &nnode->flags)) {
				c->dirty_nn_cnt += 1;
				add_nnode_dirt(c, nnode);
				nnode = nnode->parent;
			} else
				break;
		}
	}
}

/**
 * make_pnode_dirty - find a pnode and, if found, make it dirty.
 * @c: UBIFS file-system description object
 * @node_num: pnode number of pnode to make dirty
 * @lnum: LEB number where pnode was written
 * @offs: offset where pnode was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the nodes in the LEB being garbage-collected as
 * dirty.  The dirty nodes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_pnode_dirty(struct ubifs_info *c, int node_num, int lnum,
			    int offs)
{
	struct ubifs_pnode *pnode;
	struct ubifs_nbranch *branch;

	pnode = pnode_lookup(c, node_num);
	if (IS_ERR(pnode))
		return PTR_ERR(pnode);
	branch = &pnode->parent->nbranch[pnode->iip];
	if (branch->lnum != lnum || branch->offs != offs)
		return 0;
	do_make_pnode_dirty(c, pnode);
	return 0;
}

/**
 * make_ltab_dirty - make ltab node dirty.
 * @c: UBIFS file-system description object
 * @lnum: LEB number where ltab was written
 * @offs: offset where ltab was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the nodes in the LEB being garbage-collected as
 * dirty.  The dirty nodes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_ltab_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->ltab_lnum || offs != c->ltab_offs)
		return 0; /* This ltab node is obsolete */
	if (!(c->lpt_drty_flgs & LTAB_DIRTY)) {
		c->lpt_drty_flgs |= LTAB_DIRTY;
		add_lpt_dirt(c, c->ltab_lnum, c->ltab_sz);
	}
	return 0;
}

/**
 * make_lsave_dirty - make lsave node dirty.
 * @c: UBIFS file-system description object
 * @lnum: LEB number where lsave was written
 * @offs: offset where lsave was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the nodes in the LEB being garbage-collected as
 * dirty.  The dirty nodes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_lsave_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->lsave_lnum || offs != c->lsave_offs)
		return 0; /* This lsave node is obsolete */
	if (!(c->lpt_drty_flgs & LSAVE_DIRTY)) {
		c->lpt_drty_flgs |= LSAVE_DIRTY;
		add_lpt_dirt(c, c->lsave_lnum, c->lsave_sz);
	}
	return 0;
}

/**
 * make_node_dirty - make node dirty.
 * @c: UBIFS file-system description object
 * @node_type: LPT node type
 * @node_num: node number
 * @lnum: LEB number where node was written
 * @offs: offset where node was written
 *
 * This function is used by LPT garbage collection.  LPT garbage collection is
 * used only for the "big" LPT model (c->big_lpt == 1).  Garbage collection
 * simply involves marking all the nodes in the LEB being garbage-collected as
 * dirty.  The dirty nodes are written next commit, after which the LEB is free
 * to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_node_dirty(struct ubifs_info *c, int node_type, int node_num,
			   int lnum, int offs)
{
	switch (node_type) {
	case UBIFS_LPT_NNODE:
		return make_nnode_dirty(c, node_num, lnum, offs);
	case UBIFS_LPT_PNODE:
		return make_pnode_dirty(c, node_num, lnum, offs);
	case UBIFS_LPT_LTAB:
		return make_ltab_dirty(c, lnum, offs);
	case UBIFS_LPT_LSAVE:
		return make_lsave_dirty(c, lnum, offs);
	}
	return -EINVAL;
}

/**
 * next_pnode - find next pnode.
 * @c: UBIFS file-system description object
 * @pnode: pnode
 *
 * This function returns the next pnode or %NULL if there are no more pnodes.
 */
static
struct ubifs_pnode *next_pnode(struct ubifs_info *c, struct ubifs_pnode *pnode)
{
	struct ubifs_nnode *nnode;
	int iip;

	/* Try to go right */
	nnode = pnode->parent;
	iip = pnode->iip + 1;
	if (iip < UBIFS_LPT_FANOUT) {
		/* We assume here that LEB zero is never an LPT LEB */
		if (nnode->nbranch[iip].lnum)
			return get_pnode(c, nnode, iip);
		else
			return NULL;
	}
	/* Go up while can't go right */
	do {
		iip = nnode->iip + 1;
		nnode = nnode->parent;
		if (!nnode)
			return NULL;
		/* We assume here that LEB zero is never an LPT LEB */
	} while (iip >= UBIFS_LPT_FANOUT || !nnode->nbranch[iip].lnum);
	/* Go right */
	nnode = get_nnode(c, nnode, iip);
	if (IS_ERR(nnode))
		return (void *)nnode;
	/* Go down to level 1 */
	while (nnode->level > 1) {
		nnode = get_nnode(c, nnode, 0);
		if (IS_ERR(nnode))
			return (void *)nnode;
	}
	return get_pnode(c, nnode, 0);
}

/**
 * make_tree_dirty - mark the entire LEB properties tree dirty.
 * @c: UBIFS file-system description object
 *
 * This function is used by the "small" LPT model to cause the entire LEB
 * properties tree to be written.  The "small" LPT model does not use LPT
 * garbage collection because it is more efficient to write the entire tree
 * (because it is small).
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int make_tree_dirty(struct ubifs_info *c)
{
	struct ubifs_pnode *pnode;

	pnode = pnode_lookup(c, 0);
	while (pnode) {
		do_make_pnode_dirty(c, pnode);
		pnode = next_pnode(c, pnode);
		if (IS_ERR(pnode))
			return PTR_ERR(pnode);
	}
	return 0;
}

/**
 * lpt_gc_lnum - garbage collect a LPT LEB.
 * @c: UBIFS file-system description object
 * @lnum: LEB number to garbage collect
 *
 * LPT garbage collection is used only for the "big" LPT model
 * (c->big_lpt == 1).  Garbage collection simply involves marking all the nodes
 * in the LEB being garbage-collected as dirty.  The dirty nodes are written
 * next commit, after which the LEB is free to be reused.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int lpt_gc_lnum(struct ubifs_info *c, int lnum)
{
	int err, len = c->leb_size, node_type, node_num, node_len, offs;
	void *buf = c->lpt_buf;

	dbg_lpt("LEB %d", lnum);
	err = ubi_read(c->ubi, lnum, buf, 0, c->leb_size);
	if (err) {
		ubifs_err("cannot read LEB %d, error %d", lnum, err);
		return err;
	}
	while (1) {
		if (!is_a_node(c, buf, len)) {
			int pad_len;

			pad_len = get_pad_len(c, buf, len);
			if (pad_len) {
				buf += pad_len;
				len -= pad_len;
				continue;
			}
			return 0;
		}
		node_type = get_lpt_node_type(c, buf, &node_num);
		node_len = get_lpt_node_len(c, node_type);
		offs = c->leb_size - len;
		ubifs_assert(node_len != 0);
		mutex_lock(&c->lp_mutex);
		err = make_node_dirty(c, node_type, node_num, lnum, offs);
		mutex_unlock(&c->lp_mutex);
		if (err)
			return err;
		buf += node_len;
		len -= node_len;
	}
	return 0;
}

/**
 * lpt_gc - LPT garbage collection.
 * @c: UBIFS file-system description object
 *
 * Select a LPT LEB for LPT garbage collection and call lpt_gc_lnum.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int lpt_gc(struct ubifs_info *c)
{
	int i, lnum = -1, dirty = 0;

	mutex_lock(&c->lp_mutex);
	for (i = 0; i < c->lpt_lebs; i++) {
                ubifs_assert(!c->ltab[i].tgc);
                if (i + c->lpt_first == c->nhead_lnum ||
                    c->ltab[i].free + c->ltab[i].dirty == c->leb_size)
                        continue;
		if (c->ltab[i].dirty > dirty) {
			dirty = c->ltab[i].dirty;
			lnum = i + c->lpt_first;
		}
	}
	mutex_unlock(&c->lp_mutex);
	if (lnum == -1)
		return -ENOSPC;
	return lpt_gc_lnum(c, lnum);
}

/**
 * need_write_all - determine if the LPT area is running out of free space.
 * @c: UBIFS file-system description object
 *
 * This function returns %1 if the LPT area is running out of free space and %0
 * if it is not.
 */
static int need_write_all(struct ubifs_info *c)
{
	long long free = 0;
	int i;

	for (i = 0; i < c->lpt_lebs; i++) {
		if (i + c->lpt_first == c->nhead_lnum)
			free += c->leb_size - c->nhead_offs;
		else if (c->ltab[i].free == c->leb_size)
			free += c->leb_size;
		else if (c->ltab[i].free + c->ltab[i].dirty == c->leb_size)
			free += c->leb_size;
	}
	/* Less than twice the size left */
	if (free <= c->lpt_sz * 2)
		return 1;
	return 0;
}

/**
 * lpt_tgc_start - start trivial garbage collection of LPT LEBs.
 * @c: UBIFS file-system description object
 *
 * LPT trivial garbage collection is where a LPT LEB contains only dirty and
 * free space and so may be reused as soon as the next commit is completed.
 * This function is called during start commit to mark LPT LEBs for trivial GC.
 */
static void lpt_tgc_start(struct ubifs_info *c)
{
	int i;

	for (i = 0; i < c->lpt_lebs; i++) {
		if (i + c->lpt_first == c->nhead_lnum)
			continue;
		if (c->ltab[i].dirty > 0 &&
		    c->ltab[i].free + c->ltab[i].dirty == c->leb_size) {
			c->ltab[i].tgc = 1;
			c->ltab[i].free = c->leb_size;
			c->ltab[i].dirty = 0;
			dbg_lpt("LEB %d", i + c->lpt_first);
		}
	}
}

/**
 * lpt_tgc_end - end trivial garbage collection of LPT LEBs.
 * @c: UBIFS file-system description object
 *
 * LPT trivial garbage collection is where a LPT LEB contains only dirty and
 * free space and so may be reused as soon as the next commit is completed.
 * This function is called after the commit is completed (master node has been
 * written) and unmaps LPT LEBs that were marked for trivial GC.
 */
static int lpt_tgc_end(struct ubifs_info *c)
{
	int i, err;

	for (i = 0; i < c->lpt_lebs; i++)
		if (c->ltab[i].tgc) {
			err = unmap_leb(c, i + c->lpt_first);
			if (err)
				return err;
			c->ltab[i].tgc = 0;
			dbg_lpt("LEB %d", i + c->lpt_first);
		}
	return 0;
}

/**
 * ubifs_lpt_post_commit - post commit LPT trivial GC and LPT GC.
 * @c: UBIFS file-system description object
 *
 * LPT trivial GC is completed after a commit. Also LPT GC is done after a
 * commit for the "big" LPT model.
 */
int ubifs_lpt_post_commit(struct ubifs_info *c)
{
	int err;

	mutex_lock(&c->lp_mutex);
	err = lpt_tgc_end(c);
	if (err)
		goto out;
	if (c->big_lpt)
		while (need_write_all(c)) {
			mutex_unlock(&c->lp_mutex);
			err = lpt_gc(c);
			if (err)
				return err;
			mutex_lock(&c->lp_mutex);
		}
out:
	mutex_unlock(&c->lp_mutex);
	return err;
}

/**
 * populate_lsave - fill the lsave array with important LEB numbers.
 * @c: the UBIFS file-system description object
 *
 * This function is only called for the "big" model. It records a small number
 * of LEB numbers of important LEBs.  Important LEBs are ones that are (from
 * most important to least important): empty, freeable, freeable index, dirty
 * index, dirty or free. Upon mount, we read this list of LEB numbers and bring
 * their pnodes into memory.  That will stop us from having to scan the LPT
 * straight away. For the "small" model we assume that scanning the LPT is no
 * big deal.
 */
static void populate_lsave(struct ubifs_info *c)
{
	struct ubifs_lprops *lprops;
	struct ubifs_lpt_heap *heap;
	int i, cnt = 0;

	ubifs_assert(c->big_lpt);
	if (!(c->lpt_drty_flgs & LSAVE_DIRTY)) {
		c->lpt_drty_flgs |= LSAVE_DIRTY;
		add_lpt_dirt(c, c->lsave_lnum, c->lsave_sz);
	}
	list_for_each_entry(lprops, &c->empty_list, list) {
		c->lsave[cnt++] = lprops->lnum;
		if (cnt >= c->lsave_cnt)
			return;
	}
	list_for_each_entry(lprops, &c->freeable_list, list) {
		c->lsave[cnt++] = lprops->lnum;
		if (cnt >= c->lsave_cnt)
			return;
	}
	list_for_each_entry(lprops, &c->frdi_idx_list, list) {
		c->lsave[cnt++] = lprops->lnum;
		if (cnt >= c->lsave_cnt)
			return;
	}
	heap = &c->lpt_heap[LPROPS_DIRTY_IDX - 1];
	for (i = 0; i < heap->cnt; i++) {
		c->lsave[cnt++] = heap->arr[i]->lnum;
		if (cnt >= c->lsave_cnt)
			return;
	}
	heap = &c->lpt_heap[LPROPS_DIRTY - 1];
	for (i = 0; i < heap->cnt; i++) {
		c->lsave[cnt++] = heap->arr[i]->lnum;
		if (cnt >= c->lsave_cnt)
			return;
	}
	heap = &c->lpt_heap[LPROPS_FREE - 1];
	for (i = 0; i < heap->cnt; i++) {
		c->lsave[cnt++] = heap->arr[i]->lnum;
		if (cnt >= c->lsave_cnt)
			return;
	}
	/* Fill it up completely */
	while (cnt < c->lsave_cnt)
		c->lsave[cnt++] = c->main_first;
}

/**
 * ubifs_lpt_start_commit - UBIFS commit starts.
 * @c: the UBIFS file-system description object
 *
 * This function has to be called when UBIFS starts the commit operation.
 * This function "freezes" all currently dirty LEB properties and does not
 * change them anymore. Further changes are saved and tracked separately
 * because they are not part of this commit. This function returns zero in case
 * of success and a negative error code in case of failure.
 */
int ubifs_lpt_start_commit(struct ubifs_info *c)
{
	int err, cnt;

	dbg_lp("");

	mutex_lock(&c->lp_mutex);

	err = dbg_check_ltab(c);
	if (err)
		goto out;

	lpt_tgc_start(c);

	if (!c->dirty_pn_cnt) {
		dbg_cmt("no cnodes to commit");
		err = 0;
		goto out;
	}

	if (!c->big_lpt && need_write_all(c)) {
		/* If needed, write everything */
		err = make_tree_dirty(c);
		if (err)
			goto out;
		lpt_tgc_start(c);
	}

	if (c->big_lpt)
		populate_lsave(c);

	cnt = get_cnodes_to_commit(c);
	ubifs_assert(cnt != 0);

	err = layout_cnodes(c);
	if (err)
		goto out;

	/* Copy the LPT's own lprops for end commit to write */
	memcpy(c->ltab_cmt, c->ltab,
	       sizeof(struct ubifs_lpt_lprops) * c->lpt_lebs);
	c->lpt_drty_flgs &= ~(LTAB_DIRTY | LSAVE_DIRTY);
out:
	mutex_unlock(&c->lp_mutex);
	return err;
}

/**
 * ubifs_lpt_end_commit - finish the commit operation.
 * @c: the UBIFS file-system description object
 *
 * This function has to be called when the commit operation finishes. It
 * flushes the changes which were "frozen" by 'ubifs_lprops_start_commit()' to
 * the media. Returns zero in case of success and a negative error code in case
 * of failure.
 */
int ubifs_lpt_end_commit(struct ubifs_info *c)
{
	int err;

	dbg_lp("");

	if (!c->lpt_cnext)
		return 0;

	err = write_cnodes(c);
	if (err)
		return err;

	mutex_lock(&c->lp_mutex);
	free_obsolete_cnodes(c);
	mutex_unlock(&c->lp_mutex);

	return 0;
}

/**
 * struct lpt_scan_node - somewhere to put nodes while we scan LPT.
 * @nnode: where to keep a nnode
 * @pnode: where to keep a pnode
 * @cnode: where to keep a cnode
 * @in_tree: is the node in the tree in memory
 * @ptr.nnode: pointer to the nnode (if it is an nnode) which may be here or in
 * the tree
 * @ptr.pnode: ditto for pnode
 * @ptr.cnode: ditto for cnode
 */
struct lpt_scan_node
{
	union {
		struct ubifs_nnode nnode;
		struct ubifs_pnode pnode;
		struct ubifs_cnode cnode;
	};
	int in_tree;
	union {
		struct ubifs_nnode *nnode;
		struct ubifs_pnode *pnode;
		struct ubifs_cnode *cnode;
	} ptr;
};

/**
 * scan_get_nnode - for the scan, get a nnode from either the tree or flash.
 * @c: the UBIFS file-system description object
 * @path: where to put the nnode
 * @parent: parent of the nnode
 * @iip: index in parent of the nnode
 *
 * This function returns a pointer to the nnode on success or a negative error
 * code on failure.
 */
static struct ubifs_nnode *scan_get_nnode(struct ubifs_info *c,
					  struct lpt_scan_node *path,
					  struct ubifs_nnode *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_nnode *nnode;
	void *buf = c->lpt_nod_buf;
	int err;

	branch = &parent->nbranch[iip];
	nnode = branch->nnode;
	if (nnode) {
		path->in_tree = 1;
		path->ptr.nnode = nnode;
		return nnode;
	}
	nnode = &path->nnode;
	path->in_tree = 0;
	path->ptr.nnode = nnode;
	memset(nnode, 0, sizeof(struct ubifs_nnode));
	if (branch->lnum == 0) {
		/*
		 * This nnode was not written which just means that the LEB
		 * properties in the subtree below it describe empty LEBs. We
		 * make the nnode as though we had read it, which in fact means
		 * doing almost nothing.
		 */
		if (c->big_lpt)
			nnode->num = calc_nnode_num_from_parent(c, parent, iip);
	} else {
		err = ubi_read(c->ubi, branch->lnum, buf, branch->offs,
			       c->nnode_sz);
		if (err)
			return ERR_PTR(err);
		err = unpack_nnode(c, buf, nnode);
		if (err)
			return ERR_PTR(err);
	}
	err = validate_nnode(c, nnode, parent, iip);
	if (err)
		return ERR_PTR(err);
	if (!c->big_lpt)
		nnode->num = calc_nnode_num_from_parent(c, parent, iip);
	nnode->level = parent->level - 1;
	nnode->parent = parent;
	nnode->iip = iip;
	return nnode;
}

/**
 * scan_get_pnode - for the scan, get a pnode from either the tree or flash.
 * @c: the UBIFS file-system description object
 * @path: where to put the pnode
 * @parent: parent of the pnode
 * @iip: index in parent of the pnode
 *
 * This function returns a pointer to the pnode on success or a negative error
 * code on failure.
 */
static struct ubifs_pnode *scan_get_pnode(struct ubifs_info *c,
					  struct lpt_scan_node *path,
					  struct ubifs_nnode *parent, int iip)
{
	struct ubifs_nbranch *branch;
	struct ubifs_pnode *pnode;
	void *buf = c->lpt_nod_buf;
	int err;

	branch = &parent->nbranch[iip];
	pnode = branch->pnode;
	if (pnode) {
		path->in_tree = 1;
		path->ptr.pnode = pnode;
		return pnode;
	}
	pnode = &path->pnode;
	path->in_tree = 0;
	path->ptr.pnode = pnode;
	memset(pnode, 0, sizeof(struct ubifs_pnode));
	if (branch->lnum == 0) {
		/*
		 * This pnode was not written which just means that the LEB
		 * properties in it describe empty LEBs. We make the pnode as
		 * though we had read it.
		 */
		int i;

		if (c->big_lpt)
			pnode->num = calc_pnode_num_from_parent(c, parent, iip);
		for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
			struct ubifs_lprops * const lprops = &pnode->lprops[i];

			lprops->free = c->leb_size;
			lprops->flags = ubifs_categorize_lprops(c, lprops);
		}
	} else {
		ubifs_assert(branch->lnum >= c->lpt_first &&
			     branch->lnum <= c->lpt_last);
		ubifs_assert(branch->offs >= 0 && branch->offs < c->leb_size);
		err = ubi_read(c->ubi, branch->lnum, buf, branch->offs,
			       c->pnode_sz);
		if (err)
			return ERR_PTR(err);
		err = unpack_pnode(c, buf, pnode);
		if (err)
			return ERR_PTR(err);
	}
	err = validate_pnode(c, pnode, parent, iip);
	if (err)
		return ERR_PTR(err);
	if (!c->big_lpt)
		pnode->num = calc_pnode_num_from_parent(c, parent, iip);
	pnode->parent = parent;
	pnode->iip = iip;
	set_pnode_lnum(c, pnode);
	return pnode;
}

/**
 * ubifs_lpt_scan_nolock - scan the LPT.
 * @c: the UBIFS file-system description object
 * @start_lnum: LEB number from which to start scanning
 * @end_lnum: LEB number at which to stop scanning
 * @scan_cb: callback function called for each lprops
 * @data: data to be passed to the callback function
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_lpt_scan_nolock(struct ubifs_info *c, int start_lnum, int end_lnum,
			  ubifs_lpt_scan_callback scan_cb, void *data)
{
	int err = 0, i, h, iip, shft;
	struct ubifs_nnode *nnode;
	struct ubifs_pnode *pnode;
	struct lpt_scan_node *path;

	if (start_lnum == -1) {
		start_lnum = end_lnum + 1;
		if (start_lnum >= c->leb_cnt)
			start_lnum = c->main_first;
	}

	ubifs_assert(start_lnum >= c->main_first && start_lnum < c->leb_cnt);
	ubifs_assert(end_lnum >= c->main_first && end_lnum < c->leb_cnt);

	if (!c->nroot) {
		err = read_nnode(c, NULL, 0);
		if (err)
			return err;
	}

	path = kmalloc(sizeof(struct lpt_scan_node) * (c->lpt_hght + 1),
		       GFP_NOFS);
	if (!path)
		return -ENOMEM;

	path[0].ptr.nnode = c->nroot;
	path[0].in_tree = 1;
again:
	/* Descend to the pnode containing start_lnum */
	nnode = c->nroot;
	i = start_lnum - c->main_first;
	shft = c->lpt_hght * UBIFS_LPT_FANOUT_SHIFT;
	for (h = 1; h < c->lpt_hght; h++) {
		iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
		shft -= UBIFS_LPT_FANOUT_SHIFT;
		nnode = scan_get_nnode(c, path + h, nnode, iip);
		if (IS_ERR(nnode)) {
			err = PTR_ERR(nnode);
			goto out;
		}
	}
	iip = ((i >> shft) & (UBIFS_LPT_FANOUT - 1));
	shft -= UBIFS_LPT_FANOUT_SHIFT;
	pnode = scan_get_pnode(c, path + h, nnode, iip);
	if (IS_ERR(pnode)) {
		err = PTR_ERR(pnode);
		goto out;
	}
	iip = (i & (UBIFS_LPT_FANOUT - 1));

	/* Loop for each lprops */
	while (1) {
		struct ubifs_lprops *lprops = &pnode->lprops[iip];
		int ret, lnum = lprops->lnum;

		ret = scan_cb(c, lprops, path[h].in_tree, data);
		if (ret < 0) {
			err = ret;
			goto out;
		}
		if (ret & LPT_SCAN_ADD) {
			/* Add all the nodes in path to the tree in memory */
			for (h = 1; h < c->lpt_hght; h++) {
				const size_t sz = sizeof(struct ubifs_nnode);
				struct ubifs_nnode *parent;

				if (path[h].in_tree)
					continue;
				nnode = kmalloc(sz, GFP_NOFS);
				if (!nnode) {
					err = -ENOMEM;
					goto out;
				}
				memcpy(nnode, &path[h].nnode, sz);
				parent = nnode->parent;
				parent->nbranch[nnode->iip].nnode = nnode;
				path[h].ptr.nnode = nnode;
				path[h].in_tree = 1;
				path[h + 1].cnode.parent = nnode;
			}
			if (path[h].in_tree)
				ubifs_ensure_cat(c, lprops);
			else {
				const size_t sz = sizeof(struct ubifs_pnode);
				struct ubifs_nnode *parent;

				pnode = kmalloc(sz, GFP_NOFS);
				if (!pnode) {
					err = -ENOMEM;
					goto out;
				}
				memcpy(pnode, &path[h].pnode, sz);
				parent = pnode->parent;
				parent->nbranch[pnode->iip].pnode = pnode;
				path[h].ptr.pnode = pnode;
				path[h].in_tree = 1;
				update_cats(c, pnode);
				c->pnodes_have += 1;
			}
			err = dbg_chk_nodes(c, (struct ubifs_cnode *)c->nroot,
					    0, 0);
			if (err)
				return err;
			err = dbg_check_cats(c);
			if (err)
				goto out;
		}
		if (ret & LPT_SCAN_STOP) {
			err = 0;
			break;
		}
		/* Get the next lprops */
		if (lnum == end_lnum) {
			/*
			 * We got to the end without finding what we were
			 * looking for
			 */
			err = -ENOSPC;
			goto out;
		}
		if (lnum + 1 >= c->leb_cnt) {
			/* Wrap-around to the beginning */
			start_lnum = c->main_first;
			goto again;
		}
		if (iip + 1 < UBIFS_LPT_FANOUT) {
			/* Next lprops is in the same pnode */
			iip += 1;
			continue;
		}
		/* We need to get the next pnode. Go up until we can go right */
		iip = pnode->iip;
		while (1) {
			h -= 1;
			ubifs_assert(h >= 0);
			nnode = path[h].ptr.nnode;
			if (iip + 1 < UBIFS_LPT_FANOUT)
				break;
			iip = nnode->iip;
		}
		/* Go right */
		iip += 1;
		/* Descend to the pnode */
		h += 1;
		for (; h < c->lpt_hght; h++) {
			nnode = scan_get_nnode(c, path + h, nnode, iip);
			if (IS_ERR(nnode)) {
				err = PTR_ERR(nnode);
				goto out;
			}
			iip = 0;
		}
		pnode = scan_get_pnode(c, path + h, nnode, iip);
		if (IS_ERR(pnode)) {
			err = PTR_ERR(pnode);
			goto out;
		}
		iip = 0;
	}
out:
	kfree(path);
	return err;
}

#if defined(CONFIG_UBIFS_FS_DEBUG_CHK_LPROPS) || \
    defined(CONFIG_UBIFS_FS_DEBUG_CHK_OTHER)

/**
 * dbg_chk_pnode - check a pnode.
 * @c: the UBIFS file-system description object
 * @pnode: pnode to check
 * @col: pnode column
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int dbg_chk_pnode(struct ubifs_info *c, struct ubifs_pnode *pnode,
			 int col)
{
	int i;

	if (pnode->num != col) {
		dbg_err("pnode num %d expected %d parent num %d iip %d",
			pnode->num, col, pnode->parent->num, pnode->iip);
		return -EINVAL;
	}
	for (i = 0; i < UBIFS_LPT_FANOUT; i++) {
		struct ubifs_lprops *lp, *lprops = &pnode->lprops[i];
		int lnum = (pnode->num << UBIFS_LPT_FANOUT_SHIFT) + i +
			   c->main_first;
		int found, cat = lprops->flags & LPROPS_CAT_MASK;
		struct ubifs_lpt_heap *heap;
		struct list_head *list = NULL;

		if (lnum >= c->leb_cnt)
			continue;
		if (lprops->lnum != lnum) {
			dbg_err("bad LEB number %d expected %d",
				lprops->lnum, lnum);
			return -EINVAL;
		}
		if (lprops->flags & LPROPS_TAKEN) {
			if (cat != LPROPS_UNCAT) {
				dbg_err("LEB %d taken but not uncat %d",
					lprops->lnum, cat);
				return -EINVAL;
			}
			continue;
		}
		if (lprops->flags & LPROPS_INDEX) {
			switch (cat) {
			case LPROPS_UNCAT:
			case LPROPS_DIRTY_IDX:
			case LPROPS_FRDI_IDX:
				break;
			default:
				dbg_err("LEB %d index but cat %d",
					lprops->lnum, cat);
				return -EINVAL;
			}
		} else {
			switch (cat) {
			case LPROPS_UNCAT:
			case LPROPS_DIRTY:
			case LPROPS_FREE:
			case LPROPS_EMPTY:
			case LPROPS_FREEABLE:
				break;
			default:
				dbg_err("LEB %d not index but cat %d",
					lprops->lnum, cat);
				return -EINVAL;
			}
		}
		switch (cat) {
		case LPROPS_UNCAT:
			list = &c->uncat_list;
			break;
		case LPROPS_EMPTY:
			list = &c->empty_list;
			break;
		case LPROPS_FREEABLE:
			list = &c->freeable_list;
			break;
		case LPROPS_FRDI_IDX:
			list = &c->frdi_idx_list;
			break;
		}
		found = 0;
		switch (cat) {
		case LPROPS_DIRTY:
		case LPROPS_DIRTY_IDX:
		case LPROPS_FREE:
			heap = &c->lpt_heap[cat - 1];
			if (lprops->hpos < heap->cnt &&
			    heap->arr[lprops->hpos] == lprops)
				found = 1;
			break;
		case LPROPS_UNCAT:
		case LPROPS_EMPTY:
		case LPROPS_FREEABLE:
		case LPROPS_FRDI_IDX:
			list_for_each_entry(lp, list, list)
				if (lprops == lp) {
					found = 1;
					break;
				}
			break;
		}
		if (!found) {
			dbg_err("LEB %d cat %d not found in cat heap/list",
				lprops->lnum, cat);
			return -EINVAL;
		}
		switch (cat) {
		case LPROPS_EMPTY:
			if (lprops->free != c->leb_size) {
				dbg_err("LEB %d cat %d free %d dirty %d",
					lprops->lnum, cat, lprops->free,
					lprops->dirty);
				return -EINVAL;
			}
		case LPROPS_FREEABLE:
		case LPROPS_FRDI_IDX:
			if (lprops->free + lprops->dirty != c->leb_size) {
				dbg_err("LEB %d cat %d free %d dirty %d",
					lprops->lnum, cat, lprops->free,
					lprops->dirty);
				return -EINVAL;
			}
		}
	}
	return 0;
}

/**
 * dbg_chk_nodes - check nnodes and pnodes.
 * @c: the UBIFS file-system description object
 * @cnode: next cnode (nnode or pnode) to check
 * @row: row of cnode (root is zero)
 * @col: column of cnode (leftmost is zero)
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int dbg_chk_nodes(struct ubifs_info *c, struct ubifs_cnode *cnode,
			 int row, int col)
{
	struct ubifs_nnode *nnode, *nn;
	struct ubifs_cnode *cn;
	int num, iip = 0, err;

	while (cnode) {
		ubifs_assert(row >= 0);
		nnode = cnode->parent;
		if (cnode->level) {
			/* cnode is a nnode */
			num = calc_nnode_num(row, col);
			if (cnode->num != num) {
				dbg_err("nnode num %d expected %d "
					"parent num %d iip %d", cnode->num, num,
					(nnode ? nnode->num : 0), cnode->iip);
				return -EINVAL;
			}
			nn = (struct ubifs_nnode *)cnode;
			while (iip < UBIFS_LPT_FANOUT) {
				cn = nn->nbranch[iip].cnode;
				if (cn) {
					/* Go down */
					row += 1;
					col <<= UBIFS_LPT_FANOUT_SHIFT;
					col += iip;
					iip = 0;
					cnode = cn;
					break;
				}
				/* Go right */
				iip += 1;
			}
			if (iip < UBIFS_LPT_FANOUT)
				continue;
		} else {
			struct ubifs_pnode *pnode;

			/* cnode is a pnode */
			pnode = (struct ubifs_pnode *)cnode;
			err = dbg_chk_pnode(c, pnode, col);
			if (err)
				return err;
		}
		/* Go up and to the right */
		row -= 1;
		col >>= UBIFS_LPT_FANOUT_SHIFT;
		iip = cnode->iip + 1;
		cnode = (struct ubifs_cnode *)nnode;
	}
	return 0;
}

#endif

#ifdef CONFIG_UBIFS_FS_DEBUG_CHK_LPROPS

/**
 * dbg_is_all_ff - determine if a buffer contains only 0xff bytes.
 * @buf: buffer
 * @len: buffer length
 */
static int dbg_is_all_ff(uint8_t *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
		if (buf[i] != 0xff)
			return 0;
	return 1;
}

/**
 * dbg_is_nnode_dirty - determine if a nnode is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where nnode was written
 * @offs: offset where nnode was written
 */
static int dbg_is_nnode_dirty(struct ubifs_info *c, int lnum, int offs)
{
	struct ubifs_nnode *nnode;
	int hght;

	/* Entire tree is in memory so first_nnode / next_nnode are ok */
	nnode = first_nnode(c, &hght);
	for (; nnode; nnode = next_nnode(c, nnode, &hght)) {
		struct ubifs_nbranch *branch;

		cond_resched();
		if (nnode->parent) {
			branch = &nnode->parent->nbranch[nnode->iip];
			if (branch->lnum != lnum || branch->offs != offs)
				continue;
			if (test_bit(DIRTY_CNODE, &nnode->flags))
				return 1;
			return 0;
		} else {
			if (c->lpt_lnum != lnum || c->lpt_offs != offs)
				continue;
			if (test_bit(DIRTY_CNODE, &nnode->flags))
				return 1;
			return 0;
		}
	}
	return 1;
}

/**
 * dbg_is_pnode_dirty - determine if a pnode is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where pnode was written
 * @offs: offset where pnode was written
 */
static int dbg_is_pnode_dirty(struct ubifs_info *c, int lnum, int offs)
{
	int i, cnt;

	cnt = DIV_ROUND_UP(c->main_lebs, UBIFS_LPT_FANOUT);
	for (i = 0; i < cnt; i++) {
		struct ubifs_pnode *pnode;
		struct ubifs_nbranch *branch;

		cond_resched();
		pnode = pnode_lookup(c, i);
		if (IS_ERR(pnode))
			return PTR_ERR(pnode);
		branch = &pnode->parent->nbranch[pnode->iip];
		if (branch->lnum != lnum || branch->offs != offs)
			continue;
		if (test_bit(DIRTY_CNODE, &pnode->flags))
			return 1;
		return 0;
	}
	return 1;
}

/**
 * dbg_is_ltab_dirty - determine if a ltab node is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where ltab node was written
 * @offs: offset where ltab node was written
 */
static int dbg_is_ltab_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->ltab_lnum || offs != c->ltab_offs)
		return 1;
	return (c->lpt_drty_flgs & LTAB_DIRTY) != 0;
}

/**
 * dbg_is_lsave_dirty - determine if a lsave node is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where lsave node was written
 * @offs: offset where lsave node was written
 */
static int dbg_is_lsave_dirty(struct ubifs_info *c, int lnum, int offs)
{
	if (lnum != c->lsave_lnum || offs != c->lsave_offs)
		return 1;
	return (c->lpt_drty_flgs & LSAVE_DIRTY) != 0;
}

/**
 * dbg_is_node_dirty - determine if a node is dirty.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where node was written
 * @offs: offset where node was written
 */
static int dbg_is_node_dirty(struct ubifs_info *c, int node_type, int lnum,
			     int offs)
{
	switch (node_type) {
	case UBIFS_LPT_NNODE:
		return dbg_is_nnode_dirty(c, lnum, offs);
	case UBIFS_LPT_PNODE:
		return dbg_is_pnode_dirty(c, lnum, offs);
	case UBIFS_LPT_LTAB:
		return dbg_is_ltab_dirty(c, lnum, offs);
	case UBIFS_LPT_LSAVE:
		return dbg_is_lsave_dirty(c, lnum, offs);
	}
	return 1;
}

/**
 * dbg_check_ltab_lnum - check the ltab for a LPT LEB number.
 * @c: the UBIFS file-system description object
 * @lnum: LEB number where node was written
 * @offs: offset where node was written
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int dbg_check_ltab_lnum(struct ubifs_info *c, int lnum)
{
	int err, len = c->leb_size, dirty = 0, node_type, node_num, node_len;
	int ret;
	void *buf = c->dbg_buf;

	dbg_lpt("LEB %d", lnum);
	err = ubi_read(c->ubi, lnum, buf, 0, c->leb_size);
	if (err) {
		dbg_msg("ubi_read failed, LEB %d, error %d", lnum, err);
		return err;
	}
	while (1) {
		if (!is_a_node(c, buf, len)) {
			int i, pad_len;

			pad_len = get_pad_len(c, buf, len);
			if (pad_len) {
				buf += pad_len;
				len -= pad_len;
				dirty += pad_len;
				continue;
			}
			if (!dbg_is_all_ff(buf, len)) {
				dbg_msg("invalid empty space in LEB %d at %d",
					lnum, c->leb_size - len);
				err = -EINVAL;
			}
			i = lnum - c->lpt_first;
			if (len != c->ltab[i].free) {
				dbg_msg("invalid free space in LEB %d "
					"(free %d, expected %d)",
					lnum, len, c->ltab[i].free);
				err = -EINVAL;
			}
			if (dirty != c->ltab[i].dirty) {
				dbg_msg("invalid dirty space in LEB %d "
					"(dirty %d, expected %d)",
					lnum, dirty, c->ltab[i].dirty);
				err = -EINVAL;
			}
			return err;
		}
		node_type = get_lpt_node_type(c, buf, &node_num);
		node_len = get_lpt_node_len(c, node_type);
		ret = dbg_is_node_dirty(c, node_type, lnum, c->leb_size - len);
		if (ret == 1)
			dirty += node_len;
		buf += node_len;
		len -= node_len;
	}
}

/**
 * dbg_check_ltab - check the free and dirty space in the ltab.
 * @c: the UBIFS file-system description object
 *
 * This function returns %0 on success and a negative error code on failure.
 */
static int dbg_check_ltab(struct ubifs_info *c)
{
	int lnum, err, i, cnt;

	/* Bring the entire tree into memory */
	cnt = DIV_ROUND_UP(c->main_lebs, UBIFS_LPT_FANOUT);
	for (i = 0; i < cnt; i++) {
		struct ubifs_pnode *pnode;

		pnode = pnode_lookup(c, i);
		if (IS_ERR(pnode))
			return PTR_ERR(pnode);
		cond_resched();
	}

	/* Check nodes */
	err = dbg_chk_nodes(c, (struct ubifs_cnode *)c->nroot, 0, 0);
	if (err)
		return err;

	/* Check each LEB */
	for (lnum = c->lpt_first; lnum <= c->lpt_last; lnum++) {
		err = dbg_check_ltab_lnum(c, lnum);
		if (err) {
			dbg_err("failed at LEB %d", lnum);
			return err;
		}
	}

	dbg_lpt("succeeded");

	return 0;
}

#endif /* CONFIG_UBIFS_FS_DEBUG_CHK_LPROPS */
