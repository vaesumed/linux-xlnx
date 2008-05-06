/*
 * Completely Fair Scheduling (CFS) Class (SCHED_NORMAL/SCHED_BATCH)
 *
 *  Copyright (C) 2007 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 *  Interactivity improvements by Mike Galbraith
 *  (C) 2007 Mike Galbraith <efault@gmx.de>
 *
 *  Various enhancements by Dmitry Adamushko.
 *  (C) 2007 Dmitry Adamushko <dmitry.adamushko@gmail.com>
 *
 *  Group scheduling enhancements by Srivatsa Vaddagiri
 *  Copyright IBM Corporation, 2007
 *  Author: Srivatsa Vaddagiri <vatsa@linux.vnet.ibm.com>
 *
 *  Scaled math optimizations by Thomas Gleixner
 *  Copyright (C) 2007, Thomas Gleixner <tglx@linutronix.de>
 *
 *  Adaptive scheduling granularity, math enhancements by Peter Zijlstra
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 */

#include <linux/latencytop.h>

/*
 * Targeted preemption latency for CPU-bound tasks:
 * (default: 20ms * (1 + ilog(ncpus)), units: nanoseconds)
 *
 * NOTE: this latency value is not the same as the concept of
 * 'timeslice length' - timeslices in CFS are of variable length
 * and have no persistent notion like in traditional, time-slice
 * based scheduling concepts.
 *
 * (to see the precise effective timeslice length of your workload,
 *  run vmstat and monitor the context-switches (cs) field)
 */
unsigned int sysctl_sched_latency = 20000000ULL;

/*
 * Minimal preemption granularity for CPU-bound tasks:
 * (default: 4 msec * (1 + ilog(ncpus)), units: nanoseconds)
 */
unsigned int sysctl_sched_min_granularity = 4000000ULL;

/*
 * is kept at sysctl_sched_latency / sysctl_sched_min_granularity
 */
static unsigned int sched_nr_latency = 5;

/*
 * After fork, child runs first. (default) If set to 0 then
 * parent will (try to) run first.
 */
const_debug unsigned int sysctl_sched_child_runs_first = 1;

/*
 * sys_sched_yield() compat mode
 *
 * This option switches the agressive yield implementation of the
 * old scheduler back on.
 */
unsigned int __read_mostly sysctl_sched_compat_yield;

/*
 * SCHED_OTHER wake-up granularity.
 * (default: 10 msec * (1 + ilog(ncpus)), units: nanoseconds)
 *
 * This option delays the preemption effects of decoupled workloads
 * and reduces their over-scheduling. Synchronous workloads will still
 * have immediate wakeup/sleep latencies.
 */
unsigned int sysctl_sched_wakeup_granularity = 10000000UL;

const_debug unsigned int sysctl_sched_migration_cost = 500000UL;

/**************************************************************
 * CFS operations on generic schedulable entities:
 */

static inline struct task_struct *task_of(struct sched_entity *se)
{
	return container_of(se, struct task_struct, se);
}

#ifdef CONFIG_FAIR_GROUP_SCHED

/* cpu runqueue to which this cfs_rq is attached */
static inline struct rq *rq_of(struct cfs_rq *cfs_rq)
{
	return cfs_rq->rq;
}

/* An entity is a task if it doesn't "own" a runqueue */
#define entity_is_task(se)	(!se->my_q)

/* Walk up scheduling entities hierarchy */
#define for_each_sched_entity(se) \
		for (; se; se = se->parent)

static inline struct cfs_rq *task_cfs_rq(struct task_struct *p)
{
	return p->se.cfs_rq;
}

/* runqueue on which this entity is (to be) queued */
static inline struct cfs_rq *cfs_rq_of(struct sched_entity *se)
{
	return se->cfs_rq;
}

/* runqueue "owned" by this group */
static inline struct cfs_rq *group_cfs_rq(struct sched_entity *grp)
{
	return grp->my_q;
}

/* Given a group's cfs_rq on one cpu, return its corresponding cfs_rq on
 * another cpu ('this_cpu')
 */
static inline struct cfs_rq *cpu_cfs_rq(struct cfs_rq *cfs_rq, int this_cpu)
{
	return cfs_rq->tg->cfs_rq[this_cpu];
}

/* Iterate thr' all leaf cfs_rq's on a runqueue */
#define for_each_leaf_cfs_rq(rq, cfs_rq) \
	list_for_each_entry_rcu(cfs_rq, &rq->leaf_cfs_rq_list, leaf_cfs_rq_list)

/* Do the two (enqueued) entities belong to the same group ? */
static inline int
is_same_group(struct sched_entity *se, struct sched_entity *pse)
{
	if (se->cfs_rq == pse->cfs_rq)
		return 1;

	return 0;
}

static inline struct sched_entity *parent_entity(struct sched_entity *se)
{
	return se->parent;
}

#else	/* CONFIG_FAIR_GROUP_SCHED */

static inline struct rq *rq_of(struct cfs_rq *cfs_rq)
{
	return container_of(cfs_rq, struct rq, cfs);
}

#define entity_is_task(se)	1

#define for_each_sched_entity(se) \
		for (; se; se = NULL)

static inline struct cfs_rq *task_cfs_rq(struct task_struct *p)
{
	return &task_rq(p)->cfs;
}

static inline struct cfs_rq *cfs_rq_of(struct sched_entity *se)
{
	struct task_struct *p = task_of(se);
	struct rq *rq = task_rq(p);

	return &rq->cfs;
}

/* runqueue "owned" by this group */
static inline struct cfs_rq *group_cfs_rq(struct sched_entity *grp)
{
	return NULL;
}

static inline struct cfs_rq *cpu_cfs_rq(struct cfs_rq *cfs_rq, int this_cpu)
{
	return &cpu_rq(this_cpu)->cfs;
}

#define for_each_leaf_cfs_rq(rq, cfs_rq) \
		for (cfs_rq = &rq->cfs; cfs_rq; cfs_rq = NULL)

static inline int
is_same_group(struct sched_entity *se, struct sched_entity *pse)
{
	return 1;
}

static inline struct sched_entity *parent_entity(struct sched_entity *se)
{
	return NULL;
}

#endif	/* CONFIG_FAIR_GROUP_SCHED */


/**************************************************************
 * Scheduling class tree data structure manipulation methods:
 */

static inline u64 max_vruntime(u64 min_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - min_vruntime);
	if (delta > 0)
		min_vruntime = vruntime;

	return min_vruntime;
}

static inline u64 min_vruntime(u64 min_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - min_vruntime);
	if (delta < 0)
		min_vruntime = vruntime;

	return min_vruntime;
}

static inline
s64 entity_timeline_key(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	return se->vruntime - cfs_r_rq->min_vruntime;
}

static inline struct rb_node *first_fair(struct cfs_root_rq *cfs_r_rq)
{
	return cfs_r_rq->left_timeline;
}

