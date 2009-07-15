/**
 * collectd - src/utils_cache.c
 * Copyright (C) 2007,2008  Florian octo Forster
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
 * Author:
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_avltree.h"
#include "utils_cache.h"
#include "utils_threshold.h"

#include <assert.h>
#include <pthread.h>

typedef struct cache_entry_s
{
	char name[6 * DATA_MAX_NAME_LEN];
	int        values_num;
	gauge_t   *values_gauge;
	counter_t *values_counter;
	/* Time contained in the package
	 * (for calculating rates) */
	time_t last_time;
	/* Time according to the local clock
	 * (for purging old entries) */
	time_t last_update;
	/* Interval in which the data is collected
	 * (for purding old entries) */
	int interval;
	int state;
} cache_entry_t;

static c_avl_tree_t   *cache_tree = NULL;
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

static int cache_compare (const cache_entry_t *a, const cache_entry_t *b)
{
  assert ((a != NULL) && (b != NULL));
  return (strcmp (a->name, b->name));
} /* int cache_compare */

static cache_entry_t *cache_alloc (int values_num)
{
  cache_entry_t *ce;

  ce = (cache_entry_t *) malloc (sizeof (cache_entry_t));
  if (ce == NULL)
  {
    ERROR ("utils_cache: cache_alloc: malloc failed.");
    return (NULL);
  }
  memset (ce, '\0', sizeof (cache_entry_t));
  ce->values_num = values_num;

  ce->values_gauge = (gauge_t *) calloc (values_num, sizeof (gauge_t));
  ce->values_counter = (counter_t *) calloc (values_num, sizeof (counter_t));
  if ((ce->values_gauge == NULL) || (ce->values_counter == NULL))
  {
    sfree (ce->values_gauge);
    sfree (ce->values_counter);
    sfree (ce);
    ERROR ("utils_cache: cache_alloc: calloc failed.");
    return (NULL);
  }

  return (ce);
} /* cache_entry_t *cache_alloc */

static void cache_free (cache_entry_t *ce)
{
  if (ce == NULL)
    return;

  sfree (ce->values_gauge);
  sfree (ce->values_counter);
  sfree (ce);
} /* void cache_free */

static int uc_send_notification (const char *name)
{
  cache_entry_t *ce = NULL;
  int status;

  char *name_copy;
  char *host;
  char *plugin;
  char *plugin_instance;
  char *type;
  char *type_instance;

  notification_t n;

  name_copy = strdup (name);
  if (name_copy == NULL)
  {
    ERROR ("uc_send_notification: strdup failed.");
    return (-1);
  }

  status = parse_identifier (name_copy, &host,
      &plugin, &plugin_instance,
      &type, &type_instance);
  if (status != 0)
  {
    ERROR ("uc_send_notification: Cannot parse name `%s'", name);
    return (-1);
  }

  /* Copy the associative members */
  notification_init (&n, NOTIF_FAILURE, /* host = */ NULL,
      host, plugin, plugin_instance, type, type_instance);

  sfree (name_copy);
  name_copy = host = plugin = plugin_instance = type = type_instance = NULL;

  pthread_mutex_lock (&cache_lock);

  /*
   * Set the time _after_ getting the lock because we don't know how long
   * acquiring the lock takes and we will use this time later to decide
   * whether or not the state is OKAY.
   */
  n.time = time (NULL);

  status = c_avl_get (cache_tree, name, (void *) &ce);
  if (status != 0)
  {
    pthread_mutex_unlock (&cache_lock);
    sfree (name_copy);
    return (-1);
  }
    
  /* Check if the entry has been updated in the meantime */
  if ((n.time - ce->last_update) < (2 * ce->interval))
  {
    ce->state = STATE_OKAY;
    pthread_mutex_unlock (&cache_lock);
    sfree (name_copy);
    return (-1);
  }

  ssnprintf (n.message, sizeof (n.message),
      "%s has not been updated for %i seconds.", name,
      (int) (n.time - ce->last_update));

  pthread_mutex_unlock (&cache_lock);

  plugin_dispatch_notification (&n);

  return (0);
} /* int uc_send_notification */

