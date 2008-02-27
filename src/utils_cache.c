/**
 * collectd - src/utils_cache.c
 * Copyright (C) 2007  Florian octo Forster
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

  snprintf (n.message, sizeof (n.message),
      "%s has not been updated for %i seconds.", name,
      (int) (n.time - ce->last_update));

  pthread_mutex_unlock (&cache_lock);

  n.message[sizeof (n.message) - 1] = '\0';
  plugin_dispatch_notification (&n);

  return (0);
} /* int uc_send_notification */

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
	ERROR ("uc_purge: realloc failed.");
	c_avl_iterator_destroy (iter);
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

  for (i = 0; i < keys_len; i++)
  {
    int status;

    status = ut_check_interesting (keys[i]);

    if (status < 0)
    {
      ERROR ("uc_check_timeout: ut_check_interesting failed.");
      sfree (keys[i]);
    }
    else if (status == 0) /* ``service'' is uninteresting */
    {
      ce = NULL;
      DEBUG ("uc_check_timeout: %s is missing but ``uninteresting''", keys[i]);
      status = c_avl_remove (cache_tree, keys[i], (void *) &key, (void *) &ce);
      if (status != 0)
      {
	ERROR ("uc_check_timeout: c_avl_remove (%s) failed.", keys[i]);
      }
      sfree (keys[i]);
      cache_free (ce);
    }
    else /* (status > 0); ``service'' is interesting */
    {
      /*
       * `keys[i]' is not freed and set to NULL, so that the for-loop below
       * will send out notifications. There's nothing else to do here.
       */
      DEBUG ("uc_check_timeout: %s is missing and ``interesting''", keys[i]);
      ce->state = STATE_ERROR;
    }
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

  if (FORMAT_VL (name, sizeof (name), vl, ds) != 0)
  {
    ERROR ("uc_insert: FORMAT_VL failed.");
    return (-1);
  }

  pthread_mutex_lock (&cache_lock);

  if (c_avl_get (cache_tree, name, (void *) &ce) == 0)
  {
    int i;

    assert (ce != NULL);
    assert (ce->values_num == ds->ds_num);

    if (ce->last_time >= vl->time)
    {
      pthread_mutex_unlock (&cache_lock);
      NOTICE ("uc_insert: Value too old: name = %s; value time = %u; "
	  "last cache update = %u;",
	  name, (unsigned int) vl->time, (unsigned int) ce->last_time);
      return (-1);
    }

    if ((ce->last_time + ce->interval) < vl->time)
    {
      send_okay_notification = vl->time - ce->last_time;
      ce->state = STATE_OKAY;
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
      DEBUG ("uc_insert: %s: ds[%i] = %lf", name, i, ce->values_gauge[i]);
    } /* for (i) */

    ce->last_time = vl->time;
    ce->last_update = time (NULL);
    ce->interval = vl->interval;
  }
  else /* key is not found */
  {
    int i;
    char *key;
    
    key = strdup (name);
    if (key == NULL)
    {
      pthread_mutex_unlock (&cache_lock);
      ERROR ("uc_insert: strdup failed.");
      return (-1);
    }

    ce = cache_alloc (ds->ds_num);
    if (ce == NULL)
    {
      pthread_mutex_unlock (&cache_lock);
      ERROR ("uc_insert: cache_alloc (%i) failed.", ds->ds_num);
      return (-1);
    }

    sstrncpy (ce->name, name, sizeof (ce->name));

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

    if (c_avl_insert (cache_tree, key, ce) != 0)
    {
      pthread_mutex_unlock (&cache_lock);
      ERROR ("uc_insert: c_avl_insert failed.");
      return (-1);
    }

    DEBUG ("uc_insert: Added %s to the cache.", name);
  } /* if (key is not found) */

  pthread_mutex_unlock (&cache_lock);

  /* Do not send okay notifications for uninteresting values, i. e. values for
   * which no threshold is configured. */
  if (send_okay_notification > 0)
  {
    int status;

    status = ut_check_interesting (name);
    if (status <= 0)
      send_okay_notification = 0;
  }

  if (send_okay_notification > 0)
  {
    notification_t n;
    memset (&n, '\0', sizeof (n));

    /* Copy the associative members */
    NOTIFICATION_INIT_VL (&n, vl, ds);

    n.severity = NOTIF_OKAY;
    n.time = vl->time;

    snprintf (n.message, sizeof (n.message),
	"Received a value for %s. It was missing for %i seconds.",
	name, send_okay_notification);
    n.message[sizeof (n.message) - 1] = '\0';

    plugin_dispatch_notification (&n);
  }

  return (0);
} /* int uc_insert */

gauge_t *uc_get_rate (const data_set_t *ds, const value_list_t *vl)
{
  char name[6 * DATA_MAX_NAME_LEN];
  gauge_t *ret = NULL;
  cache_entry_t *ce = NULL;

  if (FORMAT_VL (name, sizeof (name), vl, ds) != 0)
  {
    ERROR ("uc_insert: FORMAT_VL failed.");
    return (NULL);
  }

  pthread_mutex_lock (&cache_lock);

  if (c_avl_get (cache_tree, name, (void *) &ce) == 0)
  {
    assert (ce != NULL);
    assert (ce->values_num == ds->ds_num);

    ret = (gauge_t *) malloc (ce->values_num * sizeof (gauge_t));
    if (ret == NULL)
    {
      ERROR ("uc_get_rate: malloc failed.");
    }
    else
    {
      memcpy (ret, ce->values_gauge, ce->values_num * sizeof (gauge_t));
    }
  }

  pthread_mutex_unlock (&cache_lock);

  return (ret);
} /* gauge_t *uc_get_rate */

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

  if (state < STATE_OKAY)
    state = STATE_OKAY;
  if (state > STATE_ERROR)
    state = STATE_ERROR;

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