static struct sched_entity *__pick_next_timeline(struct cfs_root_rq *cfs_r_rq)
{
	return rb_entry(first_fair(cfs_r_rq),
			struct sched_entity, timeline_node);
}

static inline
struct sched_entity *__pick_last_timeline(struct cfs_root_rq *cfs_r_rq)
{
	struct rb_node *last = rb_last(&cfs_r_rq->tasks_timeline);

	if (!last)
		return NULL;

	return rb_entry(last, struct sched_entity, timeline_node);
}

#ifdef CONFIG_FAIR_GROUP_SCHED
static inline
void avg_vruntime_add(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	s64 key = entity_timeline_key(cfs_r_rq, se);
	cfs_r_rq->avg_vruntime += key;
	cfs_r_rq->nr_queued++;
}

static inline
void avg_vruntime_sub(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	s64 key = entity_timeline_key(cfs_r_rq, se);
	cfs_r_rq->avg_vruntime -= key;
	cfs_r_rq->nr_queued--;
}

static inline
void avg_vruntime_update(struct cfs_root_rq *cfs_r_rq, s64 delta)
{
	cfs_r_rq->avg_vruntime -= cfs_r_rq->nr_queued * delta;
}

static inline
u64 avg_vruntime(struct cfs_root_rq *cfs_r_rq)
{
	s64 avg = cfs_r_rq->avg_vruntime;
	int sign = 0;

	if (avg < 0) {
		sign = 1;
		avg = -avg;
	}

	if (cfs_r_rq->nr_queued)
		do_div(avg, cfs_r_rq->nr_queued);

	if (sign)
		avg = -avg;

	return cfs_r_rq->min_vruntime + avg;
}

static inline
int entity_eligible(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	s64 vruntime = entity_timeline_key(cfs_r_rq, se);

	return (vruntime * cfs_r_rq->nr_queued) <= cfs_r_rq->avg_vruntime;
}

static inline
s64 entity_deadline_key(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	struct rq *rq = container_of(cfs_r_rq, struct rq, cfs_root);

	return se->deadline - rq->clock;
}

static inline
int entity_expired(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	return entity_deadline_key(cfs_r_rq, se) <= 0;
}

static u64 sched_vslice_add(struct cfs_rq *cfs_rq, struct sched_entity *se);

static u64 sched_se_period(struct sched_entity *se)
{
	return sched_vslice_add(cfs_rq_of(se), se);
}

static void sched_calc_deadline(struct cfs_root_rq *cfs_r_rq,
				struct sched_entity *se)
{
	struct rq *rq = container_of(cfs_r_rq, struct rq, cfs_root);

	se->deadline = rq->clock + sched_se_period(se);
}

static void
__enqueue_deadline(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	struct rb_node **link;
	struct rb_node *parent = NULL;
	struct sched_entity *entry;
	s64 key, entry_key;
	int leftmost = 1;

	sched_calc_deadline(cfs_r_rq, se);

	link = &cfs_r_rq->tasks_deadline.rb_node;
	key = entity_deadline_key(cfs_r_rq, se);
	/*
	 * Find the right place in the rbtree:
	 */
	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct sched_entity, deadline_node);
		/*
		 * Prefer shorter latency tasks over higher.
		 */
		entry_key = entity_deadline_key(cfs_r_rq, entry);
		if (key < entry_key || (key == entry_key &&
				sched_se_period(se) < sched_se_period(entry))) {

			link = &parent->rb_left;

		} else {
			link = &parent->rb_right;
			leftmost = 0;
		}
	}

	/*
	 * Maintain a cache of leftmost tree entries (it is frequently
	 * used):
	 */
	if (leftmost)
		cfs_r_rq->left_deadline = &se->deadline_node;

	rb_link_node(&se->deadline_node, parent, link);
	rb_insert_color(&se->deadline_node, &cfs_r_rq->tasks_deadline);
}

static void
__dequeue_deadline(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	if (cfs_r_rq->left_deadline == &se->deadline_node)
		cfs_r_rq->left_deadline = rb_next(&se->deadline_node);

	rb_erase(&se->deadline_node, &cfs_r_rq->tasks_deadline);
}

static inline struct rb_node *first_deadline(struct cfs_root_rq *cfs_r_rq)
{
	return cfs_r_rq->left_deadline;
}

static struct sched_entity *__pick_next_deadline(struct cfs_root_rq *cfs_r_rq)
{
	return rb_entry(first_deadline(cfs_r_rq),
			struct sched_entity, deadline_node);
}

static inline struct sched_entity *se_of(struct rb_node *node)
{
	return rb_entry(node, struct sched_entity, timeline_node);
}

#define deadline_gt(cfs_r_rq, field, lnode, rnode)			\
({									\
 	struct rq *rq = container_of(cfs_r_rq, struct rq, cfs_root);	\
 	s64 l = se_of(lnode)->field - rq->clock;			\
	s64 r = se_of(rnode)->field - rq->clock;			\
	l > r;								\
})

static struct sched_entity *__pick_next_eevdf(struct cfs_root_rq *cfs_r_rq)
{
	struct rb_node *node = cfs_r_rq->tasks_timeline.rb_node;
	struct rb_node *tree = NULL, *path = NULL;

	while (node) {
		if (entity_eligible(cfs_r_rq, se_of(node))) {
			if (!path || deadline_gt(cfs_r_rq, deadline, path, node))
				path = node;

			if (!tree || (node->rb_left &&
				      deadline_gt(cfs_r_rq, min_deadline, tree,
					          node->rb_left)))
				tree = node->rb_left;

			node = node->rb_right;
		} else
			node = node->rb_left;
	}

	if (!tree || deadline_gt(cfs_r_rq, min_deadline, tree, path))
		return se_of(path);

	for (node = tree; node; ) {
		if (se_of(tree)->min_deadline == se_of(node)->min_deadline)
			return se_of(node);

		if (node->rb_left && (se_of(node)->min_deadline ==
				      se_of(node->rb_left)->min_deadline))
			node = node->rb_left;
		else
			node = node->rb_right;
	}

	BUG();
}

static struct sched_entity *__pick_next_entity(struct cfs_root_rq *cfs_r_rq)
{
	struct sched_entity *next;

	if (sched_feat(EEVDF)) {
		next = __pick_next_eevdf(cfs_r_rq);
		next->eligible = 1;

	} else if (sched_feat(DEADLINE)) {
		next = __pick_next_deadline(cfs_r_rq);

		next->eligible = entity_eligible(cfs_r_rq, next);
		if (next->eligible || entity_expired(cfs_r_rq, next))
			return next;
	}

	next = __pick_next_timeline(cfs_r_rq);
	next->eligible = 1;

	return next;
}

static void update_min_deadline(struct cfs_root_rq *cfs_r_rq,
		struct sched_entity *se, struct rb_node *node)
{
	if (node) {
		struct sched_entity *child = rb_entry(node,
				struct sched_entity, timeline_node);
		struct rq *rq = container_of(cfs_r_rq, struct rq, cfs_root);

		if ((se->min_deadline - rq->clock) >
				(child->min_deadline - rq->clock))
			se->min_deadline = child->min_deadline;
	}
}

