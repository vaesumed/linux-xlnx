/*
 * fs/fs-writeback.c
 *
 * Copyright (C) 2002, Linus Torvalds.
 *
 * Contains all the functions related to writing back and waiting
 * upon dirty inodes against superblocks, and writing back dirty
 * pages against inodes.  ie: data writeback.  Writeout of the
 * inode itself is not handled here.
 *
 * 10Apr2002	Andrew Morton
 *		Split out of fs/inode.c
 *		Additions for address_space-based writeback
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/backing-dev.h>
#include <linux/buffer_head.h>
#include "internal.h"

#define inode_to_bdi(inode)	((inode)->i_mapping->backing_dev_info)

/*
 * We don't actually have pdflush, but this one is exported though /proc...
 */
int nr_pdflush_threads;

/**
 * writeback_acquire - attempt to get exclusive writeback access to a device
 * @bdi: the device's backing_dev_info structure
 *
 * It is a waste of resources to have more than one pdflush thread blocked on
 * a single request queue.  Exclusion at the request_queue level is obtained
 * via a flag in the request_queue's backing_dev_info.state.
 *
 * Non-request_queue-backed address_spaces will share default_backing_dev_info,
 * unless they implement their own.  Which is somewhat inefficient, as this
 * may prevent concurrent writeback against multiple devices.
 */
static int writeback_acquire(struct backing_dev_info *bdi)
{
	return !test_and_set_bit(BDI_pdflush, &bdi->state);
}

/**
 * writeback_in_progress - determine whether there is writeback in progress
 * @bdi: the device's backing_dev_info structure.
 *
 * Determine whether there is writeback in progress against a backing device.
 */
int writeback_in_progress(struct backing_dev_info *bdi)
{
	return test_bit(BDI_pdflush, &bdi->state);
}

/**
 * writeback_release - relinquish exclusive writeback access against a device.
 * @bdi: the device's backing_dev_info structure
 */
static void writeback_release(struct backing_dev_info *bdi)
{
	WARN_ON_ONCE(!writeback_in_progress(bdi));
	bdi->wb_arg.nr_pages = 0;
	bdi->wb_arg.sb = NULL;
	clear_bit(BDI_pdflush, &bdi->state);
}

void bdi_start_writeback(struct backing_dev_info *bdi, struct super_block *sb,
			 long nr_pages, enum writeback_sync_modes sync_mode)
{
	/*
	 * This only happens the first time someone kicks this bdi, so put
	 * it out-of-line.
	 */
	if (unlikely(!bdi->task))
		wake_up_process(default_backing_dev_info.task);

	if (writeback_acquire(bdi)) {
		bdi->wb_arg.nr_pages = nr_pages;
		bdi->wb_arg.sb = sb;
		bdi->wb_arg.sync_mode = sync_mode;

		if (bdi->task)
			wake_up_process(bdi->task);
	}
}

/*
 * The maximum number of pages to writeout in a single bdi flush/kupdate
 * operation.  We do this so we don't hold I_SYNC against an inode for
 * enormous amounts of time, which would block a userspace task which has
 * been forced to throttle against that inode.  Also, the code reevaluates
 * the dirty each time it has written this many pages.
 */
#define MAX_WRITEBACK_PAGES     1024

/*
 * Periodic writeback of "old" data.
 *
 * Define "old": the first time one of an inode's pages is dirtied, we mark the
 * dirtying-time in the inode's address_space.  So this periodic writeback code
 * just walks the superblock inode list, writing back any inodes which are
 * older than a specific point in time.
 *
 * Try to run once per dirty_writeback_interval.  But if a writeback event
 * takes longer than a dirty_writeback_interval interval, then leave a
 * one-second gap.
 *
 * older_than_this takes precedence over nr_to_write.  So we'll only write back
 * all dirty pages if they are all attached to "old" mappings.
 */
