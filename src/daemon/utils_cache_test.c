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

static metric_family_t *create_metric_family_test1(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));

  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m[] = {{
                      .family = fam,
                      .value.distribution = distribution_new_linear(5, 23),
                      .time = cdtime_mock++,
                  },
                  {
                      .family = fam,
                      .value.distribution = distribution_new_linear(5, 23),
                      .time = cdtime_mock++,
                  },
                  {
                      .family = fam,
                      .value.distribution = distribution_new_linear(5, 23),
                      .time = cdtime_mock++,
                  }};

  for (int i = 0; i < (sizeof(m) / sizeof(m[0])); ++i) {
    metric_family_metric_append(fam, m[i]);
  }

  return fam;
}

/* TODO(bkjg): write more test cases */
DEF_TEST(uc_update) {
  struct {
    int want_get;
    double **updates;
    int *num_updates;
    metric_family_t *fam;
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
          .fam = create_metric_family_test1("test1-update"),
      },
  };
  uc_init();
  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); ++i) {
    printf("## Case %zu:\n", i);

    for (int k = 0; k < cases[i].fam->metric.num; ++k) {
      for (int j = 0; j < cases[i].num_updates[k]; ++j) {
        CHECK_ZERO(
            distribution_update(cases[i].fam->metric.ptr[k].value.distribution,
                                cases[i].updates[k][j]));
      }
    }
    
    EXPECT_EQ_INT(cases[i].want_get, uc_update(cases[i].fam));
    EXPECT_EQ_UINT64(
        cases[i].fam->metric.ptr[cases[i].fam->metric.num - 1].time,
        uc_get_last_time(cases[i].fam->name));
    EXPECT_EQ_UINT64(cdtime(), uc_get_last_update(cases[i].fam->name));

    CHECK_ZERO(metric_family_metric_reset(cases[i].fam));
    free(cases[i].fam);
  }

  return 0;
}

static metric_family_t *
create_metric_family_for_get_percentile_by_name_test1(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_GAUGE;

  metric_t m = {
      .value.gauge = 843.43,
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);
  return fam;
}

static metric_family_t *
create_metric_family_for_get_percentile_by_name_test2(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_COUNTER;

  metric_t m = {
      .value.counter = 593,
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);

  return fam;
}

static metric_family_t *
create_metric_family_for_get_percentile_by_name_test3(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_UNTYPED;

  metric_t m = {
      .value.gauge = 965.67,
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);

  return fam;
}