static void update_node(struct cfs_root_rq *cfs_r_rq, struct rb_node *node)
{
	struct sched_entity *se = rb_entry(node,
			struct sched_entity, timeline_node);

	se->min_deadline = se->deadline;
	update_min_deadline(cfs_r_rq, se, node->rb_right);
	update_min_deadline(cfs_r_rq, se, node->rb_left);
}

static void update_tree(struct cfs_root_rq *cfs_r_rq, struct rb_node *node)
{
	struct rb_node *parent;
up:
	update_node(cfs_r_rq, node);

	parent = rb_parent(node);
	if (!parent)
		return;

	if (node == parent->rb_left && parent->rb_right)
		update_node(cfs_r_rq, parent->rb_right);
	else if (parent->rb_left)
		update_node(cfs_r_rq, parent->rb_left);

	node = parent;
	goto up;
}

static void
update_tree_enqueue(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	struct rb_node *node = &se->timeline_node;

	if (node->rb_left)
		node = node->rb_left;
	else if (node->rb_right)
		node = node->rb_right;

	update_tree(cfs_r_rq, node);
}

static struct rb_node *
update_tree_dequeue_begin(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	struct rb_node *deepest;
	struct rb_node *node = &se->timeline_node;

	if (!node->rb_right && !node->rb_left)
		deepest = rb_parent(node);
	else if (!node->rb_right)
		deepest = node->rb_left;
	else if (!node->rb_left)
		deepest = node->rb_right;
	else {
		deepest = rb_next(node);
		if (deepest->rb_right)
			deepest = deepest->rb_right;
		else if (rb_parent(deepest) != node)
			deepest = rb_parent(deepest);
	}

	return deepest;
}

static void
update_tree_dequeue_end(struct cfs_root_rq *cfs_r_rq, struct rb_node *node)
{
	if (node)
		update_tree(cfs_r_rq, node);
}

#else /* CONFIG_FAIR_GROUP_SCHED */

static inline
void avg_vruntime_add(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	cfs_r_rq->nr_queued++;
}

static inline
void avg_vruntime_sub(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	cfs_r_rq->nr_queued--;
}

static inline
void avg_vruntime_update(struct cfs_root_rq *cfs_r_rq, s64 delta)
{
}

static inline
void __enqueue_deadline(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
}

static inline
void __dequeue_deadline(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
}

static inline
void update_tree_enqueue(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
}

static rb_node *
update_tree_dequeue_begin(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	return NULL;
}

static void
update_tree_dequeue_end(struct cfs_root_rq *cfs_r_rq, rb_node *node)
{
}

static inline
struct sched_entity *__pick_next_entity(struct cfs_root_rq *cfs_r_rq)
{
	return __pick_next_timeline(cfs_r_rq);
}
#endif /* CONFIG_FAIR_GROUP_SCHED */

/*
 * maintain cfs_rq->min_vruntime to be a monotonic increasing
 * value tracking the leftmost vruntime in the tree.
 */
static void
update_min_vruntime(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	/*
	 * open coded max_vruntime() to allow updating avg_vruntime
	 */
	s64 delta = (s64)(se->vruntime - cfs_r_rq->min_vruntime);
	if (delta > 0) {
		avg_vruntime_update(cfs_r_rq, delta);
		cfs_r_rq->min_vruntime = se->vruntime;
	}
}

static void
__enqueue_timeline(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	struct rb_node **link;
	struct rb_node *parent = NULL;
	struct sched_entity *entry;
	s64 key;
	int leftmost = 1;

	link = &cfs_r_rq->tasks_timeline.rb_node;
	key = entity_timeline_key(cfs_r_rq, se);
	/*
	 * Find the right place in the rbtree:
	 */
	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct sched_entity, timeline_node);
		/*
		 * We dont care about collisions. Nodes with
		 * the same key stay together.
		 */
		if (key < entity_timeline_key(cfs_r_rq, entry)) {
			link = &parent->rb_left;
		} else {
			link = &parent->rb_right;
			leftmost = 0;
		}
	}

	/*
	 * Maintain a cache of leftmost tree entries (it is frequently
	 * used):
	 */
	if (leftmost) {
		cfs_r_rq->left_timeline = &se->timeline_node;
		update_min_vruntime(cfs_r_rq, se);
	}

	rb_link_node(&se->timeline_node, parent, link);
	rb_insert_color(&se->timeline_node, &cfs_r_rq->tasks_timeline);

	update_tree_enqueue(cfs_r_rq, se);
}

static void
__dequeue_timeline(struct cfs_root_rq *cfs_r_rq, struct sched_entity *se)
{
	struct rb_node *node = update_tree_dequeue_begin(cfs_r_rq, se);

	if (cfs_r_rq->left_timeline == &se->timeline_node) {
		struct rb_node *next_node;

		next_node = rb_next(&se->timeline_node);
		cfs_r_rq->left_timeline = next_node;

		if (next_node) {
			update_min_vruntime(cfs_r_rq, rb_entry(next_node,
					struct sched_entity, timeline_node));
		}
	}

	if (cfs_r_rq->next == se)
		cfs_r_rq->next = NULL;

	rb_erase(&se->timeline_node, &cfs_r_rq->tasks_timeline);

	update_tree_dequeue_end(cfs_r_rq, node);
}

/*
 * Enqueue an entity into the rb-tree:
 */
static void __enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (!entity_is_task(se))
		return;

	if (se == cfs_rq->curr)
		return;

	avg_vruntime_add(&rq_of(cfs_rq)->cfs_root, se);
	__enqueue_timeline(&rq_of(cfs_rq)->cfs_root, se);
	__enqueue_deadline(&rq_of(cfs_rq)->cfs_root, se);
}

static void __dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (!entity_is_task(se))
		return;

	if (se == cfs_rq->curr)
		return;

	__dequeue_timeline(&rq_of(cfs_rq)->cfs_root, se);
	__dequeue_deadline(&rq_of(cfs_rq)->cfs_root, se);
	avg_vruntime_sub(&rq_of(cfs_rq)->cfs_root, se);
}

/**************************************************************
 * Scheduling class statistics methods:
 */

#ifdef CONFIG_SCHED_DEBUG
int sched_nr_latency_handler(struct ctl_table *table, int write,
		struct file *filp, void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret = proc_dointvec_minmax(table, write, filp, buffer, lenp, ppos);

	if (ret || !write)
		return ret;

	sched_nr_latency = DIV_ROUND_UP(sysctl_sched_latency,
					sysctl_sched_min_granularity);

	return 0;
}
#endif

/*
 * delta *= w / rw
 */
static inline unsigned long
calc_delta_weight(unsigned long delta, struct sched_entity *se)
{
	for_each_sched_entity(se) {
		delta = calc_delta_mine(delta,
				se->load.weight, &cfs_rq_of(se)->load);
	}

	return delta;
}

