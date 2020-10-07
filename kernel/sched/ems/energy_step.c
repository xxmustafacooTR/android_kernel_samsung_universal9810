/*
 * CPUFreq governor based on Energy-Step-Data And Scheduler-Event.
 *
 * Copyright (C) 2019,Samsung Electronic Corporation
 * Author: Youngtae Lee <yt0729.lee@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kthread.h>
#include <linux/cpufreq.h>
#include <uapi/linux/sched.h>
#include <linux/slab.h>
#include <linux/cpu_pm.h>
#include <linux/pm_qos.h>
#include <linux/cpufreq.h>
#include <linux/ems.h>
#include <trace/events/ems.h>

#include "../sched.h"
#include "ems.h"

struct esgov_policy {
	struct cpufreq_policy	*policy;
	struct cpumask		cpus;
	struct kobject		kobj;
	raw_spinlock_t		update_lock;
	bool			enabled;	/* whether esg is current cpufreq governor or not */

	unsigned int		last_caller;
	unsigned int		target_freq;	/* target frequency at the current status */
	int			util;		/* target util  */
	u64			last_freq_update_time;

	/* The next fields are for the tunnables */
	int			step;
	unsigned long		step_power;	/* allowed energy at a step */
	s64			rate_delay_ns;
	int			patient_mode;

	/* Tracking min max information */
	int			min_cap;	/* allowed max capacity */
	int			max_cap;	/* allowed min capacity */
	int			min;		/* min freq */
	int			max;		/* max freq */

	/* PM PoS Fields */
	unsigned int		qos_min_class;
	unsigned int		qos_max_class;
	struct notifier_block	qos_min_notifier;
	struct notifier_block	qos_max_notifier;

	/* The next fields are for frequency change work */
	bool			work_in_progress;
	struct			irq_work irq_work;
	struct			kthread_work work;
	struct			mutex work_lock;
	struct			kthread_worker worker;
	struct task_struct	*thread;

	/* For MIGov boost */
	int			boost;
};

struct esgov_cpu {
	struct update_util_data	update_util;

	struct esgov_policy	*esg_policy;
	unsigned int		cpu;
	int			util;		/* target util */
	int			pelt_util;	/* pelt util */
	int			step_util;	/* energy step util */
	int			io_util;	/* io boost util */
	int			active_ratio;

	int			capacity;
	int			last_idx;

	bool			iowait_boost_pending;
	unsigned int		iowait_boost;
	u64			last_update;

	unsigned long		min;		/* min util matched with min_cap */
};

struct kobject *esg_kobj;
DEFINE_PER_CPU(struct esgov_policy *, esgov_policy);
DEFINE_PER_CPU(struct esgov_cpu, esgov_cpu);
bool esg_apply_migov;

/*************************************************************************/
/*			       HELPER FUNCTION			       */
/************************************************************************/
static struct esgov_policy * esg_find_esg_pol_qos_class(int class)
{
	int cpu;

	for_each_online_cpu(cpu) {
		struct esgov_policy *esg_policy = per_cpu(esgov_policy, cpu);

		if (unlikely(!esg_policy))
			continue;

		if ((esg_policy->qos_min_class == class) ||
			(esg_policy->qos_max_class == class))
			return esg_policy;
	}

	return NULL;
}

static void esg_update_step(struct esgov_policy *esg_policy, int step)
{
	int cpu = cpumask_first(&esg_policy->cpus);

	esg_policy->step = step;
	esg_policy->step_power = find_step_power(cpu, esg_policy->step);
}

static void esg_update_freq_range(struct cpufreq_policy *policy)
{
	unsigned int qos_min, qos_max, new_min, new_max, new_min_idx, new_max_idx;
	struct esgov_policy *esg_policy = per_cpu(esgov_policy, policy->cpu);

	if (unlikely((!esg_policy) || !esg_policy->enabled))
		return;

	if (cpumask_empty(policy->cpus))
		return;

	qos_min = pm_qos_request(esg_policy->qos_min_class);
	qos_max = pm_qos_request(esg_policy->qos_max_class);

	new_min = max(policy->min, qos_min);
	new_max = min(policy->max, qos_max);

	if (esg_policy->min == new_min && esg_policy->max == new_max)
		return;

	esg_policy->min = new_min;
	esg_policy->max = new_max;

	new_min_idx = cpufreq_frequency_table_target(
				policy, new_min, CPUFREQ_RELATION_L);
	new_max_idx = cpufreq_frequency_table_target(
				policy, new_max, CPUFREQ_RELATION_H);

	new_min = esg_policy->policy->freq_table[new_min_idx].frequency;
	new_max = esg_policy->policy->freq_table[new_max_idx].frequency;

	esg_policy->min_cap = find_allowed_capacity(policy->cpu, new_min, 0);
	esg_policy->max_cap = find_allowed_capacity(policy->cpu, new_max, 0);
	esg_policy->min_cap = min(esg_policy->max_cap, esg_policy->min_cap);

	trace_esg_update_limit(policy->cpu, esg_policy->min_cap, esg_policy->max_cap);
}

