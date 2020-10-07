#include <asm/perf_event.h>
#include <asm/sysreg.h>
#include <asm/pgalloc.h>
#include <linux/perf/arm_pmu.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/cpu_pm.h>

#include "../sched.h"

#include <trace/events/ems.h>
#include <trace/events/ems_debug.h>

static DECLARE_COMPLETION(pmu_cont_init);

#define ARMV8_PMUV3_PERFCTR_INST_RETIRED			0x08
#define ARMV8_IMPDEF_PERFCTR_STREX_SPEC				0x6F
#define ARMV8_IMPDEF_PERFCTR_STREX_FAIL_SPEC			0x6E

#define PMU_COUNT_HW_MAX	6
#define PERF_COUNT_USED_MAX	2
#define EVENT_SHIFT		20
#define PERIOD_HIST_SIZE_MAX	(20)
#define GET_PMU_IDX(idx)	(PMU_COUNT_HW_MAX - idx - 1)

/*
 * below values are shared with PART to synchronize up cpu_active_ratio
 * with pmu_cont_avg
*/
extern __read_mostly u64 period_size;
extern __read_mostly u64 period_hist_size;

enum cont_flags {
	CONT_INIT = 0,
	CONT_IDLE_EXIT,
	CONT_TICK,
	CONT_IDLE_ENTRY,
};

enum cont_avg_user {
	CONT_NONE,
	CONT_ATTACKER,
	CONT_VICTIM,
};

struct cont_avg_victim {
	unsigned int	found_cnt;
};

struct cont_avg_attacker {
	unsigned int		found_cnt;
	unsigned int		found;
	u64			found_time;
	unsigned int		search_in_progress;
	struct task_struct	*attacker;

	bool			work_in_progress;
	struct irq_work		irq_work;
	struct kthread_work	work;
	struct kthread_worker	worker;
	struct task_struct	*migrate_thread;
};

struct cont_avg {
	int	type;	/* cpu type */

	int		*pmu_event;
	u64		prev_event[PMU_COUNT_HW_MAX];
	u64		event[PMU_COUNT_HW_MAX];
	unsigned int	ratio[PERIOD_HIST_SIZE_MAX];
	unsigned int	hist_idx;
        u64		last_updated;
	u64		period_start;

	unsigned int	search_hist_size;
	unsigned int	event_thr;
	unsigned int	active_thr;

	/* For Victim Only */
	struct cont_avg_victim		*victimd;

	/* For Attacker Only */
	struct cont_avg_attacker	*attackerd;
};


/* tunable knob */
unsigned int cont_enabled;
unsigned int attacker_search_timeout_msec;

struct kobject *cont_kobj;

struct cpumask contention_cpus;

static int attacker_pmu_event[PERF_COUNT_USED_MAX] = {
	ARMV8_IMPDEF_PERFCTR_STREX_SPEC,
	ARMV8_PMUV3_PERFCTR_INST_RETIRED,
};
static int victim_pmu_event[PERF_COUNT_USED_MAX] = {
	ARMV8_IMPDEF_PERFCTR_STREX_FAIL_SPEC,
	ARMV8_IMPDEF_PERFCTR_STREX_SPEC,
};

DEFINE_PER_CPU(struct cont_avg, *cont_avg);

/*
 * All Counter that are accessible at Non-secure EL1
 */
static void armpmu_start(void)
{
	u32 val;

	val = read_sysreg(pmcr_el0);

	/* Already counters are enabled */
	if (val & ARMV8_PMU_PMCR_E)
		return;

	/* Enable All Counters */
	val = val | ARMV8_PMU_PMCR_E;
	write_sysreg(val, pmcr_el0);
}

/*
 * All Counter that are non-accessible at Non-secure EL1
 */
static void armpmu_stop(void)
{
	u32 val;

	/* Disable All Counters */
	val = read_sysreg(pmcr_el0) & ~ARMV8_PMU_PMCR_E;
	write_sysreg(val, pmcr_el0);
}

