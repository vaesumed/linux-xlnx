/*++========================================================================
 * Program Name:     Novell NCP Redirector for Linux
 * File Name:        vfs.h
 * Version:          v1.00
 * Author:           James Turner
 *
 * Abstract:         Include module for novfs.
 * Notes:
 * Revision History:
 *
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *======================================================================--*/
#ifndef __STDC_VERSION__
#define __STDC_VERSION__ 0L
#endif

#ifdef	CONFIG_KDB
#include <linux/kdb.h>
#include <linux/kdbprivate.h>

#endif /* CONFIG_KDB */

#include <linux/version.h>
#include <linux/namei.h>

#include "nwcapi.h"

#ifndef  XTIER_HANDLE
typedef void *HANDLE;
typedef HANDLE *PHANDLE;
#define XTIER_HANDLE
#endif

//
// SCHANDLE
//

#ifndef  XTIER_SCHANDLE
typedef struct _SCHANDLE {
	HANDLE hTypeId;
	HANDLE hId;

} SCHANDLE, *PSCHANDLE;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#define SC_PRESENT(X)		((X.hTypeId != NULL) || (X.hId != NULL)) ? TRUE : FALSE
#define SC_EQUAL(X, Y)		((X.hTypeId == Y.hTypeId) && (X.hId == Y.hId)) ? TRUE : FALSE
#define SC_INITIALIZE(X)	{X.hTypeId = X.hId = NULL;}

#define UID_TO_SCHANDLE(hSC, uid)	\
		{ \
			hSC.hTypeId = NULL; \
			hSC.hId = (HANDLE)(u_long)(uid); \
		}

#define XTIER_SCHANDLE
#endif

extern int Novfs_Version_Major;
extern int Novfs_Version_Minor;
extern int Novfs_Version_Sub;
extern int Novfs_Version_Release;

extern void *Novfs_Malloc(size_t, int);
extern void Novfs_Free(const void *);

/*===[ Manifest constants ]===============================================*/
#define NOVFS_MAGIC	0x4e574653
#define MODULE_NAME "novfs"

#define UNUSED_VARIABLE(a) (a) = (a)

#define TREE_DIRECTORY_NAME   ".Trees"
#define SERVER_DIRECTORY_NAME ".Servers"

#define PATH_LENGTH_BUFFER	PATH_MAX
#define NW_MAX_PATH_LENGTH 255

#define XA_BUFFER	(8 * 1024)

#define IOC_LOGIN    0x4a540000
#define IOC_LOGOUT	0x4a540001
#define IOC_XPLAT    0x4a540002
#define IOC_SESSION  0x4a540003
#define IOC_DEBUGPRINT  0x4a540004

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
#define D_CHILD d_u.d_child
#define AS_TREE_LOCK(l)   read_lock_irq(l)
#define AS_TREE_UNLOCK(l) read_unlock_irq(l)
#else
#define D_CHILD d_child
#define AS_TREE_LOCK(l)   spin_lock_irq(l)
#define AS_TREE_UNLOCK(l) spin_unlock_irq(l)
#endif

/*
 * NetWare file attributes
 */

#define NW_ATTRIBUTE_NORMAL		0x00
#define NW_ATTRIBUTE_READ_ONLY		0x01
#define NW_ATTRIBUTE_HIDDEN		0x02
#define NW_ATTRIBUTE_SYSTEM		0x04
#define NW_ATTRIBUTE_EXECUTE_ONLY	0x08
#define NW_ATTRIBUTE_DIRECTORY		0x10
#define NW_ATTRIBUTE_ARCHIVE		0x20
#define NW_ATTRIBUTE_EXECUTE		0x40
#define NW_ATTRIBUTE_SHAREABLE		0x80

/*
 * Define READ/WRITE flag for DATA_LIST
 */
#define DLREAD    0
#define DLWRITE   1

/*
 * Define list type
 */
#define USER_LIST	1
#define SERVER_LIST	2
#define VOLUME_LIST	3

/*
 * Define flags used in for inodes
 */
#define USER_INODE	1
#define UPDATE_INODE	2

/*
 * Define flags for directory cache flags
 */
#define ENTRY_VALID	0x00000001

#ifdef INTENT_MAGIC
#define NDOPENFLAGS intent.it_flags
#else
#define NDOPENFLAGS intent.open.flags
#endif

/*
 * daemon_command_t flags values
 */
#define INTERRUPTIBLE	1