static int uc_insert (const data_set_t *ds, const value_list_t *vl,
    const char *key)
{
  int i;
  char *key_copy;
  cache_entry_t *ce;

  /* `cache_lock' has been locked by `uc_update' */

  key_copy = strdup (key);
  if (key_copy == NULL)
  {
    ERROR ("uc_insert: strdup failed.");
    return (-1);
  }

  ce = cache_alloc (ds->ds_num);
  if (ce == NULL)
  {
    sfree (key_copy);
    ERROR ("uc_insert: cache_alloc (%i) failed.", ds->ds_num);
    return (-1);
  }

  sstrncpy (ce->name, key, sizeof (ce->name));

  for (i = 0; i < ds->ds_num; i++)
  {
    if (ds->ds[i].type == DS_TYPE_COUNTER)
    {
      ce->values_gauge[i] = NAN;
      ce->values_counter[i] = vl->values[i].counter;
    }
    else /* if (ds->ds[i].type == DS_TYPE_GAUGE) */
    {
      ce->values_gauge[i] = vl->values[i].gauge;
    }
  } /* for (i) */

  ce->last_time = vl->time;
  ce->last_update = time (NULL);
  ce->interval = vl->interval;
  ce->state = STATE_OKAY;

  if (c_avl_insert (cache_tree, key_copy, ce) != 0)
  {
    sfree (key_copy);
    ERROR ("uc_insert: c_avl_insert failed.");
    return (-1);
  }

  DEBUG ("uc_insert: Added %s to the cache.", key);
  return (0);
} /* int uc_insert */

int uc_init (void)
{
  if (cache_tree == NULL)
    cache_tree = c_avl_create ((int (*) (const void *, const void *))
	cache_compare);

  return (0);
} /* int uc_init */

int uc_check_timeout (void)
{
  time_t now;
  cache_entry_t *ce;

  char **keys = NULL;
  int keys_len = 0;

  char *key;
  c_avl_iterator_t *iter;
  int i;
  
  pthread_mutex_lock (&cache_lock);

  now = time (NULL);

  /* Build a list of entries to be flushed */
  iter = c_avl_get_iterator (cache_tree);
  while (c_avl_iterator_next (iter, (void *) &key, (void *) &ce) == 0)
  {
    /* If entry has not been updated, add to `keys' array */
    if ((now - ce->last_update) >= (2 * ce->interval))
    {
      char **tmp;

      tmp = (char **) realloc ((void *) keys,
	  (keys_len + 1) * sizeof (char *));
      if (tmp == NULL)
      {
	ERROR ("uc_check_timeout: realloc failed.");
	c_avl_iterator_destroy (iter);
	sfree (keys);
	pthread_mutex_unlock (&cache_lock);
	return (-1);
      }

      keys = tmp;
      keys[keys_len] = strdup (key);
      if (keys[keys_len] == NULL)
      {
	ERROR ("uc_check_timeout: strdup failed.");
	continue;
      }
      keys_len++;
    }
  } /* while (c_avl_iterator_next) */

  ce = NULL;

  for (i = 0; i < keys_len; i++)
  {
    int status;

    status = ut_check_interesting (keys[i]);

    if (status < 0)
    {
      ERROR ("uc_check_timeout: ut_check_interesting failed.");
      sfree (keys[i]);
      continue;
    }
    else if (status == 0) /* ``service'' is uninteresting */
    {
      DEBUG ("uc_check_timeout: %s is missing but ``uninteresting''",
	  keys[i]);
      status = c_avl_remove (cache_tree, keys[i],
	  (void *) &key, (void *) &ce);
      if (status != 0)
      {
	ERROR ("uc_check_timeout: c_avl_remove (%s) failed.", keys[i]);
      }
      sfree (keys[i]);
      sfree (key);
      cache_free (ce);
      continue;
    }

    /* If we get here, the value is ``interesting''. Query the record from the
     * cache and update the state field. */
    if (c_avl_get (cache_tree, keys[i], (void *) &ce) != 0)
    {
      ERROR ("uc_check_timeout: cannot get data for %s from cache", keys[i]);
      /* Do not free `keys[i]' so a notification is sent further down. */
      continue;
    }
    assert (ce != NULL);

    if (status == 2) /* persist */
    {
      DEBUG ("uc_check_timeout: %s is missing, sending notification.",
	  keys[i]);
      ce->state = STATE_MISSING;
      /* Do not free `keys[i]' so a notification is sent further down. */
    }
    else if (status == 1) /* do not persist */
    {
      if (ce->state == STATE_MISSING)
      {
	DEBUG ("uc_check_timeout: %s is missing but "
	    "notification has already been sent.",
	    keys[i]);
	/* Set `keys[i]' to NULL to no notification is sent. */
	sfree (keys[i]);
      }
      else /* (ce->state != STATE_MISSING) */
      {
	DEBUG ("uc_check_timeout: %s is missing, sending one notification.",
	    keys[i]);
	ce->state = STATE_MISSING;
	/* Do not free `keys[i]' so a notification is sent further down. */
      }
    }
    else
    {
      WARNING ("uc_check_timeout: ut_check_interesting (%s) returned "
	  "invalid status %i.",
	  keys[i], status);
      sfree (keys[i]);
    }

    /* Make really sure the next iteration doesn't work with this pointer.
     * There have been too many bugs in the past.. :/  -- octo */
    ce = NULL;
  } /* for (keys[i]) */

  c_avl_iterator_destroy (iter);

  pthread_mutex_unlock (&cache_lock);

  for (i = 0; i < keys_len; i++)
  {
    if (keys[i] == NULL)
      continue;

    uc_send_notification (keys[i]);
    sfree (keys[i]);
  }

  sfree (keys);

  return (0);
} /* int uc_check_timeout */

