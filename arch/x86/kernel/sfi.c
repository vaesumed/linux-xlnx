/*
 *  sfi.c - SFI Boot Support (refer acpi/boot.c)
 *
 *  Copyright (C) 2008-2009	Intel Corporation
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#define KMSG_COMPONENT "SFI"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/bootmem.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/efi.h>
#include <linux/sfi.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/io_apic.h>
#include <asm/pgtable.h>
#include <asm/mpspec.h>
#include <asm/setup.h>
#include <asm/apic.h>
#include <asm/e820.h>

#ifdef CONFIG_X86_LOCAL_APIC
static unsigned long sfi_lapic_addr __initdata = APIC_DEFAULT_PHYS_BASE;
#endif

#ifdef CONFIG_X86_IO_APIC
static struct mp_ioapic_routing {
	int	apic_id;
	int	gsi_base;
	int	gsi_end;
	u32	pin_programmed[4];
} mp_ioapic_routing[MAX_IO_APICS];
#endif

static __init struct sfi_table_simple *sfi_early_find_syst(void)
{
	unsigned long i;
	char *pchar = (char *)SFI_SYST_SEARCH_BEGIN;

	/* SFI spec defines the SYST starts at a 16-byte boundary */
	for (i = 0; SFI_SYST_SEARCH_BEGIN + i < SFI_SYST_SEARCH_END; i += 16) {
		if (!strncmp(SFI_SIG_SYST, pchar, SFI_SIGNATURE_SIZE))
			return (struct sfi_table_simple *)
					(SFI_SYST_SEARCH_BEGIN + i);

		pchar += 16;
	}
	return NULL;
}

/*
 * called in a early boot phase before the paging table is created,
 * setup a mmap table in e820 format
 */
int __init sfi_init_memory_map(void)
{
	struct sfi_table_simple *syst, *mmapt;
	struct sfi_mem_entry *mentry;
	unsigned long long start, end, size;
	int i, num, type, tbl_cnt;
	u64 *pentry;

	if (sfi_disabled)
		return -1;

	/* first search the syst table */
	syst = sfi_early_find_syst();
	if (!syst)
		return -1;

	tbl_cnt = (syst->header.length - sizeof(struct sfi_table_header)) /
			sizeof(u64);
	pentry = syst->pentry;

	/* walk through the syst to search the mmap table */
	mmapt = NULL;
	for (i = 0; i < tbl_cnt; i++) {
		if (!strncmp(SFI_SIG_MMAP, (char *)(u32)*pentry, 4)) {
			mmapt = (struct sfi_table_simple *)(u32)*pentry;
			break;
		}
		pentry++;
	}
	if (!mmapt) {
		pr_warning("could not find a valid memory map table\n");
		return -1;
	}

	/* refer copy_e820_memory() */
	num = SFI_GET_ENTRY_NUM(mmapt, sfi_mem_entry);
	mentry = (struct sfi_mem_entry *)mmapt->pentry;
	for (i = 0; i < num; i++) {
		start = mentry->phy_start;
		size = mentry->pages << PAGE_SHIFT;
		end = start + size;

		if (start > end)
			return -1;

		pr_debug("start = 0x%08x end = 0x%08x type = %d\n",
			(u32)start, (u32)end, mentry->type);

		/* translate SFI mmap type to E820 map type */
		switch (mentry->type) {
		case EFI_CONVENTIONAL_MEMORY:
			type = E820_RAM;
			break;
		case EFI_MEMORY_MAPPED_IO:
		case EFI_UNUSABLE_MEMORY:
		case EFI_RUNTIME_SERVICES_DATA:
			mentry++;
			continue;
		default:
			type = E820_RESERVED;
		}

		e820_add_region(start, size, type);
		mentry++;
	}

	return 0;
}

#ifdef CONFIG_X86_LOCAL_APIC
void __init mp_sfi_register_lapic_address(unsigned long address)
{
	mp_lapic_addr = address;
	set_fixmap_nocache(FIX_APIC_BASE, mp_lapic_addr);

	if (boot_cpu_physical_apicid == -1U)
		boot_cpu_physical_apicid = read_apic_id();

	pr_debug("Boot CPU = %d\n", boot_cpu_physical_apicid);
}

/* All CPUs enumerated by SFI must be present and enabled */
void __cpuinit mp_sfi_register_lapic(u8 id)
{
	struct mpc_cpu cpu;
	int boot_cpu = 0;

	if (MAX_APICS - id <= 0) {
		printk(KERN_WARNING "Processor #%d invalid (max %d)\n",
			id, MAX_APICS);
		return;
	}

	if (id == boot_cpu_physical_apicid)
		boot_cpu = 1;

	cpu.type = MP_PROCESSOR;
	cpu.apicid = id;
	cpu.apicver = GET_APIC_VERSION(apic_read(APIC_LVR));
	cpu.cpuflag = CPU_ENABLED;
	cpu.cpuflag |= (boot_cpu ? CPU_BOOTPROCESSOR : 0);
	cpu.cpufeature = (boot_cpu_data.x86 << 8) |
		(boot_cpu_data.x86_model << 4) | boot_cpu_data.x86_mask;
	cpu.featureflag = boot_cpu_data.x86_capability[0];
	cpu.reserved[0] = 0;
	cpu.reserved[1] = 0;

	generic_processor_info(id, cpu.apicver);
}

