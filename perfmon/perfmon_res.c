/*
 * perfmon_res.c:  perfmon2 resource allocations
 *
 * This file implements the perfmon2 interface which
 * provides access to the hardware performance counters
 * of the host processor.
 *
 * The initial version of perfmon.c was written by
 * Ganesh Venkitachalam, IBM Corp.
 *
 * Then it was modified for perfmon-1.x by Stephane Eranian and
 * David Mosberger, Hewlett Packard Co.
 *
 * Version Perfmon-2.x is a complete rewrite of perfmon-1.x
 * by Stephane Eranian, Hewlett Packard Co.
 *
 * Copyright (c) 1999-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *                David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * More information about perfmon available at:
 * 	http://perfmon2.sf.net
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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/perfmon_kern.h>
#include "perfmon_priv.h"

/*
 * global information about all sessions
 */
struct pfm_resources {
	cpumask_t sys_cpumask;     /* bitmask of used cpus */
	u32 thread_sessions; /* #num loaded per-thread sessions */
};

static struct pfm_resources pfm_res;

static __cacheline_aligned_in_smp DEFINE_SPINLOCK(pfm_res_lock);

/**
 * pfm_session_acquire - reserve a per-thread session
 *
 * return:
 * 	 0    : success
 * 	-EBUSY: if conflicting session exist
 */
int pfm_session_acquire(void)
{
	unsigned long flags;
	int ret = 0;

	/*
	 * validy checks on cpu_mask have been done upstream
	 */
	spin_lock_irqsave(&pfm_res_lock, flags);

	PFM_DBG("in  thread=%u",
		pfm_res.thread_sessions);

	pfm_res.thread_sessions++;

	PFM_DBG("out thread=%u ret=%d",
		pfm_res.thread_sessions,
		ret);

	spin_unlock_irqrestore(&pfm_res_lock, flags);

	return ret;
}

/**
 * pfm_session_release - release a per-thread session
 *
 * called from __pfm_unload_context()
 */
void pfm_session_release(void)
{
	unsigned long flags;

	spin_lock_irqsave(&pfm_res_lock, flags);

	PFM_DBG("in thread=%u",
		pfm_res.thread_sessions);

	pfm_res.thread_sessions--;

	PFM_DBG("out thread=%u",
		pfm_res.thread_sessions);

	spin_unlock_irqrestore(&pfm_res_lock, flags);
}

/**
 * pfm_session_allcpus_acquire - acquire per-cpu sessions on all available cpus
 *
 * currently used by Oprofile on X86
 */
int pfm_session_allcpus_acquire(void)
{
	unsigned long flags;
	u32 nsys_cpus, cpu;
	int ret = -EBUSY;

	spin_lock_irqsave(&pfm_res_lock, flags);

	nsys_cpus = cpus_weight(pfm_res.sys_cpumask);

	PFM_DBG("in  sys=%u task=%u",
		nsys_cpus,
		pfm_res.thread_sessions);

	if (nsys_cpus) {
		PFM_DBG("already some system-wide sessions");
		goto abort;
	}

	/*
	 * cannot mix system wide and per-task sessions
	 */
	if (pfm_res.thread_sessions) {
		PFM_DBG("%u conflicting thread_sessions",
			pfm_res.thread_sessions);
		goto abort;
	}

	for_each_online_cpu(cpu) {
		cpu_set(cpu, pfm_res.sys_cpumask);
		nsys_cpus++;
	}

	PFM_DBG("out sys=%u task=%u",
		nsys_cpus,
		pfm_res.thread_sessions);

	ret = 0;
abort:
	spin_unlock_irqrestore(&pfm_res_lock, flags);

	return ret;
}
EXPORT_SYMBOL(pfm_session_allcpus_acquire);

/**
 * pfm_session_allcpus_release - relase per-cpu sessions on all cpus
 *
 * currently used by Oprofile code
 */
void pfm_session_allcpus_release(void)
{
	unsigned long flags;
	u32 nsys_cpus, cpu;

	spin_lock_irqsave(&pfm_res_lock, flags);

	nsys_cpus = cpus_weight(pfm_res.sys_cpumask);

	PFM_DBG("in  sys=%u task=%u",
		nsys_cpus,
		pfm_res.thread_sessions);

	/*
	 * XXX: could use __cpus_clear() with nbits
	 */
	for_each_online_cpu(cpu) {
		cpu_clear(cpu, pfm_res.sys_cpumask);
		nsys_cpus--;
	}

	PFM_DBG("out sys=%u task=%u",
		nsys_cpus,
		pfm_res.thread_sessions);

	spin_unlock_irqrestore(&pfm_res_lock, flags);
}
EXPORT_SYMBOL(pfm_session_allcpus_release);

/**
 * pfm_sysfs_res_show - return currnt resourcde usage for sysfs
 * @buf: buffer to hold string in return
 * @sz: size of buf
 * @what: what to produce
 *        what=0 : thread_sessions
 *        what=1 : cpus_weight(sys_cpumask)
 *        what=2 : smpl_buf_mem_cur
 *        what=3 : pmu model name
 *
 * called from perfmon_sysfs.c
 * return number of bytes written into buf (up to sz)
 */
ssize_t pfm_sysfs_res_show(char *buf, size_t sz, int what)
{
	unsigned long flags;

	spin_lock_irqsave(&pfm_res_lock, flags);

	switch (what) {
	case 0: snprintf(buf, sz, "%u\n", pfm_res.thread_sessions);
		break;
	case 1: snprintf(buf, sz, "%d\n", cpus_weight(pfm_res.sys_cpumask));
		break;
	case 3:
		snprintf(buf, sz, "%s\n",
			pfm_pmu_conf ?	pfm_pmu_conf->pmu_name
				     :	"unknown\n");
	}
	spin_unlock_irqrestore(&pfm_res_lock, flags);
	return strlen(buf);
}
