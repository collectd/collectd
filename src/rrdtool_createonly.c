/**
 * collectd - src/rrdtool_createonly.c
 * Copyright (C) 2006-2008  Florian octo Forster
 * Copyright (C) 2008-2008  Sebastian Harl
 * Copyright (C) 2009       Mariusz Gronczewski
 * Copyright (C) 2012       Yves Mettier
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
 *   Sebastian Harl <sh at tokkee.org>
 *   Mariusz Gronczewski <xani666 at gmail.com>
 *   Yves Mettier <ymettier at free.fr>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "utils_avltree.h"
#include "utils_rrdcreate.h"

#if HAVE_PTHREAD_H
# include <pthread.h>
#endif

#include <rrd.h>

/*
 * Private types
 */

typedef enum {
	STAT_CACHE_FILE_UNDEF,
	STAT_CACHE_FILE_EXISTS,
	STAT_CACHE_FILE_MISSING,
	STAT_CACHE_FILE_ERROR
} stat_cache_file_stat_e;

typedef struct stat_cache_s {
	char *filename;
	cdtime_t last_update;
	stat_cache_file_stat_e file_exists;
	struct stat_cache_s *prev;
	struct stat_cache_s *next;
} stat_cache_t;

/*
 * Private variables
 */
static const char *config_keys[] =
{
	"CacheTimeout",
	"CacheFlush",
	"DataDir",
	"StepSize",
	"HeartBeat",
	"RRARows",
	"RRATimespan",
	"XFF",
	"WritesPerSecond",
	"RandomTimeout",
	"CreateRRDOnly",
	"RRA"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

/* If datadir is zero, the daemon's basedir is used. If stepsize or heartbeat
 * is zero a default, depending on the `interval' member of the value list is
 * being used. */
static char *datadir   = NULL;
static size_t datadirlen = 0;
static double write_rate = 0.0;
static rrdcreate_config_t rrdcreate_config =
{
	/* stepsize = */ 0,
	/* heartbeat = */ 0,
	/* rrarows = */ 1200,
	/* xff = */ 0.1,

	/* timespans = */ NULL,
	/* timespans_num = */ 0,

	/* rra_types = */ NULL,
	/* rra_types_num = */ 0,

	/* consolidation_functions = */ NULL,
	/* consolidation_functions_num = */ 0
};

static cdtime_t    cache_timeout = 0;
static cdtime_t    cache_flush_timeout = 0;
static cdtime_t    random_timeout = TIME_T_TO_CDTIME_T (1);
static stat_cache_t *stat_cache_stack_head = NULL;
static stat_cache_t *stat_cache_stack_tail = NULL;
static c_avl_tree_t *stat_cache_tree = NULL;
static cdtime_t    cache_flush_last = 0;

static int do_shutdown = 0;

static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

static int value_list_to_string (char *buffer, int buffer_len,
		const data_set_t *ds, const value_list_t *vl)
{
	int offset;
	int status;
	time_t tt;
	int i;

	memset (buffer, '\0', buffer_len);

	tt = CDTIME_T_TO_TIME_T (vl->time);
	status = ssnprintf (buffer, buffer_len, "%u", (unsigned int) tt);
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

		if (ds->ds[i].type == DS_TYPE_COUNTER)
			status = ssnprintf (buffer + offset, buffer_len - offset,
					":%llu", vl->values[i].counter);
		else if (ds->ds[i].type == DS_TYPE_GAUGE)
			status = ssnprintf (buffer + offset, buffer_len - offset,
					":%lf", vl->values[i].gauge);
		else if (ds->ds[i].type == DS_TYPE_DERIVE)
			status = ssnprintf (buffer + offset, buffer_len - offset,
					":%"PRIi64, vl->values[i].derive);
		else /*if (ds->ds[i].type == DS_TYPE_ABSOLUTE) */
			status = ssnprintf (buffer + offset, buffer_len - offset,
					":%"PRIu64, vl->values[i].absolute);

		if ((status < 1) || (status >= (buffer_len - offset)))
			return (-1);

		offset += status;
	} /* for ds->ds_num */

	return (0);
} /* int value_list_to_string */

static int value_list_to_filename (char *buffer, int buffer_len,
		const data_set_t __attribute__((unused)) *ds, const value_list_t *vl)
{
	int offset = 0;
	int status;

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
				"%s-%s.rrd", vl->type, vl->type_instance);
	else
		status = ssnprintf (buffer + offset, buffer_len - offset,
				"%s.rrd", vl->type);
	if ((status < 1) || (status >= buffer_len - offset))
		return (-1);
	offset += status;

	return (0);
} /* int value_list_to_filename */

