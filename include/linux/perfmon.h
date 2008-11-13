/*
 * Copyright (c) 2001-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 */

#ifndef __LINUX_PERFMON_H__
#define __LINUX_PERFMON_H__

/*
 * This file contains all the user visible generic definitions for the
 * interface. Model-specific user-visible definitions are located in
 * the asm/perfmon.h file.
 */

/*
 * include arch-specific user interface definitions
 */
#include <asm/perfmon.h>

/*
 * defined by each arch
 */
#define PFM_MAX_PMCS	PFM_ARCH_MAX_PMCS
#define PFM_MAX_PMDS	PFM_ARCH_MAX_PMDS

/*
 * number of elements for each type of bitvector
 * all bitvectors use u64 fixed size type on all architectures.
 */
#define PFM_BVSIZE(x)	(((x)+(sizeof(__u64)<<3)-1) / (sizeof(__u64)<<3))
#define PFM_PMD_BV	PFM_BVSIZE(PFM_MAX_PMDS)
#define PFM_PMC_BV	PFM_BVSIZE(PFM_MAX_PMCS)

/*
 * argument to pfm_create
 * populated on return
 */
struct pfarg_sinfo {
	__u64 sif_avail_pmcs[PFM_PMC_BV];/* out: available PMCs */
	__u64 sif_avail_pmds[PFM_PMD_BV];/* out: available PMDs */
	__u64 sif_reserved1[4];		 /* for future use */
};

/*
 * PMC and PMD generic register description
 */
struct pfarg_pmr {
	__u16 reg_num;		/* which register */
	__u16 reg_res1;		/* reserved */
	__u32 reg_flags;	/* REGFL flags */
	__u64 reg_value;	/* 64-bit value */
};

/*
 * pfm_write, pfm_read type:
 */
#define PFM_RW_PMD	0x01 /* accessing PMD registers */
#define PFM_RW_PMC	0x02 /* accessing PMC registers */

/*
 * pfm_set_state state:
 */
#define PFM_ST_START	0x01 /* start monitoring */
#define PFM_ST_STOP	0x02 /* stop monitoring */

/*
 * pfm_attach special target to trigger detach
 */
#define PFM_NO_TARGET	-1 /* detach session target */

/*
 * default value for the user and group security parameters in
 * /proc/sys/kernel/perfmon/sys_group
 * /proc/sys/kernel/perfmon/task_group
 */
#define PFM_GROUP_PERM_ANY	-1	/* any user/group */

/*
 * perfmon version number
 */
#define PFM_VERSION_MAJ		 3U
#define PFM_VERSION_MIN		 0U
#define PFM_VERSION		 (((PFM_VERSION_MAJ&0xffff)<<16)|\
				  (PFM_VERSION_MIN & 0xffff))
#define PFM_VERSION_MAJOR(x)	 (((x)>>16) & 0xffff)
#define PFM_VERSION_MINOR(x)	 ((x) & 0xffff)

#endif /* __LINUX_PERFMON_H__ */
