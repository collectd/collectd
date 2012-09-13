/**
 * collectd - src/match_hashed.c
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
struct mh_hash_match_s
{
  uint32_t match;
  uint32_t total;
};
typedef struct mh_hash_match_s mh_hash_match_t;

struct mh_match_s;
typedef struct mh_match_s mh_match_t;
struct mh_match_s
{
  mh_hash_match_t *matches;
  size_t           matches_num;
};

/*
 * internal helper functions
 */
static int mh_config_match (const oconfig_item_t *ci, /* {{{ */
    mh_match_t *m)
{
  mh_hash_match_t *tmp;

  if ((ci->values_num != 2)
      || (ci->values[0].type != OCONFIG_TYPE_NUMBER)
      || (ci->values[1].type != OCONFIG_TYPE_NUMBER))
  {
    ERROR ("hashed match: The `Match' option requires "
        "exactly two numeric arguments.");
    return (-1);
  }

  if ((ci->values[0].value.number < 0)
      || (ci->values[1].value.number < 0))
  {
    ERROR ("hashed match: The arguments of the `Match' "
        "option must be positive.");
    return (-1);
  }

  tmp = realloc (m->matches, sizeof (*tmp) * (m->matches_num + 1));
  if (tmp == NULL)
  {
    ERROR ("hashed match: realloc failed.");
    return (-1);
  }
  m->matches = tmp;
  tmp = m->matches + m->matches_num;

  tmp->match = (uint32_t) (ci->values[0].value.number + .5);
  tmp->total = (uint32_t) (ci->values[1].value.number + .5);

  if (tmp->match >= tmp->total)
  {
    ERROR ("hashed match: The first argument of the `Match' option "
        "must be smaller than the second argument.");
    return (-1);
  }
  assert (tmp->total != 0);

  m->matches_num++;
  return (0);
} /* }}} int mh_config_match */

static int mh_create (const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  mh_match_t *m;
  int i;

  m = (mh_match_t *) malloc (sizeof (*m));
  if (m == NULL)
  {
    ERROR ("mh_create: malloc failed.");
    return (-ENOMEM);
  }
  memset (m, 0, sizeof (*m));

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Match", child->key) == 0)
      mh_config_match (child, m);
    else
      ERROR ("hashed match: No such config option: %s", child->key);
  }

  if (m->matches_num == 0)
  {
    sfree (m->matches);
    sfree (m);
    ERROR ("hashed match: No matches were configured. Not creating match.");
    return (-1);
  }

  *user_data = m;
  return (0);
} /* }}} int mh_create */

static int mh_destroy (void **user_data) /* {{{ */
{
  mh_match_t *mh;

  if ((user_data == NULL) || (*user_data == NULL))
    return (0);

  mh = *user_data;
  sfree (mh->matches);
  sfree (mh);

  return (0);
} /* }}} int mh_destroy */

static int mh_match (const data_set_t __attribute__((unused)) *ds, /* {{{ */
    const value_list_t *vl,
    notification_meta_t __attribute__((unused)) **meta, void **user_data)
{
  mh_match_t *m;
  uint32_t hash_val;
  const char *host_ptr;
  size_t i;

  if ((user_data == NULL) || (*user_data == NULL))
    return (-1);

  m = *user_data;

  hash_val = 0;

  for (host_ptr = vl->host; *host_ptr != 0; host_ptr++)
  {
    /* 2184401929 is some appropriately sized prime number. */
    hash_val = (hash_val * UINT32_C (2184401929)) + ((uint32_t) *host_ptr);
  }
  DEBUG ("hashed match: host = %s; hash_val = %"PRIu32";", vl->host, hash_val);

  for (i = 0; i < m->matches_num; i++)
    if ((hash_val % m->matches[i].total) == m->matches[i].match)
      return (FC_MATCH_MATCHES);

  return (FC_MATCH_NO_MATCH);
} /* }}} int mh_match */

void module_register (void)
{
  match_proc_t mproc;

  memset (&mproc, 0, sizeof (mproc));
  mproc.create  = mh_create;
  mproc.destroy = mh_destroy;
  mproc.match   = mh_match;
  fc_register_match ("hashed", mproc);
} /* module_register */

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */
