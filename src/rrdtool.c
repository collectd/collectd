/**
 * collectd - src/rrdtool.c
 * Copyright (C) 2006  Florian octo Forster
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
#include "plugin.h"
#include "common.h"
#include "utils_avltree.h"
#include "utils_debug.h"

/*
 * This weird macro cascade forces the glibc to define `NAN'. I don't know
 * another way to solve this, so more intelligent solutions are welcome. -octo
 */
#ifndef __USE_ISOC99
# define DISABLE__USE_ISOC99 1
# define __USE_ISOC99 1
#endif
#include <math.h>
#ifdef DISABLE__USE_ISOC99
# undef DISABLE__USE_ISOC99
# undef __USE_ISOC99
#endif

/*
 * Private types
 */
struct rrd_cache_s
{
	int    values_num;
	char **values;
	time_t first_value;
};
typedef struct rrd_cache_s rrd_cache_t;

/*
 * Private variables
 */
static int rra_timespans[] =
{
	3600,
	86400,
	604800,
	2678400,
	31622400,
	0
};
static int rra_timespans_num = 5;

static char *rra_types[] =
{
	"AVERAGE",
	"MIN",
	"MAX",
	NULL
};
static int rra_types_num = 3;

static const char *config_keys[] =
{
	"CacheTimeout",
	"CacheFlush",
	"DataDir",
	NULL
};
static int config_keys_num = 3;

static char *datadir = NULL;

static int         cache_timeout = 0;
static int         cache_flush_timeout = 0;
static time_t      cache_flush_last;
static avl_tree_t *cache = NULL;

/* * * * * * * * * *
 * WARNING:  Magic *
 * * * * * * * * * */
static int rra_get (char ***ret)
{
	static char **rra_def = NULL;
	static int rra_num = 0;

	int rra_max = rra_timespans_num * rra_types_num;

	int step;
	int rows;
	int span;

	int cdp_num;
	int cdp_len;
	int i, j;

	char buffer[64];

	if ((rra_num != 0) && (rra_def != NULL))
	{
		*ret = rra_def;
		return (rra_num);
	}

	if ((rra_def = (char **) malloc ((rra_max + 1) * sizeof (char *))) == NULL)
		return (-1);
	memset (rra_def, '\0', (rra_max + 1) * sizeof (char *));

	step = atoi (COLLECTD_STEP);
	rows = atoi (COLLECTD_ROWS);

	if ((step <= 0) || (rows <= 0))
	{
		*ret = NULL;
		return (-1);
	}

	cdp_len = 0;
	for (i = 0; i < rra_timespans_num; i++)
	{
		span = rra_timespans[i];

		if ((span / step) < rows)
			continue;

		if (cdp_len == 0)
			cdp_len = 1;
		else
			cdp_len = (int) floor (((double) span) / ((double) (rows * step)));

		cdp_num = (int) ceil (((double) span) / ((double) (cdp_len * step)));

		for (j = 0; j < rra_types_num; j++)
		{
			if (rra_num >= rra_max)
				break;

			if (snprintf (buffer, sizeof(buffer), "RRA:%s:%3.1f:%u:%u",
						rra_types[j], COLLECTD_XFF,
						cdp_len, cdp_num) >= sizeof (buffer))
			{
				syslog (LOG_ERR, "rra_get: Buffer would have been truncated.");
				continue;
			}

			rra_def[rra_num++] = sstrdup (buffer);
		}
	}

#if COLLECT_DEBUG
	DBG ("rra_num = %i", rra_num);
	for (i = 0; i < rra_num; i++)
		DBG ("  %s", rra_def[i]);
#endif

	*ret = rra_def;
	return (rra_num);
}

static void ds_free (int ds_num, char **ds_def)
{
	int i;

	for (i = 0; i < ds_num; i++)
		if (ds_def[i] != NULL)
			free (ds_def[i]);
	free (ds_def);
}