void pmu_select_counter(u32 event)
{
	u32 counter = event & ARMV8_PMU_COUNTER_MASK;

	write_sysreg(counter, pmselr_el0);
}

static void write_cont_avg(int idx, u64 val)
{
	switch(idx)
	{
	case 0:
		write_sysreg(val, pmevcntr0_el0);
		break;
	case 1:
		write_sysreg(val, pmevcntr1_el0);
		break;
	case 2:
		write_sysreg(val, pmevcntr2_el0);
		break;
	case 3:
		write_sysreg(val, pmevcntr3_el0);
		break;
	case 4:
		write_sysreg(val, pmevcntr4_el0);
		break;
	case 5:
		write_sysreg(val, pmevcntr5_el0);
		break;
	default:
		break;
	}
}

static u64 read_cont_avg(int idx)
{
	switch(idx)
	{
	case 0:
		return read_sysreg(pmevcntr0_el0);
	case 1:
		return read_sysreg(pmevcntr1_el0);
	case 2:
		return read_sysreg(pmevcntr2_el0);
	case 3:
		return read_sysreg(pmevcntr3_el0);
	case 4:
		return read_sysreg(pmevcntr4_el0);
	case 5:
		return read_sysreg(pmevcntr5_el0);
	default:
		break;
	}

	return 0;
}

static void pmu_write_evttype(int *cont_avg)
{
	u32 val;
	int idx;

	armpmu_start();

	for (idx = 0; idx < PERF_COUNT_USED_MAX; idx++) {
		/* Select Counter idx */
		write_sysreg(GET_PMU_IDX(idx), pmselr_el0);
		isb();

		/* Select Counter Type */
		write_sysreg(cont_avg[idx], pmxevtyper_el0);
		isb();

		/* Clear Counter */
		write_cont_avg(GET_PMU_IDX(idx), 0);
		isb();

		/* Enable individual Counter */
		val = read_sysreg(pmcntenset_el0) | BIT(GET_PMU_IDX(idx));
		write_sysreg(val, pmcntenset_el0);
		isb();
	}
}

static int pmu_stop_all_event(void *data)
{
	armpmu_stop();
	complete(&pmu_cont_init);

	return 0;
}

static void __pmu_start_all_event(int cpu)
{
	struct cont_avg *ca = per_cpu(cont_avg, cpu);

	if (unlikely(!cont_enabled))
		return;

	if (!ca)
		return;

	if (ca->type == CONT_VICTIM) {
		pmu_write_evttype(ca->pmu_event);
		return;
	}

	/* attacker */
	if (unlikely(READ_ONCE(ca->attackerd->found))) {
		ca->attackerd->search_in_progress = 1;
		pmu_write_evttype(ca->pmu_event);
	}
}

static int pmu_start_all_event(void *data)
{
	__pmu_start_all_event(raw_smp_processor_id());

	complete(&pmu_cont_init);

	return 0;
}

static int exynos_cont_pm_notifier(struct notifier_block *self,
						unsigned long action, void *v)
{
	int cpu = raw_smp_processor_id();

	if (action != CPU_PM_EXIT)
		return NOTIFY_OK;

	__pmu_start_all_event(cpu);

	return NOTIFY_OK;
}
static struct notifier_block exynos_cont_pm_nb = {
	.notifier_call = exynos_cont_pm_notifier,
};

static inline int inc_hist_idx(int idx)
{
	return (idx + 1) % period_hist_size;
}

static inline int dec_hist_idx(int idx)
{
	return idx ? (idx - 1) : (period_hist_size - 1);
}

static void cont_found_victim(struct cont_avg_victim *victimd, u64 now)
{
	int cpu;

	for_each_cpu(cpu, cpu_active_mask) {
		struct cont_avg *ca = per_cpu(cont_avg, cpu);
		if (!ca || !ca->attackerd)
			continue;

		WRITE_ONCE(ca->attackerd->found, 1);
		WRITE_ONCE(ca->attackerd->found_time, now);
	};

	victimd->found_cnt++;
	trace_cont_found_victim(1, now);
}

