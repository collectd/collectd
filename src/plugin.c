/**
 * collectd - src/plugin.c
 * Copyright (C) 2005-2008  Florian octo Forster
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
#include "utils_cache.h"
#include "utils_threshold.h"

/*
 * Private structures
 */
struct read_func_s
{
	int wait_time;
	int wait_left;
	int (*callback) (void);
	enum { DONE = 0, TODO = 1, ACTIVE = 2 } needs_read;
};
typedef struct read_func_s read_func_t;

/*
 * Private variables
 */
static llist_t *list_init;
static llist_t *list_read;
static llist_t *list_write;
static llist_t *list_flush;
static llist_t *list_shutdown;
static llist_t *list_log;
static llist_t *list_notification;

static c_avl_tree_t *data_sets;

static char *plugindir = NULL;

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

static int register_callback (llist_t **list, const char *name, void *callback)
{
	llentry_t *le;
	char *key;

	if ((*list == NULL)
			&& ((*list = llist_create ()) == NULL))
		return (-1);

	le = llist_search (*list, name);
	if (le == NULL)
	{
		key = strdup (name);
		if (key == NULL)
			return (-1);

		le = llentry_create (key, callback);
		if (le == NULL)
		{
			free (key);
			return (-1);
		}

		llist_append (*list, le);
	}
	else
	{
		le->value = callback;
	}

	return (0);
} /* int register_callback */

static int plugin_unregister (llist_t *list, const char *name)
{
	llentry_t *e;

	e = llist_search (list, name);

	if (e == NULL)
		return (-1);

	llist_remove (list, e);
	free (e->key);
	llentry_destroy (e);

	return (0);
} /* int plugin_unregister */

/*
 * (Try to) load the shared object `file'. Won't complain if it isn't a shared
 * object, but it will bitch about a shared object not having a
 * ``module_register'' symbol..
 */
static int plugin_load_file (char *file)
{
	lt_dlhandle dlh;
	void (*reg_handle) (void);

	DEBUG ("file = %s", file);

	lt_dlinit ();
	lt_dlerror (); /* clear errors */

	if ((dlh = lt_dlopen (file)) == NULL)
	{
		const char *error = lt_dlerror ();

		ERROR ("lt_dlopen failed: %s", error);
		fprintf (stderr, "lt_dlopen failed: %s\n", error);
		return (1);
	}

	if ((reg_handle = (void (*) (void)) lt_dlsym (dlh, "module_register")) == NULL)
	{
		WARNING ("Couldn't find symbol ``module_register'' in ``%s'': %s\n",
				file, lt_dlerror ());
		lt_dlclose (dlh);
		return (-1);
	}

	(*reg_handle) ();

	return (0);
}

static void *plugin_read_thread (void *args)
{
	llentry_t   *le;
	read_func_t *rf;
	int          status;
	int          done;

	pthread_mutex_lock (&read_lock);

	while (read_loop != 0)
	{
		le = llist_head (list_read);
		done = 0;

		while ((read_loop != 0) && (le != NULL))
		{
			rf = (read_func_t *) le->value;

			if (rf->needs_read != TODO)
			{
				le = le->next;
				continue;
			}

			/* We will do this read function */
			rf->needs_read = ACTIVE;

			DEBUG ("[thread #%5lu] plugin: plugin_read_thread: Handling %s",
					(unsigned long int) pthread_self (), le->key);
			pthread_mutex_unlock (&read_lock);

			status = rf->callback ();
			done++;

			if (status != 0)
			{
				if (rf->wait_time < interval_g)
					rf->wait_time = interval_g;
				rf->wait_left = rf->wait_time;
				rf->wait_time = rf->wait_time * 2;
				if (rf->wait_time > 86400)
					rf->wait_time = 86400;

				NOTICE ("read-function of plugin `%s' "
						"failed. Will suspend it for %i "
						"seconds.", le->key, rf->wait_left);
			}
			else
			{
				rf->wait_left = 0;
				rf->wait_time = interval_g;
			}

			pthread_mutex_lock (&read_lock);

			rf->needs_read = DONE;
			le = le->next;
		} /* while (le != NULL) */

		if ((read_loop != 0) && (done == 0))
		{
			DEBUG ("[thread #%5lu] plugin: plugin_read_thread: Waiting on read_cond.",
					(unsigned long int) pthread_self ());
			pthread_cond_wait (&read_cond, &read_lock);
		}
	} /* while (read_loop) */

	pthread_mutex_unlock (&read_lock);

	pthread_exit (NULL);
	return ((void *) 0);
} /* void *plugin_read_thread */

