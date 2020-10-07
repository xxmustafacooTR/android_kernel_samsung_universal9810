/*
 * Copyright 2016 Jesse Chan <cjx123@outlook.com>
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 */

#include <linux/string.h>
#include <linux/cmdline_helper.h>
#include <asm/setup.h>

char* add_cmdline(const char* original_cmdline, char* cmdline_to_be_add)
{
	static char new_command_line[COMMAND_LINE_SIZE];
	char *cmd = new_command_line;

	strcpy(cmd, original_cmdline);

	strcat(cmd, " ");
	strcat(cmd, cmdline_to_be_add);

	return new_command_line;
}

char* del_cmdline(const char* original_cmdline, char* cmdline_to_be_del)
{
	static char new_command_line[COMMAND_LINE_SIZE];
	char *offset_addr, *cmd = new_command_line;

	strcpy(cmd, original_cmdline);

	offset_addr = strstr(cmd, cmdline_to_be_del);
	if (offset_addr) {
		size_t i, len, offset;

		len = strlen(cmd);
		offset = offset_addr - cmd;

		for (i = 1; i < (len - offset); i++) {
			if (cmd[offset + i] == ' ')
				break;
		}

		memmove(offset_addr, &cmd[offset + i + 1], len - i - offset);
	}

	return new_command_line;
}
