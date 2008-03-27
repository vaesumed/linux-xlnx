/*++========================================================================
 * Program Name:     Novell NCP Redirector for Linux
 * File Name:        profile.c
 * Version:          v1.00
 * Author:           James Turner
 *
 * Abstract:         This module contains a debugging code for
 *                   the novfs VFS.
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

/*===[ Include files specific to Linux ]==================================*/
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/time.h>

#include <linux/profile.h>
#include <linux/notifier.h>

/*===[ Include files specific to this module ]============================*/
#include "vfs.h"

/*===[ External data ]====================================================*/
extern struct dentry *Novfs_root;
extern struct proc_dir_entry *Novfs_Procfs_dir;
extern unsigned long File_update_timeout;
extern int PageCache;

/*===[ External prototypes ]==============================================*/
extern void Scope_Dump_Tasklist(void);
extern void Scope_Dump_Scopetable(void);
extern void Daemon_Dumpque(void);
extern char *Scope_dget_path(struct dentry *Dentry, char *Buf,
			     unsigned int Buflen, int Flags);
extern int Novfs_dump_inode_cache(int argc, const char **argv,
				  const char **envp, struct pt_regs *regs);
extern void Novfs_dump_inode(void *pf);
extern int Daemon_SendDebugCmd(char *Command);

/*===[ Manifest constants ]===============================================*/
#define DBGBUFFERSIZE (1024*1024*32)

/*===[ Type definitions ]=================================================*/
typedef void daemon_command_t;

typedef struct _SYMBOL_TABLE {
	void *address;
	char *name;
} SYMBOL_TABLE, *PSYMBOL_TABLE;

struct local_rtc_time {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};

/*===[ Function prototypes ]==============================================*/
int profile_task_exit_callback(struct notifier_block *self, unsigned long val,
			       void *data)
    __attribute__ ((__no_instrument_function__));
int init_profile(void);

char *ctime_r(time_t * clock, char *buf);
int LocalPrint(char *Fmt, ...) __attribute__ ((__no_instrument_function__));
int DbgPrint(char *Fmt, ...) __attribute__ ((__no_instrument_function__));

void __cyg_profile_func_enter(void *this_fn, void *call_site)
    __attribute__ ((__no_instrument_function__));
void __cyg_profile_func_exit(void *this_fn, void *call_site)
    __attribute__ ((__no_instrument_function__));
void doline(unsigned char *b, unsigned char *p, unsigned char *l)
    __attribute__ ((__no_instrument_function__));
void mydump(int size, void *dumpptr)
    __attribute__ ((__no_instrument_function__));
void GregorianDay(struct local_rtc_time *tm)
    __attribute__ ((__no_instrument_function__));
void to_tm(int tim, struct local_rtc_time *tm)
    __attribute__ ((__no_instrument_function__));
char *ctime_r(time_t * clock, char *buf)
    __attribute__ ((__no_instrument_function__));
int profile_dump_tasklist(int argc, const char **argv, const char **envp,
			  struct pt_regs *regs);
int profile_dump_scopetable(int argc, const char **argv, const char **envp,
			    struct pt_regs *regs);
int profile_dump_daemonque(int argc, const char **argv, const char **envp,
			   struct pt_regs *regs);
int profile_dump_DbgBuffer(int argc, const char **argv, const char **envp,
			   struct pt_regs *regs);
int profile_dump_DentryTree(int argc, const char **argv, const char **envp,
			    struct pt_regs *regs);
int profile_dump_inode(int argc, const char **argv, const char **envp,
		       struct pt_regs *regs);

void *Novfs_Malloc(size_t size, int flags)
    __attribute__ ((__no_instrument_function__));
void Novfs_Free(const void *p) __attribute__ ((__no_instrument_function__));

int profile_dump_memorylist_dbg(int argc, const char **argv, const char **envp,
				struct pt_regs *regs)
    __attribute__ ((__no_instrument_function__));
void profile_dump_memorylist(void *pf);

static ssize_t User_proc_write_DbgBuffer(struct file *file,
					 const char __user * buf, size_t nbytes,
					 loff_t * ppos)
    __attribute__ ((__no_instrument_function__));
static ssize_t User_proc_read_DbgBuffer(struct file *file, char *buf,
					size_t nbytes, loff_t * ppos)
    __attribute__ ((__no_instrument_function__));
static int proc_read_DbgBuffer(char *page, char **start, off_t off, int count,
			       int *eof, void *data)
    __attribute__ ((__no_instrument_function__));

