/**
 * collectd - src/vserver.c
 * Copyright (C) 2006,2007  Sebastian Harl
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
 *   Sebastian Harl <sh at tokkee.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <dirent.h>
#include <sys/types.h>

#define BUFSIZE 512

#define MODULE_NAME "vserver"
#define PROCDIR "/proc/virtual"

#if defined(KERNEL_LINUX)
# define VSERVER_HAVE_READ 1
#else
# define VSERVER_HAVE_READ 0
#endif /* defined(KERNEL_LINUX) */

static data_source_t octets_dsrc[2] =
{
	{"rx", DS_TYPE_COUNTER, 0, 4294967295.0},
	{"tx", DS_TYPE_COUNTER, 0, 4294967295.0}
};

static data_set_t octets_ds =
{
	"if_octets", 2, octets_dsrc
};

static data_source_t load_dsrc[3] =
{
	{"shortterm", DS_TYPE_GAUGE, 0.0, 100.0},
	{"midterm",   DS_TYPE_GAUGE, 0.0, 100.0},
	{"longterm",  DS_TYPE_GAUGE, 0.0, 100.0}
};

static data_set_t load_ds =
{
	"load", 3, load_dsrc
};

static data_source_t threads_dsrc[1] =
{
	{"value", DS_TYPE_GAUGE, 0.0, 65535.0}
};

static data_set_t threads_ds =
{
	"vs_threads", 1, threads_dsrc
};

static data_source_t processes_dsrc[1] =
{
	{"value", DS_TYPE_GAUGE, 0.0, 65535.0}
};

static data_set_t processes_ds =
{
	"vs_processes", 1, processes_dsrc
};

static data_source_t memory_dsrc[1] =
{
	{"value", DS_TYPE_GAUGE, 0.0, 9223372036854775807.0}
};

static data_set_t memory_ds =
{
	"vs_memory", 1, memory_dsrc
};

#if VSERVER_HAVE_READ
static int pagesize = 0;

static int vserver_init (void)
{
	/* XXX Should we check for getpagesize () in configure?
	 * What's the right thing to do, if there is no getpagesize ()? */
	pagesize = getpagesize ();

	return (0);
} /* static void vserver_init(void) */

static void traffic_submit (const char *plugin_instance,
		const char *type_instance, counter_t rx, counter_t tx)
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = rx;
	values[1].counter = tx;

	vl.values = values;
	vl.values_len = STATIC_ARRAY_SIZE (values);
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "vserver");
	strncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));
	strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values ("if_octets", &vl);
} /* void traffic_submit */

static void load_submit (const char *plugin_instance,
		gauge_t snum, gauge_t mnum, gauge_t lnum)
{
	value_t values[3];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = snum;
	values[1].gauge = mnum;
	values[2].gauge = lnum;

	vl.values = values;
	vl.values_len = STATIC_ARRAY_SIZE (values);
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "vserver");
	strncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));

	plugin_dispatch_values ("load", &vl);
}

static void submit_gauge (const char *plugin_instance, const char *type,
		const char *type_instance, gauge_t value)

{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = STATIC_ARRAY_SIZE (values);
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "vserver");
	strncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));
	strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (type, &vl);
} /* void submit_gauge */

static inline long long __get_sock_bytes(const char *s)
{
	while (s[0] != '/')
		++s;

	/* Remove '/' */
	++s;
	return atoll(s);
}

