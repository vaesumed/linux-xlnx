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

#ifndef __UBIFS_DEBUG_H__
#define __UBIFS_DEBUG_H__

#ifdef CONFIG_UBIFS_FS_DEBUG
#define UBIFS_DBG(op) op
#define ubifs_assert(expr)  do {if (unlikely(!(expr))) { \
	printk(KERN_CRIT "UBIFS assert failed in %s at %u (pid %d)\n" , \
	       __FUNCTION__, __LINE__, current->pid);                   \
	dump_stack();                                                   \
}} while (0)

/* Generic debugging message */
#define dbg_msg(fmt, ...) do {                                               \
	printk(KERN_DEBUG "UBIFS DBG (pid %d): %s: " fmt "\n", current->pid, \
	       __FUNCTION__, ##__VA_ARGS__);                                 \
} while(0)

/* Debugging message which prints UBIFS key */
#define dbg_key(c, key, fmt, ...) do {                                        \
	spin_lock(&dbg_lock);                                                 \
	printk(KERN_DEBUG "UBIFS DBG (pid %d): %s: " fmt " %s", current->pid, \
	       __FUNCTION__,  ##__VA_ARGS__, dbg_get_key_dump(c, key));       \
	spin_unlock(&dbg_lock);                                               \
} while(0)

#define dbg_err(fmt, ...) ubifs_err(fmt, ##__VA_ARGS__)
#define dbg_dump_stack() dump_stack()

#define ubifs_assert_cmt_locked(c) do {                           \
	if (unlikely(down_write_trylock(&(c)->commit_sem))) {     \
		up_write(&(c)->commit_sem);                       \
		printk(KERN_CRIT "commit lock is not locked!\n"); \
		ubifs_assert(0);                                  \
	}                                                         \
} while(0)

#ifndef UBIFS_DBG_PRESERVE_KMALLOC
#define kmalloc dbg_km_chkr
#define kzalloc dbg_kz_chkr
#define kfree dbg_kf_chkr
#define vmalloc dbg_vm_chkr
#define vfree dbg_vf_chkr
#endif

#else

#define UBIFS_DBG(op)
#define ubifs_assert(expr)        ({})
#define dbg_msg(fmt, ...)         ({})
#define dbg_key(c, key, fmt, ...) ({})
#define dbg_err(fmt, ...)         ({})
#define dbg_dump_stack()
#define ubifs_assert_cmt_locked(c)

#endif /* !CONFIG_UBIFS_FS_DEBUG */

#ifdef CONFIG_UBIFS_FS_DEBUG

extern spinlock_t dbg_lock;
const char *dbg_ntype(int type);
const char *dbg_cstate(int cmt_state);
const char *dbg_get_key_dump(const struct ubifs_info *c,
			     const union ubifs_key *key);
void dbg_dump_node(const struct ubifs_info *c, const void *node);
void dbg_dump_budget_req(const struct ubifs_budget_req *req);
void dbg_dump_lstats(const struct ubifs_lp_stats *lst);
void dbg_dump_budg(struct ubifs_info *c);
void dbg_dump_lprop(const struct ubifs_info *c, const struct ubifs_lprops *lp);
void dbg_dump_lprops(struct ubifs_info *c);
void dbg_dump_leb(const struct ubifs_info *c, int lnum);
void dbg_dump_znode(const struct ubifs_info *c,
		    const struct ubifs_znode *znode);
void dbg_dump_heap(struct ubifs_info *c, struct ubifs_lpt_heap *heap, int cat);
void *dbg_km_chkr(size_t size, gfp_t flags);
void *dbg_kz_chkr(size_t size, gfp_t flags);
void dbg_kf_chkr(const void *addr);
void *dbg_vm_chkr(size_t size);
void dbg_vf_chkr(void *addr);
void dbg_leak_rpt(void);

typedef int (*dbg_leaf_callback)(struct ubifs_info *c,
				 struct ubifs_zbranch *zbr, void *priv);
typedef int (*dbg_znode_callback)(struct ubifs_info *c,
				  struct ubifs_znode *znode, void *priv);

int dbg_walk_index(struct ubifs_info *c, dbg_leaf_callback leaf_cb,
		   dbg_znode_callback znode_cb, void *priv);
int dbg_read_leaf_nolock(struct ubifs_info *c, const union ubifs_key *key,
			 struct ubifs_zbranch *zbr, void *node);
#else

#define dbg_ntype(type)             ""
#define dbg_cstate(cmt_state)       ""
#define dbg_get_key_dump(c, key)    ({})
#define dbg_dump_node(c, node)      ({})
#define dbg_dump_budget_req(req)    ({})
#define dbg_dump_lstats(lst)        ({})
#define dbg_dump_budg(c)            ({})
#define dbg_dump_lprop(c, lp)       ({})
#define dbg_dump_lprops(c)          ({})
#define dbg_dump_leb(c, lnum)       ({})
#define dbg_dump_znode(c, znode)    ({})
#define dbg_dump_heap(c, heap, cat) ({})
#define dbg_leak_rpt()              ({})
#define dbg_walk_index(c, leaf_cb, znode_cb, priv) 0
#define dbg_read_leaf_nolock(c, key, zbr, node)    0

