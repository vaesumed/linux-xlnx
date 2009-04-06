/*
 *    Enables/disables PCIe ECRC checking.
 *
 *    (C) Copyright 2009 Hewlett-Packard Development Company, L.P.
 *    Andrew Patterson <andrew.patterson@hp.com>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; version 2 of the License.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *    General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *    02111-1307, USA.
 *
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/errno.h>
#include "../pci.h"

#define ECRC_POLICY_DEFAULT 0		/* ECRC set by BIOS */
#define ECRC_POLICY_OFF     1		/* ECRC off */
#define ECRC_POLICY_ON      2		/* ECRC on */

static int ecrc_policy = ECRC_POLICY_DEFAULT;

static const char *ecrc_policy_str[] = {
	[ECRC_POLICY_DEFAULT] = "bios",
	[ECRC_POLICY_OFF] = "off",
	[ECRC_POLICY_ON] = "on"
};

/**
 * pcie_set_ercr_checking - enable/disable PCIe ECRC checking
 * @dev: the PCI device
 *
 * Returns 0 on success, or negative on failure.
 */
int pcie_set_ecrc_checking(struct pci_dev *dev)
{
	int pos;
	u32 reg32;

	if (!dev->is_pcie)
		return -ENODEV;

	/* Use firmware/BIOS setting if default */
	if (ecrc_policy == ECRC_POLICY_DEFAULT)
		return 0;

	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	if (!pos)
		return -ENODEV;

	pci_read_config_dword(dev, pos + PCI_ERR_CAP, &reg32);
	if (ecrc_policy == ECRC_POLICY_OFF) {
		reg32 &= ~(PCI_ERR_CAP_ECRC_GENE | PCI_ERR_CAP_ECRC_CHKE);
	} else if (ecrc_policy == ECRC_POLICY_ON) {
		if (reg32 & PCI_ERR_CAP_ECRC_GENC)
			reg32 |= PCI_ERR_CAP_ECRC_GENE;
		if (reg32 & PCI_ERR_CAP_ECRC_CHKC)
			reg32 |= PCI_ERR_CAP_ECRC_CHKE;
	}
	pci_write_config_dword(dev, pos + PCI_ERR_CAP, reg32);
	return 0;
}

void pcie_ecrc_get_policy(char *str)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ecrc_policy_str); i++)
		if (!strncmp(str, ecrc_policy_str[i],
			     strlen(ecrc_policy_str[i])))
			break;
	if (i >= ARRAY_SIZE(ecrc_policy_str))
		return;

	ecrc_policy = i;
}
