/**
 * collectd - src/daemon/utils_cache_test.c
 * Copyright (C) 2020       Barbara bkjg Kaczorowska
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

#include "collectd.h"

#include "distribution.h"
#include "testing.h"
#include "utils_cache.h"

/* TODO(bkjg): change all fams to pointers */
static metric_family_t create_metric_family_test1(char *name) {
  metric_family_t fam = {
      .name = name,
      .type = METRIC_TYPE_DISTRIBUTION,
  };

  metric_t m[] = {{
                      .family = &fam,
                      .value.distribution = distribution_new_linear(5, 23),
                      .time = cdtime_mock++,
                  },
                  {
                      .family = &fam,
                      .value.distribution = distribution_new_linear(5, 23),
                      .time = cdtime_mock++,
                  },
                  {
                      .family = &fam,
                      .value.distribution = distribution_new_linear(5, 23),
                      .time = cdtime_mock++,
                  }};

  for (int i = 0; i < (sizeof(m) / sizeof(m[0])); ++i) {
    metric_family_metric_append(&fam, m[i]);
  }

  return fam;
}

DEF_TEST(uc_update) {
  struct {
    int want_get;
    double **updates;
    int *num_updates;
    metric_family_t fam;
  } cases[] = {
      {
          .want_get = 0,
          .updates =
              (double *[]){
                  (double[]){5.432, 5.4234, 6.3424, 7.5453, 902.3525,
                             52352.523523, 523524.524524, 90342.4325, 136.352,
                             90.8},
                  (double[]){5.432, 5.4234, 6.3424, 7.5453, 902.3525,
                             52352.523523, 523524.524524, 90342.4325, 136.352,
                             90.8, 1.214, 90432.31434, 43141.1342, 41412.4314,
                             97.47, 90.78},
                  (double[]){
                      5.432,      5.4234,       6.3424,        7.5453,
                      902.3525,   52352.523523, 523524.524524, 90342.4325,
                      136.352,    90.8,         1.214,         90432.31434,
                      43141.1342, 41412.4314,   97.47,         90.78,
                      78.57,      90.467,       89.658,        879.67},
              },
          .num_updates = (int[]){10, 16, 20},
          .fam = create_metric_family_test1("test1"),
      },
  };
  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); ++i) {
    printf("## Case %zu:\n", i);
    uc_init();
    for (int k = 0; k < cases[i].fam.metric.num; ++k) {
      for (int j = 0; j < cases[i].num_updates[k]; ++j) {
        CHECK_ZERO(
            distribution_update(cases[i].fam.metric.ptr[k].value.distribution,
                                cases[i].updates[k][j]));
      }
    }

    /* TODO(bkjg): check if it was it updated for sure, for example: check if we
     * can find it in the avl tree or so */
    EXPECT_EQ_INT(cases[i].want_get, uc_update(&cases[i].fam));

    CHECK_ZERO(metric_family_metric_reset(&cases[i].fam));
  }

  return 0;
}

DEF_TEST(uc_get_percentile_by_name) { return 0; }

static metric_family_t *
create_metric_family_for_get_percentile_test1(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_GAUGE;

  metric_t m = {
      .value.gauge = 6.0,
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);
  return fam;
}

static metric_family_t *
create_metric_family_for_get_percentile_test2(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_COUNTER;

  metric_t m = {
      .value.counter = 45,
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);

  return fam;
}

static metric_family_t *
create_metric_family_for_get_percentile_test3(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_UNTYPED;

  metric_t m = {
      .value.gauge = 4.9,
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);

  return fam;
}

static metric_family_t *
create_metric_family_for_get_percentile_test4(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m = {
      .value.distribution = NULL,
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);

  return fam;
}

static metric_family_t *
create_metric_family_for_get_percentile_test5(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m = {
      .value.distribution = distribution_new_exponential(56, 1.24, 5),
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);

  return fam;
}

static metric_family_t *
create_metric_family_for_get_percentile_test6(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m = {
      .value.distribution = distribution_new_custom(
          8, (double[]){1.54, 3.543, 7.234, 78.435, 285.5435, 9023.53453,
                        100043.43, 900000.43}),
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);

  return fam;
}

static metric_family_t *
create_metric_family_for_get_percentile_test7(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m = {
      .value.distribution = distribution_new_linear(68, 84.543),
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);

  return fam;
}

static metric_family_t *
create_metric_family_for_get_percentile_test8(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m = {
      .value.distribution = distribution_new_exponential(24, 1.345, 9.67),
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);

  return fam;
}

static metric_family_t *
create_metric_family_for_get_percentile_test9(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m = {
      .value.distribution = distribution_new_custom(
          49,
          (double[]){283.11051,  341.94139,  512.86531,  604.69627,  657.38026,
                     868.54382,  1057.70293, 1441.49331, 1499.54159, 2011.40738,
                     2135.40374, 2411.07421, 2658.56919, 2771.7077,  2913.22869,
                     3171.10203, 3734.33039, 4004.57893, 4194.52351, 4209.34402,
                     4395.85013, 4413.95106, 4575.35795, 4827.7257,  5012.33716,
                     5579.60919, 5857.40106, 6154.67381, 6167.46927, 6183.56511,
                     6247.11633, 6548.84595, 6798.12567, 6915.32327, 6975.11549,
                     7010.67086, 7102.20424, 7296.39035, 7599.84557, 7621.5989,
                     8055.85652, 8514.09805, 8786.67945, 8814.77247, 9421.52142,
                     9584.05069, 9618.27028, 9788.40721, 9862.77031}),
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);

  return fam;
}

