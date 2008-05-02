/*
 * Novell NCP Redirector for Linux
 * Author: James Turner
 *
 * This file contains all the functions necessary for sending commands to our
 * daemon module.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/poll.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/semaphore.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/time.h>

#include "vfs.h"
#include "nwcapi.h"
#include "commands.h"
#include "nwerror.h"

#define QUEUE_SENDING 0
#define QUEUE_WAITING 1
#define QUEUE_TIMEOUT 2
#define QUEUE_ACKED   3
#define QUEUE_DONE    4

#define TIMEOUT_VALUE 10

#define DH_TYPE_UNDEFINED    0
#define DH_TYPE_STREAM       1
#define DH_TYPE_CONNECTION   2

/*===[ Type definitions ]=================================================*/
typedef struct _DAEMON_QUEUE {
	struct list_head list;	/* Must be first entry */
	spinlock_t lock;	/* Used to control access to list */
	struct semaphore semaphore;	/* Used to signal when data is available */
} daemon_queue_t;

typedef struct _DAEMON_COMMAND {
	struct list_head list;	/* Must be first entry */
	atomic_t reference;
	unsigned int status;
	unsigned int flags;
	struct semaphore semaphore;
	unsigned long sequence;
	struct timer_list timer;
	void *request;
	unsigned long reqlen;
	void *data;
	int datalen;
	void *reply;
	unsigned long replen;
} daemon_command_t;

typedef struct _DAEMON_HANDLE_ {
	struct list_head list;
	rwlock_t lock;
	session_t session;
} daemon_handle_t;

typedef struct _DAEMON_RESOURCE_ {
	struct list_head list;
	int type;
	HANDLE connection;
	unsigned char handle[6];
	mode_t mode;
	loff_t size;
} daemon_resource_t;

typedef struct _DRIVE_MAP_ {
	struct list_head list;	/* Must be first item */
	session_t session;
	unsigned long hash;
	int namelen;
	char name[1];
} drive_map_t;

/*===[ Function prototypes ]==============================================*/
int Daemon_Close_Control(struct inode *Inode, struct file *File);
int Daemon_Library_close(struct inode *inode, struct file *file);
int Daemon_Library_open(struct inode *inode, struct file *file);
loff_t Daemon_Library_llseek(struct file *file, loff_t offset, int origin);
int Daemon_Open_Control(struct inode *Inode, struct file *File);
uint Daemon_Poll(struct file *file, struct poll_table_struct *poll_table);
int Daemon_Remove_Resource(daemon_handle_t * DHandle, int Type, HANDLE CHandle,
			   unsigned long FHandle);

int Daemon_SetMountPoint(char *Path);
void Daemon_Timer(unsigned long data);
int Daemon_getpwuid(uid_t uid, int unamelen, char *uname);
int Queue_Daemon_Command(void *request, unsigned long reqlen, void *data, int dlen,
			 void **reply, unsigned long * replen, int interruptible);
void Queue_get(daemon_command_t * que);
void Queue_put(daemon_command_t * que);
void Uninit_Daemon_Queue(void);
daemon_command_t *find_queue(unsigned long sequence);
daemon_command_t *get_next_queue(int Set_Queue_Waiting);
int NwdConvertNetwareHandle(PXPLAT pdata, daemon_handle_t * DHandle);
int NwdConvertLocalHandle(PXPLAT pdata, daemon_handle_t * DHandle);
int NwdGetMountPath(PXPLAT pdata);
static int NwdSetMapDrive(PXPLAT pdata, session_t Session);
static int NwdUnMapDrive(PXPLAT pdata, session_t Session);

/*===[ Global variables ]=================================================*/
static daemon_queue_t Daemon_Queue;

static DECLARE_WAIT_QUEUE_HEAD(Read_waitqueue);

static atomic_t Sequence = ATOMIC_INIT(-1);
static atomic_t Daemon_Open_Count = ATOMIC_INIT(0);

static unsigned long Daemon_Command_Timeout = TIMEOUT_VALUE;

static DECLARE_MUTEX(drive_map_lock);
static LIST_HEAD(drive_map_list);

int MaxIoSize = PAGE_SIZE;

static int local_unlink(const char *pathname)
{
	int error;
	struct dentry *dentry;
	struct nameidata nd;
	struct inode *inode = NULL;

	DbgPrint("%s: %s\n", __func__, pathname);
	error = path_lookup(pathname, LOOKUP_PARENT, &nd);
	DbgPrint("%s: path_lookup %d\n", __func__, error);
	if (!error) {
		error = -EISDIR;
		if (nd.last_type == LAST_NORM) {
			dentry = lookup_create(&nd, 1);
			DbgPrint("%s: lookup_hash 0x%p\n", __func__, dentry);

			error = PTR_ERR(dentry);
			if (!IS_ERR(dentry)) {
				if (nd.last.name[nd.last.len]) {
					error =
					    !dentry->
					    d_inode ? -ENOENT : S_ISDIR(dentry->
									d_inode->
									i_mode)
					    ? -EISDIR : -ENOTDIR;
				} else {
					inode = dentry->d_inode;
					if (inode) {
						atomic_inc(&inode->i_count);
					}
					error = vfs_unlink(nd.path.dentry->d_inode, dentry);
					DbgPrint("%s: vfs_unlink %d\n", __func__, error);
				}
				dput(dentry);
			}
			mutex_unlock(&nd.path.dentry->d_inode->i_mutex);

		}
		path_put(&nd.path);
	}

	if (inode) {
		iput(inode);	/* truncate the inode here */
	}

	DbgPrint("%s: error=%d\n", __func__, error);
	return error;
}

static void remove_drive_maps(void)
{
	drive_map_t *dm;
	struct list_head *list;

	down(&drive_map_lock);
	list_for_each(list, &drive_map_list) {
		dm = list_entry(list, drive_map_t, list);

		DbgPrint("%s: dm=0x%p hash: 0x%lx namelen: %d name: %s\n",
			 __func__, dm, dm->hash, dm->namelen, dm->name);
		local_unlink(dm->name);
		list = list->prev;
		list_del(&dm->list);
		kfree(dm);
	}
	up(&drive_map_lock);
}

void Init_Daemon_Queue(void)
{
	INIT_LIST_HEAD(&Daemon_Queue.list);
	spin_lock_init(&Daemon_Queue.lock);
	init_MUTEX_LOCKED(&Daemon_Queue.semaphore);
}

/*++======================================================================*/
void Uninit_Daemon_Queue(void)
/*
 *
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	/* Does nothing for now but we maybe should clear the queue. */
}

