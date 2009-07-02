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

#define KMSG_COMPONENT "SFI"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/bootmem.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/sfi.h>

#include <asm/pgtable.h>

#include "sfi_core.h"

#define ON_SAME_PAGE(addr1, addr2) \
	(((unsigned long)(addr1) & PAGE_MASK) == \
	((unsigned long)(addr2) & PAGE_MASK))
#define TABLE_ON_PAGE(page, table, size) (ON_SAME_PAGE(page, table) && \
				ON_SAME_PAGE(page, table + size))

int sfi_disabled __read_mostly;
EXPORT_SYMBOL(sfi_disabled);

static u64 syst_pa __read_mostly;
static struct sfi_table_simple *syst_va __read_mostly;

/*
 * flag for whether using ioremap() to map the sfi tables, if yes
 * each table only need be mapped once, otherwise each arch's
 * early_ioremap  and early_iounmap should be used each time a
 * table is visited
 */
static u32 sfi_use_ioremap __read_mostly;

static void __iomem *sfi_map_memory(u64 phys, u32 size)
{
	if (!phys || !size)
		return NULL;

	if (sfi_use_ioremap)
		return ioremap(phys, size);
	else
		return early_ioremap(phys, size);
}

static void sfi_unmap_memory(void __iomem *virt, u32 size)
{
	if (!virt || !size)
		return;

	if (sfi_use_ioremap)
		iounmap(virt);
	else
		early_iounmap(virt, size);
}

static void sfi_print_table_header(unsigned long long pa,
				struct sfi_table_header *header)
{
	pr_info("%4.4s %llX, %04X (r%d %6.6s %8.8s)\n",
		header->signature, pa,
		header->length, header->revision, header->oem_id,
		header->oem_table_id);
}

/*
 * Verify table checksum  -- errors are usually fatal
 */
static int sfi_verify_checksum(struct sfi_table_header *table)
{

	u8 checksum = 0;
	u8 *puchar = (u8 *)table;
	u32 length = table->length;

	while (length--)
		checksum += *puchar++;

	if (checksum) {
		pr_err("Checksum %2.2X should be %2.2X\n",
			table->checksum, table->checksum - checksum);
		return -1;
	}
	return 0;
}

/*
 * sfi_map_table()
 *
 * Return address of mapped table
 * Check for common case that we can re-use mapping to SYST,
 * which requires syst_pa, syst_va to be initialized.
 */
static struct sfi_table_header *sfi_map_table(u64 pa)
{
	struct sfi_table_header *th;
	u32 length;

	if (!TABLE_ON_PAGE(syst_pa, pa, sizeof(struct sfi_table_header))) {
		th = sfi_map_memory(pa, sizeof(struct sfi_table_header));
	} else {
		th = (void *)syst_va - (syst_pa - pa);
	}

 	 /* If table fits on same page as its header, we are done */
	if (TABLE_ON_PAGE(th, th, th->length))
		return th;

	/* entire table does not fit on same page as SYST */
	length = th->length;

	if (!TABLE_ON_PAGE(syst_pa, pa, sizeof(struct sfi_table_header))) {
		sfi_unmap_memory(th, sizeof(struct sfi_table_header));
	}
	return (sfi_map_memory(pa, length));
}

/*
 * sfi_unmap_table()
 *
 * undoes effect of sfi_map_table() by unmapping table
 * if it did not completely fit on same page as SYST.
 */
static void sfi_unmap_table(struct sfi_table_header *th)
{
	if (!TABLE_ON_PAGE(syst_va, th, th->length))
		sfi_unmap_memory(th, th->length);
}

/*
 * sfi_get_table()
 *
 * Search SYST for the specified table.
 * return the mapped table
 */
static struct sfi_table_header *sfi_get_table(char *signature, char *oem_id,
		char *oem_table_id, unsigned int flags)
{
	struct sfi_table_header *th;
	u32 tbl_cnt, i;
	u64 pa;

	tbl_cnt = SFI_GET_NUM_ENTRIES(syst_va, u64);

	for (i = 0; i < tbl_cnt; i++) {
		pa = syst_va->pentry[i];

		th = sfi_map_table(pa);
		if (!th)
			return NULL;

		if (strncmp(th->signature, signature, SFI_SIGNATURE_SIZE))
			goto loop_continue;

		if (oem_id && strncmp(th->oem_id, oem_id, SFI_OEM_ID_SIZE))
			goto loop_continue;

		if (oem_table_id && strncmp(th->oem_table_id, oem_table_id,
						SFI_OEM_TABLE_ID_SIZE))
			goto loop_continue;

		return th;	/* success */
loop_continue:
		sfi_unmap_table(th);
	}

	return NULL;
}