/*
 * delta *= rw / w
 */
static inline unsigned long
calc_delta_fair(unsigned long delta, struct sched_entity *se)
{
	for_each_sched_entity(se) {
		delta = calc_delta_mine(delta,
				cfs_rq_of(se)->load.weight, &se->load);
	}

	return delta;
}

/*
 * The idea is to set a period in which each task runs once.
 *
 * When there are too many tasks (sysctl_sched_nr_latency) we have to stretch
 * this period because otherwise the slices get too small.
 *
 * p = (nr <= nl) ? l : l*nr/nl
 */
static inline u64 __sched_period(unsigned long nr_running)
{
	u64 period = sysctl_sched_latency;
	unsigned long nr_latency = sched_nr_latency;

	if (unlikely(nr_running > nr_latency)) {
		period = sysctl_sched_min_granularity;
		period *= nr_running;
	}

	return period;
}

/*
 * We calculate the wall-time slice from the period by taking a part
 * proportional to the weight.
 *
 * s = p*w/rw
 */
static u64 sched_slice(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	u64 slice = calc_delta_weight(__sched_period(cfs_rq->nr_running), se);

#ifdef CONFIG_FAIR_GROUP_SCHED
	/*
	 * Limit the max slice length when there is contention (strictly
	 * speaking we only need to do this when there are tasks of more
	 * than a single group). This avoids very longs slices of a lightly
	 * loaded group delaying tasks from another group.
	 */
	if (rq_of(cfs_rq)->cfs_root.nr_queued)
		slice = min_t(u64, slice, sysctl_sched_min_granularity);
#endif

	if (!se->eligible)
		slice /= 2;

	return slice;
}

/*
 * We calculate the vruntime slice of a to be inserted task
 *
 * vs = s*rw/w = p
 */
static u64 sched_vslice_add(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	unsigned long nr_running = cfs_rq->nr_running;

	if (!se->on_rq)
		nr_running++;

	return __sched_period(nr_running);
}

/*
 * The goal of calc_delta_asym() is to be asymmetrically around NICE_0_LOAD, in
 * that it favours >=0 over <0.
 *
 *   -20         |
 *               |
 *     0 --------+-------
 *             .'
 *    19     .'
 *
 */
static unsigned long
calc_delta_asym(unsigned long delta, struct sched_entity *se)
{
	struct load_weight lw = {
		.weight = NICE_0_LOAD,
		.inv_weight = 1UL << (WMULT_SHIFT-NICE_0_SHIFT)
	};

	for_each_sched_entity(se) {
		struct load_weight *se_lw = &se->load;

		if (se->load.weight < NICE_0_LOAD)
			se_lw = &lw;

		delta = calc_delta_mine(delta,
				cfs_rq_of(se)->load.weight, se_lw);
	}

	return delta;
}

/*
 * Update the current task's runtime statistics. Skip current tasks that
 * are not in our scheduling class.
 */
static inline void
__update_curr(struct cfs_rq *cfs_rq, struct sched_entity *curr,
	      unsigned long delta_exec)
{
	schedstat_set(curr->exec_max, max((u64)delta_exec, curr->exec_max));

	curr->sum_exec_runtime += delta_exec;
	schedstat_add(&rq_of(cfs_rq)->cfs_root, exec_clock, delta_exec);
	curr->vruntime += calc_delta_fair(delta_exec, curr);
}

static void update_curr(struct cfs_rq *cfs_rq)
{
	struct sched_entity *curr = cfs_rq->curr;
	u64 now = rq_of(cfs_rq)->clock;
	unsigned long delta_exec;

	if (unlikely(!curr))
		return;

	if (!entity_is_task(curr))
		return;

	cfs_rq = &rq_of(cfs_rq)->cfs;

	/*
	 * Get the amount of time the current task was running
	 * since the last time we changed load (this cannot
	 * overflow on 32 bits):
	 */
	delta_exec = (unsigned long)(now - curr->exec_start);

	__update_curr(cfs_rq, curr, delta_exec);
	curr->exec_start = now;

	if (entity_is_task(curr))
		cpuacct_charge(task_of(curr), delta_exec);
}

static inline void
update_stats_wait_start(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	schedstat_set(se->wait_start, rq_of(cfs_rq)->clock);
}

/*
 * Task is being enqueued - update stats:
 */
static void update_stats_enqueue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * Are we enqueueing a waiting task? (for current tasks
	 * a dequeue/enqueue event is a NOP)
	 */
	if (se != cfs_rq->curr)
		update_stats_wait_start(cfs_rq, se);
}

static void
update_stats_wait_end(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	schedstat_set(se->wait_max, max(se->wait_max,
			rq_of(cfs_rq)->clock - se->wait_start));
	schedstat_set(se->wait_count, se->wait_count + 1);
	schedstat_set(se->wait_sum, se->wait_sum +
			rq_of(cfs_rq)->clock - se->wait_start);
	schedstat_set(se->wait_start, 0);
}

static inline void
update_stats_dequeue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * Mark the end of the wait period if dequeueing a
	 * waiting task:
	 */
	if (se != cfs_rq->curr)
		update_stats_wait_end(cfs_rq, se);
}

/*
 * We are picking a new current task - update its stats:
 */
static inline void
update_stats_curr_start(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * We are starting a new run period:
	 */
	se->exec_start = rq_of(cfs_rq)->clock;
}

/**************************************************
 * Scheduling class queueing methods:
 */

#if defined CONFIG_SMP && defined CONFIG_FAIR_GROUP_SCHED
static void
add_cfs_task_weight(struct cfs_rq *cfs_rq, unsigned long weight)
{
	cfs_rq->task_weight += weight;
}
#else
static inline void
add_cfs_task_weight(struct cfs_rq *cfs_rq, unsigned long weight)
{
}
#endif

static void
account_entity_enqueue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	update_load_add(&cfs_rq->load, se->load.weight);
	if (!parent_entity(se))
		inc_cpu_load(rq_of(cfs_rq), se->load.weight);
	if (entity_is_task(se))
		add_cfs_task_weight(cfs_rq, se->load.weight);
	cfs_rq->nr_running++;
	se->on_rq = 1;
	list_add(&se->group_node, &cfs_rq->tasks);
}

static void
account_entity_dequeue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	update_load_sub(&cfs_rq->load, se->load.weight);
	if (!parent_entity(se))
		dec_cpu_load(rq_of(cfs_rq), se->load.weight);
	if (entity_is_task(se))
		add_cfs_task_weight(cfs_rq, -se->load.weight);
	cfs_rq->nr_running--;
	se->on_rq = 0;
	list_del_init(&se->group_node);
}