#endif /* !CONFIG_UBIFS_FS_DEBUG */

#ifdef CONFIG_UBIFS_FS_DEBUG_CHK_MEMPRESS
void dbg_eat_memory(void);
void __init dbg_mempressure_init(void);
void __exit dbg_mempressure_exit(void);
#else
#define dbg_eat_memory()       ({})
#define dbg_mempressure_init() ({})
#define dbg_mempressure_exit() ({})
#endif

#ifdef CONFIG_UBIFS_FS_DEBUG_CHK_LPROPS
int dbg_check_lprops(struct ubifs_info *c);
#else
#define dbg_check_lprops(c) 0
#endif

#ifdef CONFIG_UBIFS_FS_DEBUG_CHK_OLD_IDX
int dbg_old_index_check_init(struct ubifs_info *c, struct ubifs_zbranch *zroot);
int dbg_check_old_index(struct ubifs_info *c, struct ubifs_zbranch *zroot);
#else
#define dbg_old_index_check_init(c, zroot) 0
#define dbg_check_old_index(c, zroot) 0
#endif

#ifdef CONFIG_UBIFS_FS_DEBUG_CHK_OTHER
int dbg_check_cats(struct ubifs_info *c);
#else
#define dbg_check_cats(c) 0
#endif

/* General messages */
#ifdef CONFIG_UBIFS_FS_DEBUG_MSG_GEN
#define dbg_gen(fmt, ...)             dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_gen_key(c, key, fmt, ...) dbg_key(c, key, fmt, ##__VA_ARGS__)
#else
#define dbg_gen(fmt, ...)             ({})
#define dbg_gen_key(c, key, fmt, ...) ({})
#endif

/* Additional journal messages */
#ifdef CONFIG_UBIFS_FS_DEBUG_MSG_JRN
#define dbg_jrn(fmt, ...)             dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_jrn_key(c, key, fmt, ...) dbg_key(c, key, fmt, ##__VA_ARGS__)
#else
#define dbg_jrn(fmt, ...)             ({})
#define dbg_jrn_key(c, key, fmt, ...) ({})
#endif

/* Additional TNC messages */
#ifdef CONFIG_UBIFS_FS_DEBUG_MSG_TNC
#define dbg_tnc(fmt, ...)             dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_tnc_key(c, key, fmt, ...) dbg_key(c, key, fmt, ##__VA_ARGS__)
#else
#define dbg_tnc(fmt, ...)             ({})
#define dbg_tnc_key(c, key, fmt, ...) ({})
#endif

/* Additional lprops messages */
#ifdef CONFIG_UBIFS_FS_DEBUG_MSG_LP
#define dbg_lp(fmt, ...) dbg_msg(fmt, ##__VA_ARGS__)
#else
#define dbg_lp(fmt, ...) ({})
#endif

/* Additional LEB find messages */
#ifdef CONFIG_UBIFS_FS_DEBUG_MSG_FIND
#define dbg_find(fmt, ...) dbg_msg(fmt, ##__VA_ARGS__)
#else
#define dbg_find(fmt, ...) ({})
#endif

/* Additional mount messages */
#ifdef CONFIG_UBIFS_FS_DEBUG_MSG_MNT
#define dbg_mnt(fmt, ...)             dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_mnt_key(c, key, fmt, ...) dbg_key(c, key, fmt, ##__VA_ARGS__)
#else
#define dbg_mnt(fmt, ...)             ({})
#define dbg_mnt_key(c, key, fmt, ...) ({})
#endif

/* Additional I/O messages */
#ifdef CONFIG_UBIFS_FS_DEBUG_MSG_IO
#define dbg_io(fmt, ...) dbg_msg(fmt, ##__VA_ARGS__)
#else
#define dbg_io(fmt, ...) ({})
#endif

/* Additional commit messages */
#ifdef CONFIG_UBIFS_FS_DEBUG_MSG_CMT
#define dbg_cmt(fmt, ...) dbg_msg(fmt, ##__VA_ARGS__)
#else
#define dbg_cmt(fmt, ...) ({})
#endif

/* Additional budgeting messages */
#ifdef CONFIG_UBIFS_FS_DEBUG_MSG_BUDG
#define dbg_budg(fmt, ...) dbg_msg(fmt, ##__VA_ARGS__)
#else
#define dbg_budg(fmt, ...) ({})
#endif

/* Additional log messages */
#ifdef CONFIG_UBIFS_FS_DEBUG_MSG_LOG
#define dbg_log(fmt, ...) dbg_msg(fmt, ##__VA_ARGS__)
#else
#define dbg_log(fmt, ...) ({})
#endif

