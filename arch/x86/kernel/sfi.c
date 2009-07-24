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
#include <linux/irq.h>
#include <linux/sfi.h>
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
	int	gsi_base;
	int	gsi_end;
} mp_ioapic_routing[MAX_IO_APICS];

static u32 gsi_base;
#endif

#ifdef CONFIG_X86_LOCAL_APIC
void __init mp_sfi_register_lapic_address(unsigned long address)
{
	mp_lapic_addr = address;

	set_fixmap_nocache(FIX_APIC_BASE, mp_lapic_addr);
	if (boot_cpu_physical_apicid == -1U)
		boot_cpu_physical_apicid = read_apic_id();

	pr_info("Boot CPU = %d\n", boot_cpu_physical_apicid);
}

/* All CPUs enumerated by SFI must be present and enabled */
void __cpuinit mp_sfi_register_lapic(u8 id)
{
	int boot_cpu = 0;

	if (MAX_APICS - id <= 0) {
		pr_warning("Processor #%d invalid (max %d)\n",
			id, MAX_APICS);
		return;
	}

	if (id == boot_cpu_physical_apicid)
		boot_cpu = 1;
	pr_info("registering lapic[%d]\n", id);

	generic_processor_info(id, GET_APIC_VERSION(apic_read(APIC_LVR)));
}

static int __init sfi_parse_cpus(struct sfi_table_header *table)
{
	struct sfi_table_simple *sb;
	struct sfi_cpu_table_entry *pentry;
	int i;
	int cpu_num;

	sb = (struct sfi_table_simple *)table;
	cpu_num = SFI_GET_NUM_ENTRIES(sb, struct sfi_cpu_table_entry);
	pentry = (struct sfi_cpu_table_entry *)sb->pentry;

	for (i = 0; i < cpu_num; i++) {
		mp_sfi_register_lapic(pentry->apic_id);
		pentry++;
	}

	smp_found_config = 1;
	return 0;
}
#endif /* CONFIG_X86_LOCAL_APIC */

#ifdef CONFIG_X86_IO_APIC
void __init mp_sfi_register_ioapic(u8 id, u32 paddr)
{
	int idx = 0;

	if (nr_ioapics >= MAX_IO_APICS) {
		pr_warning("WARNING: Max # of I/O APICs (%d) exceeded "
			"(found %d), skipping!\n", MAX_IO_APICS, nr_ioapics);
		return;
	}

	if (!paddr) {
		pr_warning("WARNING: Bogus (zero) I/O APIC address"
			" found in IOAPIC table, skipping!\n");
		return;
	}

	idx = nr_ioapics;
	mp_ioapics[idx].type = MP_IOAPIC;
	mp_ioapics[idx].flags = MPC_APIC_USABLE;
	mp_ioapics[idx].apicaddr = paddr;

	set_fixmap_nocache(FIX_IO_APIC_BASE_0 + idx, paddr);
	mp_ioapics[idx].apicid = unique_ioapic_id(id);
	if (mp_ioapics[idx].apicid == -1)
		return;
	mp_ioapics[idx].apicver = io_apic_get_version(idx);

	/*
	 * Build basic GSI lookup table to facilitate gsi->io_apic lookups
	 * and to prevent reprogramming of IOAPIC pins (PCI GSIs).
	 */
	mp_ioapic_routing[idx].gsi_base = gsi_base;
	mp_ioapic_routing[idx].gsi_end = gsi_base +
		io_apic_get_redir_entries(idx);
	gsi_base = mp_ioapic_routing[idx].gsi_end + 1;

	pr_info("IOAPIC[%d]: apic_id %d, version %d, address 0x%x, "
		"GSI %d-%d\n",
		idx, mp_ioapics[idx].apicid,
		mp_ioapics[idx].apicver, (u32)mp_ioapics[idx].apicaddr,
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
	num = SFI_GET_NUM_ENTRIES(sb, struct sfi_apic_table_entry);
	pentry = (struct sfi_apic_table_entry *)sb->pentry;

	for (i = 0; i < num; i++) {
		mp_sfi_register_ioapic(i, pentry->phys_addr);
		pentry++;
	}

	WARN(pic_mode, KERN_WARNING
		"SFI: pic_mod shouldn't be 1 when IOAPIC table is present\n");
	pic_mode = 0;
	return 0;
}
#endif /* CONFIG_X86_IO_APIC */

/*
 * sfi_platform_init(): register lapics & io-apics
 */
int __init sfi_platform_init(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	mp_sfi_register_lapic_address(sfi_lapic_addr);
	sfi_table_parse(SFI_SIG_CPUS, NULL, NULL, sfi_parse_cpus);
#endif
#ifdef CONFIG_X86_IO_APIC
	sfi_table_parse(SFI_SIG_APIC, NULL, NULL, sfi_parse_ioapic);
#endif
	return 0;
}
