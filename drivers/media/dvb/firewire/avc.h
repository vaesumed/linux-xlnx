/*
 * AV/C API
 *
 * Copyright (C) 2000 Manfred Weihs
 * Copyright (C) 2003 Philipp Gutgsell <0014guph@edu.fh-kaernten.ac.at>
 * Copyright (C) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (C) 2008 Ben Backx <ben@bbackx.com>
 * Copyright (C) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 * This is based on code written by Peter Halwachs, Thomas Groiss and
 * Andreas Monitzer.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#ifndef _AVC_API_H
#define _AVC_API_H

#include <linux/types.h>

struct dvb_diseqc_master_cmd;
struct dvb_frontend_parameters;
struct firedtv_tuner_status;
struct firedtv;

int avc_recv(struct firedtv *fdtv, void *data, size_t length);
int avc_tuner_status(struct firedtv *fdtv, struct firedtv_tuner_status *stat);
int avc_tuner_dsd(struct firedtv *fdtv,
		struct dvb_frontend_parameters *params);
int avc_tuner_set_pids(struct firedtv *fdtv, unsigned char pidc, u16 pid[]);
int avc_tuner_get_ts(struct firedtv *fdtv);
int avc_identify_subunit(struct firedtv *fdtv);
int avc_lnb_control(struct firedtv *fdtv, char voltage, char burst,
		char conttone, char nrdiseq,
		struct dvb_diseqc_master_cmd *diseqcmd);
void avc_remote_ctrl_work(struct work_struct *work);
int avc_register_remote_control(struct firedtv *fdtv);
int avc_ca_app_info(struct firedtv *fdtv, char *app_info, unsigned int *len);
int avc_ca_info(struct firedtv *fdtv, char *app_info, unsigned int *len);
int avc_ca_reset(struct firedtv *fdtv);
int avc_ca_pmt(struct firedtv *fdtv, char *app_info, int length);
int avc_ca_get_time_date(struct firedtv *fdtv, int *interval);
int avc_ca_enter_menu(struct firedtv *fdtv);
int avc_ca_get_mmi(struct firedtv *fdtv, char *mmi_object, unsigned int *len);

int cmp_establish_pp_connection(struct firedtv *fdtv, int plug, int channel);
void cmp_break_pp_connection(struct firedtv *fdtv, int plug, int channel);

#endif /* _AVC_API_H */