static int cont_found_attacker(int cpu, struct cont_avg *ca, int event)
{
	struct cont_avg_attacker *attackerd = ca->attackerd;
	struct rq *rq = cpu_rq(cpu);
	struct task_struct *task = rq->curr;

	if (event == CONT_IDLE_EXIT)
		return 0;

	if (!task || task == rq->idle || task == rq->stop)
		return 0;

	if (!cpumask_intersects(&task->cpus_allowed, &contention_cpus))
		return 0;

	if (attackerd->work_in_progress)
		return 0;

	attackerd->attacker = task;
	attackerd->work_in_progress = true;
	attackerd->found_cnt++;
	irq_work_queue_on(&attackerd->irq_work, 0);

	trace_cont_found_attacker(cpu, task);

	return 1;
}

static void cont_set_period_start(int cpu)
{
	struct cont_avg *ca = per_cpu(cont_avg, cpu);
	struct part *pa = &cpu_rq(cpu)->pa;
	int idx, cnt;

	ca->last_updated = pa->last_updated;
	ca->period_start = pa->period_start;
	ca->hist_idx = pa->hist_idx;

	for (idx = 0; idx < PERF_COUNT_USED_MAX; idx++) {
		ca->prev_event[GET_PMU_IDX(idx)] = read_cont_avg(GET_PMU_IDX(idx));
		ca->event[GET_PMU_IDX(idx)] = 0;
	}

	idx = ca->hist_idx;
	for (cnt = 0; cnt < ca->search_hist_size; cnt++) {
		ca->ratio[idx] = 0;
		idx = dec_hist_idx(idx);
	}

	trace_cont_set_period_start(cpu, ca->last_updated, ca->period_start,
		ca->hist_idx, ca->prev_event[GET_PMU_IDX(0)],  ca->prev_event[GET_PMU_IDX(1)]);
}

/* output in the range of 0 ~ 1000000 */
static bool is_crossed_thr(int cpu, struct cont_avg *ca)
{
	struct part *pa = &cpu_rq(cpu)->pa;
	unsigned int search_hist_size = ca->search_hist_size;
	int cnt, ca_idx, pa_idx;
	u64 event_thr, active_thr, event_ratio, active_ratio;
	u64 weighted_sum = 0, active_ratio_sum = 0;

	event_thr = ca->event_thr << (EVENT_SHIFT / 2);
	active_thr = ca->active_thr;
	ca_idx = ca->hist_idx;
	pa_idx = pa->hist_idx;

	for (cnt = 0; cnt < search_hist_size; cnt++) {

		weighted_sum += (ca->ratio[ca_idx] * pa->hist[pa_idx]);
		active_ratio_sum += pa->hist[pa_idx];

		ca_idx = dec_hist_idx(ca_idx);
		pa_idx = dec_hist_idx(pa_idx);

	}

	event_ratio = (weighted_sum >> SCHED_CAPACITY_SHIFT) / search_hist_size;
	active_ratio = active_ratio_sum / search_hist_size;

	trace_cont_crossed_thr(cpu, event_thr, event_ratio, active_thr, active_ratio);

	if (event_ratio > event_thr && active_ratio > active_thr)
		return true;

	return false;
}

static void close_period(int cpu, struct cont_avg *ca, u64 now, int event)
{
	ca->hist_idx = inc_hist_idx(ca->hist_idx);

	if (!ca->event[GET_PMU_IDX(1)])
		ca->ratio[ca->hist_idx] = 0;
	else
		ca->ratio[ca->hist_idx] =
			(ca->event[GET_PMU_IDX(0)] << EVENT_SHIFT) / ca->event[GET_PMU_IDX(1)];
}

