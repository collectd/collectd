/**
 * collectd - src/daemon/distribution.h
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
 * Authors:
 *   Svetlana Shmidt <sshmidt at google.com>
 **/

#ifndef COLLECTD_DISTRIBUTION_H
#define COLLECTD_DISTRIBUTION_H

#include"collectd.h"

typedef struct bucket_s {
  uint64_t bucket_counter;
  double maximum;
} bucket_t;

struct distribution_s;
typedef struct distribution_s distribution_t;

typedef struct buckets_array_s {
  size_t num_buckets;
  bucket_t *buckets;
} buckets_array_t;

//constructor functions:
/**
 * function creates new distribution with the linear buckets:
 * [0; size) [size; 2 * size) ... [(num_buckets - 1) * size; infinity)
 * @param num_buckets - number of buckets. Should be greater than 0
 * @param size - size of each bucket. Should be greater than 0
 * @return - pointer to a new distribution or null pointer if parameters are wrong or memory allocation fails
 */
distribution_t* distribution_new_linear(size_t num_buckets, double size);

/**
 * function creates new distribution with the exponential buckets:
 * [0; factor) [factor; factor * base) ... [factor * base^{num_buckets - 2}; infinity)
 * @param num_buckets - number of buckets. Should be greater than 0
 * @param base - base of geometric progression. Should be greater than 1
 * @param factor - size of the first bucket. Should be greater than 0
 * @return - pointer to a new distribution or null pointer if parameters are wrong or memory allocation fails
 */
distribution_t* distribution_new_exponential(size_t num_buckets, double base, double factor);

/**
 * function creates new distribution with the custom buckets:
 * [0; custom_bucket_boundaries[0]) [custom_bucket_boundaries[0]; custom_bucket_boundaries[1]) ...
 * ... [custom_bucket_boundaries[array_size - 1], infinity)
 * @param array_size - size of array of bucket boundaries. Number of buckets is array_size + 1
 * @param custom_buckets_boundaries - array with bucket boundaries. Should be increasing and positive
 * @return - pointer to a new distribution or null pointer if parameters are wrong or memory allocation fails
 */
distribution_t* distribution_new_custom(size_t array_size, double *custom_buckets_boundaries);

/** add new value to a distribution **/
void distribution_update(distribution_t *dist, double gauge);

/**
 * @param percent - should be in (0; 100] range
 * @return - an approximation of percent percentile
 * (upper bound of such bucket that all less or equal buckets contain more than percent percents of values)
 * or NAN if parameters are wrong or distribution is empty
 */
double distribution_percentile(distribution_t *dist, double percent);

/** @return - average of all values in distribution or NAN if distribution is empty */
double distribution_average(distribution_t *dist);

/** @return - pointer to the copy of distribution or null if memory allocation fails */
distribution_t* distribution_clone(distribution_t *dist);

/** destroy the distribution and free memory **/
void distribution_destroy(distribution_t *d);

/** @return - number of buckets stored in the distribution **/
size_t distribution_num_buckets(distribution_t *dist);

/** @return - array of buckets in the distribution **/
buckets_array_t get_buckets(distribution_t *dist);

void destroy_buckets_array(buckets_array_t buckets_array);

#endif // COLLECTD_DISTRIBUTION_H
