/**
 * collectd - src/daemon/distribution.c
 * Copyright (C) 2020       Google LLC
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
 *   Barbara bkjg Kaczorowska <bkjg at google.com>
 */

#include "distribution.h"
#include <math.h>
#include <pthread.h>

struct bucket_s {
  double max_boundary;
  uint64_t counter;
};

struct distribution_s {
  bucket_t *buckets;
  double sum_gauges;
  size_t num_buckets;
  pthread_mutex_t mutex;
};

double *distribution_get_buckets_boundaries(distribution_t *d) {
  if (d == NULL) {
    errno = EINVAL;
    return NULL;
  }

  double *boundaries = calloc(d->num_buckets, sizeof(double));

  if (boundaries == NULL) {
    return NULL;
  }

  /* boundaries won't change, so we don't have to lock the mutex */
  for (size_t i = 0; i < d->num_buckets; ++i) {
    boundaries[i] = d->buckets[i].max_boundary;
  }

  return boundaries;
}

uint64_t *distribution_get_buckets_counters(distribution_t *d) {
  if (d == NULL) {
    errno = EINVAL;
    return NULL;
  }

  uint64_t *counters = calloc(d->num_buckets, sizeof(uint64_t));

  if (counters == NULL) {
    return NULL;
  }

  pthread_mutex_lock(&d->mutex);
  for (size_t i = 0; i < d->num_buckets; ++i) {
    counters[i] = d->buckets[i].counter;
  }
  pthread_mutex_unlock(&d->mutex);

  return counters;
}

size_t distribution_get_num_buckets(distribution_t *d) {
  if (d == NULL) {
    errno = EINVAL;
    return 0;
  }

  return d->num_buckets;
}

double distribution_get_sum_gauges(distribution_t *d) {
  if (d == NULL) {
    errno = EINVAL;
    return NAN;
  }

  return d->sum_gauges;
}

static bucket_t *bucket_new_linear(size_t num_buckets, double size) {
  bucket_t *buckets = calloc(num_buckets, sizeof(bucket_t));

  if (buckets == NULL) {
    return NULL;
  }

  for (size_t i = 0; i < num_buckets - 1; ++i) {
    buckets[i].max_boundary = (double)(i + 1) * size;
  }

  buckets[num_buckets - 1].max_boundary = INFINITY;

  return buckets;
}

static bucket_t *bucket_new_exponential(size_t num_buckets, double base,
                                        double factor) {
  bucket_t *buckets = calloc(num_buckets, sizeof(bucket_t));

  if (buckets == NULL) {
    return NULL;
  }

  double multiplier = 1.0;

  for (size_t i = 0; i < num_buckets - 1; ++i) {
    buckets[i].max_boundary = factor * multiplier;
    multiplier *= base;
  }

  buckets[num_buckets - 1].max_boundary = INFINITY;

  return buckets;
}

static bucket_t *bucket_new_custom(size_t num_boundaries,
                                   const double *custom_buckets_boundaries) {
  bucket_t *buckets = calloc(num_boundaries + 1, sizeof(bucket_t));

  if (buckets == NULL) {
    return NULL;
  }

  if (num_boundaries > 0) {
    if (custom_buckets_boundaries[0] <= 0 ||
        custom_buckets_boundaries[0] == INFINITY) {
      free(buckets);
      errno = EINVAL;
      return NULL;
    }

    buckets[0].max_boundary = custom_buckets_boundaries[0];

    for (size_t i = 1; i < num_boundaries; ++i) {
      if (custom_buckets_boundaries[i] <= 0 ||
          custom_buckets_boundaries[i] == INFINITY ||
          custom_buckets_boundaries[i - 1] >= custom_buckets_boundaries[i]) {
        free(buckets);
        errno = EINVAL;
        return NULL;
      }

      buckets[i].max_boundary = custom_buckets_boundaries[i];
    }
  }

  buckets[num_boundaries].max_boundary = INFINITY;

  return buckets;
}

distribution_t *distribution_new_linear(size_t num_buckets, double size) {
  if (num_buckets == 0 || size <= 0) {
    errno = EINVAL;
    return NULL;
  }

  distribution_t *d = calloc(1, sizeof(distribution_t));

  if (d == NULL) {
    return NULL;
  }

  d->buckets = bucket_new_linear(num_buckets, size);

  if (d->buckets == NULL) {
    free(d);
    return NULL;
  }

  d->num_buckets = num_buckets;
  pthread_mutex_init(&d->mutex, NULL);

  return d;
}