/*++======================================================================*/
void Daemon_Timer(unsigned long data)
/*
 *
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	daemon_command_t *que = (daemon_command_t *) data;

	if (QUEUE_ACKED != que->status) {
		que->status = QUEUE_TIMEOUT;
	}
	up(&que->semaphore);
}

int Queue_Daemon_Command(void *request, unsigned long reqlen, void *data, int dlen,
			 void **reply, unsigned long * replen, int interruptible)
/*
 *  Arguments:     void *request - pointer to the request that is to be sent.  Needs to be kernel memory.
 *                 int reqlen - length of the request.
 *========================================================================*/
{
	daemon_command_t *que;
	int retCode = 0;
	uint64_t ts1, ts2;

	ts1 = get_nanosecond_time();

	DbgPrint("Queue_Daemon_Command: %p %ld\n", request, reqlen);

	if (atomic_read(&Daemon_Open_Count)) {

		que = kmalloc(sizeof(*que), GFP_KERNEL);
		DbgPrint("Queue_Daemon_Command: que=0x%p\n", que);
		if (que) {
			atomic_set(&que->reference, 0);
			que->status = QUEUE_SENDING;
			que->flags = 0;

			init_MUTEX_LOCKED(&que->semaphore);

			que->sequence = atomic_inc_return(&Sequence);

			((struct novfs_command_header *)request)->sequence_number = que->sequence;

			/*
			 * Setup and start que timer
			 */
			init_timer(&que->timer);
			que->timer.expires = jiffies + (HZ * Daemon_Command_Timeout);
			que->timer.data = (unsigned long) que;
			que->timer.function = Daemon_Timer;
			add_timer(&que->timer);

			/*
			 * Setup request
			 */
			que->request = request;
			que->reqlen = reqlen;
			que->data = data;
			que->datalen = dlen;
			que->reply = NULL;
			que->replen = 0;

			/*
			 * Added entry to queue.
			 */
			/*
			 * Check to see if interruptible and set flags.
			 */
			if (interruptible) {
				que->flags |= INTERRUPTIBLE;
			}

			Queue_get(que);

			spin_lock(&Daemon_Queue.lock);
			list_add_tail(&que->list, &Daemon_Queue.list);
			spin_unlock(&Daemon_Queue.lock);

			/*
			 * Signal that there is data to be read
			 */
			up(&Daemon_Queue.semaphore);

			/*
			 * Give a change to the other processes.
			 */
			yield();

			/*
			 * Block waiting for reply or timeout
			 */
			down(&que->semaphore);

			if (QUEUE_ACKED == que->status) {
				que->status = QUEUE_WAITING;
				mod_timer(&que->timer,
					  jiffies +
					  (HZ * 2 * Daemon_Command_Timeout));
				if (interruptible) {
					retCode =
					    down_interruptible(&que->semaphore);
				} else {
					down(&que->semaphore);
				}
			}

			/*
			 * Delete timer
			 */
			del_timer(&que->timer);

			/*
			 * Check for timeout
			 */
			if ((QUEUE_TIMEOUT == que->status)
			    && (NULL == que->reply)) {
				DbgPrint("Queue_Daemon_Command: Timeout\n");
				retCode = -ETIME;
			}
			*reply = que->reply;
			*replen = que->replen;

			/*
			 * Remove item from queue
			 */
			Queue_put(que);

		} else {	/* Error case with no memory */

			retCode = -ENOMEM;
			*reply = NULL;
			*replen = 0;
		}
	} else {
		retCode = -EIO;
		*reply = NULL;
		*replen = 0;

	}
	ts2 = get_nanosecond_time();
	ts2 = ts2 - ts1;

	DbgPrint("Queue_Daemon_Command: %llu retCode=%d \n", ts2, retCode);
	return (retCode);
}

/*++======================================================================*/
void Queue_get(daemon_command_t * Que)
/*
 *
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	DbgPrint("Queue_get: que=0x%p %d\n", Que, atomic_read(&Que->reference));
	atomic_inc(&Que->reference);
}

/*++======================================================================*/
void Queue_put(daemon_command_t * Que)
/*
 *
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{

	DbgPrint("Queue_put: que=0x%p %d\n", Que, atomic_read(&Que->reference));
	spin_lock(&Daemon_Queue.lock);

	if (atomic_dec_and_test(&Que->reference)) {
		/*
		 * Remove item from queue
		 */
		list_del(&Que->list);
		spin_unlock(&Daemon_Queue.lock);

		/*
		 * Free item memory
		 */
		kfree(Que);
	} else {
		spin_unlock(&Daemon_Queue.lock);
	}
}

/*++======================================================================*/
daemon_command_t *get_next_queue(int Set_Queue_Waiting)
/*
 *
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	daemon_command_t *que;

	DbgPrint("get_next_queue: que=0x%p\n", Daemon_Queue.list.next);

	spin_lock(&Daemon_Queue.lock);
	que = (daemon_command_t *) Daemon_Queue.list.next;

	while (que && (que != (daemon_command_t *) & Daemon_Queue.list.next)
	       && (que->status != QUEUE_SENDING)) {
		que = (daemon_command_t *) que->list.next;
	}

	if ((NULL == que) || (que == (daemon_command_t *) & Daemon_Queue.list)
	    || (que->status != QUEUE_SENDING)) {
		que = NULL;
	} else if (Set_Queue_Waiting) {
		que->status = QUEUE_WAITING;
	}

	if (que) {
		atomic_inc(&que->reference);
	}

	spin_unlock(&Daemon_Queue.lock);

	DbgPrint("get_next_queue: return=0x%p\n", que);
	return (que);
}

/*++======================================================================*/
daemon_command_t *find_queue(unsigned long sequence)
/*
 *
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	daemon_command_t *que;

	DbgPrint("find_queue: 0x%lx\n", sequence);

	spin_lock(&Daemon_Queue.lock);
	que = (daemon_command_t *) Daemon_Queue.list.next;

	while (que && (que != (daemon_command_t *) & Daemon_Queue.list.next)
	       && (que->sequence != sequence)) {
		que = (daemon_command_t *) que->list.next;
	}

	if ((NULL == que)
	    || (que == (daemon_command_t *) & Daemon_Queue.list.next)
	    || (que->sequence != sequence)) {
		que = NULL;
	}

	if (que) {
		atomic_inc(&que->reference);
	}

	spin_unlock(&Daemon_Queue.lock);

	DbgPrint("find_queue: return 0x%p\n", que);
	return (que);
}

/*++======================================================================*/
int Daemon_Open_Control(struct inode *Inode, struct file *File)
/*
 *
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	DbgPrint("Daemon_Open_Control: pid=%d Count=%d\n", current->pid,
		 atomic_read(&Daemon_Open_Count));
	atomic_inc(&Daemon_Open_Count);

	return (0);
}

/*++======================================================================*/
int Daemon_Close_Control(struct inode *Inode, struct file *File)
/*
 *
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	daemon_command_t *que;

	DbgPrint("Daemon_Close_Control: pid=%d Count=%d\n", current->pid,
		 atomic_read(&Daemon_Open_Count));

	if (atomic_dec_and_test(&Daemon_Open_Count)) {
		/*
		 * Signal any pending que itmes.
		 */

		spin_lock(&Daemon_Queue.lock);
		que = (daemon_command_t *) Daemon_Queue.list.next;

		while (que
		       && (que != (daemon_command_t *) & Daemon_Queue.list.next)
		       && (que->status != QUEUE_DONE)) {
			que->status = QUEUE_TIMEOUT;
			up(&que->semaphore);

			que = (daemon_command_t *) que->list.next;
		}
		spin_unlock(&Daemon_Queue.lock);

		remove_drive_maps();

		Scope_Cleanup();
	}

	return (0);
}

ssize_t Daemon_Send_Command(struct file *file, char __user *buf, size_t len, loff_t * off)
{
	daemon_command_t *que;
	size_t retValue = 0;
	int Finished = 0;
	struct data_list *dlist;
	int i, dcnt, bcnt, ccnt, error;
	char *vadr;
	unsigned long cpylen;

	DbgPrint("Daemon_Send_Command: %u %lld\n", len, *off);
	if (len > MaxIoSize) {
		MaxIoSize = len;
	}

	while (!Finished) {
		que = get_next_queue(1);
		DbgPrint("Daemon_Send_Command: 0x%p\n", que);
		if (que) {
			retValue = que->reqlen;
			if (retValue > len) {
				retValue = len;
			}
			if (retValue > 0x80)
				mydump(0x80, que->request);
			else
				mydump(retValue, que->request);

			cpylen = copy_to_user(buf, que->request, retValue);
			if (que->datalen && (retValue < len)) {
				buf += retValue;
				dlist = que->data;
				dcnt = que->datalen;
				for (i = 0; i < dcnt; i++, dlist++) {
					if (DLREAD == dlist->rwflag) {
						bcnt = dlist->len;
						DbgPrint
						    ("Daemon_Send_Command%d: page=0x%p offset=0x%p len=%d\n",
						     i, dlist->page,
						     dlist->offset, dlist->len);
						if ((bcnt + retValue) <= len) {
							void *km_adr = NULL;

							if (dlist->page) {
								km_adr =
								    kmap(dlist->
									 page);
								vadr = km_adr;
								vadr +=
								    (unsigned long)
								    dlist->
								    offset;
							} else {
								vadr =
								    dlist->
								    offset;
							}

							ccnt =
							    copy_to_user(buf,
									 vadr,
									 bcnt);

							DbgPrint
							    ("Daemon_Send_Command: Copy %d from 0x%p to 0x%p.\n",
							     bcnt, vadr, buf);
							if (bcnt > 0x80)
								mydump(0x80,
								       vadr);
							else
								mydump(bcnt,
								       vadr);

							if (km_adr) {
								kunmap(dlist->
								       page);
							}

							retValue += bcnt;
							buf += bcnt;
						} else {
							break;
						}
					}
				}
			}
			Queue_put(que);
			break;
		}

		if (O_NONBLOCK & file->f_flags) {
			retValue = -EAGAIN;
			break;
		} else {
			if ((error =
			     down_interruptible(&Daemon_Queue.semaphore))) {
				DbgPrint
				    ("Daemon_Send_Command: after down_interruptible error...%d\n",
				     error);
				retValue = -EINTR;
				break;
			}
			DbgPrint
			    ("Daemon_Send_Command: after down_interruptible\n");
		}
	}

	*off = *off;

	DbgPrint("Daemon_Send_Command: return 0x%x\n", retValue);

	return (retValue);
}

ssize_t Daemon_Receive_Reply(struct file *file, const char __user *buf, size_t nbytes, loff_t *ppos)
{
	daemon_command_t *que;
	size_t retValue = 0;
	void *reply;
	unsigned long sequence, cpylen;

	struct data_list *dlist;
	char *vadr;
	int i;

	DbgPrint("Daemon_Receive_Reply: buf=0x%p nbytes=%d ppos=%llx\n", buf,
		 nbytes, *ppos);

	/*
	 * Get sequence number from reply buffer
	 */

	cpylen = copy_from_user(&sequence, buf, sizeof(sequence));

	/*
	 * Find item based on sequence number
	 */
	que = find_queue(sequence);

	DbgPrint("Daemon_Receive_Reply: 0x%lx %p %d\n", sequence, que, nbytes);
	if (que) {
		do {
			retValue = nbytes;
			/*
			 * Ack packet from novfsd.  Remove timer and
			 * return
			 */
			if (nbytes == sizeof(sequence)) {
				que->status = QUEUE_ACKED;
				break;
			}

			if (NULL != (dlist = que->data)) {
				int thiscopy, left = nbytes;
				retValue = 0;

				DbgPrint
				    ("Daemon_Receive_Reply: dlist=0x%p count=%d\n",
				     dlist, que->datalen);
				for (i = 0;
				     (i < que->datalen) && (retValue < nbytes);
				     i++, dlist++) {
					DbgPrint("Daemon_Receive_Reply:\n"
						 "   dlist[%d].page:   0x%p\n"
						 "   dlist[%d].offset: 0x%p\n"
						 "   dlist[%d].len:    0x%x\n"
						 "   dlist[%d].rwflag: 0x%x\n",
						 i, dlist->page, i,
						 dlist->offset, i, dlist->len,
						 i, dlist->rwflag);

					if (DLWRITE == dlist->rwflag) {
						void *km_adr = NULL;

						if (dlist->page) {
							km_adr =
							    kmap(dlist->page);
							vadr = km_adr;
							vadr +=
							    (unsigned long) dlist->
							    offset;
						} else {
							vadr = dlist->offset;
						}

						thiscopy = dlist->len;
						if (thiscopy > left) {
							thiscopy = left;
							dlist->len = left;
						}
						cpylen =
						    copy_from_user(vadr, buf,
								   thiscopy);

						if (thiscopy > 0x80)
							mydump(0x80, vadr);
						else
							mydump(thiscopy, vadr);

						if (km_adr) {
							kunmap(dlist->page);
						}

						left -= thiscopy;
						retValue += thiscopy;
						buf += thiscopy;
					}
				}
				que->replen = retValue;
			} else {
				reply = kmalloc(nbytes, GFP_KERNEL);
				DbgPrint("Daemon_Receive_Reply: reply=0x%p\n", reply);
				if (reply) {
					retValue = nbytes;
					que->reply = reply;
					que->replen = nbytes;

					retValue -= copy_from_user(reply, buf, retValue);
					if (retValue > 0x80)
						mydump(0x80, reply);
					else
						mydump(retValue, reply);

				} else {
					retValue = -ENOMEM;
				}
			}

			/*
			 * Set status that packet is done.
			 */
			que->status = QUEUE_DONE;

		} while (0);
		up(&que->semaphore);
		Queue_put(que);
	}

	DbgPrint("Daemon_Receive_Reply: return 0x%x\n", retValue);

	return (retValue);
}

int do_login(NclString *Server, NclString *Username, NclString *Password, HANDLE *lgnId, struct schandle *Session)
{
	PLOGIN_USER_REQUEST cmd;
	PLOGIN_USER_REPLY reply;
	unsigned long replylen = 0;
	int retCode, cmdlen, datalen;
	unsigned char *data;

	datalen = Server->len + Username->len + Password->len;
	cmdlen = sizeof(*cmd) + datalen;
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	data = (unsigned char *) cmd + sizeof(*cmd);
	cmd->command.command_type = VFS_COMMAND_LOGIN_USER;
	cmd->command.sequence_number = 0;
	memcpy(&cmd->command.session_id, Session, sizeof(*Session));

	cmd->srvNameType = Server->type;
	cmd->serverLength = Server->len;
	cmd->serverOffset = (unsigned long) (data - (unsigned char *) cmd);
	memcpy(data, Server->buffer, Server->len);
	data += Server->len;

	cmd->usrNameType = Username->type;
	cmd->userNameLength = Username->len;
	cmd->userNameOffset = (unsigned long) (data - (unsigned char *) cmd);
	memcpy(data, Username->buffer, Username->len);
	data += Username->len;

	cmd->pwdNameType = Password->type;
	cmd->passwordLength = Password->len;
	cmd->passwordOffset = (unsigned long) (data - (unsigned char *) cmd);
	memcpy(data, Password->buffer, Password->len);
	data += Password->len;

	retCode = Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (reply) {
		if (reply->Reply.error_code) {
			retCode = reply->Reply.error_code;
		} else {
			retCode = 0;
			if (lgnId) {
				*lgnId = reply->loginIdentity;
			}
		}
		kfree(reply);
	}
	memset(cmd, 0, cmdlen);
	kfree(cmd);
	return retCode;

}

int do_logout(struct qstr *Server, struct schandle *Session)
{
	PLOGOUT_REQUEST cmd;
	PLOGOUT_REPLY reply;
	unsigned long replylen = 0;
	int retCode, cmdlen;

	cmdlen = offsetof(LOGOUT_REQUEST, Name) + Server->len;
	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->command.command_type = VFS_COMMAND_LOGOUT_USER;
	cmd->command.sequence_number = 0;
	memcpy(&cmd->command.session_id, Session, sizeof(*Session));
	cmd->length = Server->len;
	memcpy(cmd->Name, Server->name, Server->len);

	retCode = Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		if (reply->Reply.error_code) {
			retCode = -EIO;
		}
		kfree(reply);
	}
	kfree(cmd);
	return (retCode);

}

