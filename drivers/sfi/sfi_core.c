/* sfi_core.c Simple Firmware Interface - core internals */

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

#include <linux/bootmem.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/sfi.h>

#include <asm/pgtable.h>

#include "sfi_core.h"

#define on_same_page(addr1, addr2) \
	((addr1 & PAGE_MASK) == (addr2 & PAGE_MASK))

int sfi_disabled __read_mostly;
EXPORT_SYMBOL(sfi_disabled);

#define SFI_MAX_TABLES		64
struct sfi_internal_syst sfi_tblist;
static struct sfi_table_desc sfi_initial_tables[SFI_MAX_TABLES] __initdata;

/*
 * flag for whether using ioremap() to map the sfi tables, if yes
 * each table only need be mapped once, otherwise each arch's
 * early_ioremap  and early_iounmap should be used each time a
 * table is visited
 */
static u32 sfi_tbl_permanent_mapped __read_mostly;

static void __iomem *sfi_map_memory(u32 phys, u32 size)
{
	if (!phys || !size)
		return NULL;

	if (sfi_tbl_permanent_mapped)
		return ioremap((unsigned long)phys, size);
	else
		return arch_early_ioremap((unsigned long)phys, size);
}

static void sfi_unmap_memory(void __iomem *virt, u32 size)
{
	if (!virt || !size)
		return;

	if (sfi_tbl_permanent_mapped)
		iounmap(virt);
	else
		arch_early_iounmap(virt, size);
}

static void sfi_print_table_header(u32 address, struct sfi_table_header *header)
{
	pr_info("%4.4s %08X, %04X (r%d %6.6s %8.8s)\n",
		header->signature, address,
		header->length, header->revision, header->oem_id,
		header->oem_table_id);
}

static u8 sfi_checksum_table(void *buffer, u32 length)
{
	u8 sum = 0;
	u8 *puchar = buffer;

	while (length--)
		sum += *puchar++;
	return sum;
}

/* Verifies if the table checksums is zero */
static int sfi_tb_verify_checksum(struct sfi_table_header *table, u32 length)
{
	u8 checksum;

	checksum = sfi_checksum_table(table, length);
	if (checksum) {
		pr_warning("Incorrect checksum in table [%4.4s] -  %2.2X,"
			" should be %2.2X\n", table->signature,
			table->checksum, (u8)(table->checksum - checksum));
		return -1;
	}
	return 0;
}

 /* find the right table based on signaure, return the mapped table */
int sfi_get_table(char *signature, char *oem_id, char *oem_table_id,
		unsigned int flags, struct sfi_table_header **out_table)
{
	struct sfi_table_desc *tdesc;
	struct sfi_table_header *th;
	u32 i;

	if (!signature || !out_table)
		return -1;

	/* Walk the global SFI table list */
	for (i = 0; i < sfi_tblist.count; i++) {
		tdesc = &sfi_tblist.tables[i];
		th = &tdesc->header;

		if ((flags & SFI_ACPI_TABLE) != (tdesc->flags & SFI_ACPI_TABLE))
			continue;

		if (strncmp(th->signature, signature, SFI_SIGNATURE_SIZE))
			continue;

		if (oem_id && strncmp(th->oem_id, oem_id, SFI_OEM_ID_SIZE))
			continue;

		if (oem_table_id && strncmp(th->oem_table_id, oem_table_id,
						SFI_OEM_TABLE_ID_SIZE))
			continue;

		if (!tdesc->pointer) {
			tdesc->pointer = sfi_map_memory(tdesc->address,
							th->length);
			if (!tdesc->pointer)
				return -ENOMEM;
		}
		*out_table = tdesc->pointer;

		if (!sfi_tbl_permanent_mapped)
			tdesc->pointer = NULL;

		return 0;
	}

	return -1;
}

void sfi_put_table(struct sfi_table_header *table)
{
	if (!sfi_tbl_permanent_mapped)
		sfi_unmap_memory(table, table->length);
}

/* find table with signature, run handler on it */
int sfi_table_parse(char *signature, char *oem_id, char *oem_table_id,
			unsigned int flags, sfi_table_handler handler)
{
	int ret = 0;
	struct sfi_table_header *table = NULL;

	if (!handler)
		return -EINVAL;

	sfi_get_table(signature, oem_id, oem_table_id, flags, &table);
	if (!table)
		return -EINVAL;

	ret = handler(table);
	sfi_put_table(table);
	return ret;
}
EXPORT_SYMBOL_GPL(sfi_table_parse);