distribution_t *distribution_new_exponential(size_t num_buckets, double base,
                                             double factor) {
  if (num_buckets == 0 || base <= 1 || factor <= 0) {
    errno = EINVAL;
    return NULL;
  }

  distribution_t *d = calloc(1, sizeof(distribution_t));

  if (d == NULL) {
    return NULL;
  }

  /* as in distribution_new_linear: it would be nice to check if base
   * and factor are greater than zero, for consideration: one of them also
   * greater than one */
  d->buckets = bucket_new_exponential(num_buckets, base, factor);

  if (d->buckets == NULL) {
    free(d);
    return NULL;
  }

  d->num_buckets = num_buckets;
  pthread_mutex_init(&d->mutex, NULL);

  return d;
}

distribution_t *distribution_new_custom(size_t num_boundaries,
                                        double *custom_buckets_boundaries) {
  distribution_t *d = calloc(1, sizeof(distribution_t));

  if (d == NULL) {
    return NULL;
  }

  d->buckets = bucket_new_custom(num_boundaries, custom_buckets_boundaries);

  if (d->buckets == NULL) {
    free(d);
    return NULL;
  }

  d->num_buckets = num_boundaries + 1;
  pthread_mutex_init(&d->mutex, NULL);

  return d;
}

static void bucket_update(bucket_t *buckets, size_t num_buckets, double gauge) {
  int idx = (int)num_buckets - 1;

  while (idx >= 0 && buckets[idx].max_boundary > gauge) {
    buckets[idx].counter++;
    idx--;
  }
}

void distribution_update(distribution_t *d, double gauge) {
  if (d == NULL || gauge < 0) {
    errno = EINVAL;
    return;
  }

  pthread_mutex_lock(&d->mutex);

  bucket_update(d->buckets, d->num_buckets, gauge);

  d->sum_gauges += gauge;
  pthread_mutex_unlock(&d->mutex);

  return;
}

static double find_percentile(bucket_t *buckets, size_t num_buckets,
                              uint64_t quantity) {
  size_t left = 0;
  size_t right = num_buckets - 1;
  size_t middle;

  while (left < right) {
    middle = (left + right) / 2;

    if (buckets[middle].counter >= quantity) {
      right = middle;
    } else {
      left = middle + 1;
    }
  }

  return buckets[left].max_boundary;
}

double distribution_percentile(distribution_t *d, double percent) {
  if (d == NULL || percent > 100.0 || percent < 0) {
    errno = EINVAL;
    return NAN;
  }

  pthread_mutex_lock(&d->mutex);

  uint64_t quantity = (uint64_t)(
      (percent / 100.0) * (double)d->buckets[d->num_buckets - 1].counter);

  percent = find_percentile(d->buckets, d->num_buckets, quantity);

  pthread_mutex_unlock(&d->mutex);
  return percent;
}

double distribution_average(distribution_t *d) {
  if (d == NULL) {
    errno = EINVAL;
    return NAN;
  }

  pthread_mutex_lock(&d->mutex);

  double average =
      d->sum_gauges / (double)d->buckets[d->num_buckets - 1].counter;

  pthread_mutex_unlock(&d->mutex);

  return average;
}

distribution_t *distribution_clone(distribution_t *d) {
  if (d == NULL) {
    errno = EINVAL;
    return NULL;
  }

  distribution_t *distribution = calloc(1, sizeof(distribution_t));

  if (distribution == NULL) {
    return NULL;
  }

  pthread_mutex_lock(&d->mutex);

  distribution->sum_gauges = d->sum_gauges;
  distribution->num_buckets = d->num_buckets;

  distribution->buckets = calloc(d->num_buckets, sizeof(bucket_t));

  if (distribution->buckets == NULL) {
    free(distribution);
    pthread_mutex_unlock(&d->mutex);
    return NULL;
  }

  memcpy(distribution->buckets, d->buckets, d->num_buckets * sizeof(bucket_t));

  pthread_mutex_init(&distribution->mutex, NULL);

  pthread_mutex_unlock(&d->mutex);

  return distribution;
}

void distribution_destroy(distribution_t *d) {
  if (d == NULL) {
    return;
  }

  pthread_mutex_destroy(&d->mutex);
  free(d->buckets);
  free(d);
}
