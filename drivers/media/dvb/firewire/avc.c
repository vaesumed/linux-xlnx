/*
 * FireDTV driver (formerly known as FireSAT)
 *
 * Copyright (C) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (C) 2008 Ben Backx <ben@bbackx.com>
 * Copyright (C) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include <linux/bug.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <ieee1394.h>
#include <ieee1394_transactions.h>
#include <nodemgr.h>

#include "avc.h"
#include "firedtv.h"
#include "firedtv-rc.h"

#define FCP_COMMAND_REGISTER	0xfffff0000b00ULL

static int __avc_write(struct firedtv *fdtv,
		const struct avc_command_frame *c, struct avc_response_frame *r)
{
	int err, retry;

	if (r)
		fdtv->avc_reply_received = false;

	for (retry = 0; retry < 6; retry++) {
		err = hpsb_node_write(fdtv->ud->ne, FCP_COMMAND_REGISTER,
				      (quadlet_t *)c, c->length);
		if (err) {
			fdtv->avc_reply_received = true;
			dev_err(&fdtv->ud->device,
				"FCP command write failed\n");
			return err;
		}

		if (!r)
			return 0;

		/*
		 * AV/C specs say that answers should be sent within 150 ms.
		 * Time out after 200 ms.
		 */
		if (wait_event_timeout(fdtv->avc_wait,
				       fdtv->avc_reply_received,
				       HZ / 5) != 0) {
			memcpy(r, fdtv->respfrm, fdtv->resp_length);
			r->length = fdtv->resp_length;

			return 0;
		}
	}
	dev_err(&fdtv->ud->device, "FCP response timed out\n");
	return -ETIMEDOUT;
}

static int avc_write(struct firedtv *fdtv,
		const struct avc_command_frame *c, struct avc_response_frame *r)
{
	int ret;

	if (mutex_lock_interruptible(&fdtv->avc_mutex))
		return -EINTR;

	ret = __avc_write(fdtv, c, r);

	mutex_unlock(&fdtv->avc_mutex);
	return ret;
}

int avc_recv(struct firedtv *fdtv, u8 *data, size_t length)
{
	struct avc_response_frame *r = (struct avc_response_frame *)data;

	if (length >= 8 &&
	    r->operand[0] == SFE_VENDOR_DE_COMPANYID_0 &&
	    r->operand[1] == SFE_VENDOR_DE_COMPANYID_1 &&
	    r->operand[2] == SFE_VENDOR_DE_COMPANYID_2 &&
	    r->operand[3] == SFE_VENDOR_OPCODE_REGISTER_REMOTE_CONTROL) {
		if (r->resp == CHANGED) {
			fdtv_handle_rc(fdtv,
			    r->operand[4] << 8 | r->operand[5]);
			schedule_work(&fdtv->remote_ctrl_work);
		} else if (r->resp != INTERIM) {
			dev_info(&fdtv->ud->device,
				 "remote control result = %d\n", r->resp);
		}
		return 0;
	}

	if (fdtv->avc_reply_received) {
		dev_err(&fdtv->ud->device,
			"received out-of-order AVC response, ignored\n");
		return -EIO;
	}

	memcpy(fdtv->respfrm, data, length);
	fdtv->resp_length = length;

	fdtv->avc_reply_received = true;
	wake_up(&fdtv->avc_wait);

	return 0;
}

/*
 * tuning command for setting the relative LNB frequency
 * (not supported by the AVC standard)
 */
static void avc_tuner_tuneqpsk(struct firedtv *fdtv,
			       struct dvb_frontend_parameters *params,
			       struct avc_command_frame *c)
{
	c->opcode = VENDOR;

	c->operand[0] = SFE_VENDOR_DE_COMPANYID_0;
	c->operand[1] = SFE_VENDOR_DE_COMPANYID_1;
	c->operand[2] = SFE_VENDOR_DE_COMPANYID_2;
	c->operand[3] = SFE_VENDOR_OPCODE_TUNE_QPSK;

	c->operand[4] = (params->frequency >> 24) & 0xff;
	c->operand[5] = (params->frequency >> 16) & 0xff;
	c->operand[6] = (params->frequency >> 8) & 0xff;
	c->operand[7] = params->frequency & 0xff;

	c->operand[8] = ((params->u.qpsk.symbol_rate / 1000) >> 8) & 0xff;
	c->operand[9] = (params->u.qpsk.symbol_rate / 1000) & 0xff;

	switch(params->u.qpsk.fec_inner) {
	case FEC_1_2:	c->operand[10] = 0x1; break;
	case FEC_2_3:	c->operand[10] = 0x2; break;
	case FEC_3_4:	c->operand[10] = 0x3; break;
	case FEC_5_6:	c->operand[10] = 0x4; break;
	case FEC_7_8:	c->operand[10] = 0x5; break;
	case FEC_4_5:
	case FEC_8_9:
	case FEC_AUTO:
	default:	c->operand[10] = 0x0;
	}

	if (fdtv->voltage == 0xff)
		c->operand[11] = 0xff;
	else if (fdtv->voltage == SEC_VOLTAGE_18) /* polarisation */
		c->operand[11] = 0;
	else
		c->operand[11] = 1;

	if (fdtv->tone == 0xff)
		c->operand[12] = 0xff;
	else if (fdtv->tone == SEC_TONE_ON) /* band */
		c->operand[12] = 1;
	else
		c->operand[12] = 0;

	if (fdtv->type == FIREDTV_DVB_S2) {
		c->operand[13] = 0x1;
		c->operand[14] = 0xff;
		c->operand[15] = 0xff;
		c->length = 20;
	} else {
		c->length = 16;
	}
}

