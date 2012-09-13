/**
 * collectd - src/csv.c
 * Copyright (C) 2007-2009  Florian octo Forster
 * Copyright (C) 2009       Doug MacEachern
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
 *   Doug MacEachern <dougm@hyperic.com>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "utils_cache.h"
#include "utils_parse_option.h"

/*
 * Private variables
 */
static const char *config_keys[] =
{
	"DataDir",
	"StoreRates"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static char *datadir   = NULL;
static int store_rates = 0;
static int use_stdio   = 0;

static int value_list_to_string (char *buffer, int buffer_len,
		const data_set_t *ds, const value_list_t *vl)
{
	int offset;
	int status;
	int i;
	gauge_t *rates = NULL;

	assert (0 == strcmp (ds->type, vl->type));

	memset (buffer, '\0', buffer_len);

	status = ssnprintf (buffer, buffer_len, "%.3f",
			CDTIME_T_TO_DOUBLE (vl->time));
	if ((status < 1) || (status >= buffer_len))
		return (-1);
	offset = status;

	for (i = 0; i < ds->ds_num; i++)
	{
		if ((ds->ds[i].type != DS_TYPE_COUNTER)
				&& (ds->ds[i].type != DS_TYPE_GAUGE)
				&& (ds->ds[i].type != DS_TYPE_DERIVE)
				&& (ds->ds[i].type != DS_TYPE_ABSOLUTE))
			return (-1);

		if (ds->ds[i].type == DS_TYPE_GAUGE) 
		{
			status = ssnprintf (buffer + offset, buffer_len - offset,
					",%lf", vl->values[i].gauge);
		} 
		else if (store_rates != 0)
		{
			if (rates == NULL)
				rates = uc_get_rate (ds, vl);
			if (rates == NULL)
			{
				WARNING ("csv plugin: "
						"uc_get_rate failed.");
				return (-1);
			}
			status = ssnprintf (buffer + offset,
					buffer_len - offset,
					",%lf", rates[i]);
		}
		else if (ds->ds[i].type == DS_TYPE_COUNTER)
		{
			status = ssnprintf (buffer + offset,
					buffer_len - offset,
					",%llu",
					vl->values[i].counter);
		}
		else if (ds->ds[i].type == DS_TYPE_DERIVE)
		{
			status = ssnprintf (buffer + offset,
					buffer_len - offset,
					",%"PRIi64,
					vl->values[i].derive);
		}
		else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
		{
			status = ssnprintf (buffer + offset,
					buffer_len - offset,
					",%"PRIu64,
					vl->values[i].absolute);
		}

		if ((status < 1) || (status >= (buffer_len - offset)))
		{
			sfree (rates);
			return (-1);
		}

		offset += status;
	} /* for ds->ds_num */

	sfree (rates);
	return (0);
} /* int value_list_to_string */

static int value_list_to_filename (char *buffer, int buffer_len,
		const data_set_t *ds, const value_list_t *vl)
{
	int offset = 0;
	int status;

	assert (0 == strcmp (ds->type, vl->type));

	if (datadir != NULL)
	{
		status = ssnprintf (buffer + offset, buffer_len - offset,
				"%s/", datadir);
		if ((status < 1) || (status >= buffer_len - offset))
			return (-1);
		offset += status;
	}

	status = ssnprintf (buffer + offset, buffer_len - offset,
			"%s/", vl->host);
	if ((status < 1) || (status >= buffer_len - offset))
		return (-1);
	offset += status;

	if (strlen (vl->plugin_instance) > 0)
		status = ssnprintf (buffer + offset, buffer_len - offset,
				"%s-%s/", vl->plugin, vl->plugin_instance);
	else
		status = ssnprintf (buffer + offset, buffer_len - offset,
				"%s/", vl->plugin);
	if ((status < 1) || (status >= buffer_len - offset))
		return (-1);
	offset += status;

	if (strlen (vl->type_instance) > 0)
		status = ssnprintf (buffer + offset, buffer_len - offset,
				"%s-%s", vl->type, vl->type_instance);
	else
		status = ssnprintf (buffer + offset, buffer_len - offset,
				"%s", vl->type);
	if ((status < 1) || (status >= buffer_len - offset))
		return (-1);
	offset += status;

	if (!use_stdio)
	{
		time_t now;
		struct tm stm;

		/* TODO: Find a way to minimize the calls to `localtime_r',
		 * since they are pretty expensive.. */
		now = time (NULL);
		if (localtime_r (&now, &stm) == NULL)
		{
			ERROR ("csv plugin: localtime_r failed");
			return (1);
		}

		strftime (buffer + offset, buffer_len - offset,
				"-%Y-%m-%d", &stm);
	}

	return (0);
} /* int value_list_to_filename */

static int csv_create_file (const char *filename, const data_set_t *ds)
{
	FILE *csv;
	int i;

	if (check_create_dir (filename))
		return (-1);

	csv = fopen (filename, "w");
	if (csv == NULL)
	{
		char errbuf[1024];
		ERROR ("csv plugin: fopen (%s) failed: %s",
				filename,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	fprintf (csv, "epoch");
	for (i = 0; i < ds->ds_num; i++)
		fprintf (csv, ",%s", ds->ds[i].name);

	fprintf (csv, "\n");
	fclose (csv);

	return 0;
} /* int csv_create_file */

static int csv_config (const char *key, const char *value)
{
	if (strcasecmp ("DataDir", key) == 0)
	{
		if (datadir != NULL)
			free (datadir);
		if (strcasecmp ("stdout", value) == 0)
		{
			use_stdio = 1;
			return (0);
		}
		else if (strcasecmp ("stderr", value) == 0)
		{
			use_stdio = 2;
			return (0);
		}
		datadir = strdup (value);
		if (datadir != NULL)
		{
			int len = strlen (datadir);
			while ((len > 0) && (datadir[len - 1] == '/'))
			{
				len--;
				datadir[len] = '\0';
			}
			if (len <= 0)
			{
				free (datadir);
				datadir = NULL;
			}
		}
	}
	else if (strcasecmp ("StoreRates", key) == 0)
	{
		if (IS_TRUE (value))
			store_rates = 1;
		else
			store_rates = 0;
	}
	else
	{
		return (-1);
	}
	return (0);
} /* int csv_config */

static int csv_write (const data_set_t *ds, const value_list_t *vl,
		user_data_t __attribute__((unused)) *user_data)
{
	struct stat  statbuf;
	char         filename[512];
	char         values[4096];
	FILE        *csv;
	int          csv_fd;
	struct flock fl;
	int          status;

	if (0 != strcmp (ds->type, vl->type)) {
		ERROR ("csv plugin: DS type does not match value list type");
		return -1;
	}

	if (value_list_to_filename (filename, sizeof (filename), ds, vl) != 0)
		return (-1);

	DEBUG ("csv plugin: csv_write: filename = %s;", filename);

	if (value_list_to_string (values, sizeof (values), ds, vl) != 0)
		return (-1);

	if (use_stdio)
	{
		size_t i;

		escape_string (filename, sizeof (filename));

		/* Replace commas by colons for PUTVAL compatible output. */
		for (i = 0; i < sizeof (values); i++)
		{
			if (values[i] == 0)
				break;
			else if (values[i] == ',')
				values[i] = ':';
		}

		fprintf (use_stdio == 1 ? stdout : stderr,
			 "PUTVAL %s interval=%.3f %s\n",
			 filename,
			 CDTIME_T_TO_DOUBLE (vl->interval),
			 values);
		return (0);
	}

	if (stat (filename, &statbuf) == -1)
	{
		if (errno == ENOENT)
		{
			if (csv_create_file (filename, ds))
				return (-1);
		}
		else
		{
			char errbuf[1024];
			ERROR ("stat(%s) failed: %s", filename,
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
			return (-1);
		}
	}
	else if (!S_ISREG (statbuf.st_mode))
	{
		ERROR ("stat(%s): Not a regular file!",
				filename);
		return (-1);
	}

	csv = fopen (filename, "a");
	if (csv == NULL)
	{
		char errbuf[1024];
		ERROR ("csv plugin: fopen (%s) failed: %s", filename,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}
	csv_fd = fileno (csv);

	memset (&fl, '\0', sizeof (fl));
	fl.l_start  = 0;
	fl.l_len    = 0; /* till end of file */
	fl.l_pid    = getpid ();
	fl.l_type   = F_WRLCK;
	fl.l_whence = SEEK_SET;

	status = fcntl (csv_fd, F_SETLK, &fl);
	if (status != 0)
	{
		char errbuf[1024];
		ERROR ("csv plugin: flock (%s) failed: %s", filename,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		fclose (csv);
		return (-1);
	}

	fprintf (csv, "%s\n", values);

	/* The lock is implicitely released. I we don't release it explicitely
	 * because the `FILE *' may need to flush a cache first */
	fclose (csv);

	return (0);
} /* int csv_write */

void module_register (void)
{
	plugin_register_config ("csv", csv_config,
			config_keys, config_keys_num);
	plugin_register_write ("csv", csv_write, /* user_data = */ NULL);
} /* void module_register */