void profile_dump_dt(struct dentry *parent, void *pf);
ssize_t profile_inode_read(struct file *file, char *buf, size_t len,
			   loff_t * off);
ssize_t profile_dentry_read(struct file *file, char *buf, size_t len,
			    loff_t * off);
ssize_t profile_memory_read(struct file *file, char *buf, size_t len,
			    loff_t * off);
uint64_t get_nanosecond_time(void);

/*===[ Global variables ]=================================================*/
char *DbgPrintBuffer = NULL;
char DbgPrintOn = 0;
char DbgSyslogOn = 0;
char DbgProfileOn = 0;

unsigned long DbgPrintBufferOffset = 0;
unsigned long DbgPrintBufferReadOffset = 0;
unsigned long DbgPrintBufferSize = DBGBUFFERSIZE;

int Indent = 0;
char IndentString[] = "                                       ";

static struct file_operations Dbg_proc_file_operations;
static struct file_operations dentry_proc_file_ops;
static struct file_operations inode_proc_file_ops;
static struct file_operations memory_proc_file_ops;

static struct proc_dir_entry *dbg_dir = NULL, *dbg_file = NULL;
static struct proc_dir_entry *dentry_file = NULL;
static struct proc_dir_entry *inode_file = NULL;
static struct proc_dir_entry *memory_file = NULL;

static struct notifier_block taskexit_nb;

DECLARE_MUTEX(LocalPrint_lock);
spinlock_t Syslog_lock = SPIN_LOCK_UNLOCKED;

#include "profile_funcs.h"

/*===[ Code ]=============================================================*/
int
profile_task_exit_callback(struct notifier_block *self, unsigned long val,
			   void *data)
{
	struct task_struct *task = (struct task_struct *)data;

	DbgPrint("profile_task_exit_callback: task 0x%p %u exiting %s\n", task,
		 task->pid, task->comm);
	return (0);
}

int init_profile(void)
{
	int retCode = 0;

	if (Novfs_Procfs_dir) {
		dbg_dir = Novfs_Procfs_dir;
	} else {
		dbg_dir = proc_mkdir(MODULE_NAME, NULL);
	}

	if (dbg_dir) {
		dbg_dir->owner = THIS_MODULE;
		dbg_file = create_proc_read_entry("Debug",
						  0600,
						  dbg_dir,
						  proc_read_DbgBuffer, NULL);
		if (dbg_file) {
			dbg_file->owner = THIS_MODULE;
			dbg_file->size = DBGBUFFERSIZE;
			memcpy(&Dbg_proc_file_operations, dbg_file->proc_fops,
			       sizeof(struct file_operations));
			Dbg_proc_file_operations.read =
			    User_proc_read_DbgBuffer;
			Dbg_proc_file_operations.write =
			    User_proc_write_DbgBuffer;
			dbg_file->proc_fops = &Dbg_proc_file_operations;
		} else {
			remove_proc_entry(MODULE_NAME, NULL);
			vfree(DbgPrintBuffer);
			DbgPrintBuffer = NULL;
		}
	}

	if (DbgPrintBuffer) {
		if (dbg_dir) {
			inode_file = create_proc_entry("inode", 0600, dbg_dir);
			if (inode_file) {
				inode_file->owner = THIS_MODULE;
				inode_file->size = 0;
				memcpy(&inode_proc_file_ops,
				       inode_file->proc_fops,
				       sizeof(struct file_operations));
				inode_proc_file_ops.owner = THIS_MODULE;
				inode_proc_file_ops.read = profile_inode_read;
				inode_file->proc_fops = &inode_proc_file_ops;
			}

			dentry_file = create_proc_entry("dentry",
							0600, dbg_dir);
			if (dentry_file) {
				dentry_file->owner = THIS_MODULE;
				dentry_file->size = 0;
				memcpy(&dentry_proc_file_ops,
				       dentry_file->proc_fops,
				       sizeof(struct file_operations));
				dentry_proc_file_ops.owner = THIS_MODULE;
				dentry_proc_file_ops.read = profile_dentry_read;
				dentry_file->proc_fops = &dentry_proc_file_ops;
			}

			memory_file = create_proc_entry("memory",
							0600, dbg_dir);
			if (memory_file) {
				memory_file->owner = THIS_MODULE;
				memory_file->size = 0;
				memcpy(&memory_proc_file_ops,
				       memory_file->proc_fops,
				       sizeof(struct file_operations));
				memory_proc_file_ops.owner = THIS_MODULE;
				memory_proc_file_ops.read = profile_memory_read;
				memory_file->proc_fops = &memory_proc_file_ops;
			}

		} else {
			vfree(DbgPrintBuffer);
			DbgPrintBuffer = NULL;
		}
	}
	return (retCode);
}

