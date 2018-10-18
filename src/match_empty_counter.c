/**
 * collectd - src/match_empty_counter.c
 * Copyright (C) 2009-2016  Florian Forster
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

#include "filter_chain.h"
#include "utils/common/common.h"

/*
 * internal helper functions
 */
static int mec_create(const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  if (ci->children_num != 0) {
    ERROR("empty_counter match: This match does not take any additional "
          "configuration.");
  }

  *user_data = NULL;
  return 0;
} /* }}} int mec_create */

static int mec_destroy(__attribute__((unused)) void **user_data) /* {{{ */
{
  return 0;
} /* }}} int mec_destroy */

static int mec_match(__attribute__((unused)) const data_set_t *ds, /* {{{ */
                     const value_list_t *vl,
                     __attribute__((unused)) notification_meta_t **meta,
                     __attribute__((unused)) void **user_data) {
  int num_counters = 0;
  int num_empty = 0;

  for (size_t i = 0; i < ds->ds_num; i++) {
    if ((ds->ds[i].type != DS_TYPE_DERIVE) &&
        (ds->ds[i].type != DS_TYPE_COUNTER))
      continue;

    num_counters++;
    if (((ds->ds[i].type == DS_TYPE_DERIVE) && (vl->values[i].derive == 0)) ||
        ((ds->ds[i].type == DS_TYPE_COUNTER) && (vl->values[i].counter == 0)))
      num_empty++;
  }

  if ((num_counters != 0) && (num_counters == num_empty))
    return FC_MATCH_MATCHES;

  return FC_MATCH_NO_MATCH;
} /* }}} int mec_match */

void module_register(void) {
  fc_register_match(
      "empty_counter",
      (match_proc_t){
          .create = mec_create, .destroy = mec_destroy, .match = mec_match,
      });
} /* module_register */
