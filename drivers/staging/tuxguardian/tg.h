/*
    TuxGuardian is copyright 2004, Bruno Castro da Silva (brunocs@portoweb.com.br)
                                   http://tuxguardian.sourceforge.net

    This file is part of TuxGuardian.

    TuxGuardian is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    TuxGuardian is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with TuxGuardian; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
*/



#ifndef __HAS_TG_H
#define __HAS_TG_H


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/types.h>
#include <linux/ctype.h>

#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/fs.h>
#include <asm/unistd.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/in.h>
#include <linux/un.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/poll.h>
#include <linux/file.h>



#include <linux/string.h>

#include <linux/security.h>  // LSM stuff


#define PATH_MODULE "/tmp/tux_daemon_server"


#if defined(CONFIG_SECURITY_TESTE_MODULE)
#define MY_NAME THIS_MODULE->name
#else
#define MY_NAME "TuxGuardian"
#endif


// sender
#define TG_MODULE 0
#define TG_DAEMON 1
#define TG_FRONTEND 2

// query_type
#define TG_ASK_PERMIT_APP 0
#define TG_RESPOND_PERMIT_APP 1
#define TG_PERMIT_REMOVE_MODULE 2
#define TG_PERMIT_ACCESS_FILE 3
#define TG_PERMIT_SERVER 4
#define TG_RESPOND_PERMIT_SERVER 5

// RESPOND_PERMIT_APP possibilities
#define YES 0
#define YES_SAVE_IN_FILE 6
#define NO_ACCESS_IS_DENIED 7
#define NO_SAVE_IN_FILE 8
#define NO_WRONG_HASH 1
#define NO_NOT_IN_HASHTABLE 2
#define NO_ERROR_IN_DAEMON 3
#define NO_USER_FORBID 4
#define NO_ERROR_IN_FRONTEND 5

#define debug_message(fmt, arg...)                                      \
        do {                                                    \
                        printk(KERN_INFO "%s: %s: " fmt ,       \
                                MY_NAME , __FUNCTION__ ,        \
                                ## arg);                        \
        } while (0)

struct tg_query {
  u8 sender;
  u32 seqno;        // sequence number
  u8 query_type;
  u32 query_data;   // might be a pid, YES, NO,.. depending on the query_type
};

#define tg_query_size sizeof(struct tg_query)




#endif
