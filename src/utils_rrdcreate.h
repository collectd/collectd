/**
 * collectd - src/utils_rrdcreate.h
 * Copyright (C) 2008-2013  Florian octo Forster
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
  int precision;   /* 0 = undef; ignored if pdp_per_row is set */
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
