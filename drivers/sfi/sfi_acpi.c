/* sfi_acpi.c Simple Firmware Interface - ACPI extensions */

/*
 * Copyright (C) 2009, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#define KMSG_COMPONENT "SFI"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/sfi.h>

#include "sfi_core.h"

/*
 * SFI can access ACPI-defined tables via an optional ACPI XSDT.
 *
 * This allows re-use, and avoids re-definition, of standard tables.
 * For example, the "MCFG" table is defined by PCI, reserved by ACPI,
 * and is expected to be present many SFI-only systems.
 */

/*
 * sfi_acpi_parse_xsdt()
 *
 * Parse the ACPI XSDT for later access by sfi_acpi_table_parse().
 */
static int sfi_acpi_parse_xsdt(struct sfi_table_header *table)
{
	int tbl_cnt, i;
	struct acpi_table_xsdt *xsdt = (struct acpi_table_xsdt *)table;

	tbl_cnt = (xsdt->header.length - sizeof(struct acpi_table_header))
			/ sizeof(u64);

	for (i = 0; i < tbl_cnt; i++) {
		if (sfi_check_table(xsdt->table_offset_entry[i])) {
			disable_sfi();
			return -1;
		}
	}

	return 0;
}

int sfi_acpi_init()
{
	sfi_table_parse(SFI_SIG_XSDT, NULL, NULL, 0, sfi_acpi_parse_xsdt);
	return 0;
}

int sfi_acpi_table_parse(char *signature, char *oem_id, char *oem_table_id,
			 unsigned int flags, acpi_table_handler handler)
{
	return 0;
}
