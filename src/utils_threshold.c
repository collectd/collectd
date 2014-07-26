/**
 * collectd - src/utils_threshold.c
 * Copyright (C) 2014 Pierre-Yves Ritschard
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
 *   Pierre-Yves Ritschard <pyr at spootnik.org>
 **/

#include "collectd.h"
#include "common.h"
#include "utils_avltree.h"
#include "utils_threshold.h"

#include <pthread.h>

/*
 * Exported symbols
 * {{{ */
c_avl_tree_t   *threshold_tree = NULL;
pthread_mutex_t threshold_lock = PTHREAD_MUTEX_INITIALIZER;
/* }}} */

/*
 * threshold_t *threshold_get
 *
 * Retrieve one specific threshold configuration. For looking up a threshold
 * matching a value_list_t, see "threshold_search" below. Returns NULL if the
 * specified threshold doesn't exist.
 */
threshold_t *threshold_get (const char *hostname,
    const char *plugin, const char *plugin_instance,
    const char *type, const char *type_instance)
{ /* {{{ */
  char name[6 * DATA_MAX_NAME_LEN];
  threshold_t *th = NULL;

  format_name (name, sizeof (name),
      (hostname == NULL) ? "" : hostname,
      (plugin == NULL) ? "" : plugin, plugin_instance,
      (type == NULL) ? "" : type, type_instance);
  name[sizeof (name) - 1] = '\0';

  if (c_avl_get (threshold_tree, name, (void *) &th) == 0)
    return (th);
  else
    return (NULL);
} /* }}} threshold_t *threshold_get */

/*
 * threshold_t *threshold_search
 *
 * Searches for a threshold configuration using all the possible variations of
 * "Host", "Plugin" and "Type" blocks. Returns NULL if no threshold could be
 * found.
 * XXX: This is likely the least efficient function in collectd.
 */
threshold_t *threshold_search (const value_list_t *vl)
{ /* {{{ */
  threshold_t *th;

  if ((th = threshold_get (vl->host, vl->plugin, vl->plugin_instance,
	  vl->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get (vl->host, vl->plugin, vl->plugin_instance,
	  vl->type, NULL)) != NULL)
    return (th);
  else if ((th = threshold_get (vl->host, vl->plugin, NULL,
	  vl->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get (vl->host, vl->plugin, NULL,
	  vl->type, NULL)) != NULL)
    return (th);
  else if ((th = threshold_get (vl->host, "", NULL,
	  vl->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get (vl->host, "", NULL,
	  vl->type, NULL)) != NULL)
    return (th);
  else if ((th = threshold_get ("", vl->plugin, vl->plugin_instance,
	  vl->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get ("", vl->plugin, vl->plugin_instance,
	  vl->type, NULL)) != NULL)
    return (th);
  else if ((th = threshold_get ("", vl->plugin, NULL,
	  vl->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get ("", vl->plugin, NULL,
	  vl->type, NULL)) != NULL)
    return (th);
  else if ((th = threshold_get ("", "", NULL,
	  vl->type, vl->type_instance)) != NULL)
    return (th);
  else if ((th = threshold_get ("", "", NULL,
	  vl->type, NULL)) != NULL)
    return (th);

  return (NULL);
} /* }}} threshold_t *threshold_search */

int ut_search_threshold (const value_list_t *vl, /* {{{ */
    threshold_t *ret_threshold)
{
  threshold_t *t;

  if (vl == NULL)
    return (EINVAL);

	/* Is this lock really necessary? */
	pthread_mutex_lock (&threshold_lock);
  t = threshold_search (vl);
  if (t == NULL) {
		pthread_mutex_unlock (&threshold_lock);
    return (ENOENT);
	}

  memcpy (ret_threshold, t, sizeof (*ret_threshold));
	pthread_mutex_unlock (&threshold_lock);

  ret_threshold->next = NULL;

  return (0);
} /* }}} int ut_search_threshold */