static int ds_get (char ***ret, const data_set_t *ds)
{
	char **ds_def;
	int ds_num;

	char min[32];
	char max[32];
	char buffer[128];

	DBG ("ds->ds_num = %i", ds->ds_num);

	ds_def = (char **) malloc (ds->ds_num * sizeof (char *));
	if (ds_def == NULL)
	{
		syslog (LOG_ERR, "rrdtool plugin: malloc failed: %s",
				strerror (errno));
		return (-1);
	}
	memset (ds_def, '\0', ds->ds_num * sizeof (char *));

	for (ds_num = 0; ds_num < ds->ds_num; ds_num++)
	{
		data_source_t *d = ds->ds + ds_num;
		char *type;
		int status;

		ds_def[ds_num] = NULL;

		if (d->type == DS_TYPE_COUNTER)
			type = "COUNTER";
		else if (d->type == DS_TYPE_GAUGE)
			type = "GAUGE";
		else
		{
			syslog (LOG_ERR, "rrdtool plugin: Unknown DS type: %i",
					d->type);
			break;
		}

		if (d->min == NAN)
		{
			strcpy (min, "U");
		}
		else
		{
			snprintf (min, sizeof (min), "%lf", d->min);
			min[sizeof (min) - 1] = '\0';
		}

		if (d->max == NAN)
		{
			strcpy (max, "U");
		}
		else
		{
			snprintf (max, sizeof (max), "%lf", d->max);
			max[sizeof (max) - 1] = '\0';
		}

		status = snprintf (buffer, sizeof (buffer),
				"DS:%s:%s:%s:%s:%s",
				d->name, type, COLLECTD_HEARTBEAT,
				min, max);
		if ((status < 1) || (status >= sizeof (buffer)))
			break;

		ds_def[ds_num] = sstrdup (buffer);
	} /* for ds_num = 0 .. ds->ds_num */

#if COLLECT_DEBUG
{
	int i;
	DBG ("ds_num = %i", ds_num);
	for (i = 0; i < ds_num; i++)
		DBG ("  %s", ds_def[i]);
}
#endif

	if (ds_num != ds->ds_num)
	{
		ds_free (ds_num, ds_def);
		return (-1);
	}

	*ret = ds_def;
	return (ds_num);
}

static int rrd_create_file (char *filename, const data_set_t *ds)
{
	char **argv;
	int argc;
	char **rra_def;
	int rra_num;
	char **ds_def;
	int ds_num;
	int i, j;
	int status = 0;

	if (check_create_dir (filename))
		return (-1);

	if ((rra_num = rra_get (&rra_def)) < 1)
	{
		syslog (LOG_ERR, "rrd_create_file failed: Could not calculate RRAs");
		return (-1);
	}

	if ((ds_num = ds_get (&ds_def, ds)) < 1)
	{
		syslog (LOG_ERR, "rrd_create_file failed: Could not calculate DSes");
		return (-1);
	}

	argc = ds_num + rra_num + 4;

	if ((argv = (char **) malloc (sizeof (char *) * (argc + 1))) == NULL)
	{
		syslog (LOG_ERR, "rrd_create failed: %s", strerror (errno));
		return (-1);
	}

	argv[0] = "create";
	argv[1] = filename;
	argv[2] = "-s";
	argv[3] = COLLECTD_STEP;

	j = 4;
	for (i = 0; i < ds_num; i++)
		argv[j++] = ds_def[i];
	for (i = 0; i < rra_num; i++)
		argv[j++] = rra_def[i];
	argv[j] = NULL;

	optind = 0; /* bug in librrd? */
	rrd_clear_error ();
	if (rrd_create (argc, argv) == -1)
	{
		syslog (LOG_ERR, "rrd_create failed: %s: %s", filename, rrd_get_error ());
		status = -1;
	}

	free (argv);
	ds_free (ds_num, ds_def);

	return (status);
}

static int value_list_to_string (char *buffer, int buffer_len,
		const data_set_t *ds, const value_list_t *vl)
{
	int offset;
	int status;
	int i;

	memset (buffer, '\0', sizeof (buffer_len));

	status = snprintf (buffer, buffer_len, "%u", (unsigned int) vl->time);
	if ((status < 1) || (status >= buffer_len))
		return (-1);
	offset = status;

	for (i = 0; i < ds->ds_num; i++)
	{
		if ((ds->ds[i].type != DS_TYPE_COUNTER)
				&& (ds->ds[i].type != DS_TYPE_GAUGE))
			return (-1);

		if (ds->ds[i].type == DS_TYPE_COUNTER)
			status = snprintf (buffer + offset, buffer_len - offset,
					":%llu", vl->values[i].counter);
		else
			status = snprintf (buffer + offset, buffer_len - offset,
					":%lf", vl->values[i].gauge);

		if ((status < 1) || (status >= (buffer_len - offset)))
			return (-1);

		offset += status;
	} /* for ds->ds_num */

	return (0);
} /* int value_list_to_string */

