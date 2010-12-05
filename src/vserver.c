/**
 * collectd - src/vserver.c
 * Copyright (C) 2006,2007  Sebastian Harl
 * Copyright (C) 2007-2010  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the license is applicable.
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <dirent.h>
#include <sys/types.h>

#define BUFSIZE 512

#define PROCDIR "/proc/virtual"

#if !KERNEL_LINUX
# error "No applicable input method."
#endif

static int pagesize = 0;

static int vserver_init (void)
{
	/* XXX Should we check for getpagesize () in configure?
	 * What's the right thing to do, if there is no getpagesize ()? */
	pagesize = getpagesize ();

	return (0);
} /* static void vserver_init(void) */

static void traffic_submit (const char *plugin_instance,
		const char *type_instance, derive_t rx, derive_t tx)
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].derive = rx;
	values[1].derive = tx;

	vl.values = values;
	vl.values_len = STATIC_ARRAY_SIZE (values);
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "vserver", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, "if_octets", sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
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
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "vserver", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, "load", sizeof (vl.type));

	plugin_dispatch_values (&vl);
}

static void submit_gauge (const char *plugin_instance, const char *type,
		const char *type_instance, gauge_t value)

{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = STATIC_ARRAY_SIZE (values);
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "vserver", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* void submit_gauge */

static derive_t vserver_get_sock_bytes(const char *s)
{
	value_t v;
	int status;

	while (s[0] != '/')
		++s;

	/* Remove '/' */
	++s;

	status = parse_value (s, &v, DS_TYPE_DERIVE);
	if (status != 0)
		return (-1);
	return (v.derive);
}

static int vserver_read (void)
{
#if NAME_MAX < 1024
# define DIRENT_BUFFER_SIZE (sizeof (struct dirent) + 1024 + 1)
#else
# define DIRENT_BUFFER_SIZE (sizeof (struct dirent) + NAME_MAX + 1)
#endif

	DIR 			*proc;
	struct dirent 	*dent; /* 42 */
	char dirent_buffer[DIRENT_BUFFER_SIZE];

	errno = 0;
	proc = opendir (PROCDIR);
	if (proc == NULL)
	{
		char errbuf[1024];
		ERROR ("vserver plugin: fopen (%s): %s", PROCDIR, 
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	while (42)
	{
		int len;
		char file[BUFSIZE];

		FILE *fh;
		char buffer[BUFSIZE];

		struct stat statbuf;
		char *cols[4];

		int status;

		status = readdir_r (proc, (struct dirent *) dirent_buffer, &dent);
		if (status != 0)
		{
			char errbuf[4096];
			ERROR ("vserver plugin: readdir_r failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			closedir (proc);
			return (-1);
		}
		else if (dent == NULL)
		{
			/* end of directory */
			break;
		}

		if (dent->d_name[0] == '.')
			continue;

		len = ssnprintf (file, sizeof (file), PROCDIR "/%s", dent->d_name);
		if ((len < 0) || (len >= BUFSIZE))
			continue;
		
		status = stat (file, &statbuf);
		if (status != 0)
		{
			char errbuf[4096];
			WARNING ("vserver plugin: stat (%s) failed: %s",
					file, sstrerror (errno, errbuf, sizeof (errbuf)));
			continue;
		}
		
		if (!S_ISDIR (statbuf.st_mode))
			continue;

		/* socket message accounting */
		len = ssnprintf (file, sizeof (file),
				PROCDIR "/%s/cacct", dent->d_name);
		if ((len < 0) || ((size_t) len >= sizeof (file)))
			continue;

		if (NULL == (fh = fopen (file, "r")))
		{
			char errbuf[1024];
			ERROR ("Cannot open '%s': %s", file,
					sstrerror (errno, errbuf, sizeof (errbuf)));
		}

		while ((fh != NULL) && (NULL != fgets (buffer, BUFSIZE, fh)))
		{
			derive_t rx;
			derive_t tx;
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

			rx = vserver_get_sock_bytes (cols[1]);
			tx = vserver_get_sock_bytes (cols[2]);
			/* cols[3] == errors */

			traffic_submit (dent->d_name, type_instance, rx, tx);
		} /* while (fgets) */

		if (fh != NULL)
		{
			fclose (fh);
			fh = NULL;
		}

		/* thread information and load */
		len = ssnprintf (file, sizeof (file),
				PROCDIR "/%s/cvirt", dent->d_name);
		if ((len < 0) || ((size_t) len >= sizeof (file)))
			continue;

		if (NULL == (fh = fopen (file, "r")))
		{
			char errbuf[1024];
			ERROR ("Cannot open '%s': %s", file,
					sstrerror (errno, errbuf, sizeof (errbuf)));
		}

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
		len = ssnprintf (file, sizeof (file),
				PROCDIR "/%s/limit", dent->d_name);
		if ((len < 0) || ((size_t) len >= sizeof (file)))
			continue;

		if (NULL == (fh = fopen (file, "r")))
		{
			char errbuf[1024];
			ERROR ("Cannot open '%s': %s", file,
					sstrerror (errno, errbuf, sizeof (errbuf)));
		}

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

void module_register (void)
{
	plugin_register_init ("vserver", vserver_init);
	plugin_register_read ("vserver", vserver_read);
} /* void module_register(void) */

/* vim: set ts=4 sw=4 noexpandtab : */
