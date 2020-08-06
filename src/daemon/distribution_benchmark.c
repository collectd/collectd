#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#include "distribution.h"

const size_t NUM_UPDATES = 1000000;
const size_t NUM_PERCENTILES = 1000000;
const size_t MIXED = 1000000;

distribution_t *build(size_t num_buckets) {
  srand(5);
  double custom[num_buckets - 1];
  custom[0] = rand() % 100;
  for (size_t i = 1; i < num_buckets - 1; i++) {
   custom[i] = custom[i - 1] + rand() % 100 + 1;
  }
  distribution_t *dist = distribution_new_custom(num_buckets - 1, custom);
  return dist;
}

double calculate_update_time(distribution_t *dist) {
  double updates[NUM_UPDATES];
  for (size_t i = 0; i < NUM_UPDATES; i++) {
    updates[i] = (rand() * RAND_MAX + rand()) % (distribution_num_buckets(dist) * 100);
  }
  struct timespec start, finish;
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (size_t i = 0; i < NUM_UPDATES; i++) {
    distribution_update(dist, updates[i]);
  }
  clock_gettime(CLOCK_MONOTONIC, &finish);
  double update_dur = 1000.0 * (finish.tv_sec - start.tv_sec) + 1e-6 * (finish.tv_nsec - start.tv_nsec);
  double average = update_dur / NUM_UPDATES * 1000000.0;
  return average; 
}

double calculate_percentile_time(distribution_t *dist) {
  double percentiles[NUM_PERCENTILES];
  for (size_t i = 0; i < NUM_PERCENTILES; i++) {
    percentiles[i] = 100.0 * rand() / RAND_MAX;
  }
  struct timespec start, finish;
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (size_t i = 0; i < NUM_PERCENTILES; i++) {
    distribution_percentile(dist, percentiles[i]);
  }
  clock_gettime(CLOCK_MONOTONIC, &finish);
  double percentile_dur = 1000.0 * (finish.tv_sec - start.tv_sec) + 1e-6 * (finish.tv_nsec - start.tv_nsec);
  double average = percentile_dur / NUM_PERCENTILES * 1000000.0;
  return average; 
}

double mixed(size_t num_buckets) {
  distribution_t *dist = build(num_buckets);
  double updates[MIXED / 10 * 9];
  double percentiles[MIXED / 10];
  for (size_t i = 0; i < MIXED / 10 * 9; i++)
   updates[i] = (rand() * RAND_MAX + rand()) % (num_buckets * 100);
  for (size_t i = 0; i < MIXED / 10; i++)
    percentiles[i] = 100.0 * rand() / RAND_MAX;

  size_t uid = 0;
  size_t pid = 0;
  struct timespec start, finish;
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (size_t i = 0; i < MIXED; i++) {
    if (i % 10 == 9) 
      distribution_percentile(dist, percentiles[pid++]);
    else
      distribution_update(dist, updates[uid++]);
  }
  clock_gettime(CLOCK_MONOTONIC, &finish);
  double dur = 1000.0 * (finish.tv_sec - start.tv_sec) + 1e-6 * (finish.tv_nsec - start.tv_nsec);
  distribution_destroy(dist);
  return dur;
}

int main() {
  size_t *bucket_nums = (size_t[]){ 5, 10, 30, 50, 100, 300, 500, 1000};
  for (size_t i = 0; i < 8; i++) {
    distribution_t *dist = build(bucket_nums[i]);
    //printf("%lu %f %f %f\n", bucket_nums[i], calculate_update_time(dist), calculate_percentile_time(dist), mixed(bucket_nums[i]));
    printf("Using %lu buckets one update takes %f ns in average\n", bucket_nums[i], calculate_update_time(dist));
    printf("Using %lu buckets one percentile calculation takes %f ns in average\n", bucket_nums[i], calculate_percentile_time(dist));
    distribution_destroy(dist);
    printf("Using %lu buckets mixed function work in %f ms\n", bucket_nums[i], mixed(bucket_nums[i]));
    printf("\n");
  }
  /*printf("\n");
  for (size_t i = 0; i < 8; i++) {
    printf("Using %lu buckets one percentile calculation  takes %f ns in average\n", bucket_nums[i], calculate_percentile_time(bucket_nums[i]));
  }
  distribution_t *dist = distribution_new_linear(1000, 1);
  struct timespec start, finish;
  clock_gettime(CLOCK_MONOTONIC, &start);
  size_t num_updates = 50000000;
  for (size_t i = 0; i < num_updates; i++) {
    distribution_update(dist, rand());
  }
  clock_gettime(CLOCK_MONOTONIC, &finish);
  double update_dur = 1000.0 * (finish.tv_sec - start.tv_sec) + 1e-6 * (finish.tv_nsec - start.tv_nsec);
  printf("%lu updates takes %f ms\n", num_updates, update_dur);
  size_t num_requests = 50000000;
  start = clock();
  for (size_t i = 0; i < num_requests; i++) {
    distribution_percentile(dist, 100.0 * rand() / RAND_MAX);
  }
  finish = clock();
  double request_duration = 1000.0 * (finish - start) / CLOCKS_PER_SEC;
  printf("%lu percentile requests takes %f ms\n", num_requests, request_duration);
  distribution_destroy(dist);*/
  return 0;
}
