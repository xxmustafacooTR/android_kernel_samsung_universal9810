#include "../sched.h"
#include "../pelt.h"
#include "ems.h"

#include <trace/events/ems.h>
#include <trace/events/ems_debug.h>

struct frt_dom {
	unsigned int		coverage_ratio;
	unsigned int		active_ratio;
	int			coregroup;
	struct cpumask		cpus;

	struct list_head	list;
	struct frt_dom		*next;
	struct frt_dom		*prev;
	/* kobject for sysfs group */
	struct kobject		kobj;
};

struct cpumask available_mask;
unsigned int frt_disable_cpufreq;

LIST_HEAD(frt_list);
DEFINE_RAW_SPINLOCK(frt_lock);
DEFINE_PER_CPU_SHARED_ALIGNED(struct frt_dom *, frt_rqs);
bool frt_initialized;

/*
 * Optional action to be done while updating the load average
 */
#define UPDATE_TG	0x1
#define SKIP_AGE_LOAD	0x2
#define DO_ATTACH	0x4

#define ENABLE_CACHE_HOT 0

#define RATIO_SCALE_SHIFT	10
#define ratio_scale(v, r) (((v) * (r) * 10) >> RATIO_SCALE_SHIFT)

#define cpu_selected(cpu)	(cpu >= 0)
#define tsk_cpus_allowed(tsk)	(&(tsk)->cpus_allowed)

#define for_each_sched_rt_entity(rt_se) \
	for (; rt_se; rt_se = rt_se->parent)

#define add_positive(_ptr, _val) do {                           \
	typeof(_ptr) ptr = (_ptr);                              \
	typeof(_val) val = (_val);                              \
	typeof(*ptr) res, var = READ_ONCE(*ptr);                \
								\
	res = var + val;                                        \
								\
	if (val < 0 && res > var)                               \
		res = 0;                                        \
								\
	WRITE_ONCE(*ptr, res);                                  \
} while (0)

#define sub_positive(_ptr, _val) do {				\
	typeof(_ptr) ptr = (_ptr);				\
	typeof(*ptr) val = (_val);				\
	typeof(*ptr) res, var = READ_ONCE(*ptr);		\
	res = var - val;					\
	if (res > var)						\
		res = 0;					\
	WRITE_ONCE(*ptr, res);					\
} while (0)

#define rt_entity_is_task(rt_se) (!(rt_se)->my_q)

/*
 * Signed add and clamp on underflow.
 *
 * Explicitly do a load-store to ensure the intermediate value never hits
 * memory. This allows lockless observations without ever seeing the negative
 * values.
 */
static inline struct task_struct *rt_task_of(struct sched_rt_entity *rt_se)
{
#ifdef CONFIG_SCHED_DEBUG
	WARN_ON_ONCE(!rt_entity_is_task(rt_se));
#endif
	return container_of(rt_se, struct task_struct, rt);
}

static inline struct rq *rq_of_rt_rq(struct rt_rq *rt_rq)
{
	return rt_rq->rq;
}

static inline struct rt_rq *rt_rq_of_se(struct sched_rt_entity *rt_se)
{
	return rt_se->rt_rq;
}

static inline struct rq *rq_of_rt_se(struct sched_rt_entity *rt_se)
{
	struct rt_rq *rt_rq = rt_se->rt_rq;

	return rt_rq->rq;
}

static struct kobject *frt_kobj;
struct frt_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *, char *);
	ssize_t (*store)(struct kobject *, const char *, size_t count);
};