/*++======================================================================*/
int Daemon_getpwuid(uid_t uid, int unamelen, char *uname)
/*
 *
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	GETPWUID_REQUEST cmd;
	PGETPWUID_REPLY reply;
	unsigned long replylen = 0;
	int retCode;

	cmd.command.command_type = VFS_COMMAND_GETPWUD;
	cmd.command.sequence_number = 0;
	SC_INITIALIZE(cmd.command.session_id);
	cmd.uid = uid;

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (reply) {
		if (reply->Reply.error_code) {
			retCode = -EIO;
		} else {
			retCode = 0;
			memset(uname, 0, unamelen);
			replylen = replylen - offsetof(GETPWUID_REPLY, UserName);
			if (replylen) {
				if (replylen > unamelen) {
					retCode = -EINVAL;
					replylen = unamelen - 1;
				}
				memcpy(uname, reply->UserName, replylen);
			}
		}
		kfree(reply);
	}
	return (retCode);

}

/*++======================================================================*/
int Daemon_getversion(char *Buf, int length)
/*
 *
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	GET_VERSION_REQUEST cmd;
	PGET_VERSION_REPLY reply;
	unsigned long replylen = 0;
	int retVal = 0;

	cmd.command.command_type = VFS_COMMAND_GET_VERSION;
	cmd.command.sequence_number = 0;
	SC_INITIALIZE(cmd.command.session_id);

	Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
			     &replylen, INTERRUPTIBLE);
	if (reply) {
		if (reply->Reply.error_code) {
			retVal = -EIO;
		} else {
			retVal = replylen - offsetof(GET_VERSION_REPLY, Version);
			if (retVal < length) {
				memcpy(Buf, reply->Version, retVal);
				Buf[retVal] = '\0';
			}
		}
		kfree(reply);
	}
	return (retVal);

}

static int daemon_login(struct login __user *Login, struct schandle *Session)
{
	int retCode = -ENOMEM;
	struct login lLogin;
	NclString server;
	NclString username;
	NclString password;

	if (!copy_from_user(&lLogin, Login, sizeof(lLogin))) {
		server.buffer = kmalloc(lLogin.Server.length, GFP_KERNEL);
		if (server.buffer) {
			server.len = lLogin.Server.length;
			server.type = NWC_STRING_TYPE_ASCII;
			if (!copy_from_user((void *)server.buffer, lLogin.Server.data, server.len)) {
				username.buffer = kmalloc(lLogin.UserName.length, GFP_KERNEL);
				if (username.buffer) {
					username.len = lLogin.UserName.length;
					username.type = NWC_STRING_TYPE_ASCII;
					if (!copy_from_user((void *)username.buffer, lLogin.UserName.data, username.len)) {
						password.buffer = kmalloc(lLogin.Password.length, GFP_KERNEL);
						if (password.buffer) {
							password.len = lLogin.Password.length;
							password.type = NWC_STRING_TYPE_ASCII;
							if (!copy_from_user((void *)password.buffer, lLogin.Password.data, password.len)) {
								retCode = do_login (&server, &username, &password, NULL, Session);
								if (!retCode) {
									char *name;
									name = Scope_Get_UserName();
									if (name)
										Novfs_Add_to_Root(name);
								}
							}
							memset(password.buffer, 0, password.len);
							kfree(password.buffer);
						}
					}
					memset(username.buffer, 0, username.len);
					kfree(username.buffer);
				}
			}
			kfree(server.buffer);
		}
	}

	return (retCode);
}

static int daemon_logout(struct logout __user *Logout, struct schandle *Session)
{
	struct logout lLogout;
	struct qstr server;
	int retCode = -ENOMEM;

	if (copy_from_user(&lLogout, Logout, sizeof(lLogout)))
		return -EFAULT;

	server.name = kmalloc(lLogout.Server.length, GFP_KERNEL);
	if (!server.name)
		return -ENOMEM;
	server.len = lLogout.Server.length;
	if (copy_from_user((void *)server.name, lLogout.Server.data, server.len))
		goto exit;

	retCode = do_logout(&server, Session);
exit:
	kfree(server.name);
	return retCode;
}

int Daemon_Createsession_id(struct schandle *session_id)
{
	CREATE_CONTEXT_REQUEST cmd;
	PCREATE_CONTEXT_REPLY reply;
	unsigned long replylen = 0;
	int retCode = 0;

	DbgPrint("%s: %d\n", __func__, current->pid);

	cmd.command.command_type = VFS_COMMAND_CREATE_CONTEXT;
	cmd.command.sequence_number = 0;
	SC_INITIALIZE(cmd.command.session_id);

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (reply) {
		if (!reply->Reply.error_code
		    && replylen > sizeof(struct novfs_command_reply_header)) {
			*session_id = reply->session_id;
			retCode = 0;
		} else {
			session_id->hTypeId = NULL;
			session_id->hId = NULL;
			retCode = -EIO;
		}
		kfree(reply);
	}
	DbgPrint("%s: session_id=%p\n", __func__, session_id);
	return (retCode);
}

int Daemon_Destroysession_id(struct schandle *session_id)
{
	DESTROY_CONTEXT_REQUEST cmd;
	PDESTROY_CONTEXT_REPLY reply;
	unsigned long replylen = 0;
	int retCode = 0;

	DbgPrint("%s: %p:%p\n", __func__, session_id->hTypeId, session_id->hId);

	cmd.command.command_type = VFS_COMMAND_DESTROY_CONTEXT;
	cmd.command.sequence_number = 0;
	memcpy(&cmd.command.session_id, session_id, sizeof (*session_id));

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (reply) {
		if (!reply->Reply.error_code) {
			drive_map_t *dm;
			struct list_head *list;

			retCode = 0;

			/*
			 * When destroying the session check to see if there are any
			 * mapped drives.  If there are then remove them.
			 */
			down(&drive_map_lock);
			list_for_each(list, &drive_map_list) {
				struct schandle *temp;

				dm = list_entry(list, drive_map_t, list);
				temp = &dm->session;
				if (SC_EQUAL(session_id, temp)) {
					local_unlink(dm->name);
					list = list->prev;
					list_del(&dm->list);
					kfree(dm);
				}

			}
			up(&drive_map_lock);

		} else {
			retCode = -EIO;
		}
		kfree(reply);
	}
	return (retCode);
}

int Daemon_Get_UserSpace(struct schandle *session_id, uint64_t * TotalSize,
			 uint64_t * Free, uint64_t * TotalEnties,
			 uint64_t * FreeEnties)
{
	GET_USER_SPACE_REQUEST cmd;
	PGET_USER_SPACE_REPLY reply;
	unsigned long replylen = 0;
	int retCode = 0;

	DbgPrint("%s: %p:%p\n", __func__, session_id->hTypeId, session_id->hId);

	cmd.command.command_type = VFS_COMMAND_GET_USER_SPACE;
	cmd.command.sequence_number = 0;
	memcpy(&cmd.command.session_id, session_id, sizeof (*session_id));

	retCode =
	    Queue_Daemon_Command(&cmd, sizeof(cmd), NULL, 0, (void *)&reply,
				 &replylen, INTERRUPTIBLE);
	if (reply) {
		if (!reply->Reply.error_code) {
			DbgPrint("TotalSpace:  %llu\n", reply->TotalSpace);
			DbgPrint("FreeSpace:   %llu\n", reply->FreeSpace);
			DbgPrint("TotalEnties: %llu\n", reply->TotalEnties);
			DbgPrint("FreeEnties:  %llu\n", reply->FreeEnties);

			if (TotalSize)
				*TotalSize = reply->TotalSpace;
			if (Free)
				*Free = reply->FreeSpace;
			if (TotalEnties)
				*TotalEnties = reply->TotalEnties;
			if (FreeEnties)
				*FreeEnties = reply->FreeEnties;
			retCode = 0;
		} else {
			retCode = -EIO;
		}
		kfree(reply);
	}
	return (retCode);
}

