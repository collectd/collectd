/**
 * collectd - src/rrdtool.c
 * Copyright (C) 2006,2007  Florian octo Forster
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

#include <rrd.h>

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
	time_t last_value;
	enum
	{
		FLAG_NONE   = 0x00,
		FLAG_QUEUED = 0x01
	} flags;
};
typedef struct rrd_cache_s rrd_cache_t;

struct rrd_queue_s
{
	char *filename;
	struct rrd_queue_s *next;
};
typedef struct rrd_queue_s rrd_queue_t;

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

/* If datadir is zero, the daemon's basedir is used. If stepsize or heartbeat
 * is zero a default, depending on the `interval' member of the value list is
 * being used. */
static char   *datadir   = NULL;
static int     stepsize  = 0;
static int     heartbeat = 0;
static int     rrarows   = 1200;
static double  xff       = 0.1;

/* XXX: If you need to lock both, cache_lock and queue_lock, at the same time,
 * ALWAYS lock `cache_lock' first! */
static int         cache_timeout = 0;
static int         cache_flush_timeout = 0;
static time_t      cache_flush_last;
static c_avl_tree_t *cache = NULL;
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

static rrd_queue_t    *queue_head = NULL;
static rrd_queue_t    *queue_tail = NULL;
static pthread_t       queue_thread = 0;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  queue_cond = PTHREAD_COND_INITIALIZER;

#if !HAVE_THREADSAFE_LIBRRD
static pthread_mutex_t librrd_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static int do_shutdown = 0;

/* * * * * * * * * *
 * WARNING:  Magic *
 * * * * * * * * * */

static void rra_free (int rra_num, char **rra_def)
{
	int i;

	for (i = 0; i < rra_num; i++)
	{
		sfree (rra_def[i]);
	}
	sfree (rra_def);
} /* void rra_free */

