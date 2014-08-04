/**
 * collectd - src/utils_threshold.h
 * Copyright (C) 2014       Pierre-Yves Ritschard
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
 *   Pierre-Yves Ritschard <pyr at spootnik.org>
 **/

#ifndef UTILS_THRESHOLD_H
#define UTILS_THRESHOLD_H 1

#define UT_FLAG_INVERT  0x01
#define UT_FLAG_PERSIST 0x02
#define UT_FLAG_PERCENTAGE 0x04
#define UT_FLAG_INTERESTING 0x08
#define UT_FLAG_PERSIST_OK 0x10
typedef struct threshold_s
{
  char host[DATA_MAX_NAME_LEN];
  char plugin[DATA_MAX_NAME_LEN];
  char plugin_instance[DATA_MAX_NAME_LEN];
  char type[DATA_MAX_NAME_LEN];
  char type_instance[DATA_MAX_NAME_LEN];
  char data_source[DATA_MAX_NAME_LEN];
  gauge_t warning_min;
  gauge_t warning_max;
  gauge_t failure_min;
  gauge_t failure_max;
  gauge_t hysteresis;
  unsigned int flags;
  int hits;
  struct threshold_s *next;
} threshold_t;

extern c_avl_tree_t   *threshold_tree;
extern pthread_mutex_t threshold_lock;

threshold_t *threshold_get (const char *hostname,
    const char *plugin, const char *plugin_instance,
    const char *type, const char *type_instance);

threshold_t *threshold_search (const value_list_t *vl);

int ut_search_threshold (const value_list_t *vl, 
  threshold_t *ret_threshold);

#endif /* UTILS_THRESHOLD_H */

/* vim: set sw=2 sts=2 ts=8 : */
