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




#include "tg.h"
#include <linux/ip.h>
#include <linux/version.h>

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) (((a) << 16) | ((b) << 8) | (c))
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,16)
#include <net/inet_sock.h>
#endif



int send_question_permit(struct socket **sock, pid_t pid, int question);
extern int is_internet_socket(int family);
extern void print_socket_info(int family, int type);
extern int read_answer_from_daemon(struct socket *sock, struct tg_query *answer);

//--------------------------------------------------------------------------------
static int tuxguardian_bprm_check_security (struct linux_binprm *bprm)
{
/*  binprm_security_ops is a set of program loading hook */
/*   debug_message("file %s, e_uid = %d, e_gid = %d\n", */
/* 		bprm->filename, bprm->e_uid, bprm->e_gid); */
  return 0;
}
//--------------------------------------------------------------------------------



//--------------------------------------------------------------------------------
static int tuxguardian_socket_create(int family, int type, int protocol, int kern)
{
  // TODO: i think the 'kern' parameter is used to inform if the socket
  // was created in kernel space.. not using this parameter, though

  int retval;
  struct tg_query answer;
  struct socket *sock;


  // local communication is always allowed
  // (notice that since we are creating local sockets to
  //   communicate w/ userspace this function SHOULD NOT
  //   analyse local sockets or it'll loop)

  if (! is_internet_socket(family)) {
//    printk("local communication always allowed\n");
    return 0;
  }
/*   else { */
/*     printk("\nAsking if '%s' may create a socket (", current->comm); */
/*   print_socket_info(family, type); */
/*     printk(")\n"); */
/*   } */


  retval = send_question_permit(&sock, current->pid, TG_ASK_PERMIT_APP);
  if (retval < 0)
    return -EPERM;

  // TODO: controle de timeout
  // /usr/src/linux269/fs/ncpfs/sock.c
  // /usr/src/linux269/net/bluetooth/rfcomm/sock.c
  // sock_rcvtimeo
  retval = read_answer_from_daemon(sock, &answer);
  if (retval < 0)
    return -EPERM;


  switch (answer.query_data) {
  case YES:
    return 0;
  case NO_ACCESS_IS_DENIED:
    printk(KERN_INFO "TuxGuardian: process #%d (%s) will not be allowed to access the internet"
	   " (ACCESS IS DENIED)\n", current->pid, current->comm);
    return -EPERM;
  case NO_WRONG_HASH:
    printk(KERN_INFO "TuxGuardian: process #%d (%s) will not be allowed to access the internet"
	   " (WRONG MD5HASH)\n", current->pid, current->comm);
    return -EPERM;
  case NO_NOT_IN_HASHTABLE:
    printk(KERN_INFO "TuxGuardian: process #%d (%s) will not be allowed to access the internet"
	   " (APP NOT PERMITTED)\n", current->pid, current->comm);
    return -EPERM;
  case NO_ERROR_IN_DAEMON:
    printk(KERN_INFO "TuxGuardian: process #%d (%s) will not be allowed to access the internet"
	   " (ERR IN DAEMON)\n", current->pid, current->comm);
    return -EPERM;
  case NO_USER_FORBID:
    printk(KERN_INFO "TuxGuardian: process #%d (%s) will not be allowed to access the internet"
	   " (USER FORBID)\n", current->pid, current->comm);
    return -EPERM;
  case NO_ERROR_IN_FRONTEND:
    printk(KERN_INFO "TuxGuardian: process #%d (%s) will not be allowed to access the internet"
	   " (ERR IN FRONTEND)\n", current->pid, current->comm);
    return -EPERM;
  default:
    printk(KERN_INFO "TuxGuardian: process #%d (%s) will not be allowed to access the internet"
	   " (UNKNOWN ANSWER %d)\n", current->pid, current->comm, answer.query_data);
    return -EPERM;
  }


/*   printk("resposta -> sender: %d   seqno: %d    query_type: %d    query_data:  %d\n", */
/* 	 (int)answer.sender, answer.seqno, (int)answer.query_type, answer.query_data); */

  // connection is closed by the daemon
  //  sock_release(sock);

}
//--------------------------------------------------------------------------------





//--------------------------------------------------------------------------------
static int tuxguardian_socket_connect(struct socket *conn_sock, struct sockaddr *address, int addrlen)
{
//  printk("tuxguardian_socket_connect!\n");
  return 0;

}
//--------------------------------------------------------------------------------


//--------------------------------------------------------------------------------
static int tuxguardian_socket_bind(struct socket *sock, struct sockaddr *address, int addrlen)
{
  //  printk("tuxguardian_socket_bind!\n");
  return 0;
}
//--------------------------------------------------------------------------------