static int value_list_to_filename (char *buffer, int buffer_len,
		const data_set_t *ds, const value_list_t *vl)
{
	int offset = 0;
	int status;

	if (datadir != NULL)
	{
		status = snprintf (buffer + offset, buffer_len - offset,
				"%s/", datadir);
		if ((status < 1) || (status >= buffer_len - offset))
			return (-1);
		offset += status;
	}

	status = snprintf (buffer + offset, buffer_len - offset,
			"%s/", vl->host);
	if ((status < 1) || (status >= buffer_len - offset))
		return (-1);
	offset += status;

	if (strlen (vl->plugin_instance) > 0)
		status = snprintf (buffer + offset, buffer_len - offset,
				"%s-%s/", vl->plugin, vl->plugin_instance);
	else
		status = snprintf (buffer + offset, buffer_len - offset,
				"%s/", vl->plugin);
	if ((status < 1) || (status >= buffer_len - offset))
		return (-1);
	offset += status;

	if (strlen (vl->type_instance) > 0)
		status = snprintf (buffer + offset, buffer_len - offset,
				"%s-%s.rrd", ds->type, vl->type_instance);
	else
		status = snprintf (buffer + offset, buffer_len - offset,
				"%s.rrd", ds->type);
	if ((status < 1) || (status >= buffer_len - offset))
		return (-1);
	offset += status;

	return (0);
} /* int value_list_to_filename */

static rrd_cache_t *rrd_cache_insert (const char *filename,
		const char *value)
{
	rrd_cache_t *rc = NULL;

	if (cache != NULL)
		avl_get (cache, filename, (void *) &rc);

	if (rc == NULL)
	{
		rc = (rrd_cache_t *) malloc (sizeof (rrd_cache_t));
		if (rc == NULL)
			return (NULL);
		rc->values_num = 0;
		rc->values = NULL;
		rc->first_value = 0;
	}

	rc->values = (char **) realloc ((void *) rc->values,
			(rc->values_num + 1) * sizeof (char *));
	if (rc->values == NULL)
	{
		syslog (LOG_ERR, "rrdtool plugin: realloc failed: %s",
				strerror (errno));
		if (cache != NULL)
		{
			void *cache_key = NULL;
			avl_remove (cache, filename, &cache_key, NULL);
			sfree (cache_key);
		}
		free (rc);
		return (NULL);
	}

	rc->values[rc->values_num] = strdup (value);
	if (rc->values[rc->values_num] != NULL)
		rc->values_num++;

	if (rc->values_num == 1)
		rc->first_value = time (NULL);

	/* Insert if this is the first value */
	if ((cache != NULL) && (rc->values_num == 1))
	{
		void *cache_key = strdup (filename);

		if (cache_key == NULL)
		{
			syslog (LOG_ERR, "rrdtool plugin: strdup failed: %s",
					strerror (errno));
			sfree (rc->values);
			sfree (rc);
			return (NULL);
		}

		avl_insert (cache, cache_key, rc);
	}

	DBG ("rrd_cache_insert (%s, %s) = %p", filename, value, (void *) rc);

	return (rc);
} /* rrd_cache_t *rrd_cache_insert */

static int rrd_write_cache_entry (const char *filename, rrd_cache_t *rc)
{
	char **argv;
	int    argc;

	char *fn;
	int status;

	argc = rc->values_num + 2;
	argv = (char **) malloc ((argc + 1) * sizeof (char *));
	if (argv == NULL)
		return (-1);

	fn = strdup (filename);
	if (fn == NULL)
	{
		free (argv);
		return (-1);
	}

	argv[0] = "update";
	argv[1] = fn;
	memcpy (argv + 2, rc->values, rc->values_num * sizeof (char *));
	argv[argc] = NULL;

	DBG ("rrd_update (argc = %i, argv = %p)", argc, (void *) argv);

	optind = 0; /* bug in librrd? */
	rrd_clear_error ();
	status = rrd_update (argc, argv);

	free (argv);
	free (fn);

	free (rc->values);
	rc->values = NULL;
	rc->values_num = 0;

	if (status != 0)
	{
		syslog (LOG_WARNING, "rrd_update failed: %s: %s",
				filename, rrd_get_error ());
		return (-1);
	}

	return (0);
} /* int rrd_update_file */

