/**
 * collectd - src/conntrack.c
 * Copyright (C) 2009  Tomasz Pala
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
 *   Tomasz Pala <gotar at pld-linux.org>
 * based on entropy.c by:
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if !KERNEL_LINUX
# error "No applicable input method."
#endif

#define CONNTRACK_FILE "/proc/sys/net/netfilter/nf_conntrack_count"

static void conntrack_submit (double conntrack)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = conntrack;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "conntrack", sizeof (vl.plugin));
	sstrncpy (vl.type, "conntrack", sizeof (vl.type));

	plugin_dispatch_values (&vl);
} /* static void conntrack_submit */

static int conntrack_read (void)
{
	double conntrack;
	FILE *fh;
	char buffer[64];

	fh = fopen (CONNTRACK_FILE, "r");
	if (fh == NULL)
		return (-1);

	if (fgets (buffer, sizeof (buffer), fh) == NULL)
	{
		fclose (fh);
		return (-1);
	}
	fclose (fh);

	conntrack = atof (buffer);

	if (conntrack > 0.0)
		conntrack_submit (conntrack);

	return (0);
} /* static int conntrack_read */

void module_register (void)
{
	plugin_register_read ("conntrack", conntrack_read);
} /* void module_register */