static void avc_tuner_dsd_dvb_c(struct dvb_frontend_parameters *params,
				struct avc_command_frame *c)
{
	M_VALID_FLAGS flags;

	flags.Bits.Modulation = params->u.qam.modulation != QAM_AUTO;
	flags.Bits.FEC_inner = params->u.qam.fec_inner != FEC_AUTO;
	flags.Bits.FEC_outer = 0;
	flags.Bits.Symbol_Rate = 1;
	flags.Bits.Frequency = 1;
	flags.Bits.Orbital_Pos = 0;
	flags.Bits.Polarisation = 0;
	flags.Bits.reserved_fields = 0;
	flags.Bits.reserved1 = 0;
	flags.Bits.Network_ID = 0;

	c->opcode = DSD;

	c->operand[0]  = 0;    /* source plug */
	c->operand[1]  = 0xd2; /* subfunction replace */
	c->operand[2]  = 0x20; /* system id = DVB */
	c->operand[3]  = 0x00; /* antenna number */
	/* system_specific_multiplex selection_length */
	c->operand[4]  = 0x11;
	c->operand[5]  = flags.Valid_Word.ByteHi; /* valid_flags [0] */
	c->operand[6]  = flags.Valid_Word.ByteLo; /* valid_flags [1] */
	c->operand[7]  = 0x00;
	c->operand[8]  = 0x00;
	c->operand[9]  = 0x00;
	c->operand[10] = 0x00;

	c->operand[11] = (((params->frequency / 4000) >> 16) & 0xff) | (2 << 6);
	c->operand[12] = ((params->frequency / 4000) >> 8) & 0xff;
	c->operand[13] = (params->frequency / 4000) & 0xff;
	c->operand[14] = ((params->u.qpsk.symbol_rate / 1000) >> 12) & 0xff;
	c->operand[15] = ((params->u.qpsk.symbol_rate / 1000) >> 4) & 0xff;
	c->operand[16] = ((params->u.qpsk.symbol_rate / 1000) << 4) & 0xf0;
	c->operand[17] = 0x00;

	switch (params->u.qpsk.fec_inner) {
	case FEC_1_2:	c->operand[18] = 0x1; break;
	case FEC_2_3:	c->operand[18] = 0x2; break;
	case FEC_3_4:	c->operand[18] = 0x3; break;
	case FEC_5_6:	c->operand[18] = 0x4; break;
	case FEC_7_8:	c->operand[18] = 0x5; break;
	case FEC_8_9:	c->operand[18] = 0x6; break;
	case FEC_4_5:	c->operand[18] = 0x8; break;
	case FEC_AUTO:
	default:	c->operand[18] = 0x0;
	}

	switch (params->u.qam.modulation) {
	case QAM_16:	c->operand[19] = 0x08; break;
	case QAM_32:	c->operand[19] = 0x10; break;
	case QAM_64:	c->operand[19] = 0x18; break;
	case QAM_128:	c->operand[19] = 0x20; break;
	case QAM_256:	c->operand[19] = 0x28; break;
	case QAM_AUTO:
	default:	c->operand[19] = 0x00;
	}

	c->operand[20] = 0x00;
	c->operand[21] = 0x00;
	/* Nr_of_dsd_sel_specs = 0 -> no PIDs are transmitted */
	c->operand[22] = 0x00;

	c->length = 28;
}

static void avc_tuner_dsd_dvb_t(struct dvb_frontend_parameters *params,
				struct avc_command_frame *c)
{
	M_VALID_FLAGS flags;

	flags.Bits_T.GuardInterval =
		params->u.ofdm.guard_interval != GUARD_INTERVAL_AUTO;
	flags.Bits_T.CodeRateLPStream =
		params->u.ofdm.code_rate_LP != FEC_AUTO;
	flags.Bits_T.CodeRateHPStream =
		params->u.ofdm.code_rate_HP != FEC_AUTO;
	flags.Bits_T.HierarchyInfo =
		params->u.ofdm.hierarchy_information != HIERARCHY_AUTO;
	flags.Bits_T.Constellation =
		params->u.ofdm.constellation != QAM_AUTO;
	flags.Bits_T.Bandwidth =
		params->u.ofdm.bandwidth != BANDWIDTH_AUTO;
	flags.Bits_T.CenterFrequency = 1;
	flags.Bits_T.reserved1 = 0;
	flags.Bits_T.reserved2 = 0;
	flags.Bits_T.OtherFrequencyFlag = 0;
	flags.Bits_T.TransmissionMode =
		params->u.ofdm.transmission_mode != TRANSMISSION_MODE_AUTO;
	flags.Bits_T.NetworkId = 0;

	c->opcode = DSD;

	c->operand[0]  = 0;    /* source plug */
	c->operand[1]  = 0xd2; /* subfunction replace */
	c->operand[2]  = 0x20; /* system id = DVB */
	c->operand[3]  = 0x00; /* antenna number */
	/* system_specific_multiplex selection_length */
	c->operand[4]  = 0x0c;
	c->operand[5]  = flags.Valid_Word.ByteHi; /* valid_flags [0] */
	c->operand[6]  = flags.Valid_Word.ByteLo; /* valid_flags [1] */
	c->operand[7]  = 0x0;
	c->operand[8]  = (params->frequency / 10) >> 24;
	c->operand[9]  = ((params->frequency / 10) >> 16) & 0xff;
	c->operand[10] = ((params->frequency / 10) >>  8) & 0xff;
	c->operand[11] = (params->frequency / 10) & 0xff;

