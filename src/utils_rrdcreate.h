/**
 * collectd - src/utils_rrdcreate.h
 * Copyright (C) 2008  Florian octo Forster
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
 **/

#ifndef UTILS_RRDCREATE_H
#define UTILS_RRDCREATE_H 1

#include "plugin.h"

#include <stddef.h>

struct rrdcreate_config_s
{
  unsigned long stepsize;
  int    heartbeat;
  int    rrarows;
  double xff;

  int *timespans;
  size_t timespans_num;

  char **consolidation_functions;
  size_t consolidation_functions_num;
};
typedef struct rrdcreate_config_s rrdcreate_config_t;

int cu_rrd_create_file (const char *filename,
    const data_set_t *ds, const value_list_t *vl,
    const rrdcreate_config_t *cfg);

#endif /* UTILS_RRDCREATE_H */

/* vim: set sw=2 sts=2 et : */