void uninit_profile(void)
{
	if (dbg_file)
		DbgPrint("Calling remove_proc_entry(Debug, NULL)\n"),
		    remove_proc_entry("Debug", dbg_dir);
	if (inode_file)
		DbgPrint("Calling remove_proc_entry(inode, NULL)\n"),
		    remove_proc_entry("inode", dbg_dir);
	if (dentry_file)
		DbgPrint("Calling remove_proc_entry(dentry, NULL)\n"),
		    remove_proc_entry("dentry", dbg_dir);
	if (memory_file)
		DbgPrint("Calling remove_proc_entry(memory, NULL)\n"),
		    remove_proc_entry("memory", dbg_dir);

	if (dbg_dir && (dbg_dir != Novfs_Procfs_dir)) {
		DbgPrint("Calling remove_proc_entry(%s, NULL)\n", MODULE_NAME);
		remove_proc_entry(MODULE_NAME, NULL);
	}
}

static
    ssize_t
User_proc_write_DbgBuffer(struct file *file, const char __user * buf,
			  size_t nbytes, loff_t * ppos)
{
	ssize_t retval = nbytes;
	u_char *lbuf, *p;
	int i;
	u_long cpylen;

	UNUSED_VARIABLE(*ppos);

	lbuf = Novfs_Malloc(nbytes + 1, GFP_KERNEL);
	if (lbuf) {
		cpylen = copy_from_user(lbuf, buf, nbytes);

		lbuf[nbytes] = 0;
		DbgPrint("User_proc_write_DbgBuffer: %s\n", lbuf);

		for (i = 0; lbuf[i] && lbuf[i] != '\n'; i++) ;

		if ('\n' == lbuf[i]) {
			lbuf[i] = '\0';
		}

		if (!strcmp("on", lbuf)) {
			DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;
			DbgPrintOn = 1;
		} else if (!strcmp("off", lbuf)) {
			DbgPrintOn = 0;
		} else if (!strcmp("reset", lbuf)) {
			DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;
		} else if (NULL != (p = strchr(lbuf, ' '))) {
			*p++ = '\0';
			if (!strcmp("syslog", lbuf)) {

				if (!strcmp("on", p)) {
					DbgSyslogOn = 1;
				} else if (!strcmp("off", p)) {
					DbgSyslogOn = 0;
				}
			} else if (!strcmp("novfsd", lbuf)) {
				Daemon_SendDebugCmd(p);
			} else if (!strcmp("file_update_timeout", lbuf)) {
				File_update_timeout =
				    simple_strtoul(p, NULL, 0);
			} else if (!strcmp("cache", lbuf)) {
				if (!strcmp("on", p)) {
					PageCache = 1;
				} else if (!strcmp("off", p)) {
					PageCache = 0;
				}
			} else if (!strcmp("profile", lbuf)) {
				if (!strcmp("on", p)) {
					DbgProfileOn = 1;
				} else if (!strcmp("off", p)) {
					DbgProfileOn = 0;
				}
			}
		}
		Novfs_Free(lbuf);
	}

	return (retval);
}

static
 ssize_t
User_proc_read_DbgBuffer(struct file *file, char *buf, size_t nbytes,
			 loff_t * ppos)
{
	ssize_t retval = 0;
	size_t count;

	UNUSED_VARIABLE(*ppos);

	if (0 != (count = DbgPrintBufferOffset - DbgPrintBufferReadOffset)) {

		if (count > nbytes) {
			count = nbytes;
		}

		count -=
		    copy_to_user(buf, &DbgPrintBuffer[DbgPrintBufferReadOffset],
				 count);

		if (count == 0) {
			if (retval == 0)
				retval = -EFAULT;
		} else {
			DbgPrintBufferReadOffset += count;
			if (DbgPrintBufferReadOffset >= DbgPrintBufferOffset) {
				DbgPrintBufferOffset =
				    DbgPrintBufferReadOffset = 0;
			}
			retval = count;
		}
	}

	return retval;
}