static void bdi_kupdated(struct backing_dev_info *bdi)
{
	unsigned long oldest_jif;
	long nr_to_write;
	struct writeback_control wbc = {
		.bdi			= bdi,
		.sync_mode		= WB_SYNC_NONE,
		.older_than_this	= &oldest_jif,
		.nr_to_write		= 0,
		.for_kupdate		= 1,
		.range_cyclic		= 1,
	};

	oldest_jif = jiffies - msecs_to_jiffies(dirty_expire_interval * 10);

	nr_to_write = global_page_state(NR_FILE_DIRTY) +
			global_page_state(NR_UNSTABLE_NFS) +
			(inodes_stat.nr_inodes - inodes_stat.nr_unused);

	while (nr_to_write > 0) {
		wbc.more_io = 0;
		wbc.encountered_congestion = 0;
		wbc.nr_to_write = MAX_WRITEBACK_PAGES;
		generic_sync_bdi_inodes(NULL, &wbc);
		if (wbc.nr_to_write > 0)
			break;	/* All the old data is written */
		nr_to_write -= MAX_WRITEBACK_PAGES;
	}
}

static inline bool over_bground_thresh(void)
{
	unsigned long background_thresh, dirty_thresh;

	get_dirty_limits(&background_thresh, &dirty_thresh, NULL, NULL);

	return (global_page_state(NR_FILE_DIRTY) +
		global_page_state(NR_UNSTABLE_NFS) >= background_thresh);
}

static void bdi_pdflush(struct backing_dev_info *bdi)
{
	struct writeback_control wbc = {
		.bdi			= bdi,
		.sync_mode		= bdi->wb_arg.sync_mode,
		.older_than_this	= NULL,
		.range_cyclic		= 1,
	};
	long nr_pages = bdi->wb_arg.nr_pages;

	for (;;) {
		if (wbc.sync_mode == WB_SYNC_NONE && nr_pages <= 0 &&
		    !over_bground_thresh())
			break;

		wbc.more_io = 0;
		wbc.encountered_congestion = 0;
		wbc.nr_to_write = MAX_WRITEBACK_PAGES;
		wbc.pages_skipped = 0;
		generic_sync_bdi_inodes(bdi->wb_arg.sb, &wbc);
		nr_pages -= MAX_WRITEBACK_PAGES - wbc.nr_to_write;
		/*
		 * If we ran out of stuff to write, bail unless more_io got set
		 */
		if (wbc.nr_to_write > 0 || wbc.pages_skipped > 0) {
			if (wbc.more_io)
				continue;
			break;
		}
	}
}

/*
 * Handle writeback of dirty data for the device backed by this bdi. Also
 * wakes up periodically and does kupdated style flushing.
 */
int bdi_writeback_task(struct backing_dev_info *bdi)
{
	while (!kthread_should_stop()) {
		unsigned long wait_jiffies;

		wait_jiffies = msecs_to_jiffies(dirty_writeback_interval * 10);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(wait_jiffies);
		try_to_freeze();

		/*
		 * We get here in two cases:
		 *
		 *  schedule_timeout() returned because the dirty writeback
		 *  interval has elapsed. If that happens, we will be able
		 *  to acquire the writeback lock and will proceed to do
		 *  kupdated style writeout.
		 *
		 *  Someone called bdi_start_writeback(), which will acquire
		 *  the writeback lock. This means our writeback_acquire()
		 *  below will fail and we call into bdi_pdflush() for
		 *  pdflush style writeout.
		 *
		 */
		if (writeback_acquire(bdi))
			bdi_kupdated(bdi);
		else
			bdi_pdflush(bdi);

		writeback_release(bdi);
	}

	return 0;
}

void bdi_writeback_all(struct super_block *sb, struct writeback_control *wbc)
{
	struct backing_dev_info *bdi;

	spin_lock(&bdi_lock);

	list_for_each_entry(bdi, &bdi_list, bdi_list) {
		if (!bdi_has_dirty_io(bdi))
			continue;
		bdi_start_writeback(bdi, sb, wbc->nr_to_write, wbc->sync_mode);
	}

	spin_unlock(&bdi_lock);
}

static noinline void block_dump___mark_inode_dirty(struct inode *inode)
{
	if (inode->i_ino || strcmp(inode->i_sb->s_id, "bdev")) {
		struct dentry *dentry;
		const char *name = "?";

		dentry = d_find_alias(inode);
		if (dentry) {
			spin_lock(&dentry->d_lock);
			name = (const char *) dentry->d_name.name;
		}
		printk(KERN_DEBUG
		       "%s(%d): dirtied inode %lu (%s) on %s\n",
		       current->comm, task_pid_nr(current), inode->i_ino,
		       name, inode->i_sb->s_id);
		if (dentry) {
			spin_unlock(&dentry->d_lock);
			dput(dentry);
		}
	}
}