int uc_update (const data_set_t *ds, const value_list_t *vl)
{
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int send_okay_notification = 0;
  time_t update_delay = 0;
  notification_t n;
  int status;
  int i;

  if (FORMAT_VL (name, sizeof (name), vl, ds) != 0)
  {
    ERROR ("uc_update: FORMAT_VL failed.");
    return (-1);
  }

  pthread_mutex_lock (&cache_lock);

  status = c_avl_get (cache_tree, name, (void *) &ce);
  if (status != 0) /* entry does not yet exist */
  {
    status = uc_insert (ds, vl, name);
    pthread_mutex_unlock (&cache_lock);
    return (status);
  }

  assert (ce != NULL);
  assert (ce->values_num == ds->ds_num);

  if (ce->last_time >= vl->time)
  {
    pthread_mutex_unlock (&cache_lock);
    NOTICE ("uc_update: Value too old: name = %s; value time = %u; "
	"last cache update = %u;",
	name, (unsigned int) vl->time, (unsigned int) ce->last_time);
    return (-1);
  }

  /* Send a notification (after the lock has been released) if we switch the
   * state from something else to `okay'. */
  if (ce->state == STATE_MISSING)
  {
    send_okay_notification = 1;
    ce->state = STATE_OKAY;
    update_delay = time (NULL) - ce->last_update;
  }

  for (i = 0; i < ds->ds_num; i++)
  {
    if (ds->ds[i].type == DS_TYPE_COUNTER)
    {
      counter_t diff;

      /* check if the counter has wrapped around */
      if (vl->values[i].counter < ce->values_counter[i])
      {
	if (ce->values_counter[i] <= 4294967295U)
	  diff = (4294967295U - ce->values_counter[i])
	    + vl->values[i].counter;
	else
	  diff = (18446744073709551615ULL - ce->values_counter[i])
	    + vl->values[i].counter;
      }
      else /* counter has NOT wrapped around */
      {
	diff = vl->values[i].counter - ce->values_counter[i];
      }

      ce->values_gauge[i] = ((double) diff)
	/ ((double) (vl->time - ce->last_time));
      ce->values_counter[i] = vl->values[i].counter;
    }
    else /* if (ds->ds[i].type == DS_TYPE_GAUGE) */
    {
      ce->values_gauge[i] = vl->values[i].gauge;
    }
    DEBUG ("uc_update: %s: ds[%i] = %lf", name, i, ce->values_gauge[i]);
  } /* for (i) */

  ce->last_time = vl->time;
  ce->last_update = time (NULL);
  ce->interval = vl->interval;

  pthread_mutex_unlock (&cache_lock);

  if (send_okay_notification == 0)
    return (0);

  /* Do not send okay notifications for uninteresting values, i. e. values for
   * which no threshold is configured. */
  status = ut_check_interesting (name);
  if (status <= 0)
    return (0);

  /* Initialize the notification */
  memset (&n, '\0', sizeof (n));
  NOTIFICATION_INIT_VL (&n, vl, ds);

  n.severity = NOTIF_OKAY;
  n.time = vl->time;

  ssnprintf (n.message, sizeof (n.message),
      "Received a value for %s. It was missing for %u seconds.",
      name, (unsigned int) update_delay);

  plugin_dispatch_notification (&n);

  return (0);
} /* int uc_update */

