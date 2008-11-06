/*
 * Copyright (c) 2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *
 * Interface for PMU description modules
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
#ifndef __PERFMON_PMU_H__
#define __PERFMON_PMU_H__ 1

/*
 * generic information about a PMC or PMD register
 */
struct pfm_regmap_desc {
	u16  type;		/* register infos */
	u16  reserved1;		/* for future use */
	u32  reserved2;		/* for future use */
	u64  dfl_val;		/* power-on default value (quiescent) */
	u64  rsvd_msk;		/* reserved bits: 1 means reserved */
	u64  no_emul64_msk;	/* bits to clear for PFM_REGFL_NO_EMUL64 */
	unsigned long hw_addr;	/* HW register address or index */
	struct kobject	kobj;	/* for internal use only */
	char *desc;		/* HW register description string */
	u64 dep_pmcs[PFM_PMC_BV];/* depending PMC registers */
};

/*
 * pfm_reg_desc helper macros
 */
#define PMC_D(t, d, v, r, n, h) \
	{ .type = t,          \
	  .desc = d,          \
	  .dfl_val = v,       \
	  .rsvd_msk = r,      \
	  .no_emul64_msk = n, \
	  .hw_addr = h	      \
	}

#define PMD_D(t, d, h)        \
	{ .type = t,          \
	  .desc = d,          \
	  .rsvd_msk = 0,      \
	  .no_emul64_msk = 0, \
	  .hw_addr = h	      \
	}

#define PMD_DR(t, d, h, r)    \
	{ .type = t,          \
	  .desc = d,          \
	  .rsvd_msk = r,      \
	  .no_emul64_msk = 0, \
	  .hw_addr = h	      \
	}

#define PMX_NA \
	{ .type = PFM_REG_NA }

/*
 * type of a PMU register (16-bit bitmask) for use with pfm_reg_desc.type
 */
#define PFM_REG_NA	0x00  /* not avail. (not impl.,no access) must be 0 */
#define PFM_REG_I	0x01  /* PMC/PMD: implemented */
#define PFM_REG_WC	0x02  /* PMC: has write_checker */
#define PFM_REG_C64	0x04  /* PMD: 64-bit virtualization */
#define PFM_REG_RO	0x08  /* PMD: read-only (writes ignored) */
#define PFM_REG_INTR	0x20  /* PMD: register can generate interrupt */
#define PFM_REG_NO64	0x100 /* PMC: supports PFM_REGFL_NO_EMUL64 */

/*
 * define some shortcuts for common types
 */
#define PFM_REG_W	(PFM_REG_WC|PFM_REG_I)
#define PFM_REG_W64	(PFM_REG_WC|PFM_REG_NO64|PFM_REG_I)
#define PFM_REG_C	(PFM_REG_C64|PFM_REG_INTR|PFM_REG_I)
#define PFM_REG_I64	(PFM_REG_NO64|PFM_REG_I)
#define PFM_REG_IRO	(PFM_REG_I|PFM_REG_RO)

typedef int (*pfm_pmc_check_t)(struct pfm_context *ctx,
			       struct pfm_event_set *set,
			       struct pfarg_pmr *req);

typedef int (*pfm_pmd_check_t)(struct pfm_context *ctx,
			       struct pfm_event_set *set,
			       struct pfarg_pmr *req);

/*
 * structure used by pmu description modules
 *
 * probe_pmu() routine return value:
 * 	- 1 means recognized PMU
 * 	- 0 means not recognized PMU
 */
struct pfm_pmu_config {
	char *pmu_name;				/* PMU family name */
	char *version;				/* config module version */

	int counter_width;			/* width of hardware counter */

	struct pfm_regmap_desc	*pmc_desc;	/* PMC register descriptions */
	struct pfm_regmap_desc	*pmd_desc;	/* PMD register descriptions */

	pfm_pmc_check_t		pmc_write_check;/* write checker (optional) */
	pfm_pmd_check_t		pmd_write_check;/* write checker (optional) */
	pfm_pmd_check_t		pmd_read_check;	/* read checker (optional) */

	u16			num_pmc_entries;/* #entries in pmc_desc */
	u16			num_pmd_entries;/* #entries in pmd_desc */
	void			*pmu_info;	/* model-specific infos */
	/*
	 * fields computed internally, do not set in module
	 */
	struct pfm_regdesc	regs_all;	/* regs available to all */
	u64			ovfl_mask;	/* overflow mask */
};

static inline void *pfm_pmu_info(void)
{
	return pfm_pmu_conf->pmu_info;
}

int pfm_pmu_register(struct pfm_pmu_config *cfg);

int pfm_sysfs_add_pmu(struct pfm_pmu_config *pmu);

#endif /* __PERFMON_PMU_H__ */
