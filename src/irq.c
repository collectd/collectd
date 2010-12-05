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

#define BUFSIZE 128

/*
 * (Module-)Global variables
 */
static const char *config_keys[] =
{
	"Irq",
	"IgnoreSelected"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static unsigned int *irq_list;
static unsigned int irq_list_num;

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
		unsigned int *temp;
		unsigned int irq;
		char *endptr;

		temp = (unsigned int *) realloc (irq_list, (irq_list_num + 1) * sizeof (unsigned int *));
		if (temp == NULL)
		{
			fprintf (stderr, "irq plugin: Cannot allocate more memory.\n");
			ERROR ("irq plugin: Cannot allocate more memory.");
			return (1);
		}
		irq_list = temp;

		/* Clear errno, because we need it to see if an error occured. */
		errno = 0;

		irq = strtol(value, &endptr, 10);
		if ((endptr == value) || (errno != 0))
		{
			fprintf (stderr, "irq plugin: Irq value is not a "
					"number: `%s'\n", value);
			ERROR ("irq plugin: Irq value is not a "
					"number: `%s'", value);
			return (1);
		}
		irq_list[irq_list_num] = irq;
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
static int check_ignore_irq (const unsigned int irq)
{
	int i;

	if (irq_list_num < 1)
		return (0);

	for (i = 0; (unsigned int)i < irq_list_num; i++)
		if (irq == irq_list[i])
			return (irq_list_action);

	return (1 - irq_list_action);
}

static void irq_submit (unsigned int irq, derive_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;
	int status;

	if (check_ignore_irq (irq))
		return;

	values[0].derive = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "irq", sizeof (vl.plugin));
	sstrncpy (vl.type, "irq", sizeof (vl.type));

	status = ssnprintf (vl.type_instance, sizeof (vl.type_instance),
			"%u", irq);
	if ((status < 1) || ((unsigned int)status >= sizeof (vl.type_instance)))
		return;

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

	while (fgets (buffer, BUFSIZE, fh) != NULL)
	{
		unsigned int irq;
		derive_t irq_value;
		char *endptr;
		int i;

		char *fields[64];
		int fields_num;

		fields_num = strsplit (buffer, fields, 64);
		if (fields_num < 2)
			continue;

		errno = 0;    /* To distinguish success/failure after call */
		irq = (unsigned int) strtoul (fields[0], &endptr, /* base = */ 10);

		if ((endptr == fields[0]) || (errno != 0) || (*endptr != ':'))
			continue;

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

		if (i < fields_num)
			continue;

		irq_submit (irq, irq_value);
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

#undef BUFSIZE