/**
 *	__mark_inode_dirty -	internal function
 *	@inode: inode to mark
 *	@flags: what kind of dirty (i.e. I_DIRTY_SYNC)
 *	Mark an inode as dirty. Callers should use mark_inode_dirty or
 *  	mark_inode_dirty_sync.
 *
 * Put the inode on the super block's dirty list.
 *
 * CAREFUL! We mark it dirty unconditionally, but move it onto the
 * dirty list only if it is hashed or if it refers to a blockdev.
 * If it was not hashed, it will never be added to the dirty list
 * even if it is later hashed, as it will have been marked dirty already.
 *
 * In short, make sure you hash any inodes _before_ you start marking
 * them dirty.
 *
 * This function *must* be atomic for the I_DIRTY_PAGES case -
 * set_page_dirty() is called under spinlock in several places.
 *
 * Note that for blockdevs, inode->dirtied_when represents the dirtying time of
 * the block-special inode (/dev/hda1) itself.  And the ->dirtied_when field of
 * the kernel-internal blockdev inode represents the dirtying time of the
 * blockdev's pages.  This is why for I_DIRTY_PAGES we always use
 * page->mapping->host, so the page-dirtying time is recorded in the internal
 * blockdev inode.
 */
void __mark_inode_dirty(struct inode *inode, int flags)
{
	struct super_block *sb = inode->i_sb;

	/*
	 * Don't do this for I_DIRTY_PAGES - that doesn't actually
	 * dirty the inode itself
	 */
	if (flags & (I_DIRTY_SYNC | I_DIRTY_DATASYNC)) {
		if (sb->s_op->dirty_inode)
			sb->s_op->dirty_inode(inode);
	}

	/*
	 * make sure that changes are seen by all cpus before we test i_state
	 * -- mikulas
	 */
	smp_mb();

	/* avoid the locking if we can */
	if ((inode->i_state & flags) == flags)
		return;

	if (unlikely(block_dump))
		block_dump___mark_inode_dirty(inode);

	spin_lock(&inode_lock);
	if ((inode->i_state & flags) != flags) {
		const int was_dirty = inode->i_state & I_DIRTY;

		inode->i_state |= flags;

		/*
		 * If the inode is being synced, just update its dirty state.
		 * The unlocker will place the inode on the appropriate
		 * superblock list, based upon its state.
		 */
		if (inode->i_state & I_SYNC)
			goto out;

		/*
		 * Only add valid (hashed) inodes to the superblock's
		 * dirty list.  Add blockdev inodes as well.
		 */
		if (!S_ISBLK(inode->i_mode)) {
			if (hlist_unhashed(&inode->i_hash))
				goto out;
		}
		if (inode->i_state & (I_FREEING|I_CLEAR))
			goto out;

		/*
		 * If the inode was already on b_dirty/b_io/b_more_io, don't
		 * reposition it (that would break b_dirty time-ordering).
		 */
		if (!was_dirty) {
			inode->dirtied_when = jiffies;
			list_move(&inode->i_list,
					&inode_to_bdi(inode)->b_dirty);
		}
	}
out:
	spin_unlock(&inode_lock);
}

EXPORT_SYMBOL(__mark_inode_dirty);

static int write_inode(struct inode *inode, int sync)
{
	if (inode->i_sb->s_op->write_inode && !is_bad_inode(inode))
		return inode->i_sb->s_op->write_inode(inode, sync);
	return 0;
}

/*
 * Redirty an inode: set its when-it-was dirtied timestamp and move it to the
 * furthest end of its superblock's dirty-inode list.
 *
 * Before stamping the inode's ->dirtied_when, we check to see whether it is
 * already the most-recently-dirtied inode on the b_dirty list.  If that is
 * the case then the inode must have been redirtied while it was being written
 * out and we don't reset its dirtied_when.
 */
static void redirty_tail(struct inode *inode)
{
	struct backing_dev_info *bdi = inode_to_bdi(inode);

	if (!list_empty(&bdi->b_dirty)) {
		struct inode *tail;

		tail = list_entry(bdi->b_dirty.next, struct inode, i_list);
		if (time_before(inode->dirtied_when, tail->dirtied_when))
			inode->dirtied_when = jiffies;
	}
	list_move(&inode->i_list, &bdi->b_dirty);
}