//--------------------------------------------------------------------------------
static int tuxguardian_socket_listen(struct socket *listen_sock, int backlog)
{
  int retval;
  struct tg_query answer;
  struct socket *sock;




  // local communication is always allowed
  // (notice that since we are creating local sockets to
  //   communicate w/ userspace this function SHOULD NOT
  //   analyse local sockets or it'll loop)

  if (! is_internet_socket(listen_sock->sk->sk_family)) {
//    printk("local communication always allowed\n");
    return 0;
  }
/*   else { */
/*   printk("\nAsking if '%s' may be a server - ", current->comm); */
/*   print_socket_info(listen_sock->sk->sk_family, listen_sock->sk->sk_type);  */
/*   printk("\n                                   protocol %s at port %d\n", listen_sock->sk->sk_prot->name, */
/* 	 htons(((struct inet_opt *)inet_sk(listen_sock->sk))->sport)); */
/*   } */

  retval = send_question_permit(&sock, current->pid, TG_PERMIT_SERVER);
  if (retval < 0)
    return -EPERM;

  // TODO: timeout
  // /usr/src/linux269/fs/ncpfs/sock.c
  // /usr/src/linux269/net/bluetooth/rfcomm/sock.c
  // sock_rcvtimeo
  retval = read_answer_from_daemon(sock, &answer);
  if (retval < 0)
    return -EPERM;


  switch (answer.query_data) {
  case YES:
    return 0;
  case NO_ACCESS_IS_DENIED:
    printk(KERN_INFO "TuxGuardian: process #%d (%s@%d) will not be allowed to act like a server"
	   " (ACCESS IS DENIED)\n", current->pid, current->comm,
	   htons(((struct inet_sock *)inet_sk(listen_sock->sk))->sport));
    return -EPERM;
  case NO_WRONG_HASH:
    printk(KERN_INFO "TuxGuardian: process #%d (%s@%d) will not be allowed to act like a server"
	   " (WRONG MD5HASH)\n", current->pid, current->comm,
	   htons(((struct inet_sock*)inet_sk(listen_sock->sk))->sport));
    return -EPERM;
  case NO_NOT_IN_HASHTABLE:
    printk(KERN_INFO "TuxGuardian: process #%d (%s@%d) will not be allowed to act like a server"
	   " (APP NOT PERMITTED)\n", current->pid, current->comm,
	   htons(((struct inet_sock *)inet_sk(listen_sock->sk))->sport));
    return -EPERM;
  case NO_ERROR_IN_DAEMON:
    printk(KERN_INFO "TuxGuardian: process #%d (%s@%d) will not be allowed to act like a server"
	   " (ERR IN DAEMON)\n", current->pid, current->comm,
	   htons(((struct inet_sock *)inet_sk(listen_sock->sk))->sport));
    return -EPERM;
  case NO_USER_FORBID:
    printk(KERN_INFO "TuxGuardian: process #%d (%s@%d) will not be allowed to act like a server"
	   " (USER FORBID)\n", current->pid, current->comm,
	   htons(((struct inet_sock *)inet_sk(listen_sock->sk))->sport));
    return -EPERM;
  case NO_ERROR_IN_FRONTEND:
    printk(KERN_INFO "TuxGuardian: process #%d (%s@%d) will not be allowed to act like a server"
	   " (ERR IN FRONTEND)\n", current->pid, current->comm,
	   htons(((struct inet_sock *)inet_sk(listen_sock->sk))->sport));
    return -EPERM;
  default:
    printk(KERN_INFO "TuxGuardian: process #%d (%s@%d) will not be allowed to act like a server"
	   " (UNKNOWN ANSWER %d)\n", current->pid, current->comm, answer.query_data,
	   htons(((struct inet_sock *)inet_sk(listen_sock->sk))->sport));
    return -EPERM;
  }


/*   printk("resposta -> sender: %d   seqno: %d    query_type: %d    query_data:  %d\n", */
/* 	 (int)answer.sender, answer.seqno, (int)answer.query_type, answer.query_data); */

  // connection is closed by the daemon
//  sock_release(sock);
}
//--------------------------------------------------------------------------------



//--------------------------------------------------------------------------------
static int tuxguardian_socket_accept(struct socket *sock, struct socket *newsock)
{
//  printk("tuxguardian_socket_accept!\n");
  return 0;
}
//--------------------------------------------------------------------------------



//--------------------------------------------------------------------------------
static int tuxguardian_socket_sendmsg(struct socket *sock, struct msghdr *msg, int size)
{
  //  printk("tuxguardian_socket_sendmsg!\n");
  return 0;
}
//--------------------------------------------------------------------------------



//--------------------------------------------------------------------------------
static int tuxguardian_socket_recvmsg(struct socket *sock, struct msghdr *msg, int size, int flags)
{
  //  printk("tuxguardian_socket_recvmsg!\n");
  return 0;
}
//--------------------------------------------------------------------------------



//--------------------------------------------------------------------------------
static int tuxguardian_socket_getsockname(struct socket *sock)
{
//  printk("tuxguardian_socket_getsockname!\n");
  return 0;
}
//--------------------------------------------------------------------------------



