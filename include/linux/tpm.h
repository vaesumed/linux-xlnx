/*
 * Copyright (C) 2004,2007 IBM Corporation
 *
 * Authors:
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Dave Safford <safford@watson.ibm.com>
 * Reiner Sailer <sailer@watson.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 * Debora Velarde <dvelarde@us.ibm.com>
 *
 * Maintained by: <tpmdd_devel@lists.sourceforge.net>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 */
#ifndef __LINUX_TPM_H__
#define __LINUX_TPM_H__

#define PCI_DEVICE_ID_AMD_8111_LPC    0x7468

/*
 * Chip type is one of these values in the upper two bytes of chip_id
 */
enum tpm_chip_type {
	TPM_HW_TYPE = 0x0,
	TPM_SW_TYPE = 0x1,
	TPM_ANY_TYPE = 0xFFFF,
};

/*
 * Chip num is this value or a valid tpm idx in lower two bytes of chip_id
 */
enum tpm_chip_num {
	TPM_ANY_NUM = 0xFFFF,
};


#if defined(CONFIG_TCG_TPM) || defined(CONFIG_TCG_TPM_MODULE)

extern int tpm_pcr_read(u32 chip_id, int pcr_idx, u8 *res_buf);
extern int tpm_pcr_extend(u32 chip_id, int pcr_idx, const u8 *hash);
#endif
#endif
