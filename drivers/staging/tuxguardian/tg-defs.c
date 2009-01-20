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




#include "tg-defs.h"



//--------------------------------------------------------------------------------
char *err_to_str(int err)
{
  int i;
  int error = -err;

  if (error == 0)
    return "ok";

  for (i = 0; i < ARRAY_SIZE(error_to_string); i++)
    if (error_to_string[i].err == error)
      return error_to_string[i].explain;

  return "unknown error";
}
//--------------------------------------------------------------------------------



//--------------------------------------------------------------------------------
char *proto_to_str(int protonumber)
{
  int i;

  for (i = 0; i < ARRAY_SIZE(proto_to_string); i++)
    if (proto_to_string[i].protonumber == protonumber)
      return proto_to_string[i].description;

  return "unknown ip protocol";
}
//--------------------------------------------------------------------------------




//--------------------------------------------------------------------------------
/* void print_string_to_tty(char *str) */
/* { */

/*   struct tty_struct *my_tty; */
/*   my_tty = current->tty; */
/*   if (my_tty != NULL) {  */
/*     (*(my_tty->driver)->write)( */
/* 			       my_tty,                 // The tty itself */
/* 			       0,                      // We don't take the string from user space */
/* 			       str,                    // String */
/* 			       strlen(str));           // Length */
/*     (*(my_tty->driver)->write)(my_tty, 0, "\015\012", 2); */
/*   } */
/* } */
//--------------------------------------------------------------------------------




//--------------------------------------------------------------------------------
void print_socket_info(int family, int type)
{
  switch (family) {

  case PF_LOCAL:  // aka PF_UNIX (the old BSD name) and AF_UNIX/AF_LOCAL
    // sockets for local interprocess communication
    switch (type) {
    case SOCK_STREAM: {
      printk("(local communication) PF_UNIX SOCK_STREAM");
      return;
    }
    case SOCK_DGRAM: {
      printk("(local communication) PF_UNIX SOCK_DGRAM");
      return;
    }
    }

  case PF_NETLINK: {
    // netlink is used to transfer information between kernel modules and user space processes
    printk("(kernel-user communication) PF_NETLINK");
    return;
  }

  case PF_PACKET: {
    // packet interface on device level (used to receive or send raw packets at the device driver)
    printk("(device level communication) PF_PACKET");
    return;
  }

  case PF_KEY: {
    // IPSEC stuff.. A user process maintains keyring information on databases that are
    // accessed by sending messages over this socket
    printk("(keyring db communication - IPSEC) IPSECPF_KEY");
    return;
  }


  case PF_INET: {
    // IPv4 communication
    printk("(IPv4 communication) PF_INET");
    switch (type) {
    case SOCK_STREAM:
      printk(" SOCK_STREAM");
    case SOCK_DGRAM:
      printk(" SOCK_DGRAM");
    case SOCK_RAW:
      printk(" SOCK_RAW");
    }
    return;
  }


  case PF_INET6: {
    // IPv6 communication
    printk("(IPv6 communication) PF_INET6");
    switch (type) {
    case SOCK_STREAM:
      printk(" SOCK_STREAM");
    case SOCK_DGRAM:
      printk(" SOCK_DGRAM");
    case SOCK_RAW:
      printk(" SOCK_RAW");
    }
    return;
  }

  }

}
//--------------------------------------------------------------------------------




//--------------------------------------------------------------------------------
int is_internet_socket(int family)
{

  if ((family == PF_INET) || (family == PF_INET6))
    return 1;
  else
    return 0;
}
//--------------------------------------------------------------------------------





//--------------------------------------------------------------------------------
static int create_socket(struct socket **sock)
{

  int retval;
  mm_segment_t oldfs;      // to save the current userspace segment descriptor

  // this is useful for SMP (shared memory multiprocessor) architectures, but i call it anyway
  lock_kernel();

  oldfs = get_fs();
  set_fs(KERNEL_DS);

  // 3rd parameter (protocol) is set to 0 to specify the family's default
  retval = sock_create(AF_UNIX, SOCK_STREAM, 0, sock);
  set_fs(oldfs);

  unlock_kernel();

  return retval;

}
//--------------------------------------------------------------------------------






//--------------------------------------------------------------------------------
static int connect_socket(struct socket **sock)
{

  struct sockaddr_un loc;  // unix domain socket address
  int retval;
  mm_segment_t oldfs;      // to save the current userspace segment descriptor


  oldfs = get_fs();
  set_fs(KERNEL_DS);

  // if close() is called and there are queued msgs, block until msg is sent or timeout
  (*sock)->sk->sk_lingertime  = 1;
//  (*sock)->sk->sk_reuse = 1;

  loc.sun_family = AF_UNIX;
  strcpy(loc.sun_path, PATH_MODULE);
  retval = (*sock)->ops->connect(*sock, (struct sockaddr *)&loc, sizeof(loc), 0);

  set_fs(oldfs);

  return retval;

}
//--------------------------------------------------------------------------------




