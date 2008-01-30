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
 * This header contains various key-related definitions and helper function.
 * UBIFS allows several key schemes, so we access key fields only via these
 * helpers. At the moment only one key scheme is supported.
 *
 * Simple key scheme
 * ~~~~~~~~~~~~~~~~~
 *
 * Keys are 64-bits long. First 32-bits are inode number (parent inode number
 * in case of direntry key). Next 3 bits are node type. The last 29 bits are
 * 4KiB offset in case of inode node, and direntry hash in case of a direntry
 * node. We use "r5" hash borrowed from reiserfs.
 */

#ifndef __UBIFS_KEY_H__
#define __UBIFS_KEY_H__

/**
 * _key_r5_hash - R5 hash function (borrowed from reiserfs).
 * @str: direntry name
 * @len: name length
 */
static inline uint32_t key_r5_hash(const char *s, int len)
{
	uint32_t a = 0;
	const signed char *str = (const signed char *)s;

	while (*str) {
		a += *str << 4;
		a += *str >> 4;
		a *= 11;
		str++;
	}

	/*
	 * We use hash values as offset in directories, so offsets 0 and 1 are
	 * reserved for "." and "..". Offset 2 is also reserved for readdir()
	 * purposes.
	 */
	if (unlikely(a >= 0 && a <= 2))
		a += 3;
	return a;
}

/**
 * _key_test_hash - testing hash function.
 * @str: direntry name
 * @len: name length
 */
static inline uint32_t key_test_hash(const char *str, int len)
{
	uint32_t a = 0;

	len = min_t(uint32_t, len, 4);
	memcpy(&a, str, len);
	if (unlikely(a >= 0 && a <= 2))
		a += 3;
	return a;
}

/**
 * ino_key_init - initialize inode key.
 * @c: UBIFS file-system description object
 * @key: key to initialize
 * @inum: inode number
 */
static inline void ino_key_init(const struct ubifs_info *c,
				union ubifs_key *key, ino_t inum)
{
	key->u32[0] = inum;
	key->u32[1] = UBIFS_INO_KEY << 29;
}

/**
 * ino_key_init_flash - initialize on-flash inode key.
 * @c: UBIFS file-system description object
 * @key: key to initialize
 * @inum: inode number
 */
static inline void ino_key_init_flash(const struct ubifs_info *c, void *k,
				      ino_t inum)
{
	union ubifs_key *key = k;

	key->j32[0] = cpu_to_le32(inum);
	key->j32[1] = cpu_to_le32(UBIFS_INO_KEY << 29);
	memset(k + 8, 0, UBIFS_MAX_KEY_LEN - 8);
}

/**
 * min_inum_key - initialize min inum key.
 * @c: UBIFS file-system description object
 * @key: key to initialize
 * @inum: inode number
 */
static inline void min_inum_key(const struct ubifs_info *c,
				union ubifs_key *key, ino_t inum)
{
	key->u32[0] = inum;
	key->u32[1] = 0;
}

/**
 * max_inum_key - initialize max inum key.
 * @c: UBIFS file-system description object
 * @key: key to initialize
 * @inum: inode number
 */
static inline void max_inum_key(const struct ubifs_info *c,
				union ubifs_key *key, ino_t inum)
{
	key->u32[0] = inum;
	key->u32[1] = 0xffffffff;
}

/**
 * dent_key_init - initialize directory entry key.
 * @c: UBIFS file-system description object
 * @key: key to initialize
 * @inum: parent inode number
 * @dname: direntry name and length
 */
static inline void dent_key_init(const struct ubifs_info *c,
				 union ubifs_key *key, ino_t inum,
				 const struct qstr *dname)
{
	uint32_t hash = c->key_hash(dname->name, dname->len);

	key->u32[0] = inum;
	key->u32[1] = (hash & 0x01FFFFFF) | (UBIFS_DENT_KEY << 29);
}

/**
 * dent_key_init_flash - initialize on-flash directory entry key.
 * @c: UBIFS file-system description object
 * @key: key to initialize
 * @inum: parent inode number
 * @dname: direntry name and length
 */
