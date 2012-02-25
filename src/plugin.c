/**
 * collectd - src/plugin.c
 * Copyright (C) 2005-2011  Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 *   Sebastian Harl <sh at tokkee.org>
 **/

#include "collectd.h"
#include "utils_complain.h"

#include <ltdl.h>

#if HAVE_PTHREAD_H
# include <pthread.h>
#endif

#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_avltree.h"
#include "utils_llist.h"
#include "utils_heap.h"
#include "utils_cache.h"
#include "filter_chain.h"

/*
 * Private structures
 */
struct callback_func_s
{
	void *cf_callback;
	user_data_t cf_udata;
};
typedef struct callback_func_s callback_func_t;

#define RF_SIMPLE  0
#define RF_COMPLEX 1
#define RF_REMOVE  65535
struct read_func_s
{
	/* `read_func_t' "inherits" from `callback_func_t'.
	 * The `rf_super' member MUST be the first one in this structure! */
#define rf_callback rf_super.cf_callback
#define rf_udata rf_super.cf_udata
	callback_func_t rf_super;
	char rf_group[DATA_MAX_NAME_LEN];
	char rf_name[DATA_MAX_NAME_LEN];
	int rf_type;
	struct timespec rf_interval;
	struct timespec rf_effective_interval;
	struct timespec rf_next_read;
};
typedef struct read_func_s read_func_t;

/*
 * Private variables
 */
static llist_t *list_init;
static llist_t *list_write;
static llist_t *list_flush;
static llist_t *list_missing;
static llist_t *list_shutdown;
static llist_t *list_log;
static llist_t *list_notification;

static fc_chain_t *pre_cache_chain = NULL;
static fc_chain_t *post_cache_chain = NULL;

static c_avl_tree_t *data_sets;

static char *plugindir = NULL;

static c_heap_t       *read_heap = NULL;
static llist_t        *read_list;
static int             read_loop = 1;
static pthread_mutex_t read_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  read_cond = PTHREAD_COND_INITIALIZER;
static pthread_t      *read_threads = NULL;
static int             read_threads_num = 0;

/*
 * Static functions
 */
static const char *plugin_get_dir (void)
{
	if (plugindir == NULL)
		return (PLUGINDIR);
	else
		return (plugindir);
}

static void destroy_callback (callback_func_t *cf) /* {{{ */
{
	if (cf == NULL)
		return;

	if ((cf->cf_udata.data != NULL) && (cf->cf_udata.free_func != NULL))
	{
		cf->cf_udata.free_func (cf->cf_udata.data);
		cf->cf_udata.data = NULL;
		cf->cf_udata.free_func = NULL;
	}
	sfree (cf);
} /* }}} void destroy_callback */

static void destroy_all_callbacks (llist_t **list) /* {{{ */
{
	llentry_t *le;

	if (*list == NULL)
		return;

	le = llist_head (*list);
	while (le != NULL)
	{
		llentry_t *le_next;

		le_next = le->next;

		sfree (le->key);
		destroy_callback (le->value);
		le->value = NULL;

		le = le_next;
	}

	llist_destroy (*list);
	*list = NULL;
} /* }}} void destroy_all_callbacks */

static void destroy_read_heap (void) /* {{{ */
{
	if (read_heap == NULL)
		return;

	while (42)
	{
		callback_func_t *cf;

		cf = c_heap_get_root (read_heap);
		if (cf == NULL)
			break;

		destroy_callback (cf);
	}

	c_heap_destroy (read_heap);
	read_heap = NULL;
} /* }}} void destroy_read_heap */

static int register_callback (llist_t **list, /* {{{ */
		const char *name, callback_func_t *cf)
{
	llentry_t *le;
	char *key;

	if (*list == NULL)
	{
		*list = llist_create ();
		if (*list == NULL)
		{
			ERROR ("plugin: register_callback: "
					"llist_create failed.");
			destroy_callback (cf);
			return (-1);
		}
	}

	key = strdup (name);
	if (key == NULL)
	{
		ERROR ("plugin: register_callback: strdup failed.");
		destroy_callback (cf);
		return (-1);
	}

	le = llist_search (*list, name);
	if (le == NULL)
	{
		le = llentry_create (key, cf);
		if (le == NULL)
		{
			ERROR ("plugin: register_callback: "
					"llentry_create failed.");
			free (key);
			destroy_callback (cf);
			return (-1);
		}

		llist_append (*list, le);
	}
	else
	{
		callback_func_t *old_cf;

		old_cf = le->value;
		le->value = cf;

		WARNING ("plugin: register_callback: "
				"a callback named `%s' already exists - "
				"overwriting the old entry!", name);

		destroy_callback (old_cf);
		sfree (key);
	}

	return (0);
} /* }}} int register_callback */

static int create_register_callback (llist_t **list, /* {{{ */
		const char *name, void *callback, user_data_t *ud)
{
	callback_func_t *cf;

	cf = (callback_func_t *) malloc (sizeof (*cf));
	if (cf == NULL)
	{
		ERROR ("plugin: create_register_callback: malloc failed.");
		return (-1);
	}
	memset (cf, 0, sizeof (*cf));

	cf->cf_callback = callback;
	if (ud == NULL)
	{
		cf->cf_udata.data = NULL;
		cf->cf_udata.free_func = NULL;
	}
	else
	{
		cf->cf_udata = *ud;
	}

	return (register_callback (list, name, cf));
} /* }}} int create_register_callback */

static int plugin_unregister (llist_t *list, const char *name) /* {{{ */
{
	llentry_t *e;

	if (list == NULL)
		return (-1);

	e = llist_search (list, name);
	if (e == NULL)
		return (-1);

	llist_remove (list, e);

	sfree (e->key);
	destroy_callback (e->value);

	llentry_destroy (e);

	return (0);
} /* }}} int plugin_unregister */

/*
 * (Try to) load the shared object `file'. Won't complain if it isn't a shared
 * object, but it will bitch about a shared object not having a
 * ``module_register'' symbol..
 */
