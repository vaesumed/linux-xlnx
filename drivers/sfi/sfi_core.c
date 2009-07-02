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

#define on_same_page(addr1, addr2) \
	(((unsigned long)(addr1) & PAGE_MASK) == \
	((unsigned long)(addr2) & PAGE_MASK))

int sfi_disabled __read_mostly;
EXPORT_SYMBOL(sfi_disabled);

static unsigned long syst_pa __read_mostly;
static struct sfi_table_simple *syst_va __read_mostly;

/*
 * flag for whether using ioremap() to map the sfi tables, if yes
 * each table only need be mapped once, otherwise each arch's
 * early_ioremap  and early_iounmap should be used each time a
 * table is visited
 */
static u32 sfi_use_ioremap __read_mostly;

static void __iomem *sfi_map_memory(unsigned long phys, u32 size)
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

static void sfi_print_table_header(unsigned long address,
				struct sfi_table_header *header)
{
	pr_info("%4.4s %08x, %04X (r%d %6.6s %8.8s)\n",
		header->signature, address,
		(u32)header->length, header->revision, header->oem_id,
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

/* check if the table can be covered by SYST's virtual memory map */
static int table_need_remap(u64 addr, u32 size)
{
	u64 start, end;

	start = syst_pa;
	end = start + syst_va->header.length;

	if (addr < start) {
		if (on_same_page(addr, start))
			return 0;
	} else if (on_same_page((addr + size), end))
		return 0;

	return 1;
}


/* Verifies if the table checksums is zero */
static int sfi_tb_verify_checksum(struct sfi_table_header *table)
{
	u8 checksum;

	checksum = sfi_checksum_table(table, table->length);
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
	struct sfi_table_header *th;
	int offset, need_remap;
	u64 *paddr;
	u32 addr, length, tbl_cnt;
	u32 i;

	/* walk through all SFI tables */
	tbl_cnt = SFI_GET_NUM_ENTRIES(syst_va, u64);
	paddr = (u64 *) syst_va->pentry;

	for (i = 0; i < tbl_cnt; i++) {
		addr = *paddr++;
		need_remap = table_need_remap(addr, sizeof(struct sfi_table_header));

		if (need_remap) {
			th = sfi_map_memory(addr, sizeof(struct sfi_table_header));
			if (!th)
				return -1;
		} else {
			offset = syst_pa - addr;
			th = (void *)syst_va - offset;
		}
		length = th->length;

		if (need_remap)
			sfi_unmap_memory(th, sizeof(struct sfi_table_header));

		need_remap = table_need_remap(addr, length);
		if (need_remap) {
			th = sfi_map_memory(addr, length);
			if (!th)
				return -1;
		}

		if (strncmp(th->signature, signature, SFI_SIGNATURE_SIZE))
			goto loop_continue;

		if (oem_id && strncmp(th->oem_id, oem_id, SFI_OEM_ID_SIZE))
			goto loop_continue;

		if (oem_table_id && strncmp(th->oem_table_id, oem_table_id,
						SFI_OEM_TABLE_ID_SIZE))
			goto loop_continue;

		*out_table = th;
		return 0;
loop_continue:
		if (need_remap)
			sfi_unmap_memory(th, length);
	}

	return -1;
}

void sfi_put_table(struct sfi_table_header *table)
{
	if (!on_same_page(((void *)table + table->length),
		(void *)syst_va + syst_va->header.length)
		&& !on_same_page(table, syst_va))
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

	sfi_get_table(signature, oem_id, oem_table_id, flags, &table);
	if (!table)
		return -EINVAL;

	ret = handler(table);
	sfi_put_table(table);
	return ret;
}
EXPORT_SYMBOL_GPL(sfi_table_parse);

/*
 * sfi_check_table(paddr)
 *
 * requires syst_pa and syst_va to be initialized
 */
int __init sfi_check_table(u64 paddr)
{
	struct sfi_table_header *th;
	unsigned long addr = (unsigned long)paddr;
	int length, need_remap, ret, offset;

	need_remap = table_need_remap(addr, sizeof(struct sfi_table_header));
	if (need_remap) {
		th = sfi_map_memory(addr, sizeof(struct sfi_table_header));
		if (!th)
			return -1;
	} else {
		offset = syst_pa - addr;
		th = (void *)syst_va - offset;
	}
	length = th->length;

	if (need_remap)
		sfi_unmap_memory(th, sizeof(struct sfi_table_header));

	need_remap = table_need_remap(addr, length);
	if (need_remap) {
		th = sfi_map_memory(addr, length);
		if (!th)
			return -1;
	}

	ret = sfi_tb_verify_checksum(th);
	if (!ret)
		sfi_print_table_header(addr, th);

	if (need_remap)
		sfi_unmap_memory(th, length);

	return ret;
}

/*
 * SFI 0.7 requires that the whole SYST s on a single page
 * TBD: so we need to enforce that here.
 */
static int __init sfi_parse_syst(unsigned long syst_addr)
{
	u64 *paddr;
	int tbl_cnt, i;

	syst_va = sfi_map_memory(syst_addr, sizeof(struct sfi_table_simple));
	if (!syst_va)
		return -ENOMEM;

	sfi_print_table_header(syst_addr, &syst_va->header);
	syst_pa = syst_addr;

	/* check all the tables in SYST */
	tbl_cnt = SFI_GET_NUM_ENTRIES(syst_va, u64);
	paddr = (u64 *) syst_va->pentry;
	for (i = 0; i < tbl_cnt; i++) {
		if (sfi_check_table(*paddr++))
			return -1;
	}

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

		if (!sfi_tb_verify_checksum(syst)) {
			sfi_unmap_memory(start, len);
			return SFI_SYST_SEARCH_BEGIN + offset;
		}
	}

	sfi_unmap_memory(start, len);
	return 0;
}

int __init sfi_table_init(void)
{
	unsigned long syst_pa;
	int status;

	syst_pa = sfi_find_syst();
	if (!syst_pa) {
		pr_warning("No system table\n");
		goto err_exit;
	}

	status = sfi_parse_syst(syst_pa);
	if (status)
		goto err_exit;

	return 0;
err_exit:
	disable_sfi();
	return -1;
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
