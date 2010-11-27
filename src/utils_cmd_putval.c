/**
 * collectd - src/utils_cms_putval.c
 * Copyright (C) 2007-2009  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include "utils_parse_option.h"

#define print_to_socket(fh, ...) \
	if (fprintf (fh, __VA_ARGS__) < 0) { \
		char errbuf[1024]; \
		WARNING ("handle_putval: failed to write to socket #%i: %s", \
				fileno (fh), sstrerror (errno, errbuf, sizeof (errbuf))); \
		return -1; \
	}

static int dispatch_values (const data_set_t *ds, value_list_t *vl,
	       	FILE *fh, char *buffer)
{
	int status;

	status = parse_values (buffer, vl, ds);
	if (status != 0)
	{
		print_to_socket (fh, "-1 Parsing the values string failed.\n");
		return (-1);
	}

	plugin_dispatch_values (vl);
	return (0);
} /* int dispatch_values */

static int set_option (value_list_t *vl, const char *key, const char *value)
{
	if ((vl == NULL) || (key == NULL) || (value == NULL))
		return (-1);

	if (strcasecmp ("interval", key) == 0)
	{
		double tmp;
		char *endptr;

		endptr = NULL;
		errno = 0;
		tmp = strtod (value, &endptr);

		if ((errno == 0) && (endptr != NULL)
				&& (endptr != value) && (tmp > 0.0))
			vl->interval = DOUBLE_TO_CDTIME_T (tmp);
	}
	else
		return (1);

	return (0);
} /* int parse_option */

int handle_putval (FILE *fh, char *buffer)
{
	char *command;
	char *identifier;
	char *hostname;
	char *plugin;
	char *plugin_instance;
	char *type;
	char *type_instance;
	int   status;
	int   values_submitted;

	char *identifier_copy;

	const data_set_t *ds;
	value_list_t vl = VALUE_LIST_INIT;

	DEBUG ("utils_cmd_putval: handle_putval (fh = %p, buffer = %s);",
			(void *) fh, buffer);

	command = NULL;
	status = parse_string (&buffer, &command);
	if (status != 0)
	{
		print_to_socket (fh, "-1 Cannot parse command.\n");
		return (-1);
	}
	assert (command != NULL);

	if (strcasecmp ("PUTVAL", command) != 0)
	{
		print_to_socket (fh, "-1 Unexpected command: `%s'.\n", command);
		return (-1);
	}

	identifier = NULL;
	status = parse_string (&buffer, &identifier);
	if (status != 0)
	{
		print_to_socket (fh, "-1 Cannot parse identifier.\n");
		return (-1);
	}
	assert (identifier != NULL);

	/* parse_identifier() modifies its first argument,
	 * returning pointers into it */
	identifier_copy = sstrdup (identifier);

	status = parse_identifier (identifier_copy, &hostname,
			&plugin, &plugin_instance,
			&type, &type_instance);
	if (status != 0)
	{
		DEBUG ("handle_putval: Cannot parse identifier `%s'.",
				identifier);
		print_to_socket (fh, "-1 Cannot parse identifier `%s'.\n",
				identifier);
		sfree (identifier_copy);
		return (-1);
	}

	if ((strlen (hostname) >= sizeof (vl.host))
			|| (strlen (plugin) >= sizeof (vl.plugin))
			|| ((plugin_instance != NULL)
				&& (strlen (plugin_instance) >= sizeof (vl.plugin_instance)))
			|| ((type_instance != NULL)
				&& (strlen (type_instance) >= sizeof (vl.type_instance))))
	{
		print_to_socket (fh, "-1 Identifier too long.\n");
		sfree (identifier_copy);
		return (-1);
	}

	sstrncpy (vl.host, hostname, sizeof (vl.host));
	sstrncpy (vl.plugin, plugin, sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	if (plugin_instance != NULL)
		sstrncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));
	if (type_instance != NULL)
		sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	ds = plugin_get_ds (type);
	if (ds == NULL) {
		print_to_socket (fh, "-1 Type `%s' isn't defined.\n", type);
		sfree (identifier_copy);
		return (-1);
	}

	/* Free identifier_copy */
	hostname = NULL;
	plugin = NULL; plugin_instance = NULL;
	type = NULL;   type_instance = NULL;
	sfree (identifier_copy);

	vl.values_len = ds->ds_num;
	vl.values = (value_t *) malloc (vl.values_len * sizeof (value_t));
	if (vl.values == NULL)
	{
		print_to_socket (fh, "-1 malloc failed.\n");
		return (-1);
	}

	/* All the remaining fields are part of the optionlist. */
	values_submitted = 0;
	while (*buffer != 0)
	{
		char *string = NULL;
		char *value  = NULL;

		status = parse_option (&buffer, &string, &value);
		if (status < 0)
		{
			/* parse_option failed, buffer has been modified.
			 * => we need to abort */
			print_to_socket (fh, "-1 Misformatted option.\n");
			return (-1);
		}
		else if (status == 0)
		{
			assert (string != NULL);
			assert (value != NULL);
			set_option (&vl, string, value);
			continue;
		}
		/* else: parse_option but buffer has not been modified. This is
		 * the default if no `=' is found.. */

		status = parse_string (&buffer, &string);
		if (status != 0)
		{
			print_to_socket (fh, "-1 Misformatted value.\n");
			return (-1);
		}
		assert (string != NULL);

		status = dispatch_values (ds, &vl, fh, string);
		if (status != 0)
		{
			/* An error has already been printed. */
			return (-1);
		}
		values_submitted++;
	} /* while (*buffer != 0) */
	/* Done parsing the options. */

	print_to_socket (fh, "0 Success: %i %s been dispatched.\n",
			values_submitted,
			(values_submitted == 1) ? "value has" : "values have");

	sfree (vl.values); 

	return (0);
} /* int handle_putval */

int create_putval (char *ret, size_t ret_len, /* {{{ */
	const data_set_t *ds, const value_list_t *vl)
{
	char buffer_ident[6 * DATA_MAX_NAME_LEN];
	char buffer_values[1024];
	int status;

	status = FORMAT_VL (buffer_ident, sizeof (buffer_ident), vl);
	if (status != 0)
		return (status);
	escape_string (buffer_ident, sizeof (buffer_ident));

	status = format_values (buffer_values, sizeof (buffer_values),
			ds, vl, /* store rates = */ 0);
	if (status != 0)
		return (status);
	escape_string (buffer_values, sizeof (buffer_values));

	ssnprintf (ret, ret_len,
			"PUTVAL %s interval=%.3f %s",
			buffer_ident,
			(vl->interval > 0)
			? CDTIME_T_TO_DOUBLE (vl->interval)
			: CDTIME_T_TO_DOUBLE (interval_g),
			buffer_values);

	return (0);
} /* }}} int create_putval */