static int plugin_load_file (char *file, uint32_t flags)
{
	lt_dlhandle dlh;
	void (*reg_handle) (void);

	lt_dlinit ();
	lt_dlerror (); /* clear errors */

#if LIBTOOL_VERSION == 2
	if (flags & PLUGIN_FLAGS_GLOBAL) {
		lt_dladvise advise;
		lt_dladvise_init(&advise);
		lt_dladvise_global(&advise);
		dlh = lt_dlopenadvise(file, advise);
		lt_dladvise_destroy(&advise);
	} else {
        	dlh = lt_dlopen (file);
	}
#else /* if LIBTOOL_VERSION == 1 */
	if (flags & PLUGIN_FLAGS_GLOBAL)
		WARNING ("plugin_load_file: The global flag is not supported, "
				"libtool 2 is required for this.");
	dlh = lt_dlopen (file);
#endif

	if (dlh == NULL)
	{
		char errbuf[1024] = "";

		ssnprintf (errbuf, sizeof (errbuf),
				"lt_dlopen (\"%s\") failed: %s. "
				"The most common cause for this problem are "
				"missing dependencies. Use ldd(1) to check "
				"the dependencies of the plugin "
				"/ shared object.",
				file, lt_dlerror ());

		ERROR ("%s", errbuf);
		/* Make sure this is printed to STDERR in any case, but also
		 * make sure it's printed only once. */
		if (list_log != NULL)
			fprintf (stderr, "ERROR: %s\n", errbuf);

		return (1);
	}

	if ((reg_handle = (void (*) (void)) lt_dlsym (dlh, "module_register")) == NULL)
	{
		WARNING ("Couldn't find symbol \"module_register\" in \"%s\": %s\n",
				file, lt_dlerror ());
		lt_dlclose (dlh);
		return (-1);
	}

	(*reg_handle) ();

	return (0);
}

static _Bool timeout_reached(struct timespec timeout)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	return (now.tv_sec >= timeout.tv_sec && now.tv_usec >= (timeout.tv_nsec / 1000));
}

static void *plugin_read_thread (void __attribute__((unused)) *args)
{
	while (read_loop != 0)
	{
		read_func_t *rf;
		cdtime_t now;
		int status;
		int rf_type;
		int rc;

		/* Get the read function that needs to be read next. */
		rf = c_heap_get_root (read_heap);
		if (rf == NULL)
		{
			struct timespec abstime;

			now = cdtime ();

			CDTIME_T_TO_TIMESPEC (now + interval_g, &abstime);

			pthread_mutex_lock (&read_lock);
			pthread_cond_timedwait (&read_cond, &read_lock,
					&abstime);
			pthread_mutex_unlock (&read_lock);
			continue;
		}

		if ((rf->rf_interval.tv_sec == 0) && (rf->rf_interval.tv_nsec == 0))
		{
			now = cdtime ();

			CDTIME_T_TO_TIMESPEC (interval_g, &rf->rf_interval);

			rf->rf_effective_interval = rf->rf_interval;

			CDTIME_T_TO_TIMESPEC (now, &rf->rf_next_read);
		}

		/* sleep until this entry is due,
		 * using pthread_cond_timedwait */
		pthread_mutex_lock (&read_lock);
		/* In pthread_cond_timedwait, spurious wakeups are possible
		 * (and really happen, at least on NetBSD with > 1 CPU), thus
		 * we need to re-evaluate the condition every time
		 * pthread_cond_timedwait returns. */
		rc = 0;
		while ((read_loop != 0)
				&& !timeout_reached(rf->rf_next_read)
				&& rc == 0)
		{
			rc = pthread_cond_timedwait (&read_cond, &read_lock,
				&rf->rf_next_read);
		}

		/* Must hold `read_lock' when accessing `rf->rf_type'. */
		rf_type = rf->rf_type;
		pthread_mutex_unlock (&read_lock);

		/* Check if we're supposed to stop.. This may have interrupted
		 * the sleep, too. */
		if (read_loop == 0)
		{
			/* Insert `rf' again, so it can be free'd correctly */
			c_heap_insert (read_heap, rf);
			break;
		}

		/* The entry has been marked for deletion. The linked list
		 * entry has already been removed by `plugin_unregister_read'.
		 * All we have to do here is free the `read_func_t' and
		 * continue. */
		if (rf_type == RF_REMOVE)
		{
			DEBUG ("plugin_read_thread: Destroying the `%s' "
					"callback.", rf->rf_name);
			destroy_callback ((callback_func_t *) rf);
			rf = NULL;
			continue;
		}

		DEBUG ("plugin_read_thread: Handling `%s'.", rf->rf_name);

		if (rf_type == RF_SIMPLE)
		{
			int (*callback) (void);

			callback = rf->rf_callback;
			status = (*callback) ();
		}
		else
		{
			plugin_read_cb callback;

			assert (rf_type == RF_COMPLEX);

			callback = rf->rf_callback;
			status = (*callback) (&rf->rf_udata);
		}

		/* If the function signals failure, we will increase the
		 * intervals in which it will be called. */
		if (status != 0)
		{
			rf->rf_effective_interval.tv_sec *= 2;
			rf->rf_effective_interval.tv_nsec *= 2;
			NORMALIZE_TIMESPEC (rf->rf_effective_interval);

			if (rf->rf_effective_interval.tv_sec >= 86400)
			{
				rf->rf_effective_interval.tv_sec = 86400;
				rf->rf_effective_interval.tv_nsec = 0;
			}

			NOTICE ("read-function of plugin `%s' failed. "
					"Will suspend it for %i seconds.",
					rf->rf_name,
					(int) rf->rf_effective_interval.tv_sec);
		}
		else
		{
			/* Success: Restore the interval, if it was changed. */
			rf->rf_effective_interval = rf->rf_interval;
		}

		/* update the ``next read due'' field */
		now = cdtime ();

		DEBUG ("plugin_read_thread: Effective interval of the "
				"%s plugin is %i.%09i.",
				rf->rf_name,
				(int) rf->rf_effective_interval.tv_sec,
				(int) rf->rf_effective_interval.tv_nsec);

		/* Calculate the next (absolute) time at which this function
		 * should be called. */
		rf->rf_next_read.tv_sec = rf->rf_next_read.tv_sec
			+ rf->rf_effective_interval.tv_sec;
		rf->rf_next_read.tv_nsec = rf->rf_next_read.tv_nsec
			+ rf->rf_effective_interval.tv_nsec;
		NORMALIZE_TIMESPEC (rf->rf_next_read);

		/* Check, if `rf_next_read' is in the past. */
		if (TIMESPEC_TO_CDTIME_T (&rf->rf_next_read) < now)
		{
			/* `rf_next_read' is in the past. Insert `now'
			 * so this value doesn't trail off into the
			 * past too much. */
			CDTIME_T_TO_TIMESPEC (now, &rf->rf_next_read);
		}

		DEBUG ("plugin_read_thread: Next read of the %s plugin at %i.%09i.",
				rf->rf_name,
				(int) rf->rf_next_read.tv_sec,
				(int) rf->rf_next_read.tv_nsec);

		/* Re-insert this read function into the heap again. */
		c_heap_insert (read_heap, rf);
	} /* while (read_loop) */

	pthread_exit (NULL);
	return ((void *) 0);
} /* void *plugin_read_thread */