	switch (params->u.ofdm.bandwidth) {
	case BANDWIDTH_7_MHZ:	c->operand[12] = 0x20; break;
	case BANDWIDTH_8_MHZ:
	case BANDWIDTH_6_MHZ:	/* not defined by AVC spec */
	case BANDWIDTH_AUTO:
	default:		c->operand[12] = 0x00;
	}

	switch (params->u.ofdm.constellation) {
	case QAM_16:	c->operand[13] = 1 << 6; break;
	case QAM_64:	c->operand[13] = 2 << 6; break;
	case QPSK:
	default:	c->operand[13] = 0x00;
	}

	switch (params->u.ofdm.hierarchy_information) {
	case HIERARCHY_1:	c->operand[13] |= 1 << 3; break;
	case HIERARCHY_2:	c->operand[13] |= 2 << 3; break;
	case HIERARCHY_4:	c->operand[13] |= 3 << 3; break;
	case HIERARCHY_AUTO:
	case HIERARCHY_NONE:
	default:		break;
	}

	switch (params->u.ofdm.code_rate_HP) {
	case FEC_2_3:	c->operand[13] |= 1; break;
	case FEC_3_4:	c->operand[13] |= 2; break;
	case FEC_5_6:	c->operand[13] |= 3; break;
	case FEC_7_8:	c->operand[13] |= 4; break;
	case FEC_1_2:
	default:	break;
	}

	switch (params->u.ofdm.code_rate_LP) {
	case FEC_2_3:	c->operand[14] = 1 << 5; break;
	case FEC_3_4:	c->operand[14] = 2 << 5; break;
	case FEC_5_6:	c->operand[14] = 3 << 5; break;
	case FEC_7_8:	c->operand[14] = 4 << 5; break;
	case FEC_1_2:
	default:	c->operand[14] = 0x00; break;
	}

	switch (params->u.ofdm.guard_interval) {
	case GUARD_INTERVAL_1_16:	c->operand[14] |= 1 << 3; break;
	case GUARD_INTERVAL_1_8:	c->operand[14] |= 2 << 3; break;
	case GUARD_INTERVAL_1_4:	c->operand[14] |= 3 << 3; break;
	case GUARD_INTERVAL_1_32:
	case GUARD_INTERVAL_AUTO:
	default:			break;
	}

	switch (params->u.ofdm.transmission_mode) {
	case TRANSMISSION_MODE_8K:	c->operand[14] |= 1 << 1; break;
	case TRANSMISSION_MODE_2K:
	case TRANSMISSION_MODE_AUTO:
	default:			break;
	}

	c->operand[15] = 0x00; /* network_ID[0] */
	c->operand[16] = 0x00; /* network_ID[1] */
	/* Nr_of_dsd_sel_specs = 0 -> no PIDs are transmitted */
	c->operand[17] = 0x00;

	c->length = 24;
}

int avc_tuner_dsd(struct firedtv *fdtv,
		  struct dvb_frontend_parameters *params)
{
	char buffer[sizeof(struct avc_command_frame)];
	struct avc_command_frame *c = (void *)buffer;
	struct avc_response_frame *r = (void *)buffer; /* FIXME: unused */

	memset(c, 0, sizeof(*c));

	c->cts   = AVC;
	c->ctype = CONTROL;
	c->sutyp = 0x5;
	c->suid  = fdtv->subunit;

	switch (fdtv->type) {
	case FIREDTV_DVB_S:
	case FIREDTV_DVB_S2: avc_tuner_tuneqpsk(fdtv, params, c); break;
	case FIREDTV_DVB_C: avc_tuner_dsd_dvb_c(params, c); break;
	case FIREDTV_DVB_T: avc_tuner_dsd_dvb_t(params, c); break;
	default:
		BUG();
	}

	if (avc_write(fdtv, c, r) < 0)
		return -EIO;

	msleep(500);
#if 0
	/* FIXME: */
	/* u8 *status was an out-parameter of avc_tuner_dsd, unused by caller */
	if (status)
		*status = r->operand[2];
#endif
	return 0;
}

int avc_tuner_set_pids(struct firedtv *fdtv, unsigned char pidc, u16 pid[])
{
	char buffer[sizeof(struct avc_command_frame)];
	struct avc_command_frame *c = (void *)buffer;
	struct avc_response_frame *r = (void *)buffer; /* FIXME: unused */
	int pos, k;

	if (pidc > 16 && pidc != 0xff)
		return -EINVAL;

	memset(c, 0, sizeof(*c));

	c->cts    = AVC;
	c->ctype  = CONTROL;
	c->sutyp  = 0x5;
	c->suid   = fdtv->subunit;
	c->opcode = DSD;

	c->operand[0] = 0;	/* source plug */
	c->operand[1] = 0xD2;	/* subfunction replace */
	c->operand[2] = 0x20;	/* system id = DVB */
	c->operand[3] = 0x00;	/* antenna number */
	c->operand[4] = 0x00;	/* system_specific_multiplex selection_length */
	c->operand[5] = pidc;	/* Nr_of_dsd_sel_specs */

	pos = 6;
	if (pidc != 0xff)
		for (k = 0; k < pidc; k++) {
			c->operand[pos++] = 0x13; /* flowfunction relay */
			c->operand[pos++] = 0x80; /* dsd_sel_spec_valid_flags -> PID */
			c->operand[pos++] = (pid[k] >> 8) & 0x1F;
			c->operand[pos++] = pid[k] & 0xFF;
			c->operand[pos++] = 0x00; /* tableID */
			c->operand[pos++] = 0x00; /* filter_length */
		}

	c->length = ALIGN(3 + pos, 4);

	if (avc_write(fdtv, c, r) < 0)
		return -EIO;

	msleep(50);
	return 0;
}

