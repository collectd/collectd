/**
 * collectd - src/write_riemann_threshold.h
 * Copyright (C) 2016       Ruben Kerkhof
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
 *   Ruben Kerkhof <ruben at rubenkerkhof.com>
 **/

#ifndef WRITE_RIEMANN_THRESHOLD_H
#define WRITE_RIEMANN_THRESHOLD_H

#include "plugin.h"

/* write_riemann_threshold_check tests all matching thresholds and returns the
 * worst result for each data source in "statuses". "statuses" must point to
 * ds->ds_num integers to which the result is written.
 *
 * Returns zero on success and if no threshold has been configured. Returns
 * less than zero on failure. */
int write_riemann_threshold_check(const data_set_t *ds, const value_list_t *vl,
                                  int *statuses);

#endif /* WRITE_RIEMANN_THRESHOLD_H */
