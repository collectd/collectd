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
 * Authors:
 *   Svetlana Shmidt <sshmidt at google.com>
 **/

#include "distribution.h"

#include <pthread.h>

struct distribution_s {
  bucket_t *tree;
  size_t num_buckets;
  double total_sum;
  double total_square_sum; // the sum of squares of gauges. We'll need it to
                           // calculate sum of squared deviations
  pthread_mutex_t mutex;
};

/**
 * This code uses an Euler path to avoid gaps in the tree-to-array mapping.
 * This way the tree contained N buckets contains 2 * N - 1 nodes
 * Thus, left subtree has 2 * (mid - left + 1) - 1 nodes,
 * therefore the right subtree starts at node_index + 2 * (mid - left + 1).
 * For a detailed explanation, see
 * https://docs.google.com/document/d/1ccsg5ffUfqt9-mBDGTymRn8X-9Wk1CuGYeMlRxmxiok/edit?usp=sharing".
 */

static size_t left_child_index(size_t node_index,
                               __attribute__((unused)) size_t left,
                               __attribute__((unused)) size_t right) {
  return node_index + 1;
}

static size_t right_child_index(size_t node_index, size_t left, size_t right) {
  size_t mid = (left + right) / 2;
  return node_index + 2 * (mid - left + 1);
}

static size_t tree_size(size_t num_buckets) { return 2 * num_buckets - 1; }

static bucket_t merge_buckets(bucket_t left_child, bucket_t right_child) {
  return (bucket_t){
      .bucket_counter = left_child.bucket_counter + right_child.bucket_counter,
      .maximum = right_child.maximum,
  };
}

static void build_tree(distribution_t *d, bucket_t *buckets, size_t node_index,
                       size_t left, size_t right) {
  if (left > right)
    return;
  if (left == right) {
    d->tree[node_index] = buckets[left];
    return;
  }
  size_t mid = (left + right) / 2;
  size_t left_child = left_child_index(node_index, left, right);
  size_t right_child = right_child_index(node_index, left, right);
  build_tree(d, buckets, left_child, left, mid);
  build_tree(d, buckets, right_child, mid + 1, right);
  d->tree[node_index] =
      merge_buckets(d->tree[left_child], d->tree[right_child]);
}

static distribution_t *
build_distribution_from_bucket_array(size_t num_buckets,
                                     bucket_t *bucket_array) {
  distribution_t *new_distribution = calloc(1, sizeof(*new_distribution));
  bucket_t *nodes = calloc(tree_size(num_buckets), sizeof(*nodes));
  if (new_distribution == NULL || nodes == NULL) {
    free(new_distribution);
    free(nodes);
    return NULL;
  }
  new_distribution->tree = nodes;

  new_distribution->num_buckets = num_buckets;
  build_tree(new_distribution, bucket_array, 0, 0, num_buckets - 1);
  pthread_mutex_init(&new_distribution->mutex, NULL);
  return new_distribution;
}

distribution_t *distribution_new_linear(size_t num_buckets, double size) {
  if (num_buckets == 0 || size <= 0) {
    errno = EINVAL;
    return NULL;
  }

  bucket_t bucket_array[num_buckets];
  for (size_t i = 0; i < num_buckets; i++) {
    bucket_array[i] = (bucket_t){
        .bucket_counter = 0,
        .maximum = (i == num_buckets - 1) ? INFINITY : (i + 1) * size,
    };
  }
  return build_distribution_from_bucket_array(num_buckets, bucket_array);
}

distribution_t *distribution_new_exponential(size_t num_buckets, double base,
                                             double factor) {
  if (num_buckets == 0 || base <= 1 || factor <= 0) {
    errno = EINVAL;
    return NULL;
  }

  bucket_t bucket_array[num_buckets];
  for (size_t i = 0; i < num_buckets; i++) {
    bucket_array[i] = (bucket_t){
        .bucket_counter = 0,
        .maximum = (i == num_buckets - 1)
                       ? INFINITY
                       : factor * pow(base, i), // check if it's slow
    };
  }
  return build_distribution_from_bucket_array(num_buckets, bucket_array);
}

