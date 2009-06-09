#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/time.h>
#include <linux/kernel_stat.h>
#include <asm/cputime.h>

static u64 uptime_jiffies = INITIAL_JIFFIES;
static struct timespec ts_uptime;
static struct timespec ts_idle;

static int uptime_proc_show(struct seq_file *m, void *v)
{
	cputime_t idletime;
	u64 now;
	int i;

	now = get_jiffies_64();
	if (uptime_jiffies != now) {
		uptime_jiffies = now;
		idletime = cputime_zero;
		for_each_possible_cpu(i)
			idletime = cputime64_add(idletime,
						 kstat_cpu(i).cpustat.idle);
		do_posix_clock_monotonic_gettime(&ts_uptime);
		monotonic_to_bootbased(&ts_uptime);
		cputime_to_timespec(idletime, &ts_idle);
	}

	seq_printf(m, "%lu.%02lu %lu.%02lu\n",
			(unsigned long) ts_uptime.tv_sec,
			(ts_uptime.tv_nsec / (NSEC_PER_SEC / 100)),
			(unsigned long) ts_idle.tv_sec,
			(ts_idle.tv_nsec / (NSEC_PER_SEC / 100)));
	return 0;
}

static int uptime_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, uptime_proc_show, NULL);
}

static const struct file_operations uptime_proc_fops = {
	.open		= uptime_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init proc_uptime_init(void)
{
	proc_create("uptime", 0, NULL, &uptime_proc_fops);
	return 0;
}
module_init(proc_uptime_init);