int uc_get_rate_by_name (const char *name, gauge_t **ret_values, size_t *ret_values_num)
{
  gauge_t *ret = NULL;
  size_t ret_num = 0;
  cache_entry_t *ce = NULL;
  int status = 0;

  pthread_mutex_lock (&cache_lock);

  if (c_avl_get (cache_tree, name, (void *) &ce) == 0)
  {
    assert (ce != NULL);

    ret_num = ce->values_num;
    ret = (gauge_t *) malloc (ret_num * sizeof (gauge_t));
    if (ret == NULL)
    {
      ERROR ("utils_cache: uc_get_rate_by_name: malloc failed.");
      status = -1;
    }
    else
    {
      memcpy (ret, ce->values_gauge, ret_num * sizeof (gauge_t));
    }
  }
  else
  {
    DEBUG ("utils_cache: uc_get_rate_by_name: No such value: %s", name);
    status = -1;
  }

  pthread_mutex_unlock (&cache_lock);

  if (status == 0)
  {
    *ret_values = ret;
    *ret_values_num = ret_num;
  }

  return (status);
} /* gauge_t *uc_get_rate_by_name */

gauge_t *uc_get_rate (const data_set_t *ds, const value_list_t *vl)
{
  char name[6 * DATA_MAX_NAME_LEN];
  gauge_t *ret = NULL;
  size_t ret_num = 0;
  int status;

  if (FORMAT_VL (name, sizeof (name), vl, ds) != 0)
  {
    ERROR ("utils_cache: uc_get_rate: FORMAT_VL failed.");
    return (NULL);
  }

  status = uc_get_rate_by_name (name, &ret, &ret_num);
  if (status != 0)
    return (NULL);

  /* This is important - the caller has no other way of knowing how many
   * values are returned. */
  if (ret_num != (size_t) ds->ds_num)
  {
    ERROR ("utils_cache: uc_get_rate: ds[%s] has %i values, "
	"but uc_get_rate_by_name returned %zu.",
	ds->type, ds->ds_num, ret_num);
    sfree (ret);
    return (NULL);
  }

  return (ret);
} /* gauge_t *uc_get_rate */

int uc_get_names (char ***ret_names, time_t **ret_times, size_t *ret_number)
{
  c_avl_iterator_t *iter;
  char *key;
  cache_entry_t *value;

  char **names = NULL;
  time_t *times = NULL;
  size_t number = 0;

  int status = 0;

  if ((ret_names == NULL) || (ret_number == NULL))
    return (-1);

  pthread_mutex_lock (&cache_lock);

  iter = c_avl_get_iterator (cache_tree);
  while (c_avl_iterator_next (iter, (void *) &key, (void *) &value) == 0)
  {
    char **temp;

    if (ret_times != NULL)
    {
      time_t *tmp_times;

      tmp_times = (time_t *) realloc (times, sizeof (time_t) * (number + 1));
      if (tmp_times == NULL)
      {
	status = -1;
	break;
      }
      times = tmp_times;
      times[number] = value->last_time;
    }

    temp = (char **) realloc (names, sizeof (char *) * (number + 1));
    if (temp == NULL)
    {
      status = -1;
      break;
    }
    names = temp;
    names[number] = strdup (key);
    if (names[number] == NULL)
    {
      status = -1;
      break;
    }
    number++;
  } /* while (c_avl_iterator_next) */

  c_avl_iterator_destroy (iter);
  pthread_mutex_unlock (&cache_lock);

  if (status != 0)
  {
    size_t i;
    
    for (i = 0; i < number; i++)
    {
      sfree (names[i]);
    }
    sfree (names);

    return (-1);
  }

  *ret_names = names;
  if (ret_times != NULL)
    *ret_times = times;
  *ret_number = number;

  return (0);
} /* int uc_get_names */

int uc_get_state (const data_set_t *ds, const value_list_t *vl)
{
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int ret = STATE_ERROR;

  if (FORMAT_VL (name, sizeof (name), vl, ds) != 0)
  {
    ERROR ("uc_get_state: FORMAT_VL failed.");
    return (STATE_ERROR);
  }

  pthread_mutex_lock (&cache_lock);

  if (c_avl_get (cache_tree, name, (void *) &ce) == 0)
  {
    assert (ce != NULL);
    ret = ce->state;
  }

  pthread_mutex_unlock (&cache_lock);

  return (ret);
} /* int uc_get_state */

int uc_set_state (const data_set_t *ds, const value_list_t *vl, int state)
{
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int ret = -1;

  if (FORMAT_VL (name, sizeof (name), vl, ds) != 0)
  {
    ERROR ("uc_get_state: FORMAT_VL failed.");
    return (STATE_ERROR);
  }

  pthread_mutex_lock (&cache_lock);

  if (c_avl_get (cache_tree, name, (void *) &ce) == 0)
  {
    assert (ce != NULL);
    ret = ce->state;
    ce->state = state;
  }

  pthread_mutex_unlock (&cache_lock);

  return (ret);
} /* int uc_set_state */
/* vim: set sw=2 ts=8 sts=2 tw=78 : */