static void start_read_threads (int num)
{
	int i;

	if (read_threads != NULL)
		return;

	read_threads = (pthread_t *) calloc (num, sizeof (pthread_t));
	if (read_threads == NULL)
	{
		ERROR ("plugin: start_read_threads: calloc failed.");
		return;
	}

	read_threads_num = 0;
	for (i = 0; i < num; i++)
	{
		if (pthread_create (read_threads + read_threads_num, NULL,
					plugin_read_thread, NULL) == 0)
		{
			read_threads_num++;
		}
		else
		{
			ERROR ("plugin: start_read_threads: pthread_create failed.");
			return;
		}
	} /* for (i) */
} /* void start_read_threads */

static void stop_read_threads (void)
{
	int i;

	if (read_threads == NULL)
		return;

	INFO ("collectd: Stopping %i read threads.", read_threads_num);

	pthread_mutex_lock (&read_lock);
	read_loop = 0;
	DEBUG ("plugin: stop_read_threads: Signalling `read_cond'");
	pthread_cond_broadcast (&read_cond);
	pthread_mutex_unlock (&read_lock);

	for (i = 0; i < read_threads_num; i++)
	{
		if (pthread_join (read_threads[i], NULL) != 0)
		{
			ERROR ("plugin: stop_read_threads: pthread_join failed.");
		}
		read_threads[i] = (pthread_t) 0;
	}
	sfree (read_threads);
	read_threads_num = 0;
} /* void stop_read_threads */

/*
 * Public functions
 */