/*++======================================================================*/
int Daemon_SetMountPoint(char *Path)
/*
 *
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	PSET_MOUNT_PATH_REQUEST cmd;
	PSET_MOUNT_PATH_REPLY reply;
	unsigned long replylen, cmdlen;
	int retCode = -ENOMEM;

	DbgPrint("%s: %s\n", __func__, Path);

	replylen = strlen(Path);
	cmdlen = sizeof(SET_MOUNT_PATH_REQUEST) + replylen;

	cmd = kmalloc(cmdlen, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->command.command_type = VFS_COMMAND_SET_MOUNT_PATH;
	cmd->command.sequence_number = 0;
	SC_INITIALIZE(cmd->command.session_id);
	cmd->PathLength = replylen;

	strcpy(cmd->Path, Path);

	replylen = 0;

	retCode = Queue_Daemon_Command(cmd, cmdlen, NULL, 0, (void *)&reply, &replylen, INTERRUPTIBLE);
	if (reply) {
		if (!reply->Reply.error_code) {
			retCode = 0;
		} else {
			retCode = -EIO;
		}
		kfree(reply);
	}
	kfree(cmd);
	return retCode;
}

int Daemon_SendDebugCmd(char *command)
{
	struct novfs_debug_request cmd;
	struct novfs_debug_reply *reply;
	struct novfs_debug_reply lreply;
	unsigned long replylen, cmdlen;
	struct data_list dlist[2];

	int retCode = -ENOMEM;

	DbgPrint("%s: %s\n", __func__, command);

	dlist[0].page = NULL;
	dlist[0].offset = (char *)command;
	dlist[0].len = strlen(command);
	dlist[0].rwflag = DLREAD;

	dlist[1].page = NULL;
	dlist[1].offset = (char *)&lreply;
	dlist[1].len = sizeof(lreply);
	dlist[1].rwflag = DLWRITE;

	cmdlen = offsetof(struct novfs_debug_request, dbgcmd);

	cmd.command.command_type = VFS_COMMAND_DBG;
	cmd.command.sequence_number = 0;
	SC_INITIALIZE(cmd.command.session_id);
	cmd.cmdlen = strlen(command);

	replylen = 0;

	retCode = Queue_Daemon_Command(&cmd, cmdlen, dlist, 2, (void *)&reply, &replylen, INTERRUPTIBLE);
	kfree(reply);
	if (retCode == 0)
		retCode = lreply.reply.error_code;

	return (retCode);
}

int Daemon_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int retCode = -ENOSYS;
	unsigned long cpylen;
	struct schandle session_id;

	switch (cmd) {
	case IOC_LOGIN:
		session_id = Scope_Get_session_id(NULL);
		retCode = daemon_login((struct login __user *)arg, &session_id);
		break;

	case IOC_LOGOUT:
		session_id = Scope_Get_session_id(NULL);
		retCode = daemon_logout((struct logout __user *) arg, &session_id);
		break;

	case IOC_DEBUGPRINT:
		{
			struct Ioctl_Debug {
				int length;
				char __user *data;
			} io;
			char *buf;
			io.length = 0;
			cpylen = copy_from_user(&io, (char __user *)arg, sizeof(io));
			if (io.length) {
				buf = kmalloc(io.length + 1, GFP_KERNEL);
				if (buf) {
					buf[0] = 0;
					cpylen = copy_from_user(buf, io.data, io.length);
					buf[io.length] = '\0';
					DbgPrint("%s", buf);
					kfree(buf);
					retCode = 0;
				}
			}
			break;
		}

	case IOC_XPLAT:
		{
			XPLAT data;

			cpylen = copy_from_user(&data, (void __user *)arg, sizeof(data));
			retCode = ((data.xfunction & 0x0000FFFF) | 0xCC000000);

			switch (data.xfunction) {
			case NWC_GET_MOUNT_PATH:
				DbgPrint("%s: Call NwdGetMountPath\n", __func__);
				retCode = NwdGetMountPath(&data);
				break;
			}

			DbgPrint("[NOVFS XPLAT] status Code = %X\n", retCode);
			break;
		}

	}
	return (retCode);
}

static int Daemon_Added_Resource(daemon_handle_t *DHandle, int Type, HANDLE CHandle, unsigned char *FHandle, unsigned long Mode, unsigned long Size)
{
	daemon_resource_t *resource;

	if (FHandle)
		DbgPrint("%s: DHandle=0x%p Type=%d CHandle=0x%p FHandle=0x%x Mode=0x%lx Size=%ld\n",
			 __func__, DHandle, Type, CHandle, *(u32 *) & FHandle[2], Mode, Size);
	else
		DbgPrint("%s: DHandle=0x%p Type=%d CHandle=0x%p\n", __func__, DHandle, Type, CHandle);

	resource = kmalloc(sizeof(daemon_resource_t), GFP_KERNEL);
	if (!resource)
		return -ENOMEM;

	resource->type = Type;
	resource->connection = CHandle;
	if (FHandle)
		memcpy(resource->handle, FHandle, sizeof(resource->handle));
	else
		memset(resource->handle, 0, sizeof(resource->handle));
	resource->mode = Mode;
	resource->size = Size;
	write_lock(&DHandle->lock);
	list_add(&resource->list, &DHandle->list);
	write_unlock(&DHandle->lock);
	DbgPrint("%s: Adding resource=0x%p\n", __func__, resource);

	return 0;
}

int Daemon_Remove_Resource(daemon_handle_t *DHandle, int Type, HANDLE CHandle,
			   unsigned long FHandle)
{
	daemon_resource_t *resource;
	struct list_head *l;
	int retVal = -ENOMEM;

	DbgPrint("%s: DHandle=%p Type=%d CHandle=%p FHandle=0x%lx\n",
		 __func__, DHandle, Type, CHandle, FHandle);

	write_lock(&DHandle->lock);

	list_for_each(l, &DHandle->list) {
		resource = list_entry(l, daemon_resource_t, list);

		if ((Type == resource->type) &&
		    (resource->connection == CHandle)) {
			DbgPrint
			    ("Daemon_Remove_Resource: Found resource=0x%p\n",
			     resource);
			l = l->prev;
			list_del(&resource->list);
			kfree(resource);
			break;
		}
	}

	write_unlock(&DHandle->lock);

	return (retVal);
}

int Daemon_Library_open(struct inode *inode, struct file *file)
{
	daemon_handle_t *dh;

	DbgPrint("%s: inode=0x%p file=0x%p\n", __func__, inode, file);

	dh = kmalloc(sizeof(daemon_handle_t), GFP_KERNEL);
	if (!dh)
		return -ENOMEM;

	file->private_data = dh;
	INIT_LIST_HEAD(&dh->list);
	rwlock_init(&dh->lock);
	dh->session = Scope_Get_session_id(NULL);

	return 0;
}

int Daemon_Library_close(struct inode *inode, struct file *file)
{
	daemon_handle_t *dh;
	daemon_resource_t *resource;
	struct list_head *l;

	char commanddata[sizeof(XPLAT_CALL_REQUEST) + sizeof(NwdCCloseConn)];
	PXPLAT_CALL_REQUEST cmd;
	PXPLAT_CALL_REPLY reply;
	PNwdCCloseConn nwdClose;
	unsigned long cmdlen, replylen;

	DbgPrint("%s: inode=0x%p file=0x%p\n", __func__, inode, file);
	if (file->private_data) {
		dh = (daemon_handle_t *) file->private_data;

		list_for_each(l, &dh->list) {
			resource = list_entry(l, daemon_resource_t, list);

			if (DH_TYPE_STREAM == resource->type) {
				Novfs_Close_Stream(resource->connection,
						   resource->handle,
						   dh->session);
			} else if (DH_TYPE_CONNECTION == resource->type) {
				cmd = (PXPLAT_CALL_REQUEST) commanddata;
				cmdlen =
				    offsetof(XPLAT_CALL_REQUEST,
					     data) + sizeof(NwdCCloseConn);
				cmd->command.command_type = VFS_COMMAND_XPLAT_CALL;
				cmd->command.sequence_number = 0;
				cmd->command.session_id = dh->session;
				cmd->nwc_command = NWC_CLOSE_CONN;

				cmd->dataLen = sizeof(NwdCCloseConn);
				nwdClose = (PNwdCCloseConn) cmd->data;
				nwdClose->ConnHandle = (HANDLE)resource->connection;

				Queue_Daemon_Command((void *)cmd, cmdlen, NULL,
						     0, (void **)&reply,
						     &replylen, 0);
				if (reply)
					kfree(reply);
			}
			l = l->prev;
			list_del(&resource->list);
			kfree(resource);
		}
		kfree(dh);
		file->private_data = NULL;
	}

	return (0);
}

ssize_t Daemon_Library_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
	daemon_handle_t *dh;
	daemon_resource_t *resource;

	size_t thisread, totalread = 0;
	loff_t offset = *off;

	DbgPrint("%s: file=0x%p len=%d off=%lld\n", __func__, file, len, *off);

	if (file->private_data) {
		dh = file->private_data;
		read_lock(&dh->lock);
		if (&dh->list != dh->list.next) {
			resource =
			    list_entry(dh->list.next, daemon_resource_t, list);

			if (DH_TYPE_STREAM == resource->type) {
				while (len > 0 && (offset < resource->size)) {
					thisread = len;
					if (Novfs_Read_Stream(resource->connection, resource->handle, buf, &thisread, &offset, dh->session) || !thisread) {
						break;
					}
					len -= thisread;
					buf += thisread;
					offset += thisread;
					totalread += thisread;
				}
			}
		}
		read_unlock(&dh->lock);
	}
	*off = offset;
	DbgPrint("%s: return = 0x%x\n", __func__, totalread);
	return (totalread);
}

ssize_t Daemon_Library_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
	daemon_handle_t *dh;
	daemon_resource_t *resource;

	size_t thiswrite, totalwrite = -EINVAL;
	loff_t offset = *off;
	int status;

	DbgPrint("%s: file=0x%p len=%d off=%lld\n", __func__, file, len, *off);

	if (file->private_data) {
		dh = file->private_data;
		write_lock(&dh->lock);
		if (&dh->list != dh->list.next) {
			resource =
			    list_entry(dh->list.next, daemon_resource_t, list);

			if ((DH_TYPE_STREAM == resource->type) && (len >= 0)) {
				totalwrite = 0;
				do {
					thiswrite = len;
					status =
					    Novfs_Write_Stream(resource->
							       connection,
							       resource->handle,
							       buf,
							       &thiswrite,
							       &offset,
							       dh->session);
					if (status || !thiswrite) {
						/*
						 * If len is zero then the file will have just been
						 * truncated to offset.  Update size.
						 */
						if (!status && !len) {
							resource->size = offset;
						}
						totalwrite = status;
						break;
					}
					len -= thiswrite;
					buf += thiswrite;
					offset += thiswrite;
					totalwrite += thiswrite;
					if (offset > resource->size) {
						resource->size = offset;
					}
				} while (len > 0);
			}
		}
		write_unlock(&dh->lock);
	}
	*off = offset;
	DbgPrint("%s: return = 0x%x\n", __func__, totalwrite);

	return (totalwrite);
}

