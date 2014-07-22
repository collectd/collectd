/**
 * collectd - src/utils_threshold.h
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

#ifndef UTILS_THRESHOLD_H
#define UTILS_THRESHOLD_H 1

#define UT_FLAG_INVERT  0x01
#define UT_FLAG_PERSIST 0x02
#define UT_FLAG_PERCENTAGE 0x04
#define UT_FLAG_INTERESTING 0x08
#define UT_FLAG_PERSIST_OK 0x10
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

extern c_avl_tree_t   *threshold_tree;
extern pthread_mutex_t threshold_lock;

#endif /* UTILS_THRESHOLD_H */

/* vim: set sw=2 sts=2 ts=8 : */
