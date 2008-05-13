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

#define print_to_socket(fh, ...) \
	if (fprintf (fh, __VA_ARGS__) < 0) { \
		char errbuf[1024]; \
		WARNING ("handle_flush: failed to write to socket #%i: %s", \
				fileno (fh), sstrerror (errno, errbuf, sizeof (errbuf))); \
		return -1; \
	}

static int add_to_array (char ***array, int *array_num, char *value)
{
	char **temp;

	temp = (char **) realloc (*array, sizeof (char *) * (*array_num + 1));
	if (temp == NULL)
		return (-1);

	*array = temp;
	(*array)[*array_num] = value;
	(*array_num)++;

	return (0);
} /* int add_to_array */

int handle_flush (FILE *fh, char **fields, int fields_num)
{
	int success = 0;
	int error   = 0;

	int timeout = -1;
	char **plugins = NULL;
	int plugins_num = 0;
	char **identifiers = NULL;
	int identifiers_num = 0;

	int i;

	for (i = 1; i < fields_num; i++)
	{
		char *option = fields[i];
		int   status = 0;

		if (strncasecmp ("plugin=", option, strlen ("plugin=")) == 0)
		{
			char *plugin;
			
			plugin = option + strlen ("plugin=");
			add_to_array (&plugins, &plugins_num, plugin);
		}
		else if (strncasecmp ("identifier=", option, strlen ("identifier=")) == 0)
		{
			char *identifier;

			identifier = option + strlen ("identifier=");
			add_to_array (&identifiers, &identifiers_num, identifier);
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
			print_to_socket (fh, "-1 Cannot parse option %s\n", option);
			return (-1);
		}
	}

	/* Add NULL entries for `any plugin' and/or `any value' if nothing was
	 * specified. */
	if (plugins_num == 0)
		add_to_array (&plugins, &plugins_num, NULL);

	if (identifiers_num == 0)
		add_to_array (&identifiers, &identifiers_num, NULL);

	for (i = 0; i < plugins_num; i++)
	{
		char *plugin;
		int j;

		plugin = plugins[i];

		for (j = 0; j < identifiers_num; j++)
		{
			char *identifier;
			int status;

			identifier = identifiers[j];
			status = plugin_flush (plugin, timeout, identifier);
			if (status == 0)
				success++;
			else
				error++;
		}
	}

	if ((success + error) > 0)
	{
		print_to_socket (fh, "0 Done: %i successful, %i errors\n",
				success, error);
	}
	else
	{
		plugin_flush_all (timeout);
		print_to_socket (fh, "0 Done\n");
	}

	return (0);
} /* int handle_flush */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

