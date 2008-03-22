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

struct flush_info_s
{
	char **plugins;
	int plugins_num;
	int timeout;
};
typedef struct flush_info_s flush_info_t;

static int parse_option_plugin (flush_info_t *fi, const char *option)
{
	char **temp;

	temp = (char **) realloc (fi->plugins,
			(fi->plugins_num + 1) * sizeof (char *));
	if (temp == NULL)
	{
		ERROR ("utils_cmd_flush: parse_option_plugin: realloc failed.");
		return (-1);
	}
	fi->plugins = temp;

	fi->plugins[fi->plugins_num] = strdup (option + strlen ("plugin="));
	if (fi->plugins[fi->plugins_num] == NULL)
	{
		/* fi->plugins is freed in handle_flush in this case */
		ERROR ("utils_cmd_flush: parse_option_plugin: strdup failed.");
		return (-1);
	}
	fi->plugins_num++;

	return (0);
} /* int parse_option_plugin */

static int parse_option_timeout (flush_info_t *fi, const char *option)
{
	const char *value_ptr = option + strlen ("timeout=");
	char *endptr = NULL;
	int timeout;

	timeout = strtol (value_ptr, &endptr, 0);
	if (value_ptr == endptr)
		return (-1);

	fi->timeout = (timeout <= 0) ? (-1) : timeout;

	return (0);
} /* int parse_option_timeout */

static int parse_option (flush_info_t *fi, const char *option)
{
	if (strncasecmp ("plugin=", option, strlen ("plugin=")) == 0)
		return (parse_option_plugin (fi, option));
	else if (strncasecmp ("timeout=", option, strlen ("timeout=")) == 0)
		return (parse_option_timeout (fi, option));
	else
		return (-1);
} /* int parse_option */

int handle_flush (FILE *fh, char **fields, int fields_num)
{
	flush_info_t fi;
	int status;
	int i;

	memset (&fi, '\0', sizeof (fi));
	fi.timeout = -1;

	for (i = 1; i < fields_num; i++)
	{
		status = parse_option (&fi, fields[i]);
		if (status != 0)
		{
			fprintf (fh, "-1 Cannot parse option %s\n", fields[i]);
			fflush (fh);
			return (-1);
		}
	}

	if (fi.plugins_num > 0)
	{
		int success = 0;
		for (i = 0; i < fi.plugins_num; i++)
		{
			status = plugin_flush_one (fi.timeout, fi.plugins[i]);
			if (status == 0)
				success++;
		}
		fprintf (fh, "0 Done: %i successful, %i errors\n",
				success, fi.plugins_num - success);
	}
	else
	{
		plugin_flush_all (fi.timeout);
		fprintf (fh, "0 Done\n");
	}
	fflush (fh);

	for (i = 0; i < fi.plugins_num; i++)
	{
		sfree (fi.plugins[i]);
	}
	sfree (fi.plugins);

	return (0);
} /* int handle_flush */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