void sfi_tb_install_table(u64 addr, u32 flags)
{
	struct sfi_table_header *table;
	u32 length;
	int need_remap;

	/* only map table header before knowing actual length */
	table = sfi_map_memory(addr, sizeof(struct sfi_table_header));
	if (!table)
		return;
	length = table->length;

	/*
	 * remap the table only when the last byte of table header and
	 * table itself are not on same page
	 */
	need_remap = !on_same_page((addr + sizeof(struct sfi_table_header)),
				(addr + length));
	if (need_remap) {
		sfi_unmap_memory(table, sizeof(struct sfi_table_header));
		table = sfi_map_memory(addr, length);
		if (!table)
			return;
	}

	if (sfi_tb_verify_checksum(table, length))
		goto unmap_and_exit;

	/* Initialize sfi_tblist entry */
	sfi_tblist.tables[sfi_tblist.count].flags = flags;
	sfi_tblist.tables[sfi_tblist.count].address = addr;
	sfi_tblist.tables[sfi_tblist.count].pointer = NULL;
	memcpy(&sfi_tblist.tables[sfi_tblist.count].header,
		table, sizeof(struct sfi_table_header));

	sfi_print_table_header(addr, table);
	sfi_tblist.count++;

unmap_and_exit:
	sfi_unmap_memory(table,
			need_remap ? length : sizeof(struct sfi_table_header));
	return;
}

/*
 * Copy system table and associated table headers to internal format
 */
static int __init sfi_parse_syst(unsigned long syst_addr)
{
	struct sfi_table_simple *syst;
	struct sfi_table_header *table;
	u64 *paddr;
	u32 length, tbl_cnt;
	int i, need_remap;

	/* map and get the total length of SYST */
	syst = sfi_map_memory(syst_addr, sizeof(struct sfi_table_simple));
	if (!syst)
		return -ENOMEM;

	table = (struct sfi_table_header *)syst;
	length = table->length;
	sfi_print_table_header(syst_addr, table);

	need_remap = !on_same_page((syst_addr + length),
				(syst_addr + sizeof(struct sfi_table_simple)));
	if (need_remap) {
		sfi_unmap_memory(syst, sizeof(struct sfi_table_simple));
		syst = sfi_map_memory(syst_addr, length);
		if (!syst)
			return -ENOMEM;
	}

	/* Calculate the number of tables */
	tbl_cnt = (length - sizeof(struct sfi_table_header)) / sizeof(u64);
	paddr = (u64 *) syst->pentry;

	sfi_tblist.count = 1;
	sfi_tblist.tables[0].address = syst_addr;
	sfi_tblist.tables[0].pointer = NULL;
	memcpy(&sfi_tblist.tables[0].header,
		syst, sizeof(struct sfi_table_header));

	/* save all tables info to the global sfi_tblist structure */
	for (i = 1; i <= tbl_cnt; i++)
		sfi_tb_install_table(*paddr++, 0);

	sfi_unmap_memory(syst,
			need_remap ? length : sizeof(struct sfi_table_simple));
	return 0;
}

/*
 * The OS finds the System Table by searching 16-byte boundaries between
 * physical address 0x000E0000 and 0x000FFFFF. The OS shall search this region
 * starting at the low address and shall stop searching when the 1st valid SFI
 * System Table is found.
 */
static __init unsigned long sfi_find_syst(void)
{
	unsigned long offset, len;
	void *start;

	len = SFI_SYST_SEARCH_END - SFI_SYST_SEARCH_BEGIN;
	start = sfi_map_memory(SFI_SYST_SEARCH_BEGIN, len);
	if (!start)
		return 0;

	for (offset = 0; offset < len; offset += 16) {
		struct sfi_table_header *syst;

		syst = start + offset;
		if (strncmp(syst->signature, SFI_SIG_SYST, SFI_SIGNATURE_SIZE))
			continue;

		if (!sfi_tb_verify_checksum(syst, syst->length)) {
			sfi_unmap_memory(start, len);
			return SFI_SYST_SEARCH_BEGIN + offset;
		}
	}

	sfi_unmap_memory(start, len);
	return 0;
}

int __init sfi_table_init(void)
{
	unsigned long syst_paddr;
	int status;

	/* set up the SFI table array */
	sfi_tblist.tables = sfi_initial_tables;
	sfi_tblist.size = SFI_MAX_TABLES;

	syst_paddr = sfi_find_syst();
	if (!syst_paddr) {
		pr_warning("No system table\n");
		goto err_exit;
	}

	status = sfi_parse_syst(syst_paddr);
	if (status)
		goto err_exit;

	return 0;
err_exit:
	disable_sfi();
	return -1;
}

static void sfi_realloc_tblist(void)
{
	int size;
	struct sfi_table_desc *table;

	size = sfi_tblist.count * sizeof(struct sfi_table_desc);
	table = kzalloc(size, GFP_KERNEL);
	if (!table) {
		disable_sfi();
		return;
	}

	memcpy(table, sfi_tblist.tables,
		sfi_tblist.count * sizeof(struct sfi_table_desc));
	sfi_tblist.tables = table;
	return;
}

int __init sfi_init(void)
{
	if (!acpi_disabled) {
		disable_sfi();
		return -1;
	}

	if (sfi_disabled)
		return -1;

	pr_info("Simple Firmware Interface v0.6\n");

	if (sfi_table_init())
		return -1;

	return sfi_platform_init();
}

/* after most of the system is up, abandon the static array */
void __init sfi_init_late(void)
{
	if (sfi_disabled)
		return;
	sfi_tbl_permanent_mapped = 1;
	sfi_realloc_tblist();
}

static int __init sfi_parse_cmdline(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (!strcmp(arg, "off"))
		sfi_disabled = 1;

	return 0;
}

early_param("sfi", sfi_parse_cmdline);
