/**
 * collectd - src/irq.c
 * Copyright (C) 2007  Peter Holik
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *   Peter Holik <peter at holik.at>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#if !KERNEL_LINUX
# error "No applicable input method."
#endif

/*
 * (Module-)Global variables
 */
static const char *config_keys[] =
{
	"Irq",
	"IgnoreSelected"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static char         **irq_list;
static unsigned int   irq_list_num = 0;

/* 
 * irq_list_action:
 * 0 => default is to collect selected irqs
 * 1 => ignore selcted irqs
 */
static int irq_list_action;

static int irq_config (const char *key, const char *value)
{
	if (strcasecmp (key, "Irq") == 0)
	{
		char **temp;

		temp = realloc (irq_list, (irq_list_num + 1) * sizeof (*irq_list));
		if (temp == NULL)
		{
			fprintf (stderr, "irq plugin: Cannot allocate more memory.\n");
			ERROR ("irq plugin: Cannot allocate more memory.");
			return (1);
		}
		irq_list = temp;

		irq_list[irq_list_num] = strdup (value);
		if (irq_list[irq_list_num] == NULL)
		{
			ERROR ("irq plugin: strdup(3) failed.");
			return (1);
		}

		irq_list_num++;
	}
	else if (strcasecmp (key, "IgnoreSelected") == 0)
	{
		if (IS_TRUE (value))
			irq_list_action = 1;
		else
			irq_list_action = 0;
	}
	else
	{
		return (-1);
	}
	return (0);
}

/*
 * Check if this interface/instance should be ignored. This is called from
 * both, `submit' and `write' to give client and server the ability to
 * ignore certain stuff..
 */
static int check_ignore_irq (const char *irq)
{
	unsigned int i;

	if (irq_list_num < 1)
		return (0);

	for (i = 0; i < irq_list_num; i++)
		if (strcmp (irq, irq_list[i]) == 0)
			return (irq_list_action);

	return (1 - irq_list_action);
}

static void irq_submit (const char *irq_name, derive_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	if (check_ignore_irq (irq_name))
		return;

	values[0].derive = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "irq", sizeof (vl.plugin));
	sstrncpy (vl.type, "irq", sizeof (vl.type));
	sstrncpy (vl.type_instance, irq_name, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* void irq_submit */

static int irq_read (void)
{
	FILE *fh;
	char buffer[1024];

	fh = fopen ("/proc/interrupts", "r");
	if (fh == NULL)
	{
		char errbuf[1024];
		ERROR ("irq plugin: fopen (/proc/interrupts): %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		char *irq_name;
		size_t irq_name_len;
		derive_t irq_value;
		int i;

		char *fields[64];
		int fields_num;

		fields_num = strsplit (buffer, fields, 64);
		if (fields_num < 2)
			continue;

		irq_name = fields[0];
		irq_name_len = strlen (irq_name);
		if (irq_name_len < 2)
			continue;

		/* Check if irq name ends with colon.
		 * Otherwise it's a header. */
		if (irq_name[irq_name_len - 1] != ':')
			continue;

		irq_name[irq_name_len - 1] = 0;
		irq_name_len--;

		irq_value = 0;
		for (i = 1; i < fields_num; i++)
		{
			/* Per-CPU value */
			value_t v;
			int status;

			status = parse_value (fields[i], &v, DS_TYPE_DERIVE);
			if (status != 0)
				break;

			irq_value += v.derive;
		} /* for (i) */

		/* No valid fields -> do not submit anything. */
		if (i <= 1)
			continue;

		irq_submit (irq_name, irq_value);
	}

	fclose (fh);

	return (0);
} /* int irq_read */

void module_register (void)
{
	plugin_register_config ("irq", irq_config,
			config_keys, config_keys_num);
	plugin_register_read ("irq", irq_read);
} /* void module_register */