//--------------------------------------------------------------------------------
static int tuxguardian_socket_getpeername(struct socket *sock)
{
//  printk("tuxguardian_socket_getpeername!\n");
  return 0;
}
//--------------------------------------------------------------------------------



//--------------------------------------------------------------------------------
static int tuxguardian_socket_setsockopt(struct socket *sock,int level,int optname)
{
//  printk("tuxguardian_socket_setsockopt!\n");
  return 0;
}
//--------------------------------------------------------------------------------



//--------------------------------------------------------------------------------
static int tuxguardian_socket_getsockopt(struct socket *sock, int level, int optname)
{
//  printk("tuxguardian_socket_getsockopt!\n");
  return 0;
}
//--------------------------------------------------------------------------------



//--------------------------------------------------------------------------------
static int tuxguardian_socket_shutdown(struct socket *sock, int how)
{
//  printk("tuxguardian_socket_shutdown!\n");
  return 0;
}
//--------------------------------------------------------------------------------



//--------------------------------------------------------------------------------
static int tuxguardian_socket_unix_stream_connect(struct socket *sock,
					      struct socket *other,
					      struct sock *newsk)
{
//  printk("tuxguardian_socket_unix_stream_connect!\n");
  return 0;
}
//--------------------------------------------------------------------------------



//--------------------------------------------------------------------------------
static int tuxguardian_socket_unix_may_send(struct socket *sock,
					struct socket *other)
{
//  printk("tuxguardian_socket_unix_stream_send!\n");
  return 0;
}
//--------------------------------------------------------------------------------










// commoncap.c exporta umas coisa dessas
static struct security_operations tuxguardian_security_ops = {
  /* Use the capability functions for some of the hooks */
  .ptrace                    =  cap_ptrace,
  .capget                    =  cap_capget,
  .capset_check              =	cap_capset_check,
  .capset_set                =  cap_capset_set,
  .capable                   =  cap_capable,

  // evil callback that keeps changing name beetwen kernel releases!
  .bprm_apply_creds          =  cap_bprm_apply_creds,

  .bprm_set_security         =	cap_bprm_set_security,

  .task_post_setuid          =	cap_task_post_setuid,
  .task_reparent_to_init     =	cap_task_reparent_to_init,

  .bprm_check_security       = tuxguardian_bprm_check_security,

  .socket_create             =	tuxguardian_socket_create,
  .socket_connect            =  tuxguardian_socket_connect,


  .socket_bind               =	tuxguardian_socket_bind,
  .socket_listen             =	tuxguardian_socket_listen,
  .socket_accept             =	tuxguardian_socket_accept,
  .socket_sendmsg            =	tuxguardian_socket_sendmsg,
  .socket_recvmsg            = 	tuxguardian_socket_recvmsg,
  .socket_getsockname        =	tuxguardian_socket_getsockname,
  .socket_getpeername        =	tuxguardian_socket_getpeername,
  .socket_getsockopt         =	tuxguardian_socket_getsockopt,
  .socket_setsockopt         =	tuxguardian_socket_setsockopt,
  .socket_shutdown           =	tuxguardian_socket_shutdown,
  .unix_stream_connect       =	tuxguardian_socket_unix_stream_connect,
  .unix_may_send             =	tuxguardian_socket_unix_may_send,



};




static int __init tuxguardian_init (void)
{

  /* register ourselves with the security framework */
  if (register_security (&tuxguardian_security_ops)) {

    printk(KERN_INFO "Failure registering TuxGuardian module with the kernel\n");

    /* try registering as the primary module */
    if (mod_reg_security (MY_NAME, &tuxguardian_security_ops)) {
      printk(KERN_INFO "Failure registering TuxGuardian as the primary security module\n");
      return -EINVAL;
    }
  }

  printk (KERN_INFO "TuxGuardian initialized\n");
  return 0;
}



static void __exit tuxguardian_exit (void)
{

  /* remove ourselves from the security framework */
  if (unregister_security (&tuxguardian_security_ops)) {
    printk (KERN_INFO "Failure unregistering TuxGuardian\n");
  }
  else
    printk (KERN_INFO "\nTuxGuardian module removed\n");


}

security_initcall (tuxguardian_init);
module_exit (tuxguardian_exit);


/* MODULE_PARAM(int_param, "i") */
/* then passing value into module as */
/* insmod module int_param=x */

/* Initialize the module - show the parameters */
/* int init_module() */
/* { */
/*   if (str1 == NULL || str2 == NULL) { */
/*     printk("Next time, do insmod param str1=<something>"); */
/*     printk("str2=<something>\n"); */
/*   } else */
/*     printk("Strings:%s and %s\n", str1, str2); */

MODULE_AUTHOR("Bruno Castro da Silva");
MODULE_DESCRIPTION("TuxGuardian Security Module");
// thanks stallman, thanks linus
MODULE_LICENSE("GPL");
