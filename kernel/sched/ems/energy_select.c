/*
 * Energy efficient cpu selection
 *
 * Copyright (C) 2018 Samsung Electronics Co., Ltd
 */

#include <trace/events/ems.h>
#include <trace/events/ems_debug.h>

#include "../sched.h"
#include "ems.h"

#define get_energy_table(cpu)	&per_cpu(energy_table, cpu)

static int get_next_cap(struct tp_env *env, int grp_cpu,
			int dst_cpu, unsigned int *cap_idx)
{
	int idx, cpu;
	unsigned int next_util = 0;
	struct energy_table *table;

	table = get_energy_table(grp_cpu);
	/* energy table does not exist */
	if (!table->nr_states)
		return -1;

	/* Get next capacity of cpu in coregroup with task */
	for_each_cpu(cpu, cpu_coregroup_mask(grp_cpu)) {
		int cpu_util;

		if (cpu == dst_cpu) {
			cpu_util = env->cpu_util_with[cpu];
		} else {
			cpu_util = env->cpu_util_wo[cpu][SSE];
			cpu_util = (cpu_util * capacity_ratio(cpu, USS)) >> SCHED_CAPACITY_SHIFT;
			cpu_util += env->cpu_util_wo[cpu][USS];
		}

		if (cpu_util < next_util)
			continue;

		next_util = cpu_util;
	}
	/* Find the capacity index according to next capacity */
	*cap_idx = table->nr_states - 1;
	for (idx = 0; idx < table->nr_states; idx++) {
		if (table->states[idx].cap >= next_util) {
			*cap_idx = idx;
			break;
		}
	}

	return 0;
}

#define PREV_ADVANTAGE_SHIFT	4
static unsigned int
compute_system_energy(struct tp_env *env, int dst_cpu)
{
	int grp_cpu, sse = env->p->sse;
	unsigned int task_util;
	unsigned int energy = 0;

	task_util = env->task_util;

	/* apply weight */
//	task_util = (task_util * 100) / env->eff_weight[dst_cpu];

	for_each_cpu(grp_cpu, cpu_active_mask) {
		struct energy_table *table;
		unsigned long power[SSE + 1];
		unsigned int cpu, cap_idx;
		unsigned int grp_cpu_util[SSE + 1] = { 0, 0 };
		unsigned int grp_energy[SSE + 1];
		unsigned int static_power = 0;
		unsigned int cl_idle_cnt = 0;
		unsigned int num_cpu = 0;

		if (grp_cpu != cpumask_first(cpu_coregroup_mask(grp_cpu)))
			continue;

		/* 1. get next_cap and cap_idx */
		table = get_energy_table(grp_cpu);
		if (get_next_cap(env, grp_cpu, dst_cpu, &cap_idx))
			return 0;

		/* 2. apply weight to power */
		power[USS] = (table->states[cap_idx].power * 100) / env->eff_weight[grp_cpu];
		power[SSE] = (table->states[cap_idx].power_s * 100) / env->eff_weight[grp_cpu];

		/* 3. calculate group util */
		for_each_cpu(cpu, cpu_coregroup_mask(grp_cpu)) {
			grp_cpu_util[USS] += env->cpu_util_wo[cpu][USS];
			grp_cpu_util[SSE] += env->cpu_util_wo[cpu][SSE];
			if (cpu == dst_cpu)
				grp_cpu_util[sse] += task_util;

			if (task_cpu(env->p) == cpu && task_cpu(env->p) == dst_cpu)
				grp_cpu_util[sse] -= (task_util >> PREV_ADVANTAGE_SHIFT);

			if (idle_get_state_idx(cpu_rq(cpu)) >= 0 && cpu != dst_cpu)
				cl_idle_cnt++;
		}


		/* 4. calculate group energy */
		grp_energy[USS] = __compute_energy(grp_cpu_util[USS],
				table->states[cap_idx].cap, power[USS], 0);

		grp_energy[SSE] = __compute_energy(grp_cpu_util[SSE],
				table->states[cap_idx].cap_s, power[SSE], 0);

		energy += (grp_energy[USS] + grp_energy[SSE]);

		/* 5. apply static power */
		if (grp_cpu) {
			num_cpu = cpumask_weight(cpu_coregroup_mask(grp_cpu));
			static_power = num_cpu == cl_idle_cnt ? 0 : (table->states[cap_idx].static_power * num_cpu);
			energy += (static_power << SCHED_CAPACITY_SHIFT);
		}

		trace_ems_compute_energy(env->p, task_util, env->eff_weight[grp_cpu],
			dst_cpu, grp_cpu, grp_cpu_util[USS], grp_cpu_util[SSE],
			table->states[cap_idx].cap, table->states[cap_idx].cap_s,
			power[USS], power[SSE],
			(grp_energy[USS] + grp_energy[SSE]));
	}

	return energy;
}
static void find_energy_candidates(struct tp_env *env, struct cpumask *candidates)
{
	int grp_cpu, src_cpu = task_cpu(env->p);
	int org_task_util = env->task_util;

	if (env->p->sse)
		org_task_util = (org_task_util * capacity_ratio(src_cpu, USS)) >> SCHED_CAPACITY_SHIFT;

	cpumask_clear(candidates);

	for_each_cpu(grp_cpu, cpu_active_mask) {
		int cpu, min_cpu = -1, min_util = INT_MAX;
		unsigned long capacity = capacity_cpu(grp_cpu, USS);

		if (grp_cpu != cpumask_first(cpu_coregroup_mask(grp_cpu)))
			continue;

		for_each_cpu_and(cpu,
			cpu_coregroup_mask(grp_cpu), &env->ontime_fit_cpus) {
			int task_util = org_task_util;
			int cpu_util;

			cpu_util = env->cpu_util_with[cpu];
			/*
			 * give dst_cpu dis-advantage about 7.5% of task util
			 * when dst_cpu is different with src_cpu
			 */
			if (cpu == src_cpu) {
				cpu_util -= (task_util >> PREV_ADVANTAGE_SHIFT);
				cpu_util = max(cpu_util, 0);
			}

			/* check overutil with fair util only */
			if (cpu_overutilized(capacity, cpu_util))
				continue;

			/* Add rt util */
			cpu_util += env->cpu_rt_util[cpu];

			/* find min util cpu with rt util */
			if (cpu_util <= min_util) {
				min_util = cpu_util;
				min_cpu = cpu;
			}
		}

		if (min_cpu >= 0)
			cpumask_set_cpu(min_cpu, candidates);
	}
	trace_ems_energy_candidates(env->p,
		*(unsigned int *)cpumask_bits(candidates));
}