static int rrdco_compare_numeric (const void *a_ptr, const void *b_ptr)
{
	int a = *((int *) a_ptr);
	int b = *((int *) b_ptr);

	if (a < b)
		return (-1);
	else if (a > b)
		return (1);
	else
		return (0);
} /* int rrdco_compare_numeric */

static stat_cache_t *new_cache_entry(const char*filename) {
	stat_cache_t *sc;
	if(NULL == (sc = malloc(sizeof(*sc)))) {
		ERROR ("malloc failed for stat_cache_t* (filename '%s'", filename);
		return(NULL);
	}
	if(NULL == (sc->filename = strdup(filename))) {
		ERROR ("strdup failed (filename '%s'", filename);
		free(sc);
		return(NULL);
	}
	sc->last_update = 0;
	sc->file_exists = STAT_CACHE_FILE_UNDEF;
	return(sc);
}

static void register_cache_entry(stat_cache_t* sc) {
/* Warning : when this function is called, the mutex cache_lock should be locked.
 * be sure that it is locled before calling this function, and do not lock it inside.
 */
	int status;
	status = c_avl_insert(stat_cache_tree, sc->filename, sc);
	assert(status == 0);
	sc->prev = NULL;
	sc->next = stat_cache_stack_head;
	if(stat_cache_stack_head) {
		stat_cache_stack_head->prev = sc;
	}
	stat_cache_stack_head = sc;
	if(NULL == stat_cache_stack_tail) {
		stat_cache_stack_tail = sc;
	}
}

static void cache_stack_move_to_tail(stat_cache_t* sc) {
/* Warning : when this function is called, the mutex cache_lock should be locked.
 * be sure that it is locled before calling this function, and do not lock it inside.
 */
	if(NULL == sc->next) return; /* Already at tail */

	if(sc->prev) {
		sc->prev->next = sc->next;
	} else {
		stat_cache_stack_head = sc->next;
	}
	sc->next->prev = sc->prev;

	sc->prev = stat_cache_stack_tail;
	sc->next = NULL;
	stat_cache_stack_tail = sc;
}

static stat_cache_t* stat_file_with_cache(const char *filename) {
		int status = 0;
		int check_on_disk;
		cdtime_t now;
		stat_cache_t *sc = NULL;

		pthread_mutex_lock (&cache_lock);
		if(NULL == stat_cache_tree) {
				stat_cache_tree = c_avl_create ((int (*) (const void *, const void *)) strcmp);
		}
		now = cdtime();
		if (0 != c_avl_get (stat_cache_tree, filename+datadirlen, (void *) &sc)) {
				check_on_disk = 1;
				sc = new_cache_entry(filename+datadirlen);
				assert(NULL != sc);
				register_cache_entry(sc);
		} else {
				if(sc->last_update + cache_timeout < now) {
						check_on_disk = 1;
				} else if(sc->file_exists != STAT_CACHE_FILE_EXISTS) {
						check_on_disk = 1;
				} else {
						check_on_disk = 0;
				}
		}

		assert(NULL != sc);
		if(check_on_disk) {
				struct stat  statbuf;
				if (stat (filename, &statbuf) == -1) {
						if (errno == ENOENT) {
								status = 0;
								sc->file_exists = STAT_CACHE_FILE_MISSING;
						} else {
								char errbuf[1024];
								sc->file_exists = STAT_CACHE_FILE_ERROR;
								ERROR ("stat(%s) failed: %s", filename,
												sstrerror (errno, errbuf,
														sizeof (errbuf)));
						}
				} else if (!S_ISREG (statbuf.st_mode)) {
						sc->file_exists = STAT_CACHE_FILE_ERROR;
						ERROR ("stat(%s): Not a regular file!", filename);
				} else {
						sc->file_exists = STAT_CACHE_FILE_EXISTS;
				}
				sc->last_update = now;
				cache_stack_move_to_tail(sc);
		}
		pthread_mutex_unlock (&cache_lock);
		return(sc);
} /* int stat_file_with_cache */

void stat_cache_flush() {
		cdtime_t now;
		now = cdtime();
		while(12345) {
				stat_cache_t *sc;
				void *key;
				void *value;
				pthread_mutex_lock (&cache_lock);
				if (! (stat_cache_stack_head && (stat_cache_stack_head->last_update + cache_flush_timeout < now))) {
						pthread_mutex_unlock (&cache_lock);
						break;
				}

				if(0 != c_avl_remove(stat_cache_tree, stat_cache_stack_head->filename, &key, &value)) {
						ERROR ("Could not find a cache entry to remove (filename '%s')", stat_cache_stack_head->filename);
				}

				sc = stat_cache_stack_head;
				if(NULL == stat_cache_stack_head->next) {
						stat_cache_stack_tail = NULL;
						stat_cache_stack_head = NULL;
				} else {
						stat_cache_stack_head = stat_cache_stack_head->next;
						stat_cache_stack_head->prev = NULL;
				}
				pthread_mutex_unlock (&cache_lock);
				free(sc->filename);
				free(sc);
		}
}