static void rrd_cache_flush (int timeout)
{
	rrd_cache_t *rc;
	time_t       now;

	char **keys = NULL;
	int    keys_num = 0;

	char *key;
	avl_iterator_t *iter;
	int i;

	if (cache == NULL)
		return;

	DBG ("Flushing cache, timeout = %i", timeout);

	now = time (NULL);

	/* Build a list of entries to be flushed */
	iter = avl_get_iterator (cache);
	while (avl_iterator_next (iter, (void *) &key, (void *) &rc) == 0)
	{
		DBG ("key = %s; age = %i;", key, now - rc->first_value);
		if ((now - rc->first_value) >= timeout)
		{
			keys = (char **) realloc ((void *) keys,
					(keys_num + 1) * sizeof (char *));
			if (keys == NULL)
			{
				DBG ("realloc failed: %s", strerror (errno));
				syslog (LOG_ERR, "rrdtool plugin: "
						"realloc failed: %s",
						strerror (errno));
				avl_iterator_destroy (iter);
				return;
			}
			keys[keys_num] = key;
			keys_num++;
		}
	} /* while (avl_iterator_next) */
	avl_iterator_destroy (iter);
	
	for (i = 0; i < keys_num; i++)
	{
		if (avl_remove (cache, keys[i], NULL, (void *) &rc) != 0)
		{
			DBG ("avl_remove (%s) failed.", keys[i]);
			continue;
		}

		rrd_write_cache_entry (keys[i], rc);
		sfree (keys[i]); keys[i] = NULL;
		sfree (rc->values);
		sfree (rc);
	} /* for (i = 0..keys_num) */

	free (keys);
	DBG ("Flushed %i value(s)", keys_num);

	cache_flush_last = now;
} /* void rrd_cache_flush */

static int rrd_write (const data_set_t *ds, const value_list_t *vl)
{
	struct stat  statbuf;
	char         filename[512];
	char         values[512];
	rrd_cache_t *rc;
	time_t       now;

	if (value_list_to_filename (filename, sizeof (filename), ds, vl) != 0)
		return (-1);

	if (value_list_to_string (values, sizeof (values), ds, vl) != 0)
		return (-1);

	if (stat (filename, &statbuf) == -1)
	{
		if (errno == ENOENT)
		{
			if (rrd_create_file (filename, ds))
				return (-1);
		}
		else
		{
			syslog (LOG_ERR, "stat(%s) failed: %s",
					filename, strerror (errno));
			return (-1);
		}
	}
	else if (!S_ISREG (statbuf.st_mode))
	{
		syslog (LOG_ERR, "stat(%s): Not a regular file!",
				filename);
		return (-1);
	}

	rc = rrd_cache_insert (filename, values);
	if (rc == NULL)
		return (-1);

	if (cache == NULL)
	{
		rrd_write_cache_entry (filename, rc);
		free (rc->values);
		free (rc);
		return (0);
	}

	now = time (NULL);

	DBG ("age (%s) = %i", filename, now - rc->first_value);

	if ((now - rc->first_value) >= cache_timeout)
		rrd_write_cache_entry (filename, rc);

	if ((now - cache_flush_last) >= cache_flush_timeout)
	{
		rrd_cache_flush (cache_flush_timeout);
	}

	return (0);
} /* int rrd_write */

static int rrd_config (const char *key, const char *val)
{
	if (strcasecmp ("CacheTimeout", key) == 0)
	{
		int tmp = atoi (val);
		if (tmp < 0)
		{
			fprintf (stderr, "rrdtool: `CacheTimeout' must "
					"be greater than 0.\n");
			return (1);
		}
		cache_timeout = tmp;
	}
	else if (strcasecmp ("CacheFlush", key) == 0)
	{
		int tmp = atoi (val);
		if (tmp < 0)
		{
			fprintf (stderr, "rrdtool: `CacheFlush' must "
					"be greater than 0.\n");
			return (1);
		}
		cache_flush_timeout = tmp;
	}
	else if (strcasecmp ("DataDir", key) == 0)
	{
		if (datadir != NULL)
			free (datadir);
		datadir = strdup (val);
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
	else
	{
		return (-1);
	}
	return (0);
} /* int rrd_config */

static int rrd_shutdown (void)
{
	rrd_cache_flush (-1);

	return (0);
} /* int rrd_shutdown */

static int rrd_init (void)
{
	if (cache_timeout < 2)
	{
		cache_timeout = 0;
		cache_flush_timeout = 0;
	}
	else
	{
		if (cache_flush_timeout < cache_timeout)
			cache_flush_timeout = 10 * cache_timeout;

		cache = avl_create ((int (*) (const void *, const void *)) strcmp);
		cache_flush_last = time (NULL);
		plugin_register_shutdown ("rrdtool", rrd_shutdown);
	}
	return (0);
} /* int rrd_init */

void module_register (void)
{
	plugin_register_config ("rrdtool", rrd_config,
			config_keys, config_keys_num);
	plugin_register_init ("rrdtool", rrd_init);
	plugin_register_write ("rrdtool", rrd_write);
}