/* Additional gc messages */
#ifdef CONFIG_UBIFS_FS_DEBUG_MSG_GC
#define dbg_gc(fmt, ...) dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_gc_key(c, key, fmt, ...) dbg_key(c, key, fmt, ##__VA_ARGS__)
#else
#define dbg_gc(fmt, ...) ({})
#define dbg_gc_key(c, key, fmt, ...) ({})
#endif

/* Additional scan messages */
#ifdef CONFIG_UBIFS_FS_DEBUG_MSG_SCAN
#define dbg_scan(fmt, ...) dbg_msg(fmt, ##__VA_ARGS__)
#else
#define dbg_scan(fmt, ...) ({})
#endif

/* Additional recovery messages */
#ifdef CONFIG_UBIFS_FS_DEBUG_MSG_RCVRY
#define dbg_rcvry(fmt, ...) dbg_msg(fmt, ##__VA_ARGS__)
#else
#define dbg_rcvry(fmt, ...) ({})
#endif

#ifdef CONFIG_UBIFS_FS_DEBUG_CHK_TNC
int dbg_check_tnc(struct ubifs_info *c, int extra);
#else
#define dbg_check_tnc(c, x) 0
#endif

#ifdef CONFIG_UBIFS_FS_DEBUG_CHK_IDX_SZ
int dbg_check_idx_size(struct ubifs_info *c, long long idx_size);
#else
#define dbg_check_idx_size(c, idx_size) 0
#endif

#ifdef CONFIG_UBIFS_FS_DEBUG_CHK_LPROPS
int dbg_check_lprops(struct ubifs_info *c);
#else
#define dbg_check_lprops(c) 0
#endif

/* TODO: remove later */
extern int bug_hunting;
#define dbg_bug(fmt, ...) do {                        \
	if (bug_hunting) dbg_msg(fmt, ##__VA_ARGS__); \
} while(0)

#ifdef CONFIG_UBIFS_FS_DEBUG_TEST_RCVRY
void dbg_failure_mode_registration(struct ubifs_info *c);
void dbg_failure_mode_deregistration(struct ubifs_info *c);
#undef dbg_dump_stack
#define dbg_dump_stack()
#define dbg_failure_mode 1
#ifndef UBIFS_DBG_PRESERVE_UBI
#define ubi_leb_read   dbg_leb_read
#define ubi_leb_write  dbg_leb_write
#define ubi_leb_change dbg_leb_change
#define ubi_leb_erase  dbg_leb_erase
#define ubi_leb_unmap  dbg_leb_unmap
#define ubi_is_mapped  dbg_is_mapped
int dbg_leb_read(struct ubi_volume_desc *desc, int lnum, char *buf, int offset,
		 int len, int check);
int dbg_leb_write(struct ubi_volume_desc *desc, int lnum, const void *buf,
		  int offset, int len, int dtype);
int dbg_leb_change(struct ubi_volume_desc *desc, int lnum, const void *buf,
		   int len, int dtype);
int dbg_leb_erase(struct ubi_volume_desc *desc, int lnum);
int dbg_leb_unmap(struct ubi_volume_desc *desc, int lnum);
int dbg_is_mapped(struct ubi_volume_desc *desc, int lnum);
static inline int dbg_read(struct ubi_volume_desc *desc, int lnum, char *buf,
			   int offset, int len)
{
	return dbg_leb_read(desc, lnum, buf, offset, len, 0);
}
static inline int dbg_write(struct ubi_volume_desc *desc, int lnum,
			    const void *buf, int offset, int len)
{
	return dbg_leb_write(desc, lnum, buf, offset, len, UBI_UNKNOWN);
}
static inline int dbg_change(struct ubi_volume_desc *desc, int lnum,
				    const void *buf, int len)
{
	return dbg_leb_change(desc, lnum, buf, len, UBI_UNKNOWN);
}
#endif /* !UBIFS_DBG_PRESERVE_UBI */
#else  /* CONFIG_UBIFS_FS_DEBUG_TEST_RCVRY */
#define dbg_failure_mode_registration(c) ({})
#define dbg_failure_mode_deregistration(c) ({})
#define dbg_failure_mode 0
#endif /* !CONFIG_UBIFS_FS_DEBUG_TEST_RCVRY */

/* TODO: for backward compatibility, remove later */
#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22))
#define uninitialized_var(x) x
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
void ubifs_hexdump(const void *ptr, int size);
#define print_hex_dump(a, b, c, f, e, buf, len, g) ubifs_hexdump(buf, len)
struct shrinker {
	int (*shrink)(int nr_to_scan, gfp_t gfp_mask);
	int seeks;
};
#define register_shrinker(x)
#define unregister_shrinker(x)
#define is_owner_or_cap(inode)  \
        ((current->fsuid == (inode)->i_uid) || capable(CAP_FOWNER))
#define UBIFSCOMPATNULL ,NULL
#define set_freezable()
#else
#define UBIFSCOMPATNULL
#endif

#endif /* !__UBIFS_DEBUG_H__ */
