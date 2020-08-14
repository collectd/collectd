#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "distribution.h"

const size_t NUM_UPDATES = 1e6;
const size_t NUM_PERCENTILES = 1e6;
const size_t MIXED = 1e6;

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
  double *updates = calloc(NUM_UPDATES, sizeof(*updates));
  for (size_t i = 0; i < NUM_UPDATES; i++) {
    updates[i] =
        (rand() * RAND_MAX + rand()) % (distribution_num_buckets(dist) * 100);
  }
  struct timespec start, finish;
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (size_t i = 0; i < NUM_UPDATES; i++) {
    distribution_update(dist, updates[i]);
  }
  clock_gettime(CLOCK_MONOTONIC, &finish);
  double update_dur = 1000.0 * (finish.tv_sec - start.tv_sec) +
                      1e-6 * (finish.tv_nsec - start.tv_nsec);
  double average = update_dur / NUM_UPDATES * 1000000.0;
  free(updates);
  return average;
}

double calculate_percentile_time(distribution_t *dist) {
  double *percentiles = calloc(NUM_PERCENTILES, sizeof(*percentiles));
  for (size_t i = 0; i < NUM_PERCENTILES; i++) {
    percentiles[i] = 100.0 * rand() / RAND_MAX;
  }
  struct timespec start, finish;
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (size_t i = 0; i < NUM_PERCENTILES; i++) {
    distribution_percentile(dist, percentiles[i]);
  }
  clock_gettime(CLOCK_MONOTONIC, &finish);
  double percentile_dur = 1000.0 * (finish.tv_sec - start.tv_sec) +
                          1e-6 * (finish.tv_nsec - start.tv_nsec);
  double average = percentile_dur / NUM_PERCENTILES * 1000000.0;
  free(percentiles);
  return average;
}

double mixed(size_t num_buckets) {
  distribution_t *dist = build(num_buckets);
  double *updates = calloc(MIXED / 10 * 9, sizeof(*updates));
  double *percentiles = calloc(MIXED / 10, sizeof(*percentiles));
  for (size_t i = 0; i < MIXED / 10 * 9; i++)
    updates[i] = (rand() * RAND_MAX + rand()) % (num_buckets * 100);
  for (size_t i = 0; i < MIXED / 10; i++)
    percentiles[i] = 100.0 * rand() / RAND_MAX;

  size_t uid = 0;
  size_t pid = 0;
  struct timespec start, finish;
  clock_gettime(CLOCK_MONOTONIC, &start);
  double val = 0;
  for (size_t i = 0; i < MIXED; i++) {
    if (i % 10 == 9) {
      double d = distribution_percentile(dist, percentiles[pid++]);
      if (d != INFINITY)
        val += d;
    } else
      distribution_update(dist, updates[uid++]);
  }
  clock_gettime(CLOCK_MONOTONIC, &finish);
  double dur = 1000.0 * (finish.tv_sec - start.tv_sec) +
               1e-6 * (finish.tv_nsec - start.tv_nsec);
  distribution_destroy(dist);
  free(percentiles);
  free(updates);
  // printf("%f\n", val);
  return dur;
}

int main() {
  FILE *fout = fopen("benchmark_small.csv", "w");
  fprintf(fout,
          "Number of buckets,Average for update,Average for percentile,Total "
          "for %lu mixed iterations\n",
          MIXED);
  for (size_t num_buckets = 50; num_buckets <= 5000; num_buckets += 50) {
    distribution_t *dist = build(num_buckets);
    fprintf(fout, "%lu,", num_buckets);
    fprintf(fout, "%f,", calculate_update_time(dist));
    fprintf(fout, "%f,", calculate_percentile_time(dist));
    fprintf(fout, "%f\n", mixed(num_buckets));
    // fprintf(fout, "%lu,%f,%f,%f\n", num_buckets, calculate_update_time(dist),
    // calculate_percentile_time(dist), mixed(num_buckets));
    distribution_destroy(dist);
    printf("OK %lu\n", num_buckets);
  }
  fclose(fout);
  return 0;
}