static metric_family_t *
create_metric_family_for_get_percentile_by_name_test4(char *name) {
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
create_metric_family_for_get_percentile_by_name_test5(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m = {
      .value.distribution = distribution_new_exponential(104, 1.0012, 8),
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);

  return fam;
}

static metric_family_t *
create_metric_family_for_get_percentile_by_name_test6(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m = {
      .value.distribution = distribution_new_custom(
          98,
          (double[]){
              1836.54646,  2590.13251,  11074.53839, 12667.70191, 16493.74399,
              17201.67548, 19654.09863, 19822.70671, 20219.84235, 20628.6294,
              21330.3241,  21835.60702, 22005.01013, 23060.45248, 23755.791,
              24345.93352, 24595.10389, 25076.58155, 27877.29969, 29174.41583,
              29356.96818, 30235.43401, 31973.82099, 32834.01715, 36895.83728,
              37756.94675, 38943.5799,  40728.19275, 40968.78746, 41013.32028,
              41643.66803, 41941.75445, 41968.68699, 42139.98045, 42171.18171,
              42563.64426, 42707.00144, 44633.4339,  45017.01279, 45030.25685,
              45341.78369, 46078.46776, 46961.65554, 49016.42988, 49746.57551,
              50113.49909, 50589.66556, 50693.99199, 51591.89517, 52126.52965,
              52297.96765, 52811.00834, 52952.61506, 53226.50739, 58331.95697,
              58763.57321, 59090.41844, 59838.52915, 61566.98641, 63679.9936,
              64845.77561, 65082.73513, 66327.92101, 66394.5264,  67929.13548,
              68336.25877, 68548.39663, 68701.53922, 68771.86106, 69873.86819,
              71036.25018, 74018.27601, 75350.4665,  75355.084,   76614.2913,
              78877.96414, 79371.25209, 80041.87913, 80321.92699, 81510.64319,
              82640.81058, 84725.19083, 84789.66383, 85361.16797, 88086.65824,
              88308.13984, 90015.37417, 91817.05354, 91913.38988, 92477.00628,
              93033.2967,  94944.94684, 95986.58155, 96384.70299, 96657.04388,
              98245.02403, 99031.53623, 99147.69204}),
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);

  return fam;
}

static metric_family_t *
create_metric_family_for_get_percentile_by_name_test7(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m = {
      .value.distribution = distribution_new_linear(193, 47.97),
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);

  return fam;
}

static metric_family_t *
create_metric_family_for_get_percentile_by_name_test8(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m = {
      .value.distribution = distribution_new_exponential(74, 1.065, 45.784),
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);

  return fam;
}

static metric_family_t *
create_metric_family_for_get_percentile_by_name_test9(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m = {
      .value.distribution = distribution_new_custom(
          26, (double[]){7174.31149,  14377.23879, 19392.54763, 21299.07721,
                         24435.75879, 34705.8173,  35494.67336, 45019.09439,
                         48184.06042, 51450.77931, 53363.60859, 55081.31397,
                         55968.203,   64090.62914, 75022.61352, 79168.79336,
                         79769.46266, 79982.26847, 82362.20702, 83499.3666,
                         84368.98886, 86621.73007, 94893.89038, 95883.59771,
                         96327.48458, 97958.59675}),
      .family = fam,
      .time = cdtime_mock++,
  };

  metric_family_metric_append(fam, m);

  return fam;
}

DEF_TEST(uc_get_percentile_by_name) {
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
          .fam = create_metric_family_for_get_percentile_by_name_test1(
              "test1-percentile-by-name"),
          .want_get = -1,
          .percent = 57.34,
      },
      {
          .fam = create_metric_family_for_get_percentile_by_name_test2(
              "test2-percentile-by-name"),
          .want_get = -1,
          .percent = 49.23,
      },
      {
          .fam = create_metric_family_for_get_percentile_by_name_test3(
              "test3-percentile-by-name"),
          .want_get = -1,
          .percent = 23.54,
      },
      {
          .fam = create_metric_family_for_get_percentile_by_name_test4(
              "test4-percentile-by-name"),
          .percent = 89.73,
          .want_ret_value = NAN,
      },
      {
          .fam = create_metric_family_for_get_percentile_by_name_test5(
              "test5-percentile-by-name"),
          .want_get = -1,
          .percent = -76,
      },
      {
          .fam = create_metric_family_for_get_percentile_by_name_test6(
              "test6-percentile-by-name"),
          .want_get = -1,
          .percent = 100.4,
      },
      {
          .fam = create_metric_family_for_get_percentile_by_name_test7(
              "test7-percentile-by-name"),
          .percent = 0,
          .num_updates = 71,
          .updates =
              (double[]){82359.12294, 68158.41378, 48412.81238, 5904.4289,
                         34826.95792, 57900.41609, 33671.55587, 24959.94718,
                         93646.26915, 1520.75565,  14639.69397, 70222.39415,
                         21981.56962, 16146.0281,  13983.92362, 52167.56135,
                         27539.19779, 45057.29114, 3319.77024,  34066.56465,
                         10173.62876, 91667.04402, 98416.81719, 96030.39534,
                         79983.43468, 77316.03541, 5659.02605,  55984.92588,
                         77457.94164, 3384.64733,  72593.37989, 66288.34529,
                         17548.12711, 39080.58183, 70098.8097,  64922.09787,
                         38622.17965, 63267.44964, 80319.19425, 57421.82315,
                         54532.87389, 23394.05223, 6053.33071,  81659.40367,
                         38240.65576, 85423.95662, 54837.08557, 30067.92726,
                         60829.11685, 5428.52609,  6612.06311,  70475.98393,
                         85464.98161, 20740.81419, 43986.23893, 70876.29799,
                         1037.86676,  76031.71462, 53813.00219, 4035.65718,
                         5811.23089,  58692.27907, 94178.50533, 12500.30161,
                         81487.15188, 69029.50582, 49662.02744, 73422.53592,
                         68386.53142, 91320.37996, 91735.92333},
          .want_ret_value = 47.97,
      },
      {
          .fam = create_metric_family_for_get_percentile_by_name_test8(
              "test8-percentile-by-name"),
          .percent = 100,
          .num_updates = 42,
          .updates =
              (double[]){72506.34561, 91418.8635,  80572.52619, 38484.68244,
                         87877.24226, 57174.59598, 1551.41153,  9145.58047,
                         72967.20258, 53348.1573,  48132.4808,  39831.68688,
                         78359.72224, 76905.47862, 5348.62723,  8070.63794,
                         11716.49737, 29432.91898, 67222.86733, 29114.50366,
                         67117.86881, 15388.23779, 48933.6252,  74134.36183,
                         24786.55592, 42282.17781, 87869.09351, 26252.42492,
                         7323.72669,  47943.4361,  29671.97547, 6225.43339,
                         44457.12541, 34822.90173, 50059.94181, 26860.86093,
                         71182.72552, 65944.4019,  21285.09149, 19641.2854,
                         19254.37358, 61342.40975},
          .want_ret_value = INFINITY,
      },
      {
          .fam = create_metric_family_for_get_percentile_by_name_test9(
              "test9-percentile-by-name"),
          .percent = 42.3,
          .num_updates = 38,
          .updates =
              (double[]){71079.41279, 64378.08534, 58308.74218, 62899.70407,
                         43084.69866, 59286.43609, 95311.89101, 46082.97057,
                         21002.88808, 13767.93511, 6832.77194,  9641.53968,
                         93547.80996, 53104.75652, 30484.31134, 63405.23635,
                         87195.96601, 91571.10233, 10134.44248, 63686.4723,
                         323.06071,   48039.79776, 84822.6203,  89630.53538,
                         15187.56664, 76056.39513, 91604.62264, 47667.05402,
                         11471.67096, 82920.8644,  57711.02046, 73579.12752,
                         35093.01442, 95393.57805, 17610.14104, 54931.47418,
                         19359.63012, 46414.44434},
          .want_ret_value = 48184.06042,
      },
  };

  uc_init();
  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); ++i) {
    printf("## Case %zu:\n", i);
    for (int j = 0; j < cases[i].num_updates; ++j) {
      CHECK_ZERO(distribution_update(
          cases[i].fam->metric.ptr[0].value.distribution, cases[i].updates[j]));
    }

    CHECK_ZERO(uc_update(cases[i].fam));

    EXPECT_EQ_INT(cases[i].want_get,
                  uc_get_percentile_by_name(cases[i].fam->name,
                                            &cases[i].ret_value,
                                            cases[i].percent));

    if (cases[i].want_get != -1) {
      EXPECT_EQ_DOUBLE(cases[i].want_ret_value, cases[i].ret_value);
    }

    CHECK_ZERO(metric_family_metric_reset(cases[i].fam));
    free(cases[i].fam);
  }

  return 0;
}

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

  uc_init();
  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); ++i) {
    printf("## Case %zu:\n", i);

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
  // reset_cache_tree();

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