/*
 * requeue inode for re-scanning after bdi->b_io list is exhausted.
 */
static void requeue_io(struct inode *inode)
{
	list_move(&inode->i_list, &inode_to_bdi(inode)->b_more_io);
}

static void inode_sync_complete(struct inode *inode)
{
	/*
	 * Prevent speculative execution through spin_unlock(&inode_lock);
	 */
	smp_mb();
	wake_up_bit(&inode->i_state, __I_SYNC);
}

static bool inode_dirtied_after(struct inode *inode, unsigned long t)
{
	bool ret = time_after(inode->dirtied_when, t);
#ifndef CONFIG_64BIT
	/*
	 * For inodes being constantly redirtied, dirtied_when can get stuck.
	 * It _appears_ to be in the future, but is actually in distant past.
	 * This test is necessary to prevent such wrapped-around relative times
	 * from permanently stopping the whole pdflush writeback.
	 */
	ret = ret && time_before_eq(inode->dirtied_when, jiffies);
#endif
	return ret;
}

/*
 * Move expired dirty inodes from @delaying_queue to @dispatch_queue.
 */
static void move_expired_inodes(struct list_head *delaying_queue,
			       struct list_head *dispatch_queue,
				unsigned long *older_than_this)
{
	while (!list_empty(delaying_queue)) {
		struct inode *inode = list_entry(delaying_queue->prev,
						struct inode, i_list);
		if (older_than_this &&
		    inode_dirtied_after(inode, *older_than_this))
			break;
		list_move(&inode->i_list, dispatch_queue);
	}
}

/*
 * Queue all expired dirty inodes for io, eldest first.
 */
static void queue_io(struct backing_dev_info *bdi,
		     unsigned long *older_than_this)
{
	list_splice_init(&bdi->b_more_io, bdi->b_io.prev);
	move_expired_inodes(&bdi->b_dirty, &bdi->b_io, older_than_this);
}

/*
 * Write a single inode's dirty pages and inode data out to disk.
 * If `wait' is set, wait on the writeout.
 *
 * The whole writeout design is quite complex and fragile.  We want to avoid
 * starvation of particular inodes when others are being redirtied, prevent
 * livelocks, etc.
 *
 * Called under inode_lock.
 */
static int
__sync_single_inode(struct inode *inode, struct writeback_control *wbc)
{
	unsigned dirty;
	struct address_space *mapping = inode->i_mapping;
	int wait = wbc->sync_mode == WB_SYNC_ALL;
	int ret;

	BUG_ON(inode->i_state & I_SYNC);

	/* Set I_SYNC, reset I_DIRTY */
	dirty = inode->i_state & I_DIRTY;
	inode->i_state |= I_SYNC;
	inode->i_state &= ~I_DIRTY;

	spin_unlock(&inode_lock);

	ret = do_writepages(mapping, wbc);

	/* Don't write the inode if only I_DIRTY_PAGES was set */
	if (dirty & (I_DIRTY_SYNC | I_DIRTY_DATASYNC)) {
		int err = write_inode(inode, wait);
		if (ret == 0)
			ret = err;
	}

	if (wait) {
		int err = filemap_fdatawait(mapping);
		if (ret == 0)
			ret = err;
	}

	spin_lock(&inode_lock);
	inode->i_state &= ~I_SYNC;
	if (!(inode->i_state & (I_FREEING | I_CLEAR))) {
		if (!(inode->i_state & I_DIRTY) &&
		    mapping_tagged(mapping, PAGECACHE_TAG_DIRTY)) {
			/*
			 * We didn't write back all the pages.  nfs_writepages()
			 * sometimes bales out without doing anything. Redirty
			 * the inode; Move it from b_io onto b_more_io/b_dirty.
			 */
			/*
			 * akpm: if the caller was the kupdate function we put
			 * this inode at the head of b_dirty so it gets first
			 * consideration.  Otherwise, move it to the tail, for
			 * the reasons described there.  I'm not really sure
			 * how much sense this makes.  Presumably I had a good
			 * reasons for doing it this way, and I'd rather not
			 * muck with it at present.
			 */
			if (wbc->for_kupdate) {
				/*
				 * For the kupdate function we move the inode
				 * to b_more_io so it will get more writeout as
				 * soon as the queue becomes uncongested.
				 */
				inode->i_state |= I_DIRTY_PAGES;
				if (wbc->nr_to_write <= 0) {
					/*
					 * slice used up: queue for next turn
					 */
					requeue_io(inode);
				} else {
					/*
					 * somehow blocked: retry later
					 */
					redirty_tail(inode);
				}
			} else {
				/*
				 * Otherwise fully redirty the inode so that
				 * other inodes on this superblock will get some
				 * writeout.  Otherwise heavy writing to one
				 * file would indefinitely suspend writeout of
				 * all the other files.
				 */
				inode->i_state |= I_DIRTY_PAGES;
				redirty_tail(inode);
			}
		} else if (inode->i_state & I_DIRTY) {
			/*
			 * Someone redirtied the inode while were writing back
			 * the pages.
			 */
			redirty_tail(inode);
		} else if (atomic_read(&inode->i_count)) {
			/*
			 * The inode is clean, inuse
			 */
			list_move(&inode->i_list, &inode_in_use);
		} else {
			/*
			 * The inode is clean, unused
			 */
			list_move(&inode->i_list, &inode_unused);
		}
	}
	inode_sync_complete(inode);
	return ret;
}