static int esg_cpufreq_policy_callback(struct notifier_block *nb,
				unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;

	if (event == CPUFREQ_NOTIFY)
		esg_update_freq_range(policy);

	return NOTIFY_OK;
}

static struct notifier_block esg_cpufreq_policy_notifier = {
	.notifier_call = esg_cpufreq_policy_callback,
};

static int esg_cpufreq_pm_qos_callback(struct notifier_block *nb,
					unsigned long val, void *v)
{
	int pm_qos_class = *((int *)v);
	struct esgov_policy *esg_pol;

	esg_pol = esg_find_esg_pol_qos_class(pm_qos_class);
	if (unlikely(!esg_pol))
		return NOTIFY_OK;

	down_read(&esg_pol->policy->rwsem);
	esg_update_freq_range(esg_pol->policy);
	up_read(&esg_pol->policy->rwsem);

	return NOTIFY_OK;
}

/************************************************************************/
/*				BOOST				 	*/
/************************************************************************/
static int esg_mode_update_callback(struct notifier_block *nb,
				unsigned long val, void *v)
{
	struct emstune_set *cur_set = (struct emstune_set *)v;
	struct esgov_policy *esg_policy;
	int cpu;

	for_each_possible_cpu(cpu) {
		if (cpu != cpumask_first(cpu_coregroup_mask(cpu)))
			continue;

		esg_policy = per_cpu(esgov_policy, cpu);
		if (unlikely((!esg_policy) || !esg_policy->enabled))
			continue;

		esg_update_step(esg_policy, cur_set->esg.step[cpu]);
		esg_policy->patient_mode = cur_set->esg.patient_mode[cpu];
	}

	esg_apply_migov = cur_set->migov.migov_en;

	return NOTIFY_OK;
}
static struct notifier_block esg_mode_update_notifier = {
	.notifier_call = esg_mode_update_callback,
};

int esg_get_migov_boost(int cpu)
{
	struct esgov_policy *esg_policy = per_cpu(esgov_policy, cpu);

	if (unlikely((!esg_policy) || !esg_policy->enabled))
		return 0;

	return esg_policy->boost;
}

void esg_set_migov_boost(int cpu, int boost)
{
	struct esgov_policy *esg_policy = per_cpu(esgov_policy, cpu);

	if (unlikely((!esg_policy) || !esg_policy->enabled))
		return;

	esg_policy->boost = boost;
}

static int esg_apply_migov_boost(int cpu, int util)
{
	struct esgov_policy *esg_policy = per_cpu(esgov_policy, cpu);
	int capacity, boosted_util = 0, margin = 0, boost;

	capacity = esg_policy->max_cap;
	boost = esg_policy->boost;

	if (boost >= 0) {
		if (capacity > util) {
			margin = capacity - util;
			margin *= boost;
		} else
			margin  = 0;
	} else
		margin = util * boost;

	margin = margin / 100;

	boosted_util = util + margin;

	trace_esg_migov_boost(cpu, boost, util, boosted_util);

	return boosted_util;
}

/*************************************************************************/
/*			       IOWAIT BOOST				 */
/************************************************************************/

/**
 * esgov_iowait_reset() - Reset the IO boost status of a CPU.
 * @esg_cpu: the esgov data for the CPU to boost
 * @time: the update time from the caller
 * @set_iowait_boost: true if an IO boost has been requested
 *
 * The IO wait boost of a task is disabled after a tick since the last update
 * of a CPU. If a new IO wait boost is requested after more then a tick, then
 * we enable the boost starting from the minimum frequency, which improves
 * energy efficiency by ignoring sporadic wakeups from IO.
 */
static bool esgov_iowait_reset(struct esgov_cpu *esg_cpu, u64 time,
			       bool set_iowait_boost)
{
	s64 delta_ns = time - esg_cpu->last_update;

	/* Reset boost only if a tick has elapsed since last request */
	if (delta_ns <= TICK_NSEC)
		return false;

	esg_cpu->iowait_boost = set_iowait_boost ? esg_cpu->min : 0;
	esg_cpu->iowait_boost_pending = set_iowait_boost;

	return true;
}