int avc_tuner_get_ts(struct firedtv *fdtv)
{
	char buffer[sizeof(struct avc_command_frame)];
	struct avc_command_frame *c = (void *)buffer;
	struct avc_response_frame *r = (void *)buffer; /* FIXME: unused */
	int sl;

	memset(c, 0, sizeof(*c));

	c->cts    = AVC;
	c->ctype  = CONTROL;
	c->sutyp  = 0x5;
	c->suid   = fdtv->subunit;
	c->opcode = DSIT;

	sl = fdtv->type == FIREDTV_DVB_T ? 0x0c : 0x11;

	c->operand[0] = 0;	/* source plug */
	c->operand[1] = 0xD2;	/* subfunction replace */
	c->operand[2] = 0xFF;	/* status */
	c->operand[3] = 0x20;	/* system id = DVB */
	c->operand[4] = 0x00;	/* antenna number */
	c->operand[5] = 0x0; 	/* system_specific_search_flags */
	c->operand[6] = sl;	/* system_specific_multiplex selection_length */
	c->operand[7] = 0x00;	/* valid_flags [0] */
	c->operand[8] = 0x00;	/* valid_flags [1] */
	c->operand[7 + sl] = 0x00; /* nr_of_dsit_sel_specs (always 0) */

	c->length = fdtv->type == FIREDTV_DVB_T ? 24 : 28;

	if (avc_write(fdtv, c, r) < 0)
		return -EIO;

	msleep(250);
	return 0;
}

int avc_identify_subunit(struct firedtv *fdtv)
{
	char buffer[sizeof(struct avc_command_frame)];
	struct avc_command_frame *c = (void *)buffer;
	struct avc_response_frame *r = (void *)buffer;

	memset(c, 0, sizeof(*c));

	c->cts    = AVC;
	c->ctype  = CONTROL;
	c->sutyp  = 0x5; /* tuner */
	c->suid   = fdtv->subunit;
	c->opcode = READ_DESCRIPTOR;

	c->operand[0] = DESCRIPTOR_SUBUNIT_IDENTIFIER;
	c->operand[1] = 0xff;
	c->operand[2] = 0x00;
	c->operand[3] = 0x00; /* length highbyte */
	c->operand[4] = 0x08; /* length lowbyte  */
	c->operand[5] = 0x00; /* offset highbyte */
	c->operand[6] = 0x0d; /* offset lowbyte  */

	c->length = 12;

	if (avc_write(fdtv, c, r) < 0)
		return -EIO;

	if ((r->resp != STABLE && r->resp != ACCEPTED) ||
	    (r->operand[3] << 8) + r->operand[4] != 8) {
		dev_err(&fdtv->ud->device,
			"cannot read subunit identifier\n");
		return -EINVAL;
	}
	return 0;
}

int avc_tuner_status(struct firedtv *fdtv,
		     ANTENNA_INPUT_INFO *antenna_input_info)
{
	char buffer[sizeof(struct avc_command_frame)];
	struct avc_command_frame *c = (void *)buffer;
	struct avc_response_frame *r = (void *)buffer;
	int length;

	memset(c, 0, sizeof(*c));

	c->cts    = AVC;
	c->ctype  = CONTROL;
	c->sutyp  = 0x05; /* tuner */
	c->suid   = fdtv->subunit;
	c->opcode = READ_DESCRIPTOR;

	c->operand[0] = DESCRIPTOR_TUNER_STATUS;
	c->operand[1] = 0xff;	/* read_result_status */
	c->operand[2] = 0x00;	/* reserver */
	c->operand[3] = 0;	/* sizeof(ANTENNA_INPUT_INFO) >> 8; */
	c->operand[4] = 0;	/* sizeof(ANTENNA_INPUT_INFO) & 0xFF; */
	c->operand[5] = 0x00;
	c->operand[6] = 0x00;

	c->length = 12;

	if (avc_write(fdtv, c, r) < 0)
		return -EIO;

	if (r->resp != STABLE && r->resp != ACCEPTED) {
		dev_err(&fdtv->ud->device, "cannot read tuner status\n");
		return -EINVAL;
	}

	length = r->operand[9];
	if (r->operand[1] != 0x10 || length != sizeof(ANTENNA_INPUT_INFO)) {
		dev_err(&fdtv->ud->device, "got invalid tuner status\n");
		return -EINVAL;
	}

	memcpy(antenna_input_info, &r->operand[10], length);
	return 0;
}

int avc_lnb_control(struct firedtv *fdtv, char voltage, char burst,
		    char conttone, char nrdiseq,
		    struct dvb_diseqc_master_cmd *diseqcmd)
{
	char buffer[sizeof(struct avc_command_frame)];
	struct avc_command_frame *c = (void *)buffer;
	struct avc_response_frame *r = (void *)buffer;
	int i, j, k;

	memset(c, 0, sizeof(*c));

	c->cts    = AVC;
	c->ctype  = CONTROL;
	c->sutyp  = 0x05;
	c->suid   = fdtv->subunit;
	c->opcode = VENDOR;

	c->operand[0] = SFE_VENDOR_DE_COMPANYID_0;
	c->operand[1] = SFE_VENDOR_DE_COMPANYID_1;
	c->operand[2] = SFE_VENDOR_DE_COMPANYID_2;
	c->operand[3] = SFE_VENDOR_OPCODE_LNB_CONTROL;

