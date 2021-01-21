/* drivers/media/platform/exynos/jsqz/jsqz-exynos9810-workaround.h
 *
 * Internal header for Samsung jsqz encoder driver
 * for the workaround of a H/W bug of the H/W jsqz encoder chip
 * in Exynos9810 (out of bounds buffer access)
 *
 * Copyright (c) 2017 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrea Bernabei <a.bernabei@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef jsqz_EXYNOS9810_WORKAROUND_H_
#define jsqz_EXYNOS9810_WORKAROUND_H_

#include "jsqz-core.h"

/* The description is before the implementation, for better readability */
int jsqz_exynos9810_setup_workaround(struct device *dev,
				     struct jsqz_task *task,
				     u32 *rounded_up_width,
				     u32 *rounded_up_height,
				     size_t *extension_size);

/*
 * just a random number used to store in the task state that the
 * workaround is needed
 */
#define EXYNOS9810_BUG_WORKAROUND_MAGIC 2002

/*
 * Used by buffer mapping logic to know if the given task requires the Exynos9810
 * bug workaround logic to be applied.
 */
static inline bool is_exynos9810bug_workaround_active(struct jsqz_task *task) {
	return task->user_task.reserved[0] == EXYNOS9810_BUG_WORKAROUND_MAGIC;
}
static inline void set_exynos9810bug_workaround_enable(struct jsqz_task *task, bool status) {
	task->user_task.reserved[0] = status
			? EXYNOS9810_BUG_WORKAROUND_MAGIC
			: 0;
	return;
}

#endif /* jsqz_EXYNOS9810_WORKAROUND_H_ */

