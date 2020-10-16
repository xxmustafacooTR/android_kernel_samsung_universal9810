// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Adithya R <gh0strider.2k18.reborn@gmail.com>.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/battery_saver.h>

#ifdef CONFIG_PCIEASPM_PERFORMANCE
static bool enabled = false;
#else
static bool enabled = true;
#endif
module_param(enabled, bool, 0644);

// returns whether battery saver is enabled or disabled
bool is_battery_saver_on(void)
{
	return enabled;
}

// enable or disable battery saver mode
void update_battery_saver(bool status)
{
	enabled = status;
}
