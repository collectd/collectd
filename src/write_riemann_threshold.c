/**
 * collectd - src/threshold.c
 * Copyright (C) 2007-2010  Florian Forster
 * Copyright (C) 2008-2009  Sebastian Harl
 * Copyright (C) 2009       Andrés J. Díaz
 * Copyright (C) 2014       Pierre-Yves Ritschard
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
 *   Florian octo Forster <octo at collectd.org>
 *   Sebastian Harl <sh at tokkee.org>
 *   Andrés J. Díaz <ajdiaz at connectical.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_avltree.h"
#include "utils_cache.h"
#include "utils_threshold.h"

#include <assert.h>
#include <ltdl.h>
#include <pthread.h>

/*
 * Threshold management
 * ====================
 * The following functions add, delete, etc. configured thresholds to
 * the underlying AVL trees.
 */

/*
 * int ut_check_one_data_source
 *
 * Checks one data source against the given threshold configuration. If the
 * `DataSource' option is set in the threshold, and the name does NOT match,
 * `okay' is returned. If the threshold does match, its failure and warning
 * min and max values are checked and `failure' or `warning' is returned if
 * appropriate.
 * Does not fail.
 */
static int ut_check_one_data_source (const data_set_t *ds,
    const value_list_t __attribute__((unused)) *vl,
    const threshold_t *th,
    const gauge_t *values,
    int ds_index)
{ /* {{{ */
  const char *ds_name;
  int is_warning = 0;
  int is_failure = 0;
  int prev_state = STATE_OKAY;

  /* check if this threshold applies to this data source */
  if (ds != NULL)
  {
    ds_name = ds->ds[ds_index].name;
    if ((th->data_source[0] != 0)
	&& (strcmp (ds_name, th->data_source) != 0))
      return (STATE_OKAY);
  }

  if ((th->flags & UT_FLAG_INVERT) != 0)
  {
    is_warning--;
    is_failure--;
  }

  /* XXX: This is an experimental code, not optimized, not fast, not reliable,
   * and probably, do not work as you expect. Enjoy! :D */
  if ( (th->hysteresis > 0) && ((prev_state = uc_get_state(ds,vl)) != STATE_OKAY) )
  {
    switch(prev_state)
    {
      case STATE_ERROR:
	if ( (!isnan (th->failure_min) && ((th->failure_min + th->hysteresis) < values[ds_index])) ||
	     (!isnan (th->failure_max) && ((th->failure_max - th->hysteresis) > values[ds_index])) )
	  return (STATE_OKAY);
	else
	  is_failure++;
      case STATE_WARNING:
	if ( (!isnan (th->warning_min) && ((th->warning_min + th->hysteresis) < values[ds_index])) ||
	     (!isnan (th->warning_max) && ((th->warning_max - th->hysteresis) > values[ds_index])) )
	  return (STATE_OKAY);
	else
	  is_warning++;
     }
  }
  else { /* no hysteresis */
    if ((!isnan (th->failure_min) && (th->failure_min > values[ds_index]))
	|| (!isnan (th->failure_max) && (th->failure_max < values[ds_index])))
      is_failure++;

    if ((!isnan (th->warning_min) && (th->warning_min > values[ds_index]))
	|| (!isnan (th->warning_max) && (th->warning_max < values[ds_index])))
      is_warning++;
 }

  if (is_failure != 0)
    return (STATE_ERROR);

  if (is_warning != 0)
    return (STATE_WARNING);

  return (STATE_OKAY);
} /* }}} int ut_check_one_data_source */

/*
 * int ut_check_one_threshold
 *
 * Checks all data sources of a value list against the given threshold, using
 * the ut_check_one_data_source function above. Returns the worst status,
 * which is `okay' if nothing has failed.
 * Returns less than zero if the data set doesn't have any data sources.
 */
static int ut_check_one_threshold (const data_set_t *ds,
    const value_list_t *vl,
    const threshold_t *th,
    const gauge_t *values,
    int *statuses)
{ /* {{{ */
  int ret = -1;
  int i;
  int status;
  gauge_t values_copy[ds->ds_num];

  memcpy (values_copy, values, sizeof (values_copy));

  if ((th->flags & UT_FLAG_PERCENTAGE) != 0)
  {
    int num = 0;
    gauge_t sum=0.0;

    if (ds->ds_num == 1)
    {
      WARNING ("ut_check_one_threshold: The %s type has only one data "
          "source, but you have configured to check this as a percentage. "
          "That doesn't make much sense, because the percentage will always "
          "be 100%%!", ds->type);
    }

    /* Prepare `sum' and `num'. */
    for (i = 0; i < ds->ds_num; i++)
      if (!isnan (values[i]))
      {
        num++;
	sum += values[i];
      }

    if ((num == 0) /* All data sources are undefined. */
        || (sum == 0.0)) /* Sum is zero, cannot calculate percentage. */
    {
      for (i = 0; i < ds->ds_num; i++)
        values_copy[i] = NAN;
    }
    else /* We can actually calculate the percentage. */
    {
      for (i = 0; i < ds->ds_num; i++)
        values_copy[i] = 100.0 * values[i] / sum;
    }
  } /* if (UT_FLAG_PERCENTAGE) */

  for (i = 0; i < ds->ds_num; i++)
  {
    status = ut_check_one_data_source (ds, vl, th, values_copy, i);
    if (status != -1) {
	    ret = 0;
	    if (statuses[i] < status)
		    statuses[i] = status;
    }
  } /* for (ds->ds_num) */

  return (ret);
} /* }}} int ut_check_one_threshold */

/*
 * int ut_check_threshold
 *
 * Gets a list of matching thresholds and searches for the worst status by one
 * of the thresholds. Then reports that status using the ut_report_state
 * function above.
 * Returns zero on success and if no threshold has been configured. Returns
 * less than zero on failure.
 */
int write_riemann_threshold_check (const data_set_t *ds, const value_list_t *vl,
				   int *statuses)
{ /* {{{ */
  threshold_t *th;
  gauge_t *values;
  int status;

  memset(statuses, 0, vl->values_len * sizeof(*statuses));
  if (threshold_tree == NULL)
	  return 0;

  /* Is this lock really necessary? So far, thresholds are only inserted at
   * startup. -octo */
  pthread_mutex_lock (&threshold_lock);
  th = threshold_search (vl);
  pthread_mutex_unlock (&threshold_lock);
  if (th == NULL)
	  return (0);

  DEBUG ("ut_check_threshold: Found matching threshold(s)");

  values = uc_get_rate (ds, vl);
  if (values == NULL)
	  return (0);

  while (th != NULL)
  {
    status = ut_check_one_threshold (ds, vl, th, values, statuses);
    if (status < 0)
    {
      ERROR ("ut_check_threshold: ut_check_one_threshold failed.");
      sfree (values);
      return (-1);
    }

    th = th->next;
  } /* while (th) */

  sfree (values);

  return (0);
} /* }}} int ut_check_threshold */


/* vim: set sw=2 ts=8 sts=2 tw=78 et fdm=marker : */
