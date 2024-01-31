/**
 * collectd - utils_cache.h
 * Copyright (C) 2007       Florian octo Forster
 * Copyright (C) 2016       Sebastian tokkee Harl
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
 *   Sebastian tokkee Harl <sh at tokkee.org>
 *   Manoj Srivastava <srivasta at google.com>
 **/

#ifndef UTILS_CACHE_H
#define UTILS_CACHE_H 1

#include "plugin.h"

#define STATE_UNKNOWN 0
#define STATE_OKAY 1
#define STATE_WARNING 2
#define STATE_ERROR 3
#define STATE_MISSING 15

int uc_check_timeout(void);
int uc_update(metric_family_t const *fam);

gauge_t *uc_get_rate_vl(const data_set_t *ds, const value_list_t *vl);
int uc_get_value_by_name_vl(const char *name, value_t **ret_values,
                            size_t *ret_values_num);
value_t *uc_get_value_vl(const data_set_t *ds, const value_list_t *vl);

int uc_get_rate_by_name(const char *name, gauge_t *ret_value);

int uc_get_rate(metric_t const *m, gauge_t *ret_value);
int uc_get_value_by_name(const char *name, value_t *ret_value);
int uc_get_value(metric_t const *m, value_t *ret_value);

typedef struct {
  cdtime_t time;
  value_t value;
  int err;
} uc_first_metric_result_t;

// uc_first_metric returns the first observed metric value and time.
// For cumulative metrics (METRIC_TYPE_COUNTER and METRIC_TYPE_FPCOUNTER),
// counter resets and counter overflows will reset the value.
uc_first_metric_result_t uc_first_metric(metric_t const *m);

size_t uc_get_size(void);
int uc_get_names(char ***ret_names, cdtime_t **ret_times, size_t *ret_number);

int uc_get_state(metric_t const *m);
int uc_set_state(metric_t const *m, int state);

int uc_get_hits(metric_t const *m);
int uc_set_hits(metric_t const *m, int hits);
int uc_inc_hits(metric_t const *m, int step);

int uc_set_callbacks_mask(const char *name, unsigned long callbacks_mask);

int uc_get_history_by_name(const char *name, gauge_t *ret_history,
                           size_t num_steps);

/*
 * Iterator interface
 */
struct uc_iter_s;
typedef struct uc_iter_s uc_iter_t;

/*
 * NAME
 *   uc_get_iterator
 *
 * DESCRIPTION
 *   Create an iterator for the cache. It will hold the cache lock until it's
 *   destroyed.
 *
 * RETURN VALUE
 *   An iterator object on success or NULL else.
 */
uc_iter_t *uc_get_iterator(void);

/*
 * NAME
 *   uc_iterator_next
 *
 * DESCRIPTION
 *   Advance the iterator to the next positiion and (optionally) returns the
 *   name of the entry.
 *
 * PARAMETERS
 *   `iter'     The iterator object to advance.
 *   `ret_name' Optional pointer to a string where to store the name. If not
 *              NULL, the returned value is a copy of the value and has to be
 *              freed by the caller.
 *
 * RETURN VALUE
 *   Zero upon success or non-zero if the iterator ie NULL or no further
 *   values are available.
 */
int uc_iterator_next(uc_iter_t *iter, char **ret_name);
void uc_iterator_destroy(uc_iter_t *iter);

/* Return the timestamp of the value at the current position. */
int uc_iterator_get_time(uc_iter_t *iter, cdtime_t *ret_time);
/* Return the (raw) value at the current position. */
int uc_iterator_get_values(uc_iter_t *iter, value_t *ret_value);
/* Return the interval of the value at the current position. */
int uc_iterator_get_interval(uc_iter_t *iter, cdtime_t *ret_interval);
/* Return the metadata for the value at the current position. */
int uc_iterator_get_meta(uc_iter_t *iter, meta_data_t **ret_meta);

/*
 * Meta data interface
 */
int uc_meta_data_exists(metric_t const *m, const char *key);
int uc_meta_data_delete(metric_t const *m, const char *key);
/* Same API as meta_data_toc. */
int uc_meta_data_toc(metric_t const *m, char ***toc);

int uc_meta_data_add_string(metric_t const *m, const char *key,
                            const char *value);
int uc_meta_data_add_signed_int(metric_t const *m, const char *key,
                                int64_t value);
int uc_meta_data_add_unsigned_int(metric_t const *m, const char *key,
                                  uint64_t value);
int uc_meta_data_add_double(metric_t const *m, const char *key, double value);
int uc_meta_data_add_boolean(metric_t const *m, const char *key, bool value);

int uc_meta_data_get_string(metric_t const *m, const char *key, char **value);
int uc_meta_data_get_signed_int(metric_t const *m, const char *key,
                                int64_t *value);
int uc_meta_data_get_unsigned_int(metric_t const *m, const char *key,
                                  uint64_t *value);
int uc_meta_data_get_double(metric_t const *m, const char *key, double *value);
int uc_meta_data_get_boolean(metric_t const *m, const char *key, bool *value);

#endif /* !UTILS_CACHE_H */
