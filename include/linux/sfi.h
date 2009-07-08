/* sfi.h Simple Firmware Interface */

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

#ifndef _LINUX_SFI_H
#define _LINUX_SFI_H

/* Table signatures reserved by the SFI specification */
#define SFI_SIG_SYST		"SYST"
#define SFI_SIG_FREQ		"FREQ"
#define SFI_SIG_IDLE		"IDLE"
#define SFI_SIG_CPUS		"CPUS"
#define SFI_SIG_MTMR		"MTMR"
#define SFI_SIG_MRTC		"MRTC"
#define SFI_SIG_MMAP		"MMAP"
#define SFI_SIG_APIC		"APIC"
#define SFI_SIG_XSDT		"XSDT"
#define SFI_SIG_WAKE		"WAKE"
#define SFI_SIG_SPIB		"SPIB"
#define SFI_SIG_I2CB		"I2CB"
#define SFI_SIG_GPEM		"GPEM"

#define SFI_ACPI_TABLE		(1 << 0)
#define SFI_NORMAL_TABLE	(1 << 1)

#define SFI_SIGNATURE_SIZE	4
#define SFI_OEM_ID_SIZE		6
#define SFI_OEM_TABLE_ID_SIZE	8

#define SFI_SYST_SEARCH_BEGIN		0x000E0000
#define SFI_SYST_SEARCH_END		0x000FFFFF

#define SFI_GET_NUM_ENTRIES(ptable, entry_type) \
	((ptable->header.length - sizeof(struct sfi_table_header)) / \
	(sizeof(entry_type)))
/*
 * Table structures must be byte-packed to match the SFI specification,
 * as they are provided by the BIOS.
 */
struct sfi_table_header {
	char	signature[SFI_SIGNATURE_SIZE];
	u32	length;
	u8	revision;
	u8	checksum;
	char	oem_id[SFI_OEM_ID_SIZE];
	char	oem_table_id[SFI_OEM_TABLE_ID_SIZE];
} __packed;

struct sfi_table_simple {
	struct sfi_table_header		header;
	u64				pentry[1];
} __packed;

/* comply with UEFI spec 2.1 */
struct sfi_mem_entry {
	u32	type;
	u64	phy_start;
	u64	vir_start;
	u64	pages;
	u64	attrib;
} __packed;

struct sfi_cpu_table_entry {
	u32	apicid;
} __packed;

struct sfi_cstate_table_entry {
	u32	hint;		/* MWAIT hint */
	u32	latency;	/* latency in ms */
} __packed;

struct sfi_apic_table_entry {
	u64	phy_addr;	/* phy base addr for APIC reg */
} __packed;

struct sfi_freq_table_entry {
	u32	freq;
	u32	latency;	/* transition latency in ms */
	u32	ctrl_val;	/* value to write to PERF_CTL */
} __packed;

struct sfi_wake_table_entry {
	u64 phy_addr;	/* pointer to where the wake vector locates */
} __packed;

struct sfi_timer_table_entry {
	u64	phy_addr;	/* phy base addr for the timer */
	u32	freq;		/* in HZ */
	u32	irq;
} __packed;

struct sfi_rtc_table_entry {
	u64	phy_addr;	/* phy base addr for the RTC */
	u32	irq;
} __packed;

struct sfi_spi_table_entry {
	u16	host_num;	/* attached to host 0, 1...*/
	u16	cs;		/* chip select */
	u16	irq_info;
	char	name[16];
	u8	dev_info[10];
} __packed;

struct sfi_i2c_table_entry {
	u16	host_num;
	u16	addr;		/* slave addr */
	u16	irq_info;
	char	name[16];
	u8	dev_info[10];
} __packed;

struct sfi_gpe_table_entry {
	u16	logical_id;	/* logical id */
	u16	phy_id;		/* physical GPE id */
} __packed;


typedef int (*sfi_table_handler) (struct sfi_table_header *table);

#ifdef	CONFIG_SFI
extern int __init sfi_init_memory_map(void);
extern void __init sfi_init(void);
extern int __init sfi_platform_init(void);
extern void __init sfi_init_late(void);

int sfi_table_parse(char *signature, char *oem_id, char *oem_table_id,
			uint flag, sfi_table_handler handler);

extern int sfi_disabled;
static inline void disable_sfi(void)
{
	sfi_disabled = 1;
}

#else /* !CONFIG_SFI */

static inline int sfi_init_memory_map(void)
{
	return -1;
}

static inline void sfi_init(void)
{
	return;
}

static inline void sfi_init_late(void)
{
}

#define sfi_disabled	0

static inline int sfi_table_parse(char *signature, char *oem_id,
					char *oem_table_id, unsigned int flags,
					sfi_table_handler handler)
{
	return -1;
}

#endif /* CONFIG_SFI */

#endif	/*_LINUX_SFI_H*/