distribution_t *distribution_new_custom(size_t array_size,
                                        double *custom_buckets_boundaries) {
  for (size_t i = 0; i < array_size; i++) {
    double previous_boundary = 0;
    if (i > 0) {
      previous_boundary = custom_buckets_boundaries[i - 1];
    }
    if (custom_buckets_boundaries[i] <= previous_boundary) {
      errno = EINVAL;
      return NULL;
    }
  }
  if (array_size > 0 && custom_buckets_boundaries[array_size - 1] == INFINITY) {
    errno = EINVAL;
    return NULL;
  }

  size_t num_buckets = array_size + 1;
  bucket_t bucket_array[num_buckets];
  for (size_t i = 0; i < num_buckets; i++) {
    bucket_array[i] = (bucket_t){
        .bucket_counter = 0,
        .maximum =
            (i == num_buckets - 1) ? INFINITY : custom_buckets_boundaries[i],
    };
  }
  return build_distribution_from_bucket_array(num_buckets, bucket_array);
}

void distribution_destroy(distribution_t *d) {
  if (d == NULL)
    return;
  pthread_mutex_destroy(&d->mutex);
  free(d->tree);
  free(d);
}

distribution_t *distribution_clone(distribution_t *dist) {
  if (dist == NULL)
    return NULL;
  distribution_t *new_distribution = calloc(1, sizeof(*new_distribution));
  bucket_t *nodes = calloc(tree_size(dist->num_buckets), sizeof(*nodes));
  if (new_distribution == NULL || nodes == NULL) {
    free(new_distribution);
    free(nodes);
    return NULL;
  }
  pthread_mutex_lock(&dist->mutex);
  memcpy(nodes, dist->tree, tree_size(dist->num_buckets) * sizeof(bucket_t));
  new_distribution->num_buckets = dist->num_buckets;
  new_distribution->total_sum = dist->total_sum;
  new_distribution->total_square_sum = dist->total_square_sum;
  pthread_mutex_unlock(&dist->mutex);
  new_distribution->tree = nodes;
  pthread_mutex_init(&new_distribution->mutex, NULL);
  return new_distribution;
}

static void update_tree(distribution_t *dist, size_t node_index, size_t left,
                        size_t right, double gauge) {
  if (left > right)
    return;
  dist->tree[node_index].bucket_counter++;
  if (left == right) {
    return;
  }
  size_t mid = (left + right) / 2;
  size_t left_child = left_child_index(node_index, left, right);
  size_t right_child = right_child_index(node_index, left, right);
  if (dist->tree[left_child].maximum > gauge)
    update_tree(dist, left_child, left, mid, gauge);
  else
    update_tree(dist, right_child, mid + 1, right, gauge);
}

void distribution_update(distribution_t *dist, double gauge) {
  if (dist == NULL)
    return;
  if (gauge < 0) {
    errno = EINVAL;
    return;
  }
  pthread_mutex_lock(&dist->mutex);
  update_tree(dist, 0, 0, dist->num_buckets - 1, gauge);
  dist->total_sum += gauge;
  dist->total_square_sum += gauge * gauge;
  pthread_mutex_unlock(&dist->mutex);
}

static double tree_get_counter(distribution_t *d, size_t node_index,
                               size_t left, size_t right, uint64_t counter) {
  if (left > right)
    return NAN;
  if (left == right) {
    return d->tree[node_index].maximum;
  }
  size_t mid = (left + right) / 2;
  size_t left_child = left_child_index(node_index, left, right);
  size_t right_child = right_child_index(node_index, left, right);
  if (d->tree[left_child].bucket_counter >= counter)
    return tree_get_counter(d, left_child, left, mid, counter);
  else
    return tree_get_counter(d, right_child, mid + 1, right,
                            counter - d->tree[left_child].bucket_counter);
}