static void distribute_pmu_count(int cpu, struct cont_avg *ca, u64 now,
		u64 elapsed, u64 period_count, u64 *curr_event, int event)
{
	u64 diff[PERF_COUNT_USED_MAX];
	u64 contributer, period_count_org = period_count, remainder = elapsed;
	int idx;

	/* Diff during idle period ignored */
	for (idx = 0; idx < PERF_COUNT_USED_MAX; idx++)
		diff[idx] = (event == CONT_IDLE_EXIT) ? 0 : curr_event[idx] - ca->prev_event[GET_PMU_IDX(idx)];

	if (period_count) {
		contributer = ca->period_start + period_size - ca->last_updated;
		remainder -= contributer;

		for (idx = 0; idx < PERF_COUNT_USED_MAX; idx++)
			ca->event[GET_PMU_IDX(idx)] += (diff[idx] * contributer) / elapsed;

		close_period(cpu, ca, now, event);
		period_count--;
	}

	while (period_count) {
		remainder -= period_size;
		for (idx = 0; idx < PERF_COUNT_USED_MAX; idx++)
			ca->event[GET_PMU_IDX(idx)] = (diff[idx] * period_size) / elapsed;

		close_period(cpu, ca, now, event);
		period_count--;
	}

	for (idx = 0; idx < PERF_COUNT_USED_MAX; idx++)
		ca->event[GET_PMU_IDX(idx)] = (diff[idx] * remainder) / elapsed;

	trace_cont_distribute_pmu_count(cpu, elapsed, period_count_org,
				curr_event[0], curr_event[1],
				ca->prev_event[GET_PMU_IDX(0)], ca->prev_event[GET_PMU_IDX(1)],
				diff[0], diff[1], event);
}

static void cont_release_founding_attacker(struct cont_avg *ca)
{
	/* finish checking attacker */
	WRITE_ONCE(ca->attackerd->found, 0);
	ca->period_start = 0;
	ca->attackerd->search_in_progress = 0;
}

void _update_cont_avg(int cpu, u64 now, int event)
{
	struct cont_avg *ca = per_cpu(cont_avg, cpu);
	u64 elapsed, period_count, curr_event[PERF_COUNT_USED_MAX];
	int idx, found_attacker = 0;

	if (unlikely(!ca->period_start)) {
		cont_set_period_start(cpu);
		return;
	}

	elapsed = now - ca->last_updated;
	period_count = div64_u64((now - ca->period_start), period_size);

	for (idx = 0; idx < PERF_COUNT_USED_MAX; idx++)
		curr_event[idx] = read_cont_avg(GET_PMU_IDX(idx));

	distribute_pmu_count(cpu, ca, now, elapsed, period_count, curr_event, event);

	/* Check it is victim or attacker */
	if (period_count && is_crossed_thr(cpu, ca)) {
		if (ca->type == CONT_VICTIM)
			cont_found_victim(ca->victimd, now);
		else
			found_attacker = cont_found_attacker(cpu, ca, event);
	}

	for (idx = 0; idx < PERF_COUNT_USED_MAX; idx++)
		ca->prev_event[GET_PMU_IDX(idx)] = curr_event[idx];

	ca->last_updated = now;
	ca->period_start += period_count * period_size;

	/* When found attacker, restart  */
	if (found_attacker)
		cont_release_founding_attacker(ca);
}

static int get_event(struct rq *rq, struct task_struct *prev, struct task_struct *next)
{
	if (!next)
		return CONT_TICK;

	if (prev == rq->idle)
		return CONT_IDLE_EXIT;

	if (next == rq->idle)
		return CONT_IDLE_ENTRY;

	return 0;
}

