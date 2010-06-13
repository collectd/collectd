/**
 * collectd - src/utils_threshold.h
 * Copyright (C) 2007-2009  Florian octo Forster
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

#ifndef UTILS_THRESHOLD_H
#define UTILS_THRESHOLD_H 1

#include "collectd.h"
#include "liboconfig/oconfig.h"
#include "plugin.h"

typedef struct threshold_s
{
  char host[DATA_MAX_NAME_LEN];
  char plugin[DATA_MAX_NAME_LEN];
  char plugin_instance[DATA_MAX_NAME_LEN];
  char type[DATA_MAX_NAME_LEN];
  char type_instance[DATA_MAX_NAME_LEN];
  char data_source[DATA_MAX_NAME_LEN];
  gauge_t warning_min;
  gauge_t warning_max;
  gauge_t failure_min;
  gauge_t failure_max;
  gauge_t hysteresis;
  unsigned int flags;
  int hits;
  struct threshold_s *next;
} threshold_t;

/*
 * ut_config
 *
 * Parses the configuration and sets up the module. This is called from
 * `src/configfile.c'.
 */
int ut_config (const oconfig_item_t *ci);

/*
 * ut_check_threshold
 *
 * Checks if a threshold is defined for this value and if such a threshold is
 * configured, check if the value within the acceptable range. If it is not, a
 * notification is dispatched to inform the user that a problem exists. This is
 * called from `plugin_read_all'.
 */
int ut_check_threshold (const data_set_t *ds, const value_list_t *vl);

/*
 * Given an identification returns
 * 0: No threshold is defined.
 * 1: A threshold has been found. The flag `persist' is off.
 * 2: A threshold has been found. The flag `persist' is on.
 *    (That is, it is expected that many notifications are sent until the
 *    problem disappears.)
 */
int ut_check_interesting (const char *name);

/* 
 * Given an identifier in form of a `value_list_t', searches for the best
 * matching threshold configuration. `ret_threshold' may be NULL.
 *
 * Returns:
 *        0: Success. Threshold configuration has been copied to
 *           `ret_threshold' (if it is non-NULL).
 *   ENOENT: No configuration for this identifier found.
 *     else: Error.
 */
int ut_search_threshold (const value_list_t *vl, threshold_t *ret_threshold);

#endif /* UTILS_THRESHOLD_H */