/**
 * esggov_iowait_boost() - Updates the IO boost status of a CPU.
 * @esg_cpu: the esgov data for the CPU to boost
 * @time: the update time from the caller
 * @flags: SCHED_CPUFREQ_IOWAIT if the task is waking up after an IO wait
 *
 * Each time a task wakes up after an IO operation, the CPU utilization can be
 * boosted to a certain utilization which doubles at each "frequent and
 * successive" wakeup from IO, ranging from the utilization of the minimum
 * OPP to the utilization of the maximum OPP.
 * To keep doubling, an IO boost has to be requested at least once per tick,
 * otherwise we restart from the utilization of the minimum OPP.
 */
static void esgov_iowait_boost(struct esgov_cpu *esg_cpu, u64 time,
			       unsigned int flags)
{
	bool set_iowait_boost = flags & SCHED_CPUFREQ_IOWAIT;

	/* Reset boost if the CPU appears to have been idle enough */
	if (esg_cpu->iowait_boost &&
	    esgov_iowait_reset(esg_cpu, time, set_iowait_boost))
		return;

	/* Boost only tasks waking up after IO */
	if (!set_iowait_boost)
		return;

	/* Ensure boost doubles only one time at each request */
	if (esg_cpu->iowait_boost_pending)
		return;
	esg_cpu->iowait_boost_pending = true;

	/* Double the boost at each request */
	if (esg_cpu->iowait_boost) {
		esg_cpu->iowait_boost =
			min_t(unsigned int, esg_cpu->iowait_boost << 1, SCHED_CAPACITY_SCALE);
		return;
	}

	/* First wakeup after IO: start with minimum boost */
	esg_cpu->iowait_boost = esg_cpu->min;
}

/**
 * esgov_iowait_apply() - Apply the IO boost to a CPU.
 * @esg_cpu: the esgov data for the cpu to boost
 * @time: the update time from the caller
 * @util: the utilization to (eventually) boost
 * @max: the maximum value the utilization can be boosted to
 *
 * A CPU running a task which woken up after an IO operation can have its
 * utilization boosted to speed up the completion of those IO operations.
 * The IO boost value is increased each time a task wakes up from IO, in
 * esgov_iowait_apply(), and it's instead decreased by this function,
 * each time an increase has not been requested (!iowait_boost_pending).
 *
 * A CPU which also appears to have been idle for at least one tick has also
 * its IO boost utilization reset.
 *
 * This mechanism is designed to boost high frequently IO waiting tasks, while
 * being more conservative on tasks which does sporadic IO operations.
 */
static unsigned long esgov_iowait_apply(struct esgov_cpu *esg_cpu,
					u64 time, unsigned long max)
{
	unsigned long boost;

	/* No boost currently required */
	if (!esg_cpu->iowait_boost)
		return 0;

	/* Reset boost if the CPU appears to have been idle enough */
	if (esgov_iowait_reset(esg_cpu, time, false))
		return 0;

	if (!esg_cpu->iowait_boost_pending) {
		/*
		 * No boost pending; reduce the boost value.
		 */
		esg_cpu->iowait_boost >>= 1;
		if (esg_cpu->iowait_boost < esg_cpu->min) {
			esg_cpu->iowait_boost = 0;
			return 0;
		}
	}

	esg_cpu->iowait_boost_pending = false;

	boost = (esg_cpu->iowait_boost * max) >> SCHED_CAPACITY_SHIFT;
	boost = boost + (boost >> 2);
	return boost;
}

struct esg_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *, char *);
	ssize_t (*store)(struct kobject *, const char *, size_t count);
};