loff_t Daemon_Library_llseek(struct file *file, loff_t offset, int origin)
{
	daemon_handle_t *dh;
	daemon_resource_t *resource;

	loff_t retVal = -EINVAL;

	DbgPrint("%s: file=0x%p offset=%lld origin=%d\n", __func__,
		 file, offset, origin);

	if (file->private_data) {
		dh = file->private_data;
		read_lock(&dh->lock);
		if (&dh->list != dh->list.next) {
			resource =
			    list_entry(dh->list.next, daemon_resource_t, list);

			if (DH_TYPE_STREAM == resource->type) {
				switch (origin) {
				case 2:
					offset += resource->size;
					break;
				case 1:
					offset += file->f_pos;
				}
				if (offset >= 0) {
					if (offset != file->f_pos) {
						file->f_pos = offset;
						file->f_version = 0;
					}
					retVal = offset;
				}
			}
		}
		read_unlock(&dh->lock);
	}

	DbgPrint("%s: ret %lld\n", __func__, retVal);

	return retVal;
}

int Daemon_Library_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int retCode = -ENOSYS;
	daemon_handle_t *dh;
	HANDLE handle = NULL;
	unsigned long cpylen;

	dh = file->private_data;

	DbgPrint("%s: file=0x%p 0x%x 0x%lx dh=0x%p\n", __func__, file, cmd, arg, dh);

	if (dh) {

		switch (cmd) {
		case IOC_LOGIN:
			retCode = daemon_login((struct login __user*)arg, &dh->session);
			break;

		case IOC_LOGOUT:
			retCode = daemon_logout((struct logout __user*)arg, &dh->session);
			break;

		case IOC_DEBUGPRINT:
			{
				struct Ioctl_Debug {
					int length;
					char __user *data;
				} io;
				char *buf;
				io.length = 0;
				cpylen = copy_from_user(&io, (void __user *)arg, sizeof(io));
				if (io.length) {
					buf = kmalloc(io.length + 1, GFP_KERNEL);
					if (buf) {
						buf[0] = 0;
						cpylen = copy_from_user(buf, io.data, io.length);
						buf[io.length] = '\0';
						DbgPrint("%s", buf);
						kfree(buf);
						retCode = 0;
					}
				}
				break;
			}

		case IOC_XPLAT:
			{
				XPLAT data;

				cpylen = copy_from_user(&data, (void __user *)arg, sizeof(data));
				retCode = ((data.xfunction & 0x0000FFFF) | 0xCC000000);

				switch (data.xfunction) {
				case NWC_OPEN_CONN_BY_NAME:
					DbgPrint("[VFS XPLAT] Call NwOpenConnByName\n");
					retCode = NwOpenConnByName(&data, &handle, dh->session);
					if (!retCode)
						Daemon_Added_Resource(dh, DH_TYPE_CONNECTION, handle, NULL, 0, 0);
					break;

				case NWC_OPEN_CONN_BY_ADDRESS:
					DbgPrint("[VFS XPLAT] Call NwOpenConnByAddress\n");
					retCode = NwOpenConnByAddr(&data, &handle, dh->session);
					if (!retCode)
						Daemon_Added_Resource(dh, DH_TYPE_CONNECTION, handle, NULL, 0, 0);
					break;

				case NWC_OPEN_CONN_BY_REFERENCE:
					DbgPrint("[VFS XPLAT] Call NwOpenConnByReference\n");
					retCode = NwOpenConnByRef(&data, &handle, dh->session);
					if (!retCode)
						Daemon_Added_Resource(dh,
								      DH_TYPE_CONNECTION,
								      handle, NULL,
								      0, 0);
					break;

				case NWC_SYS_CLOSE_CONN:
					DbgPrint("[VFS XPLAT] Call NwSysCloseConn\n");
					retCode = NwSysConnClose(&data, (unsigned long *)&handle, dh->session);
					Daemon_Remove_Resource(dh, DH_TYPE_CONNECTION, handle, 0);
					break;

				case NWC_CLOSE_CONN:
					DbgPrint
					    ("[VFS XPLAT] Call NwCloseConn\n");
					retCode =
					    NwConnClose(&data, &handle,
							dh->session);
					Daemon_Remove_Resource(dh,
							       DH_TYPE_CONNECTION,
							       handle, 0);
					break;

				case NWC_LOGIN_IDENTITY:
					DbgPrint("[VFS XPLAT] Call NwLoginIdentity\n");
					retCode = NwLoginIdentity(&data, &dh->session);
					break;

				case NWC_RAW_NCP_REQUEST:
					DbgPrint("[VFS XPLAT] Send Raw NCP Request\n");
					retCode = NwRawSend(&data, dh->session);
					break;

				case NWC_AUTHENTICATE_CONN_WITH_ID:
					DbgPrint
					    ("[VFS XPLAT] Authenticate Conn With ID\n");
					retCode =
					    NwAuthConnWithId(&data,
							     dh->session);
					break;

				case NWC_UNAUTHENTICATE_CONN:
					DbgPrint
					    ("[VFS XPLAT] UnAuthenticate Conn With ID\n");
					retCode =
					    NwUnAuthenticate(&data,
							     dh->session);
					break;

				case NWC_LICENSE_CONN:
					DbgPrint("Call NwLicenseConn\n");
					retCode =
					    NwLicenseConn(&data, dh->session);
					break;

				case NWC_LOGOUT_IDENTITY:
					DbgPrint
					    ("[VFS XPLAT] Call NwLogoutIdentity\n");
					retCode =
					    NwLogoutIdentity(&data,
							     dh->session);
					break;

				case NWC_UNLICENSE_CONN:
					DbgPrint
					    ("[VFS XPLAT] Call NwUnlicense\n");
					retCode =
					    NwUnlicenseConn(&data, dh->session);
					break;

				case NWC_GET_CONN_INFO:
					DbgPrint
					    ("[VFS XPLAT] Call NwGetConnInfo\n");
					retCode =
					    NwGetConnInfo(&data, dh->session);
					break;

				case NWC_SET_CONN_INFO:
					DbgPrint
					    ("[VFS XPLAT] Call NwGetConnInfo\n");
					retCode =
					    NwSetConnInfo(&data, dh->session);
					break;

				case NWC_SCAN_CONN_INFO:
					DbgPrint
					    ("[VFS XPLAT] Call NwScanConnInfo\n");
					retCode =
					    NwScanConnInfo(&data, dh->session);
					break;

				case NWC_GET_IDENTITY_INFO:
					DbgPrint
					    ("[VFS XPLAT] Call NwGetIdentityInfo\n");
					retCode =
					    NwGetIdentityInfo(&data,
							      dh->session);
					break;

				case NWC_GET_REQUESTER_VERSION:
					DbgPrint
					    ("[VFS XPLAT] Call NwGetDaemonVersion\n");
					retCode =
					    NwGetDaemonVersion(&data,
							       dh->session);
					break;

				case NWC_GET_PREFERRED_DS_TREE:
					DbgPrint
					    ("[VFS XPLAT] Call NwcGetPreferredDsTree\n");
					retCode =
					    NwcGetPreferredDSTree(&data,
								  dh->session);
					break;

				case NWC_SET_PREFERRED_DS_TREE:
					DbgPrint
					    ("[VFS XPLAT] Call NwcSetPreferredDsTree\n");
					retCode =
					    NwcSetPreferredDSTree(&data,
								  dh->session);
					break;

				case NWC_GET_DEFAULT_NAME_CONTEXT:
					DbgPrint
					    ("[VFS XPLAT] Call NwcGetDefaultNameContext\n");
					retCode =
					    NwcGetDefaultNameCtx(&data,
								 dh->session);
					break;

				case NWC_SET_DEFAULT_NAME_CONTEXT:
					DbgPrint
					    ("[VFS XPLAT] Call NwcSetDefaultNameContext\n");
					retCode =
					    NwcSetDefaultNameCtx(&data,
								 dh->session);
					break;

				case NWC_QUERY_FEATURE:
					DbgPrint
					    ("[VFS XPLAT] Call NwQueryFeature\n");
					retCode =
					    NwQueryFeature(&data, dh->session);
					break;

				case NWC_GET_TREE_MONITORED_CONN_REF:
					DbgPrint
					    ("[VFS XPLAT] Call NwcGetTreeMonitoredConn\n");
					retCode =
					    NwcGetTreeMonitoredConn(&data,
								    dh->
								    session);
					break;

				case NWC_ENUMERATE_IDENTITIES:
					DbgPrint
					    ("[VFS XPLAT] Call NwcEnumerateIdentities\n");
					retCode =
					    NwcEnumIdentities(&data,
							      dh->session);
					break;

				case NWC_CHANGE_KEY:
					DbgPrint
					    ("[VFS XPLAT] Call NwcChangeAuthKey\n");
					retCode =
					    NwcChangeAuthKey(&data,
							     dh->session);
					break;

				case NWC_CONVERT_LOCAL_HANDLE:
					DbgPrint
					    ("[VFS XPLAT] Call NwdConvertLocalHandle\n");
					retCode =
					    NwdConvertLocalHandle(&data, dh);
					break;

				case NWC_CONVERT_NETWARE_HANDLE:
					DbgPrint
					    ("[VFS XPLAT] Call NwdConvertNetwareHandle\n");
					retCode =
					    NwdConvertNetwareHandle(&data, dh);
					break;

				case NWC_SET_PRIMARY_CONN:
					DbgPrint
					    ("[VFS XPLAT] Call NwcSetPrimaryConn\n");
					retCode =
					    NwcSetPrimaryConn(&data,
							      dh->session);
					break;

				case NWC_GET_PRIMARY_CONN:
					DbgPrint
					    ("[VFS XPLAT] Call NwcGetPrimaryConn\n");
					retCode =
					    NwcGetPrimaryConn(&data,
							      dh->session);
					break;

				case NWC_MAP_DRIVE:
					DbgPrint("[VFS XPLAT] Call NwcMapDrive\n");
					retCode = NwdSetMapDrive(&data, dh->session);
					break;

				case NWC_UNMAP_DRIVE:
					DbgPrint
					    ("[VFS XPLAT] Call NwcUnMapDrive\n");
					retCode = NwdUnMapDrive(&data, dh->session);
					break;

				case NWC_ENUMERATE_DRIVES:
					DbgPrint
					    ("[VFS XPLAT] Call NwcEnumerateDrives\n");
					retCode =
					    NwcEnumerateDrives(&data,
							       dh->session);
					break;

				case NWC_GET_MOUNT_PATH:
					DbgPrint
					    ("[VFS XPLAT] Call NwdGetMountPath\n");
					retCode = NwdGetMountPath(&data);
					break;

				case NWC_GET_BROADCAST_MESSAGE:
					DbgPrint
					    ("[VSF XPLAT Call NwdGetBroadcastMessage\n");
					retCode =
					    NwcGetBroadcastMessage(&data,
								   dh->session);
					break;

				case NWC_SET_KEY:
					DbgPrint("[VSF XPLAT Call NwdSetKey\n");
					retCode =
					    NwdSetKeyValue(&data, dh->session);
					break;

				case NWC_VERIFY_KEY:
					DbgPrint
					    ("[VSF XPLAT Call NwdVerifyKey\n");
					retCode =
					    NwdVerifyKeyValue(&data,
							      dh->session);
					break;

				case NWC_RAW_NCP_REQUEST_ALL:
				case NWC_NDS_RESOLVE_NAME_TO_ID:
				case NWC_FRAGMENT_REQUEST:
				case NWC_GET_CONFIGURED_NSPS:
				default:
					break;

				}

				DbgPrint("[NOVFS XPLAT] status Code = %X\n",
					 retCode);
				break;
			}
		}
	}

	return (retCode);
}

