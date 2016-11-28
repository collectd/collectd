/**
 * collectd - src/irq.c
 * Copyright (C) 2007  Peter Holik
 * Copyright (C) 2011  Florian Forster
 * Copyright (C) 2015  Damien Degois
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
 *   Giles Westwood
 *   Damien Degois <damien at degois.info>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#if !KERNEL_LINUX
# error "No applicable input method."
#endif

/*
 * (Module-)Global variables
 */
static const char *config_keys[] =
{
	"Irq",
	"IgnoreSelected",
	"NamedIrq",
	"AppendNum",
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static ignorelist_t *ignorelist = NULL;
static int named_irq;
static int append_num;

/*
 * Private functions
 */
static int irq_config (const char *key, const char *value)
{
	if (ignorelist == NULL)
		ignorelist = ignorelist_create (/* invert = */ 1);

	if (strcasecmp (key, "Irq") == 0)
	{
		ignorelist_add (ignorelist, value);
	}
	else if (strcasecmp (key, "IgnoreSelected") == 0)
	{
		int invert = 1;
		if (IS_TRUE (value))
			invert = 0;
		ignorelist_set_invert (ignorelist, invert);
	}
	else if (strcasecmp (key, "NamedIrq") == 0)
	{
		if (IS_TRUE (value))
		{
			named_irq = 1;
		}
		else
		{
			named_irq = 0;
		}
	}
	else if (strcasecmp (key, "AppendNum") == 0)
	{
		if (IS_TRUE (value))
		{
			append_num = 1;
		}
		else
		{
			append_num = 0;
		}
	}
	else
	{
		return (-1);
	}

	return (0);
}

static void irq_submit (const char *irq_name, derive_t value)
{
	value_list_t vl = VALUE_LIST_INIT;

	if (ignorelist_match (ignorelist, irq_name) != 0)
		return;

	vl.values = &(value_t) { .derive = value };
	vl.values_len = 1;
	sstrncpy (vl.plugin, "irq", sizeof (vl.plugin));
	sstrncpy (vl.type, "irq", sizeof (vl.type));
	sstrncpy (vl.type_instance, irq_name, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* void irq_submit */

static int irq_read (void)
{
	FILE *fh;
	char buffer[1024];
	int  cpu_count;
	char *fields[256];

	/*
	 * Example content:
	 *         CPU0       CPU1       CPU2       CPU3
	 * 0:       2574          1          3          2   IO-APIC-edge      timer
	 * 1:     102553     158669     218062      70587   IO-APIC-edge      i8042
	 * 8:          0          0          0          1   IO-APIC-edge      rtc0
	 */
	fh = fopen ("/proc/interrupts", "r");
	if (fh == NULL)
	{
		char errbuf[1024];
		ERROR ("irq plugin: fopen (/proc/interrupts): %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	/* Get CPU count from the first line */
	if(fgets (buffer, sizeof (buffer), fh) != NULL) {
		cpu_count = strsplit (buffer, fields,
				STATIC_ARRAY_SIZE (fields));
	} else {
		ERROR ("irq plugin: unable to get CPU count from first line "
				"of /proc/interrupts");
		fclose (fh);
		return (-1);
	}

	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		char *irq_name;
		size_t irq_name_len;
		derive_t irq_value;
		int i;
		int fields_num;
		int irq_values_to_parse;
		char name_buffer[1024];

		fields_num = strsplit (buffer, fields,
				STATIC_ARRAY_SIZE (fields));
		if (fields_num < 2)
			continue;

		/* Parse this many numeric fields, skip the rest
		 * (+1 because first there is a name of irq in each line) */
		if (fields_num >= cpu_count + 1)
			irq_values_to_parse = cpu_count;
		else
			irq_values_to_parse = fields_num - 1;

		/* First field is irq name and colon */
		irq_name = fields[0];
		irq_name_len = strlen (irq_name);
		if (irq_name_len < 2)
			continue;

		/* Check if irq name ends with colon.
		 * Otherwise it's a header. */
		if (irq_name[irq_name_len - 1] != ':')
			continue;

		/* Is it the the ARM fast interrupt (FIQ)? */
		if (irq_name_len == 4 && (strncmp(irq_name, "FIQ:", 4) == 0))
			continue;

		irq_name[irq_name_len - 1] = 0;
		irq_name_len--;
		/* Use the last column instead of the irq number */
		if (named_irq == 1)
		{
			/* Some first column names are already non digits */
			if (isdigit(irq_name[0]))
			{
				//  0:       2574          1          3          2   IO-APIC-edge      timer
				// 43:          0          0          0          0   IR-PCI-MSI-edge   PCIe PME, pciehp

				// +1 for IRQ name, +1 for chip name
				if (fields_num > cpu_count+2)
				{
					// If the field ends with ',' trimm it [Ex: line 2]
					int field_len;
					int used_name_buffer;
					for (i = cpu_count+2; i < fields_num; i++)
					{
						field_len = strlen(fields[i]);
						if (fields[i][field_len - 1] == ',')
						{
							fields[i][--field_len] = '\0';
						}
					}

					// Glue extra fields (not irq num, per cpu count nor chip)
					used_name_buffer = strjoin(name_buffer, STATIC_ARRAY_SIZE(name_buffer), &fields[cpu_count+2], fields_num-2-cpu_count, " ");

					// If append num AND we have enought remaining place in the buffer: add the IRQ number
					// Note: strjoin doesn't count termination so add 2 in the check: termination and separator
					if (append_num == 1 &&
						(used_name_buffer+irq_name_len+2) < STATIC_ARRAY_SIZE(name_buffer) )
					{
						// Add separator, copy irq_name, add string termination
						name_buffer[used_name_buffer++] = '_';
						memcpy(&name_buffer[used_name_buffer],irq_name,irq_name_len);
						used_name_buffer += irq_name_len;
						name_buffer[used_name_buffer] = '\0';
					}
					irq_name = name_buffer;
				}
			}
		}

		irq_value = 0;
		for (i = 1; i <= irq_values_to_parse; i++)
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
