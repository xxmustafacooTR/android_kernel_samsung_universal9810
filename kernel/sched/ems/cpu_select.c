/*
 * Exynos Mobile Scheduler CPU selection
 *
 * Copyright (C) 2020 Samsung Electronics Co., Ltd
 */

#include <trace/events/ems.h>
#include <trace/events/ems_debug.h>
#include <dt-bindings/soc/samsung/ems.h>

#include "../sched.h"
#include "ems.h"

unsigned int
__compute_energy(unsigned long util, unsigned long cap,
				unsigned long dp, unsigned long sp)
{
	return 1 + ((dp * util << SCHED_CAPACITY_SHIFT) / cap)
				+ (sp << SCHED_CAPACITY_SHIFT);
}

static unsigned int
compute_energy(struct energy_table *table, struct task_struct *p,
			int target_cpu, int cap_idx)
{
	unsigned int energy;

	energy = __compute_energy(__ml_cpu_util_with(target_cpu, p, SSE),
				table->states[cap_idx].cap_s,
				table->states[cap_idx].power_s,
				table->states[cap_idx].static_power);
	energy += __compute_energy(__ml_cpu_util_with(target_cpu, p, USS),
				table->states[cap_idx].cap,
				table->states[cap_idx].power,
				table->states[cap_idx].static_power);

	return energy;
}

/******************************************************************************
 * best efficiency cpu selection                                              *
 ******************************************************************************/
static unsigned int
compute_efficiency(struct tp_env *env, int target_cpu, unsigned int eff_weight)
{
	struct energy_table *table = get_energy_table(target_cpu);
	struct task_struct *p = env->p;
	unsigned long next_cap = 0;
	unsigned long capacity, util, energy;
	unsigned long long eff;
	unsigned int cap_idx;
	int i;

	/* energy table does not exist */
	if (!table->nr_states)
		return 0;

	/* Get next capacity of cpu in coregroup with task */
	next_cap = get_gov_next_cap(target_cpu, target_cpu, env);
	if ((int)next_cap < 0)
		next_cap = default_get_next_cap(target_cpu, p);

	/* Find the capacity index according to next capacity */
	cap_idx = table->nr_states - 1;
	for (i = 0; i < table->nr_states; i++) {
		if (table->states[i].cap >= next_cap) {
			cap_idx = i;
			break;
		}
	}

	capacity = table->states[cap_idx].cap;
	util = ml_cpu_util_with(target_cpu, p);
	energy = compute_energy(table, p, target_cpu, cap_idx);

	/*
	 * Compute performance efficiency
	 *  efficiency = (capacity / util) / energy
	 */
	eff = (capacity << SCHED_CAPACITY_SHIFT * 2) / energy;
	eff = (eff * eff_weight) / 100;

	trace_ems_compute_eff(p, target_cpu, util, eff_weight,
						capacity, energy, (unsigned int)eff);

	return (unsigned int)eff;
}

#define INVALID_CPU	-1
static int find_best_eff_cpu(struct tp_env *env)
{
	unsigned int eff;
	unsigned int best_eff = 0, prev_eff = 0;
	int best_cpu = INVALID_CPU, prev_cpu = task_cpu(env->p);
	int cpu;

	if (cpumask_empty(&env->idle_candidates))
		goto skip_find_idle;

	/* find best efficiency cpu among idle */
	for_each_cpu(cpu, &env->idle_candidates) {
		eff = compute_efficiency(env, cpu, env->eff_weight[cpu]);

		if (cpu == prev_cpu)
			prev_eff = eff;

		if (eff > best_eff) {
			best_eff = eff;
			best_cpu = cpu;
		}
	}

skip_find_idle:
	if (cpumask_empty(&env->candidates))
		goto skip_find_running;

	/* find best efficiency cpu among running */
	for_each_cpu(cpu, &env->candidates) {
		eff = compute_efficiency(env, cpu, env->eff_weight[cpu]);

		if (cpu == prev_cpu)
			prev_eff = eff;

		if (eff > best_eff) {
			best_eff = eff;
			best_cpu = cpu;
		}
	}

skip_find_running:
	if (best_cpu == INVALID_CPU)
		return INVALID_CPU;

	if (prev_eff && cpumask_test_cpu(best_cpu, cpu_coregroup_mask(prev_cpu))) {
		/*
		 * delta of efficiency between prev and best cpu is under 6.25%,
		 * keep the task on prev cpu
		 */
		if (prev_eff && (best_eff - prev_eff) < (prev_eff >> 4))
			return prev_cpu;
	}

	return best_cpu;
}