#define esg_attr_rw(name)				\
static struct esg_attr name##_attr =			\
__ATTR(name, 0644, show_##name, store_##name)

#define esg_show_step(name, related_val)						\
static ssize_t show_##name(struct kobject *k, char *buf)			\
{										\
	struct esgov_policy *esg_policy =					\
			container_of(k, struct esgov_policy, kobj);		\
										\
	return sprintf(buf, "step: %d (energy: %lu)\n",				\
			esg_policy->name, esg_policy->related_val);		\
}

#define esg_store_step(name)								\
static ssize_t store_##name(struct kobject *k, const char *buf, size_t count)	\
{										\
	struct esgov_policy *esg_policy =					\
			container_of(k, struct esgov_policy, kobj);		\
	int data;								\
										\
	if (!sscanf(buf, "%d", &data))						\
		return -EINVAL;							\
										\
	esg_update_##name(esg_policy, data);					\
										\
	return count;								\
}

#define esg_show(name)								\
static ssize_t show_##name(struct kobject *k, char *buf)			\
{										\
	struct esgov_policy *esg_policy =					\
			container_of(k, struct esgov_policy, kobj);		\
										\
	return sprintf(buf, "%d\n", esg_policy->name);				\
}										\

#define esg_store(name)								\
static ssize_t store_##name(struct kobject *k, const char *buf, size_t count)	\
{										\
	struct esgov_policy *esg_policy =					\
			container_of(k, struct esgov_policy, kobj);		\
	int data;								\
										\
	if (!sscanf(buf, "%d", &data))						\
		return -EINVAL;							\
										\
	esg_policy->name = data;						\
	return count;								\
}

esg_show_step(step, step_power);
esg_store_step(step);
esg_attr_rw(step);

esg_show(patient_mode);
esg_store(patient_mode);
esg_attr_rw(patient_mode);

static ssize_t show(struct kobject *kobj, struct attribute *at, char *buf)
{
	struct esg_attr *fvattr = container_of(at, struct esg_attr, attr);
	return fvattr->show(kobj, buf);
}

static ssize_t store(struct kobject *kobj, struct attribute *at,
					const char *buf, size_t count)
{
	struct esg_attr *fvattr = container_of(at, struct esg_attr, attr);
	return fvattr->store(kobj, buf, count);
}

static const struct sysfs_ops esg_sysfs_ops = {
	.show	= show,
	.store	= store,
};

static struct attribute *esg_attrs[] = {
	&step_attr.attr,
	&patient_mode_attr.attr,
	NULL
};

static struct kobj_type ktype_esg = {
	.sysfs_ops	= &esg_sysfs_ops,
	.default_attrs	= esg_attrs,
};

static struct esgov_policy *esgov_policy_alloc(struct cpufreq_policy *policy)
{
	struct device_node *dn = NULL;
	struct esgov_policy *esg_policy;
	const char *buf;
	char name[15];
	int val;

	/* find DT for this policy */
	snprintf(name, sizeof(name), "esgov-pol%d", policy->cpu);
	dn = of_find_node_by_type(dn, name);
	if (!dn)
		goto init_failed;

	/* allocate esgov_policy */
	esg_policy = kzalloc(sizeof(struct esgov_policy), GFP_KERNEL);
	if (!esg_policy)
		goto init_failed;

	/* initialize shared data of esgov_policy */
	if (of_property_read_string(dn, "shared-cpus", &buf))
		goto free_allocation;

	cpulist_parse(buf, &esg_policy->cpus);
	cpumask_and(&esg_policy->cpus, &esg_policy->cpus, policy->cpus);
	if (cpumask_weight(&esg_policy->cpus) == 0)
		goto free_allocation;

	/* Init tunable knob */
	if (of_property_read_u32(dn, "step", &val))
		goto free_allocation;
	esg_update_step(esg_policy, val);

	if (of_property_read_u32(dn, "patient_mode", &val))
		goto free_allocation;
	esg_policy->patient_mode = val;

	esg_policy->rate_delay_ns = 4 * NSEC_PER_MSEC;

	/* Init Sysfs */
	if (kobject_init_and_add(&esg_policy->kobj, &ktype_esg, esg_kobj,
			"coregroup%d", cpumask_first(&esg_policy->cpus)))
		goto free_allocation;

	/* init spin lock */
	raw_spin_lock_init(&esg_policy->update_lock);

	/* init pm qos */
	if (of_property_read_u32(dn, "pm_qos-min-class", &esg_policy->qos_min_class))
		goto free_allocation;
	if (of_property_read_u32(dn, "pm_qos-max-class", &esg_policy->qos_max_class))
		goto free_allocation;

	esg_policy->qos_min_notifier.notifier_call = esg_cpufreq_pm_qos_callback;
	esg_policy->qos_min_notifier.priority = INT_MAX;
	esg_policy->qos_max_notifier.notifier_call = esg_cpufreq_pm_qos_callback;
	esg_policy->qos_max_notifier.priority = INT_MAX;

	pm_qos_add_notifier(esg_policy->qos_min_class, &esg_policy->qos_min_notifier);
	pm_qos_add_notifier(esg_policy->qos_max_class, &esg_policy->qos_max_notifier);

	esg_policy->policy = policy;

	return esg_policy;

free_allocation:
	kfree(esg_policy);

init_failed:
	pr_warn("%s: Failed esgov_init(cpu%d)\n", __func__, policy->cpu);

	return NULL;
}

static void esgov_work(struct kthread_work *work)
{
	struct esgov_policy *esg_policy = container_of(work, struct esgov_policy, work);
	unsigned int freq;
	unsigned long flags;

	raw_spin_lock_irqsave(&esg_policy->update_lock, flags);
	freq = esg_policy->target_freq;
	esg_policy->work_in_progress = false;
	raw_spin_unlock_irqrestore(&esg_policy->update_lock, flags);

	down_write(&esg_policy->policy->rwsem);
	mutex_lock(&esg_policy->work_lock);
	__cpufreq_driver_target(esg_policy->policy, freq, CPUFREQ_RELATION_L);
	mutex_unlock(&esg_policy->work_lock);
	up_write(&esg_policy->policy->rwsem);
}

static void esgov_irq_work(struct irq_work *irq_work)
{
	struct esgov_policy *esg_policy;

	esg_policy = container_of(irq_work, struct esgov_policy, irq_work);

	kthread_queue_work(&esg_policy->worker, &esg_policy->work);
}

static int esgov_kthread_create(struct esgov_policy *esg_policy)
{
	struct task_struct *thread;
	struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO / 2 };
	struct cpufreq_policy *policy = esg_policy->policy;
	struct device_node *dn;
	int ret;

	kthread_init_work(&esg_policy->work, esgov_work);
	kthread_init_worker(&esg_policy->worker);
	thread = kthread_create(kthread_worker_fn, &esg_policy->worker,
				"esgov:%d", cpumask_first(policy->related_cpus));
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

	dn = of_find_node_by_path("/esg");
	if (dn) {
		struct cpumask mask;
		const char *buf;

		cpumask_copy(&mask, cpu_possible_mask);
		if (!of_property_read_string(dn, "thread-run-on", &buf))
			cpulist_parse(buf, &mask);

		kthread_bind_mask(thread, &mask);
	}

	esg_policy->thread = thread;
	init_irq_work(&esg_policy->irq_work, esgov_irq_work);
	mutex_init(&esg_policy->work_lock);

	wake_up_process(thread);

	return 0;
}