	c->operand[4] = voltage;
	c->operand[5] = nrdiseq;

	i = 6;

	for (j = 0; j < nrdiseq; j++) {
		c->operand[i++] = diseqcmd[j].msg_len;

		for (k = 0; k < diseqcmd[j].msg_len; k++)
			c->operand[i++] = diseqcmd[j].msg[k];
	}

	c->operand[i++] = burst;
	c->operand[i++] = conttone;

	c->length = ALIGN(3 + i, 4);

	if (avc_write(fdtv, c, r) < 0)
		return -EIO;

	if (r->resp != ACCEPTED) {
		dev_err(&fdtv->ud->device, "LNB control failed\n");
		return -EINVAL;
	}

	return 0;
}

int avc_register_remote_control(struct firedtv *fdtv)
{
	char buffer[sizeof(struct avc_command_frame)];
	struct avc_command_frame *c = (void *)buffer;

	memset(c, 0, sizeof(*c));

	c->cts = AVC;
	c->ctype = NOTIFY;
	c->sutyp = 0x1f;
	c->suid = 0x7;
	c->opcode = VENDOR;

	c->operand[0] = SFE_VENDOR_DE_COMPANYID_0;
	c->operand[1] = SFE_VENDOR_DE_COMPANYID_1;
	c->operand[2] = SFE_VENDOR_DE_COMPANYID_2;
	c->operand[3] = SFE_VENDOR_OPCODE_REGISTER_REMOTE_CONTROL;

	c->length = 8;

	return avc_write(fdtv, c, NULL);
}

void avc_remote_ctrl_work(struct work_struct *work)
{
	struct firedtv *fdtv =
			container_of(work, struct firedtv, remote_ctrl_work);

	/* Should it be rescheduled in failure cases? */
	avc_register_remote_control(fdtv);
}

#if 0 /* FIXME: unused */
int avc_tuner_host2ca(struct firedtv *fdtv)
{
	char buffer[sizeof(struct avc_command_frame)];
	struct avc_command_frame *c = (void *)buffer;
	struct avc_response_frame *r = (void *)buffer; /* FIXME: unused */

	memset(c, 0, sizeof(*c));

	c->cts    = AVC;
	c->ctype  = CONTROL;
	c->sutyp  = 0x5;
	c->suid   = fdtv->subunit;
	c->opcode = VENDOR;

	c->operand[0] = SFE_VENDOR_DE_COMPANYID_0;
	c->operand[1] = SFE_VENDOR_DE_COMPANYID_1;
	c->operand[2] = SFE_VENDOR_DE_COMPANYID_2;
	c->operand[3] = SFE_VENDOR_OPCODE_HOST2CA;
	c->operand[4] = 0; /* slot */
	c->operand[5] = SFE_VENDOR_TAG_CA_APPLICATION_INFO; /* ca tag */
	c->operand[6] = 0; /* more/last */
	c->operand[7] = 0; /* length */

	c->length = 12;

	if (avc_write(fdtv, c, r) < 0)
		return -EIO;

	return 0;
}
#endif

static int get_ca_object_pos(struct avc_response_frame *r)
{
	int length = 1;

	/* Check length of length field */
	if (r->operand[7] & 0x80)
		length = (r->operand[7] & 0x7f) + 1;
	return length + 7;
}

static int get_ca_object_length(struct avc_response_frame *r)
{
#if 0 /* FIXME: unused */
	int size = 0;
	int i;

	if (r->operand[7] & 0x80)
		for (i = 0; i < (r->operand[7] & 0x7f); i++) {
			size <<= 8;
			size += r->operand[8 + i];
		}
#endif
	return r->operand[7];
}

int avc_ca_app_info(struct firedtv *fdtv, char *app_info, unsigned int *len)
{
	char buffer[sizeof(struct avc_command_frame)];
	struct avc_command_frame *c = (void *)buffer;
	struct avc_response_frame *r = (void *)buffer;
	int pos;

	memset(c, 0, sizeof(*c));

	c->cts    = AVC;
	c->ctype  = STATUS;
	c->sutyp  = 0x5;
	c->suid   = fdtv->subunit;
	c->opcode = VENDOR;

	c->operand[0] = SFE_VENDOR_DE_COMPANYID_0;
	c->operand[1] = SFE_VENDOR_DE_COMPANYID_1;
	c->operand[2] = SFE_VENDOR_DE_COMPANYID_2;
	c->operand[3] = SFE_VENDOR_OPCODE_CA2HOST;
	c->operand[4] = 0; /* slot */
	c->operand[5] = SFE_VENDOR_TAG_CA_APPLICATION_INFO; /* ca tag */

	c->length = 12;

	if (avc_write(fdtv, c, r) < 0)
		return -EIO;

	/* FIXME: check response code and validate response data */

	pos = get_ca_object_pos(r);
	app_info[0] = (TAG_APP_INFO >> 16) & 0xFF;
	app_info[1] = (TAG_APP_INFO >> 8) & 0xFF;
	app_info[2] = (TAG_APP_INFO >> 0) & 0xFF;
	app_info[3] = 6 + r->operand[pos + 4];
	app_info[4] = 0x01;
	memcpy(&app_info[5], &r->operand[pos], 5 + r->operand[pos + 4]);
	*len = app_info[3] + 4;

	return 0;
}