static
int
proc_read_DbgBuffer(char *page, char **start,
		    off_t off, int count, int *eof, void *data)
{
	int len;
	static char bufd[512];

	UNUSED_VARIABLE(start);
	UNUSED_VARIABLE(eof);
	UNUSED_VARIABLE(data);

	sprintf(bufd,
		KERN_ALERT
		"proc_read_DbgBuffer: off=%ld count=%d DbgPrintBufferOffset=%lu DbgPrintBufferReadOffset=%lu\n",
		off, count, DbgPrintBufferOffset, DbgPrintBufferReadOffset);
	printk(bufd);

	len = DbgPrintBufferOffset - DbgPrintBufferReadOffset;

	if ((int)(DbgPrintBufferOffset - DbgPrintBufferReadOffset) > count) {
		len = count;
	}

	if (len) {
		memcpy(page, &DbgPrintBuffer[DbgPrintBufferReadOffset], len);
		DbgPrintBufferReadOffset += len;
	}

	if (DbgPrintBufferReadOffset >= DbgPrintBufferOffset) {
		DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;
	}

	sprintf(bufd, KERN_ALERT "proc_read_DbgBuffer: return %d\n", len);
	printk(bufd);

	return len;
}

#define DBG_BUFFER_SIZE (2*1024)

int LocalPrint(char *Fmt, ...)
{
	int len = 0;
	va_list args;

	if (DbgPrintBuffer) {
		va_start(args, Fmt);
		len +=
		    vsnprintf(DbgPrintBuffer + DbgPrintBufferOffset,
			      DbgPrintBufferSize - DbgPrintBufferOffset, Fmt,
			      args);
		DbgPrintBufferOffset += len;
	}

	return (len);
}

int DbgPrint(char *Fmt, ...)
{
	char *buf;
	int len = 0;
	unsigned long offset;
	va_list args;

	if ((DbgPrintBuffer && DbgPrintOn) || DbgSyslogOn) {
		buf = kmalloc(DBG_BUFFER_SIZE, GFP_KERNEL);

		if (buf) {
			va_start(args, Fmt);
			len = sprintf(buf, "[%d] ", current->pid);

			len +=
			    vsnprintf(buf + len, DBG_BUFFER_SIZE - len, Fmt,
				      args);
			if (-1 == len) {
				len = DBG_BUFFER_SIZE - 1;
				buf[len] = '\0';
			}
			/*
			   len = sprintf(&DbgPrintBuffer[offset], "[%llu] ", ts);
			   len += vsprintf(&DbgPrintBuffer[offset+len], Fmt, args);
			 */

			if (len) {
				if (DbgSyslogOn) {
					printk("<6>%s", buf);
				}

				if (DbgPrintBuffer && DbgPrintOn) {
					if ((DbgPrintBufferOffset + len) >
					    DbgPrintBufferSize) {
						offset = DbgPrintBufferOffset;
						DbgPrintBufferOffset = 0;
						memset(&DbgPrintBuffer[offset],
						       0,
						       DbgPrintBufferSize -
						       offset);
					}

					mb();

					if ((DbgPrintBufferOffset + len) <
					    DbgPrintBufferSize) {
						DbgPrintBufferOffset += len;
						offset =
						    DbgPrintBufferOffset - len;
						memcpy(&DbgPrintBuffer[offset],
						       buf, len + 1);
					}
				}
			}
			kfree(buf);
		}
	}

	return (len);
}

