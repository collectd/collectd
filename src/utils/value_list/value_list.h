/**
 * collectd - src/utils/value_list/value_list.h
 * Copyright (C) 2005-2023  Florian octo Forster
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
 *   Sebastian Harl <sh at tokkee.org>
 *   Manoj Srivastava <srivasta at google.com>
 **/

#ifndef UTILS_VALUE_LIST_H
#define UTILS_VALUE_LIST_H 1

struct value_list_s {
  value_t *values;
  size_t values_len;
  cdtime_t time;
  cdtime_t interval;
  char host[DATA_MAX_NAME_LEN];
  char plugin[DATA_MAX_NAME_LEN];
  char plugin_instance[DATA_MAX_NAME_LEN];
  char type[DATA_MAX_NAME_LEN];
  char type_instance[DATA_MAX_NAME_LEN];
  meta_data_t *meta;
};
typedef struct value_list_s value_list_t;

#define VALUE_LIST_INIT                                                        \
  { .values = NULL, .meta = NULL }

/*
 * NAME
 *  plugin_dispatch_values
 *
 * DESCRIPTION
 *  This function is called by reading processes with the values they've
 *  aquired. The function fetches the data-set definition (that has been
 *  registered using `plugin_register_data_set') and calls _all_ registered
 *  write-functions.
 *
 * ARGUMENTS
 *  `vl'        Value list of the values that have been read by a `read'
 *              function.
 */
int plugin_dispatch_values(value_list_t const *vl);

/*
 * NAME
 *  plugin_dispatch_multivalue
 *
 * SYNOPSIS
 *  plugin_dispatch_multivalue (vl, true, DS_TYPE_GAUGE,
 *                              "free", 42.0,
 *                              "used", 58.0,
 *                              NULL);
 *
 * DESCRIPTION
 *  Takes a list of type instances and values and dispatches that in a batch,
 *  making sure that all values have the same time stamp. If "store_percentage"
 *  is set to true, the "type" is set to "percent" and a percentage is
 *  calculated and dispatched, rather than the absolute values. Values that are
 *  NaN are dispatched as NaN and will not influence the total.
 *
 *  The variadic arguments is a list of type_instance / type pairs, that are
 *  interpreted as type "char const *" and type, encoded by their corresponding
 *  "store_type":
 *
 *     - "gauge_t"    when "DS_TYPE_GAUGE"
 *     - "derive_t"   when "DS_TYPE_DERIVE"
 *     - "counter_t"  when "DS_TYPE_COUNTER"
 *
 *  The last argument must be
 *  a NULL pointer to signal end-of-list.
 *
 * RETURNS
 *  The number of values it failed to dispatch (zero on success).
 */
__attribute__((sentinel)) int plugin_dispatch_multivalue(value_list_t const *vl,
                                                         bool store_percentage,
                                                         int store_type, ...);

#endif
