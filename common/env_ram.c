/*
 * (C) Copyright 2010
 * Rob Emanuele <rje@crystalfontz.com>
 * Crystalfontz America, Inc. <www.crystalfontz.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <common.h>
#include <command.h>
#include <environment.h>
#include <linux/stddef.h>

DECLARE_GLOBAL_DATA_PTR;

env_t *env_ptr = NULL;

extern uchar default_environment[];


void env_relocate_spec (void)
{
	char* orig = (char*)CONFIG_ENV_ADDR;
	char* dest = env_ptr->data;

	while (*orig != 0) {
		if (*orig == '\n')
			*orig = '\0';
		if (*orig != '\r')
			*(dest++) = *orig;
		orig++;
	}

	env_crc_update();
}

uchar env_get_char_spec (int index)
{
	return ( *((uchar *)(gd->env_addr + index)) );
}

/************************************************************************
 * Initialize Environment use
 */
int  env_init(void)
{
	gd->env_addr  = (ulong) env_ptr->data;
	gd->env_valid = 1;

	return (0);
}
