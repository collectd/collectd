/**
 * collectd - src/daemon/data_set.h
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

#ifndef DAEMON_DATA_SET_H
#define DAEMON_DATA_SET_H

#include "collectd.h"

struct data_source_s {
  char name[DATA_MAX_NAME_LEN];
  int type;
  double min;
  double max;
};
typedef struct data_source_s data_source_t;

struct data_set_s {
  char type[DATA_MAX_NAME_LEN];
  size_t ds_num;
  data_source_t *ds;
};
typedef struct data_set_s data_set_t;

int plugin_register_data_set(const data_set_t *ds);
int plugin_unregister_data_set(const char *name);
const data_set_t *plugin_get_ds(const char *name);
void plugin_free_data_sets(void);

#endif /* DAEMON_DATA_SET_H */