int avc_ca_info(struct firedtv *fdtv, char *app_info, unsigned int *len)
{
	char buffer[sizeof(struct avc_command_frame)];
	struct avc_command_frame *c = (void *)buffer;
	struct avc_response_frame *r = (void *)buffer;
	int pos;

	memset(c, 0, sizeof(*c));

	c->cts    = AVC;
	c->ctype  = STATUS;
	c->sutyp  = 0x5;
	c->suid   = fdtv->subunit;
	c->opcode = VENDOR;

	c->operand[0] = SFE_VENDOR_DE_COMPANYID_0;
	c->operand[1] = SFE_VENDOR_DE_COMPANYID_1;
	c->operand[2] = SFE_VENDOR_DE_COMPANYID_2;
	c->operand[3] = SFE_VENDOR_OPCODE_CA2HOST;
	c->operand[4] = 0; /* slot */
	c->operand[5] = SFE_VENDOR_TAG_CA_APPLICATION_INFO; /* ca tag */

	c->length = 12;

	if (avc_write(fdtv, c, r) < 0)
		return -EIO;

	pos = get_ca_object_pos(r);
	app_info[0] = (TAG_CA_INFO >> 16) & 0xFF;
	app_info[1] = (TAG_CA_INFO >> 8) & 0xFF;
	app_info[2] = (TAG_CA_INFO >> 0) & 0xFF;
	app_info[3] = 2;
	app_info[4] = r->operand[pos + 0];
	app_info[5] = r->operand[pos + 1];
	*len = app_info[3] + 4;

	return 0;
}

int avc_ca_reset(struct firedtv *fdtv)
{
	char buffer[sizeof(struct avc_command_frame)];
	struct avc_command_frame *c = (void *)buffer;
	struct avc_response_frame *r = (void *)buffer; /* FIXME: unused */

	memset(c, 0, sizeof(*c));

	c->cts    = AVC;
	c->ctype  = CONTROL;
	c->sutyp  = 0x5;
	c->suid   = fdtv->subunit;
	c->opcode = VENDOR;

	c->operand[0] = SFE_VENDOR_DE_COMPANYID_0;
	c->operand[1] = SFE_VENDOR_DE_COMPANYID_1;
	c->operand[2] = SFE_VENDOR_DE_COMPANYID_2;
	c->operand[3] = SFE_VENDOR_OPCODE_HOST2CA;
	c->operand[4] = 0; /* slot */
	c->operand[5] = SFE_VENDOR_TAG_CA_RESET; /* ca tag */
	c->operand[6] = 0; /* more/last */
	c->operand[7] = 1; /* length */
	c->operand[8] = 0; /* force hardware reset */

	c->length = 12;

	if (avc_write(fdtv, c, r) < 0)
		return -EIO;

	return 0;
}

int avc_ca_pmt(struct firedtv *fdtv, char *msg, int length)
{
	char buffer[sizeof(struct avc_command_frame)];
	struct avc_command_frame *c = (void *)buffer;
	struct avc_response_frame *r = (void *)buffer;
	int list_management;
	int program_info_length;
	int pmt_cmd_id;
	int read_pos;
	int write_pos;
	int es_info_length;
	int crc32_csum;

	memset(c, 0, sizeof(*c));

	c->cts    = AVC;
	c->ctype  = CONTROL;
	c->sutyp  = 0x5;
	c->suid   = fdtv->subunit;
	c->opcode = VENDOR;

	if (msg[0] != LIST_MANAGEMENT_ONLY) {
		dev_info(&fdtv->ud->device,
			 "forcing list_management to ONLY\n");
		msg[0] = LIST_MANAGEMENT_ONLY;
	}
	/* We take the cmd_id from the programme level only! */
	list_management = msg[0];
	program_info_length = ((msg[4] & 0x0F) << 8) + msg[5];
	if (program_info_length > 0)
		program_info_length--; /* Remove pmt_cmd_id */
	pmt_cmd_id = msg[6];

	c->operand[0] = SFE_VENDOR_DE_COMPANYID_0;
	c->operand[1] = SFE_VENDOR_DE_COMPANYID_1;
	c->operand[2] = SFE_VENDOR_DE_COMPANYID_2;
	c->operand[3] = SFE_VENDOR_OPCODE_HOST2CA;
	c->operand[4] = 0; /* slot */
	c->operand[5] = SFE_VENDOR_TAG_CA_PMT; /* ca tag */
	c->operand[6] = 0; /* more/last */
	/* c->operand[7] = XXXprogram_info_length + 17; */ /* length */
	c->operand[8] = list_management;
	c->operand[9] = 0x01; /* pmt_cmd=OK_descramble */

	/* TS program map table */

	c->operand[10] = 0x02; /* Table id=2 */
	c->operand[11] = 0x80; /* Section syntax + length */
	/* c->operand[12] = XXXprogram_info_length + 12; */
	c->operand[13] = msg[1]; /* Program number */
	c->operand[14] = msg[2];
	c->operand[15] = 0x01; /* Version number=0 + current/next=1 */
	c->operand[16] = 0x00; /* Section number=0 */
	c->operand[17] = 0x00; /* Last section number=0 */
	c->operand[18] = 0x1F; /* PCR_PID=1FFF */
	c->operand[19] = 0xFF;
	c->operand[20] = (program_info_length >> 8); /* Program info length */
	c->operand[21] = (program_info_length & 0xFF);

	/* CA descriptors at programme level */
	read_pos = 6;
	write_pos = 22;
	if (program_info_length > 0) {
		pmt_cmd_id = msg[read_pos++];
		if (pmt_cmd_id != 1 && pmt_cmd_id != 4)
			dev_err(&fdtv->ud->device,
				"invalid pmt_cmd_id %d\n", pmt_cmd_id);

		memcpy(&c->operand[write_pos], &msg[read_pos],
		       program_info_length);
		read_pos += program_info_length;
		write_pos += program_info_length;
	}
	while (read_pos < length) {
		c->operand[write_pos++] = msg[read_pos++];
		c->operand[write_pos++] = msg[read_pos++];
		c->operand[write_pos++] = msg[read_pos++];
		es_info_length =
			((msg[read_pos] & 0x0F) << 8) + msg[read_pos + 1];
		read_pos += 2;
		if (es_info_length > 0)
			es_info_length--; /* Remove pmt_cmd_id */
		c->operand[write_pos++] = es_info_length >> 8;
		c->operand[write_pos++] = es_info_length & 0xFF;
		if (es_info_length > 0) {
			pmt_cmd_id = msg[read_pos++];
			if (pmt_cmd_id != 1 && pmt_cmd_id != 4)
				dev_err(&fdtv->ud->device,
					"invalid pmt_cmd_id %d "
					"at stream level\n", pmt_cmd_id);

			memcpy(&c->operand[write_pos], &msg[read_pos],
			       es_info_length);
			read_pos += es_info_length;
			write_pos += es_info_length;
		}
	}

	/* CRC */
	c->operand[write_pos++] = 0x00;
	c->operand[write_pos++] = 0x00;
	c->operand[write_pos++] = 0x00;
	c->operand[write_pos++] = 0x00;

	c->operand[7] = write_pos - 8;
	c->operand[12] = write_pos - 13;

	crc32_csum = crc32_be(0, &c->operand[10], c->operand[12] - 1);
	c->operand[write_pos - 4] = (crc32_csum >> 24) & 0xFF;
	c->operand[write_pos - 3] = (crc32_csum >> 16) & 0xFF;
	c->operand[write_pos - 2] = (crc32_csum >>  8) & 0xFF;
	c->operand[write_pos - 1] = (crc32_csum >>  0) & 0xFF;

	c->length = ALIGN(3 + write_pos, 4);

	if (avc_write(fdtv, c, r) < 0)
		return -EIO;

	if (r->resp != ACCEPTED) {
		dev_err(&fdtv->ud->device,
			"CA PMT failed with response 0x%x\n", r->resp);
		return -EFAULT;
	}

	return 0;
}