static void start_threads (int num)
{
	int i;

	if (read_threads != NULL)
		return;

	read_threads = (pthread_t *) calloc (num, sizeof (pthread_t));
	if (read_threads == NULL)
	{
		ERROR ("plugin: start_threads: calloc failed.");
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
			ERROR ("plugin: start_threads: pthread_create failed.");
			return;
		}
	} /* for (i) */
} /* void start_threads */

static void stop_threads (void)
{
	int i;

	pthread_mutex_lock (&read_lock);
	read_loop = 0;
	DEBUG ("plugin: stop_threads: Signalling `read_cond'");
	pthread_cond_broadcast (&read_cond);
	pthread_mutex_unlock (&read_lock);

	for (i = 0; i < read_threads_num; i++)
	{
		if (pthread_join (read_threads[i], NULL) != 0)
		{
			ERROR ("plugin: stop_threads: pthread_join failed.");
		}
		read_threads[i] = (pthread_t) 0;
	}
	sfree (read_threads);
	read_threads_num = 0;
} /* void stop_threads */

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
int plugin_load (const char *type)
{
	DIR  *dh;
	const char *dir;
	char  filename[BUFSIZE] = "";
	char  typename[BUFSIZE];
	int   typename_len;
	int   ret;
	struct stat    statbuf;
	struct dirent *de;

	DEBUG ("type = %s", type);

	dir = plugin_get_dir ();
	ret = 1;

	/* `cpu' should not match `cpufreq'. To solve this we add `.so' to the
	 * type when matching the filename */
	if (ssnprintf (typename, sizeof (typename),
			"%s.so", type) >= sizeof (typename))
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

		if (ssnprintf (filename, sizeof (filename),
				"%s/%s", dir, de->d_name) >= sizeof (filename))
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
			continue;
		}

		if (plugin_load_file (filename) == 0)
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
	return (register_callback (&list_init, name, (void *) callback));
} /* plugin_register_init */

