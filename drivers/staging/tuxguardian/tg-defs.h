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




#ifndef __HAS_TG_DEFS_H
#define __HAS_TG_DEFS_H


// #include <linux/tty.h>      /* For the tty declarations */

#include "tg.h"
#include "errors.h"


static u32 cur_seqno = 0;


// void print_string_to_tty(char *str);

int is_internet_socket(int family);
void print_socket_info(int family, int type);

int send_question_permit(struct socket **sock, pid_t pid, int question);
  //int send_question_permit_app(struct socket **sock, pid_t pid);
int read_answer_from_daemon(struct socket *sock, struct tg_query *answer);

static int create_socket(struct socket **sock);
static int connect_socket(struct socket **sock);
static int send_query_to_daemon (struct socket **sock, struct tg_query *query, pid_t pid);


#endif
