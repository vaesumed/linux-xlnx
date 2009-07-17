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
#include <acpi/acpi.h>

#include <linux/sfi.h>
#include "sfi_core.h"

/*
 * SFI can access ACPI-defined tables via an optional ACPI XSDT.
 *
 * This allows re-use, and avoids re-definition, of standard tables.
 * For example, the "MCFG" table is defined by PCI, reserved by ACPI,
 * and is expected to be present many SFI-only systems.
 */

static struct acpi_table_xsdt *xsdt_va __read_mostly;

#define XSDT_GET_NUM_ENTRIES(ptable, entry_type) \
	((ptable->header.length - sizeof(struct acpi_table_header)) / \
	(sizeof(entry_type)))

static inline struct sfi_table_header *acpi_to_sfi_th(
				struct acpi_table_header *th)
{
	return (struct sfi_table_header *)th;
}

static inline struct acpi_table_header *sfi_to_acpi_th(
				struct sfi_table_header *th)
{
	return (struct acpi_table_header *)th;
}

/*
 * sfi_acpi_parse_xsdt()
 *
 * Parse the ACPI XSDT for later access by sfi_acpi_table_parse().
 */
static int __init sfi_acpi_parse_xsdt(struct sfi_table_header *th)
{
	struct sfi_table_key key = SFI_ANY_KEY;
	int tbl_cnt, i;
	void *ret;

	xsdt_va = (struct acpi_table_xsdt *)th;
	tbl_cnt = XSDT_GET_NUM_ENTRIES(xsdt_va, u64);
	for (i = 0; i < tbl_cnt; i++) {
		ret = sfi_check_table(xsdt_va->table_offset_entry[i], &key);
		if (IS_ERR(ret)) {
			disable_sfi();
			return -1;
		}
	}

	return 0;
}

int __init sfi_acpi_init(void)
{
	sfi_table_parse(SFI_SIG_XSDT, NULL, NULL, sfi_acpi_parse_xsdt);
	return 0;
}

static struct acpi_table_header *sfi_acpi_get_table(struct sfi_table_key *key)
{
	u32 tbl_cnt, i;
	void *ret;

	tbl_cnt = XSDT_GET_NUM_ENTRIES(xsdt_va, u64);
	for (i = 0; i < tbl_cnt; i++) {
		ret = sfi_check_table(xsdt_va->table_offset_entry[i], key);
		if (!IS_ERR(ret) && ret)
			return sfi_to_acpi_th(ret);
	}

	return NULL;
}

static void sfi_acpi_put_table(struct acpi_table_header *table)
{
	sfi_put_table(acpi_to_sfi_th(table));
}

/*
 * sfi_acpi_table_parse()
 *
 * Find specified table in XSDT, run handler on it and return its return value
 */
int sfi_acpi_table_parse(char *signature, char *oem_id, char *oem_table_id,
			int(*handler)(struct acpi_table_header *))
{
	struct acpi_table_header *table = NULL;
	struct sfi_table_key key;
	int ret = 0;

	if (sfi_disabled)
		return -1;

	key.sig = signature;
	key.oem_id = oem_id;
	key.oem_table_id = oem_table_id;

	table = sfi_acpi_get_table(&key);
	if (!table)
		return -EINVAL;

	ret = handler(table);
	sfi_acpi_put_table(table);
	return ret;
}
