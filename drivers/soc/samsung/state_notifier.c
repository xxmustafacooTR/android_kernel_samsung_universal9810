/*
 * State Notifier Driver
 *
 * Copyright (c) 2013-2016, Pranav Vashi <neobuddy89@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/export.h>
#include <linux/module.h>
#include <linux/state_notifier.h>

#define STATE_NOTIFIER "state_notifier"

static struct delayed_work suspend_work;
static struct delayed_work resume_work;
static struct workqueue_struct *susp_wq;
static unsigned int suspend_defer_time = 0;
static unsigned int resume_defer_time = 0;
bool state_suspended;
static bool suspend_in_progress;
//static bool resume_in_progress;

static BLOCKING_NOTIFIER_HEAD(state_notifier_list);

/**
 *	state_register_client - register a client notifier
 *	@nb: notifier block to callback on events
 */
int state_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&state_notifier_list, nb);
}
EXPORT_SYMBOL(state_register_client);

/**
 *	state_unregister_client - unregister a client notifier
 *	@nb: notifier block to callback on events
 */
int state_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&state_notifier_list, nb);
}
EXPORT_SYMBOL(state_unregister_client);

/**
 *	state_notifier_call_chain - notify clients on state_events
 *	@val: Value passed unmodified to notifier function
 *	@v: pointer passed unmodified to notifier function
 *
 */
int state_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&state_notifier_list, val, v);
}
EXPORT_SYMBOL_GPL(state_notifier_call_chain);

static void _suspend_work(struct work_struct *work)
{
	printk("[STATE_NOTIFIER] SUSPENDING\n");
	state_suspended = true;
	state_notifier_call_chain(STATE_NOTIFIER_SUSPEND, NULL);
	suspend_in_progress = false;
}

static void _resume_work(struct work_struct *work)
{
	printk("[STATE_NOTIFIER] RESUMING\n");
	state_suspended = false;
	state_notifier_call_chain(STATE_NOTIFIER_ACTIVE, NULL);
	//resume_in_progress = false;
}

void state_suspend(void)
{
	if (state_suspended || suspend_in_progress)
		return;

	printk("[STATE NOTIFIER] - Suspend Called\n");
	cancel_delayed_work_sync(&resume_work);
	//resume_in_progress = false;
	suspend_in_progress = true;

	queue_delayed_work(susp_wq, &suspend_work,
		msecs_to_jiffies(suspend_defer_time * 1000));
}

void state_resume(void)
{
//	if (resume_in_progress)
//		return;

	if (suspend_in_progress)
		printk("[STATE NOTIFIER] - Suspend Cancelled by Resume\n");
	else
		printk("[STATE NOTIFIER] - Resume Called\n");

	cancel_delayed_work_sync(&suspend_work);
	suspend_in_progress = false;
	//resume_in_progress = true;

	if (state_suspended)
		queue_delayed_work(susp_wq, &resume_work,
			msecs_to_jiffies(resume_defer_time * 1000));
	else
		printk("[STATE_NOTIFIER] Skipping Resume\n");
}

static ssize_t suspend_defer_time_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
        return sprintf(buf, "%u\n", suspend_defer_time);
}

static ssize_t suspend_defer_time_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int val;

	sscanf(buf, "%u\n", &val);

	suspend_defer_time = val;
	return count;
}

static struct kobj_attribute suspend_defer_time_attribute =
	__ATTR(suspend_defer_time, 0664,
		suspend_defer_time_show,
		suspend_defer_time_store);

static ssize_t resume_defer_time_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
        return sprintf(buf, "%u\n", resume_defer_time);
}

static ssize_t resume_defer_time_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int val;

	sscanf(buf, "%u\n", &val);

	resume_defer_time = val;
	return count;
}

static struct kobj_attribute resume_defer_time_attribute =
	__ATTR(resume_defer_time, 0664,
		resume_defer_time_show,
		resume_defer_time_store);

static struct attribute *state_notifier_attrs[] =
{
	&suspend_defer_time_attribute.attr,
	&resume_defer_time_attribute.attr,
	NULL,
};

static struct attribute_group state_notifier_attr_group =
{
	.attrs = state_notifier_attrs,
};

static struct kobject *state_notifier_kobj;

static int state_notifier_init(void)
{
	int sysfs_result;

	state_notifier_kobj = kobject_create_and_add("state_notifier",
		kernel_kobj);

	if (!state_notifier_kobj) {
		pr_err("%s kobject create failed!\n", __FUNCTION__);
		return -ENOMEM;
	}

	sysfs_result = sysfs_create_group(state_notifier_kobj,
		&state_notifier_attr_group);

	if (sysfs_result) {
		pr_info("%s group create failed!\n", __FUNCTION__);
		kobject_put(state_notifier_kobj);
		return -ENOMEM;
	}
	susp_wq = alloc_workqueue("state_susp_wq", WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_HIGHPRI, 1);
	if (!susp_wq)
		pr_err("[State_Notifier] failed to allocate workqueue\n");

	INIT_DELAYED_WORK(&suspend_work, _suspend_work);
	INIT_DELAYED_WORK(&resume_work, _resume_work);

	return 0;
}

static void state_notifier_exit(void)
{
	flush_delayed_work(&suspend_work);
	flush_delayed_work(&resume_work);
	destroy_workqueue(susp_wq);
	if (state_notifier_kobj != NULL) {
		kobject_put(state_notifier_kobj);
	}
}

subsys_initcall(state_notifier_init);
module_exit(state_notifier_exit);

MODULE_AUTHOR("Pranav Vashi <neobuddy89@gmail.com>");
MODULE_DESCRIPTION("State Notifier Driver");
MODULE_LICENSE("GPLv2");