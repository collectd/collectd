/**
 * collectd - src/match_empty_counter.c
 * Copyright (C) 2009  Florian Forster
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
 *   Florian Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "utils_cache.h"
#include "filter_chain.h"

/*
 * private data types
 */
struct mec_match_s;
typedef struct mec_match_s mec_match_t;
struct mec_match_s
{
  int dummy;
};

/*
 * internal helper functions
 */
static int mec_create (const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  mec_match_t *m;

  m = (mec_match_t *) malloc (sizeof (*m));
  if (m == NULL)
  {
    ERROR ("mec_create: malloc failed.");
    return (-ENOMEM);
  }
  memset (m, 0, sizeof (*m));

  if (ci->children_num != 0)
  {
    ERROR ("empty_counter match: This match does not take any additional "
        "configuration.");
  }

  *user_data = m;
  return (0);
} /* }}} int mec_create */

static int mec_destroy (void **user_data) /* {{{ */
{
  if (user_data != NULL)
  {
    sfree (*user_data);
  }

  return (0);
} /* }}} int mec_destroy */

static int mec_match (const data_set_t __attribute__((unused)) *ds, /* {{{ */
    const value_list_t *vl,
    notification_meta_t __attribute__((unused)) **meta, void **user_data)
{
  int num_counters;
  int num_empty;
  int i;

  if ((user_data == NULL) || (*user_data == NULL))
    return (-1);


  num_counters = 0;
  num_empty = 0;

  for (i = 0; i < ds->ds_num; i++)
  {
    if (ds->ds[i].type != DS_TYPE_COUNTER)
      continue;

    num_counters++;
    if (vl->values[i].counter == 0)
      num_empty++;
  }

  if (num_counters == 0)
    return (FC_MATCH_NO_MATCH);
  else if (num_counters == num_empty)
    return (FC_MATCH_MATCHES);
  else
    return (FC_MATCH_NO_MATCH);
} /* }}} int mec_match */

void module_register (void)
{
  match_proc_t mproc;

  memset (&mproc, 0, sizeof (mproc));
  mproc.create  = mec_create;
  mproc.destroy = mec_destroy;
  mproc.match   = mec_match;
  fc_register_match ("empty_counter", mproc);
} /* module_register */

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */
