/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */
#include <linux/kernel.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/cpu.h>
#include <linux/clockchips.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define ACPI_PROCESSOR_AGGREGATOR_CLASS	"processor_aggregator"
#define ACPI_PROCESSOR_AGGREGATOR_DEVICE_NAME "Processor Aggregator"
#define ACPI_PROCESSOR_AGGREGATOR_NOTIFY 0x80
static DEFINE_MUTEX(isolated_cpus_lock);

#define MWAIT_SUBSTATE_MASK	(0xf)
#define MWAIT_CSTATE_MASK	(0xf)
#define MWAIT_SUBSTATE_SIZE	(4)
#define CPUID_MWAIT_LEAF (5)
#define CPUID5_ECX_EXTENSIONS_SUPPORTED (0x1)
#define CPUID5_ECX_INTERRUPT_BREAK	(0x2)
static unsigned long power_saving_mwait_eax;
static void power_saving_mwait_init(void)
{
	unsigned int eax, ebx, ecx, edx;
	unsigned int highest_cstate = 0;
	unsigned int highest_subcstate = 0;
	int i;

	if (!boot_cpu_has(X86_FEATURE_MWAIT))
		return;
	if (boot_cpu_data.cpuid_level < CPUID_MWAIT_LEAF)
		return;

	cpuid(CPUID_MWAIT_LEAF, &eax, &ebx, &ecx, &edx);

	if (!(ecx & CPUID5_ECX_EXTENSIONS_SUPPORTED) ||
	    !(ecx & CPUID5_ECX_INTERRUPT_BREAK))
		return;

	edx >>= MWAIT_SUBSTATE_SIZE;
	for (i = 0; i < 7 && edx; i++, edx >>= MWAIT_SUBSTATE_SIZE) {
		if (edx & MWAIT_SUBSTATE_MASK) {
			highest_cstate = i;
			highest_subcstate = edx & MWAIT_SUBSTATE_MASK;
		}
	}
	power_saving_mwait_eax = (highest_cstate << MWAIT_SUBSTATE_SIZE) |
		(highest_subcstate - 1);

	for_each_online_cpu(i)
		clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ON, &i);

#if defined (CONFIG_GENERIC_TIME) && defined (CONFIG_X86)
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
	case X86_VENDOR_INTEL:
		/*
		 * AMD Fam10h TSC will tick in all
		 * C/P/S0/S1 states when this bit is set.
		 */
		if (boot_cpu_has(X86_FEATURE_NONSTOP_TSC))
			return;

		/*FALL THROUGH*/
	default:
		/* TSC could halt in idle, so notify users */
		mark_tsc_unstable("TSC halts in idle");
	}
#endif
}

static int power_saving_thread(void *data)
{
	struct sched_param param = {.sched_priority = MAX_RT_PRIO - 1};
	int do_sleep;

	/*
	 * we just create a RT task to do power saving. Scheduler will migrate
	 * the task to any CPU.
	 */
	sched_setscheduler(current, SCHED_RR, &param);

	while (!kthread_should_stop()) {
		int cpu;
		u64 expire_time;

		try_to_freeze();

		do_sleep = 0;

		current_thread_info()->status &= ~TS_POLLING;
		/*
		 * TS_POLLING-cleared state must be visible before we test
		 * NEED_RESCHED:
		 */
		smp_mb();

		expire_time = jiffies + HZ * 95 /100;

		while (!need_resched()) {
			local_irq_disable();
			cpu = smp_processor_id();
			clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER, &cpu);
			stop_critical_timings();

			__monitor((void *)&current_thread_info()->flags, 0, 0);
			smp_mb();
			if (!need_resched())
				__mwait(power_saving_mwait_eax, 1);

			start_critical_timings();
			clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT, &cpu);
			local_irq_enable();

			if (jiffies > expire_time) {
				do_sleep = 1;
				break;
			}
		}

		current_thread_info()->status |= TS_POLLING;

		/*
		 * current sched_rt has threshold for rt task running time.
		 * When a rt task uses 95% CPU time, the rt thread will be
		 * scheduled out for 5% CPU time to not starve other tasks. But
		 * the mechanism only works when all CPUs have RT task running,
		 * as if one CPU hasn't RT task, RT task from other CPUs will
		 * borrow CPU time from this CPU and cause RT task use > 95%
		 * CPU time. To make 'avoid staration' work, takes a nap here.
		 */
		if (do_sleep)
			schedule_timeout_killable(HZ * 5 /100);
	}
	return 0;
}