void update_cont_avg(struct rq *rq, struct task_struct *prev, struct task_struct *next)
{
	int event, cpu = cpu_of(rq);
	struct cont_avg *ca = per_cpu(cont_avg, cpu);
	u64 now, search_time;

	if (unlikely(!cont_enabled))
		return;

	if (!ca)
		return;

	event = get_event(rq, prev, next);
	if (!event)
		return;

	now = sched_clock_cpu(0);

	if (ca->type == CONT_VICTIM) {
		_update_cont_avg(cpu, now, event);
		return;
	}

	if (likely(!READ_ONCE(ca->attackerd->found)))
		return;

	/* Check whether it is real attacker or not */
	search_time = now - READ_ONCE(ca->attackerd->found_time);
	if (search_time < attacker_search_timeout_msec * NSEC_PER_MSEC) {
		/* in case victim is found after attacker idle exit */
		if (!ca->attackerd->search_in_progress) {
			ca->attackerd->search_in_progress = 1;
			pmu_write_evttype(ca->pmu_event);
		}

		_update_cont_avg(cpu, now, event);
		return;
	}

	cont_release_founding_attacker(ca);
}

static void cont_work(struct kthread_work *work)
{
	struct cont_avg_attacker *attackerd
		= container_of(work, struct cont_avg_attacker, work);

	/* do set allowed */
	set_cpus_allowed_ptr(attackerd->attacker, &contention_cpus);
	attackerd->work_in_progress = false;
}

static void cont_irq_work(struct irq_work *irq_work)
{
	struct cont_avg_attacker *attackerd;

	attackerd = container_of(irq_work, struct cont_avg_attacker, irq_work);
	kthread_queue_work(&attackerd->worker, &attackerd->work);
}

static int cont_kthread_create(int cpu, struct cont_avg_attacker *attackerd)
{
	struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO / 2 };
	struct task_struct *thread;
	int ret;

	kthread_init_work(&attackerd->work, cont_work);
	kthread_init_worker(&attackerd->worker);
	thread = kthread_create(kthread_worker_fn, &attackerd->worker,
					"cont_migrater:%d", cpu);
	if (IS_ERR(thread)) {
		pr_err("failed to create esgov thread: %ld\n", PTR_ERR(thread));
		return PTR_ERR(thread);
	}

	ret = sched_setscheduler_nocheck(thread, SCHED_FIFO, &param);
	if (ret) {
		kthread_stop(thread);
		pr_warn("%s: failed to set SCHED_CLASS\n", __func__);
		return ret;
	}

	attackerd->migrate_thread = thread;
	init_irq_work(&attackerd->irq_work, cont_irq_work);

	wake_up_process(thread);

	return 0;
}

/* SYSFS */
#define attr_cont_gl(name)						\
static ssize_t show_##name(struct kobject *kobj,		\
		struct kobj_attribute *attr, char *buf)		\
{								\
	return snprintf(buf, 10, "%lu\n", name);		\
}								\
								\
static ssize_t store_##name(struct kobject *kobj,		\
		struct kobj_attribute *attr, const char *buf,	\
		size_t count)					\
{								\
	unsigned int input;					\
								\
	if (!sscanf(buf, "%u", &input))				\
		return -EINVAL;					\
								\
	name = input;						\
								\
	return count;						\
}								\
								\