int avc_ca_get_time_date(struct firedtv *fdtv, int *interval)
{
	char buffer[sizeof(struct avc_command_frame)];
	struct avc_command_frame *c = (void *)buffer;
	struct avc_response_frame *r = (void *)buffer;

	memset(c, 0, sizeof(*c));

	c->cts    = AVC;
	c->ctype  = STATUS;
	c->sutyp  = 0x5;
	c->suid   = fdtv->subunit;
	c->opcode = VENDOR;

	c->operand[0] = SFE_VENDOR_DE_COMPANYID_0;
	c->operand[1] = SFE_VENDOR_DE_COMPANYID_1;
	c->operand[2] = SFE_VENDOR_DE_COMPANYID_2;
	c->operand[3] = SFE_VENDOR_OPCODE_CA2HOST;
	c->operand[4] = 0; /* slot */
	c->operand[5] = SFE_VENDOR_TAG_CA_DATE_TIME; /* ca tag */
	c->operand[6] = 0; /* more/last */
	c->operand[7] = 0; /* length */

	c->length = 12;

	if (avc_write(fdtv, c, r) < 0)
		return -EIO;

	/* FIXME: check response code and validate response data */

	*interval = r->operand[get_ca_object_pos(r)];

	return 0;
}

int avc_ca_enter_menu(struct firedtv *fdtv)
{
	char buffer[sizeof(struct avc_command_frame)];
	struct avc_command_frame *c = (void *)buffer;
	struct avc_response_frame *r = (void *)buffer; /* FIXME: unused */

	memset(c, 0, sizeof(*c));

	c->cts    = AVC;
	c->ctype  = STATUS;
	c->sutyp  = 0x5;
	c->suid   = fdtv->subunit;
	c->opcode = VENDOR;

	c->operand[0] = SFE_VENDOR_DE_COMPANYID_0;
	c->operand[1] = SFE_VENDOR_DE_COMPANYID_1;
	c->operand[2] = SFE_VENDOR_DE_COMPANYID_2;
	c->operand[3] = SFE_VENDOR_OPCODE_HOST2CA;
	c->operand[4] = 0; /* slot */
	c->operand[5] = SFE_VENDOR_TAG_CA_ENTER_MENU;
	c->operand[6] = 0; /* more/last */
	c->operand[7] = 0; /* length */

	c->length = 12;

	if (avc_write(fdtv, c, r) < 0)
		return -EIO;

	return 0;
}

int avc_ca_get_mmi(struct firedtv *fdtv, char *mmi_object, unsigned int *len)
{
	char buffer[sizeof(struct avc_command_frame)];
	struct avc_command_frame *c = (void *)buffer;
	struct avc_response_frame *r = (void *)buffer;

	memset(c, 0, sizeof(*c));

	c->cts    = AVC;
	c->ctype  = STATUS;
	c->sutyp  = 0x5;
	c->suid   = fdtv->subunit;
	c->opcode = VENDOR;

	c->operand[0] = SFE_VENDOR_DE_COMPANYID_0;
	c->operand[1] = SFE_VENDOR_DE_COMPANYID_1;
	c->operand[2] = SFE_VENDOR_DE_COMPANYID_2;
	c->operand[3] = SFE_VENDOR_OPCODE_CA2HOST;
	c->operand[4] = 0; /* slot */
	c->operand[5] = SFE_VENDOR_TAG_CA_MMI;
	c->operand[6] = 0; /* more/last */
	c->operand[7] = 0; /* length */

	c->length = 12;

	if (avc_write(fdtv, c, r) < 0)
		return -EIO;

	/* FIXME: check response code and validate response data */

	*len = get_ca_object_length(r);
	memcpy(mmi_object, &r->operand[get_ca_object_pos(r)], *len);

	return 0;
}

