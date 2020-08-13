/**
 * collectd - src/daemon/distribution.c
 * Copyright (C) 2019-2020  Google LLC
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
 * Author:
 *   Elene Margalitadze <elene.margalit at gmail.com>
 **/

#include "distribution.h"
#include <math.h>
#include <pthread.h>

struct bucket_s {
  uint64_t bucket_counter;
  double min_boundary;
  double max_boundary;
};

struct distribution_s {
  bucket_t *buckets;
  size_t num_buckets;
  uint64_t total_scalar_count; // count of all registered scalar metrics
  double raw_data_sum;         // sum of all registered raw scalar metrics
  pthread_mutex_t mutex;
};

bucket_t *distribution_get_buckets(distribution_t *dist) {
  if (dist == NULL) {
    errno = EINVAL;
    return NULL;
  }

  bucket_t *buckets = calloc(dist->num_buckets, sizeof(bucket_t));

  if (buckets == NULL) {
    free(buckets);
    return NULL;
  }
  pthread_mutex_lock(&dist->mutex);
  memcpy(buckets, dist->buckets, sizeof(bucket_t) * dist->num_buckets);
  pthread_mutex_unlock(&dist->mutex);
  return buckets;
}

/*Private bucket constructor, min_boundary is inclusive, max_boundary is
exclusive because the max_boundary Infinity is exclusive and we want the other
max_boundaries to be consistent with that.*/
static bucket_t initialize_bucket(double min_boundary, double max_boundary) {
  bucket_t new_bucket = {
      .bucket_counter = 0,
      .min_boundary = min_boundary,
      .max_boundary = max_boundary,
  };
  return new_bucket;
}

distribution_t *distribution_new_linear(size_t num_buckets, double size) {
  if ((num_buckets == 0) || (size <= 0)) {
    errno = EINVAL;
    return NULL;
  }

  distribution_t *new_distribution = calloc(1, sizeof(distribution_t));
  bucket_t *buckets = calloc(num_buckets, sizeof(bucket_t));

  if ((new_distribution == NULL) || (buckets == NULL)) {
    free(new_distribution);
    free(buckets);
    return NULL;
  }
  new_distribution->buckets = buckets;

  for (size_t i = 0; i < num_buckets; i++) {
    if (i < num_buckets - 1) {
      new_distribution->buckets[i] =
          initialize_bucket(i * size, i * size + size);
    } else {
      new_distribution->buckets[i] = initialize_bucket(i * size, INFINITY);
    }
    double min_boundary = i * size;
    double max_boundary = (i == num_buckets - 1) ? INFINITY : i * size + size;
    new_distribution->buckets[i] = initialize_bucket(min_boundary, max_boundary);
  }

  new_distribution->num_buckets = num_buckets;
  new_distribution->total_scalar_count = 0;
  new_distribution->raw_data_sum = 0;
  pthread_mutex_init(&new_distribution->mutex, NULL);
  return new_distribution;
}

distribution_t *distribution_new_exponential(size_t num_buckets, double factor,
                                             double base) {
  if ((num_buckets == 0) || (factor <= 0) || (base <= 1)) {
    errno = EINVAL;
    return NULL;
  }

  distribution_t *new_distribution = calloc(1, sizeof(distribution_t));
  bucket_t *buckets = calloc(num_buckets, sizeof(bucket_t));

  if ((new_distribution == NULL) || (buckets == NULL)) {
    free(new_distribution);
    free(buckets);
    return NULL;
  }
  new_distribution->buckets = buckets;

  for (size_t i = 0; i < num_buckets; i++) {
    double min_boundary = (i == 0) ? 0 : new_distribution->buckets[i - 1].max_boundary;
    double max_boundary = (i == num_buckets - 1) ? INFINITY : factor * pow(base, i);
    new_distribution->buckets[i] = initialize_bucket(min_boundary, max_boundary);
  }

  new_distribution->num_buckets = num_buckets;
  new_distribution->total_scalar_count = 0;
  new_distribution->raw_data_sum = 0;
  pthread_mutex_init(&new_distribution->mutex, NULL);
  return new_distribution;
}