double distribution_percentile(distribution_t *dist, double percent) {
  if (percent < 0 || percent > 100 || dist == NULL) {
    errno = EINVAL;
    return NAN;
  }
  pthread_mutex_lock(&dist->mutex);
  if (dist->tree[0].bucket_counter == 0) {
    pthread_mutex_unlock(&dist->mutex);
    return NAN;
  }
  uint64_t counter = ceil(dist->tree[0].bucket_counter * percent / 100.0);
  double percentile =
      tree_get_counter(dist, 0, 0, dist->num_buckets - 1, counter);
  pthread_mutex_unlock(&dist->mutex);
  return percentile;
}

double distribution_average(distribution_t *dist) {
  if (dist == NULL)
    return NAN;
  pthread_mutex_lock(&dist->mutex);
  if (dist->tree[0].bucket_counter == 0) {
    pthread_mutex_unlock(&dist->mutex);
    return NAN;
  }
  double average = dist->total_sum / dist->tree[0].bucket_counter;
  pthread_mutex_unlock(&dist->mutex);
  return average;
}

size_t distribution_num_buckets(distribution_t *dist) {
  if (dist == NULL)
    return 0;
  return dist->num_buckets;
}

static void tree_write_leave_buckets(distribution_t *dist, bucket_t *write_ptr,
                                     size_t node_index, size_t left,
                                     size_t right) {
  if (left > right)
    return;
  if (left == right) {
    write_ptr[left] = dist->tree[node_index];
    return;
  }
  size_t mid = (left + right) / 2;
  size_t left_child = left_child_index(node_index, left, right);
  size_t right_child = right_child_index(node_index, left, right);
  tree_write_leave_buckets(dist, write_ptr, left_child, left, mid);
  tree_write_leave_buckets(dist, write_ptr, right_child, mid + 1, right);
}

buckets_array_t get_buckets(distribution_t *dist) {
  buckets_array_t bucket_array = {
      .num_buckets = dist == NULL ? 0 : dist->num_buckets,
      .buckets = dist == NULL
                     ? NULL
                     : calloc(dist->num_buckets, sizeof(*bucket_array.buckets)),
  };
  if (dist == NULL)
    return bucket_array;
  bucket_t *write_ptr = bucket_array.buckets;
  pthread_mutex_lock(&dist->mutex);
  tree_write_leave_buckets(dist, write_ptr, 0, 0, dist->num_buckets - 1);
  pthread_mutex_unlock(&dist->mutex);
  return bucket_array;
}

void destroy_buckets_array(buckets_array_t buckets_array) {
  free(buckets_array.buckets);
}

double distribution_total_sum(distribution_t *dist) {
  if (dist == NULL) {
    return NAN;
  }
  return dist->total_sum; // should I add mutex here?
}

uint64_t distribution_total_counter(distribution_t *dist) {
  if (dist == NULL) {
    return EINVAL;
  }
  return dist->tree[0].bucket_counter; // should I add mutex here?
}

double distribution_squared_deviation_sum(distribution_t *dist) {
  if (dist == NULL) {
    return NAN;
  }
  double mean = distribution_average(dist);
  pthread_mutex_lock(&dist->mutex);
  double squared_deviation_sum =
      mean * mean * (double)distribution_total_counter(dist) -
      2 * mean * dist->total_sum + dist->total_square_sum;
  pthread_mutex_unlock(&dist->mutex);
  return squared_deviation_sum;
}

static int compare_longint(uint64_t a, uint64_t b) {
  return a == b ? 0 : a < b ? -1 : 1;
}

/** return a pointer to array int[2].
 * The first value is error code or 0 if succeed.
 * The second value is result of comparison
 */