/*
 * Write out an inode's dirty pages.  Called under inode_lock.  Either the
 * caller has ref on the inode (either via __iget or via syscall against an fd)
 * or the inode has I_WILL_FREE set (via generic_forget_inode)
 */
static int
__writeback_single_inode(struct inode *inode, struct writeback_control *wbc)
{
	wait_queue_head_t *wqh;

	if (!atomic_read(&inode->i_count))
		WARN_ON(!(inode->i_state & (I_WILL_FREE|I_FREEING)));
	else
		WARN_ON(inode->i_state & I_WILL_FREE);

	if ((wbc->sync_mode != WB_SYNC_ALL) && (inode->i_state & I_SYNC)) {
		/*
		 * We're skipping this inode because it's locked, and we're not
		 * doing writeback-for-data-integrity.  Move it to b_more_io so
		 * that writeback can proceed with the other inodes on b_io.
		 * We'll have another go at writing back this inode when we
		 * completed a full scan of b_io.
		 */
		requeue_io(inode);
		return 0;
	}

	/*
	 * It's a data-integrity sync.  We must wait.
	 */
	if (inode->i_state & I_SYNC) {
		DEFINE_WAIT_BIT(wq, &inode->i_state, __I_SYNC);

		wqh = bit_waitqueue(&inode->i_state, __I_SYNC);
		do {
			spin_unlock(&inode_lock);
			__wait_on_bit(wqh, &wq, inode_wait,
							TASK_UNINTERRUPTIBLE);
			spin_lock(&inode_lock);
		} while (inode->i_state & I_SYNC);
	}
	return __sync_single_inode(inode, wbc);
}

void generic_sync_bdi_inodes(struct super_block *sb,
			     struct writeback_control *wbc)
{
	const int is_blkdev_sb = sb_is_blkdev_sb(sb);
	struct backing_dev_info *bdi = wbc->bdi;
	const unsigned long start = jiffies;	/* livelock avoidance */

	spin_lock(&inode_lock);

	if (!wbc->for_kupdate || list_empty(&bdi->b_io))
		queue_io(bdi, wbc->older_than_this);

