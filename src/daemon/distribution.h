//
// Created by Svetlana Shmidt on 24.07.2020.
//

#ifndef COLLECTD_DISTRIBUTION_H
#define COLLECTD_DISTRIBUTION_H

#include <stdlib.h>

struct distribution_s;
typedef struct distribution_s distribution_t;

//constructor functions:
distribution_t* distribution_new_linear(size_t num_buckets, double size);
distribution_t* distribution_new_exponential(size_t num_buckets, double initial_size, double factor);
distribution_t* distribution_new_custom(size_t num_buckets, double *custom_buckets_sizes);

void distribution_update(distribution_t *dist, double gauge);
double distribution_percentile(distribution_t *dist, double percent);
double distribution_average(distribution_t *dist);
distribution_t* distribution_clone(distribution_t *dist);
void distribution_destroy(distribution_t *d);

#endif // COLLECTD_DISTRIBUTION_H