static void esgov_policy_free(struct esgov_policy *esg_policy)
{
	kfree(esg_policy);
}

static int esgov_init(struct cpufreq_policy *policy)
{
	struct esgov_policy *esg_policy;
	int ret = 0;
	int cpu;

	if (policy->governor_data)
		return -EBUSY;

	esg_policy = per_cpu(esgov_policy, policy->cpu);
	if (per_cpu(esgov_policy, policy->cpu)) {
		pr_info("%s: Already allocated esgov_policy\n", __func__);
		goto complete_esg_init;
	}

	esg_policy = esgov_policy_alloc(policy);
	if (!esg_policy) {
		ret = -ENOMEM;
		goto failed_to_init;
	}

	ret = esgov_kthread_create(esg_policy);
	if (ret)
		goto free_esg_policy;

	for_each_cpu(cpu, &esg_policy->cpus)
		per_cpu(esgov_policy, cpu) = esg_policy;

complete_esg_init:
	policy->governor_data = esg_policy;
	esg_policy->min = policy->min;
	esg_policy->max = policy->max;
	esg_policy->min_cap = find_allowed_capacity(policy->cpu, policy->min, 0);
	esg_policy->max_cap = find_allowed_capacity(policy->cpu, policy->max, 0);
	esg_policy->enabled = true;;
	esg_policy->last_caller = UINT_MAX;

	return 0;

free_esg_policy:
	esgov_policy_free(esg_policy);

failed_to_init:
	pr_err("initialization failed (error %d)\n", ret);

	return ret;
}

static void esgov_exit(struct cpufreq_policy *policy)
{
	struct esgov_policy *esg_policy = per_cpu(esgov_policy, policy->cpu);

	esg_policy->enabled = false;
	policy->governor_data = NULL;
}

static unsigned int get_next_freq(struct esgov_policy *esg_policy,
				unsigned long util, unsigned long max)
{
	struct cpufreq_policy *policy = esg_policy->policy;
	unsigned int freq;

	freq = (policy->user_policy.max * util) / max;
	freq = cpufreq_driver_resolve_freq(policy, freq);
	return clamp_val(freq, esg_policy->min, esg_policy->max);
}

