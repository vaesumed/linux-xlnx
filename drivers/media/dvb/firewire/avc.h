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

typedef struct
{
#ifdef __LITTLE_ENDIAN
	__u8       RF_frequency_hByte:6;
	__u8       raster_Frequency:2;//Bit7,6 raster frequency
#else
	__u8 raster_Frequency:2;
	__u8 RF_frequency_hByte:6;
#endif
	__u8       RF_frequency_mByte;
	__u8       RF_frequency_lByte;

}FREQUENCY;

typedef struct
{
#ifdef __LITTLE_ENDIAN
  __u8      ActiveSystem;
  __u8      reserved:5;
  __u8      NoRF:1;
  __u8      Moving:1;
  __u8      Searching:1;

  __u8      SelectedAntenna:7;
  __u8      Input:1;

  __u8      BER[4];

  __u8      SignalStrength;
  FREQUENCY Frequency;

  __u8      ManDepInfoLength;

  __u8 PowerSupply:1;
  __u8 FrontEndPowerStatus:1;
  __u8 reserved3:1;
  __u8 AntennaError:1;
  __u8 FrontEndError:1;
  __u8 reserved2:3;

  __u8 CarrierNoiseRatio[2];
  __u8 reserved4[2];
  __u8 PowerSupplyVoltage;
  __u8 AntennaVoltage;
  __u8 FirewireBusVoltage;

  __u8 CaMmi:1;
  __u8 reserved5:7;

  __u8 reserved6:1;
  __u8 CaInitializationStatus:1;
  __u8 CaErrorFlag:1;
  __u8 CaDvbFlag:1;
  __u8 CaModulePresentStatus:1;
  __u8 CaApplicationInfo:1;
  __u8 CaDateTimeRequest:1;
  __u8 CaPmtReply:1;

#else
  __u8 ActiveSystem;
  __u8 Searching:1;
  __u8 Moving:1;
  __u8 NoRF:1;
  __u8 reserved:5;

  __u8 Input:1;
  __u8 SelectedAntenna:7;

  __u8 BER[4];

  __u8 SignalStrength;
  FREQUENCY Frequency;

  __u8 ManDepInfoLength;

  __u8 reserved2:3;
  __u8 FrontEndError:1;
  __u8 AntennaError:1;
  __u8 reserved3:1;
  __u8 FrontEndPowerStatus:1;
  __u8 PowerSupply:1;

  __u8 CarrierNoiseRatio[2];
  __u8 reserved4[2];
  __u8 PowerSupplyVoltage;
  __u8 AntennaVoltage;
  __u8 FirewireBusVoltage;

  __u8 reserved5:7;
  __u8 CaMmi:1;
  __u8 CaPmtReply:1;
  __u8 CaDateTimeRequest:1;
  __u8 CaApplicationInfo:1;
  __u8 CaModulePresentStatus:1;
  __u8 CaDvbFlag:1;
  __u8 CaErrorFlag:1;
  __u8 CaInitializationStatus:1;
  __u8 reserved6:1;

#endif
} ANTENNA_INPUT_INFO; // 22 Byte

struct dvb_diseqc_master_cmd;
struct dvb_frontend_parameters;
struct firedtv;

int avc_recv(struct firedtv *fdtv, void *data, size_t length);
int avc_tuner_status(struct firedtv *fdtv,
		ANTENNA_INPUT_INFO *antenna_input_info);
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
