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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Tomasz Pala <gotar at pld-linux.org>
 * based on entropy.c by:
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if !KERNEL_LINUX
# error "No applicable input method."
#endif

#define CONNTRACK_FILE "/proc/sys/net/netfilter/nf_conntrack_count"
#define CONNTRACK_MAX_FILE "/proc/sys/net/netfilter/nf_conntrack_max"
#define CONNTRACK_FILE_OLD "/proc/sys/net/ipv4/netfilter/ip_conntrack_count"
#define CONNTRACK_MAX_FILE_OLD "/proc/sys/net/ipv4/netfilter/ip_conntrack_max"

static const char *config_keys[] =
{
	"OldFiles"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);
/*
    Each table/chain combo that will be queried goes into this list
*/

static int old_files = 0;

static int conntrack_config(const char *key, const char *value)
{
    if (strcmp(key, "OldFiles") == 0)
        old_files = 1;

    return 0;
}

static void conntrack_submit (const char *type, const char *type_instance,
			      value_t conntrack)
{
	value_list_t vl = VALUE_LIST_INIT;

	vl.values = &conntrack;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "conntrack", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	if (type_instance != NULL)
		sstrncpy (vl.type_instance, type_instance,
			  sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* static void conntrack_submit */

static int conntrack_read (void)
{
	value_t conntrack, conntrack_max, conntrack_pct;
	FILE *fh;
	char buffer[64];
	size_t buffer_len;

	fh = fopen (old_files?CONNTRACK_FILE_OLD:CONNTRACK_FILE, "r");
	if (fh == NULL)
		return (-1);

	memset (buffer, 0, sizeof (buffer));
	if (fgets (buffer, sizeof (buffer), fh) == NULL)
	{
		fclose (fh);
		return (-1);
	}
	fclose (fh);

	/* strip trailing newline. */
	buffer_len = strlen (buffer);
	while ((buffer_len > 0) && isspace ((int) buffer[buffer_len - 1]))
	{
		buffer[buffer_len - 1] = 0;
		buffer_len--;
	}

	if (parse_value (buffer, &conntrack, DS_TYPE_GAUGE) != 0)
		return (-1);

	conntrack_submit ("conntrack", NULL, conntrack);

	fh = fopen (old_files?CONNTRACK_MAX_FILE_OLD:CONNTRACK_MAX_FILE, "r");
	if (fh == NULL)
		return (-1);

	memset (buffer, 0, sizeof (buffer));
	if (fgets (buffer, sizeof (buffer), fh) == NULL)
	{
		fclose (fh);
		return (-1);
	}
	fclose (fh);

	/* strip trailing newline. */
	buffer_len = strlen (buffer);
	while ((buffer_len > 0) && isspace ((int) buffer[buffer_len - 1]))
	{
		buffer[buffer_len - 1] = 0;
		buffer_len--;
	}

	if (parse_value (buffer, &conntrack_max, DS_TYPE_GAUGE) != 0)
		return (-1);

	conntrack_submit ("conntrack", "max", conntrack_max);
	conntrack_pct.gauge = (conntrack.gauge / conntrack_max.gauge) * 100;
	conntrack_submit ("percent", "used", conntrack_pct);


	return (0);
} /* static int conntrack_read */

void module_register (void)
{
    plugin_register_config ("conntrack", conntrack_config,
                            config_keys, config_keys_num);
	plugin_register_read ("conntrack", conntrack_read);
} /* void module_register */
