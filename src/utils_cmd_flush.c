/**
 * collectd - src/utils_cmd_flush.c
 * Copyright (C) 2008  Sebastian Harl
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
 * Author:
 *   Sebastian "tokkee" Harl <sh at tokkee.org>
 **/

#include "collectd.h"
#include "plugin.h"

int handle_flush (FILE *fh, char **fields, int fields_num)
{
	int timeout = -1;

	if ((fields_num != 1) && (fields_num != 2))
	{
		DEBUG ("unixsock plugin: us_handle_flush: "
				"Wrong number of fields: %i", fields_num);
		fprintf (fh, "-1 Wrong number of fields: Got %i, expected 1 or 2.\n",
				fields_num);
		fflush (fh);
		return (-1);
	}

	if (fields_num == 2)
		timeout = atoi (fields[1]);

	INFO ("unixsock plugin: flushing all data");
	plugin_flush_all (timeout);
	INFO ("unixsock plugin: finished flushing all data");

	fprintf (fh, "0 Done\n");
	fflush (fh);
	return (0);
} /* int handle_flush */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