static struct task_struct *ps_tsks[NR_CPUS];
static unsigned int ps_tsk_num;
static int create_power_saving_task(void)
{
	ps_tsks[ps_tsk_num] = kthread_run(power_saving_thread, NULL,
		"power_saving/%d", ps_tsk_num);
	if (ps_tsks[ps_tsk_num]) {
		ps_tsk_num++;
		return 0;
	}
	return -EINVAL;
}

static void destroy_power_saving_task(void)
{
	if (ps_tsk_num > 0) {
		ps_tsk_num--;
		kthread_stop(ps_tsks[ps_tsk_num]);
	}
}

static void set_power_saving_task_num(unsigned int num)
{
	if (num > ps_tsk_num) {
		while (ps_tsk_num < num) {
			if (create_power_saving_task())
				return;
		}
	} else if (num < ps_tsk_num) {
		while (ps_tsk_num > num)
			destroy_power_saving_task();
	}
}

static int acpi_processor_aggregator_idle_cpus(unsigned int num_cpus)
{
	get_online_cpus();

	num_cpus = min_t(unsigned int, num_cpus, num_online_cpus());
	set_power_saving_task_num(num_cpus);

	put_online_cpus();
	return 0;
}

static uint32_t acpi_processor_aggregator_idle_cpus_num(void)
{
	return ps_tsk_num;
}

static ssize_t acpi_processor_aggregator_idlecpus_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long num;
	if (strict_strtoul(buf, 0, &num))
		return -EINVAL;
	mutex_lock(&isolated_cpus_lock);
	acpi_processor_aggregator_idle_cpus(num);
	mutex_unlock(&isolated_cpus_lock);
	return count;
}

static ssize_t acpi_processor_aggregator_idlecpus_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d",
		acpi_processor_aggregator_idle_cpus_num());
}
static DEVICE_ATTR(idlecpus, S_IRUGO|S_IWUSR,
	acpi_processor_aggregator_idlecpus_show,
	acpi_processor_aggregator_idlecpus_store);

static int acpi_processor_aggregator_add_sysfs(struct acpi_device *device)
{
	int result;

	result = device_create_file(&device->dev, &dev_attr_idlecpus);
	if (result)
		return -ENODEV;
	return 0;
}

static void acpi_processor_aggregator_remove_sysfs(struct acpi_device *device)
{
	device_remove_file(&device->dev, &dev_attr_idlecpus);
}