unsigned int Daemon_Poll(struct file *file, struct poll_table_struct *poll_table)
{
	daemon_command_t *que;
	unsigned int mask = POLLOUT | POLLWRNORM;

	que = get_next_queue(0);
	if (que)
		mask |= (POLLIN | POLLRDNORM);
	return mask;
}

int NwdConvertNetwareHandle(PXPLAT pdata, daemon_handle_t *DHandle)
{
	int retVal;
	NwcConvertNetWareHandle nh;
	unsigned long cpylen;

	DbgPrint("NwdConvertNetwareHandle: DHandle=0x%p\n", DHandle);

	cpylen = copy_from_user(&nh, pdata->reqData, sizeof(NwcConvertNetWareHandle));

	retVal = Daemon_Added_Resource(DHandle, DH_TYPE_STREAM,
				       Uint32toHandle(nh.ConnHandle),
				       nh.NetWareHandle, nh.uAccessMode,
				       nh.uFileSize);

	return retVal;
}

/*++======================================================================*/
int NwdConvertLocalHandle(PXPLAT pdata, daemon_handle_t * DHandle)
/*
 *
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	int retVal = NWE_REQUESTER_FAILURE;
	daemon_resource_t *resource;
	NwcConvertLocalHandle lh;
	struct list_head *l;
	unsigned long cpylen;

	DbgPrint("NwdConvertLocalHandle: DHandle=0x%p\n", DHandle);

	read_lock(&DHandle->lock);

	list_for_each(l, &DHandle->list) {
		resource = list_entry(l, daemon_resource_t, list);

		if (DH_TYPE_STREAM == resource->type) {
			lh.uConnReference =
			    HandletoUint32(resource->connection);

//sgled         memcpy(lh.NwWareHandle, resource->handle, sizeof(resource->handle));
			memcpy(lh.NetWareHandle, resource->handle, sizeof(resource->handle));	//sgled
			if (pdata->repLen >= sizeof(NwcConvertLocalHandle)) {
				cpylen = copy_to_user(pdata->repData, &lh, sizeof(NwcConvertLocalHandle));
				retVal = 0;
			} else {
				retVal = NWE_BUFFER_OVERFLOW;
			}
			break;
		}
	}

	read_unlock(&DHandle->lock);

	return (retVal);
}

/*++======================================================================*/
int NwdGetMountPath(PXPLAT pdata)
/*
 *
 *  Arguments:
 *
 *  Returns:
 *
 *  Abstract:
 *
 *  Notes:
 *
 *  Environment:
 *
 *========================================================================*/
{
	int retVal = NWE_REQUESTER_FAILURE;
	int len;
	unsigned long cpylen;
	NwcGetMountPath mp;

	cpylen = copy_from_user(&mp, pdata->reqData, pdata->reqLen);

	if (Novfs_CurrentMount) {

		len = strlen(Novfs_CurrentMount) + 1;
		if ((len > mp.MountPathLen) && mp.pMountPath) {
			retVal = NWE_BUFFER_OVERFLOW;
		} else {
			if (mp.pMountPath) {
				cpylen = copy_to_user(mp.pMountPath, Novfs_CurrentMount, len);
			}
			retVal = 0;
		}

		mp.MountPathLen = len;

		if (pdata->repData && (pdata->repLen >= sizeof(mp))) {
			cpylen = copy_to_user(pdata->repData, &mp, sizeof(mp));
		}
	}

	return (retVal);
}