/******************************************************************************
 * best performance cpu selection                                             *
 ******************************************************************************/
static int find_biggest_spare_cpu(int sse,
			struct cpumask *candidates, int *cpu_util_wo)
{
	int cpu, max_cpu = INVALID_CPU, max_cap = 0;

	for_each_cpu(cpu, candidates) {
		int curr_cap;
		int spare_cap;

		/* get current cpu capacity */
		curr_cap = (capacity_cpu(cpu, sse)
				* arch_scale_freq_capacity(cpu)) >> SCHED_CAPACITY_SHIFT;
		spare_cap = curr_cap - cpu_util_wo[cpu];

		if (max_cap < spare_cap) {
			max_cap = spare_cap;
			max_cpu = cpu;
		}
	}
	return max_cpu;
}

static int set_candidate_cpus(struct tp_env *env, int task_util, const struct cpumask *mask,
		struct cpumask *idle_candidates, struct cpumask *active_candidates, int *cpu_util_wo)
{
	int cpu, bind = false;

	if (unlikely(!cpumask_equal(mask, cpu_possible_mask)))
		bind = true;

	for_each_cpu_and(cpu, mask, cpu_active_mask) {
		unsigned long capacity = capacity_cpu(cpu, env->p->sse);

		if (!cpumask_test_cpu(cpu, &env->p->cpus_allowed))
			continue;

		/* remove overfit cpus from candidates */
		if (likely(!bind) && (capacity < (cpu_util_wo[cpu] + task_util)))
			continue;

		if (!cpu_rq(cpu)->nr_running)
			cpumask_set_cpu(cpu, idle_candidates);
		else
			cpumask_set_cpu(cpu, active_candidates);
	}

	return bind;
}

static int find_best_perf_cpu(struct tp_env *env)
{
	struct cpumask idle_candidates, active_candidates;
	int cpu_util_wo[NR_CPUS];
	int cpu, best_cpu = INVALID_CPU;
	int task_util, sse = env->p->sse;
	int bind;

	cpumask_clear(&idle_candidates);
	cpumask_clear(&active_candidates);

	task_util = ml_task_util_est(env->p);
	for_each_cpu(cpu, cpu_active_mask) {
		/* get the ml cpu util wo */
		cpu_util_wo[cpu] = _ml_cpu_util(cpu, env->p->sse);
		if (cpu == task_cpu(env->p))
			cpu_util_wo[cpu] = max(cpu_util_wo[cpu] - task_util, 0);
	}

	bind = set_candidate_cpus(env, task_util, emstune_cpus_allowed(env->p),
			&idle_candidates, &active_candidates, cpu_util_wo);

	/* find biggest spare cpu among the idle_candidates */
	best_cpu = find_biggest_spare_cpu(sse, &idle_candidates, cpu_util_wo);
	if (best_cpu != INVALID_CPU) {
		trace_ems_find_best_idle(env->p, task_util,
			*(unsigned int *)cpumask_bits(&idle_candidates),
			*(unsigned int *)cpumask_bits(&active_candidates), bind, best_cpu);
		return best_cpu;
	}

	/* find biggest spare cpu among the active_candidates */
	best_cpu = find_biggest_spare_cpu(sse, &active_candidates, cpu_util_wo);
	if (best_cpu != INVALID_CPU) {
		trace_ems_find_best_idle(env->p, task_util,
			*(unsigned int *)cpumask_bits(&idle_candidates),
			*(unsigned int *)cpumask_bits(&active_candidates), bind, best_cpu);
		return best_cpu;
	}

	/* if there is no best_cpu, return previous cpu */
	best_cpu = task_cpu(env->p);
	trace_ems_find_best_idle(env->p, task_util,
		*(unsigned int *)cpumask_bits(&idle_candidates),
		*(unsigned int *)cpumask_bits(&active_candidates), bind, best_cpu);

	return best_cpu;
}

/******************************************************************************
 * best energy cpu selection                                                  *
 ******************************************************************************/

int find_best_cpu(struct tp_env *env)
{
	int best_cpu = INVALID_CPU;

	switch (env->sched_policy) {
	case SCHED_POLICY_EFF:
		/* Find best efficiency cpu */
		best_cpu = find_best_eff_cpu(env);
		break;
	case SCHED_POLICY_ENERGY:
		/* Find lowest energy cpu */
		best_cpu = find_energy_cpu(env);
		break;
	case SCHED_POLICY_PERF:
		/* Find best performance cpu */
		best_cpu = find_best_perf_cpu(env);
		break;
	}

	return best_cpu;
}