void sfi_put_table(struct sfi_table_header *table)
{
	if (!ON_SAME_PAGE(((void *)table + table->length),
		(void *)syst_va + syst_va->header.length)
		&& !ON_SAME_PAGE(table, syst_va))
		sfi_unmap_memory(table, table->length);
}

/* find table with signature, run handler on it */
int sfi_table_parse(char *signature, char *oem_id, char *oem_table_id,
			unsigned int flags, sfi_table_handler handler)
{
	int ret = 0;
	struct sfi_table_header *table = NULL;

	if (sfi_disabled || !handler || !signature)
		return -EINVAL;

	table = sfi_get_table(signature, oem_id, oem_table_id, flags);
	if (!table)
		return -EINVAL;

	ret = handler(table);
	sfi_put_table(table);
	return ret;
}
EXPORT_SYMBOL_GPL(sfi_table_parse);

/*
 * sfi_check_table(pa)
 */
int __init sfi_check_table(u64 pa)
{
	struct sfi_table_header *th;
	int ret;

	th = sfi_map_table(pa);
	if (!th)
		return -1;

	sfi_print_table_header(pa, th);
	ret = sfi_verify_checksum(th);

	sfi_unmap_table(th);

	return ret;
}

/*
 * sfi_parse_syst()
 * checksum all the tables in SYST and print their headers
 *
 * success: set syst_va, return 0
 */
static int __init sfi_parse_syst(void)
{
	int tbl_cnt, i;

	syst_va = sfi_map_memory(syst_pa, sizeof(struct sfi_table_simple));
	if (!syst_va)
		return -1;

	tbl_cnt = SFI_GET_NUM_ENTRIES(syst_va, u64);
	for (i = 0; i < tbl_cnt; i++) {
		if (sfi_check_table(syst_va->pentry[i]))
			return -1;
	}

	return 0;
}

/*
 * The OS finds the System Table by searching 16-byte boundaries between
 * physical address 0x000E0000 and 0x000FFFFF. The OS shall search this region
 * starting at the low address and shall stop searching when the 1st valid SFI
 * System Table is found.
 *
 * success: set syst_pa, return 0
 * fail: return -1
 */
static __init int sfi_find_syst(void)
{
	unsigned long offset, len;
	void *start;

	len = SFI_SYST_SEARCH_END - SFI_SYST_SEARCH_BEGIN;
	start = sfi_map_memory(SFI_SYST_SEARCH_BEGIN, len);
	if (!start)
		return -1;

	for (offset = 0; offset < len; offset += 16) {
		struct sfi_table_header *syst_hdr;

		syst_hdr = start + offset;
		if (strncmp(syst_hdr->signature, SFI_SIG_SYST, SFI_SIGNATURE_SIZE))
			continue;

		if (syst_hdr->length > PAGE_SIZE)
			continue;

		sfi_print_table_header(SFI_SYST_SEARCH_BEGIN + offset, syst_hdr);

		if (sfi_verify_checksum(syst_hdr))
			continue;

		/*
 		 * Enforce SFI spec mandate that SYST reside within a page.
 		 */
		if (!ON_SAME_PAGE(syst_pa, syst_pa + syst_hdr->length)) {
			pr_debug("SYST 0x%llx + 0x%x crosses page\n", syst_pa, syst_hdr->length);
			continue;
		}

		/* success */
		syst_pa = SFI_SYST_SEARCH_BEGIN + offset;
		sfi_unmap_memory(start, len);
		return 0;
	}

	sfi_unmap_memory(start, len);
	return -1;
}

void __init sfi_init(void)
{
	if (!acpi_disabled)
		disable_sfi();

	if (sfi_disabled)
		return;

	pr_info("Simple Firmware Interface v0.7 http://simplefirmware.org\n");

	if (sfi_find_syst() || sfi_parse_syst() || sfi_platform_init())
		disable_sfi();
	return;	
}

void __init sfi_init_late(void)
{
	int length;

	if (sfi_disabled)
		return;

	length = syst_va->header.length;
	sfi_unmap_memory(syst_va, sizeof(struct sfi_table_simple));

	/* use ioremap now after it is ready */
	sfi_use_ioremap = 1;
	syst_va = sfi_map_memory(syst_pa, length);
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
