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

#if HAVE_PTHREAD_H
# include <pthread.h>
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
	31622400
};
static int rra_timespans_num = STATIC_ARRAY_SIZE (rra_timespans);

static int *rra_timespans_custom = NULL;
static int rra_timespans_custom_num = 0;

static char *rra_types[] =
{
	"AVERAGE",
	"MIN",
	"MAX"
};
static int rra_types_num = STATIC_ARRAY_SIZE (rra_types);

static const char *config_keys[] =
{
	"CacheTimeout",
	"CacheFlush",
	"DataDir",
	"StepSize",
	"HeartBeat",
	"RRARows",
	"RRATimespan",
	"XFF"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static char   *datadir   = NULL;
static int     stepsize  = 0;
static int     heartbeat = 0;
static int     rrarows   = 1200;
static double  xff       = 0.1;

static int         cache_timeout = 0;
static int         cache_flush_timeout = 0;
static time_t      cache_flush_last;
static avl_tree_t *cache = NULL;
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

/* * * * * * * * * *
 * WARNING:  Magic *
 * * * * * * * * * */
static int rra_get (char ***ret)
{
	static char **rra_def = NULL;
	static int rra_num = 0;

	int *rts;
	int  rts_num;

	int rra_max;

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

	/* Use the configured timespans or fall back to the built-in defaults */
	if (rra_timespans_custom_num != 0)
	{
		rts = rra_timespans_custom;
		rts_num = rra_timespans_custom_num;
	}
	else
	{
		rts = rra_timespans;
		rts_num = rra_timespans_num;
	}

	rra_max = rts_num * rra_types_num;

	if ((rra_def = (char **) malloc ((rra_max + 1) * sizeof (char *))) == NULL)
		return (-1);
	memset (rra_def, '\0', (rra_max + 1) * sizeof (char *));

	if ((stepsize <= 0) || (rrarows <= 0))
	{
		*ret = NULL;
		return (-1);
	}

	cdp_len = 0;
	for (i = 0; i < rts_num; i++)
	{
		span = rts[i];

		if ((span / stepsize) < rrarows)
			continue;

		if (cdp_len == 0)
			cdp_len = 1;
		else
			cdp_len = (int) floor (((double) span)
					/ ((double) (rrarows * stepsize)));

		cdp_num = (int) ceil (((double) span)
				/ ((double) (cdp_len * stepsize)));

		for (j = 0; j < rra_types_num; j++)
		{
			if (rra_num >= rra_max)
				break;

			if (snprintf (buffer, sizeof (buffer), "RRA:%s:%3.1f:%u:%u",
						rra_types[j], xff,
						cdp_len, cdp_num) >= sizeof (buffer))
			{
				ERROR ("rra_get: Buffer would have been truncated.");
				continue;
			}

			rra_def[rra_num++] = sstrdup (buffer);
		}
	}

#if COLLECT_DEBUG
	DEBUG ("rra_num = %i", rra_num);
	for (i = 0; i < rra_num; i++)
		DEBUG ("  %s", rra_def[i]);
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

	DEBUG ("ds->ds_num = %i", ds->ds_num);

	ds_def = (char **) malloc (ds->ds_num * sizeof (char *));
	if (ds_def == NULL)
	{
		char errbuf[1024];
		ERROR ("rrdtool plugin: malloc failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
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
			ERROR ("rrdtool plugin: Unknown DS type: %i",
					d->type);
			break;
		}

		if (isnan (d->min))
		{
			strcpy (min, "U");
		}
		else
		{
			snprintf (min, sizeof (min), "%lf", d->min);
			min[sizeof (min) - 1] = '\0';
		}

		if (isnan (d->max))
		{
			strcpy (max, "U");
		}
		else
		{
			snprintf (max, sizeof (max), "%lf", d->max);
			max[sizeof (max) - 1] = '\0';
		}

		status = snprintf (buffer, sizeof (buffer),
				"DS:%s:%s:%i:%s:%s",
				d->name, type, heartbeat,
				min, max);
		if ((status < 1) || (status >= sizeof (buffer)))
			break;

		ds_def[ds_num] = sstrdup (buffer);
	} /* for ds_num = 0 .. ds->ds_num */

#if COLLECT_DEBUG
{
	int i;
	DEBUG ("ds_num = %i", ds_num);
	for (i = 0; i < ds_num; i++)
		DEBUG ("  %s", ds_def[i]);
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
	char stepsize_str[16];
	int status = 0;

	if (check_create_dir (filename))
		return (-1);

	if ((rra_num = rra_get (&rra_def)) < 1)
	{
		ERROR ("rrd_create_file failed: Could not calculate RRAs");
		return (-1);
	}

	if ((ds_num = ds_get (&ds_def, ds)) < 1)
	{
		ERROR ("rrd_create_file failed: Could not calculate DSes");
		return (-1);
	}

	argc = ds_num + rra_num + 4;

	if ((argv = (char **) malloc (sizeof (char *) * (argc + 1))) == NULL)
	{
		char errbuf[1024];
		ERROR ("rrd_create failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	status = snprintf (stepsize_str, sizeof (stepsize_str),
			"%i", stepsize);
	if ((status < 1) || (status >= sizeof (stepsize_str)))
	{
		ERROR ("rrdtool plugin: snprintf failed.");
		return (-1);
	}

	argv[0] = "create";
	argv[1] = filename;
	argv[2] = "-s";
	argv[3] = stepsize_str;

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
		ERROR ("rrd_create failed: %s: %s", filename, rrd_get_error ());
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
	int new_rc = 0;

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
		new_rc = 1;
	}

	rc->values = (char **) realloc ((void *) rc->values,
			(rc->values_num + 1) * sizeof (char *));
	if (rc->values == NULL)
	{
		char errbuf[1024];
		ERROR ("rrdtool plugin: realloc failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
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
	if ((cache != NULL) && (new_rc == 1))
	{
		void *cache_key = strdup (filename);

		if (cache_key == NULL)
		{
			char errbuf[1024];
			ERROR ("rrdtool plugin: strdup failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			sfree (rc->values[0]);
			sfree (rc->values);
			sfree (rc);
			return (NULL);
		}

		avl_insert (cache, cache_key, rc);
	}

	DEBUG ("rrd_cache_insert (%s, %s) = %p", filename, value, (void *) rc);

	return (rc);
} /* rrd_cache_t *rrd_cache_insert */

static int rrd_write_cache_entry (const char *filename, rrd_cache_t *rc)
{
	char **argv;
	int    argc;

	char *fn;
	int status;

	int i;

	if (rc->values_num < 1)
		return (0);

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

	DEBUG ("rrd_update (argc = %i, argv = %p)", argc, (void *) argv);

	optind = 0; /* bug in librrd? */
	rrd_clear_error ();
	status = rrd_update (argc, argv);
	if (status != 0)
	{
		WARNING ("rrd_update failed: %s: %s",
				filename, rrd_get_error ());
		status = -1;
	}

	free (argv);
	free (fn);
	/* Free the value list of `rc' */
	for (i = 0; i < rc->values_num; i++)
		free (rc->values[i]);
	free (rc->values);
	rc->values = NULL;
	rc->values_num = 0;

	return (status);
} /* int rrd_write_cache_entry */

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

	DEBUG ("Flushing cache, timeout = %i", timeout);

	now = time (NULL);

	/* Build a list of entries to be flushed */
	iter = avl_get_iterator (cache);
	while (avl_iterator_next (iter, (void *) &key, (void *) &rc) == 0)
	{
		DEBUG ("key = %s; age = %i;", key, now - rc->first_value);
		if ((now - rc->first_value) >= timeout)
		{
			keys = (char **) realloc ((void *) keys,
					(keys_num + 1) * sizeof (char *));
			if (keys == NULL)
			{
				char errbuf[1024];
				ERROR ("rrdtool plugin: "
						"realloc failed: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
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
		if (avl_remove (cache, keys[i], (void *) &key, (void *) &rc) != 0)
		{
			DEBUG ("avl_remove (%s) failed.", keys[i]);
			continue;
		}

		rrd_write_cache_entry (keys[i], rc);
		/* rc's value-list is free's by `rrd_write_cache_entry' */
		sfree (rc);
		sfree (key);
		keys[i] = NULL;
	} /* for (i = 0..keys_num) */

	free (keys);
	DEBUG ("Flushed %i value(s)", keys_num);

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

	pthread_mutex_lock (&cache_lock);
	rc = rrd_cache_insert (filename, values);
	if (rc == NULL)
	{
		pthread_mutex_unlock (&cache_lock);
		return (-1);
	}

	if (cache == NULL)
	{
		rrd_write_cache_entry (filename, rc);
		/* rc's value-list is free's by `rrd_write_cache_entry' */
		sfree (rc);
		pthread_mutex_unlock (&cache_lock);
		return (0);
	}

	now = time (NULL);

	DEBUG ("age (%s) = %i", filename, now - rc->first_value);

	/* `rc' is not free'd here, because we'll likely reuse it. If not, then
	 * the next flush will remove this entry.  */
	if ((now - rc->first_value) >= cache_timeout)
		rrd_write_cache_entry (filename, rc);

	if ((now - cache_flush_last) >= cache_flush_timeout)
		rrd_cache_flush (cache_flush_timeout);

	pthread_mutex_unlock (&cache_lock);
	return (0);
} /* int rrd_write */

static int rrd_config (const char *key, const char *value)
{
	if (strcasecmp ("CacheTimeout", key) == 0)
	{
		int tmp = atoi (value);
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
		int tmp = atoi (value);
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
	else if (strcasecmp ("StepSize", key) == 0)
	{
		int tmp = atoi (value);
		if (tmp <= 0)
		{
			fprintf (stderr, "rrdtool: `StepSize' must "
					"be greater than 0.\n");
			return (1);
		}
		stepsize = tmp;
	}
	else if (strcasecmp ("HeartBeat", key) == 0)
	{
		int tmp = atoi (value);
		if (tmp <= 0)
		{
			fprintf (stderr, "rrdtool: `HeartBeat' must "
					"be greater than 0.\n");
			return (1);
		}
		heartbeat = tmp;
	}
	else if (strcasecmp ("RRARows", key) == 0)
	{
		int tmp = atoi (value);
		if (tmp <= 0)
		{
			fprintf (stderr, "rrdtool: `RRARows' must "
					"be greater than 0.\n");
			return (1);
		}
		rrarows = tmp;
	}
	else if (strcasecmp ("RRATimespan", key) == 0)
	{
		char *saveptr = NULL;
		char *dummy;
		char *ptr;
		int *tmp_alloc;

		dummy = value;
		while ((ptr = strtok_r (dummy, ", \t", &saveptr)) != NULL)
		{
			dummy = NULL;
			
			tmp_alloc = realloc (rra_timespans_custom,
					sizeof (int) * (rra_timespans_custom_num + 1));
			if (tmp_alloc == NULL)
			{
				fprintf (stderr, "rrdtool: realloc failed.\n");
				return (1);
			}
			rra_timespans_custom = tmp_alloc;
			rra_timespans_custom[rra_timespans_custom_num] = atoi (ptr);
			if (rra_timespans_custom[rra_timespans_custom_num] != 0)
				rra_timespans_custom_num++;
		} /* while (strtok_r) */

	}
	else if (strcasecmp ("XFF", key) == 0)
	{
		double tmp = atof (value);
		if ((tmp < 0.0) || (tmp >= 1.0))
		{
			fprintf (stderr, "rrdtool: `XFF' must "
					"be in the range 0 to 1 (exclusive).");
			return (1);
		}
		xff = tmp;
	}
	else
	{
		return (-1);
	}
	return (0);
} /* int rrd_config */

static int rrd_shutdown (void)
{
	pthread_mutex_lock (&cache_lock);
	rrd_cache_flush (-1);
	if (cache != NULL)
		avl_destroy (cache);
	cache = NULL;
	pthread_mutex_unlock (&cache_lock);

	return (0);
} /* int rrd_shutdown */

static int rrd_init (void)
{
	if (stepsize <= 0)
		stepsize = interval_g;
	if (heartbeat <= 0)
		heartbeat = 2 * interval_g;

	if (heartbeat < interval_g)
		WARNING ("rrdtool plugin: Your `heartbeat' is "
				"smaller than your `interval'. This will "
				"likely cause problems.");
	else if (stepsize < interval_g)
		WARNING ("rrdtool plugin: Your `stepsize' is "
				"smaller than your `interval'. This will "
				"create needlessly big RRD-files.");

	pthread_mutex_lock (&cache_lock);
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
	pthread_mutex_unlock (&cache_lock);

	DEBUG ("rrdtool plugin: rrd_init: datadir = %s; stepsize = %i;"
			" heartbeat = %i; rrarows = %i; xff = %lf;",
			(datadir == NULL) ? "(null)" : datadir,
			stepsize, heartbeat, rrarows, xff);

	return (0);
} /* int rrd_init */

void module_register (modreg_e load)
{
	plugin_register_config ("rrdtool", rrd_config,
			config_keys, config_keys_num);
	plugin_register_init ("rrdtool", rrd_init);
	plugin_register_write ("rrdtool", rrd_write);
}