static int *distribution_cmp(distribution_t *d1, distribution_t *d2) {
  int *comparison = calloc(2, sizeof(*comparison));
  if (comparison == NULL) {
    return NULL;
  }
  if (d1 == NULL || d2 == NULL) {
    comparison[0] = EINVAL;
    return comparison;
  }
  if (d1->num_buckets != d2->num_buckets) {
    comparison[0] = EINVAL;
    return comparison;
  }
  for (size_t i = 0; i < tree_size(d1->num_buckets); i++) {
    if (d1->tree[i].maximum != d2->tree[i].maximum) { // will it work with doubles?
      comparison[0] = EINVAL;
      return comparison;
    }
  }

  int res = compare_longint(d1->tree[0].bucket_counter, d2->tree[0].bucket_counter);
  for (size_t i = 1; i < tree_size(d1->num_buckets); i++) {
    int cur_res = compare_longint(d1->tree[i].bucket_counter, d2->tree[i].bucket_counter);
    if (res != cur_res) {
      if (cur_res == 0)
        continue;
      if (res == 0) {
        res = cur_res;
        continue;
      }
      comparison[0] = ERANGE;
      break;
    }
  }
  comparison[1] = res;
  return comparison;
}

bool distribution_equal(distribution_t *d1, distribution_t *d2) {
  pthread_mutex_lock(&d1->mutex);
  pthread_mutex_lock(&d2->mutex);
  int *cmp = distribution_cmp(d1, d2);
  bool ans = (cmp[0] == 0 && cmp[1] == 0);
  pthread_mutex_unlock(&d2->mutex);
  pthread_mutex_unlock(&d1->mutex);
  free(cmp);
  return ans;
}

/* TODO(bkjg): add tests for this function */
int distribution_sub(distribution_t *d1, distribution_t *d2) {
  pthread_mutex_lock(&d1->mutex);
  pthread_mutex_lock(&d2->mutex);

  int *cmp_status = distribution_cmp(d1, d2);
  if (cmp_status[0] != 0 || cmp_status[1] == -1) { // i.e. d1 < d2 or can't compare
    int status = cmp_status[0];
    if (cmp_status[1] == -1)
      status = ERANGE;
    pthread_mutex_unlock(&d2->mutex);
    pthread_mutex_unlock(&d1->mutex);
    free(cmp_status);
    return status;
  }

  d1->total_sum -= d2->total_sum;
  d1->total_square_sum -= d2->total_square_sum;
  for (size_t i = 0; i < tree_size(d1->num_buckets); i++) {
    d1->tree[i].bucket_counter -= d2->tree[i].bucket_counter;
  }

  pthread_mutex_unlock(&d2->mutex);
  pthread_mutex_unlock(&d1->mutex);
  free(cmp_status);
  return 0;
}

/*int distribution_sub(distribution_t *d1, distribution_t *d2) {
  if (d1 == NULL || d2 == NULL) {
    return errno = EINVAL;
  }

  if (d1->num_buckets != d2->num_buckets) {
    return errno = EINVAL;
  }

  pthread_mutex_lock(&d1->mutex);
  pthread_mutex_lock(&d2->mutex);

  if (d1->total_sum < d2->total_sum) {
    pthread_mutex_unlock(&d1->mutex);
    pthread_mutex_unlock(&d2->mutex);
    return ERANGE;
  } else {
    d1->total_sum -= d2->total_sum;
    for (size_t i = 0; i < tree_size(d1->num_buckets); ++i) {
      if (d1->tree[i].maximum != d2->tree[i].maximum ||
          d1->tree[i].bucket_counter < d2->tree[i].bucket_counter) {
        pthread_mutex_unlock(&d2->mutex);
        pthread_mutex_unlock(&d1->mutex);
        return errno = EINVAL;
      }

      d1->tree[i].bucket_counter -= d2->tree[i].bucket_counter;
    }
  }

  pthread_mutex_unlock(&d2->mutex);
  pthread_mutex_unlock(&d1->mutex);

  return 0;
}*/