void __cyg_profile_func_enter(void *this_fn, void *call_site)
{
	PSYMBOL_TABLE sym;
	uint64_t t64;

	if ((void *)init_novfs == this_fn) {
		DbgPrintBuffer = vmalloc(DBGBUFFERSIZE);
		taskexit_nb.notifier_call = profile_task_exit_callback;

#ifdef	CONFIG_KDB
		kdb_register("novfs_tl", profile_dump_tasklist, "",
			     "Dumps task list", 0);
		kdb_register("novfs_st", profile_dump_scopetable, "",
			     "Dumps the novfs scope table", 0);
		kdb_register("novfs_dque", profile_dump_daemonque, "",
			     "Dumps the novfs daemon que", 0);
		kdb_register("novfs_db", profile_dump_DbgBuffer,
			     "[-r] [-e size] [-i]", "Dumps the novfs DbgBuffer",
			     0);
		kdb_register("novfs_den", profile_dump_DentryTree, "[dentry]",
			     "Dumps a Dentry tree", 0);
		kdb_register("novfs_ic", Novfs_dump_inode_cache, "[inode]",
			     "Dumps a Inode Cache", 0);
		kdb_register("novfs_inode", profile_dump_inode, "",
			     "Dump allocated Inodes", 0);
		kdb_register("novfs_mem", profile_dump_memorylist_dbg, "",
			     "Dumps allocated memory", 0);
#endif
	} else if (exit_novfs == this_fn) {
		/*
		   if (dbg_file)    DbgPrint("Calling remove_proc_entry(Debug, NULL)\n"), remove_proc_entry( "Debug", dbg_dir );
		   if (inode_file)  DbgPrint("Calling remove_proc_entry(inode, NULL)\n"), remove_proc_entry( "inode", dbg_dir );
		   if (dentry_file) DbgPrint("Calling remove_proc_entry(dentry, NULL)\n"), remove_proc_entry( "dentry", dbg_dir );
		   if (memory_file) DbgPrint("Calling remove_proc_entry(memory, NULL)\n"), remove_proc_entry( "memory", dbg_dir );

		   if (dbg_dir && (dbg_dir != Novfs_Procfs_dir))
		   {
		   printk( KERN_INFO "Calling remove_proc_entry(%s, NULL)\n", MODULE_NAME);
		   remove_proc_entry( MODULE_NAME, NULL );
		   }
		 */
	}

	if (DbgProfileOn) {
		sym = SymbolTable;

		while (sym->address) {
			if (this_fn == sym->address) {
				t64 = get_nanosecond_time();
				DbgPrint("[%llu]%sS %s (0x%p 0x%p)\n", t64,
					 &IndentString[sizeof(IndentString) -
						       Indent - 1], sym->name,
					 this_fn, call_site);

				Indent++;
				if (Indent > (int)(sizeof(IndentString) - 1))
					Indent--;

				break;
			}
			sym++;
		}
	}
}

void __cyg_profile_func_exit(void *this_fn, void *call_site)
{
	PSYMBOL_TABLE sym;
	uint64_t t64;

	if (exit_novfs == this_fn) {
		if (DbgPrintBuffer)
			vfree(DbgPrintBuffer);
		DbgPrintBuffer = NULL;

#ifdef	CONFIG_KDB
		kdb_unregister("novfs_tl");
		kdb_unregister("novfs_st");
		kdb_unregister("novfs_dque");
		kdb_unregister("novfs_db");
		kdb_unregister("novfs_den");
		kdb_unregister("novfs_ic");
		kdb_unregister("novfs_inode");
		kdb_unregister("novfs_mem");
#endif
		return;
	}

	if (DbgProfileOn) {
		sym = SymbolTable;
		while (sym->address) {
			if (this_fn == sym->address) {
				Indent--;
				if (Indent < 0)
					Indent = 0;

				t64 = get_nanosecond_time();
				DbgPrint("[%llu]%sR %s (0x%p)\n", t64,
					 &IndentString[sizeof(IndentString) -
						       Indent - 1], sym->name,
					 call_site);
				break;
			}
			sym++;
		}
	}
}

void doline(unsigned char *b, unsigned char *e, unsigned char *l)
{
	unsigned char c;

	*b++ = ' ';

	while (l < e) {
		c = *l++;
		if ((c < ' ') || (c > '~')) {
			c = '.';
		}
		*b++ = c;
		*b = '\0';
	}
}

void mydump(int size, void *dumpptr)
{
	unsigned char *ptr = (unsigned char *)dumpptr;
	unsigned char *line = NULL, buf[100], *bptr = buf;
	int i;

	if (DbgPrintBuffer || DbgSyslogOn) {
		if (size) {
			for (i = 0; i < size; i++) {
				if (0 == (i % 16)) {
					if (line) {
						doline(bptr, ptr, line);
						DbgPrint("%s\n", buf);
						bptr = buf;
					}
					bptr += sprintf(bptr, "0x%p: ", ptr);
					line = ptr;
				}
				bptr += sprintf(bptr, "%02x ", *ptr++);
			}
			doline(bptr, ptr, line);
			DbgPrint("%s\n", buf);
		}
	}
}

#define FEBRUARY	2
#define	STARTOFTIME	1970
#define SECDAY		86400L
#define SECYR		(SECDAY * 365)
#define	leapyear(year)		((year) % 4 == 0)
#define	days_in_year(a) 	(leapyear(a) ? 366 : 365)
#define	days_in_month(a) 	(month_days[(a) - 1])