	while (!list_empty(&bdi->b_io)) {
		struct inode *inode = list_entry(bdi->b_io.prev,
						struct inode, i_list);
		long pages_skipped;

		/*
		 * super block given and doesn't match, skip this inode
		 */
		if (sb && sb != inode->i_sb) {
			redirty_tail(inode);
			continue;
		}

		if (!bdi_cap_writeback_dirty(bdi)) {
			redirty_tail(inode);
			if (is_blkdev_sb) {
				/*
				 * Dirty memory-backed blockdev: the ramdisk
				 * driver does this.  Skip just this inode
				 */
				continue;
			}
			/*
			 * Dirty memory-backed inode against a filesystem other
			 * than the kernel-internal bdev filesystem.  Skip the
			 * entire superblock.
			 */
			break;
		}

		if (inode->i_state & (I_NEW | I_WILL_FREE)) {
			requeue_io(inode);
			continue;
		}

		if (wbc->nonblocking && bdi_write_congested(bdi)) {
			wbc->encountered_congestion = 1;
			if (!is_blkdev_sb)
				break;		/* Skip a congested fs */
			requeue_io(inode);
			continue;		/* Skip a congested blockdev */
		}

		/*
		 * Was this inode dirtied after sync_sb_inodes was called?
		 * This keeps sync from extra jobs and livelock.
		 */
		if (inode_dirtied_after(inode, start))
			break;

		BUG_ON(inode->i_state & (I_FREEING | I_CLEAR));
		__iget(inode);
		pages_skipped = wbc->pages_skipped;
		__writeback_single_inode(inode, wbc);
		if (wbc->pages_skipped != pages_skipped) {
			/*
			 * writeback is not making progress due to locked
			 * buffers.  Skip this inode for now.
			 */
			redirty_tail(inode);
		}
		spin_unlock(&inode_lock);
		iput(inode);
		cond_resched();
		spin_lock(&inode_lock);
		if (wbc->nr_to_write <= 0) {
			wbc->more_io = 1;
			break;
		}
		if (!list_empty(&bdi->b_more_io))
			wbc->more_io = 1;
	}

	spin_unlock(&inode_lock);
	/* Leave any unwritten inodes on b_io */
}

/*
 * Write out a superblock's list of dirty inodes.  A wait will be performed
 * upon no inodes, all inodes or the final one, depending upon sync_mode.
 *
 * If older_than_this is non-NULL, then only write out inodes which
 * had their first dirtying at a time earlier than *older_than_this.
 *
 * If we're a pdlfush thread, then implement pdflush collision avoidance
 * against the entire list.
 *
 * If `bdi' is non-zero then we're being asked to writeback a specific queue.
 * This function assumes that the blockdev superblock's inodes are backed by
 * a variety of queues, so all inodes are searched.  For other superblocks,
 * assume that all inodes are backed by the same queue.
 *
 * The inodes to be written are parked on bdi->b_io.  They are moved back onto
 * bdi->b_dirty as they are selected for writing.  This way, none can be missed
 * on the writer throttling path, and we get decent balancing between many
 * throttled threads: we don't want them all piling up on inode_sync_wait.
 */
void generic_sync_sb_inodes(struct super_block *sb,
				struct writeback_control *wbc)
{
	if (wbc->bdi)
		generic_sync_bdi_inodes(sb, wbc);
	else
		bdi_writeback_all(sb, wbc);

	if (wbc->sync_mode == WB_SYNC_ALL) {
		struct inode *inode, *old_inode = NULL;

		spin_lock(&inode_lock);

		/*
		 * Data integrity sync. Must wait for all pages under writeback,
		 * because there may have been pages dirtied before our sync
		 * call, but which had writeout started before we write it out.
		 * In which case, the inode may not be on the dirty list, but
		 * we still have to wait for that writeout.
		 */
		list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
			struct address_space *mapping;

			if (inode->i_state &
					(I_FREEING|I_CLEAR|I_WILL_FREE|I_NEW))
				continue;
			mapping = inode->i_mapping;
			if (mapping->nrpages == 0)
				continue;
			__iget(inode);
			spin_unlock(&inode_lock);
			/*
			 * We hold a reference to 'inode' so it couldn't have
			 * been removed from s_inodes list while we dropped the
			 * inode_lock.  We cannot iput the inode now as we can
			 * be holding the last reference and we cannot iput it
			 * under inode_lock. So we keep the reference and iput
			 * it later.
			 */
			iput(old_inode);
			old_inode = inode;

			filemap_fdatawait(mapping);

			cond_resched();

			spin_lock(&inode_lock);
		}
		spin_unlock(&inode_lock);
		iput(old_inode);
	}

}
EXPORT_SYMBOL_GPL(generic_sync_sb_inodes);

static void sync_sb_inodes(struct super_block *sb,
				struct writeback_control *wbc)
{
	generic_sync_sb_inodes(sb, wbc);
}

/*
 * writeback and wait upon the filesystem's dirty inodes.  The caller will
 * do this in two passes - one to write, and one to wait.
 *
 * A finite limit is set on the number of pages which will be written.
 * To prevent infinite livelock of sys_sync().
 *
 * We add in the number of potentially dirty inodes, because each inode write
 * can dirty pagecache in the underlying blockdev.
 */