//--------------------------------------------------------------------------------
static int send_query_to_daemon (struct socket **sock, struct tg_query *query, pid_t pid)
{

  struct msghdr	msg;       // msg is the message that will carry our query to the daemon
  struct iovec	iov;
  mm_segment_t oldfs;      // to save the current userspace segment descriptor

  int retval;

  if ((*sock)==NULL) {
    printk(KERN_INFO "TuxGuardian: lost communication with the daemon (sock is NULL)\n");
    printk(KERN_INFO "TuxGuardian: process #%d (%s) will not be allowed to access the internet\n", pid,
	   current->comm);
    return -1;
  }
  else
    if (! ((*sock)->sk) ) {
      printk(KERN_INFO "TuxGuardian: lost communication with the daemon (sock->sk is NULL)\n");
      printk(KERN_INFO "TuxGuardian: process #%d (%s) will not be allowed to access the internet\n", pid,
	     current->comm);
      return -1;
    }


  msg.msg_name     = 0;    //  optional address
  msg.msg_namelen  = 0;

  msg.msg_iov	 = &iov;    //  information about the send (or receive) buffer.
  msg.msg_iovlen   = 1;

  msg.msg_iov->iov_base = (void *) query;     // the send buffer
  msg.msg_iov->iov_len  = (__kernel_size_t) tg_query_size;

  msg.msg_control  = NULL;   // we don't want to transmit access rights
  msg.msg_controllen = 0;

  msg.msg_flags    = MSG_NOSIGNAL;    // we don't want a SIGPIPE if the daemon close
                                      // the connection


  // any system call checks whether the provided buffer is in a valid userspace address.
  // To avoid weird error, we'll prevent the usual check to fail by making the task's
  // maximum valid address conform to kernelspace addresses.
  oldfs = get_fs();  // saves the current userspace segment descriptor
  set_fs(KERNEL_DS);  // sets to the segment descriptor associated to kernelspace

  retval = sock_sendmsg(*sock, &msg, (size_t) tg_query_size);
  if (retval == 0) {
    printk(KERN_INFO "TuxGuardian: connection reset by peer (daemon)\n");
    printk(KERN_INFO "TuxGuardian: process #%d (%s) will not be allowed to access the internet\n", pid,
	   current->comm);
    return -1;
  }
  else
    if (retval < 0) {
      printk(KERN_INFO "TuxGuardian: error %d on sending a query to the daemon\n", retval);
      printk(KERN_INFO "TuxGuardian: process #%d (%s) will not be allowed to access the internet\n", pid,
	     current->comm);
      return -1;
    }

  set_fs(oldfs);  // restores the userspace segment descriptor

  return 0;

  // this is the correct way of doing it!
  /* 	while (nbytes > 0) { */
  /* 		msg.msg_iov->iov_base = (void *) buffer + offset; */
  /* 		msg.msg_iov->iov_len = nbytes; */

  /* 		oldfs = get_fs(); */
  /* 		set_fs(KERNEL_DS); */
  /* 		len = sock_sendmsg(sock, &msg, nbytes); */
  /* 		set_fs(oldfs); */

  /* 		if (len < 0) { */
  /* 			ret = -1; */
  /* 			break; */
  /* 		} */

  /* 		nbytes -= len; */
  /* 		offset += len; */
  /* 	} */

}
//--------------------------------------------------------------------------------




//--------------------------------------------------------------------------------
int send_question_permit(struct socket **sock, pid_t pid, int question)
{

  int retval;
  struct tg_query query;   // used to ask the daemon if 'pid' is allowed to use the internet

  retval = create_socket(sock);
  if (retval != 0) {
    printk(KERN_INFO "TuxGuardian: communication with daemon failed (could not create a socket)\n");
    printk(KERN_INFO "TuxGuardian: error %d\n", retval);
    printk(KERN_INFO "TuxGuardian: process #%d (%s) will not be allowed to access the internet\n", pid,
	   current->comm);
    return -1;
  }

   retval = connect_socket(sock);
   if (retval != 0) {
     printk(KERN_INFO "TuxGuardian: could not connect to the daemon! Error %d\n", retval);
     printk(KERN_INFO "TuxGuardian: process #%d (%s) will not be allowed to access the internet\n", pid,
	    current->comm);
     return -1;
   }

   query.sender = TG_MODULE;
   query.seqno = cur_seqno++;
   query.query_type = question;
   query.query_data = pid;

   retval = send_query_to_daemon(sock, &query, pid);   // (errors are treated inside the function)
   if (retval != 0)
     return -1;   // don't continue if send_query failed

   // TODO: return query.resposta
   return retval;

}
//--------------------------------------------------------------------------------




//--------------------------------------------------------------------------------
int read_answer_from_daemon(struct socket *sock, struct tg_query *answer)
{

  struct msghdr	msg;
  struct iovec	iov;
  int retval;
  mm_segment_t oldfs;      // to save the current userspace segment descriptor
  //  struct tg_query answer;


  //  Receive a packet
  msg.msg_name = 0;
  msg.msg_namelen = 0;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = NULL;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;

  msg.msg_iov->iov_base = (void *) answer;
  msg.msg_iov->iov_len  = (__kernel_size_t) tg_query_size;

  oldfs = get_fs();
  set_fs(KERNEL_DS);


  // MSG_PEEK flag: do not remove data from the receive queue

  retval = sock_recvmsg(sock, &msg, (size_t) tg_query_size, MSG_WAITALL /* 0 flags*/);
  if (retval < 0) {
    printk(KERN_INFO "TuxGuardian: (%s) read_answer_from_app failed. Error %d (%s)\n",
	   current->comm, retval, err_to_str(retval));
  }

  set_fs(oldfs);

  return retval;

}
//--------------------------------------------------------------------------------
