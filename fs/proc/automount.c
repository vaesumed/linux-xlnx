#include <linux/list.h>
#include <linux/mount.h>
#include <linux/workqueue.h>
#include "internal.h"

LIST_HEAD(proc_automounts);

static void proc_expire_automounts(struct work_struct *work);

static DECLARE_DELAYED_WORK(proc_automount_task, proc_expire_automounts);
static int proc_automount_timeout = 500 * HZ;

void proc_shrink_automounts(void)
{
	struct list_head *list = &proc_automounts;

	mark_mounts_for_expiry(list);
	mark_mounts_for_expiry(list);
	if (list_empty(list))
		return;

	schedule_delayed_work(&proc_automount_task, proc_automount_timeout);
}

static void proc_expire_automounts(struct work_struct *work)
{
	proc_shrink_automounts();
}
