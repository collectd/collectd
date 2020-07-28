//
// Created by Svetlana Shmidt on 24.07.2020.
//

#include "distribution.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct bucket_s {
  uint64_t bucket_counter;
  double minimum, maximum;
} bucket_t;

struct distribution_s {
  bucket_t *tree;
  size_t num_buckets;
  double total_sum;
};

static size_t left_child_index(size_t node_index, size_t left, size_t right) {
  return node_index + 1;
}

static size_t right_child_index(size_t node_index, size_t left, size_t right) {
  size_t mid = (left + right) / 2;
  return node_index + 2 * (mid - left + 1);
}

static bucket_t merge_buckets(bucket_t left_child, bucket_t right_child) {
  return (bucket_t) {
      .bucket_counter = left_child.bucket_counter + right_child.bucket_counter,
      .minimum = left_child.minimum,
      .maximum = right_child.maximum,
  };
}

static void build_tree(distribution_t *d, bucket_t *buckets, size_t node_index, size_t left, size_t right) {
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
  d->tree[node_index] = merge_buckets(d->tree[left_child], d->tree[right_child]);
}

static distribution_t* build_distribution_from_bucket_array(size_t num_buckets, bucket_t *bucket_array) {
  distribution_t *new_distribution = calloc(1, sizeof(distribution_t));
  if (new_distribution == NULL) {
    return NULL;
  }
  new_distribution->tree = calloc(2 * num_buckets - 1, sizeof(bucket_t));
  if (new_distribution->tree == NULL) {
    free(new_distribution);
    return NULL;
  }

  new_distribution->num_buckets = num_buckets;
  build_tree(new_distribution, bucket_array, 0, 0, num_buckets - 1);
  return new_distribution;
}

distribution_t* distribution_new_linear(size_t num_buckets, double size) {
  if (num_buckets == 0 || size <= 0) {
    errno = EINVAL;
    return NULL;
  }

  bucket_t *bucket_array = calloc(num_buckets, sizeof(bucket_t));
  if (bucket_array == NULL)
    return NULL;

  for (size_t i = 0; i < num_buckets; i++) {
    bucket_array[i].minimum = i * size;
    if (i == num_buckets - 1)
      bucket_array[i].maximum = INFINITY;
    else
      bucket_array[i].maximum = (i + 1) * size;
  }

  distribution_t *new_distribution = build_distribution_from_bucket_array(num_buckets, bucket_array);
  free(bucket_array);
  return new_distribution;
}

distribution_t* distribution_new_exponential(size_t num_buckets, double initial_size, double factor) {
  if (num_buckets == 0 || initial_size <= 0 || factor <= 0) {
    errno = EINVAL;
    return NULL;
  }

  bucket_t *bucket_array = calloc(num_buckets, sizeof(bucket_t));
  if (bucket_array == NULL)
    return NULL;

  bucket_array[0] = (bucket_t) {
    .maximum = initial_size,
  };
  for (size_t i = 1; i < num_buckets; i++) {
    bucket_array[i].minimum = bucket_array[i - 1].maximum;
    if (i == num_buckets - 1)
      bucket_array[i].maximum = INFINITY;
    else
      bucket_array[i].maximum = bucket_array[i].minimum * factor;
  }

  distribution_t *new_distribution = build_distribution_from_bucket_array(num_buckets, bucket_array);
  free(bucket_array);
  return new_distribution;
}

distribution_t* distribution_new_custom(size_t num_buckets, double *custom_buckets_sizes) {
  if (num_buckets == 0) {
    errno = EINVAL;
    return NULL;
  }
  for (size_t i = 0; i < num_buckets; i++)
    if (custom_buckets_sizes[i] <= 0) {
      errno = EINVAL;
      return NULL;
    }

  bucket_t *bucket_array = calloc(num_buckets + 1, sizeof(bucket_t));
  if (bucket_array == NULL)
    return NULL;

  for (size_t i = 0; i < num_buckets + 1; i++) {
    if (i != 0)
      bucket_array[i].minimum = bucket_array[i - 1].maximum;
    if (i == num_buckets)
      bucket_array[i].maximum = INFINITY;
    else
      bucket_array[i].maximum = bucket_array[i].minimum + custom_buckets_sizes[i];
  }

  distribution_t *new_distribution = build_distribution_from_bucket_array(num_buckets + 1, bucket_array);
  free(bucket_array);
  return new_distribution;
}

void distribution_destroy(distribution_t *d) {
  if (d == NULL)
    return;
  free(d->tree);
  free(d);
}

distribution_t* distribution_clone(distribution_t *dist) {
  if (dist == NULL)
    return NULL;
  distribution_t *new_distribution = calloc(1, sizeof(distribution_t));
  new_distribution->num_buckets = dist->num_buckets;
  memcpy(new_distribution->tree, dist->tree, sizeof(bucket_t) * (2 * dist->num_buckets - 1));
  return new_distribution;
}

static void update_tree(distribution_t *dist, size_t node_index, size_t left, size_t right, double gauge) {
  if (left > right)
    return;
  if (left == right) {
    dist->tree[node_index].bucket_counter++;
    return;
  }
  size_t mid = (left + right) / 2;
  size_t left_child = left_child_index(node_index, left, right);
  size_t right_child = right_child_index(node_index, left, right);
  if (dist->tree[left_child].maximum > gauge)
    update_tree(dist, left_child, left, mid, gauge);
  else
    update_tree(dist, right_child, mid + 1, right, gauge);
  dist->tree[node_index].bucket_counter++;
}

void distribution_update(distribution_t *dist, double gauge) {
  if (dist == NULL)
    return;
  update_tree(dist, 0, 0, dist->num_buckets - 1, gauge);
  dist->total_sum += gauge;
}

double distribution_average(distribution_t *dist) {
  return dist->total_sum / dist->tree[0].bucket_counter;
}

int main() {
  double a[] = {3.0, 2.7, 1.0};
  distribution_t *p = distribution_new_custom(3, a);
  distribution_update(p, 2);
  distribution_update(p, 5);
  distribution_update(p, 7.5);
  distribution_update(p, 3.1);
  for (size_t i = 0; i < 7; i++) {
    printf("%f %f %llu\n", p->tree[i].minimum, p->tree[i].maximum, p->tree[i].bucket_counter);
  }
  printf("%f\n", distribution_average(p));
  distribution_destroy(p);
}