void stat_cache_free() {
		stat_cache_t *sc;

		pthread_mutex_lock (&cache_lock);
		while(NULL != (sc = stat_cache_stack_head)) {
			stat_cache_stack_head = sc->next;
			free(sc->filename);
			free(sc);
		}
		stat_cache_stack_head = NULL;
		stat_cache_stack_tail = NULL;
		c_avl_destroy(stat_cache_tree);
		stat_cache_tree = NULL;
		pthread_mutex_unlock (&cache_lock);
}

static int rrdco_write (const data_set_t *ds, const value_list_t *vl,
		user_data_t __attribute__((unused)) *user_data)
{
	char         filename[512];
	char         values[512];
	int          status = 0;
	stat_cache_t *sc = NULL;
	cdtime_t now;

	if (do_shutdown)
		return (0);

	now = cdtime();
	if(cache_flush_last+cache_flush_timeout < now) {
		stat_cache_flush();
	}

	if (0 != strcmp (ds->type, vl->type)) {
		ERROR ("rrdtool_createonly plugin: DS type does not match value list type");
		return (-1);
	}

	if (value_list_to_filename (filename, sizeof (filename), ds, vl) != 0)
		return (-1);
	

	sc = stat_file_with_cache(filename);
	assert(NULL != sc);

	if (value_list_to_string (values, sizeof (values), ds, vl) != 0)
		return (-1);

	if (sc->file_exists == STAT_CACHE_FILE_MISSING) {
			status = cu_rrd_create_file (filename,
					ds, vl, &rrdcreate_config);
			if (status != 0)
				return (-1);
	}

	return (0);
} /* int rrdco_write */

static int rrdco_config (const char *key, const char *value)
{
	if (strcasecmp ("CacheTimeout", key) == 0)
	{
		double tmp = atof (value);
		if (tmp < 0)
		{
			fprintf (stderr, "rrdtool_createonly: `CacheTimeout' must "
					"be greater than 0.\n");
			ERROR ("rrdtool_createonly: `CacheTimeout' must "
					"be greater than 0.\n");
			return (1);
		}
		cache_timeout = DOUBLE_TO_CDTIME_T (tmp);
	}
	else if (strcasecmp ("CacheFlush", key) == 0)
	{
		int tmp = atoi (value);
		if (tmp < 0)
		{
			fprintf (stderr, "rrdtool_createonly: `CacheFlush' must "
					"be greater than 0.\n");
			ERROR ("rrdtool_createonly: `CacheFlush' must "
					"be greater than 0.\n");
			return (1);
		}
		cache_flush_timeout = DOUBLE_TO_CDTIME_T (tmp);
	}
	else if (strcasecmp ("DataDir", key) == 0)
	{
		if (datadir != NULL)
			free (datadir);
		datadir = strdup (value);
		if (datadir != NULL)
		{
			datadirlen = strlen (datadir);
			while ((datadirlen > 0) && (datadir[datadirlen - 1] == '/'))
			{
				datadirlen--;
				datadir[datadirlen] = '\0';
			}
			if (datadirlen <= 0)
			{
				free (datadir);
				datadir = NULL;
				datadirlen = 0;
			}
		}
	}
	else if (strcasecmp ("StepSize", key) == 0)
	{
		unsigned long temp = strtoul (value, NULL, 0);
		if (temp > 0)
			rrdcreate_config.stepsize = temp;
	}
	else if (strcasecmp ("HeartBeat", key) == 0)
	{
		int temp = atoi (value);
		if (temp > 0)
			rrdcreate_config.heartbeat = temp;
	}
	else if (strcasecmp ("RRARows", key) == 0)
	{
		int tmp = atoi (value);
		if (tmp <= 0)
		{
			fprintf (stderr, "rrdtool_createonly: `RRARows' must "
					"be greater than 0.\n");
			ERROR ("rrdtool_createonly: `RRARows' must "
					"be greater than 0.\n");
			return (1);
		}
		rrdcreate_config.rrarows = tmp;
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
			
			tmp_alloc = realloc (rrdcreate_config.timespans,
					sizeof (int) * (rrdcreate_config.timespans_num + 1));
			if (tmp_alloc == NULL)
			{
				fprintf (stderr, "rrdtool_createonly: realloc failed.\n");
				ERROR ("rrdtool_createonly: realloc failed.\n");
				free (value_copy);
				return (1);
			}
			rrdcreate_config.timespans = tmp_alloc;
			rrdcreate_config.timespans[rrdcreate_config.timespans_num] = atoi (ptr);
			if (rrdcreate_config.timespans[rrdcreate_config.timespans_num] != 0)
				rrdcreate_config.timespans_num++;
		} /* while (strtok_r) */

		qsort (/* base = */ rrdcreate_config.timespans,
				/* nmemb  = */ rrdcreate_config.timespans_num,
				/* size   = */ sizeof (rrdcreate_config.timespans[0]),
				/* compar = */ rrdco_compare_numeric);

		free (value_copy);
	}
	else if (strcasecmp ("XFF", key) == 0)
	{
		double tmp = atof (value);
		if ((tmp < 0.0) || (tmp >= 1.0))
		{
			fprintf (stderr, "rrdtool_createonly: `XFF' must "
					"be in the range 0 to 1 (exclusive).");
			ERROR ("rrdtool_createonly: `XFF' must "
					"be in the range 0 to 1 (exclusive).");
			return (1);
		}
		rrdcreate_config.xff = tmp;
	}
	else if (strcasecmp ("WritesPerSecond", key) == 0)
	{
		double wps = atof (value);

		if (wps < 0.0)
		{
			fprintf (stderr, "rrdtool_createonly: `WritesPerSecond' must be "
					"greater than or equal to zero.");
			return (1);
		}
		else if (wps == 0.0)
		{
			write_rate = 0.0;
		}
		else
		{
			write_rate = 1.0 / wps;
		}
	}
	else if (strcasecmp ("RandomTimeout", key) == 0)
        {
		double tmp;

		tmp = atof (value);
		if (tmp < 0.0)
		{
			fprintf (stderr, "rrdtool_createonly: `RandomTimeout' must "
					"be greater than or equal to zero.\n");
			ERROR ("rrdtool_createonly: `RandomTimeout' must "
					"be greater then or equal to zero.");
		}
		else
		{
			random_timeout = DOUBLE_TO_CDTIME_T (tmp);
		}
	}
	else if (strcasecmp ("RRA", key) == 0)
        {
			if(NULL != value) {
				if(0 != cu_rrd_rra_types_set(&rrdcreate_config, value))
					return(-1);
			}
	}
	else
	{
		return (-1);
	}
	return (0);
} /* int rrdco_config */