distribution_t *distribution_new_custom(size_t num_bounds,
                                        double *custom_max_boundaries) {

  if ((num_bounds == 0) || (custom_max_boundaries == NULL)) {
    errno = EINVAL;
    return NULL;
  }

  for (size_t i = 1; i < num_bounds; i++) {
    if ((custom_max_boundaries[i] <= custom_max_boundaries[i - 1]) ||
        (custom_max_boundaries[i] == INFINITY)) {
      errno = EINVAL;
      return NULL;
    }
  }

  distribution_t *new_distribution = calloc(1, sizeof(distribution_t));
  bucket_t *buckets =
      calloc(num_bounds + 1, sizeof(bucket_t)); //+1 for infinity bucket

  if ((new_distribution == NULL) || (buckets == NULL)) {
    free(new_distribution);
    free(buckets);
    return NULL;
  }
  new_distribution->buckets = buckets;

  for (size_t i = 0; i < num_bounds + 1; i++) {
    double min_boundary = (i == 0) ? 0 : new_distribution->buckets[i - 1].max_boundary;
    double max_boundary = (i == num_bounds) ? INFINITY : custom_max_boundaries[i];
    new_distribution->buckets[i] = initialize_bucket(min_boundary, max_boundary);
  }

  new_distribution->num_buckets =
      num_bounds + 1; // plus one for infinity bucket
  new_distribution->total_scalar_count = 0;
  new_distribution->raw_data_sum = 0;
  pthread_mutex_init(&new_distribution->mutex, NULL);
  return new_distribution;
}

static int find_bucket(distribution_t *dist, size_t left, size_t right,
                            double gauge) {
  if (left > right) {
    return -1;
  }

  int mid = left + (right - left) / 2;
  if (gauge >= dist->buckets[mid].min_boundary &&
      gauge < dist->buckets[mid].max_boundary) {
    return mid;
  }

  if (gauge < dist->buckets[mid].min_boundary) {
    return find_bucket(dist, left, mid - 1, gauge);
  }

  return find_bucket(dist, mid + 1, right, gauge);
}

void distribution_update(distribution_t *dist, double gauge) {
  if ((dist == NULL) || (gauge <= 0)) {
    errno = EINVAL;
    return;
  }
  /*
  for(size_t i = 0; i < dist->num_buckets; i++) {
    if(gauge >= dist->buckets[i].min_boundary && gauge <
  dist->buckets[i].max_boundary) { dist->buckets[i].bucket_counter++;
    }
  }
  */
  size_t left = 0;
  //pthread_mutex_lock(&dist->mutex);
  size_t right = dist->num_buckets - 1;
  int index = find_bucket(dist, left, right, gauge);

  dist->buckets[index].bucket_counter++;
  dist->total_scalar_count++;
  dist->raw_data_sum += gauge;
  //pthread_mutex_lock(&dist->mutex);
  return;
}

double distribution_average(distribution_t *dist) {
  if (dist == NULL) {
    errno = EINVAL;
    return NAN;
  }

  pthread_mutex_lock(&dist->mutex);

  if (dist->total_scalar_count == 0) {
    return NAN;
  }
  double average = dist->raw_data_sum / (double) dist->total_scalar_count;
  pthread_mutex_unlock(&dist->mutex);
  return average;
}

double distribution_percentile(distribution_t *dist, double percent) {
  if ((percent < 0) || (percent > 100) || (dist == NULL)) {
    errno = EINVAL;
    return NAN;
  }
  int sum = 0;
  double bound = 0;
  pthread_mutex_lock(&dist->mutex);
  double target_amount = (percent / 100) * (double) dist->total_scalar_count;
  for (size_t i = 0; i < dist->num_buckets; i++) {
    sum += dist->buckets[i].bucket_counter;
    if ((double)sum >= target_amount) {
      bound = dist->buckets[i].max_boundary;
      break;
    }
  }
  pthread_mutex_unlock(&dist->mutex);
  return bound;
}

distribution_t *distribution_clone(distribution_t *dist) {
  if (dist == NULL) {
    errno = EINVAL;
    return NULL;
  }

  distribution_t *new_distribution = calloc(1, sizeof(distribution_t));

  if (new_distribution == NULL) {
    free(new_distribution);
    return NULL;
  }
  pthread_mutex_lock(&dist->mutex);
  new_distribution->buckets = distribution_get_buckets(dist);
  new_distribution->num_buckets = dist->num_buckets;

  new_distribution->total_scalar_count = dist->total_scalar_count;
  new_distribution->raw_data_sum = dist->raw_data_sum;
  pthread_mutex_init(&new_distribution->mutex, NULL);
  pthread_mutex_unlock(&dist->mutex);
  return new_distribution;
}

void distribution_destroy(distribution_t *dist) {
  if (dist == NULL) {
    return;
  }
  free(dist->buckets);
  free(dist);
}

int distribution_get_num_buckets(distribution_t *dist) {
  if (dist == NULL) {
    errno = EINVAL;
    return -1;
  }

  return dist->num_buckets;
}

int distribution_get_total_scalar_count(distribution_t *dist) {
  if (dist == NULL) {
    errno = EINVAL;
    return -1;
  }
  return dist->total_scalar_count;
}

double distribution_get_raw_data_sum(distribution_t *dist) {
  if (dist == NULL) {
    errno = EINVAL;
    return NAN;
  }
  return dist->raw_data_sum;
}