static struct kobj_attribute cont_##name =			\
__ATTR(name, 0644, show_##name, store_##name);			\

attr_cont_gl(attacker_search_timeout_msec);
attr_cont_gl(cont_enabled);

#define attr_cont_cpu(name)							\
static ssize_t show_##name(struct kobject *kobj,				\
		struct kobj_attribute *attr, char *buf)				\
{										\
	int cpu, ret = 0;							\
										\
	for_each_possible_cpu(cpu) {						\
		struct cont_avg *ca = per_cpu(cont_avg, cpu);			\
		if (!ca)							\
			continue;						\
		ret += snprintf(buf + ret, 15, "cpu%d: %d\n", cpu, ca->name);	\
	}									\
										\
	return ret;								\
}										\
static ssize_t store_##name(struct kobject *kobj,				\
		struct kobj_attribute *attr, const char *buf,			\
		size_t count)							\
{										\
	int cpu, target_cpu, input;						\
										\
	if (!sscanf(buf, "%d %d", &target_cpu, &input))				\
		return -EINVAL;							\
										\
	for_each_cpu(cpu, cpu_coregroup_mask(target_cpu)) {			\
		struct cont_avg *ca = per_cpu(cont_avg, cpu);			\
		if (!ca)							\
			continue;						\
		ca->name = input;						\
	}									\
										\
	return count;								\
}										\
static struct kobj_attribute  cont_##name =					\
__ATTR(name, 0644, show_##name, store_##name);					\

attr_cont_cpu(event_thr);
attr_cont_cpu(active_thr);
attr_cont_cpu(search_hist_size);

static ssize_t show_found_cnt(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int cpu_i, ret = 0;

	for_each_possible_cpu(cpu_i) {
		struct cont_avg *ca = per_cpu(cont_avg, cpu_i);
		u64 cnt = 0;
		int cpu_j;

		if (!ca)
			continue;

		if (cpu_i != cpumask_first(cpu_coregroup_mask(cpu_i)))
			continue;

		for_each_cpu(cpu_j, cpu_coregroup_mask(cpu_i)) {
			ca = per_cpu(cont_avg, cpu_j);
			if (ca->type == CONT_ATTACKER)
				cnt += ca->attackerd->found_cnt;
			else
				cnt += ca->victimd->found_cnt;
		}
		if (ca->type == CONT_ATTACKER)
			ret += snprintf(buf + ret, 25, "ATTACKER: %lu\n", cnt);
		else
			ret += snprintf(buf + ret, 25, "VICTIM: %lu\n", cnt);
	}

	return ret;
}
static struct kobj_attribute cont_found_cnt =
__ATTR(found_cnt, 0444, show_found_cnt, NULL);

static void __init cont_sysfs_init(void)
{
	int ret;

	cont_kobj = kobject_create_and_add("pmu_cont", kernel_kobj);
	if (!cont_kobj) {
		pr_err("Fail to create cont kboject\n");
		return;
	}

	ret = sysfs_create_file(cont_kobj, &cont_found_cnt.attr);
	if (ret)
		pr_warn("%s: failed to create sysfs\n", __func__);

	ret = sysfs_create_file(cont_kobj, &cont_attacker_search_timeout_msec.attr);
	if (ret)
		pr_warn("%s: failed to create sysfs\n", __func__);
	ret = sysfs_create_file(cont_kobj, &cont_cont_enabled.attr);
	if (ret)
		pr_warn("%s: failed to create sysfs\n", __func__);

	ret = sysfs_create_file(cont_kobj, &cont_event_thr.attr);
	if (ret)
		pr_warn("%s: failed to create sysfs\n", __func__);

	ret = sysfs_create_file(cont_kobj, &cont_active_thr.attr);
	if (ret)
		pr_warn("%s: failed to create sysfs\n", __func__);

	ret = sysfs_create_file(cont_kobj, &cont_search_hist_size.attr);
	if (ret)
		pr_warn("%s: failed to create sysfs\n", __func__);

	return;
}

static int cont_cpu_init(struct device_node *dn, struct cpumask *cpus, int type)
{
	unsigned int val;
	int cpu;

	for_each_cpu(cpu, cpus) {
		struct cont_avg *ca;
		ca = kzalloc(sizeof(struct cont_avg), GFP_KERNEL);
		if (!ca)
			return -EINVAL;

		ca->type = type;

		if (of_property_read_u32(dn, "search_hist_size", &val))
			goto fail_cont_init;
		ca->search_hist_size = val;

		if (of_property_read_u32(dn, "event_thr", &val))
			goto fail_cont_init;
		ca->event_thr = val;

		if (of_property_read_u32(dn, "active_thr", &val))
			goto fail_cont_init;
		ca->active_thr = val;

		if (type == CONT_VICTIM) {
			ca->pmu_event = victim_pmu_event;
			ca->victimd = kzalloc(sizeof(struct cont_avg_victim), GFP_KERNEL);
			if (!ca->victimd)
				goto fail_cont_init;
		} else {
			ca->pmu_event = attacker_pmu_event;
			ca->attackerd = kzalloc(sizeof(struct cont_avg_attacker), GFP_KERNEL);
			if (!ca->attackerd)
				goto fail_cont_init;
			cont_kthread_create(cpu, ca->attackerd);
		}
		per_cpu(cont_avg, cpu) = ca;
	}

	return 0;

fail_cont_init:
	for_each_cpu(cpu, cpus) {
		struct cont_avg *ca;
		ca = per_cpu(cont_avg, cpu);
		if (!ca)
			continue;
		if (ca->victimd)
			kfree(ca->victimd);
		else if (ca->attackerd)
			kfree(ca->attackerd);
		kfree(ca);
	}
	return 0;
}

static int __init cont_parse_dt(struct cpumask *victim_cpus, struct cpumask *attacker_cpus)
{
	struct device_node *root, *child;
	const char *buf;
	unsigned int val;

	root = of_find_node_by_path("/ems");
	if (!root)
		goto init_failed;

	root = of_get_child_by_name(root, "pmu_cont");
	if (!root)
		goto init_failed;

	if (of_property_read_string(root, "attacker_cpus", &buf))
		goto init_failed;
	cpulist_parse(buf, attacker_cpus);

	if (of_property_read_string(root, "victim_cpus", &buf))
		goto init_failed;
	cpulist_parse(buf, victim_cpus);

	if (of_property_read_string(root, "contention_cpus", &buf))
		goto init_failed;
	cpulist_parse(buf, &contention_cpus);

	if (of_property_read_u32(root, "attacker_search_timout_msec", &val))
		goto init_failed;
	attacker_search_timeout_msec = val;

	child = of_get_child_by_name(root, "victim");
	if (!child)
		goto init_failed;
	if (cont_cpu_init(child, victim_cpus, CONT_VICTIM)) {
		pr_warn("%s: Faield to CONT_VICTIM init\n", __func__);
		goto init_failed;
	}

	child = of_get_child_by_name(root, "attacker");
	if (!child)
		goto init_failed;
	if(cont_cpu_init(child, attacker_cpus, CONT_ATTACKER)) {
		pr_warn("%s: Faield to CONT_ATTACKER init\n", __func__);
		goto init_failed;
	}

	return 0;

init_failed:
	pr_warn("%s: Faield to parse dt\n", __func__);
	return -EINVAL;
}

static int __init cont_init(void)
{
	struct cpumask attacker_cpus, victim_cpus;
	struct task_struct *task;
	int cpu;
	int ret = 0;

	cpumask_clear(&attacker_cpus);
	cpumask_clear(&victim_cpus);
	ret = cont_parse_dt(&victim_cpus, &attacker_cpus);
	if (ret)
		goto init_fialed;

	for_each_online_cpu(cpu) {
		struct cont_avg *ca = per_cpu(cont_avg, cpu);
		if (!ca)
			continue;

		init_completion(&pmu_cont_init);

		task = kthread_create(pmu_start_all_event, NULL, "pmu_%u", cpu);
		kthread_bind(task, cpu);
		wake_up_process(task);

		wait_for_completion(&pmu_cont_init);

		pr_info("CPU%d : cont init complete.\n",cpu);
	}

	cont_sysfs_init();
	cpu_pm_register_notifier(&exynos_cont_pm_nb);

	cont_enabled = true;

init_fialed:
	return ret;
}

static void __exit cont_exit(void)
{
	struct task_struct *task;
	int cpu;

	cont_enabled = false;

	for_each_online_cpu(cpu) {
		struct cont_avg *ca = per_cpu(cont_avg, cpu);
		if (!ca)
			continue;

		init_completion(&pmu_cont_init);

		task = kthread_create(pmu_stop_all_event, NULL, "pmu_%u", cpu);
		kthread_bind(task, cpu);
		wake_up_process(task);

		wait_for_completion(&pmu_cont_init);

		pr_info("CPU%d: cont exit complete.\n",cpu);
	}

}
late_initcall(cont_init);
module_exit(cont_exit);