static void enqueue_sleeper(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
#ifdef CONFIG_SCHEDSTATS
	if (se->sleep_start) {
		u64 delta = rq_of(cfs_rq)->clock - se->sleep_start;
		struct task_struct *tsk = task_of(se);

		if ((s64)delta < 0)
			delta = 0;

		if (unlikely(delta > se->sleep_max))
			se->sleep_max = delta;

		se->sleep_start = 0;
		se->sum_sleep_runtime += delta;

		account_scheduler_latency(tsk, delta >> 10, 1);
	}
	if (se->block_start) {
		u64 delta = rq_of(cfs_rq)->clock - se->block_start;
		struct task_struct *tsk = task_of(se);

		if ((s64)delta < 0)
			delta = 0;

		if (unlikely(delta > se->block_max))
			se->block_max = delta;

		se->block_start = 0;
		se->sum_sleep_runtime += delta;

		/*
		 * Blocking time is in units of nanosecs, so shift by 20 to
		 * get a milliseconds-range estimation of the amount of
		 * time that the task spent sleeping:
		 */
		if (unlikely(prof_on == SLEEP_PROFILING)) {

			profile_hits(SLEEP_PROFILING, (void *)get_wchan(tsk),
				     delta >> 20);
		}
		account_scheduler_latency(tsk, delta >> 10, 0);
	}
#endif
}

static void check_spread(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
#ifdef CONFIG_SCHED_DEBUG
	struct cfs_root_rq *cfs_r_rq = &rq_of(cfs_rq)->cfs_root;
	s64 d = se->vruntime - cfs_r_rq->min_vruntime;

	if (d < 0)
		d = -d;

	if (d > 3*sysctl_sched_latency)
		schedstat_inc(cfs_r_rq, nr_spread_over);
#endif
}

static void
place_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int initial)
{
	struct cfs_root_rq *cfs_r_rq = &rq_of(cfs_rq)->cfs_root;
	u64 vruntime;

	if (first_fair(cfs_r_rq)) {
		vruntime = min_vruntime(cfs_r_rq->min_vruntime,
				__pick_next_timeline(cfs_r_rq)->vruntime);
	} else
		vruntime = cfs_r_rq->min_vruntime;

	/*
	 * The 'current' period is already promised to the current tasks,
	 * however the extra weight of the new task will slow them down a
	 * little, place the new task so that it fits in the slot that
	 * stays open at the end.
	 */
	if (initial && sched_feat(START_DEBIT))
		vruntime += sched_vslice_add(cfs_rq, se);

	if (!initial) {
		/* sleeps upto a single latency don't count. */
		if (sched_feat(NEW_FAIR_SLEEPERS)) {
			if (sched_feat(NORMALIZED_SLEEPER))
				vruntime -= calc_delta_weight(sysctl_sched_latency, se);
			else
				vruntime -= sysctl_sched_latency;
		}

		/* ensure we never gain time by being placed backwards. */
		vruntime = max_vruntime(se->vruntime, vruntime);
	}

	se->vruntime = vruntime;
}

static void
enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int wakeup)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(cfs_rq);
	account_entity_enqueue(cfs_rq, se);

	if (wakeup) {
		place_entity(cfs_rq, se, 0);
		enqueue_sleeper(cfs_rq, se);
	}

	update_stats_enqueue(cfs_rq, se);
	check_spread(cfs_rq, se);
	__enqueue_entity(cfs_rq, se);
}

static void update_avg(u64 *avg, u64 sample)
{
	s64 diff = sample - *avg;
	*avg += diff >> 3;
}

static void update_avg_stats(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (!se->last_wakeup)
		return;

	update_avg(&se->avg_overlap, se->sum_exec_runtime - se->last_wakeup);
	se->last_wakeup = 0;
}

static void
dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int sleep)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(cfs_rq);

	update_stats_dequeue(cfs_rq, se);
	if (sleep) {
		update_avg_stats(cfs_rq, se);
#ifdef CONFIG_SCHEDSTATS
		if (entity_is_task(se)) {
			struct task_struct *tsk = task_of(se);

			if (tsk->state & TASK_INTERRUPTIBLE)
				se->sleep_start = rq_of(cfs_rq)->clock;
			if (tsk->state & TASK_UNINTERRUPTIBLE)
				se->block_start = rq_of(cfs_rq)->clock;
		}
#endif
	}

	__dequeue_entity(cfs_rq, se);
	account_entity_dequeue(cfs_rq, se);
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void
check_preempt_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr)
{
	unsigned long ideal_runtime, delta_exec;

	ideal_runtime = sched_slice(cfs_rq, curr);
	delta_exec = curr->sum_exec_runtime - curr->prev_sum_exec_runtime;
	if (delta_exec > ideal_runtime)
		resched_task(rq_of(cfs_rq)->curr);
}

static void
set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/* 'current' is not kept within the tree. */
	if (se->on_rq) {
		/*
		 * Any task has to be enqueued before it get to execute on
		 * a CPU. So account for the time it spent waiting on the
		 * runqueue.
		 */
		update_stats_wait_end(cfs_rq, se);
		if (WARN_ON_ONCE(cfs_rq->curr))
			cfs_rq->curr = NULL;
		__dequeue_entity(cfs_rq, se);
	}

	update_stats_curr_start(cfs_rq, se);
	cfs_rq->curr = se;
#ifdef CONFIG_SCHEDSTATS
	/*
	 * Track our maximum slice length, if the CPU's load is at
	 * least twice that of our own weight (i.e. dont track it
	 * when there are only lesser-weight tasks around):
	 */
	if (rq_of(cfs_rq)->load.weight >= 2*se->load.weight) {
		se->slice_max = max(se->slice_max,
			se->sum_exec_runtime - se->prev_sum_exec_runtime);
	}
#endif
	se->prev_sum_exec_runtime = se->sum_exec_runtime;
}

static int
wakeup_preempt_entity(struct sched_entity *curr, struct sched_entity *se);

static void put_prev_entity(struct cfs_rq *cfs_rq, struct sched_entity *prev)
{
	/*
	 * If still on the runqueue then deactivate_task()
	 * was not called and update_curr() has to be done:
	 */
	if (prev->on_rq)
		update_curr(cfs_rq);

	check_spread(cfs_rq, prev);
	cfs_rq->curr = NULL;
	if (prev->on_rq) {
		update_stats_wait_start(cfs_rq, prev);
		/* Put 'current' back into the tree. */
		__enqueue_entity(cfs_rq, prev);
	}
}

static void
entity_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr, int queued)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(cfs_rq);

	if (!entity_is_task(curr))
		return;

#ifdef CONFIG_SCHED_HRTICK
	/*
	 * queued ticks are scheduled to match the slice, so don't bother
	 * validating it and just reschedule.
	 */
	if (queued) {
		resched_task(rq_of(cfs_rq)->curr);
		return;
	}
	/*
	 * don't let the period tick interfere with the hrtick preemption
	 */
	if (!sched_feat(DOUBLE_TICK) &&
			hrtimer_active(&rq_of(cfs_rq)->hrtick_timer))
		return;
#endif

	if (rq_of(cfs_rq)->load.weight != curr->load.weight ||
			!sched_feat(WAKEUP_PREEMPT))
		check_preempt_tick(cfs_rq, curr);
}