static int __init sfi_parse_cpus(struct sfi_table_header *table)
{
	struct sfi_table_simple *sb;
	struct sfi_cpu_table_entry *pentry;
	int i;
	int cpu_num;

	sb = (struct sfi_table_simple *)table;
	cpu_num = SFI_GET_ENTRY_NUM(sb, sfi_cpu_table_entry);
	pentry = (struct sfi_cpu_table_entry *)sb->pentry;

	for (i = 0; i < cpu_num; i++) {
		mp_sfi_register_lapic(pentry->apicid);
		pentry++;
	}

	smp_found_config = 1;
	return 0;
}
#endif /* CONFIG_X86_LOCAL_APIC */

#ifdef	CONFIG_X86_IO_APIC
/* refer acpi/boot.c */
static u8 __init uniq_ioapic_id(u8 id)
{
#ifdef CONFIG_X86_32
	if ((boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) &&
	    !APIC_XAPIC(apic_version[boot_cpu_physical_apicid]))
		return io_apic_get_unique_id(nr_ioapics, id);
	else
		return id;
#else
	int i;

	DECLARE_BITMAP(used, 256);
	bitmap_zero(used, 256);
	for (i = 0; i < nr_ioapics; i++) {
		struct mpc_ioapic *ia = &mp_ioapics[i];
		__set_bit(ia->apicid, used);
	}
	if (!test_bit(id, used))
		return id;
	return find_first_zero_bit(used, 256);
#endif
}

void __init mp_sfi_register_ioapic(u8 id, u32 paddr)
{
	int idx = 0;
	int tmpid;
	static u32 gsi_base;

	if (nr_ioapics >= MAX_IO_APICS) {
		printk(KERN_ERR "ERROR: Max # of I/O APICs (%d) exceeded "
			"(found %d)\n", MAX_IO_APICS, nr_ioapics);
		panic("Recompile kernel with bigger MAX_IO_APICS!\n");
	}
	if (!paddr) {
		printk(KERN_ERR "WARNING: Bogus (zero) I/O APIC address"
			" found in MADT table, skipping!\n");
		return;
	}

	idx = nr_ioapics;

	mp_ioapics[idx].type = MP_IOAPIC;
	mp_ioapics[idx].flags = MPC_APIC_USABLE;
	mp_ioapics[idx].apicaddr = paddr;

	set_fixmap_nocache(FIX_IO_APIC_BASE_0 + idx, paddr);
	tmpid = uniq_ioapic_id(id);
	if (tmpid == -1)
		return;

	mp_ioapics[idx].apicid = tmpid;
#ifdef CONFIG_X86_32
	mp_ioapics[idx].apicver = io_apic_get_version(idx);
#else
	mp_ioapics[idx].apicver = 0;
#endif

	pr_info("IOAPIC[%d]: apic_id %d, version %d, address 0x%x\n",
		idx, mp_ioapics[idx].apicid,
		mp_ioapics[idx].apicver, (u32)mp_ioapics[idx].apicaddr);
	/*
	 * Build basic GSI lookup table to facilitate gsi->io_apic lookups
	 * and to prevent reprogramming of IOAPIC pins (PCI GSIs).
	 */
	mp_ioapic_routing[idx].apic_id = mp_ioapics[idx].apicid;
	mp_ioapic_routing[idx].gsi_base = gsi_base;
	mp_ioapic_routing[idx].gsi_end = gsi_base +
		io_apic_get_redir_entries(idx);
	gsi_base = mp_ioapic_routing[idx].gsi_end + 1;
	pr_info("IOAPIC[%d]: apic_id %d, version %d, address 0x%x, "
		"GSI %d-%d\n", idx, mp_ioapics[idx].apicid,
		mp_ioapics[idx].apicver, (u32) mp_ioapics[idx].apicaddr,
		mp_ioapic_routing[idx].gsi_base,
		mp_ioapic_routing[idx].gsi_end);

	nr_ioapics++;
}

static int __init sfi_parse_ioapic(struct sfi_table_header *table)
{
	struct sfi_table_simple *sb;
	struct sfi_apic_table_entry *pentry;
	int i, num;

	sb = (struct sfi_table_simple *)table;
	num = SFI_GET_ENTRY_NUM(sb, sfi_apic_table_entry);
	pentry = (struct sfi_apic_table_entry *)sb->pentry;

	for (i = 0; i < num; i++) {
		mp_sfi_register_ioapic(i, pentry->phy_addr);
		pentry++;
	}

	WARN(pic_mode, KERN_WARNING
		"SFI: pic_mod shouldn't be 1 when IOAPIC table is present\n");
	pic_mode = 0;
	return 0;
}
#endif /* CONFIG_X86_IO_APIC */

static int __init sfi_parse_mcfg(struct sfi_table_header *table)
{
	pr_info("%s() successfully get called\n", __func__);
	return 0;
}

/*
 * sfi_platform_init(): register lapics & io-apics
 */
int __init sfi_platform_init(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	mp_sfi_register_lapic_address(sfi_lapic_addr);
	sfi_table_parse(SFI_SIG_CPUS, NULL, NULL, 0, sfi_parse_cpus);
#endif
#ifdef CONFIG_X86_IO_APIC
	sfi_table_parse(SFI_SIG_APIC, NULL, NULL, 0, sfi_parse_ioapic);
#endif
	sfi_table_parse("MCFG", NULL, NULL, SFI_ACPI_TABLE, sfi_parse_mcfg);
	return 0;
}