static int month_days[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 * This only works for the Gregorian calendar - i.e. after 1752 (in the UK)
 */
void GregorianDay(struct local_rtc_time *tm)
{
	int leapsToDate;
	int lastYear;
	int day;
	int MonthOffset[] =
	    { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

	lastYear = tm->tm_year - 1;

	/*
	 * Number of leap corrections to apply up to end of last year
	 */
	leapsToDate = lastYear / 4 - lastYear / 100 + lastYear / 400;

	/*
	 * This year is a leap year if it is divisible by 4 except when it is
	 * divisible by 100 unless it is divisible by 400
	 *
	 * e.g. 1904 was a leap year, 1900 was not, 1996 is, and 2000 will be
	 */
	if ((tm->tm_year % 4 == 0) &&
	    ((tm->tm_year % 100 != 0) || (tm->tm_year % 400 == 0)) &&
	    (tm->tm_mon > 2)) {
		/*
		 * We are past Feb. 29 in a leap year
		 */
		day = 1;
	} else {
		day = 0;
	}

	day += lastYear * 365 + leapsToDate + MonthOffset[tm->tm_mon - 1] +
	    tm->tm_mday;

	tm->tm_wday = day % 7;
}

void to_tm(int tim, struct local_rtc_time *tm)
{
	register int i;
	register long hms, day;

	day = tim / SECDAY;
	hms = tim % SECDAY;

	/* Hours, minutes, seconds are easy */
	tm->tm_hour = hms / 3600;
	tm->tm_min = (hms % 3600) / 60;
	tm->tm_sec = (hms % 3600) % 60;

	/* Number of years in days */
	for (i = STARTOFTIME; day >= days_in_year(i); i++)
		day -= days_in_year(i);
	tm->tm_year = i;

	/* Number of months in days left */
	if (leapyear(tm->tm_year))
		days_in_month(FEBRUARY) = 29;
	for (i = 1; day >= days_in_month(i); i++)
		day -= days_in_month(i);
	days_in_month(FEBRUARY) = 28;
	tm->tm_mon = i;

	/* Days are what is left over (+1) from all that. */
	tm->tm_mday = day + 1;

	/*
	 * Determine the day of week
	 */
	GregorianDay(tm);
}

char *ctime_r(time_t * clock, char *buf)
{
	struct local_rtc_time tm;
	static char *DAYOFWEEK[] =
	    { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static char *MONTHOFYEAR[] =
	    { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
"Oct", "Nov", "Dec" };

	to_tm(*clock, &tm);

	sprintf(buf, "%s %s %d %d:%02d:%02d %d", DAYOFWEEK[tm.tm_wday],
		MONTHOFYEAR[tm.tm_mon - 1], tm.tm_mday, tm.tm_hour, tm.tm_min,
		tm.tm_sec, tm.tm_year);
	return (buf);
}

#ifdef	CONFIG_KDB

int
profile_dump_tasklist(int argc, const char **argv, const char **envp,
		      struct pt_regs *regs)
{
	Scope_Dump_Tasklist();
	return (0);
}

int
profile_dump_scopetable(int argc, const char **argv, const char **envp,
			struct pt_regs *regs)
{
	Scope_Dump_Scopetable();
	return (0);
}

int
profile_dump_daemonque(int argc, const char **argv, const char **envp,
		       struct pt_regs *regs)
{
	Daemon_Dumpque();
	return (0);
}

int
profile_dump_DbgBuffer(int argc, const char **argv, const char **envp,
		       struct pt_regs *regs)
{
	unsigned long offset = DbgPrintBufferReadOffset;
	if (argc > 0) {
		if (!strcmp("-r", argv[1])) {
			DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;
		} else if (!strcmp("-e", argv[1]) && (argc > 1)) {
			offset = simple_strtoul(argv[2], NULL, 0);
			if (offset && offset < DbgPrintBufferOffset) {
				offset = DbgPrintBufferOffset - offset;
			} else {
				offset = DbgPrintBufferOffset;
			}
		} else if (!strcmp("-i", argv[1])) {
			kdb_printf("DbgPrintBuffer       =0x%p\n",
				   DbgPrintBuffer);
			kdb_printf("DbgPrintBufferOffset =0x%lx\n",
				   DbgPrintBufferOffset);
			kdb_printf("DbgPrintBufferSize   =0x%lx\n",
				   DbgPrintBufferSize);
			offset = DbgPrintBufferOffset;

		}
	}
	while (DbgPrintBufferOffset > offset) {
		kdb_printf("%c", DbgPrintBuffer[offset++]);
	}
	return (0);
}

int
profile_dump_DentryTree(int argc, const char **argv, const char **envp,
			struct pt_regs *regs)
{
	struct dentry *parent = Novfs_root;

	if (argc > 0) {
		parent = (void *)simple_strtoul(argv[1], NULL, 0);
	}

	if (parent) {
		profile_dump_dt(parent, kdb_printf);
	}

	return (0);
}

int
profile_dump_inode(int argc, const char **argv, const char **envp,
		   struct pt_regs *regs)
{
	Novfs_dump_inode(kdb_printf);
	return (0);
}

#endif /* CONFIG_KDB */

typedef struct memory_header {
	struct list_head list;
	void *caller;
	size_t size;
} MEMORY_LIST, *PMEMORY_LIST;

spinlock_t Malloc_Lock = SPIN_LOCK_UNLOCKED;
LIST_HEAD(Memory_List);

void *Novfs_Malloc(size_t size, int flags)
{
	void *p = NULL;
	PMEMORY_LIST mh;

	mh = kmalloc(size + sizeof(MEMORY_LIST), flags);
	if (mh) {
		mh->caller = __builtin_return_address(0);
		mh->size = size;
		spin_lock(&Malloc_Lock);
		list_add(&mh->list, &Memory_List);
		spin_unlock(&Malloc_Lock);
		p = (char *)mh + sizeof(MEMORY_LIST);
		/*DbgPrint("Novfs_Malloc: 0x%p 0x%p %d\n", p, mh->caller, size);
		 */
	}
	return (p);
}

void Novfs_Free(const void *p)
{
	PMEMORY_LIST mh;

	if (p) {
		/*DbgPrint("Novfs_Free: 0x%p 0x%p\n", p, __builtin_return_address(0));
		 */
		mh = (PMEMORY_LIST) ((char *)p - sizeof(MEMORY_LIST));

		spin_lock(&Malloc_Lock);
		list_del(&mh->list);
		spin_unlock(&Malloc_Lock);
		kfree(mh);
	}
}

int
profile_dump_memorylist_dbg(int argc, const char **argv, const char **envp,
			    struct pt_regs *regs)
{
#ifdef	CONFIG_KDB
	profile_dump_memorylist(kdb_printf);
#endif /* CONFIG_KDB */

	return (0);
}

void profile_dump_memorylist(void *pf)
{
	void (*pfunc) (char *Fmt, ...) = pf;

	PMEMORY_LIST mh;
	struct list_head *l;

	size_t total = 0;
	int count = 0;

	spin_lock(&Malloc_Lock);

	list_for_each(l, &Memory_List) {
		mh = list_entry(l, MEMORY_LIST, list);
		pfunc("0x%p 0x%p 0x%p %d\n", mh,
		      (char *)mh + sizeof(MEMORY_LIST), mh->caller, mh->size);
		count++;
		total += mh->size;
	}
	spin_unlock(&Malloc_Lock);

	pfunc("Blocks=%d Total=%d\n", count, total);
}

void profile_dump_dt(struct dentry *parent, void *pf)
{
	void (*pfunc) (char *Fmt, ...) = pf;
	struct l {
		struct l *next;
		struct dentry *dentry;
	} *l, *n, *start;
	struct list_head *p;
	struct dentry *d;
	char *buf, *path, *sd;
	char inode_number[16];

	buf = (char *)Novfs_Malloc(PATH_LENGTH_BUFFER, GFP_KERNEL);

	if (NULL == buf) {
		return;
	}

	if (parent) {
		pfunc("starting 0x%p %.*s\n", parent, parent->d_name.len,
		      parent->d_name.name);
		if (parent->d_subdirs.next == &parent->d_subdirs) {
			pfunc("No children...\n");
		} else {
			start = Novfs_Malloc(sizeof(*start), GFP_KERNEL);
			if (start) {
				start->next = NULL;
				start->dentry = parent;
				l = start;
				while (l) {
					p = l->dentry->d_subdirs.next;
					while (p != &l->dentry->d_subdirs) {
						d = list_entry(p, struct dentry,
							       D_CHILD);
						p = p->next;

						if (d->d_subdirs.next !=
						    &d->d_subdirs) {
							n = Novfs_Malloc(sizeof
									 (*n),
									 GFP_KERNEL);
							if (n) {
								n->next =
								    l->next;
								l->next = n;
								n->dentry = d;
							}
						} else {
							path =
							    Scope_dget_path(d,
									    buf,
									    PATH_LENGTH_BUFFER,
									    1);
							if (path) {
								pfunc
								    ("1-0x%p %s\n"
								     "   d_name:    %.*s\n"
								     "   d_parent:  0x%p\n"
								     "   d_count:   %d\n"
								     "   d_flags:   0x%x\n"
								     "   d_subdirs: 0x%p\n"
								     "   d_inode:   0x%p\n",
								     d, path,
								     d->d_name.
								     len,
								     d->d_name.
								     name,
								     d->
								     d_parent,
								     atomic_read
								     (&d->
								      d_count),
								     d->d_flags,
								     d->
								     d_subdirs.
								     next,
								     d->
								     d_inode);
							}
						}
					}
					l = l->next;
				}
				l = start;
				while (l) {
					d = l->dentry;
					path =
					    Scope_dget_path(d, buf,
							    PATH_LENGTH_BUFFER,
							    1);
					if (path) {
						sd = " (None)";
						if (&d->d_subdirs !=
						    d->d_subdirs.next) {
							sd = "";
						}
						inode_number[0] = '\0';
						if (d->d_inode) {
							sprintf(inode_number,
								" (%lu)",
								d->d_inode->
								i_ino);
						}
						pfunc("0x%p %s\n"
						      "   d_parent:  0x%p\n"
						      "   d_count:   %d\n"
						      "   d_flags:   0x%x\n"
						      "   d_subdirs: 0x%p%s\n"
						      "   d_inode:   0x%p%s\n",
						      d, path, d->d_parent,
						      atomic_read(&d->d_count),
						      d->d_flags,
						      d->d_subdirs.next, sd,
						      d->d_inode, inode_number);
					}

					n = l;
					l = l->next;
					Novfs_Free(n);
				}
			}
		}
	}

	Novfs_Free(buf);

}

/*int profile_inode_open(struct inode *inode, struct file *file)
{

}

int profile_inode_close(struct inode *inode, struct file *file)
{
}
*/
ssize_t profile_common_read(char *buf, size_t len, loff_t * off)
{
	ssize_t retval = 0;
	size_t count;
	unsigned long offset = *off;

	if (0 != (count = DbgPrintBufferOffset - offset)) {
		if (count > len) {
			count = len;
		}

		count -= copy_to_user(buf, &DbgPrintBuffer[offset], count);

		if (count == 0) {
			retval = -EFAULT;
		} else {
			*off += (loff_t) count;
			retval = count;
		}
	}
	return retval;

}

//ssize_t profile_inode_read(struct file *file, char *buf, size_t len, loff_t *off)
ssize_t profile_inode_read(struct file * file, char *buf, size_t len,
			   loff_t * off)
{
	ssize_t retval = 0;
	unsigned long offset = *off;
	static char save_DbgPrintOn;

	if (offset == 0) {
		down(&LocalPrint_lock);
		save_DbgPrintOn = DbgPrintOn;
		DbgPrintOn = 0;

		DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;
		Novfs_dump_inode(LocalPrint);
	}

	retval = profile_common_read(buf, len, off);

	if (0 == retval) {
		DbgPrintOn = save_DbgPrintOn;
		DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;

		up(&LocalPrint_lock);
	}

	return retval;

}

ssize_t profile_dentry_read(struct file * file, char *buf, size_t len,
				     loff_t * off)
{
	ssize_t retval = 0;
	unsigned long offset = *off;
	static char save_DbgPrintOn;

	if (offset == 0) {
		down(&LocalPrint_lock);
		save_DbgPrintOn = DbgPrintOn;
		DbgPrintOn = 0;
		DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;
		profile_dump_dt(Novfs_root, LocalPrint);
	}

	retval = profile_common_read(buf, len, off);

	if (0 == retval) {
		DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;
		DbgPrintOn = save_DbgPrintOn;

		up(&LocalPrint_lock);
	}

	return retval;

}

ssize_t profile_memory_read(struct file * file, char *buf, size_t len,
				     loff_t * off)
{
	ssize_t retval = 0;
	unsigned long offset = *off;
	static char save_DbgPrintOn;

	if (offset == 0) {
		down(&LocalPrint_lock);
		save_DbgPrintOn = DbgPrintOn;
		DbgPrintOn = 0;
		DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;
		profile_dump_memorylist(LocalPrint);
	}

	retval = profile_common_read(buf, len, off);

	if (0 == retval) {
		DbgPrintBufferOffset = DbgPrintBufferReadOffset = 0;
		DbgPrintOn = save_DbgPrintOn;

		up(&LocalPrint_lock);
	}

	return retval;

}

uint64_t get_nanosecond_time()
{
	struct timespec ts;
	uint64_t retVal;

	ts = current_kernel_time();

	retVal = (uint64_t) NSEC_PER_SEC;
	retVal *= (uint64_t) ts.tv_sec;
	retVal += (uint64_t) ts.tv_nsec;

	return (retVal);
}
