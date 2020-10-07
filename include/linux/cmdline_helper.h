/*
 * Copyright 2016 Jesse Chan <cjx123@outlook.com>
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 */

#ifndef _LINUX_CMDLINE_HELPER_H
#define _LINUX_CMDLINE_HELPER_H

extern char* add_cmdline(const char* original_cmdline, char* cmdline_to_be_add);
extern char* del_cmdline(const char* original_cmdline, char* cmdline_to_be_del);

#endif  /*_LINUX_CMDLINE_HELPER_H*/
