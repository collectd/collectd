/**
 * collectd - src/utils_cmd_flush.c
 * Copyright (C) 2008       Sebastian Harl
 * Copyright (C) 2008       Florian Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Sebastian "tokkee" Harl <sh at tokkee.org>
 *   Florian "octo" Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_parse_option.h"

#define print_to_socket(fh, ...) \
	do { \
		if (fprintf (fh, __VA_ARGS__) < 0) { \
			char errbuf[1024]; \
			WARNING ("handle_flush: failed to write to socket #%i: %s", \
					fileno (fh), sstrerror (errno, errbuf, sizeof (errbuf))); \
			return -1; \
		} \
		fflush(fh); \
	} while (0)

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

int handle_flush (FILE *fh, char *buffer)
{
	int success = 0;
	int error   = 0;

	double timeout = 0.0;
	char **plugins = NULL;
	int plugins_num = 0;
	char **identifiers = NULL;
	int identifiers_num = 0;

	int i;

	if ((fh == NULL) || (buffer == NULL))
		return (-1);

	DEBUG ("utils_cmd_flush: handle_flush (fh = %p, buffer = %s);",
			(void *) fh, buffer);

	if (strncasecmp ("FLUSH", buffer, strlen ("FLUSH")) != 0)
	{
		print_to_socket (fh, "-1 Cannot parse command.\n");
		return (-1);
	}
	buffer += strlen ("FLUSH");

	while (*buffer != 0)
	{
		char *opt_key;
		char *opt_value;
		int status;

		opt_key = NULL;
		opt_value = NULL;
		status = parse_option (&buffer, &opt_key, &opt_value);
		if (status != 0)
		{
			print_to_socket (fh, "-1 Parsing options failed.\n");
			sfree (plugins);
			sfree (identifiers);
			return (-1);
		}

		if (strcasecmp ("plugin", opt_key) == 0)
		{
			add_to_array (&plugins, &plugins_num, opt_value);
		}
		else if (strcasecmp ("identifier", opt_key) == 0)
		{
			add_to_array (&identifiers, &identifiers_num, opt_value);
		}
		else if (strcasecmp ("timeout", opt_key) == 0)
		{
			char *endptr;
			
			errno = 0;
			endptr = NULL;
			timeout = strtod (opt_value, &endptr);

			if ((endptr == opt_value) || (errno != 0) || (!isfinite (timeout)))
			{
				print_to_socket (fh, "-1 Invalid value for option `timeout': "
						"%s\n", opt_value);
				sfree (plugins);
				sfree (identifiers);
				return (-1);
			}
			else if (timeout < 0.0)
			{
				timeout = 0.0;
			}
		}
		else
		{
			print_to_socket (fh, "-1 Cannot parse option %s\n", opt_key);
			sfree (plugins);
			sfree (identifiers);
			return (-1);
		}
	} /* while (*buffer != 0) */

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
			status = plugin_flush (plugin,
					DOUBLE_TO_CDTIME_T (timeout),
					identifier);
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
		plugin_flush (NULL, DOUBLE_TO_CDTIME_T (timeout), NULL);
		print_to_socket (fh, "0 Done\n");
	}

	sfree (plugins);
	sfree (identifiers);
	return (0);
} /* int handle_flush */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