/* return the max_util of this cpu */
static unsigned int esgov_calc_cpu_target_util(struct esgov_cpu *esg_cpu,
			int max, int org_pelt_util, int pelt_util_diff, int nr_running)
{
	int util, step_util, pelt_util, io_util;
	int org_io_util = esg_cpu->io_util;
	int org_step_util = esg_cpu->step_util;

	/* calculate boost util */
	io_util = org_io_util;

	/*
	 * calculate pelt_util
	 * apply pelt_util_diff and then apply 25% margin to sched_util
	 * pelt_util_diff: util_diff by migrating task
	 */
	pelt_util = org_pelt_util + pelt_util_diff;
	pelt_util = max(pelt_util, 0);

	if (!esg_apply_migov)
		pelt_util = pelt_util + (pelt_util >> 2);
	else
		pelt_util = esg_apply_migov_boost(esg_cpu->cpu, pelt_util);

	pelt_util = min(max, pelt_util);

	/*
	 * calculate step util
	 * if there is no running task, step util is always 0
	 */
	step_util = nr_running ? org_step_util : 0;
	step_util = (esg_cpu->active_ratio == 1024) ? step_util : 0;

	/* find max util */
	util = max(pelt_util, step_util);
	util = max(util, io_util);

	/* apply emstune_boost */
	util = emstune_freq_boost(esg_cpu->cpu, util);

	if (nr_running == -1)
		trace_esg_cpu_util(esg_cpu->cpu, nr_running, pelt_util_diff, org_io_util,
			io_util, org_step_util, step_util, org_pelt_util, pelt_util, max, util);

	return util;

}

/* return max util of the cluster of this cpu */
static unsigned int esgov_get_target_util(struct esgov_policy *esg_policy,
						u64 time, unsigned long max)
{
	unsigned long max_util = 0;
	unsigned int cpu;

	/* get max util in the cluster */
	for_each_cpu(cpu, esg_policy->policy->cpus) {
		struct esgov_cpu *esg_cpu = &per_cpu(esgov_cpu, cpu);

		esg_cpu->util = esgov_calc_cpu_target_util(
					esg_cpu, max, esg_cpu->pelt_util, 0, -1);

		if (esg_cpu->util > max_util)
			max_util = esg_cpu->util;
	}

	return max_util;
}

#define HIST_SIZE 10
static int dec_hist_idx(int idx)
{
	idx = idx - 1;
	if (idx < 0)
		return HIST_SIZE - 1;
	return idx;
}

/* update the step_util */
static unsigned long esgov_get_step_util(struct esgov_cpu *esg_cpu, unsigned long max) {
	struct esgov_policy *esg_policy = per_cpu(esgov_policy, esg_cpu->cpu);
	struct rq *rq = cpu_rq(esg_cpu->cpu);
	struct part *pa = &rq->pa;
	unsigned int freq;
	int active_ratio = 0, util, prev_idx, hist_count = 0, idx;
	int patient_tick = esg_policy->patient_mode;

	if (unlikely(!esg_policy->step))
		return 0;

	if (esg_cpu->last_idx == pa->hist_idx)
		return esg_cpu->step_util;

	/* get active ratio for patient mode */
	idx = esg_cpu->last_idx = pa->hist_idx;
	while (hist_count++ < patient_tick) {
		active_ratio += pa->hist[idx];
		idx = dec_hist_idx(idx);
	}
	active_ratio = patient_tick ? (active_ratio / patient_tick) : 1024;
	esg_cpu->active_ratio = active_ratio;

	/* get active ratio for step util */
	prev_idx = dec_hist_idx(esg_cpu->last_idx);
	active_ratio = (pa->hist[esg_cpu->last_idx] + pa->hist[prev_idx]) >> 1;

	/* update the capacity */
	freq = esg_cpu->step_util * (esg_policy->policy->max / max);
	esg_cpu->capacity = find_allowed_capacity(esg_cpu->cpu, freq, esg_policy->step_power);

	/* calculate step_util */
	util = (esg_cpu->capacity * active_ratio) >> SCHED_CAPACITY_SHIFT;

	trace_esg_cpu_step_util(esg_cpu->cpu, esg_cpu->capacity, active_ratio, max, util);

	return util;
}

 /* update cpu util */
unsigned long ml_boosted_cpu_util(int cpu);
static void esgov_update_cpu_util(struct esgov_policy *esg_policy, u64 time, unsigned long max)
{
	int cpu;

	for_each_cpu(cpu, esg_policy->policy->cpus) {
		struct esgov_cpu *esg_cpu = &per_cpu(esgov_cpu, cpu);
		struct rq *rq = cpu_rq(cpu);

		/* update iowait boost util */
		esg_cpu->io_util = esgov_iowait_apply(esg_cpu, time, max);

		/* update sched_util */
		esg_cpu->pelt_util = ml_boosted_cpu_util(cpu) + cpu_util_rt(rq);

		/* update step_util, If cpu is idle, we want to ignore step_util */
		esg_cpu->step_util = esgov_get_step_util(esg_cpu, max);
	}
}

static bool esgov_check_rate_delay(struct esgov_policy *esg_policy, u64 time)
{
	s64 delta_ns = time - esg_policy->last_freq_update_time;

	if (delta_ns < esg_policy->rate_delay_ns)
		return false;

	return true;
}

