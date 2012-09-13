/**
 * collectd - src/match_timediff.c
 * Copyright (C) 2008,2009  Florian Forster
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

#define SATISFY_ALL 0
#define SATISFY_ANY 1

/*
 * private data types
 */
struct mt_match_s;
typedef struct mt_match_s mt_match_t;
struct mt_match_s
{
  cdtime_t future;
  cdtime_t past;
};

/*
 * internal helper functions
 */
static int mt_create (const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  mt_match_t *m;
  int status;
  int i;

  m = (mt_match_t *) malloc (sizeof (*m));
  if (m == NULL)
  {
    ERROR ("mt_create: malloc failed.");
    return (-ENOMEM);
  }
  memset (m, 0, sizeof (*m));

  m->future = 0;
  m->past = 0;

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Future", child->key) == 0)
      status = cf_util_get_cdtime (child, &m->future);
    else if (strcasecmp ("Past", child->key) == 0)
      status = cf_util_get_cdtime (child, &m->past);
    else
    {
      ERROR ("timediff match: The `%s' configuration option is not "
          "understood and will be ignored.", child->key);
      status = 0;
    }

    if (status != 0)
      break;
  }

  /* Additional sanity-checking */
  while (status == 0)
  {
    if ((m->future == 0) && (m->past == 0))
    {
      ERROR ("timediff match: Either `Future' or `Past' must be configured. "
          "This match will be ignored.");
      status = -1;
    }

    break;
  }

  if (status != 0)
  {
    free (m);
    return (status);
  }

  *user_data = m;
  return (0);
} /* }}} int mt_create */

static int mt_destroy (void **user_data) /* {{{ */
{
  if (user_data != NULL)
  {
    sfree (*user_data);
  }

  return (0);
} /* }}} int mt_destroy */

static int mt_match (const data_set_t __attribute__((unused)) *ds, /* {{{ */
    const value_list_t *vl,
    notification_meta_t __attribute__((unused)) **meta, void **user_data)
{
  mt_match_t *m;
  cdtime_t now;

  if ((user_data == NULL) || (*user_data == NULL))
    return (-1);

  m = *user_data;
  now = cdtime ();

  if (m->future != 0)
  {
    if (vl->time >= (now + m->future))
      return (FC_MATCH_MATCHES);
  }

  if (m->past != 0)
  {
    if (vl->time <= (now - m->past))
      return (FC_MATCH_MATCHES);
  }

  return (FC_MATCH_NO_MATCH);
} /* }}} int mt_match */

void module_register (void)
{
  match_proc_t mproc;

  memset (&mproc, 0, sizeof (mproc));
  mproc.create  = mt_create;
  mproc.destroy = mt_destroy;
  mproc.match   = mt_match;
  fc_register_match ("timediff", mproc);
} /* module_register */

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */
