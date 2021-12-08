/**
 * collectd - src/tests/mock/utils_cache.c
 * Copyright (C) 2013       Florian octo Forster
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
 */

#include "utils_cache.h"
#include <errno.h>

#include <errno.h>

gauge_t *uc_get_rate(__attribute__((unused)) data_set_t const *ds,
                     __attribute__((unused)) value_list_t const *vl) {
  errno = ENOTSUP;
  return NULL;
}

int uc_get_rate_by_name(const char *name, gauge_t **ret_values,
                        size_t *ret_values_num) {
  return ENOTSUP;
}

int uc_get_names(char ***ret_names, cdtime_t **ret_times, size_t *ret_number) {
  return ENOTSUP;
}

int uc_get_value_by_name(const char *name, value_t **ret_values,
                         size_t *ret_values_num) {
  return ENOTSUP;
}

int uc_meta_data_get_signed_int(const value_list_t *vl, const char *key,
                                int64_t *value) {
  return -ENOENT;
}

int uc_meta_data_get_unsigned_int(const value_list_t *vl, const char *key,
                                  uint64_t *value) {
  return -ENOENT;
}

int uc_meta_data_add_signed_int(const value_list_t *vl, const char *key,
                                int64_t value) {
  return 0;
}

int uc_meta_data_add_unsigned_int(const value_list_t *vl, const char *key,
                                  uint64_t value) {
  return 0;
}
