/**
 * collectd - src/utils_threshold.c
 * Copyright (C) 2014       Pierre-Yves Ritschard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
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