static int vserver_read (void)
{
	DIR 			*proc;
	struct dirent 	*dent; /* 42 */

	static complain_t complain_obj;

	errno = 0;
	if (NULL == (proc = opendir (PROCDIR)))
	{
		plugin_complain (LOG_ERR, &complain_obj, "vserver plugin: "
				"fopen (%s) failed: %s", PROCDIR, strerror (errno));
		return (-1);
	}
	plugin_relief (LOG_NOTICE, &complain_obj, "vserver plugin: "
			"fopen (%s) succeeded.", PROCDIR);

	while (NULL != (dent = readdir (proc)))
	{
		int  len;
		char file[BUFSIZE];

		FILE *fh;
		char buffer[BUFSIZE];

		char *cols[4];

		if (dent->d_name[0] == '.')
			continue;

		/* This is not a directory */
		if (dent->d_type != DT_DIR)
			continue;

		/* socket message accounting */
		len = snprintf (file, BUFSIZE, PROCDIR "/%s/cacct", dent->d_name);
		if ((len < 0) || (len >= BUFSIZE))
			continue;

		if (NULL == (fh = fopen (file, "r")))
			ERROR ("Cannot open '%s': %s", file, strerror (errno));

		while ((fh != NULL) && (NULL != fgets (buffer, BUFSIZE, fh)))
		{
			counter_t rx;
			counter_t tx;
			char *type_instance;

			if (strsplit (buffer, cols, 4) < 4)
				continue;

			if (0 == strcmp (cols[0], "UNIX:"))
				type_instance = "unix";
			else if (0 == strcmp (cols[0], "INET:"))
				type_instance = "inet";
			else if (0 == strcmp (cols[0], "INET6:"))
				type_instance = "inet6";
			else if (0 == strcmp (cols[0], "OTHER:"))
				type_instance = "other";
			else if (0 == strcmp (cols[0], "UNSPEC:"))
				type_instance = "unspec";
			else
				continue;

			rx = __get_sock_bytes (cols[1]);
			tx = __get_sock_bytes (cols[2]);
			/* cols[3] == errors */

			traffic_submit (dent->d_name, type_instance, rx, tx);
		} /* while (fgets) */

		if (fh != NULL)
		{
			fclose (fh);
			fh = NULL;
		}

		/* thread information and load */
		len = snprintf (file, BUFSIZE, PROCDIR "/%s/cvirt", dent->d_name);
		if ((len < 0) || (len >= BUFSIZE))
			continue;

		if (NULL == (fh = fopen (file, "r")))
			ERROR ("Cannot open '%s': %s", file, strerror (errno));

		while ((fh != NULL) && (NULL != fgets (buffer, BUFSIZE, fh)))
		{
			int n = strsplit (buffer, cols, 4);

			if (2 == n)
			{
				char   *type_instance;
				gauge_t value;

				if (0 == strcmp (cols[0], "nr_threads:"))
					type_instance = "total";
				else if (0 == strcmp (cols[0], "nr_running:"))
					type_instance = "running";
				else if (0 == strcmp (cols[0], "nr_unintr:"))
					type_instance = "uninterruptable";
				else if (0 == strcmp (cols[0], "nr_onhold:"))
					type_instance = "onhold";
				else
					continue;

				value = atof (cols[1]);
				submit_gauge (dent->d_name, "vs_threads", type_instance, value);
			}
			else if (4 == n) {
				if (0 == strcmp (cols[0], "loadavg:"))
				{
					gauge_t snum = atof (cols[1]);
					gauge_t mnum = atof (cols[2]);
					gauge_t lnum = atof (cols[3]);
					load_submit (dent->d_name, snum, mnum, lnum);
				}
			}
		} /* while (fgets) */

		if (fh != NULL)
		{
			fclose (fh);
			fh = NULL;
		}

		/* processes and memory usage */
		len = snprintf (file, BUFSIZE, PROCDIR "/%s/limit", dent->d_name);
		if ((len < 0) || (len >= BUFSIZE))
			continue;

		if (NULL == (fh = fopen (file, "r")))
			ERROR ("Cannot open '%s': %s", file, strerror (errno));

		while ((fh != NULL) && (NULL != fgets (buffer, BUFSIZE, fh)))
		{
			char *type = "vs_memory";
			char *type_instance;
			gauge_t value;

			if (strsplit (buffer, cols, 2) < 2)
				continue;

			if (0 == strcmp (cols[0], "PROC:"))
			{
				type = "vs_processes";
				type_instance = "";
				value = atof (cols[1]);
			}
			else
			{
				if (0 == strcmp (cols[0], "VM:"))
					type_instance = "vm";
				else if (0 == strcmp (cols[0], "VML:"))
					type_instance = "vml";
				else if (0 == strcmp (cols[0], "RSS:"))
					type_instance = "rss";
				else if (0 == strcmp (cols[0], "ANON:"))
					type_instance = "anon";
				else
					continue;

				value = atof (cols[1]) * pagesize;
			}

			submit_gauge (dent->d_name, type, type_instance, value);
		} /* while (fgets) */

		if (fh != NULL)
		{
			fclose (fh);
			fh = NULL;
		}
	} /* while (readdir) */

	closedir (proc);

	return (0);
} /* int vserver_read */
#endif /* VSERVER_HAVE_READ */

void module_register (void)
{
	plugin_register_data_set (&octets_ds);
	plugin_register_data_set (&load_ds);
	plugin_register_data_set (&threads_ds);
	plugin_register_data_set (&processes_ds);
	plugin_register_data_set (&memory_ds);

#if VSERVER_HAVE_READ
	plugin_register_init ("vserver", vserver_init);
	plugin_register_read ("vserver", vserver_read);
#endif /* VSERVER_HAVE_READ */
} /* void module_register(void) */

/* vim: set ts=4 sw=4 noexpandtab : */
