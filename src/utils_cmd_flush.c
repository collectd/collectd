/**
 * collectd - src/utils_cmd_flush.c
 * Copyright (C) 2008  Sebastian Harl
 * Copyright (C) 2008  Florian Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Sebastian "tokkee" Harl <sh at tokkee.org>
 *   Florian "octo" Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

int handle_flush (FILE *fh, char **fields, int fields_num)
{
	int success = 0;
	int error   = 0;

	int timeout = -1;

	int i;

	for (i = 1; i < fields_num; i++)
	{
		char *option = fields[i];
		int   status = 0;

		if (strncasecmp ("plugin=", option, strlen ("plugin=")) == 0)
		{
			char *plugin = option + strlen ("plugin=");

			if (0 == plugin_flush_one (timeout, plugin))
				++success;
			else
				++error;
		}
		else if (strncasecmp ("timeout=", option, strlen ("timeout=")) == 0)
		{
			char *endptr = NULL;
			char *value  = option + strlen ("timeout=");

			errno = 0;
			timeout = strtol (value, &endptr, 0);

			if ((endptr == value) || (0 != errno))
				status = -1;
			else if (0 >= timeout)
				timeout = -1;
		}
		else
			status = -1;

		if (status != 0)
		{
			fprintf (fh, "-1 Cannot parse option %s\n", option);
			fflush (fh);
			return (-1);
		}
	}

	if ((success + error) > 0)
	{
		fprintf (fh, "0 Done: %i successful, %i errors\n", success, error);
	}
	else
	{
		plugin_flush_all (timeout);
		fprintf (fh, "0 Done\n");
	}
	fflush (fh);

	return (0);
} /* int handle_flush */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

