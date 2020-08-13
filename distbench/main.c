/* System headers */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if HAVE_CLOCK_GETTIME
#include <time.h>
#else
#include <sys/time.h>
#endif
#include <unistd.h>

#include "distribution.h"
#include <stdio.h>
#include <time.h>
#include <math.h>

/* Macro to exit with an error. */
#define error(fmt, args...)                                                    \
  do {                                                                         \
    fprintf(stderr, "ERROR: " fmt "\n", ##args);                               \
    _exit(1);                                                                  \
  } while (0)

/* How many nanoseconds there are in a second. */
#define NANOS_PER_SEC 10000000

/* How many microseconds there are in a nanosecond. */
#define MICROS_PER_NANO 1000

/* How many iterations to run. */
#define ITERATIONS 10000000

/* Returns the clock in nanoseconds. */
static uint64_t get_clock() {
#if HAVE_CLOCK_GETTIME
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1)
    error("Unable to retrieve monotonic clock: %s", strerror(errno));

  return (ts.tv_sec * NANOS_PER_SECOND) + ts.tv_nsec;
#else
  struct timeval tv;

  if (gettimeofday(&tv, NULL) == -1)
    error("Unable to retrieve current time: %s", strerror(errno));

  return (tv.tv_sec * NANOS_PER_SEC) + (tv.tv_usec * MICROS_PER_NANO);
#endif
}

const size_t iterations = 1000000;
const int dist_number = 3;

static double * calculate_gauges_arr(double gauges[], size_t iterations) {
    for(size_t i = 0; i < iterations; i++) {
        gauges[i] = (double) (rand() % (int) 1e6);
    }
    return gauges;
}

static double * calculate_percents_arr(double percents[], size_t iterations) {
    for(size_t i = 0; i < iterations; i++) {
        percents[i] =  (double) (rand() % 101);
    }
    return percents;
}

static size_t * calculate_dist_index_arr(size_t indexes[], size_t iterations) {
    for(size_t i = 0; i < iterations; i++) {
        indexes[i] = rand() % 3;
    }
    return indexes;
}

double measure_update(distribution_t *dist, size_t iterations, double *gauges) {
    uint64_t start = get_clock();
    for(size_t i = 0; i < iterations; i++) {
        distribution_update(dist, gauges[i]);
    }
    uint64_t end = get_clock();
    double seconds = (end - start) / (double)(NANOS_PER_SEC);
    printf("%f, ", seconds);
    return seconds;
}

double measure_percentile(distribution_t *dist, size_t iterations, double *percents) {
    uint64_t start = get_clock();
    for(size_t i = 0; i < iterations; i++) {
        volatile double res = distribution_percentile(dist, percents[i]);
	(void)res;
    }
    uint64_t end = get_clock();
    double seconds = (end - start) / (double)(NANOS_PER_SEC);
    printf("%f, ", seconds);
    return seconds;
}

double  measure_mixed(distribution_t *dist, size_t iterations, double *percents, double *gauges) {
    uint64_t start = get_clock();
    for(size_t i = 0; i < iterations; i++) {
        if (i % 10 == 0) {
	  volatile double res = distribution_percentile(dist, percents[i]);
	  (void)res;
	}
        else {
	  distribution_update(dist, gauges[i]);
	}
    }
    uint64_t end = get_clock();
    double seconds = (end - start) / (double)(NANOS_PER_SEC);
    printf("%f, ", seconds);
    return seconds;
}

double measure_update_all_dists(distribution_t **dists, size_t iterations, double *gauges, size_t *indexes) {
    uint64_t start = get_clock();
    for(size_t i = 0; i < iterations; i++) {
        distribution_update(dists[indexes[i]], gauges[i]);
    }
    uint64_t end = get_clock();
    double seconds = (end - start) / (double)(NANOS_PER_SEC);
    printf("%f, ", seconds);
    return seconds;
}