static int rra_get (char ***ret, const value_list_t *vl)
{
	char **rra_def;
	int rra_num;

	int *rts;
	int  rts_num;

	int rra_max;

	int span;

	int cdp_num;
	int cdp_len;
	int i, j;

	char buffer[64];

	/* The stepsize we use here: If it is user-set, use it. If not, use the
	 * interval of the value-list. */
	int ss;

	if (rrarows <= 0)
	{
		*ret = NULL;
		return (-1);
	}

	ss = (stepsize > 0) ? stepsize : vl->interval;
	if (ss <= 0)
	{
		*ret = NULL;
		return (-1);
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
	rra_num = 0;

	cdp_len = 0;
	for (i = 0; i < rts_num; i++)
	{
		span = rts[i];

		if ((span / ss) < rrarows)
			span = ss * rrarows;

		if (cdp_len == 0)
			cdp_len = 1;
		else
			cdp_len = (int) floor (((double) span)
					/ ((double) (rrarows * ss)));

		cdp_num = (int) ceil (((double) span)
				/ ((double) (cdp_len * ss)));

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
} /* int rra_get */

static void ds_free (int ds_num, char **ds_def)
{
	int i;

	for (i = 0; i < ds_num; i++)
		if (ds_def[i] != NULL)
			free (ds_def[i]);
	free (ds_def);
}

static int ds_get (char ***ret, const data_set_t *ds, const value_list_t *vl)
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
				d->name, type,
				(heartbeat > 0) ? heartbeat : (2 * vl->interval),
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

#if HAVE_THREADSAFE_LIBRRD
static int srrd_create (char *filename, unsigned long pdp_step, time_t last_up,
		int argc, char **argv)
{
	int status;

	optind = 0; /* bug in librrd? */
	rrd_clear_error ();

	status = rrd_create_r (filename, pdp_step, last_up, argc, argv);

	if (status != 0)
	{
		WARNING ("rrdtool plugin: rrd_create_r (%s) failed: %s",
				filename, rrd_get_error ());
	}

	return (status);
} /* int srrd_create */

static int srrd_update (char *filename, char *template, int argc, char **argv)
{
	int status;

	optind = 0; /* bug in librrd? */
	rrd_clear_error ();

	status = rrd_update_r (filename, template, argc, argv);

	if (status != 0)
	{
		WARNING ("rrdtool plugin: rrd_update_r (%s) failed: %s",
				filename, rrd_get_error ());
	}

	return (status);
} /* int srrd_update */
/* #endif HAVE_THREADSAFE_LIBRRD */

#else /* !HAVE_THREADSAFE_LIBRRD */
static int srrd_create (char *filename, unsigned long pdp_step, time_t last_up,
		int argc, char **argv)
{
	int status;

	int new_argc;
	char **new_argv;

	char pdp_step_str[16];
	char last_up_str[16];

	new_argc = 6 + argc;
	new_argv = (char **) malloc ((new_argc + 1) * sizeof (char *));
	if (new_argv == NULL)
	{
		ERROR ("rrdtool plugin: malloc failed.");
		return (-1);
	}

	if (last_up == 0)
		last_up = time (NULL) - 10;

	snprintf (pdp_step_str, sizeof (pdp_step_str), "%lu", pdp_step);
	pdp_step_str[sizeof (pdp_step_str) - 1] = '\0';
	snprintf (last_up_str, sizeof (last_up_str), "%u", (unsigned int) last_up);
	last_up_str[sizeof (last_up_str) - 1] = '\0';

	new_argv[0] = "create";
	new_argv[1] = filename;
	new_argv[2] = "-s";
	new_argv[3] = pdp_step_str;
	new_argv[4] = "-b";
	new_argv[5] = last_up_str;

	memcpy (new_argv + 6, argv, argc * sizeof (char *));
	new_argv[new_argc] = NULL;
	
	pthread_mutex_lock (&librrd_lock);
	optind = 0; /* bug in librrd? */
	rrd_clear_error ();

	status = rrd_create (new_argc, new_argv);
	pthread_mutex_unlock (&librrd_lock);

	if (status != 0)
	{
		WARNING ("rrdtool plugin: rrd_create (%s) failed: %s",
				filename, rrd_get_error ());
	}

	sfree (new_argv);

	return (status);
} /* int srrd_create */

static int srrd_update (char *filename, char *template, int argc, char **argv)
{
	int status;

	int new_argc;
	char **new_argv;

	assert (template == NULL);

	new_argc = 2 + argc;
	new_argv = (char **) malloc ((new_argc + 1) * sizeof (char *));
	if (new_argv == NULL)
	{
		ERROR ("rrdtool plugin: malloc failed.");
		return (-1);
	}

	new_argv[0] = "update";
	new_argv[1] = filename;

	memcpy (new_argv + 2, argv, argc * sizeof (char *));
	new_argv[new_argc] = NULL;

	pthread_mutex_lock (&librrd_lock);
	optind = 0; /* bug in librrd? */
	rrd_clear_error ();

	status = rrd_update (new_argc, new_argv);
	pthread_mutex_unlock (&librrd_lock);

	if (status != 0)
	{
		WARNING ("rrdtool plugin: rrd_update_r failed: %s: %s",
				argv[1], rrd_get_error ());
	}

	sfree (new_argv);

	return (status);
} /* int srrd_update */
#endif /* !HAVE_THREADSAFE_LIBRRD */

static int rrd_create_file (char *filename, const data_set_t *ds, const value_list_t *vl)
{
	char **argv;
	int argc;
	char **rra_def;
	int rra_num;
	char **ds_def;
	int ds_num;
	int status = 0;

	if (check_create_dir (filename))
		return (-1);

	if ((rra_num = rra_get (&rra_def, vl)) < 1)
	{
		ERROR ("rrd_create_file failed: Could not calculate RRAs");
		return (-1);
	}

	if ((ds_num = ds_get (&ds_def, ds, vl)) < 1)
	{
		ERROR ("rrd_create_file failed: Could not calculate DSes");
		return (-1);
	}

	argc = ds_num + rra_num;

	if ((argv = (char **) malloc (sizeof (char *) * (argc + 1))) == NULL)
	{
		char errbuf[1024];
		ERROR ("rrd_create failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	memcpy (argv, ds_def, ds_num * sizeof (char *));
	memcpy (argv + ds_num, rra_def, rra_num * sizeof (char *));
	argv[ds_num + rra_num] = NULL;

	assert (vl->time > 10);
	status = srrd_create (filename,
			(stepsize > 0) ? stepsize : vl->interval,
			vl->time - 10,
			argc, argv);

	free (argv);
	ds_free (ds_num, ds_def);
	rra_free (rra_num, rra_def);

	return (status);
}

static int value_list_to_string (char *buffer, int buffer_len,
		const data_set_t *ds, const value_list_t *vl)
{
	int offset;
	int status;
	int i;

	memset (buffer, '\0', buffer_len);

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

static void *rrd_queue_thread (void *data)
{
	while (42)
	{
		rrd_queue_t *queue_entry;
		rrd_cache_t *cache_entry;
		char **values;
		int    values_num;
		int    i;

		/* XXX: If you need to lock both, cache_lock and queue_lock, at
		 * the same time, ALWAYS lock `cache_lock' first! */

		/* wait until an entry is available */
		pthread_mutex_lock (&queue_lock);
		while ((queue_head == NULL) && (do_shutdown == 0))
			pthread_cond_wait (&queue_cond, &queue_lock);

		/* We're in the shutdown phase */
		if (queue_head == NULL)
		{
			pthread_mutex_unlock (&queue_lock);
			break;
		}

		/* Dequeue the first entry */
		queue_entry = queue_head;
		if (queue_head == queue_tail)
			queue_head = queue_tail = NULL;
		else
			queue_head = queue_head->next;

		/* Unlock the queue again */
		pthread_mutex_unlock (&queue_lock);

		/* We now need the cache lock so the entry isn't updated while
		 * we make a copy of it's values */
		pthread_mutex_lock (&cache_lock);

		c_avl_get (cache, queue_entry->filename, (void *) &cache_entry);

		values = cache_entry->values;
		values_num = cache_entry->values_num;

		cache_entry->values = NULL;
		cache_entry->values_num = 0;
		cache_entry->flags = FLAG_NONE;

		pthread_mutex_unlock (&cache_lock);

		/* Write the values to the RRD-file */
		srrd_update (queue_entry->filename, NULL, values_num, values);
		DEBUG ("rrdtool plugin: queue thread: Wrote %i values to %s",
				values_num, queue_entry->filename);

		for (i = 0; i < values_num; i++)
		{
			sfree (values[i]);
		}
		sfree (values);
		sfree (queue_entry->filename);
		sfree (queue_entry);
	} /* while (42) */

	pthread_mutex_lock (&cache_lock);
	c_avl_destroy (cache);
	cache = NULL;
	pthread_mutex_unlock (&cache_lock);

	pthread_exit ((void *) 0);
	return ((void *) 0);
} /* void *rrd_queue_thread */

static int rrd_queue_cache_entry (const char *filename)
{
	rrd_queue_t *queue_entry;

	queue_entry = (rrd_queue_t *) malloc (sizeof (rrd_queue_t));
	if (queue_entry == NULL)
		return (-1);

	queue_entry->filename = strdup (filename);
	if (queue_entry->filename == NULL)
	{
		free (queue_entry);
		return (-1);
	}

	queue_entry->next = NULL;

	pthread_mutex_lock (&queue_lock);
	if (queue_tail == NULL)
		queue_head = queue_entry;
	else
		queue_tail->next = queue_entry;
	queue_tail = queue_entry;
	pthread_cond_signal (&queue_cond);
	pthread_mutex_unlock (&queue_lock);

	DEBUG ("rrdtool plugin: Put `%s' into the update queue", filename);

	return (0);
} /* int rrd_queue_cache_entry */

static void rrd_cache_flush (int timeout)
{
	rrd_cache_t *rc;
	time_t       now;

	char **keys = NULL;
	int    keys_num = 0;

	char *key;
	c_avl_iterator_t *iter;
	int i;

	DEBUG ("rrdtool plugin: Flushing cache, timeout = %i", timeout);

	now = time (NULL);

	/* Build a list of entries to be flushed */
	iter = c_avl_get_iterator (cache);
	while (c_avl_iterator_next (iter, (void *) &key, (void *) &rc) == 0)
	{
		if (rc->flags == FLAG_QUEUED)
			continue;
		else if ((now - rc->first_value) < timeout)
			continue;
		else if (rc->values_num > 0)
		{
			if (rrd_queue_cache_entry (key) == 0)
				rc->flags = FLAG_QUEUED;
		}
		else /* ancient and no values -> waste of memory */
		{
			char **tmp = (char **) realloc ((void *) keys,
					(keys_num + 1) * sizeof (char *));
			if (tmp == NULL)
			{
				char errbuf[1024];
				ERROR ("rrdtool plugin: "
						"realloc failed: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				c_avl_iterator_destroy (iter);
				sfree (keys);
				return;
			}
			keys = tmp;
			keys[keys_num] = key;
			keys_num++;
		}
	} /* while (c_avl_iterator_next) */
	c_avl_iterator_destroy (iter);
	
	for (i = 0; i < keys_num; i++)
	{
		if (c_avl_remove (cache, keys[i], (void *) &key, (void *) &rc) != 0)
		{
			DEBUG ("rrdtool plugin: c_avl_remove (%s) failed.", keys[i]);
			continue;
		}

		assert (rc->values == NULL);
		assert (rc->values_num == 0);

		sfree (rc);
		sfree (key);
		keys[i] = NULL;
	} /* for (i = 0..keys_num) */

	sfree (keys);

	cache_flush_last = now;
} /* void rrd_cache_flush */

static int rrd_cache_insert (const char *filename,
		const char *value, time_t value_time)
{
	rrd_cache_t *rc = NULL;
	int new_rc = 0;
	char **values_new;

	pthread_mutex_lock (&cache_lock);

	c_avl_get (cache, filename, (void *) &rc);

	if (rc == NULL)
	{
		rc = (rrd_cache_t *) malloc (sizeof (rrd_cache_t));
		if (rc == NULL)
			return (-1);
		rc->values_num = 0;
		rc->values = NULL;
		rc->first_value = 0;
		rc->last_value = 0;
		rc->flags = FLAG_NONE;
		new_rc = 1;
	}

	if (rc->last_value >= value_time)
	{
		pthread_mutex_unlock (&cache_lock);
		WARNING ("rrdtool plugin: (rc->last_value = %u) >= (value_time = %u)",
				(unsigned int) rc->last_value,
				(unsigned int) value_time);
		return (-1);
	}

	values_new = (char **) realloc ((void *) rc->values,
			(rc->values_num + 1) * sizeof (char *));
	if (values_new == NULL)
	{
		char errbuf[1024];
		void *cache_key = NULL;

		sstrerror (errno, errbuf, sizeof (errbuf));

		c_avl_remove (cache, filename, &cache_key, NULL);
		pthread_mutex_unlock (&cache_lock);

		ERROR ("rrdtool plugin: realloc failed: %s", errbuf);

		sfree (cache_key);
		sfree (rc->values);
		sfree (rc);
		return (-1);
	}
	rc->values = values_new;

	rc->values[rc->values_num] = strdup (value);
	if (rc->values[rc->values_num] != NULL)
		rc->values_num++;

	if (rc->values_num == 1)
		rc->first_value = value_time;
	rc->last_value = value_time;

	/* Insert if this is the first value */
	if (new_rc == 1)
	{
		void *cache_key = strdup (filename);

		if (cache_key == NULL)
		{
			char errbuf[1024];
			sstrerror (errno, errbuf, sizeof (errbuf));

			pthread_mutex_unlock (&cache_lock);

			ERROR ("rrdtool plugin: strdup failed: %s", errbuf);

			sfree (rc->values[0]);
			sfree (rc->values);
			sfree (rc);
			return (-1);
		}

		c_avl_insert (cache, cache_key, rc);
	}

	DEBUG ("rrdtool plugin: rrd_cache_insert: file = %s; "
			"values_num = %i; age = %u;",
			filename, rc->values_num,
			rc->last_value - rc->first_value);

	if ((rc->last_value - rc->first_value) >= cache_timeout)
	{
		/* XXX: If you need to lock both, cache_lock and queue_lock, at
		 * the same time, ALWAYS lock `cache_lock' first! */
		if (rc->flags != FLAG_QUEUED)
		{
			if (rrd_queue_cache_entry (filename) == 0)
				rc->flags = FLAG_QUEUED;
		}
		else
		{
			DEBUG ("rrdtool plugin: `%s' is already queued.", filename);
		}
	}

	if ((cache_timeout > 0) &&
			((time (NULL) - cache_flush_last) > cache_flush_timeout))
		rrd_cache_flush (cache_flush_timeout);


	pthread_mutex_unlock (&cache_lock);

	return (0);
} /* int rrd_cache_insert */

static int rrd_compare_numeric (const void *a_ptr, const void *b_ptr)
{
	int a = *((int *) a_ptr);
	int b = *((int *) b_ptr);

	if (a < b)
		return (-1);
	else if (a > b)
		return (1);
	else
		return (0);
} /* int rrd_compare_numeric */

static int rrd_write (const data_set_t *ds, const value_list_t *vl)
{
	struct stat  statbuf;
	char         filename[512];
	char         values[512];
	int          status;

	if (value_list_to_filename (filename, sizeof (filename), ds, vl) != 0)
		return (-1);

	if (value_list_to_string (values, sizeof (values), ds, vl) != 0)
		return (-1);

	if (stat (filename, &statbuf) == -1)
	{
		if (errno == ENOENT)
		{
			if (rrd_create_file (filename, ds, vl))
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

	status = rrd_cache_insert (filename, values, vl->time);

	return (status);
} /* int rrd_write */

static int rrd_flush (const int timeout)
{
	pthread_mutex_lock (&cache_lock);

	if (cache == NULL) {
		pthread_mutex_unlock (&cache_lock);
		return (0);
	}

	rrd_cache_flush (timeout);
	pthread_mutex_unlock (&cache_lock);
	return (0);
} /* int rrd_flush */

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
		stepsize = atoi (value);
		if (stepsize < 0)
			stepsize = 0;
	}
	else if (strcasecmp ("HeartBeat", key) == 0)
	{
		heartbeat = atoi (value);
		if (heartbeat < 0)
			heartbeat = 0;
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
		char *value_copy;
		int *tmp_alloc;

		value_copy = strdup (value);
		if (value_copy == NULL)
			return (1);

		dummy = value_copy;
		while ((ptr = strtok_r (dummy, ", \t", &saveptr)) != NULL)
		{
			dummy = NULL;
			
			tmp_alloc = realloc (rra_timespans_custom,
					sizeof (int) * (rra_timespans_custom_num + 1));
			if (tmp_alloc == NULL)
			{
				fprintf (stderr, "rrdtool: realloc failed.\n");
				free (value_copy);
				return (1);
			}
			rra_timespans_custom = tmp_alloc;
			rra_timespans_custom[rra_timespans_custom_num] = atoi (ptr);
			if (rra_timespans_custom[rra_timespans_custom_num] != 0)
				rra_timespans_custom_num++;
		} /* while (strtok_r) */

		qsort (/* base = */ rra_timespans_custom,
				/* nmemb  = */ rra_timespans_custom_num,
				/* size   = */ sizeof (rra_timespans_custom[0]),
				/* compar = */ rrd_compare_numeric);

		free (value_copy);
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
	pthread_mutex_unlock (&cache_lock);

	pthread_mutex_lock (&queue_lock);
	do_shutdown = 1;
	pthread_cond_signal (&queue_cond);
	pthread_mutex_unlock (&queue_lock);

	/* Wait for all the values to be written to disk before returning. */
	if (queue_thread != 0)
	{
		pthread_join (queue_thread, NULL);
		queue_thread = 0;
		DEBUG ("rrdtool plugin: queue_thread exited.");
	}

	return (0);
} /* int rrd_shutdown */

static int rrd_init (void)
{
	int status;

	if (stepsize < 0)
		stepsize = 0;
	if (heartbeat <= 0)
		heartbeat = 2 * stepsize;

	if ((heartbeat > 0) && (heartbeat < interval_g))
		WARNING ("rrdtool plugin: Your `heartbeat' is "
				"smaller than your `interval'. This will "
				"likely cause problems.");
	else if ((stepsize > 0) && (stepsize < interval_g))
		WARNING ("rrdtool plugin: Your `stepsize' is "
				"smaller than your `interval'. This will "
				"create needlessly big RRD-files.");

	/* Set the cache up */
	pthread_mutex_lock (&cache_lock);

	cache = c_avl_create ((int (*) (const void *, const void *)) strcmp);
	if (cache == NULL)
	{
		ERROR ("rrdtool plugin: c_avl_create failed.");
		return (-1);
	}

	cache_flush_last = time (NULL);
	if (cache_timeout < 2)
	{
		cache_timeout = 0;
		cache_flush_timeout = 0;
	}
	else if (cache_flush_timeout < cache_timeout)
		cache_flush_timeout = 10 * cache_timeout;

	pthread_mutex_unlock (&cache_lock);

	status = pthread_create (&queue_thread, NULL, rrd_queue_thread, NULL);
	if (status != 0)
	{
		ERROR ("rrdtool plugin: Cannot create queue-thread.");
		return (-1);
	}

	DEBUG ("rrdtool plugin: rrd_init: datadir = %s; stepsize = %i;"
			" heartbeat = %i; rrarows = %i; xff = %lf;",
			(datadir == NULL) ? "(null)" : datadir,
			stepsize, heartbeat, rrarows, xff);

	return (0);
} /* int rrd_init */

void module_register (void)
{
	plugin_register_config ("rrdtool", rrd_config,
			config_keys, config_keys_num);
	plugin_register_init ("rrdtool", rrd_init);
	plugin_register_write ("rrdtool", rrd_write);
	plugin_register_flush ("rrdtool", rrd_flush);
	plugin_register_shutdown ("rrdtool", rrd_shutdown);
}