/**************************************************
 * CFS operations on tasks:
 */

#ifdef CONFIG_SCHED_HRTICK
static void hrtick_start_fair(struct rq *rq, struct task_struct *p)
{
	int requeue = rq->curr == p;
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	WARN_ON(task_rq(p) != rq);

	if (hrtick_enabled(rq) && cfs_rq->nr_running > 1) {
		u64 slice = sched_slice(cfs_rq, se);
		u64 ran = se->sum_exec_runtime - se->prev_sum_exec_runtime;
		s64 delta = slice - ran;

		if (delta < 0) {
			if (rq->curr == p)
				resched_task(p);
			return;
		}

		/*
		 * Don't schedule slices shorter than 10000ns, that just
		 * doesn't make sense. Rely on vruntime for fairness.
		 */
		if (!requeue)
			delta = max(10000LL, delta);

		hrtick_start(rq, delta, requeue);
	}
}
#else
static inline void
hrtick_start_fair(struct rq *rq, struct task_struct *p)
{
}
#endif

/*
 * The enqueue_task method is called before nr_running is
 * increased. Here we update the fair scheduling stats and
 * then put the task into the rbtree:
 */
static void enqueue_task_fair(struct rq *rq, struct task_struct *p, int wakeup)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;

	for_each_sched_entity(se) {
		if (se->on_rq)
			break;
		cfs_rq = cfs_rq_of(se);
		enqueue_entity(cfs_rq, se, wakeup);
		wakeup = 1;
	}

	hrtick_start_fair(rq, rq->curr);
}

/*
 * The dequeue_task method is called before nr_running is
 * decreased. We remove the task from the rbtree and
 * update the fair scheduling stats:
 */
static void dequeue_task_fair(struct rq *rq, struct task_struct *p, int sleep)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		dequeue_entity(cfs_rq, se, sleep);
		/* Don't dequeue parent if it has other entities besides us */
		if (cfs_rq->load.weight)
			break;
		sleep = 1;
	}

	hrtick_start_fair(rq, rq->curr);
}

/*
 * sched_yield() support is very simple - we dequeue and enqueue.
 *
 * If compat_yield is turned on then we requeue to the end of the tree.
 */
static void yield_task_fair(struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	struct cfs_rq *cfs_rq = task_cfs_rq(curr);
	struct sched_entity *rightmost, *se = &curr->se;

	/*
	 * Are we the only task in the tree?
	 */
	if (unlikely(!rq->cfs_root.nr_queued))
		return;

	if (likely(!sysctl_sched_compat_yield) && curr->policy != SCHED_BATCH) {
		update_rq_clock(rq);
		/*
		 * Update run-time statistics of the 'current'.
		 */
		update_curr(cfs_rq);

		return;
	}
	/*
	 * Find the rightmost entry in the rbtree:
	 */
	rightmost = __pick_last_timeline(&rq->cfs_root);
	/*
	 * Already in the rightmost position?
	 */
	if (unlikely(!rightmost || rightmost->vruntime < se->vruntime))
		return;

	/*
	 * Minimally necessary key value to be last in the tree:
	 * Upon rescheduling, sched_class::put_prev_task() will place
	 * 'current' within the tree based on its new key value.
	 */
	se->vruntime = rightmost->vruntime + 1;
}

/*
 * wake_idle() will wake a task on an idle cpu if task->cpu is
 * not idle and an idle cpu is available.  The span of cpus to
 * search starts with cpus closest then further out as needed,
 * so we always favor a closer, idle cpu.
 *
 * Returns the CPU we should wake onto.
 */
#if defined(ARCH_HAS_SCHED_WAKE_IDLE)
static int wake_idle(int cpu, struct task_struct *p)
{
	cpumask_t tmp;
	struct sched_domain *sd;
	int i;

	/*
	 * If it is idle, then it is the best cpu to run this task.
	 *
	 * This cpu is also the best, if it has more than one task already.
	 * Siblings must be also busy(in most cases) as they didn't already
	 * pickup the extra load from this cpu and hence we need not check
	 * sibling runqueue info. This will avoid the checks and cache miss
	 * penalities associated with that.
	 */
	if (idle_cpu(cpu) || cpu_rq(cpu)->cfs.nr_running > 1)
		return cpu;

	for_each_domain(cpu, sd) {
		if ((sd->flags & SD_WAKE_IDLE)
		    || ((sd->flags & SD_WAKE_IDLE_FAR)
			&& !task_hot(p, task_rq(p)->clock, sd))) {
			cpus_and(tmp, sd->span, p->cpus_allowed);
			for_each_cpu_mask(i, tmp) {
				if (idle_cpu(i)) {
					if (i != task_cpu(p)) {
						schedstat_inc(p,
						       se.nr_wakeups_idle);
					}
					return i;
				}
			}
		} else {
			break;
		}
	}
	return cpu;
}
#else
static inline int wake_idle(int cpu, struct task_struct *p)
{
	return cpu;
}
#endif

#ifdef CONFIG_SMP

static const struct sched_class fair_sched_class;

static int
wake_affine(struct rq *rq, struct sched_domain *this_sd, struct rq *this_rq,
	    struct task_struct *p, int prev_cpu, int this_cpu, int sync,
	    int idx, unsigned long load, unsigned long this_load,
	    unsigned int imbalance)
{
	struct task_struct *curr = this_rq->curr;
	unsigned long tl = this_load;
	unsigned long tl_per_task;

	if (!(this_sd->flags & SD_WAKE_AFFINE))
		return 0;

	/*
	 * If the currently running task will sleep within
	 * a reasonable amount of time then attract this newly
	 * woken task:
	 */
	if (sync && curr->sched_class == &fair_sched_class) {
		if (curr->se.avg_overlap < sysctl_sched_migration_cost &&
				p->se.avg_overlap < sysctl_sched_migration_cost)
			return 1;
	}

	schedstat_inc(p, se.nr_wakeups_affine_attempts);
	tl_per_task = cpu_avg_load_per_task(this_cpu);

	/*
	 * If sync wakeup then subtract the (maximum possible)
	 * effect of the currently running task from the load
	 * of the current CPU:
	 */
	if (sync)
		tl -= current->se.load.weight;

	if ((tl <= load && tl + target_load(prev_cpu, idx) <= tl_per_task) ||
			100*(tl + p->se.load.weight) <= imbalance*load) {
		/*
		 * This domain has SD_WAKE_AFFINE and
		 * p is cache cold in this domain, and
		 * there is no bad imbalance.
		 */
		schedstat_inc(this_sd, ttwu_move_affine);
		schedstat_inc(p, se.nr_wakeups_affine);

		return 1;
	}
	return 0;
}