#ifndef NOVFS_VFS_MAJOR
#define NOVFS_VFS_MAJOR 0
#endif

#ifndef NOVFS_VFS_MINOR
#define NOVFS_VFS_MINOR 0
#endif

#ifndef NOVFS_VFS_SUB
#define NOVFS_VFS_SUB 0
#endif

#ifndef NOVFS_VFS_RELEASE
#define NOVFS_VFS_RELEASE 0
#endif

#define VALUE_TO_STR( value ) #value
#define DEFINE_TO_STR(value) VALUE_TO_STR(value)

#define NOVFS_VERSION_STRING \
         DEFINE_TO_STR(NOVFS_VFS_MAJOR)"." \
         DEFINE_TO_STR(NOVFS_VFS_MINOR)"." \
         DEFINE_TO_STR(NOVFS_VFS_SUB)"-" \
         DEFINE_TO_STR(NOVFS_VFS_RELEASE) \
         "\0"

/*===[ Type definitions ]=================================================*/
typedef struct _ENTRY_INFO {
	int type;
	umode_t mode;
	uid_t uid;
	gid_t gid;
	loff_t size;
	struct timespec atime;
	struct timespec mtime;
	struct timespec ctime;
	int namelength;
	unsigned char name[1];
} ENTRY_INFO, *PENTRY_INFO;

struct novfs_string {
	int length;
	unsigned char *data;
};

typedef struct _LOGIN_ {
	struct novfs_string Server;
	struct novfs_string UserName;
	struct novfs_string Password;
} LOGIN, *PLOGIN;

typedef struct _LOGOUT_ {
	struct novfs_string Server;
} LOGOUT, *PLOGOUT;

typedef SCHANDLE scope_t;
typedef SCHANDLE session_t;

typedef struct _DIR_CACHE_ {
	struct list_head list;
	int flags;
	u64 jiffies;
	ino_t ino;
	loff_t size;
	umode_t mode;
	struct timespec atime;
	struct timespec mtime;
	struct timespec ctime;
	unsigned long hash;
	int nameLen;
	char name[1];
} DIR_CACHE, *PDIR_CACHE;

typedef struct _INODE_DATA_ {
	void *Scope;
	unsigned long Flags;
	struct list_head IList;
	struct inode *Inode;
	unsigned long cntDC;
	struct list_head DirCache;
	struct semaphore DirCacheLock;
	HANDLE FileHandle;
	int CacheFlag;
	char Name[1];		/* Needs to be last entry */
} INODE_DATA, *PINODE_DATA;

typedef struct _DATA_LIST_ {
	void *page;
	void *offset;
	int len;
	int rwflag;
} DATA_LIST, *PDATA_LIST;

#if 0				//sgled
typedef struct _XPLAT_ {
	int xfunction;
	unsigned long reqLen;
	void *reqData;
	unsigned long repLen;
	void *repData;

} XPLAT, *PXPLAT;
#endif //sgled

/*===[ Function prototypes ]==============================================*/

extern int DbgPrint(char *Fmt, ...);
extern char *ctime_r(time_t * clock, char *buf);

/*++======================================================================*/
static inline unsigned long InterlockedIncrement(unsigned long *p)
/*
 *
 *  Arguments:   unsigned long *p - pointer to value.
 *
 *  Returns:     unsigned long - value prior to increment.
 *
 *  Abstract:    The value of *p is incremented and the value of *p before
 *               it was incremented is returned.  This is an atomic operation.
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	unsigned long x = 1;

	mb();

#if   defined(__i386) || defined(__i386__) || defined(__x86_64) || defined (__x86_64__)
	__asm__ __volatile__(" lock xadd %0,(%2)":"+r"(x), "=m"(p)
			     :"r"(p), "m"(p)
			     :"memory");

#else
#error Fix InterlockedIncrement!
#endif

	return (x);
}

/*++======================================================================*/
static inline u32 HandletoUint32(HANDLE h)
/*
 *
 *  Arguments:   HANDLE h - handle value
 *
 *  Returns:     u32 - u32 value
 *
 *  Abstract:    Converts a HANDLE to a u32 type.
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	return (u32) ((u_long) h);
}

/*++======================================================================*/
static inline HANDLE Uint32toHandle(u32 ui32)
/*
 *
 *  Arguments:   u32 ui32
 *
 *  Returns:     HANDLE - Handle type.
 *
 *  Abstract:    Converts a u32 to a HANDLE type.
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	return ((HANDLE) (u_long) ui32);
}