static inline void dent_key_init_flash(const struct ubifs_info *c, void *k,
				       ino_t inum, const struct qstr *dname)
{
	union ubifs_key *key = k;
	uint32_t hash = c->key_hash(dname->name, dname->len);

	key->j32[0] = cpu_to_le32(inum);
	key->j32[1] = cpu_to_le32((hash & 0x01FFFFFF) | (UBIFS_DENT_KEY << 29));
	memset(k + 8, 0, UBIFS_MAX_KEY_LEN - 8);
}

/**
 * data_key_init - initialize data key.
 * @c: UBIFS file-system description object
 * @key: key to initialize
 * @inum: inode number
 * @block: block number
 */
static inline void data_key_init(const struct ubifs_info *c,
				 union ubifs_key *key, ino_t inum,
				 unsigned int block)
{
	key->u32[0] = inum;
	key->u32[1] = (block & 0x01FFFFFF) | (UBIFS_DATA_KEY << 29);
}

/**
 * data_key_init_flash - initialize on-flash data key.
 * @c: UBIFS file-system description object
 * @key: key to initialize
 * @inum: inode number
 * @block: block number
 */
static inline void data_key_init_flash(const struct ubifs_info *c, void *k,
				       ino_t inum, unsigned int block)
{
	union ubifs_key *key = k;

	key->j32[0] = cpu_to_le32(inum);
	key->j32[1] = cpu_to_le32((block & 0x01FFFFFF) | (UBIFS_DATA_KEY << 29));
	memset(k + 8, 0, UBIFS_MAX_KEY_LEN - 8);
}

/**
 * lowest_key - get the lowest possible key for a directory entry.
 * @c: UBIFS file-system description object
 * @key: where to store the lowest key
 * @pino: parent inode number
 */
static inline void lowest_dent_key(const struct ubifs_info *c,
				   union ubifs_key *key, uint32_t pino)
{
	key->u32[0] = pino;
	key->u32[1] = UBIFS_DENT_KEY << 29;
}

/**
 * make_dent_key - make directory entry key.
 * @c: UBIFS file-system description object
 * @key: key to initialize
 * @inum: parent inode number
 * @hash: hash
 */
static inline void make_dent_key(const struct ubifs_info *c,
				 union ubifs_key *key, ino_t inum,
				 uint32_t hash)
{
	key->u32[0] = inum;
	key->u32[1] = (hash & 0x01FFFFFF) | (UBIFS_DENT_KEY << 29);
}

/**
 * trun_key_init - initialize truncate key.
 * @c: UBIFS file-system description object
 * @key: key to initialize
 * @inum: inode number
 */
static inline void trun_key_init(const struct ubifs_info *c,
				 union ubifs_key *key, ino_t inum)
{
	key->u32[0] = inum;
	key->u32[1] = UBIFS_TRUN_KEY << 29;
}

/**
 * trun_key_init_flash - initialize on-flash truncate key.
 * @c: UBIFS file-system description object
 * @key: key to initialize
 * @inum: inode number
 */
static inline void trun_key_init_flash(const struct ubifs_info *c, void *k,
				       ino_t inum)
{
	union ubifs_key *key = k;

	key->j32[0] = cpu_to_le32(inum);
	key->j32[1] = cpu_to_le32(UBIFS_TRUN_KEY << 29);
	memset(k + 8, 0, UBIFS_MAX_KEY_LEN - 8);
}

/**
 * key_type - get key type.
 * @c: UBIFS file-system description object
 * @key: key to get type of
 */
static inline int key_type(const struct ubifs_info *c,
			   const union ubifs_key *key)
{
	return key->u32[1] >> 29;
}

/**
 * key_ino - fetch inode number from key.
 * @c: UBIFS file-system description object
 * @key: key to fetch inode number from
 */
static inline ino_t key_ino(const struct ubifs_info *c, const void *k)
{
	const union ubifs_key *key = k;

	return key->u32[0];
}