static int select_task_rq_fair(struct task_struct *p, int sync)
{
	struct sched_domain *sd, *this_sd = NULL;
	int prev_cpu, this_cpu, new_cpu;
	unsigned long load, this_load;
	struct rq *rq, *this_rq;
	unsigned int imbalance;
	int idx;

	prev_cpu	= task_cpu(p);
	rq		= task_rq(p);
	this_cpu	= smp_processor_id();
	this_rq		= cpu_rq(this_cpu);
	new_cpu		= prev_cpu;

	/*
	 * 'this_sd' is the first domain that both
	 * this_cpu and prev_cpu are present in:
	 */
	for_each_domain(this_cpu, sd) {
		if (cpu_isset(prev_cpu, sd->span)) {
			this_sd = sd;
			break;
		}
	}

	if (unlikely(!cpu_isset(this_cpu, p->cpus_allowed)))
		goto out;

	/*
	 * Check for affine wakeup and passive balancing possibilities.
	 */
	if (!this_sd)
		goto out;

	idx = this_sd->wake_idx;

	imbalance = 100 + (this_sd->imbalance_pct - 100) / 2;

	load = source_load(prev_cpu, idx);
	this_load = target_load(this_cpu, idx);

	if (wake_affine(rq, this_sd, this_rq, p, prev_cpu, this_cpu, sync, idx,
				     load, this_load, imbalance))
		return this_cpu;

	if (prev_cpu == this_cpu)
		goto out;

	/*
	 * Start passive balancing when half the imbalance_pct
	 * limit is reached.
	 */
	if (this_sd->flags & SD_WAKE_BALANCE) {
		if (imbalance*this_load <= 100*load) {
			schedstat_inc(this_sd, ttwu_move_balance);
			schedstat_inc(p, se.nr_wakeups_passive);
			return this_cpu;
		}
	}

out:
	return wake_idle(new_cpu, p);
}
#endif /* CONFIG_SMP */

static unsigned long wakeup_gran(struct sched_entity *se)
{
	unsigned long gran = sysctl_sched_wakeup_granularity;

	/*
	 * More easily preempt - nice tasks, while not making it harder for
	 * + nice tasks.
	 */
	gran = calc_delta_asym(sysctl_sched_wakeup_granularity, se);

	return gran;
}

/*
 * Should 'se' preempt 'curr'.
 *
 *             |s1
 *        |s2
 *   |s3
 *         g
 *      |<--->|c
 *
 *  w(c, s1) = -1
 *  w(c, s2) =  0
 *  w(c, s3) =  1
 *
 */
static int
wakeup_preempt_entity(struct sched_entity *curr, struct sched_entity *se)
{
	s64 gran, vdiff = curr->vruntime - se->vruntime;

	if (vdiff < 0)
		return -1;

	gran = wakeup_gran(curr);
	if (vdiff > gran)
		return 1;

	return 0;
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void check_preempt_wakeup(struct rq *rq, struct task_struct *p)
{
	struct task_struct *curr = rq->curr;
	struct cfs_rq *cfs_rq = task_cfs_rq(curr);
	struct sched_entity *se = &curr->se, *pse = &p->se;

	if (unlikely(rt_prio(p->prio))) {
		update_rq_clock(rq);
		update_curr(cfs_rq);
		resched_task(curr);
		return;
	}

	se->last_wakeup = se->sum_exec_runtime;
	if (unlikely(se == pse))
		return;

	rq->cfs_root.next = pse;

	/*
	 * Batch tasks do not preempt (their preemption is driven by
	 * the tick):
	 */
	if (unlikely(p->policy == SCHED_BATCH))
		return;

	if (!sched_feat(WAKEUP_PREEMPT))
		return;

	if (wakeup_preempt_entity(se, pse) == 1)
		resched_task(curr);
}

static struct sched_entity *
pick_next_entity(struct cfs_root_rq *cfs_r_rq)
{
	struct sched_entity *se = __pick_next_entity(cfs_r_rq);

	if (!cfs_r_rq->next)
		return se;

	if (wakeup_preempt_entity(cfs_r_rq->next, se))
		return se;

	return cfs_r_rq->next;
}

static struct task_struct *pick_next_task_fair(struct rq *rq)
{
	struct task_struct *p;
	struct cfs_root_rq *cfs_r_rq = &rq->cfs_root;
	struct sched_entity *se, *next;

	if (!first_fair(cfs_r_rq))
		return NULL;

	next = se = pick_next_entity(cfs_r_rq);
	for_each_sched_entity(se) {
		struct cfs_rq *cfs_rq = cfs_rq_of(se);
		set_next_entity(cfs_rq, se);
	}

	p = task_of(next);
	hrtick_start_fair(rq, p);

	return p;
}

/*
 * Account for a descheduled task:
 */
static void put_prev_task_fair(struct rq *rq, struct task_struct *prev)
{
	struct sched_entity *se = &prev->se;

	for_each_sched_entity(se)
		put_prev_entity(cfs_rq_of(se), se);
}

#ifdef CONFIG_SMP
/**************************************************
 * Fair scheduling class load-balancing methods:
 */

/*
 * Load-balancing iterator. Note: while the runqueue stays locked
 * during the whole iteration, the current task might be
 * dequeued so the iterator has to be dequeue-safe. Here we
 * achieve that by always pre-iterating before returning
 * the current task:
 */
static struct task_struct *
__load_balance_iterator(struct cfs_rq *cfs_rq, struct list_head *next)
{
	struct task_struct *p = NULL;
	struct sched_entity *se;

	if (next == &cfs_rq->tasks)
		return NULL;

	/* Skip over entities that are not tasks */
	do {
		se = list_entry(next, struct sched_entity, group_node);
		next = next->next;
	} while (next != &cfs_rq->tasks && !entity_is_task(se));

	if (next == &cfs_rq->tasks)
		return NULL;

	cfs_rq->balance_iterator = next;

	if (entity_is_task(se))
		p = task_of(se);

	return p;
}

static struct task_struct *load_balance_start_fair(void *arg)
{
	struct cfs_rq *cfs_rq = arg;

	return __load_balance_iterator(cfs_rq, cfs_rq->tasks.next);
}

static struct task_struct *load_balance_next_fair(void *arg)
{
	struct cfs_rq *cfs_rq = arg;

	return __load_balance_iterator(cfs_rq, cfs_rq->balance_iterator);
}

static unsigned long
__load_balance_fair(struct rq *this_rq, int this_cpu, struct rq *busiest,
		unsigned long max_load_move, struct sched_domain *sd,
		enum cpu_idle_type idle, int *all_pinned, int *this_best_prio,
		struct cfs_rq *cfs_rq)
{
	struct rq_iterator cfs_rq_iterator;

	cfs_rq_iterator.start = load_balance_start_fair;
	cfs_rq_iterator.next = load_balance_next_fair;
	cfs_rq_iterator.arg = cfs_rq;

	return balance_tasks(this_rq, this_cpu, busiest,
			max_load_move, sd, idle, all_pinned,
			this_best_prio, &cfs_rq_iterator);
}

#ifdef CONFIG_FAIR_GROUP_SCHED
static unsigned long
load_balance_fair(struct rq *this_rq, int this_cpu, struct rq *busiest,
		  unsigned long max_load_move,
		  struct sched_domain *sd, enum cpu_idle_type idle,
		  int *all_pinned, int *this_best_prio)
{
	long rem_load_move = max_load_move;
	int busiest_cpu = cpu_of(busiest);
	struct task_group *tg;

	rcu_read_lock();
	list_for_each_entry(tg, &task_groups, list) {
		long imbalance;
		unsigned long this_weight, busiest_weight;
		long rem_load, max_load, moved_load;

		/*
		 * empty group
		 */
		if (!aggregate(tg, sd)->task_weight)
			continue;

		rem_load = rem_load_move * aggregate(tg, sd)->rq_weight;
		rem_load /= aggregate(tg, sd)->load + 1;

		this_weight = tg->cfs_rq[this_cpu]->task_weight;
		busiest_weight = tg->cfs_rq[busiest_cpu]->task_weight;

		imbalance = (busiest_weight - this_weight) / 2;

		if (imbalance < 0)
			imbalance = busiest_weight;

		max_load = max(rem_load, imbalance);
		moved_load = __load_balance_fair(this_rq, this_cpu, busiest,
				max_load, sd, idle, all_pinned, this_best_prio,
				tg->cfs_rq[busiest_cpu]);

		if (!moved_load)
			continue;

		move_group_shares(tg, sd, busiest_cpu, this_cpu);

		moved_load *= aggregate(tg, sd)->load;
		moved_load /= aggregate(tg, sd)->rq_weight + 1;

		rem_load_move -= moved_load;
		if (rem_load_move < 0)
			break;
	}
	rcu_read_unlock();

	return max_load_move - rem_load_move;
}
#else
static unsigned long
load_balance_fair(struct rq *this_rq, int this_cpu, struct rq *busiest,
		  unsigned long max_load_move,
		  struct sched_domain *sd, enum cpu_idle_type idle,
		  int *all_pinned, int *this_best_prio)
{
	return __load_balance_fair(this_rq, this_cpu, busiest,
			max_load_move, sd, idle, all_pinned,
			this_best_prio, &busiest->cfs);
}
#endif

static int
move_one_task_fair(struct rq *this_rq, int this_cpu, struct rq *busiest,
		   struct sched_domain *sd, enum cpu_idle_type idle)
{
	struct cfs_rq *busy_cfs_rq;
	struct rq_iterator cfs_rq_iterator;

	cfs_rq_iterator.start = load_balance_start_fair;
	cfs_rq_iterator.next = load_balance_next_fair;

	for_each_leaf_cfs_rq(busiest, busy_cfs_rq) {
		/*
		 * pass busy_cfs_rq argument into
		 * load_balance_[start|next]_fair iterators
		 */
		cfs_rq_iterator.arg = busy_cfs_rq;
		if (iter_move_one_task(this_rq, this_cpu, busiest, sd, idle,
				       &cfs_rq_iterator))
		    return 1;
	}

	return 0;
}
#endif

/*
 * scheduler tick hitting a task of our scheduling class:
 */
static void task_tick_fair(struct rq *rq, struct task_struct *curr, int queued)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &curr->se;

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		entity_tick(cfs_rq, se, queued);
	}
}