double measure_percentile_all_dists(distribution_t **dists, size_t iterations, double *percents, size_t *indexes) {
    uint64_t start = get_clock();
    for(size_t i = 0; i < iterations; i++) {
        volatile double res = distribution_percentile(dists[indexes[i]], percents[i]);
	(void)res;
    }
    uint64_t end = get_clock();
    double seconds = (end - start) / (double)(NANOS_PER_SEC);
    printf("%f, ", seconds);
    return seconds;
}

double measure_mixed_all_dists(distribution_t **dists, size_t iterations, double *percents, double *gauges, size_t *indexes) {
    uint64_t start = get_clock();
    for(size_t i = 0; i < iterations; i++) {
        if (i % 10 == 0) {
	 volatile double res = distribution_percentile(dists[indexes[i]], percents[i]);
	 (void)res;
        } 
        else {
	  distribution_update(dists[indexes[i]], gauges[i]);
	}
    }
    uint64_t end = get_clock();
    double seconds = (end - start) / (double)(NANOS_PER_SEC);
    printf("%f ", seconds);
    return seconds;
}

int main(int argc, char **argv) {
    srand(1770);
    if (argc < 2) {
        printf("No bucket number found.\n");
        exit(EXIT_FAILURE);
    }

    int buckets_number = atoi(argv[1]);
    double buckets_size = 25;

    double *custom = malloc(sizeof(double) * buckets_number - 1);
    if(custom == NULL) {
        printf("Malloc failed.\n");
        exit(EXIT_FAILURE);
    } 

    custom[0] = rand() % (100 + 1);
    for (size_t i = 1; i < buckets_number - 1; i++) {
        custom[i] = custom[i - 1] + rand() % 100 + 1;
    }

    distribution_t *dists[dist_number];
    dists[0] = distribution_new_linear(buckets_number, buckets_size);
    dists[1] = distribution_new_exponential(buckets_number, 3, 2);
    dists[2] = distribution_new_custom(buckets_number - 1, custom);
    free(custom);

    double *gauges = malloc(sizeof(double) * iterations);
    if(gauges == NULL) {
        printf("Malloc failed.\n");
        exit(EXIT_FAILURE);
    } 
    gauges = calculate_gauges_arr(gauges, iterations);

    double *percents = malloc(sizeof(double) * iterations);
    if(percents == NULL) {
        printf("Malloc failed.\n");
        exit(EXIT_FAILURE);
    } 
    percents = calculate_percents_arr(percents, iterations);

    size_t *indexes = malloc(sizeof(size_t) * iterations);
    if(indexes == NULL) {
        printf("Malloc failed.\n");
        exit(EXIT_FAILURE);
    } 
    indexes = calculate_dist_index_arr(indexes, iterations);
    
    /*printf("Number of buckets, 
	    Update linear, Percentile linear, Mixed linear, 
	    Update exponential, Percentile exponential, Mixed exponential, 
	    Update custom, Percentile custom, Mixed custom, 
	    Update all, Percentile all, Mixed all");*/

    printf("%d, ", buckets_number);
    volatile double res = measure_update(dists[0], iterations, gauges);
    res = measure_percentile(dists[0], iterations, percents);
    res = measure_mixed(dists[0], iterations, percents, gauges);

    //printf("%d, ", buckets_number);
    res = measure_update(dists[1], iterations, gauges);
    res = measure_percentile(dists[1], iterations, percents);
    res = measure_mixed(dists[1], iterations, percents, gauges);

    //printf("%d, ", buckets_number);
    res = measure_update(dists[2], iterations, gauges);
    res = measure_percentile(dists[2], iterations, percents);
    res = measure_mixed(dists[2], iterations, percents, gauges);

    //printf("%d, ", buckets_number);
    res = measure_update_all_dists(dists, iterations, gauges, indexes);
    res = measure_percentile_all_dists(dists, iterations, gauges, indexes);
    res = measure_mixed_all_dists(dists, iterations, percents, gauges, indexes);
    (void)res;
    free(gauges);
    free(percents);
    free(indexes);
    for (size_t i = 0; i < dist_number; i++)
       distribution_destroy(dists[i]);
    return 0;
}
