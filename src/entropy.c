/**
 * collectd - src/entropy.c
 * Copyright (C) 2007  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if !KERNEL_LINUX
# error "No applicable input method."
#endif

#define ENTROPY_FILE "/proc/sys/kernel/random/entropy_avail"

static void entropy_submit (double entropy)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = entropy;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "entropy", sizeof (vl.plugin));
	sstrncpy (vl.type, "entropy", sizeof (vl.type));

	plugin_dispatch_values (&vl);
}

static int entropy_read (void)
{
	double entropy;
	FILE *fh;
	char buffer[64];

	fh = fopen (ENTROPY_FILE, "r");
	if (fh == NULL)
		return (-1);

	if (fgets (buffer, sizeof (buffer), fh) == NULL)
	{
		fclose (fh);
		return (-1);
	}
	fclose (fh);

	entropy = atof (buffer);
	
	if (entropy > 0.0)
		entropy_submit (entropy);

	return (0);
}

void module_register (void)
{
	plugin_register_read ("entropy", entropy_read);
} /* void module_register */
