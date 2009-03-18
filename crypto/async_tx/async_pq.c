/*
 *	Copyright(c) 2007 Yuri Tikhonov <yur@emcraft.com>
 *	Copyright(c) 2009 Intel Corporation
 *
 *	Developed for DENX Software Engineering GmbH
 *
 *	Asynchronous GF-XOR calculations ASYNC_TX API.
 *
 *	based on async_xor.c code written by:
 *		Dan Williams <dan.j.williams@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called COPYING.
 */
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/raid/raid6.h>
#include <linux/async_tx.h>

/**
 * spare_pages - synchronous zero sum result buffers
 *
 * Protected by spare_lock
 */
static struct page *spare_pages[2];
static spinlock_t spare_lock;

/* scribble - space to hold throwaway P buffer for synchronous gen_syndrome */
static struct page *scribble;

static bool is_raid6_zero_block(void *p)
{
	return p == (void *) raid6_empty_zero_page;
}

/**
 * do_async_pq - asynchronously calculate P and/or Q
 */
static __async_inline struct dma_async_tx_descriptor *
do_async_pq(struct dma_chan *chan, struct page **blocks, unsigned char *scfs,
	    unsigned int offset, int src_cnt, size_t len,
	    enum async_tx_flags flags,
	    struct dma_async_tx_descriptor *depend_tx,
	    dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_device *dma = chan->device;
	dma_addr_t dma_dest[2], dma_src[src_cnt];
	struct dma_async_tx_descriptor *tx = NULL;
	dma_async_tx_callback _cb_fn;
	void *_cb_param;
	unsigned char *scf = NULL;
	int i, src_off = 0;
	unsigned short pq_src_cnt;
	enum async_tx_flags async_flags;
	enum dma_ctrl_flags dma_flags = 0;
	int idx;
	u8 coefs[src_cnt];

	/* DMAs use destinations as sources, so use BIDIRECTIONAL mapping */
	if (blocks[src_cnt])
		dma_dest[0] = dma_map_page(dma->dev, blocks[src_cnt],
					   offset, len, DMA_BIDIRECTIONAL);
	else
		dma_flags |= DMA_PREP_PQ_DISABLE_P;
	if (blocks[src_cnt+1])
		dma_dest[1] = dma_map_page(dma->dev, blocks[src_cnt+1],
					   offset, len, DMA_BIDIRECTIONAL);
	else
		dma_flags |= DMA_PREP_PQ_DISABLE_Q;

	/* convert source addresses being careful to collapse 'zero'
	 * sources and update the coefficients accordingly
	 */
	for (i = 0, idx = 0; i < src_cnt; i++) {
		if (is_raid6_zero_block(blocks[i]))
			continue;
		dma_src[idx] = dma_map_page(dma->dev, blocks[i],
					    offset, len, DMA_TO_DEVICE);
		coefs[idx] = scfs[i];
		idx++;
	}
	src_cnt = idx;

	while (src_cnt > 0) {
		async_flags = flags;
		pq_src_cnt = min(src_cnt, dma_maxpq(dma, flags));
		/* if we are submitting additional pqs, leave the chain open,
		 * clear the callback parameters, and leave the destination
		 * buffers mapped
		 */
		if (src_cnt > pq_src_cnt) {
			async_flags &= ~ASYNC_TX_ACK;
			dma_flags |= DMA_COMPL_SKIP_DEST_UNMAP;
			_cb_fn = NULL;
			_cb_param = NULL;
		} else {
			_cb_fn = cb_fn;
			_cb_param = cb_param;
		}
		if (_cb_fn)
			dma_flags |= DMA_PREP_INTERRUPT;
		if (scfs)
			scf = &scfs[src_off];

		/* Since we have clobbered the src_list we are committed
		 * to doing this asynchronously.  Drivers force forward
		 * progress in case they can not provide a descriptor
		 */
		tx = dma->device_prep_dma_pq(chan, dma_dest,
					     &dma_src[src_off], pq_src_cnt,
					     scf, len, dma_flags);
		if (unlikely(!tx))
			async_tx_quiesce(&depend_tx);

		/* spin wait for the preceeding transactions to complete */
		while (unlikely(!tx)) {
			dma_async_issue_pending(chan);
			tx = dma->device_prep_dma_pq(chan, dma_dest,
					&dma_src[src_off], pq_src_cnt,
					scf, len, dma_flags);
		}

		async_tx_submit(chan, tx, async_flags, depend_tx,
				_cb_fn, _cb_param);

		depend_tx = tx;
		flags |= ASYNC_TX_DEP_ACK;

		/* drop completed sources */
		src_cnt -= pq_src_cnt;
		src_off += pq_src_cnt;

		dma_flags |= DMA_PREP_CONTINUE;
	}

	return tx;
}