static int rrdco_shutdown (void)
{
	stat_cache_free();
	do_shutdown = 1;

	return (0);
} /* int rrdco_shutdown */

static int rrdco_init (void)
{
	static int init_once = 0;

	if (init_once != 0)
		return (0);
	init_once = 1;

	if (rrdcreate_config.heartbeat <= 0)
		rrdcreate_config.heartbeat = 2 * rrdcreate_config.stepsize;

	stat_cache_tree = c_avl_create ((int (*) (const void *, const void *)) strcmp);
	stat_cache_stack_head = NULL;
	stat_cache_stack_tail = NULL;

	if (stat_cache_tree == NULL)
	{
		ERROR ("rrdtool_createonly plugin: c_avl_create failed.");
		return (-1);
	}

	cache_flush_last = cdtime ();
	if (cache_timeout == 0)
	{
		cache_flush_timeout = 0;
	}
	else if (cache_flush_timeout < cache_timeout)
		cache_flush_timeout = 10 * cache_timeout;

	DEBUG ("rrdtool_createonly plugin: rrdco_init: datadir = %s; stepsize = %lu;"
			" heartbeat = %i; rrarows = %i; xff = %lf;",
			(datadir == NULL) ? "(null)" : datadir,
			rrdcreate_config.stepsize,
			rrdcreate_config.heartbeat,
			rrdcreate_config.rrarows,
			rrdcreate_config.xff);

	return (0);
} /* int rrdco_init */

static int rrdco_flush (__attribute__((unused)) cdtime_t timeout, /* {{{ */
				const char *identifier,
				__attribute__((unused)) user_data_t *ud)
{

		stat_cache_flush();
		DEBUG ("rrdtool_createonly plugin: rrdco_flush : Success." );

		return (0);
} /* }}} int rrdco_flush */


void module_register (void)
{
	plugin_register_config ("rrdtool_createonly", rrdco_config,
			config_keys, config_keys_num);
	plugin_register_init ("rrdtool_createonly", rrdco_init);
	plugin_register_write ("rrdtool_createonly", rrdco_write, /* user_data = */ NULL);
	plugin_register_flush ("rrdtool_createonly", rrdco_flush, /* user_data = */ NULL);
	plugin_register_shutdown ("rrdtool_createonly", rrdco_shutdown);
}