/**
 * key_ino_flash - fetch inode number from an on-flash formatted key.
 * @c: UBIFS file-system description object
 * @k: key to fetch inode number from
 */
static inline ino_t key_ino_flash(const struct ubifs_info *c, const void *k)
{
	const union ubifs_key *key = k;

	return le32_to_cpu(key->j32[0]);
}

/**
 * key_hash - get directory entry hash.
 * @c: UBIFS file-system description object
 * @key: the key to get hash from
 */
static inline int key_hash(const struct ubifs_info *c,
			   const union ubifs_key *key)
{
	return key->u32[1] & 0x01FFFFFF;
}

/**
 * key_hash_flash - get directory entry hash from an on-flash formatted key.
 * @c: UBIFS file-system description object
 * @k: the key to get hash from
 */
static inline int key_hash_flash(const struct ubifs_info *c, const void *k)
{
	const union ubifs_key *key = k;

	return le32_to_cpu(key->j32[1]) & 0x01FFFFFF;
}

/**
 * key_block - get data block number.
 * @c: UBIFS file-system description object
 * @key: the key to get the block number from
 */
static inline unsigned int key_block(const struct ubifs_info *c,
				     const union ubifs_key *key)
{
	return key->u32[1] & 0x01FFFFFF;
}

/**
 * key_read - transform a key to in-memory format.
 * @c: UBIFS file-system description object
 * @key: the key to transform
 */
static inline void key_read(const struct ubifs_info *c, const void *from,
			    union ubifs_key *to)
{
	const union ubifs_key *f = from;

	to->u32[0] = le32_to_cpu(f->j32[0]);
	to->u32[1] = le32_to_cpu(f->j32[1]);
}

/**
 * key_write - transform a key from in-memory format.
 * @c: UBIFS file-system description object
 * @key: the key to transform
 */
static inline void key_write(const struct ubifs_info *c,
			     const union ubifs_key *from, void *to)
{
	union ubifs_key *t = to;

	t->j32[0] = cpu_to_le32(from->u32[0]);
	t->j32[1] = cpu_to_le32(from->u32[1]);
	memset(to + 8, 0, UBIFS_MAX_KEY_LEN - 8);
}

/**
 * key_write_idx - transform a key from in-memory format for the index.
 * @c: UBIFS file-system description object
 * @key: the key to transform
 */
static inline void key_write_idx(const struct ubifs_info *c,
				 const union ubifs_key *from, void *to)
{
	union ubifs_key *t = to;

	t->j32[0] = cpu_to_le32(from->u32[0]);
	t->j32[1] = cpu_to_le32(from->u32[1]);
}

/**
 * key_copy - copy a key
 * @c: UBIFS file-system description object
 * @from: the key to copy from
 * @to: the key to copy to
 */
static inline void key_copy(const struct ubifs_info *c,
			    const union ubifs_key *from, union ubifs_key *to)
{
	to->u64[0] = from->u64[0];
}

/**
 * keys_cmp - compare keys.
 * @c: UBIFS file-system description object
 * @key1: the first key to compare
 * @key2: the second key to compare
 *
 * This function compares 2 keys and returns %-1 if @key1 is less then
 * @key2, 0 if the keys are equivalent and %1 if @key1 is greater then @key2.
 */
static inline int keys_cmp(const struct ubifs_info *c,
			   const union ubifs_key *key1,
			   const union ubifs_key *key2)
{
	int i;

	for (i = 0; i < 2; i++) {
		if (key1->u32[i] < key2->u32[i])
			return -1;
		if (key1->u32[i] > key2->u32[i])
			return 1;
	}

	return 0;
}

/**
 * is_hash_key - is a key vulnerable to hash collisions.
 * @c: UBIFS file-system description object
 * @key: key
 *
 * This function returns %1 if @key is a hashed key or %0 otherwise.
 */
static inline int is_hash_key(const struct ubifs_info *c,
			      const union ubifs_key *key)
{
	int type = key_type(c, key);

	return type == UBIFS_DENT_KEY || type == UBIFS_XATTR_KEY;
}

#endif /* !__UBIFS_KEY_H__ */