#define frt_attr_rw(_name)				\
static struct frt_attr _name##_attr =			\
__ATTR(_name, 0644, show_##_name, store_##_name)

#define frt_show(_name)								\
static ssize_t show_##_name(struct kobject *k, char *buf)			\
{										\
	struct frt_dom *dom = container_of(k, struct frt_dom, kobj);		\
										\
	return sprintf(buf, "%u\n", (unsigned int)dom->_name);		\
}

#define frt_store(_name, _type, _max)						\
static ssize_t store_##_name(struct kobject *k, const char *buf, size_t count)	\
{										\
	unsigned int val;							\
	struct frt_dom *dom = container_of(k, struct frt_dom, kobj);		\
										\
	if (!sscanf(buf, "%u", &val))						\
		return -EINVAL;							\
										\
	val = val > _max ? _max : val;						\
	dom->_name = (_type)val;						\
										\
	return count;								\
}

frt_store(coverage_ratio, int, 100);
frt_show(coverage_ratio);
frt_attr_rw(coverage_ratio);
frt_store(active_ratio, int, 100);
frt_show(active_ratio);
frt_attr_rw(active_ratio);

static ssize_t show(struct kobject *kobj, struct attribute *at, char *buf)
{
	struct frt_attr *frtattr = container_of(at, struct frt_attr, attr);

	return frtattr->show(kobj, buf);
}

static ssize_t store(struct kobject *kobj, struct attribute *at,
		     const char *buf, size_t count)
{
	struct frt_attr *frtattr = container_of(at, struct frt_attr, attr);

	return frtattr->store(kobj, buf, count);
}

static const struct sysfs_ops frt_sysfs_ops = {
	.show	= show,
	.store	= store,
};

static struct attribute *dom_frt_attrs[] = {
	&coverage_ratio_attr.attr,
	&active_ratio_attr.attr,
	NULL
};
static struct kobj_type ktype_frt = {
	.sysfs_ops	= &frt_sysfs_ops,
	.default_attrs	= dom_frt_attrs,
};

static ssize_t store_disable_cpufreq(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf,
		size_t count)
{
	unsigned int val;
	if (!sscanf(buf, "%u", &val))
		return -EINVAL;
	frt_disable_cpufreq = val;
	return count;
}

static ssize_t show_disable_cpufreq(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", frt_disable_cpufreq);
}

static struct kobj_attribute disable_cpufreq_attr =
__ATTR(disable_cpufreq, 0644, show_disable_cpufreq, store_disable_cpufreq);

static struct attribute *frt_attrs[] = {
	&disable_cpufreq_attr.attr,
	NULL,
};

static const struct attribute_group frt_group = {
	.attrs = frt_attrs,
};

static int find_prefer_cpu(struct task_struct *task, bool *reverse)
{
	int cpu, allowed_cpu = 0;
	unsigned int coverage_thr;
	struct frt_dom *dom;

	list_for_each_entry(dom, &frt_list, list) {
		unsigned long capacity;
		struct cpumask active_cpus;
		int first_cpu;

		cpumask_and(&active_cpus, &dom->cpus, cpu_active_mask);
		first_cpu = cpumask_first(&active_cpus);
		/* all cpus of domain are off */
		if (first_cpu == NR_CPUS)
			continue;

		capacity = capacity_cpu(first_cpu, 0);
		coverage_thr = ratio_scale(capacity, dom->coverage_ratio);

		if (list_last_entry(&frt_list, struct frt_dom, list))
			*reverse = true;

		for_each_cpu_and(cpu, &task->cpus_allowed, &dom->cpus) {
			allowed_cpu = cpu;
			if (task->rt.avg.util_avg < coverage_thr)
				return allowed_cpu;
		}
	}
	return allowed_cpu;
}

static const struct cpumask *get_available_cpus(void)
{
	return &available_mask;
}

static int frt_mode_update_callback(struct notifier_block *nb,
				unsigned long val, void *v)
{
	struct emstune_set *cur_set = (struct emstune_set *)v;
	struct frt_dom *dom;

	list_for_each_entry(dom, &frt_list, list) {
		int cpu = cpumask_first(&dom->cpus);

		dom->active_ratio = cur_set->frt.active_ratio[cpu];
		dom->coverage_ratio = cur_set->frt.coverage_ratio[cpu];
	}

	return NOTIFY_OK;
}

static struct notifier_block frt_mode_update_notifier = {
	.notifier_call = frt_mode_update_callback,
};

static int __init frt_sysfs_init(void)
{
	struct frt_dom *dom;

	if (list_empty(&frt_list))
		return 0;

	frt_kobj = kobject_create_and_add("frt", ems_kobj);
	if (!frt_kobj)
		goto out;

	/* Add frt sysfs node for each coregroup */
	list_for_each_entry(dom, &frt_list, list) {
		if (kobject_init_and_add(&dom->kobj, &ktype_frt,
				frt_kobj, "coregroup%d", dom->coregroup))
			goto out;
	}

	/* add frt syfs for global control */
	if (sysfs_create_group(frt_kobj, &frt_group))
		goto out;

	emstune_register_mode_update_notifier(&frt_mode_update_notifier);

	return 0;

out:
	pr_err("FRT(%s): failed to create sysfs node\n", __func__);
	return -EINVAL;
}

static void frt_parse_dt(struct device_node *dn, struct frt_dom *dom, int cnt)
{
	struct device_node *frt, *coregroup;
	char name[15];

	frt = of_get_child_by_name(dn, "frt");
	if (!frt)
		goto disable;

	snprintf(name, sizeof(name), "coregroup%d", cnt);
	coregroup = of_get_child_by_name(frt, name);
	if (!coregroup)
		goto disable;
	dom->coregroup = cnt;

	if (of_property_read_u32(coregroup, "coverage-ratio", &dom->coverage_ratio))
		return;

	if (of_property_read_u32(coregroup, "active-ratio", &dom->active_ratio))
		return;

	return;

disable:
	dom->coregroup = cnt;
	dom->coverage_ratio = 100;
	dom->active_ratio = 100;
	pr_err("FRT(%s): failed to parse frt node\n", __func__);
}

static int __init init_frt(void)
{
	struct frt_dom *cur, *prev = NULL, *head = NULL;
	struct device_node *dn;
	int cpu, tcpu, cnt = 0;

	dn = of_find_node_by_path("/ems");
	if (!dn)
		return 0;

	INIT_LIST_HEAD(&frt_list);
	cpumask_setall(&available_mask);

	for_each_possible_cpu(cpu) {
		if (cpu != cpumask_first(cpu_coregroup_mask(cpu)))
			continue;

		cur = kzalloc(sizeof(struct frt_dom), GFP_KERNEL);
		if (!cur) {
			pr_err("FRT(%s): failed to allocate dom\n", __func__);
			goto put_node;
		}

		cpumask_copy(&cur->cpus, cpu_coregroup_mask(cpu));

		frt_parse_dt(dn, cur, cnt++);

		/* make linke between rt domains */
		if (head == NULL)
			head = cur;

		if (prev) {
			prev->next = cur;
			cur->prev = prev;
		}
		cur->next = head;
		head->prev = cur;

		prev = cur;

		for_each_cpu(tcpu, &cur->cpus)
			per_cpu(frt_rqs, tcpu) = cur;

		list_add_tail(&cur->list, &frt_list);
	}
	frt_sysfs_init();

	frt_initialized = true;
	pr_info("%s: frt initialized complete!\n", __func__);

put_node:
	of_node_put(dn);

	return 0;

} late_initcall(init_frt);

/*****************************************************************************/
/*				CGROUP for FRT				     */
/*****************************************************************************/
#ifdef CONFIG_RT_GROUP_SCHED
void frt_attach_task_rt_rq(struct task_struct *p);
static void task_set_group_rt(struct task_struct *p)
{
	set_task_rq(p, task_cpu(p));
}

static void task_move_group_rt(struct task_struct *p)
{
	frt_detach_task_rt_rq(p);
	set_task_rq(p, task_cpu(p));

#ifdef CONFIG_SMP
	/* Tell se's rt_rq has been changed -- migrated */
	p->se.avg.last_update_time = 0;
#endif
	frt_attach_task_rt_rq(p);
}

void frt_task_change_group_rt(struct task_struct *p, int type)
{
	switch (type) {
	case TASK_SET_GROUP:
		task_set_group_rt(p);
		break;

	case TASK_MOVE_GROUP:
		task_move_group_rt(p);
		break;
	}
}

static inline struct rt_rq *group_rt_rq(struct sched_rt_entity *rt_se)
{
	return rt_se->my_q;
}

static inline void
update_tg_rt_util(struct rt_rq *rt_rq, struct sched_rt_entity *se, struct rt_rq *grt_rq)
{
	long delta = grt_rq->avg.util_avg - se->avg.util_avg;

	/* Nothing to update */
	if (!delta)
		return;

	/*
	 * The relation between sum and avg is:
	 *
	 *   LOAD_AVG_MAX - 1024 + sa->period_contrib
	 *
	 * however, the PELT windows are not aligned between grq and gse.
	 */

	/* Set new sched_rt_entity's utilization */
	se->avg.util_avg = grt_rq->avg.util_avg;
	se->avg.util_sum = se->avg.util_avg * LOAD_AVG_MAX;

	/* Update parent rt_rq utilization */
	add_positive(&rt_rq->avg.util_avg, delta);
	rt_rq->avg.util_sum = rt_rq->avg.util_avg * LOAD_AVG_MAX;
}

static inline int propagate_rt_entity_load_avg(struct sched_rt_entity *se)
{
	struct rt_rq *rt_rq, *grt_rq;

	if (entity_is_task(se))
		return 0;

	grt_rq = group_rt_rq(se);
	if (!grt_rq->propagate)
		return 0;

	grt_rq->propagate = 0;

	rt_rq = rt_rq_of_se(se);

	update_tg_rt_util(rt_rq, se, grt_rq);

	trace_frt_load_rt_rq(rt_rq);
	trace_frt_load_rt_se(se);

	return 1;
}

/*
 * Propagate the changes of the sched_rt_entity across the tg tree to make it
 * visible to the root
 */
static void propagate_entity_rt_rq(struct sched_rt_entity *se)
{
	struct rt_rq *rt_rq;

	/* Start to propagate at parent */
	se = se->parent;

	for_each_sched_rt_entity(se) {
		rt_rq = rt_rq_of_se(se);

		if (rt_rq->rt_throttled)
			break;

		frt_update_load_avg(rt_rq, se, UPDATE_TG);
	}
}
#else /* CONFIG_RT_GROUP_SCHED */
static inline int propagate_rt_entity_load_avg(struct sched_rt_entity *se) { return 0; };
static void propagate_entity_rt_rq(struct sched_rt_entity *se) { }
#endif /* CONFIG_RT_GROUP_SCHED */


/*****************************************************************************/
/*				PELT FOR FRT				     */
/*****************************************************************************/
void frt_init_entity_runnable_average(struct sched_rt_entity *rt_se)
{
	struct sched_avg *sa = &rt_se->avg;

	memset(sa, 0, sizeof(*sa));
}

static void rt_rq_util_change(struct rt_rq *rt_rq)
{
	if (&this_rq()->rt == rt_rq)
		cpufreq_update_util(rt_rq->rq, 0);
}

static __always_inline u32
accumulate_sum(u64 delta, int cpu, struct sched_avg *sa,
               unsigned long load, unsigned long runnable, int running)
{
        unsigned long scale_freq, scale_cpu;
        u32 contrib = (u32)delta; /* p == 0 -> delta < 1024 */
        u64 periods;

        scale_freq = arch_scale_freq_capacity(cpu);
        scale_cpu = arch_scale_cpu_capacity(NULL, cpu);

        delta += sa->period_contrib;
        periods = delta / 1024; /* A period is 1024us (~1ms) */

        /*
         * Step 1: decay old *_sum if we crossed period boundaries.
         */
        if (periods) {
                sa->load_sum = decay_load(sa->load_sum, periods);
                sa->util_sum = decay_load((u64)(sa->util_sum), periods);

                /*
                 * Step 2
                 */
                delta %= 1024;
                contrib = __accumulate_pelt_segments(periods,
                                1024 - sa->period_contrib, delta);
        }
        sa->period_contrib = delta;

        contrib = cap_scale(contrib, scale_freq);
        if (load)
                sa->load_sum += load * contrib;
        if (running)
                sa->util_sum += contrib * scale_cpu;

        return periods;
}

static __always_inline int
___update_load_sum(u64 now, int cpu, struct sched_avg *sa, int running)
{
	/* RT doesn't need runnable information */
	int load = 0, runnable = 0;
	u64 delta;

	delta = now - sa->last_update_time;
	/*
	 * This should only happen when time goes backwards, which it
	 * unfortunately does during sched clock init when we swap over to TSC.
	 */
	if ((s64)delta < 0) {
		sa->last_update_time = now;
		return 0;
	}

	/*
	 * Use 1024ns as the unit of measurement since it's a reasonable
	 * approximation of 1us and fast to compute.
	 */
	delta >>= 10;
	if (!delta)
		return 0;

	sa->last_update_time += delta << 10;

	/*
	 * Now we know we crossed measurement unit boundaries. The *_avg
	 * accrues by two steps:
	 *
	 * Step 1: accumulate *_sum since last_update_time. If we haven't
	 * crossed period boundaries, finish.
	 */
	if (!accumulate_sum(delta, cpu, sa, load, runnable, running))
		return 0;

	return 1;
}
static __always_inline void
___update_load_avg(struct sched_avg *sa)
{
	u32 divider = LOAD_AVG_MAX - 1024 + sa->period_contrib;
	/* Step 2: update *_avg. */
	WRITE_ONCE(sa->util_avg, sa->util_sum / divider);
}

/*
 * rt_rq:
 *
 *   util_sum = \Sum se->avg.util_sum but se->avg.util_sum is not tracked
 *   util_sum = cpu_scale * load_sum
 *
 */
static int __update_load_avg_rt_rq(u64 now, int cpu, struct rt_rq *rt_rq)
{
	int running = rt_rq->curr != NULL;

	if (___update_load_sum(now, cpu, &rt_rq->avg, running)) {

		___update_load_avg(&rt_rq->avg);

		trace_frt_load_rt_rq(rt_rq);

		return 1;
	}

	return 0;
}

int frt_update_rt_rq_load_avg(struct rq *rq)
{
	struct rt_rq *rt_rq = &rq->rt;
	unsigned long removed_util = 0;
	struct sched_avg *sa = &rt_rq->avg;
	int decayed = 0;

	if (rt_rq->removed.nr) {
		unsigned long r;
		u32 divider = LOAD_AVG_MAX - 1024 + sa->period_contrib;

		raw_spin_lock(&rt_rq->removed.lock);
		swap(rt_rq->removed.util_avg, removed_util);
		rt_rq->removed.nr = 0;
		raw_spin_unlock(&rt_rq->removed.lock);

		r = removed_util;
		sub_positive(&sa->util_avg, r);
		sub_positive(&sa->util_sum, r * divider);

		decayed = 1;
	}

	decayed |= __update_load_avg_rt_rq(rq_clock_task(rq), cpu_of(rq), rt_rq);

#ifndef CONFIG_64BIT
	smp_wmb();
	rt_rq->load_last_update_time_copy = sa->last_update_time;
#endif

	if (decayed)
		rt_rq_util_change(rt_rq);

	return decayed;
}

static int __update_load_avg_blocked_rt_se(u64 now, int cpu, struct sched_rt_entity *se)
{
	if (___update_load_sum(now, cpu, &se->avg, 0)) {
		___update_load_avg(&se->avg);

		trace_frt_load_rt_se(se);

		return 1;
	}

	return 0;
}

static int __update_load_avg_rt_se(u64 now, int cpu, struct rt_rq *rt_rq, struct sched_rt_entity *se)
{
	if (___update_load_sum(now, cpu, &se->avg, rt_rq->curr == se)) {
		___update_load_avg(&se->avg);

		trace_frt_load_rt_se(se);

		return 1;
	}

	return 0;
}

static void attach_rt_entity_load_avg(struct rt_rq *rt_rq,
			struct sched_rt_entity *se)
{
	u32 divider = LOAD_AVG_MAX - 1024 + rt_rq->avg.period_contrib;

	/*
	 * When we attach the @se to the @rt_rq, we must align the decay
	 * window because without that, really weird and wonderful things can
	 * happen.
	 *
	 * XXX illustrate
	 */
	se->avg.last_update_time = rt_rq->avg.last_update_time;
	se->avg.period_contrib = rt_rq->avg.period_contrib;

	/*
	 * Hell(o) Nasty stuff.. we need to recompute _sum based on the new
	 * period_contrib. This isn't strictly correct, but since we're
	 * entirely outside of the PELT hierarchy, nobody cares if we truncate
	 * _sum a little.
	 */
	se->avg.util_sum = se->avg.util_avg * divider;

	rt_rq->avg.util_avg += se->avg.util_avg;
	rt_rq->avg.util_sum += se->avg.util_sum;

	trace_frt_load_rt_rq(rt_rq);

	rt_rq_util_change(rt_rq);
}

/**
 * detach_rt_entity_load_avg - detach this entity from its rt_rq load avg
 * @rt_rq: rt_rq to detach from
 * @se: sched_rt_entity to detach
 *
 * Must call frt_update_rt_rq_load_avg() before this, since we rely on
 * rt_rq->avg.last_update_time being current.
 */
static void detach_rt_entity_load_avg(struct rt_rq *rt_rq, struct sched_rt_entity *se)
{
	sub_positive(&rt_rq->avg.util_avg, se->avg.util_avg);
	sub_positive(&rt_rq->avg.util_sum, se->avg.util_sum);

	rt_rq_util_change(rt_rq);

	trace_frt_load_rt_rq(rt_rq);
}

void frt_update_load_avg(struct rt_rq *rt_rq, struct sched_rt_entity *se, int flags)
{
	struct rq *rq = rq_of_rt_rq(rt_rq);
	u64 now = rq_clock_task(rq);
	int cpu = cpu_of(rq);
	int decayed;

	/*
	 * Track task load average for carrying it to new CPU after migrated, and
	 * track group sched_rt_entity load average for task_h_load calc in migration
	 */
	if (se->avg.last_update_time && !(flags & SKIP_AGE_LOAD))
		__update_load_avg_rt_se(now, cpu, rt_rq, se);

	decayed  = frt_update_rt_rq_load_avg(rq);
	decayed |= propagate_rt_entity_load_avg(se);

	if (!se->avg.last_update_time && (flags & DO_ATTACH)) {

		/*
		 * DO_ATTACH means we're here from enqueue_entity().
		 * !last_update_time means we've passed through
		 * migrate_task_rq_fair() indicating we migrated.
		 *
		 * IOW we're enqueueing a task on a new CPU.
		 */
		attach_rt_entity_load_avg(rt_rq, se);

	}
}

static inline int weight_from_rtprio(int prio)
{
	int idx = (prio >> 1);

	if (!rt_prio(prio))
		return sched_prio_to_weight[prio - MAX_RT_PRIO];

	if ((idx << 1) == prio)
		return rtprio_to_weight[idx];
	else
		return ((rtprio_to_weight[idx] + rtprio_to_weight[idx+1]) >> 1);
}

static inline unsigned long rt_task_util(struct task_struct *p)
{
	return READ_ONCE(p->rt.avg.util_avg);
}

static inline unsigned long rt_cpu_util(int cpu)
{
	return READ_ONCE(cpu_rq(cpu)->rt.avg.util_avg);
}

extern unsigned long cpu_util(int cpu);
extern unsigned long capacity_orig_of(int cpu);
static inline unsigned long frt_cpu_util_with(int cpu, struct task_struct *p)
{
	unsigned int util;

	util = rt_cpu_util(cpu) + cpu_util(cpu);
	if (cpu == task_cpu(p))
		sub_positive(&util, rt_task_util(p));

	return min_t(unsigned long, util, capacity_orig_of(cpu));
}

static inline unsigned long frt_cpu_util(int cpu)
{
	unsigned int util;
	util = rt_cpu_util(cpu) + cpu_util(cpu);
	return min_t(unsigned long, util, capacity_orig_of(cpu));
}

/*
 * Called within set_task_rq() right before setting a task's cpu. The
 * caller only guarantees p->pi_lock is held; no other assumptions,
 * including the state of rq->lock, should be made.
 */
void frt_set_task_rq_rt(struct sched_rt_entity *rt_se,
				    struct rt_rq *prev, struct rt_rq *next)
{
	u64 p_last_update_time;
	u64 n_last_update_time;

	if (!sched_feat(ATTACH_AGE_LOAD))
		return;
	/*
	 * We are supposed to update the task to "current" time, then its up to
	 * date and ready to go to new CPU/rt_rq. But we have difficulty in
	 * getting what current time is, so simply throw away the out-of-date
	 * time. This will result in the wakee task is less decayed, but giving
	 * the wakee more load sounds not bad.
	 */
	if (!(rt_se->avg.last_update_time && prev))
		return;
#ifndef CONFIG_64BIT
	{
		u64 p_last_update_time_copy;
		u64 n_last_update_time_copy;

		do {
			p_last_update_time_copy = prev->load_last_update_time_copy_rt;
			n_last_update_time_copy = next->load_last_update_time_copy_rt;

			smp_rmb();

			p_last_update_time = prev->avg.last_update_time;
			n_last_update_time = next->avg.last_update_time;

		} while (p_last_update_time != p_last_update_time_copy ||
			 n_last_update_time != n_last_update_time_copy);
	}
#else
	p_last_update_time = prev->avg.last_update_time;
	n_last_update_time = next->avg.last_update_time;
#endif
	__update_load_avg_blocked_rt_se(p_last_update_time, cpu_of(rq_of_rt_rq(prev)), rt_se);

	rt_se->avg.last_update_time = n_last_update_time;
}

#ifndef CONFIG_64BIT
static inline u64 rt_rq_last_update_time(struct rt_rq *rt_rq)
{
	u64 last_update_time_copy;
	u64 last_update_time;
	struct rq *rq = &rq_of_rt_rq(rt_rq);

	do {
		last_update_time_copy = rq->load_last_update_time_copy_rt;
		smp_rmb();
		last_update_time = rt_rq->avg.last_update_time;
	} while (last_update_time != last_update_time_copy);

	return last_update_time;
}
#else
static inline u64 rt_rq_last_update_time(struct rt_rq *rt_rq)
{
	return rt_rq->avg.last_update_time;
}
#endif

/*
 * Synchronize entity load avg of dequeued entity without locking
 * the previous rq.
 */
static void sync_rt_entity_load_avg(struct sched_rt_entity *rt_se)
{
	struct rt_rq *rt_rq = rt_rq_of_se(rt_se);
	u64 last_update_time;

	last_update_time = rt_rq_last_update_time(rt_rq);
	__update_load_avg_rt_se(last_update_time,
		cpu_of(rq_of_rt_rq(rt_rq)), rt_rq, rt_se);
}

/*
 * Task first catches up with rt_rq, and then subtract
 * itself from the rt_rq (task must be off the queue now).
 */
static void remove_rt_entity_load_avg(struct sched_rt_entity *rt_se)
{
	struct rt_rq *rt_rq = rt_rq_of_se(rt_se);
	unsigned long flags;

	/*
	 * tasks cannot exit without having gone through wake_up_new_task() ->
	 * post_init_entity_util_avg() which will have added things to the
	 * rt_rq, so we can remove unconditionally.
	 *
	 * Similarly for groups, they will have passed through
	 * post_init_entity_util_avg() before unregister_sched_fair_group()
	 * calls this.
	 */

	sync_rt_entity_load_avg(rt_se);
	raw_spin_lock_irqsave(&rt_rq->removed.lock, flags);
	++rt_rq->removed.nr;
	rt_rq->removed.util_avg	+= rt_se->avg.util_avg;
	raw_spin_unlock_irqrestore(&rt_rq->removed.lock, flags);
}

static void detach_entity_rt_rq(struct sched_rt_entity *se)
{
	struct rt_rq *rt_rq = rt_rq_of_se(se);

	/* Catch up with the rt_rq and remove our load when we leave */
	frt_update_load_avg(rt_rq, se, 0);
	detach_rt_entity_load_avg(rt_rq, se);
	propagate_entity_rt_rq(se);
}

static void attach_rt_entity_rt_rq(struct sched_rt_entity *se)
{
	struct rt_rq *rt_rq = rt_rq_of_se(se);

	/* Synchronize entity with its cfs_rq */
	frt_update_load_avg(rt_rq, se, sched_feat(ATTACH_AGE_LOAD) ? 0 : SKIP_AGE_LOAD);
	attach_rt_entity_load_avg(rt_rq, se);
	propagate_entity_rt_rq(se);
}

void frt_attach_task_rt_rq(struct task_struct *p)
{
	struct sched_rt_entity *rt_se = &p->rt;

	attach_rt_entity_rt_rq(rt_se);
}

void frt_detach_task_rt_rq(struct task_struct *p)
{
	struct sched_rt_entity *se = &p->rt;

	detach_entity_rt_rq(se);
}

void frt_migrate_task_rq_rt(struct task_struct *p, int new_cpu)
{
	if (p->on_rq == TASK_ON_RQ_MIGRATING) {
		/*
		 * In case of TASK_ON_RQ_MIGRATING we in fact hold the 'old'
		 * rq->lock and can modify state directly.
		 */
		lockdep_assert_held(&task_rq(p)->lock);
		detach_entity_rt_rq(&p->rt);

	} else {
		/*
		 * We are supposed to update the task to "current" time, then
		 * its up to date and ready to go to new CPU/rt_rq. But we
		 * have difficulty in getting what current time is, so simply
		 * throw away the out-of-date time. This will result in the
		 * wakee task is less decayed, but giving the wakee more load
		 * sounds not bad.
		 */
		remove_rt_entity_load_avg(&p->rt);
	}

	/* Tell new CPU we are migrated */
	p->rt.avg.last_update_time = 0;
}

void frt_task_dead_rt(struct task_struct *p)
{
	remove_rt_entity_load_avg(&p->rt);
}

void frt_init_rt_rq_load(struct rt_rq *rt_rq)
{
#ifdef CONFIG_SMP
	raw_spin_lock_init(&rt_rq->removed.lock);
#endif
}

void frt_store_sched_avg(struct task_struct *p, struct sched_avg *sa)
{
	p->sa_box.last_update_time = sa->last_update_time;
	p->sa_box.util_avg = sa->util_avg;
	p->sa_box.util_sum = sa->util_sum;
}

void frt_sync_sched_avg(struct task_struct *p, struct sched_avg *sa)
{
	sa->last_update_time = p->sa_box.last_update_time;
	sa->util_avg = p->sa_box.util_avg;
	sa->util_sum = p->sa_box.util_sum;
}
/*****************************************************************************/
/*				SELECT WAKEUP CPU			     */
/*****************************************************************************/
static inline void frt_set_victim_flag(struct task_struct *p)
{
	p->victim_flag = 1;
}

void frt_clear_victim_flag(struct task_struct *p)
{
	p->victim_flag = 0;
}

bool frt_test_victim_flag(struct task_struct *p)
{
	if (p->victim_flag)
		return true;

	return false;
}

/*
 * TODO: To prevent starvation by low priority, using ratio of
 * runnable and running looks better.
 */
static int find_victim_rt_rq(struct task_struct *task)
{
	int best_cpu = -1, cpu;
	bool victim_rt = true;
	unsigned int victim_cpu_cap, min_cpu_cap;
	unsigned long victim_rtweight, min_rtweight;
	struct cpumask candidate_cpus;
	struct frt_dom *dom, *prefer_dom;
	bool reverse = false;

	min_cpu_cap = arch_scale_cpu_capacity(NULL, task_cpu(task));
	min_rtweight = task->rt.avg.util_avg * weight_from_rtprio(task->prio);

	cpumask_and(&candidate_cpus, &task->cpus_allowed, get_available_cpus());

	cpu = find_prefer_cpu(task, &reverse);
	prefer_dom = dom = per_cpu(frt_rqs, cpu);
	if (unlikely(!dom))
		return best_cpu;
	do {
		for_each_cpu_and(cpu, &dom->cpus, &candidate_cpus) {
			struct task_struct *victim = cpu_rq(cpu)->curr;

			if (victim->nr_cpus_allowed < 2)
				continue;

			if (ecs_is_sparing_cpu(cpu))
				continue;

			if (!emstune_can_migrate_task(task, cpu))
				continue;

			if (!rt_task(victim) && !dl_task(victim)) {
				/* If Non-RT CPU is exist, select it first. */
				best_cpu = cpu;
				victim_rt = false;
				break;
			}

			victim_cpu_cap = arch_scale_cpu_capacity(NULL, cpu);
			victim_rtweight = victim->rt.avg.util_avg * weight_from_rtprio(victim->prio);

			/*
			 * It's necessary to un-cap the cpu capacity when comparing
			 * utilization of each CPU. This is why the Fluid RT tries to give
			 * the green light on big CPU to the long-run RT task
			 * in accordance with the priority.
			 */
			if (victim_rtweight * min_cpu_cap < min_rtweight * victim_cpu_cap) {
				min_rtweight = victim_rtweight;
				best_cpu = cpu;
				min_cpu_cap = victim_cpu_cap;
			}
		}

		if (cpu_selected(best_cpu)) {
			if (victim_rt)
				frt_set_victim_flag(cpu_rq(best_cpu)->curr);

			trace_frt_select_task_rq(task, &task->rt.avg, best_cpu,
					victim_rt ? "VICTIM-RT" : "VICTIM-FAIR");
			return best_cpu;
		}

		dom = reverse ? dom->prev : dom->next;
	} while (dom != prefer_dom);

	return best_cpu;
}

#if ENABLE_CACHE_HOT
/* Affordable CPU:
 * to find the best CPU in which the data is kept in cache-hot
 *
 * In most of time, RT task is invoked because,
 *  Case - I : it is already scheduled some time ago, or
 *  Case - II: it is requested by some task without timedelay
 *
 * In case-I, it's hardly to find the best CPU in cache-hot if the time is relatively long.
 * But in case-II, waker CPU is likely to keep the cache-hot data useful to wakee RT task.
 */
static inline int affordable_cpu(int cpu, unsigned long task_load)
{
	/*
	 * If the task.state is 'TASK_INTERRUPTIBLE',
	 * she is likely to call 'schedule()' explicitely, for waking up RT task.
	 *   and have something in common with it.
	 */
	if (cpu_curr(cpu)->state != TASK_INTERRUPTIBLE)
		return 0;

	/*
	 * Waker CPU must accommodate the target RT task.
	 */
	if (capacity_of(cpu) <= task_load)
		return 0;

	/*
	 * Future work (More concerns if needed):
	 * - Min opportunity cost between the eviction of tasks and dismiss of target RT
	 *	: If evicted tasks are expecting too many damage for its execution,
	 *		Target RT should not be this CPU.
	 *	load(RT) >= Capa(CPU)/3 && load(evicted tasks) >= Capa(CPU)/3
	 * - Identifying the relation:
	 *	: Is it possible to identify the relation (such as mutex owner and waiter)
	 * -
	 */

	return 1;
}

/* To enable cache hot, we need to study wake flags */
static int check_cache_hot(struct task_struct *task, int flags, int *best_cpu)
{
	int cpu = smp_processor_id();
	return false;
	/*
	 * 3. Cache hot : packing the callee and caller,
	 *	when there is nothing to run except callee, or
	 *	wake_flags are set.
	 */
	/* FUTURE WORK: Hierarchical cache hot */
	if (!(flags & WF_SYNC))
		return false;

	if (cpumask_test_cpu(*best_cpu, cpu_coregroup_mask(cpu))) {
		task->rt.sync_flag = 1;
		*best_cpu = cpu;
		trace_frt_select_task_rq(task, &task->rt.avg, *best_cpu, "CACHE-HOT");
		return true;
	}

	return false;
}
#endif	// ENABLE_CACHEHOT

static int find_idle_cpu(struct task_struct *task)
{
	int cpu, best_cpu = -1;
	int cpu_prio, max_prio = -1;
	u64 cpu_util, min_util = ULLONG_MAX;
	struct cpumask candidate_cpus;
	struct frt_dom *dom, *prefer_dom;
	bool reverse = false;

	cpumask_and(&candidate_cpus, &task->cpus_allowed, cpu_active_mask);
	cpumask_and(&candidate_cpus, &candidate_cpus, get_available_cpus());
	cpumask_and(&candidate_cpus, &candidate_cpus, emstune_cpus_allowed(task));
	if (unlikely(cpumask_empty(&candidate_cpus)))
		cpumask_copy(&candidate_cpus, &task->cpus_allowed);

	cpu = find_prefer_cpu(task, &reverse);
	prefer_dom = dom = per_cpu(frt_rqs, cpu);
	if (unlikely(!dom))
		return best_cpu;
	do {
		for_each_cpu_and(cpu, &dom->cpus, &candidate_cpus) {
			if (!idle_cpu(cpu))
				continue;

			if (ecs_is_sparing_cpu(cpu))
				continue;

			cpu_prio = cpu_rq(cpu)->rt.highest_prio.curr;
			if (cpu_prio < max_prio)
				continue;

			cpu_util = frt_cpu_util_with(cpu, task);
			if (cpu_util > capacity_orig_of(cpu))
				continue;

			if ((cpu_prio > max_prio) || (cpu_util < min_util) ||
				(cpu_util == min_util && task_cpu(task) == cpu)) {
				min_util = cpu_util;
				max_prio = cpu_prio;
				best_cpu = cpu;
			}

		}

		if (cpu_selected(best_cpu)) {
#if ENABLE_CACHE_HOT
			if (check_cache_hot(task, wake_flags, &best_cpu))
				return best_cpu;
#endif
			trace_frt_select_task_rq(task, &task->rt.avg, best_cpu, "IDLE-FIRST");
			return best_cpu;
		}

		dom = reverse ? dom->prev : dom->next;
	} while (dom != prefer_dom);

	return best_cpu;
}

extern DEFINE_PER_CPU(cpumask_var_t, local_cpu_mask);
static int find_recessive_cpu(struct task_struct *task)
{
	int cpu, best_cpu = -1;
	u64 cpu_util, min_util= ULLONG_MAX;
	struct cpumask *lowest_mask;
	struct cpumask candidate_cpus;
	struct frt_dom *dom, *prefer_dom;
	bool reverse = false;

	/* Make sure the mask is initialized first */
	lowest_mask = this_cpu_cpumask_var_ptr(local_cpu_mask);
	if (unlikely(!lowest_mask)) {
		trace_frt_select_task_rq(task, &task->rt.avg, best_cpu, "NA LOWESTMSK");
		return best_cpu;
	}
	/* update the per-cpu local_cpu_mask (lowest_mask) */
	cpupri_find(&task_rq(task)->rd->cpupri, task, lowest_mask);
	cpumask_and(&candidate_cpus, lowest_mask, cpu_active_mask);
	cpumask_and(&candidate_cpus, &candidate_cpus, get_available_cpus());
	cpumask_and(&candidate_cpus, &candidate_cpus, emstune_cpus_allowed(task));

	cpu = find_prefer_cpu(task, &reverse);
	prefer_dom = dom = per_cpu(frt_rqs, cpu);
	if (unlikely(!dom))
		return best_cpu;
	do {
		for_each_cpu_and(cpu, &dom->cpus, &candidate_cpus) {
			if (ecs_is_sparing_cpu(cpu))
				continue;

			cpu_util = frt_cpu_util_with(cpu, task);
			if (cpu_util > capacity_orig_of(cpu))
				continue;

			if (cpu_util < min_util ||
				(cpu_util == min_util && task_cpu(task) == cpu)) {
				min_util = cpu_util;
				best_cpu = cpu;
			}
		}

		if (cpu_selected(best_cpu)) {
#if ENABLE_CACHE_HOT
			if (check_cache_hot(task, wake_flags, &best_cpu))
				return best_cpu;
#endif
			trace_frt_select_task_rq(task, &task->rt.avg, best_cpu,
				rt_task(cpu_rq(best_cpu)->curr) ? "RT-RECESS" : "FAIR-RECESS");
			return best_cpu;
		}

		dom = reverse ? dom->prev : dom->next;
	} while (dom != prefer_dom);

	return best_cpu;
}

void frt_update_available_cpus(void)
{
	struct frt_dom *dom, *prev_idle_dom = NULL;
	struct cpumask mask;
	unsigned long flags;

	if (unlikely(!frt_initialized))
		return;

	if (!raw_spin_trylock_irqsave(&frt_lock, flags))
		return;

	cpumask_copy(&mask, cpu_active_mask);
	list_for_each_entry_reverse(dom, &frt_list, list) {
		unsigned long dom_util_sum = 0;
		unsigned long dom_active_thr = 0;
		unsigned long capacity;
		struct cpumask active_cpus;
		int first_cpu, cpu;

		cpumask_and(&active_cpus, &dom->cpus, cpu_active_mask);
		first_cpu = cpumask_first(&active_cpus);
		/* all cpus of domain is offed */
		if (first_cpu == NR_CPUS)
			continue;

		for_each_cpu(cpu, &active_cpus)
			dom_util_sum += frt_cpu_util(cpu);

		capacity = capacity_cpu(first_cpu, 0) * cpumask_weight(&active_cpus);
		dom_active_thr = ratio_scale(capacity, dom->active_ratio);

		/* domain is idle */
		if (dom_util_sum < dom_active_thr) {
			/* if prev domain is also idle, clear prev domain cpus */
			if (prev_idle_dom)
				cpumask_andnot(&mask, &mask, &prev_idle_dom->cpus);
			prev_idle_dom = dom;
		}

		trace_frt_available_cpus(first_cpu, dom_util_sum,
			dom_active_thr, *(unsigned int *)cpumask_bits(&mask));
	}
	cpumask_copy(&available_mask, &mask);
	raw_spin_unlock_irqrestore(&frt_lock, flags);
}

int frt_find_lowest_rq(struct task_struct *task)
{
	int best_cpu = -1;

	if (task->nr_cpus_allowed == 1) {
		trace_frt_select_task_rq(task, &task->rt.avg, best_cpu, "NA ALLOWED");
		return best_cpu;
	}

	if (!rt_task(task))
		return best_cpu;
	/*
	 * Fluid Sched Core selection procedure:
	 *
	 * 1. idle CPU selection
	 * 2. recessive task first
	 * 3. victim task first
	 */

	/* 1. idle CPU selection */
	best_cpu = find_idle_cpu(task);
	if (cpu_selected(best_cpu))
		goto out;

	/* 2. recessive task first */
	best_cpu = find_recessive_cpu(task);
	if (cpu_selected(best_cpu))
		goto out;

	/* 3. victim task first */
	best_cpu = find_victim_rt_rq(task);

out:
	if (!cpu_selected(best_cpu))
		best_cpu = task_rq(task)->cpu;

	if (!cpumask_test_cpu(best_cpu, cpu_active_mask)) {
		trace_frt_select_task_rq(task, &task->rt.avg, best_cpu, "NOTHING_VALID");
		best_cpu = -1;
	}

	return best_cpu;
}