#define ESG_MAX_DELAY_PERIODS 5
/*
 * Return true if we can delay frequency update because the requested frequency
 * change is not large enough, and false if it is large enough. The condition
 * for determining large enough compares the number of frequency level change
 * vs., elapsed time since last frequency update. For example,
 * ESG_MAX_DELAY_PERIODS of 5 would mean immediate frequency change is allowed
 * only if the change in frequency level is greater or equal to 5;
 * It also means change in frequency level equal to 1 would need to
 * wait 5 ticks for it to take effect.
 */
static bool esgov_postpone_freq_update(struct esgov_policy *esg_policy,
		int cpu, u64 time, unsigned int target_freq)
{
	unsigned int diff_num_levels, num_periods, elapsed, margin;

	if (!esg_apply_migov)
		return false;

	elapsed = time - esg_policy->last_freq_update_time;
	margin  = esg_policy->rate_delay_ns >> 2;
	num_periods = (elapsed + margin) / esg_policy->rate_delay_ns;
	if (num_periods > ESG_MAX_DELAY_PERIODS)
		return false;

	diff_num_levels = get_diff_num_levels(cpu, target_freq,
			esg_policy->policy->cur);
	if (diff_num_levels > ESG_MAX_DELAY_PERIODS - num_periods)
		return false;
	else
		return true;
}

static void
esgov_update(struct update_util_data *hook, u64 time, unsigned int flags)
{
	struct esgov_cpu *esg_cpu = container_of(hook, struct esgov_cpu, update_util);
	struct esgov_policy *esg_policy = esg_cpu->esg_policy;
	unsigned long max = arch_scale_cpu_capacity(NULL, esg_cpu->cpu);
	unsigned int target_util, target_freq;

	/* check iowait boot */
	esgov_iowait_boost(esg_cpu, time, flags);
	esg_cpu->last_update = time;

	if (!cpufreq_this_cpu_can_update(esg_policy->policy))
		return;

	/*
	 * try to hold lock.
	 * If somebody is holding this lock, this updater(cpu) will be failed
	 * to update freq because somebody already updated or will update
	 * the last_update_time very close with now
	 */
	if (!raw_spin_trylock(&esg_policy->update_lock))
		return;

	/* check rate delay */
	if (!esgov_check_rate_delay(esg_policy, time))
		goto out;

	/* update cpu_util of this cluster */
	esgov_update_cpu_util(esg_policy, time, max);

	/* update target util of the cluster of this cpu */
	target_util = esgov_get_target_util(esg_policy, time, max);

	/* get target freq for new target util */
	target_freq = get_next_freq(esg_policy, target_util, max);
	if (esg_policy->target_freq == target_freq)
		goto out;

	if (esgov_postpone_freq_update(esg_policy, esg_cpu->cpu, time, target_freq))
		goto out;

	if (!esg_policy->work_in_progress) {
		esg_policy->last_caller = smp_processor_id();
		esg_policy->work_in_progress = true;
		esg_policy->util = target_util;
		esg_policy->target_freq = target_freq;
		esg_policy->last_freq_update_time = time;
		trace_esg_req_freq(esg_policy->policy->cpu,
			esg_policy->util, esg_policy->target_freq);
		irq_work_queue(&esg_policy->irq_work);
	}
out:
	raw_spin_unlock(&esg_policy->update_lock);
}

static int esgov_start(struct cpufreq_policy *policy)
{
	struct esgov_policy *esg_policy = policy->governor_data;
	unsigned int cpu;

	/* TODO: We SHOULD implement FREQVAR-RATE-DELAY Base on SchedTune */
	esg_policy->last_freq_update_time = 0;
	esg_policy->target_freq = 0;
	for_each_cpu(cpu, policy->cpus) {
		struct esgov_cpu *esg_cpu = &per_cpu(esgov_cpu, cpu);
		esg_cpu->esg_policy = esg_policy;
		esg_cpu->cpu = cpu;
		esg_cpu->min =
			(SCHED_CAPACITY_SCALE * policy->cpuinfo.min_freq) /
			policy->cpuinfo.max_freq;
	}

	for_each_cpu(cpu, policy->cpus) {
		struct esgov_cpu *esg_cpu = &per_cpu(esgov_cpu, cpu);
		cpufreq_add_update_util_hook(cpu, &esg_cpu->update_util,
							esgov_update);
	}

	return 0;
}

static void esgov_stop(struct cpufreq_policy *policy)
{
	struct esgov_policy *esg_policy = policy->governor_data;
	unsigned int cpu;

	for_each_cpu(cpu, policy->cpus)
		cpufreq_remove_update_util_hook(cpu);

	synchronize_sched();
	irq_work_sync(&esg_policy->irq_work);
}

