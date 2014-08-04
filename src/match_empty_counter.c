/**
 * collectd - src/match_empty_counter.c
 * Copyright (C) 2009       Florian Forster
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
 *   Florian Forster <octo at collectd.org>
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