/* Query firmware how many CPUs should be idle */
static int acpi_processor_aggregator_pur(acpi_handle handle, int *num_cpus)
{
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_status status;
	union acpi_object *package;
	int rev, num, ret = -EINVAL;

	status = acpi_evaluate_object(handle, "_PUR", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return -EINVAL;
	package = buffer.pointer;
	if (package->type != ACPI_TYPE_PACKAGE || package->package.count != 2)
		goto out;
	rev = package->package.elements[0].integer.value;
	num = package->package.elements[1].integer.value;
	if (rev != 1)
		goto out;
	*num_cpus = num;
	ret = 0;
out:
	kfree(buffer.pointer);
	return ret;
}

/* Notify firmware how many CPUs are idle */
static void acpi_processor_aggregator_ost(acpi_handle handle, int stat,
	uint32_t idle_cpus)
{
	union acpi_object params[3] = {
		{.type = ACPI_TYPE_INTEGER,},
		{.type = ACPI_TYPE_INTEGER,},
		{.type = ACPI_TYPE_BUFFER,},
	};
	struct acpi_object_list arg_list = {3, params};

	params[0].integer.value = ACPI_PROCESSOR_AGGREGATOR_NOTIFY;
	params[1].integer.value =  stat;
	params[2].buffer.length = 4;
	params[2].buffer.pointer = (void *)&idle_cpus;
	acpi_evaluate_object(handle, "_OST", &arg_list, NULL);
}

static void acpi_processor_aggregator_handle_notify(acpi_handle handle)
{
	int num_cpus, ret;
	uint32_t idle_cpus;

	mutex_lock(&isolated_cpus_lock);
	if (acpi_processor_aggregator_pur(handle, &num_cpus)) {
		mutex_unlock(&isolated_cpus_lock);
		return;
	}
	ret = acpi_processor_aggregator_idle_cpus(num_cpus);
	idle_cpus = acpi_processor_aggregator_idle_cpus_num();
	if (!ret)
		acpi_processor_aggregator_ost(handle, 0, idle_cpus);
	else
		acpi_processor_aggregator_ost(handle, 1, 0);
	mutex_unlock(&isolated_cpus_lock);
}

static void acpi_processor_aggregator_notify(acpi_handle handle, u32 event,
	void *data)
{
	struct acpi_device *device = data;

	switch (event) {
	case ACPI_PROCESSOR_AGGREGATOR_NOTIFY:
		acpi_processor_aggregator_handle_notify(handle);
		acpi_bus_generate_proc_event(device, event, 0);
		acpi_bus_generate_netlink_event(device->pnp.device_class,
			dev_name(&device->dev), event, 0);
		break;
	default:
		printk(KERN_WARNING"Unsupported event [0x%x]\n", event);
		break;
	}
}

static int acpi_processor_aggregator_add(struct acpi_device *device)
{
	acpi_status status;

	strcpy(acpi_device_name(device), ACPI_PROCESSOR_AGGREGATOR_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_PROCESSOR_AGGREGATOR_CLASS);

	if (acpi_processor_aggregator_add_sysfs(device))
		return -ENODEV;

	status = acpi_install_notify_handler(device->handle,
		ACPI_DEVICE_NOTIFY, acpi_processor_aggregator_notify, device);
	if (ACPI_FAILURE(status)) {
		acpi_processor_aggregator_remove_sysfs(device);
		return -ENODEV;
	}

	return 0;
}

static int acpi_processor_aggregator_remove(struct acpi_device *device, int type)
{
	mutex_lock(&isolated_cpus_lock);
	acpi_processor_aggregator_idle_cpus(0);
	mutex_unlock(&isolated_cpus_lock);

	acpi_remove_notify_handler(device->handle,
		ACPI_DEVICE_NOTIFY, acpi_processor_aggregator_notify);
	acpi_processor_aggregator_remove_sysfs(device);
	return 0;
}

static const struct acpi_device_id processor_aggregator_device_ids[] = {
	{"ACPI000C", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, processor_aggregator_device_ids);

static struct acpi_driver acpi_processor_aggregator_driver = {
	.name = "processor_aggregator",
	.class = ACPI_PROCESSOR_AGGREGATOR_CLASS,
	.ids = processor_aggregator_device_ids,
	.ops = {
		.add = acpi_processor_aggregator_add,
		.remove = acpi_processor_aggregator_remove,
	},
};

static int __init acpi_processor_aggregator_init(void)
{
	power_saving_mwait_init();
	if (power_saving_mwait_eax == 0)
		return -EINVAL;

	return acpi_bus_register_driver(&acpi_processor_aggregator_driver);
}

static void __exit acpi_processor_aggregator_exit(void)
{
	acpi_bus_unregister_driver(&acpi_processor_aggregator_driver);
}

module_init(acpi_processor_aggregator_init);
module_exit(acpi_processor_aggregator_exit);
MODULE_AUTHOR("Shaohua Li<shaohua.li@intel.com>");
MODULE_DESCRIPTION("ACPI Processor Aggregator Driver");
MODULE_LICENSE("GPL");