static void esgov_limits(struct cpufreq_policy *policy)
{
	struct esgov_policy *esg_policy = policy->governor_data;

	mutex_lock(&esg_policy->work_lock);
	cpufreq_policy_apply_limits(policy);
	mutex_unlock(&esg_policy->work_lock);
}

struct cpufreq_governor energy_step_gov = {
	.name			= "energy_step",
	.owner			= THIS_MODULE,
	.dynamic_switching	= true,
	.init			= esgov_init,
	.exit			= esgov_exit,
	.start			= esgov_start,
	.stop			= esgov_stop,
	.limits			= esgov_limits,
};

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_ENERGYSTEP
/*
 * return next maximum util of this group when task moves to dst_cpu
 * grp_cpu: one of the cpu of target group to get next maximum util
 * dst_cpu: dst_cpu of task
*/
int get_gov_next_cap(int grp_cpu, int dst_cpu, struct tp_env *env)
{
	struct esgov_policy *esg_policy = per_cpu(esgov_policy, grp_cpu);
	struct task_struct *p = env->p;
	int cpu, src_cpu = task_cpu(p);
	int task_util, max_util = 0;

	if (unlikely(!esg_policy || !esg_policy->enabled))
		return -ENODEV;

	/* get task util and convert to uss */
	task_util = ml_task_util_est(p);
	if (p->sse)
		task_util = (task_util * capacity_ratio(src_cpu, USS)) >> SCHED_CAPACITY_SHIFT;

	if (esg_policy->min_cap >= esg_policy->max_cap)
		return esg_policy->max_cap;

	/* get max util of the cluster of this cpu */
	for_each_cpu(cpu, esg_policy->policy->cpus) {
		struct esgov_cpu *esg_cpu = &per_cpu(esgov_cpu, cpu);
		unsigned int max = arch_scale_cpu_capacity(NULL, cpu);
		int cpu_util, pelt_util;
		int nr_running = env->nr_running[cpu];

		pelt_util = env->cpu_util[cpu];

		if (cpu != dst_cpu && cpu!= src_cpu) {
			cpu_util = esgov_calc_cpu_target_util(esg_cpu, max,
					pelt_util, 0, nr_running);
		} else if (cpu == dst_cpu && cpu != src_cpu) {
			/* util of dst_cpu (when migrating task)*/
			cpu_util = esgov_calc_cpu_target_util(esg_cpu, max,
					pelt_util, task_util, nr_running);
		} else if (cpu != dst_cpu && cpu == src_cpu) {
			/*  util of src_cpu (when migrating task) */
			cpu_util = esgov_calc_cpu_target_util(esg_cpu, max,
					pelt_util, -task_util, nr_running);
		} else {
			/* util of src_cpu (when task stay on the src_cpu) */
			cpu_util = esgov_calc_cpu_target_util(esg_cpu, max,
					pelt_util, 0, nr_running);
		}

		if (cpu_util > max_util)
			max_util = cpu_util;
	}

	/* max floor depends on CPUFreq min/max lock */
	max_util = max(esg_policy->min_cap, max_util);
	max_util = min(esg_policy->max_cap, max_util);

	return max_util;
}

unsigned long cpufreq_governor_get_util(unsigned int cpu)
{
	struct esgov_cpu *esg_cpu = &per_cpu(esgov_cpu, cpu);

	if (!esg_cpu)
		return 0;

	return esg_cpu->util;
}

unsigned int cpufreq_governor_get_freq(int cpu)
{
	struct esgov_policy *esg_policy;
	unsigned int freq;
	unsigned long flags;

	if (cpu < 0)
		return 0;

	esg_policy = per_cpu(esgov_policy, cpu);
	if (!esg_policy)
		return 0;

	raw_spin_lock_irqsave(&esg_policy->update_lock, flags);
	freq = esg_policy->target_freq;
	raw_spin_unlock_irqrestore(&esg_policy->update_lock, flags);

	return freq;
}

struct cpufreq_governor *cpufreq_default_governor(void)
{
	return &energy_step_gov;
}
#endif

static int __init esgov_register(void)
{
	esg_kobj = kobject_create_and_add("energy_step", ems_kobj);
	if (!esg_kobj)
		return -EINVAL;

	emstune_register_mode_update_notifier(&esg_mode_update_notifier);

	cpufreq_register_notifier(&esg_cpufreq_policy_notifier,
					CPUFREQ_POLICY_NOTIFIER);

	return cpufreq_register_governor(&energy_step_gov);
}
fs_initcall(esgov_register);