DEF_TEST(uc_get_percentile) {
  struct {
    int want_get;
    double percent;
    gauge_t ret_value;
    gauge_t want_ret_value;
    metric_family_t *fam;
    double *updates;
    int num_updates;
  } cases[] = {
      {
          /* TODO(bkjg): maybe change the return value to EINVAL when the
             argument is wrong instead of setting errno and returning -1 */
          .fam = create_metric_family_for_get_percentile_test1("test1"),
          .want_get = -1,
          .percent = 57.34,
      },
      {
          .fam = create_metric_family_for_get_percentile_test2("test2"),
          .want_get = -1,
          .percent = 49.23,
      },
      {
          .fam = create_metric_family_for_get_percentile_test3("test3"),
          .want_get = -1,
          .percent = 23.54,
      },
      {
          .fam = create_metric_family_for_get_percentile_test4("test4"),
          .percent = 89.73,
          .want_ret_value = NAN,
      },
      {
          .fam = create_metric_family_for_get_percentile_test5("test5"),
          .want_get = -1,
          .percent = -76,
      },
      {
          .fam = create_metric_family_for_get_percentile_test6("test6"),
          .want_get = -1,
          .percent = 100.4,
      },
      {
          .fam = create_metric_family_for_get_percentile_test7("test7"),
          .percent = 0,
          .num_updates = 9,
          .updates = (double[]){1.354, 4.2343, 67.543, 7243.2435, 543.2543,
                                54.43, 543.534, 9023.534, 2358453.534534},
                                .want_ret_value = 84.543,
      },
      {
          .fam = create_metric_family_for_get_percentile_test8("test8"),
          .percent = 100,
          .num_updates = 15,
          .updates = (double[]){7273.23889, 2332.61737, 5700.55615, 7812.98765,
                                4264.86158, 268.74688,  6486.08937, 6204.44048,
                                7235.73534, 2794.02672, 9288.39283, 4662.6566,
                                6517.20614, 7785.61931, 8087.83614, 1094.24435,
                                8093.33661, 2796.65101, 1425.40209, 2949.08743,
                                3074.2948,  9631.15671, 1448.20895, 9843.30987,
                                5045.33169, 6653.13623},
                                .want_ret_value = INFINITY,
      },
      {
          .fam = create_metric_family_for_get_percentile_test9("test9"),
          .percent = 56.3,
          .num_updates = 54,
          .updates =
              (double[]){
                  9473.99522, 9682.46524, 6902.22386, 2632.59974, 224.97903,
                  4425.67438, 1094.25828, 5507.07452, 6140.55588, 6586.78403,
                  3748.51025, 7342.42407, 461.98087,  2370.005,   6234.53679,
                  3630.13252, 9190.53656, 2377.16807, 2554.37586, 5973.07804,
                  9459.471,   977.36175,  4707.95627, 9373.90178, 5625.67662,
                  9200.20201, 2503.82766, 5539.63445, 8564.03697, 5179.19388,
                  8530.74263, 3829.30061, 9251.19378, 8991.0057,  8464.4043,
                  7580.73952, 9025.62113, 8952.42671, 9287.5265,  8579.52376,
                  2938.24169, 1229.20803, 3995.36776, 2629.81514, 222.9282,
                  321.08365,  958.73825,  393.90684,  7396.77622, 2706.27567,
                  7376.80843, 3028.44747, 8684.45493, 8277.39937},
                  .want_ret_value = 6154.67381,
      },
  };

  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); ++i) {
    printf("## Case %zu:\n", i);

    uc_init();

    for (int j = 0; j < cases[i].num_updates; ++j) {
      CHECK_ZERO(distribution_update(
          cases[i].fam->metric.ptr[0].value.distribution, cases[i].updates[j]));
    }

    CHECK_ZERO(uc_update(cases[i].fam));

    EXPECT_EQ_INT(cases[i].want_get,
                  uc_get_percentile(&cases[i].fam->metric.ptr[0],
                                    &cases[i].ret_value, cases[i].percent));

    if (cases[i].want_get != -1) {
      EXPECT_EQ_DOUBLE(cases[i].want_ret_value, cases[i].ret_value);
    }

    CHECK_ZERO(metric_family_metric_reset(cases[i].fam));
    free(cases[i].fam);
  }
  return 0;
}

DEF_TEST(uc_get_rate_by_name) { return 0; }

DEF_TEST(uc_get_rate) { return 0; }

int main() {
  RUN_TEST(uc_update);
  RUN_TEST(uc_get_percentile_by_name);
  RUN_TEST(uc_get_percentile);
  RUN_TEST(uc_get_rate_by_name);
  RUN_TEST(uc_get_rate);

  END_TEST;
}