int plugin_register_read (const char *name,
		int (*callback) (void))
{
	read_func_t *rf;

	rf = (read_func_t *) malloc (sizeof (read_func_t));
	if (rf == NULL)
	{
		char errbuf[1024];
		ERROR ("plugin_register_read: malloc failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	memset (rf, '\0', sizeof (read_func_t));
	rf->wait_time = interval_g;
	rf->wait_left = 0;
	rf->callback = callback;
	rf->needs_read = DONE;

	return (register_callback (&list_read, name, (void *) rf));
} /* int plugin_register_read */

int plugin_register_write (const char *name,
		int (*callback) (const data_set_t *ds, const value_list_t *vl))
{
	return (register_callback (&list_write, name, (void *) callback));
} /* int plugin_register_write */

int plugin_register_flush (const char *name,
		int (*callback) (const int timeout, const char *identifier))
{
	return (register_callback (&list_flush, name, (void *) callback));
} /* int plugin_register_flush */

int plugin_register_shutdown (char *name,
		int (*callback) (void))
{
	return (register_callback (&list_shutdown, name, (void *) callback));
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

int plugin_register_log (char *name,
		void (*callback) (int priority, const char *msg))
{
	return (register_callback (&list_log, name, (void *) callback));
} /* int plugin_register_log */

int plugin_register_notification (const char *name,
		int (*callback) (const notification_t *notif))
{
	return (register_callback (&list_notification, name, (void *) callback));
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

int plugin_unregister_read (const char *name)
{
	llentry_t *e;

	e = llist_search (list_read, name);

	if (e == NULL)
		return (-1);

	llist_remove (list_read, e);
	free (e->value);
	free (e->key);
	llentry_destroy (e);

	return (0);
}

int plugin_unregister_write (const char *name)
{
	return (plugin_unregister (list_write, name));
}

int plugin_unregister_flush (const char *name)
{
	return (plugin_unregister (list_flush, name));
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
	int (*callback) (void);
	llentry_t *le;
	int status;

	/* Init the value cache */
	uc_init ();

	if ((list_init == NULL) && (list_read == NULL))
		return;

	/* Calling all init callbacks before checking if read callbacks
	 * are available allows the init callbacks to register the read
	 * callback. */
	le = llist_head (list_init);
	while (le != NULL)
	{
		callback = (int (*) (void)) le->value;
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
	if (list_read != NULL)
	{
		const char *rt;
		int num;
		rt = global_option_get ("ReadThreads");
		num = atoi (rt);
		start_threads ((num > 0) ? num : 5);
	}
} /* void plugin_init_all */

void plugin_read_all (void)
{
	llentry_t   *le;
	read_func_t *rf;

	uc_check_timeout ();

	if (list_read == NULL)
		return;

	pthread_mutex_lock (&read_lock);

	le = llist_head (list_read);
	while (le != NULL)
	{
		rf = (read_func_t *) le->value;

		if (rf->needs_read != DONE)
		{
			le = le->next;
			continue;
		}

		if (rf->wait_left > 0)
			rf->wait_left -= interval_g;

		if (rf->wait_left <= 0)
		{
			rf->needs_read = TODO;
		}

		le = le->next;
	}

	DEBUG ("plugin: plugin_read_all: Signalling `read_cond'");
	pthread_cond_broadcast (&read_cond);
	pthread_mutex_unlock (&read_lock);
} /* void plugin_read_all */

int plugin_flush (const char *plugin, int timeout, const char *identifier)
{
  int (*callback) (int timeout, const char *identifier);
  llentry_t *le;

  if (list_flush == NULL)
    return (0);

  le = llist_head (list_flush);
  while (le != NULL)
  {
    if ((plugin != NULL)
        && (strcmp (plugin, le->key) != 0))
    {
      le = le->next;
      continue;
    }

    callback = (int (*) (int, const char *)) le->value;
    (*callback) (timeout, identifier);

    le = le->next;
  }
  return (0);
} /* int plugin_flush */

void plugin_shutdown_all (void)
{
	int (*callback) (void);
	llentry_t *le;

	stop_threads ();

	if (list_shutdown == NULL)
		return;

	le = llist_head (list_shutdown);
	while (le != NULL)
	{
		callback = (int (*) (void)) le->value;

		/* Advance the pointer before calling the callback allows
		 * shutdown functions to unregister themselves. If done the
		 * other way around the memory `le' points to will be freed
		 * after callback returns. */
		le = le->next;

		(*callback) ();
	}
} /* void plugin_shutdown_all */

int plugin_dispatch_values (value_list_t *vl)
{
	static c_complain_t no_write_complaint = C_COMPLAIN_INIT;

	int (*callback) (const data_set_t *, const value_list_t *);
	data_set_t *ds;
	llentry_t *le;

	if ((vl == NULL) || (*vl->type == '\0')) {
		ERROR ("plugin_dispatch_values: Invalid value list.");
		return (-1);
	}

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
		INFO ("plugin_dispatch_values: Dataset not found: %s", vl->type);
		return (-1);
	}

	DEBUG ("plugin_dispatch_values: time = %u; interval = %i; "
			"host = %s; "
			"plugin = %s; plugin_instance = %s; "
			"type = %s; type_instance = %s;",
			(unsigned int) vl->time, vl->interval,
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

	/* Update the value cache */
	uc_update (ds, vl);
	ut_check_threshold (ds, vl);

	le = llist_head (list_write);
	while (le != NULL)
	{
		callback = (int (*) (const data_set_t *, const value_list_t *)) le->value;
		(*callback) (ds, vl);

		le = le->next;
	}

	return (0);
} /* int plugin_dispatch_values */

int plugin_dispatch_notification (const notification_t *notif)
{
	int (*callback) (const notification_t *);
	llentry_t *le;
	/* Possible TODO: Add flap detection here */

	DEBUG ("plugin_dispatch_notification: severity = %i; message = %s; "
			"time = %u; host = %s;",
			notif->severity, notif->message,
			(unsigned int) notif->time, notif->host);

	/* Nobody cares for notifications */
	if (list_notification == NULL)
		return (-1);

	le = llist_head (list_notification);
	while (le != NULL)
	{
		callback = (int (*) (const notification_t *)) le->value;
		(*callback) (notif);

		le = le->next;
	}

	return (0);
} /* int plugin_dispatch_notification */

void plugin_log (int level, const char *format, ...)
{
	char msg[512];
	va_list ap;

	void (*callback) (int, const char *);
	llentry_t *le;

	if (list_log == NULL)
		return;

#if !COLLECT_DEBUG
	if (level >= LOG_DEBUG)
		return;
#endif

	va_start (ap, format);
	vsnprintf (msg, 512, format, ap);
	msg[511] = '\0';
	va_end (ap);

	le = llist_head (list_log);
	while (le != NULL)
	{
		callback = (void (*) (int, const char *)) le->value;
		(*callback) (level, msg);

		le = le->next;
	}
} /* void plugin_log */

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
      meta->value_string = strdup ((const char *) value);
      if (meta->value_string == NULL)
      {
        ERROR ("plugin_notification_meta_add: strdup failed.");
        sfree (meta);
        return (-1);
      }
      break;
    }
    case NM_TYPE_SIGNED_INT:
    {
      meta->value_signed_int = *((int64_t *) value);
      break;
    }
    case NM_TYPE_UNSIGNED_INT:
    {
      meta->value_unsigned_int = *((uint64_t *) value);
      break;
    }
    case NM_TYPE_DOUBLE:
    {
      meta->value_double = *((double *) value);
      break;
    }
    case NM_TYPE_BOOLEAN:
    {
      meta->value_boolean = *((bool *) value);
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
    bool value)
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
          meta->value_string);
    else if (meta->type == NM_TYPE_SIGNED_INT)
      plugin_notification_meta_add_signed_int (dst, meta->name,
          meta->value_signed_int);
    else if (meta->type == NM_TYPE_UNSIGNED_INT)
      plugin_notification_meta_add_unsigned_int (dst, meta->name,
          meta->value_unsigned_int);
    else if (meta->type == NM_TYPE_DOUBLE)
      plugin_notification_meta_add_double (dst, meta->name,
          meta->value_double);
    else if (meta->type == NM_TYPE_BOOLEAN)
      plugin_notification_meta_add_boolean (dst, meta->name,
          meta->value_boolean);
  }

  return (0);
} /* int plugin_notification_meta_copy */

int plugin_notification_meta_free (notification_t *n)
{
  notification_meta_t *this;
  notification_meta_t *next;

  if (n == NULL)
  {
    ERROR ("plugin_notification_meta_free: n == NULL!");
    return (-1);
  }

  this = n->meta;
  n->meta = NULL;
  while (this != NULL)
  {
    next = this->next;

    if (this->type == NM_TYPE_STRING)
    {
      free ((char *)this->value_string);
      this->value_string = NULL;
    }
    sfree (this);

    this = next;
  }

  return (0);
} /* int plugin_notification_meta_free */