void plugin_set_dir (const char *dir)
{
	if (plugindir != NULL)
		free (plugindir);

	if (dir == NULL)
		plugindir = NULL;
	else if ((plugindir = strdup (dir)) == NULL)
	{
		char errbuf[1024];
		ERROR ("strdup failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
	}
}

#define BUFSIZE 512
int plugin_load (const char *type, uint32_t flags)
{
	DIR  *dh;
	const char *dir;
	char  filename[BUFSIZE] = "";
	char  typename[BUFSIZE];
	int   typename_len;
	int   ret;
	struct stat    statbuf;
	struct dirent *de;
	int status;

	DEBUG ("type = %s", type);

	dir = plugin_get_dir ();
	ret = 1;

	/* `cpu' should not match `cpufreq'. To solve this we add `.so' to the
	 * type when matching the filename */
	status = ssnprintf (typename, sizeof (typename), "%s.so", type);
	if ((status < 0) || ((size_t) status >= sizeof (typename)))
	{
		WARNING ("snprintf: truncated: `%s.so'", type);
		return (-1);
	}
	typename_len = strlen (typename);

	if ((dh = opendir (dir)) == NULL)
	{
		char errbuf[1024];
		ERROR ("opendir (%s): %s", dir,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	while ((de = readdir (dh)) != NULL)
	{
		if (strncasecmp (de->d_name, typename, typename_len))
			continue;

		status = ssnprintf (filename, sizeof (filename),
				"%s/%s", dir, de->d_name);
		if ((status < 0) || ((size_t) status >= sizeof (filename)))
		{
			WARNING ("snprintf: truncated: `%s/%s'", dir, de->d_name);
			continue;
		}

		if (lstat (filename, &statbuf) == -1)
		{
			char errbuf[1024];
			WARNING ("stat %s: %s", filename,
					sstrerror (errno, errbuf, sizeof (errbuf)));
			continue;
		}
		else if (!S_ISREG (statbuf.st_mode))
		{
			/* don't follow symlinks */
			WARNING ("stat %s: not a regular file", filename);
			continue;
		}

		if (plugin_load_file (filename, flags) == 0)
		{
			/* success */
			ret = 0;
			break;
		}
		else
		{
			fprintf (stderr, "Unable to load plugin %s.\n", type);
		}
	}

	closedir (dh);

	if (filename[0] == '\0')
		fprintf (stderr, "Could not find plugin %s.\n", type);

	return (ret);
}

/*
 * The `register_*' functions follow
 */
int plugin_register_config (const char *name,
		int (*callback) (const char *key, const char *val),
		const char **keys, int keys_num)
{
	cf_register (name, callback, keys, keys_num);
	return (0);
} /* int plugin_register_config */

int plugin_register_complex_config (const char *type,
		int (*callback) (oconfig_item_t *))
{
	return (cf_register_complex (type, callback));
} /* int plugin_register_complex_config */

int plugin_register_init (const char *name,
		int (*callback) (void))
{
	return (create_register_callback (&list_init, name, (void *) callback,
				/* user_data = */ NULL));
} /* plugin_register_init */

static int plugin_compare_read_func (const void *arg0, const void *arg1)
{
	const read_func_t *rf0;
	const read_func_t *rf1;

	rf0 = arg0;
	rf1 = arg1;

	if (rf0->rf_next_read.tv_sec < rf1->rf_next_read.tv_sec)
		return (-1);
	else if (rf0->rf_next_read.tv_sec > rf1->rf_next_read.tv_sec)
		return (1);
	else if (rf0->rf_next_read.tv_nsec < rf1->rf_next_read.tv_nsec)
		return (-1);
	else if (rf0->rf_next_read.tv_nsec > rf1->rf_next_read.tv_nsec)
		return (1);
	else
		return (0);
} /* int plugin_compare_read_func */

/* Add a read function to both, the heap and a linked list. The linked list if
 * used to look-up read functions, especially for the remove function. The heap
 * is used to determine which plugin to read next. */
static int plugin_insert_read (read_func_t *rf)
{
	int status;
	llentry_t *le;

	pthread_mutex_lock (&read_lock);

	if (read_list == NULL)
	{
		read_list = llist_create ();
		if (read_list == NULL)
		{
			pthread_mutex_unlock (&read_lock);
			ERROR ("plugin_insert_read: read_list failed.");
			return (-1);
		}
	}

	if (read_heap == NULL)
	{
		read_heap = c_heap_create (plugin_compare_read_func);
		if (read_heap == NULL)
		{
			pthread_mutex_unlock (&read_lock);
			ERROR ("plugin_insert_read: c_heap_create failed.");
			return (-1);
		}
	}

	le = llist_search (read_list, rf->rf_name);
	if (le != NULL)
	{
		pthread_mutex_unlock (&read_lock);
		WARNING ("The read function \"%s\" is already registered. "
				"Check for duplicate \"LoadPlugin\" lines "
				"in your configuration!",
				rf->rf_name);
		return (EINVAL);
	}

	le = llentry_create (rf->rf_name, rf);
	if (le == NULL)
	{
		pthread_mutex_unlock (&read_lock);
		ERROR ("plugin_insert_read: llentry_create failed.");
		return (-1);
	}

	status = c_heap_insert (read_heap, rf);
	if (status != 0)
	{
		pthread_mutex_unlock (&read_lock);
		ERROR ("plugin_insert_read: c_heap_insert failed.");
		llentry_destroy (le);
		return (-1);
	}

	/* This does not fail. */
	llist_append (read_list, le);

	pthread_mutex_unlock (&read_lock);
	return (0);
} /* int plugin_insert_read */

int plugin_register_read (const char *name,
		int (*callback) (void))
{
	read_func_t *rf;
	int status;

	rf = malloc (sizeof (*rf));
	if (rf == NULL)
	{
		ERROR ("plugin_register_read: malloc failed.");
		return (ENOMEM);
	}

	memset (rf, 0, sizeof (read_func_t));
	rf->rf_callback = (void *) callback;
	rf->rf_udata.data = NULL;
	rf->rf_udata.free_func = NULL;
	rf->rf_group[0] = '\0';
	sstrncpy (rf->rf_name, name, sizeof (rf->rf_name));
	rf->rf_type = RF_SIMPLE;
	rf->rf_interval.tv_sec = 0;
	rf->rf_interval.tv_nsec = 0;
	rf->rf_effective_interval = rf->rf_interval;

	status = plugin_insert_read (rf);
	if (status != 0)
		sfree (rf);

	return (status);
} /* int plugin_register_read */

int plugin_register_complex_read (const char *group, const char *name,
		plugin_read_cb callback,
		const struct timespec *interval,
		user_data_t *user_data)
{
	read_func_t *rf;
	int status;

	rf = malloc (sizeof (*rf));
	if (rf == NULL)
	{
		ERROR ("plugin_register_complex_read: malloc failed.");
		return (ENOMEM);
	}

	memset (rf, 0, sizeof (read_func_t));
	rf->rf_callback = (void *) callback;
	if (group != NULL)
		sstrncpy (rf->rf_group, group, sizeof (rf->rf_group));
	else
		rf->rf_group[0] = '\0';
	sstrncpy (rf->rf_name, name, sizeof (rf->rf_name));
	rf->rf_type = RF_COMPLEX;
	if (interval != NULL)
	{
		rf->rf_interval = *interval;
	}
	rf->rf_effective_interval = rf->rf_interval;

	/* Set user data */
	if (user_data == NULL)
	{
		rf->rf_udata.data = NULL;
		rf->rf_udata.free_func = NULL;
	}
	else
	{
		rf->rf_udata = *user_data;
	}

	status = plugin_insert_read (rf);
	if (status != 0)
		sfree (rf);

	return (status);
} /* int plugin_register_complex_read */

int plugin_register_write (const char *name,
		plugin_write_cb callback, user_data_t *ud)
{
	return (create_register_callback (&list_write, name,
				(void *) callback, ud));
} /* int plugin_register_write */

int plugin_register_flush (const char *name,
		plugin_flush_cb callback, user_data_t *ud)
{
	return (create_register_callback (&list_flush, name,
				(void *) callback, ud));
} /* int plugin_register_flush */

int plugin_register_missing (const char *name,
		plugin_missing_cb callback, user_data_t *ud)
{
	return (create_register_callback (&list_missing, name,
				(void *) callback, ud));
} /* int plugin_register_missing */

int plugin_register_shutdown (const char *name,
		int (*callback) (void))
{
	return (create_register_callback (&list_shutdown, name,
				(void *) callback, /* user_data = */ NULL));
} /* int plugin_register_shutdown */

int plugin_register_data_set (const data_set_t *ds)
{
	data_set_t *ds_copy;
	int i;

	if ((data_sets != NULL)
			&& (c_avl_get (data_sets, ds->type, NULL) == 0))
	{
		NOTICE ("Replacing DS `%s' with another version.", ds->type);
		plugin_unregister_data_set (ds->type);
	}
	else if (data_sets == NULL)
	{
		data_sets = c_avl_create ((int (*) (const void *, const void *)) strcmp);
		if (data_sets == NULL)
			return (-1);
	}

	ds_copy = (data_set_t *) malloc (sizeof (data_set_t));
	if (ds_copy == NULL)
		return (-1);
	memcpy(ds_copy, ds, sizeof (data_set_t));

	ds_copy->ds = (data_source_t *) malloc (sizeof (data_source_t)
			* ds->ds_num);
	if (ds_copy->ds == NULL)
	{
		free (ds_copy);
		return (-1);
	}

	for (i = 0; i < ds->ds_num; i++)
		memcpy (ds_copy->ds + i, ds->ds + i, sizeof (data_source_t));

	return (c_avl_insert (data_sets, (void *) ds_copy->type, (void *) ds_copy));
} /* int plugin_register_data_set */

int plugin_register_log (const char *name,
		plugin_log_cb callback, user_data_t *ud)
{
	return (create_register_callback (&list_log, name,
				(void *) callback, ud));
} /* int plugin_register_log */

int plugin_register_notification (const char *name,
		plugin_notification_cb callback, user_data_t *ud)
{
	return (create_register_callback (&list_notification, name,
				(void *) callback, ud));
} /* int plugin_register_log */

int plugin_unregister_config (const char *name)
{
	cf_unregister (name);
	return (0);
} /* int plugin_unregister_config */

int plugin_unregister_complex_config (const char *name)
{
	cf_unregister_complex (name);
	return (0);
} /* int plugin_unregister_complex_config */

int plugin_unregister_init (const char *name)
{
	return (plugin_unregister (list_init, name));
}

int plugin_unregister_read (const char *name) /* {{{ */
{
	llentry_t *le;
	read_func_t *rf;

	if (name == NULL)
		return (-ENOENT);

	pthread_mutex_lock (&read_lock);

	if (read_list == NULL)
	{
		pthread_mutex_unlock (&read_lock);
		return (-ENOENT);
	}

	le = llist_search (read_list, name);
	if (le == NULL)
	{
		pthread_mutex_unlock (&read_lock);
		WARNING ("plugin_unregister_read: No such read function: %s",
				name);
		return (-ENOENT);
	}

	llist_remove (read_list, le);

	rf = le->value;
	assert (rf != NULL);
	rf->rf_type = RF_REMOVE;

	pthread_mutex_unlock (&read_lock);

	llentry_destroy (le);

	DEBUG ("plugin_unregister_read: Marked `%s' for removal.", name);

	return (0);
} /* }}} int plugin_unregister_read */

static int compare_read_func_group (llentry_t *e, void *ud) /* {{{ */
{
	read_func_t *rf    = e->value;
	char        *group = ud;

	return strcmp (rf->rf_group, (const char *)group);
} /* }}} int compare_read_func_group */

int plugin_unregister_read_group (const char *group) /* {{{ */
{
	llentry_t *le;
	read_func_t *rf;

	int found = 0;

	if (group == NULL)
		return (-ENOENT);

	pthread_mutex_lock (&read_lock);

	if (read_list == NULL)
	{
		pthread_mutex_unlock (&read_lock);
		return (-ENOENT);
	}

	while (42)
	{
		le = llist_search_custom (read_list,
				compare_read_func_group, (void *)group);

		if (le == NULL)
			break;

		++found;

		llist_remove (read_list, le);

		rf = le->value;
		assert (rf != NULL);
		rf->rf_type = RF_REMOVE;

		llentry_destroy (le);

		DEBUG ("plugin_unregister_read_group: "
				"Marked `%s' (group `%s') for removal.",
				rf->rf_name, group);
	}

	pthread_mutex_unlock (&read_lock);

	if (found == 0)
	{
		WARNING ("plugin_unregister_read_group: No such "
				"group of read function: %s", group);
		return (-ENOENT);
	}

	return (0);
} /* }}} int plugin_unregister_read_group */

int plugin_unregister_write (const char *name)
{
	return (plugin_unregister (list_write, name));
}

int plugin_unregister_flush (const char *name)
{
	return (plugin_unregister (list_flush, name));
}

int plugin_unregister_missing (const char *name)
{
	return (plugin_unregister (list_missing, name));
}

int plugin_unregister_shutdown (const char *name)
{
	return (plugin_unregister (list_shutdown, name));
}

int plugin_unregister_data_set (const char *name)
{
	data_set_t *ds;

	if (data_sets == NULL)
		return (-1);

	if (c_avl_remove (data_sets, name, NULL, (void *) &ds) != 0)
		return (-1);

	sfree (ds->ds);
	sfree (ds);

	return (0);
} /* int plugin_unregister_data_set */

int plugin_unregister_log (const char *name)
{
	return (plugin_unregister (list_log, name));
}

int plugin_unregister_notification (const char *name)
{
	return (plugin_unregister (list_notification, name));
}

void plugin_init_all (void)
{
	const char *chain_name;
	llentry_t *le;
	int status;

	/* Init the value cache */
	uc_init ();

	chain_name = global_option_get ("PreCacheChain");
	pre_cache_chain = fc_chain_get_by_name (chain_name);

	chain_name = global_option_get ("PostCacheChain");
	post_cache_chain = fc_chain_get_by_name (chain_name);


	if ((list_init == NULL) && (read_heap == NULL))
		return;

	/* Calling all init callbacks before checking if read callbacks
	 * are available allows the init callbacks to register the read
	 * callback. */
	le = llist_head (list_init);
	while (le != NULL)
	{
		callback_func_t *cf;
		plugin_init_cb callback;

		cf = le->value;
		callback = cf->cf_callback;
		status = (*callback) ();

		if (status != 0)
		{
			ERROR ("Initialization of plugin `%s' "
					"failed with status %i. "
					"Plugin will be unloaded.",
					le->key, status);
			/* Plugins that register read callbacks from the init
			 * callback should take care of appropriate error
			 * handling themselves. */
			/* FIXME: Unload _all_ functions */
			plugin_unregister_read (le->key);
		}

		le = le->next;
	}

	/* Start read-threads */
	if (read_heap != NULL)
	{
		const char *rt;
		int num;
		rt = global_option_get ("ReadThreads");
		num = atoi (rt);
		if (num != -1)
			start_read_threads ((num > 0) ? num : 5);
	}
} /* void plugin_init_all */

/* TODO: Rename this function. */
void plugin_read_all (void)
{
	uc_check_timeout ();

	return;
} /* void plugin_read_all */

/* Read function called when the `-T' command line argument is given. */
int plugin_read_all_once (void)
{
	int status;
	int return_status = 0;

	if (read_heap == NULL)
	{
		NOTICE ("No read-functions are registered.");
		return (0);
	}

	while (42)
	{
		read_func_t *rf;

		rf = c_heap_get_root (read_heap);
		if (rf == NULL)
			break;

		if (rf->rf_type == RF_SIMPLE)
		{
			int (*callback) (void);

			callback = rf->rf_callback;
			status = (*callback) ();
		}
		else
		{
			plugin_read_cb callback;

			callback = rf->rf_callback;
			status = (*callback) (&rf->rf_udata);
		}

		if (status != 0)
		{
			NOTICE ("read-function of plugin `%s' failed.",
					rf->rf_name);
			return_status = -1;
		}

		destroy_callback ((void *) rf);
	}

	return (return_status);
} /* int plugin_read_all_once */

int plugin_write (const char *plugin, /* {{{ */
		const data_set_t *ds, const value_list_t *vl)
{
  llentry_t *le;
  int status;

  if (vl == NULL)
    return (EINVAL);

  if (list_write == NULL)
    return (ENOENT);

  if (ds == NULL)
  {
    ds = plugin_get_ds (vl->type);
    if (ds == NULL)
    {
      ERROR ("plugin_write: Unable to lookup type `%s'.", vl->type);
      return (ENOENT);
    }
  }

  if (plugin == NULL)
  {
    int success = 0;
    int failure = 0;

    le = llist_head (list_write);
    while (le != NULL)
    {
      callback_func_t *cf = le->value;
      plugin_write_cb callback;

      DEBUG ("plugin: plugin_write: Writing values via %s.", le->key);
      callback = cf->cf_callback;
      status = (*callback) (ds, vl, &cf->cf_udata);
      if (status != 0)
        failure++;
      else
        success++;

      le = le->next;
    }

    if ((success == 0) && (failure != 0))
      status = -1;
    else
      status = 0;
  }
  else /* plugin != NULL */
  {
    callback_func_t *cf;
    plugin_write_cb callback;

    le = llist_head (list_write);
    while (le != NULL)
    {
      if (strcasecmp (plugin, le->key) == 0)
        break;

      le = le->next;
    }

    if (le == NULL)
      return (ENOENT);

    cf = le->value;

    DEBUG ("plugin: plugin_write: Writing values via %s.", le->key);
    callback = cf->cf_callback;
    status = (*callback) (ds, vl, &cf->cf_udata);
  }

  return (status);
} /* }}} int plugin_write */

int plugin_flush (const char *plugin, cdtime_t timeout, const char *identifier)
{
  llentry_t *le;

  if (list_flush == NULL)
    return (0);

  le = llist_head (list_flush);
  while (le != NULL)
  {
    callback_func_t *cf;
    plugin_flush_cb callback;

    if ((plugin != NULL)
        && (strcmp (plugin, le->key) != 0))
    {
      le = le->next;
      continue;
    }

    cf = le->value;
    callback = cf->cf_callback;

    (*callback) (timeout, identifier, &cf->cf_udata);

    le = le->next;
  }
  return (0);
} /* int plugin_flush */

void plugin_shutdown_all (void)
{
	llentry_t *le;

	stop_read_threads ();

	destroy_all_callbacks (&list_init);

	pthread_mutex_lock (&read_lock);
	llist_destroy (read_list);
	read_list = NULL;
	pthread_mutex_unlock (&read_lock);

	destroy_read_heap ();

	plugin_flush (/* plugin = */ NULL,
			/* timeout = */ 0,
			/* identifier = */ NULL);

	le = NULL;
	if (list_shutdown != NULL)
		le = llist_head (list_shutdown);

	while (le != NULL)
	{
		callback_func_t *cf;
		plugin_shutdown_cb callback;

		cf = le->value;
		callback = cf->cf_callback;

		/* Advance the pointer before calling the callback allows
		 * shutdown functions to unregister themselves. If done the
		 * other way around the memory `le' points to will be freed
		 * after callback returns. */
		le = le->next;

		(*callback) ();
	}

	/* Write plugins which use the `user_data' pointer usually need the
	 * same data available to the flush callback. If this is the case, set
	 * the free_function to NULL when registering the flush callback and to
	 * the real free function when registering the write callback. This way
	 * the data isn't freed twice. */
	destroy_all_callbacks (&list_flush);
	destroy_all_callbacks (&list_missing);
	destroy_all_callbacks (&list_write);

	destroy_all_callbacks (&list_notification);
	destroy_all_callbacks (&list_shutdown);
	destroy_all_callbacks (&list_log);
} /* void plugin_shutdown_all */

int plugin_dispatch_missing (const value_list_t *vl) /* {{{ */
{
  llentry_t *le;

  if (list_missing == NULL)
    return (0);

  le = llist_head (list_missing);
  while (le != NULL)
  {
    callback_func_t *cf;
    plugin_missing_cb callback;
    int status;

    cf = le->value;
    callback = cf->cf_callback;

    status = (*callback) (vl, &cf->cf_udata);
    if (status != 0)
    {
      if (status < 0)
      {
        ERROR ("plugin_dispatch_missing: Callback function \"%s\" "
            "failed with status %i.",
            le->key, status);
        return (status);
      }
      else
      {
        return (0);
      }
    }

    le = le->next;
  }
  return (0);
} /* int }}} plugin_dispatch_missing */

int plugin_dispatch_values (value_list_t *vl)
{
	int status;
	static c_complain_t no_write_complaint = C_COMPLAIN_INIT_STATIC;

	value_t *saved_values;
	int      saved_values_len;

	data_set_t *ds;

	int free_meta_data = 0;

	if ((vl == NULL) || (vl->type[0] == 0)
			|| (vl->values == NULL) || (vl->values_len < 1))
	{
		ERROR ("plugin_dispatch_values: Invalid value list "
				"from plugin %s.", vl->plugin);
		return (-1);
	}

	/* Free meta data only if the calling function didn't specify any. In
	 * this case matches and targets may add some and the calling function
	 * may not expect (and therefore free) that data. */
	if (vl->meta == NULL)
		free_meta_data = 1;

	if (list_write == NULL)
		c_complain_once (LOG_WARNING, &no_write_complaint,
				"plugin_dispatch_values: No write callback has been "
				"registered. Please load at least one output plugin, "
				"if you want the collected data to be stored.");

	if (data_sets == NULL)
	{
		ERROR ("plugin_dispatch_values: No data sets registered. "
				"Could the types database be read? Check "
				"your `TypesDB' setting!");
		return (-1);
	}

	if (c_avl_get (data_sets, vl->type, (void *) &ds) != 0)
	{
		char ident[6 * DATA_MAX_NAME_LEN];

		FORMAT_VL (ident, sizeof (ident), vl);
		INFO ("plugin_dispatch_values: Dataset not found: %s "
				"(from \"%s\"), check your types.db!",
				vl->type, ident);
		return (-1);
	}

	if (vl->time == 0)
		vl->time = cdtime ();

	if (vl->interval <= 0)
		vl->interval = interval_g;

	DEBUG ("plugin_dispatch_values: time = %.3f; interval = %.3f; "
			"host = %s; "
			"plugin = %s; plugin_instance = %s; "
			"type = %s; type_instance = %s;",
			CDTIME_T_TO_DOUBLE (vl->time),
			CDTIME_T_TO_DOUBLE (vl->interval),
			vl->host,
			vl->plugin, vl->plugin_instance,
			vl->type, vl->type_instance);

#if COLLECT_DEBUG
	assert (0 == strcmp (ds->type, vl->type));
#else
	if (0 != strcmp (ds->type, vl->type))
		WARNING ("plugin_dispatch_values: (ds->type = %s) != (vl->type = %s)",
				ds->type, vl->type);
#endif

#if COLLECT_DEBUG
	assert (ds->ds_num == vl->values_len);
#else
	if (ds->ds_num != vl->values_len)
	{
		ERROR ("plugin_dispatch_values: ds->type = %s: "
				"(ds->ds_num = %i) != "
				"(vl->values_len = %i)",
				ds->type, ds->ds_num, vl->values_len);
		return (-1);
	}
#endif

	escape_slashes (vl->host, sizeof (vl->host));
	escape_slashes (vl->plugin, sizeof (vl->plugin));
	escape_slashes (vl->plugin_instance, sizeof (vl->plugin_instance));
	escape_slashes (vl->type, sizeof (vl->type));
	escape_slashes (vl->type_instance, sizeof (vl->type_instance));

	/* Copy the values. This way, we can assure `targets' that they get
	 * dynamically allocated values, which they can free and replace if
	 * they like. */
	if ((pre_cache_chain != NULL) || (post_cache_chain != NULL))
	{
		saved_values     = vl->values;
		saved_values_len = vl->values_len;

		vl->values = (value_t *) calloc (vl->values_len,
				sizeof (*vl->values));
		if (vl->values == NULL)
		{
			ERROR ("plugin_dispatch_values: calloc failed.");
			vl->values = saved_values;
			return (-1);
		}
		memcpy (vl->values, saved_values,
				vl->values_len * sizeof (*vl->values));
	}
	else /* if ((pre == NULL) && (post == NULL)) */
	{
		saved_values     = NULL;
		saved_values_len = 0;
	}

	if (pre_cache_chain != NULL)
	{
		status = fc_process_chain (ds, vl, pre_cache_chain);
		if (status < 0)
		{
			WARNING ("plugin_dispatch_values: Running the "
					"pre-cache chain failed with "
					"status %i (%#x).",
					status, status);
		}
		else if (status == FC_TARGET_STOP)
		{
			/* Restore the state of the value_list so that plugins
			 * don't get confused.. */
			if (saved_values != NULL)
			{
				free (vl->values);
				vl->values     = saved_values;
				vl->values_len = saved_values_len;
			}
			return (0);
		}
	}

	/* Update the value cache */
	uc_update (ds, vl);

	if (post_cache_chain != NULL)
	{
		status = fc_process_chain (ds, vl, post_cache_chain);
		if (status < 0)
		{
			WARNING ("plugin_dispatch_values: Running the "
					"post-cache chain failed with "
					"status %i (%#x).",
					status, status);
		}
	}
	else
		fc_default_action (ds, vl);

	/* Restore the state of the value_list so that plugins don't get
	 * confused.. */
	if (saved_values != NULL)
	{
		free (vl->values);
		vl->values     = saved_values;
		vl->values_len = saved_values_len;
	}

	if ((free_meta_data != 0) && (vl->meta != NULL))
	{
		meta_data_destroy (vl->meta);
		vl->meta = NULL;
	}

	return (0);
} /* int plugin_dispatch_values */

int plugin_dispatch_values_secure (const value_list_t *vl)
{
  value_list_t vl_copy;
  int status;

  if (vl == NULL)
    return EINVAL;

  memcpy (&vl_copy, vl, sizeof (vl_copy));

  /* Write callbacks must not change the values and meta pointers, so we can
   * savely skip copying those and make this more efficient. */
  if ((pre_cache_chain == NULL) && (post_cache_chain == NULL))
    return (plugin_dispatch_values (&vl_copy));

  /* Set pointers to NULL, just to be on the save side. */
  vl_copy.values = NULL;
  vl_copy.meta = NULL;

  vl_copy.values = malloc (sizeof (*vl_copy.values) * vl->values_len);
  if (vl_copy.values == NULL)
  {
    ERROR ("plugin_dispatch_values_secure: malloc failed.");
    return (ENOMEM);
  }
  memcpy (vl_copy.values, vl->values, sizeof (*vl_copy.values) * vl->values_len);

  if (vl->meta != NULL)
  {
    vl_copy.meta = meta_data_clone (vl->meta);
    if (vl_copy.meta == NULL)
    {
      ERROR ("plugin_dispatch_values_secure: meta_data_clone failed.");
      free (vl_copy.values);
      return (ENOMEM);
    }
  } /* if (vl->meta) */

  status = plugin_dispatch_values (&vl_copy);

  meta_data_destroy (vl_copy.meta);
  free (vl_copy.values);

  return (status);
} /* int plugin_dispatch_values_secure */

int plugin_dispatch_notification (const notification_t *notif)
{
	llentry_t *le;
	/* Possible TODO: Add flap detection here */

	DEBUG ("plugin_dispatch_notification: severity = %i; message = %s; "
			"time = %.3f; host = %s;",
			notif->severity, notif->message,
			CDTIME_T_TO_DOUBLE (notif->time), notif->host);

	/* Nobody cares for notifications */
	if (list_notification == NULL)
		return (-1);

	le = llist_head (list_notification);
	while (le != NULL)
	{
		callback_func_t *cf;
		plugin_notification_cb callback;
		int status;

		cf = le->value;
		callback = cf->cf_callback;
		status = (*callback) (notif, &cf->cf_udata);
		if (status != 0)
		{
			WARNING ("plugin_dispatch_notification: Notification "
					"callback %s returned %i.",
					le->key, status);
		}

		le = le->next;
	}

	return (0);
} /* int plugin_dispatch_notification */

void plugin_log (int level, const char *format, ...)
{
	char msg[1024];
	va_list ap;
	llentry_t *le;

#if !COLLECT_DEBUG
	if (level >= LOG_DEBUG)
		return;
#endif

	va_start (ap, format);
	vsnprintf (msg, sizeof (msg), format, ap);
	msg[sizeof (msg) - 1] = '\0';
	va_end (ap);

	if (list_log == NULL)
	{
		fprintf (stderr, "%s\n", msg);
		return;
	}

	le = llist_head (list_log);
	while (le != NULL)
	{
		callback_func_t *cf;
		plugin_log_cb callback;

		cf = le->value;
		callback = cf->cf_callback;

		(*callback) (level, msg, &cf->cf_udata);

		le = le->next;
	}
} /* void plugin_log */

int parse_log_severity (const char *severity)
{
	int log_level = -1;

	if ((0 == strcasecmp (severity, "emerg"))
			|| (0 == strcasecmp (severity, "alert"))
			|| (0 == strcasecmp (severity, "crit"))
			|| (0 == strcasecmp (severity, "err")))
		log_level = LOG_ERR;
	else if (0 == strcasecmp (severity, "warning"))
		log_level = LOG_WARNING;
	else if (0 == strcasecmp (severity, "notice"))
		log_level = LOG_NOTICE;
	else if (0 == strcasecmp (severity, "info"))
		log_level = LOG_INFO;
#if COLLECT_DEBUG
	else if (0 == strcasecmp (severity, "debug"))
		log_level = LOG_DEBUG;
#endif /* COLLECT_DEBUG */

	return (log_level);
} /* int parse_log_severity */

int parse_notif_severity (const char *severity)
{
	int notif_severity = -1;

	if (strcasecmp (severity, "FAILURE") == 0)
		notif_severity = NOTIF_FAILURE;
	else if (strcmp (severity, "OKAY") == 0)
		notif_severity = NOTIF_OKAY;
	else if ((strcmp (severity, "WARNING") == 0)
			|| (strcmp (severity, "WARN") == 0))
		notif_severity = NOTIF_WARNING;

	return (notif_severity);
} /* int parse_notif_severity */

const data_set_t *plugin_get_ds (const char *name)
{
	data_set_t *ds;

	if (c_avl_get (data_sets, name, (void *) &ds) != 0)
	{
		DEBUG ("No such dataset registered: %s", name);
		return (NULL);
	}

	return (ds);
} /* data_set_t *plugin_get_ds */

static int plugin_notification_meta_add (notification_t *n,
    const char *name,
    enum notification_meta_type_e type,
    const void *value)
{
  notification_meta_t *meta;
  notification_meta_t *tail;

  if ((n == NULL) || (name == NULL) || (value == NULL))
  {
    ERROR ("plugin_notification_meta_add: A pointer is NULL!");
    return (-1);
  }

  meta = (notification_meta_t *) malloc (sizeof (notification_meta_t));
  if (meta == NULL)
  {
    ERROR ("plugin_notification_meta_add: malloc failed.");
    return (-1);
  }
  memset (meta, 0, sizeof (notification_meta_t));

  sstrncpy (meta->name, name, sizeof (meta->name));
  meta->type = type;

  switch (type)
  {
    case NM_TYPE_STRING:
    {
      meta->nm_value.nm_string = strdup ((const char *) value);
      if (meta->nm_value.nm_string == NULL)
      {
        ERROR ("plugin_notification_meta_add: strdup failed.");
        sfree (meta);
        return (-1);
      }
      break;
    }
    case NM_TYPE_SIGNED_INT:
    {
      meta->nm_value.nm_signed_int = *((int64_t *) value);
      break;
    }
    case NM_TYPE_UNSIGNED_INT:
    {
      meta->nm_value.nm_unsigned_int = *((uint64_t *) value);
      break;
    }
    case NM_TYPE_DOUBLE:
    {
      meta->nm_value.nm_double = *((double *) value);
      break;
    }
    case NM_TYPE_BOOLEAN:
    {
      meta->nm_value.nm_boolean = *((_Bool *) value);
      break;
    }
    default:
    {
      ERROR ("plugin_notification_meta_add: Unknown type: %i", type);
      sfree (meta);
      return (-1);
    }
  } /* switch (type) */

  meta->next = NULL;
  tail = n->meta;
  while ((tail != NULL) && (tail->next != NULL))
    tail = tail->next;

  if (tail == NULL)
    n->meta = meta;
  else
    tail->next = meta;

  return (0);
} /* int plugin_notification_meta_add */

int plugin_notification_meta_add_string (notification_t *n,
    const char *name,
    const char *value)
{
  return (plugin_notification_meta_add (n, name, NM_TYPE_STRING, value));
}

int plugin_notification_meta_add_signed_int (notification_t *n,
    const char *name,
    int64_t value)
{
  return (plugin_notification_meta_add (n, name, NM_TYPE_SIGNED_INT, &value));
}

int plugin_notification_meta_add_unsigned_int (notification_t *n,
    const char *name,
    uint64_t value)
{
  return (plugin_notification_meta_add (n, name, NM_TYPE_UNSIGNED_INT, &value));
}

int plugin_notification_meta_add_double (notification_t *n,
    const char *name,
    double value)
{
  return (plugin_notification_meta_add (n, name, NM_TYPE_DOUBLE, &value));
}

int plugin_notification_meta_add_boolean (notification_t *n,
    const char *name,
    _Bool value)
{
  return (plugin_notification_meta_add (n, name, NM_TYPE_BOOLEAN, &value));
}

int plugin_notification_meta_copy (notification_t *dst,
    const notification_t *src)
{
  notification_meta_t *meta;

  assert (dst != NULL);
  assert (src != NULL);
  assert (dst != src);
  assert ((src->meta == NULL) || (src->meta != dst->meta));

  for (meta = src->meta; meta != NULL; meta = meta->next)
  {
    if (meta->type == NM_TYPE_STRING)
      plugin_notification_meta_add_string (dst, meta->name,
          meta->nm_value.nm_string);
    else if (meta->type == NM_TYPE_SIGNED_INT)
      plugin_notification_meta_add_signed_int (dst, meta->name,
          meta->nm_value.nm_signed_int);
    else if (meta->type == NM_TYPE_UNSIGNED_INT)
      plugin_notification_meta_add_unsigned_int (dst, meta->name,
          meta->nm_value.nm_unsigned_int);
    else if (meta->type == NM_TYPE_DOUBLE)
      plugin_notification_meta_add_double (dst, meta->name,
          meta->nm_value.nm_double);
    else if (meta->type == NM_TYPE_BOOLEAN)
      plugin_notification_meta_add_boolean (dst, meta->name,
          meta->nm_value.nm_boolean);
  }

  return (0);
} /* int plugin_notification_meta_copy */

int plugin_notification_meta_free (notification_meta_t *n)
{
  notification_meta_t *this;
  notification_meta_t *next;

  if (n == NULL)
  {
    ERROR ("plugin_notification_meta_free: n == NULL!");
    return (-1);
  }

  this = n;
  while (this != NULL)
  {
    next = this->next;

    if (this->type == NM_TYPE_STRING)
    {
      free ((char *)this->nm_value.nm_string);
      this->nm_value.nm_string = NULL;
    }
    sfree (this);

    this = next;
  }

  return (0);
} /* int plugin_notification_meta_free */

/* vim: set sw=8 ts=8 noet fdm=marker : */
