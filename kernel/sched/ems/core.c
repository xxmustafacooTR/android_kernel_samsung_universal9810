/*
 * Core Exynos Mobile Scheduler
 *
 * Copyright (C) 2018 Samsung Electronics Co., Ltd
 * Park Bumgyu <bumgyu.park@samsung.com>
 */

#include <linux/ems.h>

#include "ems.h"
#include "../sched.h"
#include "../tune.h"

#define CREATE_TRACE_POINTS
#include <trace/events/ems.h>

unsigned long task_util(struct task_struct *p)
{
	if (rt_task(p))
		return p->rt.avg.util_avg;
	else
		return p->se.avg.util_avg;
}

extern int capacity_margin;
static int select_proper_cpu(struct task_struct *p)
{
		unsigned long best_idle_util = ULONG_MAX;
	unsigned long target_capacity = 0;
	unsigned long max_spare_cap = 0;
	int best_idle_cstate = INT_MAX;
	int best_active_cpu = -1;
	int best_idle_cpu = -1;
	int cpu, cpu_cl;
	int target_cpu;
	struct task_struct *curr;

	for_each_cpu(cpu_cl, cpu_active_mask) {
		struct cpumask mask;

		if (cpu_cl != cpumask_first(cpu_coregroup_mask(cpu_cl)))
			continue;

		cpumask_and(&mask, cpu_coregroup_mask(cpu_cl), tsk_cpus_allowed(p));

		for_each_cpu_and(cpu, &mask, cpu_active_mask) {
			unsigned long capacity_orig = capacity_orig_of(cpu);
			unsigned long new_util = ml_task_attached_cpu_util(cpu, p);
			unsigned long spare_cap;

			new_util = max(new_util, ml_boosted_task_util(p));
			/* Skip over-capacity cpu */
			if (new_util * capacity_margin > capacity_orig * SCHED_CAPACITY_SCALE)
				continue;

			if (idle_cpu(cpu)) {
				int idle_idx = idle_get_state_idx(cpu_rq(cpu));

				/* find shallowest idle state cpu */
				if (capacity_orig >= target_capacity &&
				    idle_idx > best_idle_cstate)
					continue;

				if (idle_idx == best_idle_cstate &&
				    new_util >= best_idle_util)
					continue;

				/* Keep track of best idle CPU */
				target_capacity = capacity_orig;
				best_idle_cstate = idle_idx;
				best_idle_util = new_util;
				best_idle_cpu = cpu;
				continue;
			}

			/* Find maximum spare capacity CPU */
			spare_cap = capacity_orig - new_util;
			if (spare_cap > max_spare_cap) {
			    target_capacity = capacity_orig;
			    max_spare_cap = spare_cap;
			    best_active_cpu = cpu;
			}
		}

		/*
		 * Try to pack tasks to smallest cluster as possible to save energy
		 */
		if (cpu_selected(best_active_cpu) || cpu_selected(best_idle_cpu))
			break;
	}

	if (best_active_cpu != -1 && best_idle_cpu != -1) {
		curr = READ_ONCE(cpu_rq(best_active_cpu)->curr);
		if (curr && schedtune_prefer_perf(curr) > 0) {
			return best_idle_cpu;
		}
	}

	target_cpu = cpu_selected(best_active_cpu) ? best_active_cpu : best_idle_cpu;

	return cpu_selected(target_cpu) ? target_cpu : task_cpu(p);
}

extern void sync_entity_load_avg(struct sched_entity *se);

static int eff_mode;