/* setup cpu and task util */
#define CPU_ACTIVE	(-1)
int find_energy_cpu(struct tp_env *env)
{
	struct cpumask candidates;
	int cpu, energy_cpu = -1, energy_cpu_idle_idx = -10, min_util = INT_MAX;
	unsigned int min_energy = UINT_MAX;

	/* set candidates cpu to find energy cpu */
	find_energy_candidates(env, &candidates);

	/* find energy cpu */
	for_each_cpu(cpu, &candidates) {
		int cpu_util;
		unsigned int energy;
		int idle_idx = idle_get_state_idx(cpu_rq(cpu));

		/* calculate system energy */
		energy = compute_system_energy(env, cpu);

		/* 1. find min_energy cpu */
		if (energy < min_energy)
			goto energy_cpu_found;
		if (energy > min_energy)
			continue;

		/* 2. find min_util cpu when energy is same */
		cpu_util = env->cpu_util_with[cpu];
		if (cpu_util < min_util)
			goto energy_cpu_found;
		if (cpu_util > min_util)
			continue;

		/* 3. find idle cpu when energy, util are same*/
		if ((energy_cpu_idle_idx == CPU_ACTIVE) && (idle_idx > CPU_ACTIVE))
			goto energy_cpu_found;

		/* 4. find shallower idle cpu when energy, util, idle-status are same*/
		if ((energy_cpu_idle_idx > CPU_ACTIVE) && (energy_cpu_idle_idx > idle_idx))
			goto energy_cpu_found;
		else if (energy_cpu_idle_idx < idle_idx)
			continue;

		/* 5. find randomized cpu to prevent seleting same cpu continuously */
		if (cpu_util & 0x1)
			continue;
energy_cpu_found:
		min_energy = energy;
		energy_cpu = cpu;
		min_util = cpu_util;
		energy_cpu_idle_idx = idle_idx;
	}

	return energy_cpu;
}