/**
 * do_sync_pq - synchronously calculate P and Q
 */
static void
do_sync_pq(struct page **blocks, unsigned char *scfs, unsigned int offset,
	int src_cnt, size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	u8 *p = NULL;
	u8 *q = NULL;
	u8 *ptrs[src_cnt];
	int d, z;
	u8 wd, wq, wp;

	/* address convert inputs */
	if (blocks[src_cnt])
		p = (u8 *)(page_address(blocks[src_cnt]) + offset);
	if (blocks[src_cnt+1])
		q = (u8 *)(page_address(blocks[src_cnt+1]) + offset);
	for (z = 0; z < src_cnt; z++) {
		if (is_raid6_zero_block(blocks[z]))
			ptrs[z] = (void *) blocks[z];
		else
			ptrs[z] = (u8 *)(page_address(blocks[z]) + offset);
	}

	for (d = 0; d < len; d++) {
		wq = wp = ptrs[0][d];
		for (z = 1; z < src_cnt; z++) {
			wd = ptrs[z][d];
			wp ^= wd;
			wq ^= raid6_gfmul[scfs[z]][wd];
		}
		if (p)
			p[d] = wp;
		if (q)
			q[d] = wq;
	}

	async_tx_sync_epilog(cb_fn, cb_param);
}

/**
 * async_pq - attempt to do XOR and Galois calculations in parallel using
 *	a dma engine.
 * @blocks: source block array from 0 to (src_cnt-1) with the p destination
 *	at blocks[src_cnt] and q at blocks[src_cnt + 1]. Only one of two
 *	destinations may be present (another then has to be set to NULL).
 *	NOTE: client code must assume the contents of this array are destroyed
 * @offset: offset in pages to start transaction
 * @src_cnt: number of source pages
 * @scfs: array of source coefficients used in GF-multiplication
 * @len: length in bytes
 * @flags: ASYNC_TX_ACK, ASYNC_TX_DEP_ACK, ASYNC_TX_ASYNC_ONLY
 * @depend_tx: depends on the result of this transaction.
 * @cb_fn: function to call when the operation completes
 * @cb_param: parameter to pass to the callback routine
 */