void sync_inodes_sb(struct super_block *sb, int wait)
{
	struct writeback_control wbc = {
		.sync_mode	= wait ? WB_SYNC_ALL : WB_SYNC_NONE,
		.range_start	= 0,
		.range_end	= LLONG_MAX,
	};

	if (!wait) {
		unsigned long nr_dirty = global_page_state(NR_FILE_DIRTY);
		unsigned long nr_unstable = global_page_state(NR_UNSTABLE_NFS);

		wbc.nr_to_write = nr_dirty + nr_unstable +
			(inodes_stat.nr_inodes - inodes_stat.nr_unused);
	} else
		wbc.nr_to_write = LONG_MAX; /* doesn't actually matter */

	sync_sb_inodes(sb, &wbc);
}

/**
 * write_inode_now	-	write an inode to disk
 * @inode: inode to write to disk
 * @sync: whether the write should be synchronous or not
 *
 * This function commits an inode to disk immediately if it is dirty. This is
 * primarily needed by knfsd.
 *
 * The caller must either have a ref on the inode or must have set I_WILL_FREE.
 */
int write_inode_now(struct inode *inode, int sync)
{
	int ret;
	struct writeback_control wbc = {
		.nr_to_write = LONG_MAX,
		.sync_mode = sync ? WB_SYNC_ALL : WB_SYNC_NONE,
		.range_start = 0,
		.range_end = LLONG_MAX,
	};

	if (!mapping_cap_writeback_dirty(inode->i_mapping))
		wbc.nr_to_write = 0;

	might_sleep();
	spin_lock(&inode_lock);
	ret = __writeback_single_inode(inode, &wbc);
	spin_unlock(&inode_lock);
	if (sync)
		inode_sync_wait(inode);
	return ret;
}
EXPORT_SYMBOL(write_inode_now);

/**
 * sync_inode - write an inode and its pages to disk.
 * @inode: the inode to sync
 * @wbc: controls the writeback mode
 *
 * sync_inode() will write an inode and its pages to disk.  It will also
 * correctly update the inode on its superblock's dirty inode lists and will
 * update inode->i_state.
 *
 * The caller must have a ref on the inode.
 */
int sync_inode(struct inode *inode, struct writeback_control *wbc)
{
	int ret;

	spin_lock(&inode_lock);
	ret = __writeback_single_inode(inode, wbc);
	spin_unlock(&inode_lock);
	return ret;
}
EXPORT_SYMBOL(sync_inode);

/**
 * generic_osync_inode - flush all dirty data for a given inode to disk
 * @inode: inode to write
 * @mapping: the address_space that should be flushed
 * @what:  what to write and wait upon
 *
 * This can be called by file_write functions for files which have the
 * O_SYNC flag set, to flush dirty writes to disk.
 *
 * @what is a bitmask, specifying which part of the inode's data should be
 * written and waited upon.
 *
 *    OSYNC_DATA:     i_mapping's dirty data
 *    OSYNC_METADATA: the buffers at i_mapping->private_list
 *    OSYNC_INODE:    the inode itself
 */

int generic_osync_inode(struct inode *inode, struct address_space *mapping, int what)
{
	int err = 0;
	int need_write_inode_now = 0;
	int err2;

	if (what & OSYNC_DATA)
		err = filemap_fdatawrite(mapping);
	if (what & (OSYNC_METADATA|OSYNC_DATA)) {
		err2 = sync_mapping_buffers(mapping);
		if (!err)
			err = err2;
	}
	if (what & OSYNC_DATA) {
		err2 = filemap_fdatawait(mapping);
		if (!err)
			err = err2;
	}

	spin_lock(&inode_lock);
	if ((inode->i_state & I_DIRTY) &&
	    ((what & OSYNC_INODE) || (inode->i_state & I_DIRTY_DATASYNC)))
		need_write_inode_now = 1;
	spin_unlock(&inode_lock);

	if (need_write_inode_now) {
		err2 = write_inode_now(inode, 1);
		if (!err)
			err = err2;
	}
	else
		inode_sync_wait(inode);

	return err;
}
EXPORT_SYMBOL(generic_osync_inode);