#define CMP_OUTPUT_PLUG_CONTROL_REG_0	0xfffff0000904ULL

static int cmp_read(struct firedtv *fdtv, void *buf, u64 addr, size_t len)
{
	int ret;

	if (mutex_lock_interruptible(&fdtv->avc_mutex))
		return -EINTR;

	ret = hpsb_node_read(fdtv->ud->ne, addr, buf, len);
	if (ret < 0)
		dev_err(&fdtv->ud->device, "CMP: read I/O error\n");

	mutex_unlock(&fdtv->avc_mutex);
	return ret;
}

static int cmp_lock(struct firedtv *fdtv, void *data, u64 addr, __be32 arg,
		    int ext_tcode)
{
	int ret;

	if (mutex_lock_interruptible(&fdtv->avc_mutex))
		return -EINTR;

	ret = hpsb_node_lock(fdtv->ud->ne, addr, ext_tcode, data,
			     (__force quadlet_t)arg);
	if (ret < 0)
		dev_err(&fdtv->ud->device, "CMP: lock I/O error\n");

	mutex_unlock(&fdtv->avc_mutex);
	return ret;
}

static inline u32 get_opcr(__be32 opcr, u32 mask, u32 shift)
{
	return (be32_to_cpu(opcr) >> shift) & mask;
}

static inline void set_opcr(__be32 *opcr, u32 value, u32 mask, u32 shift)
{
	*opcr &= ~cpu_to_be32(mask << shift);
	*opcr |= cpu_to_be32((value & mask) << shift);
}

#define get_opcr_online(v)		get_opcr((v), 0x1, 31)
#define get_opcr_p2p_connections(v)	get_opcr((v), 0x3f, 24)
#define get_opcr_channel(v)		get_opcr((v), 0x3f, 16)

#define set_opcr_p2p_connections(p, v)	set_opcr((p), (v), 0x3f, 24)
#define set_opcr_channel(p, v)		set_opcr((p), (v), 0x3f, 16)
#define set_opcr_data_rate(p, v)	set_opcr((p), (v), 0x3, 14)
#define set_opcr_overhead_id(p, v)	set_opcr((p), (v), 0xf, 10)

int cmp_establish_pp_connection(struct firedtv *fdtv, int plug, int channel)
{
	__be32 old_opcr, opcr;
	u64 opcr_address = CMP_OUTPUT_PLUG_CONTROL_REG_0 + (plug << 2);
	int attempts = 0;
	int ret;

	ret = cmp_read(fdtv, &opcr, opcr_address, 4);
	if (ret < 0)
		return ret;

repeat:
	if (!get_opcr_online(opcr)) {
		dev_err(&fdtv->ud->device, "CMP: output offline\n");
		return -EBUSY;
	}

	old_opcr = opcr;

	if (get_opcr_p2p_connections(opcr)) {
		if (get_opcr_channel(opcr) != channel) {
			dev_err(&fdtv->ud->device,
				"CMP: cannot change channel\n");
			return -EBUSY;
		}
		dev_info(&fdtv->ud->device,
			 "CMP: overlaying existing connection\n");

		/* We don't allocate isochronous resources. */
	} else {
		set_opcr_channel(&opcr, channel);
		set_opcr_data_rate(&opcr, IEEE1394_SPEED_400);

		/* FIXME: this is for the worst case - optimize */
		set_opcr_overhead_id(&opcr, 0);

		/* FIXME: allocate isochronous channel and bandwidth at IRM */
	}

	set_opcr_p2p_connections(&opcr, get_opcr_p2p_connections(opcr) + 1);

	ret = cmp_lock(fdtv, &opcr, opcr_address, old_opcr, 2);
	if (ret < 0)
		return ret;

	if (old_opcr != opcr) {
		/*
		 * FIXME: if old_opcr.P2P_Connections > 0,
		 * deallocate isochronous channel and bandwidth at IRM
		 */

		if (++attempts < 6) /* arbitrary limit */
			goto repeat;
		return -EBUSY;
	}

	return 0;
}

void cmp_break_pp_connection(struct firedtv *fdtv, int plug, int channel)
{
	__be32 old_opcr, opcr;
	u64 opcr_address = CMP_OUTPUT_PLUG_CONTROL_REG_0 + (plug << 2);
	int attempts = 0;

	if (cmp_read(fdtv, &opcr, opcr_address, 4) < 0)
		return;

repeat:
	if (!get_opcr_online(opcr) || !get_opcr_p2p_connections(opcr) ||
	    get_opcr_channel(opcr) != channel) {
		dev_err(&fdtv->ud->device, "CMP: no connection to break\n");
		return;
	}

	old_opcr = opcr;
	set_opcr_p2p_connections(&opcr, get_opcr_p2p_connections(opcr) - 1);

	if (cmp_lock(fdtv, &opcr, opcr_address, old_opcr, 2) < 0)
		return;

	if (old_opcr != opcr) {
		/*
		 * FIXME: if old_opcr.P2P_Connections == 1, i.e. we were last
		 * owner, deallocate isochronous channel and bandwidth at IRM
		 */

		if (++attempts < 6) /* arbitrary limit */
			goto repeat;
	}
}
