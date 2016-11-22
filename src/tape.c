/**
 * collectd - src/tape.c
 * Copyright (C) 2005,2006  Scott Garrett
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
 *   Scott Garrett <sgarrett at technomancer.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if !HAVE_LIBKSTAT
# error "No applicable input method."
#endif

extern kstat_ctl_t *kc;
static kstat_set_t kstats;

static int tape_init (void)
{
	if (kstat_set_init (&kstats) != 0)
		return (-1);
	static kstat_filter_t filter = KSTAT_FILTER_INIT;
	filter.class = "tape";
	plugin_register_kstat_set ("tape", &kstats, &filter);
	return (0);
} /* int tape_init */

static void tape_submit (const char *plugin_instance,
		const char *type,
		derive_t read, derive_t write)
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].derive = read;
	values[1].derive = write;

	vl.values = values;
	vl.values_len = 2;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "tape", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance,
			sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));

	plugin_dispatch_values (&vl);
} /* void tape_submit */

static int tape_read (void)
{

#if HAVE_KSTAT_IO_T_WRITES && HAVE_KSTAT_IO_T_NWRITES && HAVE_KSTAT_IO_T_WTIME
# define KIO_ROCTETS reads
# define KIO_WOCTETS writes
# define KIO_ROPS    nreads
# define KIO_WOPS    nwrites
# define KIO_RTIME   rtime
# define KIO_WTIME   wtime
#elif HAVE_KSTAT_IO_T_NWRITTEN && HAVE_KSTAT_IO_T_WRITES && HAVE_KSTAT_IO_T_WTIME
# define KIO_ROCTETS nread
# define KIO_WOCTETS nwritten
# define KIO_ROPS    reads
# define KIO_WOPS    writes
# define KIO_RTIME   rtime
# define KIO_WTIME   wtime
#else
# error "kstat_io_t does not have the required members"
#endif
	static kstat_io_t kio;
	int i;

	if (kc == NULL)
		return (-1);

	for (i = 0; i < kstats.len; i++)
	{
		kstat_t *ks = kstats.items[i].kstat;

		if (kstat_read (kc, ks, &kio) == -1)
			continue;

		tape_submit (ks->ks_name, "tape_octets",
				kio.KIO_ROCTETS, kio.KIO_WOCTETS);
		tape_submit (ks->ks_name, "tape_ops",
				kio.KIO_ROPS, kio.KIO_WOPS);
		/* FIXME: Convert this to microseconds if necessary */
		tape_submit (ks->ks_name, "tape_time",
				kio.KIO_RTIME, kio.KIO_WTIME);
	}

	return (0);
}

void module_register (void)
{
	plugin_register_init ("tape", tape_init);
	plugin_register_read ("tape", tape_read);
}
