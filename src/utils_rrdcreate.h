/**
 * collectd - src/utils_rrdcreate.h
 * Copyright (C) 2008-2013  Florian octo Forster
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
 **/

#ifndef UTILS_RRDCREATE_H
#define UTILS_RRDCREATE_H 1

#include "plugin.h"

#include <stddef.h>

typedef enum {
  RRA_TYPE__AVERAGE = 0,
  RRA_TYPE__MIN,
  RRA_TYPE__MAX,
  RRA_TYPE_NUM
} rra_types_e;

struct rra_param_s
{
  short type[RRA_TYPE_NUM];   /* 0=undef, 1=yes, -1=no */ /* Check rra_types[] in utils_rrdcreate.c */
  int span;        /* 0 = undef */
  int pdp_per_row; /* 0 = undef */
  double xff;      /* <0 = undef (for example, -1.) */
};
typedef struct rra_param_s rra_param_t;

struct rrdcreate_config_s
{
  unsigned long stepsize;
  int    heartbeat;
  int    rrarows;
  double xff;

  int *timespans;
  size_t timespans_num;

  int *rra_types;

  rra_param_t *rra_param;
  size_t rra_param_num;

  char **consolidation_functions;
  size_t consolidation_functions_num;

  _Bool async;
};
typedef struct rrdcreate_config_s rrdcreate_config_t;

int rc_config_get_int_positive (oconfig_item_t const *ci, int *ret);
int rc_config_get_xff (oconfig_item_t const *ci, double *ret);
int rc_config_add_timespan (int timespan, rrdcreate_config_t *cfg);
int cu_rrd_rra_types_set(const oconfig_item_t *ci, rrdcreate_config_t *cfg);
int cu_rrd_rra_param_append(const oconfig_item_t *ci, rrdcreate_config_t *cfg);
int cu_rrd_sort_config_items(rrdcreate_config_t *cfg);

int cu_rrd_create_file (const char *filename,
    const data_set_t *ds, const value_list_t *vl,
    const rrdcreate_config_t *cfg);

#endif /* UTILS_RRDCREATE_H */

/* vim: set sw=2 sts=2 et : */