int exynos_wakeup_balance(struct task_struct *p, int prev_cpu, int sd_flag, int sync)
{
	int target_cpu = -1;
	char state[30] = "fail";

	/*
	 * Since the utilization of a task is accumulated before sleep, it updates
	 * the utilization to determine which cpu the task will be assigned to.
	 * Exclude new task.
	 */
	if (!(sd_flag & SD_BALANCE_FORK)) {
		unsigned long old_util = ml_task_util(p);

		sync_entity_load_avg(&p->se);
		/* update the band if a large amount of task util is decayed */
		update_band(p, old_util);
	}

	target_cpu = select_service_cpu(p);
	if (cpu_selected(target_cpu)) {
		strcpy(state, "service cpu");
		goto out;
	}

	/*
	 * Priority 1 : ontime task
	 *
	 * If task which has more utilization than threshold wakes up, the task is
	 * classified as "ontime task" and assigned to performance cpu. Conversely,
	 * if heavy task that has been classified as ontime task sleeps for a long
	 * time and utilization becomes small, it is excluded from ontime task and
	 * is no longer guaranteed to operate on performance cpu.
	 *
	 * Ontime task is very sensitive to performance because it is usually the
	 * main task of application. Therefore, it has the highest priority.
	 */
	target_cpu = ontime_task_wakeup(p, sync);
	if (cpu_selected(target_cpu)) {
		strcpy(state, "ontime migration");
		goto out;
	}

	/*
	 * Priority 2 : prefer-perf
	 *
	 * Prefer-perf is a function that operates on cgroup basis managed by
	 * schedtune. When perfer-perf is set to 1, the tasks in the group are
	 * preferentially assigned to the performance cpu.
	 *
	 * It has a high priority because it is a function that is turned on
	 * temporarily in scenario requiring reactivity(touch, app laucning).
	 */
	target_cpu = prefer_perf_cpu(p);
	if (cpu_selected(target_cpu)) {
		strcpy(state, "prefer-perf");
		goto out;
	}

	/*
	 * Priority 3 : task band
	 *
	 * The tasks in a process are likely to interact, and its operations are
	 * sequential and share resources. Therefore, if these tasks are packed and
	 * and assign on a specific cpu or cluster, the latency for interaction
	 * decreases and the reusability of the cache increases, thereby improving
	 * performance.
	 *
	 * The "task band" is a function that groups tasks on a per-process basis
	 * and assigns them to a specific cpu or cluster. If the attribute "band"
	 * of schedtune.cgroup is set to '1', task band operate on this cgroup.
	 */
	target_cpu = band_play_cpu(p);
	if (cpu_selected(target_cpu)) {
		strcpy(state, "task band");
		goto out;
	}

	/*
	 * Priority 4 : global boosting
	 *
	 * Global boost is a function that preferentially assigns all tasks in the
	 * system to the performance cpu. Unlike prefer-perf, which targets only
	 * group tasks, global boost targets all tasks. So, it maximizes performance
	 * cpu utilization.
	 *
	 * Typically, prefer-perf operates on groups that contains UX related tasks,
	 * such as "top-app" or "foreground", so that major tasks are likely to be
	 * assigned to performance cpu. On the other hand, global boost assigns
	 * all tasks to performance cpu, which is not as effective as perfer-perf.
	 * For this reason, global boost has a lower priority than prefer-perf.
	 */
	target_cpu = global_boosting(p);
	if (cpu_selected(target_cpu)) {
		strcpy(state, "global boosting");
		goto out;
	}

	if (eff_mode) {
		target_cpu = select_best_cpu(p, prev_cpu, sd_flag, sync);
		if (cpu_selected(target_cpu)) {
			strcpy(state, "best");
			goto out;
		}
	}

	/*
	 * Priority 5 : prefer-idle
	 *
	 * Prefer-idle is a function that operates on cgroup basis managed by
	 * schedtune. When perfer-idle is set to 1, the tasks in the group are
	 * preferentially assigned to the idle cpu.
	 *
	 * Prefer-idle has a smaller performance impact than the above. Therefore
	 * it has a relatively low priority.
	 */
	target_cpu = prefer_idle_cpu(p);
	if (cpu_selected(target_cpu)) {
		strcpy(state, "prefer-idle");
		goto out;
	}

	/*
	 * Priority 6 : energy cpu
	 *
	 * A scheduling scheme based on cpu energy, find the least power consumption
	 * cpu with energy table when assigning task.
	 */
	target_cpu = select_energy_cpu(p, prev_cpu, sd_flag);
	if (cpu_selected(target_cpu)) {
		strcpy(state, "energy cpu");
		goto out;
	}

	/*
	 * Priority 7 : proper cpu
	 *
	 * If the task failed to find a cpu to assign from the above conditions,
	 * it means that assigning task to any cpu does not have performance and
	 * power benefit. In this case, select cpu for balancing cpu utilization.
	 */
	target_cpu = select_proper_cpu(p);
	if (cpu_selected(target_cpu))
		strcpy(state, "proper cpu");

out:
	trace_ems_wakeup_balance(p, target_cpu, state);
	return target_cpu;
}

static ssize_t show_eff_mode(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int ret = 0;

	ret += snprintf(buf + ret, 10, "%d\n", eff_mode);

	return ret;
}

static ssize_t store_eff_mode(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf,
		size_t count)
{
	unsigned int input;

	if (!sscanf(buf, "%d", &input))
		return -EINVAL;

	eff_mode = input;

	return count;
}

static struct kobj_attribute eff_mode_attr =
__ATTR(eff_mode, 0644, show_eff_mode, store_eff_mode);

static ssize_t show_sched_topology(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int cpu;
	struct sched_domain *sd;
	int ret = 0;

	rcu_read_lock();
	for_each_possible_cpu(cpu) {
		int sched_domain_level = 0;

		sd = rcu_dereference_check_sched_domain(cpu_rq(cpu)->sd);
		while (sd->parent) {
			sched_domain_level++;
			sd = sd->parent;
		}

		for_each_lower_domain(sd) {
			ret += snprintf(buf + ret, 50,
				"[lv%d] cpu%d: sd->span=%#x sg->span=%#x\n",
				sched_domain_level, cpu,
				*(unsigned int *)cpumask_bits(sched_domain_span(sd)),
				*(unsigned int *)cpumask_bits(sched_group_cpus(sd->groups)));
			sched_domain_level--;
		}
		ret += snprintf(buf + ret,
			50, "----------------------------------------\n");
	}
	rcu_read_unlock();

	return ret;
}

static struct kobj_attribute sched_topology_attr =
__ATTR(sched_topology, 0444, show_sched_topology, NULL);

struct kobject *ems_kobj;

static int __init init_sysfs(void)
{
	int ret = 0;
	ems_kobj = kobject_create_and_add("ems", kernel_kobj);

	ret = sysfs_create_file(ems_kobj, &sched_topology_attr.attr);
	if (ret)
		return ret;

	ret = sysfs_create_file(ems_kobj, &eff_mode_attr.attr);

	return ret;
}
core_initcall(init_sysfs);

void __init init_ems(void)
{
	alloc_bands();

	init_part();
}