#define swap(a, b) do { typeof(a) tmp = (a); (a) = (b); (b) = tmp; } while (0)

/*
 * Share the fairness runtime between parent and child, thus the
 * total amount of pressure for CPU stays equal - new tasks
 * get a chance to run but frequent forkers are not allowed to
 * monopolize the CPU. Note: the parent runqueue is locked,
 * the child is not running yet.
 */
static void task_new_fair(struct rq *rq, struct task_struct *p)
{
	struct cfs_rq *cfs_rq = task_cfs_rq(p);
	struct sched_entity *se = &p->se, *curr = cfs_rq->curr;
	int this_cpu = smp_processor_id();

	sched_info_queued(p);

	update_curr(cfs_rq);
	place_entity(cfs_rq, se, 1);

	/* 'curr' will be NULL if the child belongs to a different group */
	if (sysctl_sched_child_runs_first && this_cpu == task_cpu(p) &&
			curr && curr->vruntime < se->vruntime) {
		/*
		 * Upon rescheduling, sched_class::put_prev_task() will place
		 * 'current' within the tree based on its new key value.
		 */
		swap(curr->vruntime, se->vruntime);
	}

	enqueue_task_fair(rq, p, 0);
	resched_task(rq->curr);
}

/*
 * Priority of the task has changed. Check to see if we preempt
 * the current task.
 */
static void prio_changed_fair(struct rq *rq, struct task_struct *p,
			      int oldprio, int running)
{
	/*
	 * Reschedule if we are currently running on this runqueue and
	 * our priority decreased, or if we are not currently running on
	 * this runqueue and our priority is higher than the current's
	 */
	if (running) {
		if (p->prio > oldprio)
			resched_task(rq->curr);
	} else
		check_preempt_curr(rq, p);
}

/*
 * We switched to the sched_fair class.
 */
static void switched_to_fair(struct rq *rq, struct task_struct *p,
			     int running)
{
	/*
	 * We were most likely switched from sched_rt, so
	 * kick off the schedule if running, otherwise just see
	 * if we can still preempt the current task.
	 */
	if (running)
		resched_task(rq->curr);
	else
		check_preempt_curr(rq, p);
}

/* Account for a task changing its policy or group.
 *
 * This routine is mostly called to set cfs_rq->curr field when a task
 * migrates between groups/classes.
 */
static void set_curr_task_fair(struct rq *rq)
{
	struct sched_entity *se = &rq->curr->se;

	for_each_sched_entity(se)
		set_next_entity(cfs_rq_of(se), se);
}

/*
 * All the scheduling class methods:
 */
static const struct sched_class fair_sched_class = {
	.next			= &idle_sched_class,
	.enqueue_task		= enqueue_task_fair,
	.dequeue_task		= dequeue_task_fair,
	.yield_task		= yield_task_fair,
#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_fair,
#endif /* CONFIG_SMP */

	.check_preempt_curr	= check_preempt_wakeup,

	.pick_next_task		= pick_next_task_fair,
	.put_prev_task		= put_prev_task_fair,

#ifdef CONFIG_SMP
	.load_balance		= load_balance_fair,
	.move_one_task		= move_one_task_fair,
#endif

	.set_curr_task          = set_curr_task_fair,
	.task_tick		= task_tick_fair,
	.task_new		= task_new_fair,

	.prio_changed		= prio_changed_fair,
	.switched_to		= switched_to_fair,
};

#ifdef CONFIG_SCHED_DEBUG
static void print_cfs_stats(struct seq_file *m, int cpu)
{
	struct cfs_rq *cfs_rq;

	rcu_read_lock();
	for_each_leaf_cfs_rq(cpu_rq(cpu), cfs_rq)
		print_cfs_rq(m, cpu, cfs_rq);
	rcu_read_unlock();
}
#endif