static int NwdSetMapDrive(PXPLAT pdata, session_t Session)
{
	int retVal;
	NwcMapDriveEx symInfo;
	char __user *path;
	drive_map_t *drivemap, *dm;
	struct list_head *list;

	retVal = NwcSetMapDrive(pdata, Session);
	if (retVal)
		return retVal;

	if (copy_from_user(&symInfo, pdata->reqData, sizeof(symInfo)))
		return -EFAULT;

	drivemap = kmalloc(sizeof(drive_map_t) + symInfo.linkOffsetLength, GFP_KERNEL);
	if (!drivemap)
		return -ENOMEM;

	path = pdata->reqData;
	path += symInfo.linkOffset;
	if (copy_from_user(drivemap->name, path, symInfo.linkOffsetLength)) {
		kfree(drivemap);
		return -EFAULT;
	}

	drivemap->session = Session;
	drivemap->hash = full_name_hash(drivemap->name, symInfo.linkOffsetLength - 1);
	drivemap->namelen = symInfo.linkOffsetLength - 1;
	DbgPrint("%s: hash=0x%lx path=%s\n", __func__, drivemap->hash, drivemap->name);

	dm = (drive_map_t *) & drive_map_list.next;

	down(&drive_map_lock);

	list_for_each(list, &drive_map_list) {
		dm = list_entry(list, drive_map_t, list);
		DbgPrint("%s: dm=0x%p hash: 0x%lx namelen: %d name: %s\n",
			 __func__, dm, dm->hash, dm->namelen, dm->name);

		if (drivemap->hash == dm->hash) {
			if (0 ==
			    strcmp(dm->name, drivemap->name)) {
				dm = NULL;
				break;
			}
		} else if (drivemap->hash < dm->hash) {
			break;
		}
	}

	if (dm) {
		if ((dm == (drive_map_t *) & drive_map_list) ||
		    (dm->hash < drivemap->hash)) {
			list_add(&drivemap->list, &dm->list);
		} else {
			list_add_tail(&drivemap->list,
				      &dm->list);
		}
	} else {
		kfree(drivemap);
	}
	up(&drive_map_lock);

	return (retVal);
}

static int NwdUnMapDrive(PXPLAT pdata, session_t Session)
{
	int retVal = NWE_REQUESTER_FAILURE;
	NwcUnmapDriveEx symInfo;
	char *path;
	drive_map_t *dm;
	struct list_head *list;
	unsigned long hash;

	retVal = NwcUnMapDrive(pdata, Session);
	if (retVal)
		return retVal;

	if (copy_from_user(&symInfo, pdata->reqData, sizeof(symInfo)))
		return -EFAULT;

	path = kmalloc(symInfo.linkLen, GFP_KERNEL);
	if (!path)
		return -ENOMEM;

	if (copy_from_user(path, ((NwcUnmapDriveEx __user *)pdata->reqData)->linkData, symInfo.linkLen)) {
		kfree(path);
		return -EFAULT;
	}

	hash = full_name_hash(path, symInfo.linkLen - 1);
	DbgPrint("%s: hash=0x%lx path=%s\n", __func__, hash, path);

	dm = NULL;

	down(&drive_map_lock);

	list_for_each(list, &drive_map_list) {
		dm = list_entry(list, drive_map_t, list);
		DbgPrint("%s: dm=0x%p %s hash: 0x%lx namelen: %d\n", __func__,
			 dm, dm->name, dm->hash, dm->namelen);

		if (hash == dm->hash) {
			if (0 == strcmp(dm->name, path)) {
				break;
			}
		} else if (hash < dm->hash) {
			dm = NULL;
			break;
		}
	}

	if (dm) {
		DbgPrint("%s: Remove dm=0x%p %s hash: 0x%lx namelen: %d\n",
			 __func__, dm, dm->name, dm->hash, dm->namelen);
		list_del(&dm->list);
		kfree(dm);
	}

	up(&drive_map_lock);

	return retVal;
}