struct dma_async_tx_descriptor *
async_pq(struct page **blocks, unsigned int offset, int src_cnt,
	 unsigned char *scfs, size_t len, enum async_tx_flags flags,
	 struct dma_async_tx_descriptor *depend_tx,
	 dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_chan *chan = async_tx_find_channel(depend_tx, DMA_PQ,
					&blocks[src_cnt], 2,
					blocks, src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;
	bool do_async = false;

	if (device && (src_cnt <= dma_maxpq(device, 0) ||
		       dma_maxpq(device, DMA_PREP_CONTINUE) > 0))
		do_async = true;

	if (!device && (flags & ASYNC_TX_ASYNC_ONLY))
		return NULL;

	if (do_async) {
		/* run pq asynchronously */
		tx = do_async_pq(chan, blocks, scfs, offset, src_cnt, len,
				 flags, depend_tx, cb_fn, cb_param);
	} else {
		/* run pq synchronously */
		if (!blocks[src_cnt+1]) { /* only p requested, just xor */
			flags |= ASYNC_TX_XOR_ZERO_DST;
			return async_xor(blocks[src_cnt], blocks, offset,
					 src_cnt, len, flags, depend_tx,
					 cb_fn, cb_param);
		}

		/* wait for any prerequisite operations */
		async_tx_quiesce(&depend_tx);

		do_sync_pq(blocks, scfs, offset, src_cnt, len, flags,
			depend_tx, cb_fn, cb_param);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_pq);

/**
 * do_sync_gen_syndrome - synchronously calculate P (xor) and Q (Reed-Solomon
 *	code)
 */
static void
do_sync_gen_syndrome(struct page **blocks, unsigned int offset, int src_cnt,
	size_t len, enum async_tx_flags flags,
	struct dma_async_tx_descriptor *depend_tx,
	dma_async_tx_callback cb_fn, void *cb_param)
{
	int i;
	void *tsrc[src_cnt+2];

	for (i = 0; i < src_cnt + 2; i++) {
		if (is_raid6_zero_block(blocks[i]))
			tsrc[i] = (void *) blocks[i];
		else
			tsrc[i] = page_address(blocks[i]) + offset;
	}

	raid6_call.gen_syndrome(i, len, tsrc);

	async_tx_sync_epilog(cb_fn, cb_param);
}

/**
 * async_gen_syndrome - attempt to generate P (xor) and Q (Reed-Solomon code)
 *	with a dma engine for a given set of blocks.  This routine assumes a
 *	field of GF(2^8) with a primitive polynomial of 0x11d and a generator
 *	of {02}.
 * @blocks: source block array ordered from 0..src_cnt-1 with the P destination
 *	at blocks[src_cnt] and Q at blocks[src_cnt + 1]. Only one of two
 *	destinations may be present (another then has to be set to NULL).  Some
 *	raid6 schemes calculate the syndrome over all disks with P and Q set to
 *	zero.  In this case we catch 'zero' blocks with is_raid6_zero_block()
 *	so we can drop them in the async case, or skip the page_address()
 *	conversion in the sync case.
 *	NOTE: client code must assume the contents of this array are destroyed
 * @offset: offset in pages to start transaction
 * @src_cnt: number of source pages: 2 < src_cnt <= 255
 * @len: length of blocks in bytes
 * @flags: ASYNC_TX_ACK, ASYNC_TX_DEP_ACK, ASYNC_TX_ASYNC_ONLY
 * @depend_tx: P+Q operation depends on the result of this transaction.
 * @cb_fn: function to call when P+Q generation completes
 * @cb_param: parameter to pass to the callback routine
 */
struct dma_async_tx_descriptor *
async_gen_syndrome(struct page **blocks, unsigned int offset, int src_cnt,
		   size_t len, enum async_tx_flags flags,
		   struct dma_async_tx_descriptor *depend_tx,
		   dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_chan *chan = async_tx_find_channel(depend_tx, DMA_PQ,
						     &blocks[src_cnt], 2,
						     blocks, src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;
	bool do_async = false;

	BUG_ON(src_cnt > 255 || (!blocks[src_cnt] && !blocks[src_cnt+1]));

	if (device && (src_cnt <= dma_maxpq(device, 0) ||
		       dma_maxpq(device, DMA_PREP_CONTINUE) > 0))
		do_async = true;

	if (!do_async && (flags & ASYNC_TX_ASYNC_ONLY))
		return NULL;

	if (do_async) {
		/* run the p+q asynchronously */
		tx = do_async_pq(chan, blocks, (uint8_t *)raid6_gfexp,
				 offset, src_cnt, len, flags, depend_tx,
				 cb_fn, cb_param);
	} else {
		/* run the pq synchronously */
		/* wait for any prerequisite operations */
		async_tx_quiesce(&depend_tx);

		if (!blocks[src_cnt])
			blocks[src_cnt] = scribble;
		if (!blocks[src_cnt+1])
			blocks[src_cnt+1] = scribble;
		do_sync_gen_syndrome(blocks, offset, src_cnt, len, flags,
				     depend_tx, cb_fn, cb_param);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_gen_syndrome);

static __async_inline enum dma_ctrl_flags
__pq_zero_sum_map_pages(dma_addr_t *dma, int src_cnt, struct device *dev,
			struct page **blocks, unsigned int offset, size_t len)
{
	enum dma_ctrl_flags flags = 0;
	int i;

	if (!blocks[src_cnt])
		flags |= DMA_PREP_PQ_DISABLE_P;
	if (!blocks[src_cnt+1])
		flags |= DMA_PREP_PQ_DISABLE_Q;
	for (i = 0; i < src_cnt + 2; i++)
		if (likely(blocks[i])) {
			dma[i] = dma_map_page(dev, blocks[i], offset, len,
					      DMA_TO_DEVICE);
			BUG_ON(is_raid6_zero_block(blocks[i]));
		}
	return flags;
}

/**
 * async_pq_zero_sum - attempt a PQ parities check with a dma engine.
 * @blocks: array of source pages. The 0..src_cnt-1 are the sources, the
 *	src_cnt and src_cnt+1 are the P and Q destinations to check, resp.
 *	Only one of two destinations may be present.
 *	NOTE: client code must assume the contents of this array are destroyed
 * @offset: offset in pages to start transaction
 * @src_cnt: number of source pages
 * @scfs: coefficients to use in GF-multiplications
 * @len: length in bytes
 * @pqres: SUM_CHECK_P_RESULT and/or SUM_CHECK_Q_RESULT are set on zero sum fail
 * @flags: ASYNC_TX_ACK, ASYNC_TX_DEP_ACK
 * @depend_tx: depends on the result of this transaction.
 * @cb_fn: function to call when the xor completes
 * @cb_param: parameter to pass to the callback routine
 */
struct dma_async_tx_descriptor *
async_pq_zero_sum(struct page **blocks, unsigned int offset, int src_cnt,
		  unsigned char *scfs, size_t len, enum sum_check_flags *pqres,
		  enum async_tx_flags flags,
		  struct dma_async_tx_descriptor *depend_tx,
		  dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_chan *chan = async_tx_find_channel(depend_tx,
						      DMA_PQ_ZERO_SUM,
						      &blocks[src_cnt], 2,
						      blocks, src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;
	enum dma_ctrl_flags dma_flags = cb_fn ? DMA_PREP_INTERRUPT : 0;

	BUG_ON(src_cnt < 2);

	if (device && src_cnt <= dma_maxpq(device, 0) - 2) {
		dma_addr_t dma_src[src_cnt + 2];

		dma_flags |= __pq_zero_sum_map_pages(dma_src, src_cnt,
						     device->dev, blocks,
						     offset, len);
		tx = device->device_prep_dma_pqzero_sum(chan, dma_src, src_cnt,
							scfs, len, pqres,
							dma_flags);

		if (unlikely(!tx)) {
			async_tx_quiesce(&depend_tx);

			while (unlikely(!tx)) {
				dma_async_issue_pending(chan);
				tx = device->device_prep_dma_pqzero_sum(chan,
						dma_src, src_cnt, scfs, len,
						pqres, dma_flags);
			}
		}

		async_tx_submit(chan, tx, flags, depend_tx, cb_fn, cb_param);
	} else {
		struct page *pdest = blocks[src_cnt];
		struct page *qdest = blocks[src_cnt + 1];
		void *p, *q, *s;

		flags &= ~ASYNC_TX_ACK;

		spin_lock(&spare_lock);
		blocks[src_cnt] = spare_pages[0];
		blocks[src_cnt + 1] = spare_pages[1];
		tx = async_pq(blocks, offset, src_cnt, scfs, len, flags,
			      depend_tx, NULL, NULL);
		async_tx_quiesce(&tx);

		*pqres = 0;
		if (pdest) {
			p = page_address(pdest) + offset;
			s = page_address(spare_pages[0]) + offset;
			*pqres |= !!memcmp(p, s, len) << SUM_CHECK_P;
		}

		if (qdest) {
			q = page_address(qdest) + offset;
			s = page_address(spare_pages[1]) + offset;
			*pqres |= !!memcmp(q, s, len) << SUM_CHECK_Q;
		}
		spin_unlock(&spare_lock);

		async_tx_sync_epilog(cb_fn, cb_param);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_pq_zero_sum);

/**
 * async_syndrome_zero_sum - attempt a P (xor) and Q (Reed-Solomon code)
 *	parities check with a dma engine. This routine assumes a field of
 *	GF(2^8) with a primitive polynomial of 0x11d and a generator of {02}.
 * @blocks: array of source pages. The 0..src_cnt-1 are the sources, the
 *	src_cnt and src_cnt+1 are the P and Q destinations to check, resp.
 *	Only one of two destinations may be present.
 *	NOTE: client code must assume the contents of this array are destroyed
 * @offset: offset in pages to start transaction
 * @src_cnt: number of source pages
 * @len: length in bytes
 * @pqres: SUM_CHECK_P_RESULT and/or SUM_CHECK_Q_RESULT are set on zero sum fail
 * @flags: ASYNC_TX_ACK, ASYNC_TX_DEP_ACK
 * @depend_tx: depends on the result of this transaction.
 * @cb_fn: function to call when the xor completes
 * @cb_param: parameter to pass to the callback routine
 */
struct dma_async_tx_descriptor *
async_syndrome_zero_sum(struct page **blocks, unsigned int offset, int src_cnt,
			size_t len, enum sum_check_flags *pqres,
			enum async_tx_flags flags,
			struct dma_async_tx_descriptor *depend_tx,
			dma_async_tx_callback cb_fn, void *cb_param)
{
	struct dma_chan *chan = async_tx_find_channel(depend_tx,
						      DMA_PQ_ZERO_SUM,
						      &blocks[src_cnt], 2,
						      blocks, src_cnt, len);
	struct dma_device *device = chan ? chan->device : NULL;
	struct dma_async_tx_descriptor *tx = NULL;
	enum dma_ctrl_flags dma_flags = cb_fn ? DMA_PREP_INTERRUPT : 0;

	BUG_ON(src_cnt < 2);

	if (device && src_cnt <= dma_maxpq(device, 0) - 2) {
		dma_addr_t dma_src[src_cnt + 2];

		dma_flags |= __pq_zero_sum_map_pages(dma_src, src_cnt,
						     device->dev, blocks,
						     offset, len);
		tx = device->device_prep_dma_pqzero_sum(chan, dma_src, src_cnt,
							(uint8_t *)raid6_gfexp,
							len, pqres, dma_flags);

		if (unlikely(!tx)) {
			async_tx_quiesce(&depend_tx);
			while (unlikely(!tx)) {
				dma_async_issue_pending(chan);
				tx = device->device_prep_dma_pqzero_sum(chan,
						dma_src, src_cnt,
						(uint8_t *)raid6_gfexp, len,
						pqres, dma_flags);
			}
		}

		async_tx_submit(chan, tx, flags, depend_tx, cb_fn, cb_param);
	} else {
		struct page *pdest = blocks[src_cnt];
		struct page *qdest = blocks[src_cnt + 1];
		enum async_tx_flags lflags = flags;
		void *p, *q, *s;

		lflags &= ~ASYNC_TX_ACK;

		spin_lock(&spare_lock);
		blocks[src_cnt] = spare_pages[0];
		blocks[src_cnt + 1] = spare_pages[1];
		tx = async_gen_syndrome(blocks, offset,
					src_cnt, len, lflags,
					depend_tx, NULL, NULL);
		async_tx_quiesce(&tx);

		*pqres = 0;
		if (pdest) {
			p = page_address(pdest) + offset;
			s = page_address(spare_pages[0]) + offset;
			*pqres |= !!memcmp(p, s, len) << SUM_CHECK_P;
		}

		if (qdest) {
			q = page_address(qdest) + offset;
			s = page_address(spare_pages[1]) + offset;
			*pqres |= !!memcmp(q, s, len) << SUM_CHECK_Q;
		}
		spin_unlock(&spare_lock);

		async_tx_sync_epilog(cb_fn, cb_param);
	}

	return tx;
}
EXPORT_SYMBOL_GPL(async_syndrome_zero_sum);

static void safe_put_page(struct page *p)
{
	if (p)
		put_page(p);
}

static int __init async_pq_init(void)
{
	spin_lock_init(&spare_lock);

	spare_pages[0] = alloc_page(GFP_KERNEL);
	if (!spare_pages[0])
		goto abort;
	spare_pages[1] = alloc_page(GFP_KERNEL);
	if (!spare_pages[1])
		goto abort;
	scribble = alloc_page(GFP_KERNEL);
	if (!scribble)
		goto abort;
	return 0;
abort:
	safe_put_page(scribble);
	safe_put_page(spare_pages[1]);
	safe_put_page(spare_pages[0]);
	printk(KERN_ERR "%s: cannot allocate spare!\n", __func__);
	return -ENOMEM;
}

static void __exit async_pq_exit(void)
{
	safe_put_page(scribble);
	safe_put_page(spare_pages[1]);
	safe_put_page(spare_pages[0]);
}

module_init(async_pq_init);
module_exit(async_pq_exit);

MODULE_AUTHOR("Yuri Tikhonov <yur@emcraft.com>, Dan Williams <dan.j.williams@intel.com>");
MODULE_DESCRIPTION("asynchronous pq/pq-zero-sum api");
MODULE_LICENSE("GPL");
