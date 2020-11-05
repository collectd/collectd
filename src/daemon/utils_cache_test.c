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
#include "utils/common/common.h"
#include "utils_cache.h"

static metric_family_t *create_metric_family_for_test1(char *name,
                                                       size_t num_metrics,
                                                       const gauge_t *gauges) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_GAUGE;

  metric_t m[num_metrics];
  memset(m, 0, num_metrics * sizeof(metric_t));

  for (size_t i = 0; i < num_metrics; ++i) {
    m[i].value.gauge = gauges[i];
    m[i].family = fam;
    m[i].time = cdtime_mock++;
  }

  for (size_t i = 0; i < (sizeof(m) / sizeof(metric_t)); ++i) {
    metric_family_metric_append(fam, m[i]);
    metric_reset(&m[i]);
  }

  return fam;
}

static metric_family_t *
create_metric_family_for_test2(char *name, double *want_ret_value,
                               size_t num_metrics, const counter_t *counters,
                               const uint64_t *times) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_COUNTER;

  metric_t m[num_metrics];
  memset(m, 0, num_metrics * sizeof(metric_t));

  for (size_t i = 0; i < num_metrics; ++i) {
    m[i].value.counter = counters[i];
    m[i].family = fam;
    m[i].time = (cdtime_mock += times[i]);
  }

  if (num_metrics >= 2) {
    *want_ret_value =
        (counter_diff(m[num_metrics - 2].value.counter,
                      m[num_metrics - 1].value.counter)) /
        (CDTIME_T_TO_DOUBLE(m[num_metrics - 1].time - m[num_metrics - 2].time));
  } else {
    *want_ret_value = 0;
  }

  for (size_t i = 0; i < num_metrics; ++i) {
    metric_family_metric_append(fam, m[i]);
    metric_reset(&m[i]);
  }

  return fam;
}

static metric_family_t *create_metric_family_for_test3(char *name,
                                                       size_t num_metrics,
                                                       const gauge_t *gauges) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_UNTYPED;

  metric_t m[num_metrics];
  memset(m, 0, num_metrics * sizeof(metric_t));

  for (size_t i = 0; i < num_metrics; ++i) {
    m[i].value.gauge = gauges[i];
    m[i].family = fam;
    m[i].time = cdtime_mock++;
  }

  for (size_t i = 0; i < (sizeof(m) / sizeof(metric_t)); ++i) {
    metric_family_metric_append(fam, m[i]);
    metric_reset(&m[i]);
  }

  return fam;
}

static metric_family_t *create_metric_family_for_test4(char *name) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m[] = {
      {
          .family = fam,
          .value.distribution = NULL,
          .time = cdtime_mock++,
      },
  };

  for (size_t i = 0; i < (sizeof(m) / sizeof(metric_t)); ++i) {
    metric_family_metric_append(fam, m[i]);
    metric_reset(&m[i]);
  }

  return fam;
}

static metric_family_t *
create_metric_family_for_test5(char *name, size_t num_metrics,
                               size_t num_buckets, double base, double factor) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m[num_metrics];
  memset(m, 0, num_metrics * sizeof(metric_t));

  for (size_t i = 0; i < num_metrics; ++i) {
    m[i].value.distribution =
        distribution_new_exponential(num_buckets, base, factor);
    m[i].family = fam;
    m[i].time = cdtime_mock++;
  }

  for (size_t i = 0; i < num_metrics; ++i) {
    metric_family_metric_append(fam, m[i]);
    metric_reset(&m[i]);
  }

  return fam;
}

static metric_family_t *create_metric_family_for_test6(char *name,
                                                       size_t num_metrics,
                                                       size_t num_boundaries,
                                                       double *boundaries) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m[num_metrics];
  memset(m, 0, num_metrics * sizeof(metric_t));

  for (size_t i = 0; i < num_metrics; ++i) {
    m[i].value.distribution =
        distribution_new_custom(num_boundaries, boundaries);
    m[i].family = fam;
    m[i].time = cdtime_mock++;
  }

  for (size_t i = 0; i < num_metrics; ++i) {
    metric_family_metric_append(fam, m[i]);
    metric_reset(&m[i]);
  }

  return fam;
}

static metric_family_t *create_metric_family_for_test7(char *name,
                                                       size_t num_metrics,
                                                       size_t num_buckets,
                                                       double size) {
  metric_family_t *fam = calloc(1, sizeof(metric_family_t));
  fam->name = name;
  fam->type = METRIC_TYPE_DISTRIBUTION;

  metric_t m[num_metrics];
  memset(m, 0, num_metrics * sizeof(metric_t));

  for (size_t i = 0; i < num_metrics; ++i) {
    m[i].value.distribution = distribution_new_linear(num_buckets, size);
    m[i].family = fam;
    m[i].time = cdtime_mock++;
  }

  for (size_t i = 0; i < num_metrics; ++i) {
    metric_family_metric_append(fam, m[i]);
    metric_reset(&m[i]);
  }

  return fam;
}

DEF_TEST(uc_update) {
  struct {
    int want_get;
    double **updates;
    size_t *num_updates;
    metric_family_t *fam;
  } cases[] = {
      {
          .fam = create_metric_family_for_test4("test1-update"),
          .num_updates = (size_t[]){0},
      },
      {.fam = create_metric_family_for_test5("test2-update", 6, 45, 1.46, 78.9),
       .num_updates = (size_t[]){4, 0, 7, 5, 89, 8},
       .updates =
           (double *[]){
               (double[]){4183.08566, 1317.31765, 7054.77881, 6979.86462},
               (double[]){},
               (double[]){8031.4248, 2295.41598, 151.96383, 1521.3922,
                          3394.33898, 8871.99086, 6423.20884},
               (double[]){5615.96902, 4429.79112, 3729.46686, 2221.61466,
                          5106.26763},
               (double[]){
                   3338.14436, 6734.8498,  3670.28995, 1031.25568, 4441.95445,
                   3997.05407, 5337.83242, 7398.61836, 5780.08544, 3131.0014,
                   2694.10533, 8704.19498, 6052.1578,  8838.9791,  7841.9383,
                   5790.67753, 420.21033,  7464.18245, 9877.97011, 9676.69636,
                   5966.31923, 5306.15973, 6343.30937, 7437.62127, 9285.09355,
                   3823.98309, 2042.5274,  2334.78771, 9868.86182, 4667.09399,
                   3889.42185, 3479.29699, 3102.8542,  5875.15245, 4767.73987,
                   271.3021,   9763.66807, 9869.07329, 9361.64101, 801.09197,
                   1840.35096, 9872.14093, 5321.90727, 9635.26758, 1226.0445,
                   6307.37991, 1919.78542, 2427.85393, 6886.69887, 7309.62705,
                   2437.89498, 2254.63699, 160.91143,  1996.80717, 7546.89382,
                   1330.53963, 3456.3406,  6081.6114,  4669.06625, 6623.91979,
                   9531.18935, 2044.26528, 5100.58777, 7576.0143,  2323.48974,
                   6639.79369, 5051.38553, 6117.469,   2873.8218,  2058.59431,
                   5673.71351, 8387.83663, 7970.44642, 508.45651,  9411.37676,
                   9663.28423, 9373.3203,  3733.18994, 544.66759,  2780.3152,
                   3755.17238, 3080.58406, 8913.98237, 1057.10468, 2865.68143,
                   3538.3128,  6978.06363, 2172.77992, 9566.80927},
               (double[]){49779, 5.17, 96.40446, 203.99944, 9.68401, 951.90023,
                          152, 181.3175},
           }},
      {.fam = create_metric_family_for_test6(
           "test3-update", 5, 7,
           (double[]){11.3835, 25.10331, 50.09619, 67.13256, 93.68678, 96.26066,
                      97.68498}),
       .num_updates = (size_t[]){5, 45, 7, 3, 0},
       .updates =
           (double *[]){
               (double[]){124.324, 5.32, 543.2425, 76.43, 8.4},
               (double[]){139.72697, 8.77556,   120.01873, 96.77345,  30.62022,
                          117.35748, 2.77774,   99.24504,  144.22556, 42.07964,
                          95.55326,  52.91671,  135.73019, 31.59932,  99.2114,
                          4.83134,   121.92372, 31.81964,  64.11753,  13.12282,
                          93.49707,  91.33555,  100.03341, 9.04922,   139.41451,
                          94.79717,  62.6659,   140.26381, 125.14233, 65.01135,
                          135.96624, 144.22601, 72.43663,  0.31562,   31.28456,
                          57.29803,  58.05409,  88.41997,  32.09302,  43.02904,
                          106.92403, 39.73264,  127.82747, 90.47255,  35.10884},
               (double[]){48.73912, 50.99148, 69.9, 26.2775, 22.09106, 47.66528,
                          20.56454},
               (double[]){54.32, 1.32, 97.32},
               (double[]){},
           }},
      {
          .updates =
              (double *[]){
                  (double[]){5.432, 5.4234, 6.3424, 7.5453, 902.3525,
                             52352.523523, 523524.524524, 90342.4325, 136.352,
                             90.8},
                  (double[]){1.214, 90432.31434, 43141.1342, 41412.4314, 97.47,
                             90.78},
                  (double[]){78.57, 90.467, 89.658, 879.67},
              },
          .num_updates = (size_t[]){10, 6, 4},
          .fam = create_metric_family_for_test7("test4-update", 3, 5, 23),
      },
      {
          .fam = NULL,
          .want_get = EINVAL,
      }};
  uc_init();
  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); ++i) {
    printf("## Case %zu:\n", i);

    if (cases[i].fam != NULL) {
      for (size_t l = 0; l < cases[i].fam->metric.num; ++l) {
        for (size_t k = 0; k <= l; ++k) {
          for (size_t j = 0; j < cases[i].num_updates[k]; ++j) {
            distribution_update(cases[i].fam->metric.ptr[l].value.distribution,
                                cases[i].updates[k][j]);
          }
        }
      }
    }

    EXPECT_EQ_INT(cases[i].want_get, uc_update(cases[i].fam));

    if (cases[i].fam != NULL) {
      cdtime_t time;
      CHECK_ZERO(uc_get_last_time(cases[i].fam->name, &time));
      EXPECT_EQ_UINT64(
          cases[i].fam->metric.ptr[cases[i].fam->metric.num - 1].time, time);
      CHECK_ZERO(uc_get_last_update(cases[i].fam->name, &time));
      EXPECT_EQ_UINT64(cdtime(), time);
      CHECK_ZERO(metric_family_metric_reset(cases[i].fam));
    } else {
      EXPECT_EQ_INT(EINVAL, metric_family_metric_reset(cases[i].fam));
    }

    free(cases[i].fam);
  }
  uc_destroy();
  return 0;
}

DEF_TEST(uc_get_percentile_by_name) {
  double tmp;
  struct {
    int want_get;
    double percent;
    gauge_t ret_value;
    gauge_t want_ret_value;
    metric_family_t *fam;
    double **updates;
    size_t *num_updates;
  } cases[] = {
      {
          .fam = create_metric_family_for_test1(
              "test1-percentile-by-name", 4,
              (gauge_t[]){43.543, 654.32, 948.543, 1342.42}),
          .want_get = -1,
          .percent = 57.34,
          .num_updates = (size_t[]){0, 0, 0, 0},
      },
      {
          .fam = create_metric_family_for_test2(
              "test2-percentile-by-name", &tmp, 6,
              (counter_t[]){123, 432, 6542, 9852, 10943, 903423},
              (uint64_t[]){3, 543, 5, 654, 43, 4}),
          .want_get = -1,
          .percent = 49.23,
          .num_updates = (size_t[]){0, 0, 0, 0, 0, 0},
      },
      {
          .fam = create_metric_family_for_test3(
              "test3-percentile-by-name", 2, (gauge_t[]){324.234, 52452.342}),
          .want_get = -1,
          .percent = 23.54,
          .num_updates = (size_t[]){0, 0},
      },
      {
          .fam = create_metric_family_for_test4("test4-percentile-by-name"),
          .percent = 89.73,
          .want_ret_value = NAN,
          .num_updates = (size_t[]){0},
      },
      {
          .fam = create_metric_family_for_test5("test5-percentile-by-name", 6,
                                                104, 1.0012, 8),
          .want_get = -1,
          .percent = -76,
          .num_updates = (size_t[]){0, 0, 0, 0, 0, 0},
      },
      {
          .fam = create_metric_family_for_test6(
              "test6-percentile-by-name", 8, 98,
              (double[]){1836.54646,  2590.13251,  11074.53839, 12667.70191,
                         16493.74399, 17201.67548, 19654.09863, 19822.70671,
                         20219.84235, 20628.6294,  21330.3241,  21835.60702,
                         22005.01013, 23060.45248, 23755.791,   24345.93352,
                         24595.10389, 25076.58155, 27877.29969, 29174.41583,
                         29356.96818, 30235.43401, 31973.82099, 32834.01715,
                         36895.83728, 37756.94675, 38943.5799,  40728.19275,
                         40968.78746, 41013.32028, 41643.66803, 41941.75445,
                         41968.68699, 42139.98045, 42171.18171, 42563.64426,
                         42707.00144, 44633.4339,  45017.01279, 45030.25685,
                         45341.78369, 46078.46776, 46961.65554, 49016.42988,
                         49746.57551, 50113.49909, 50589.66556, 50693.99199,
                         51591.89517, 52126.52965, 52297.96765, 52811.00834,
                         52952.61506, 53226.50739, 58331.95697, 58763.57321,
                         59090.41844, 59838.52915, 61566.98641, 63679.9936,
                         64845.77561, 65082.73513, 66327.92101, 66394.5264,
                         67929.13548, 68336.25877, 68548.39663, 68701.53922,
                         68771.86106, 69873.86819, 71036.25018, 74018.27601,
                         75350.4665,  75355.084,   76614.2913,  78877.96414,
                         79371.25209, 80041.87913, 80321.92699, 81510.64319,
                         82640.81058, 84725.19083, 84789.66383, 85361.16797,
                         88086.65824, 88308.13984, 90015.37417, 91817.05354,
                         91913.38988, 92477.00628, 93033.2967,  94944.94684,
                         95986.58155, 96384.70299, 96657.04388, 98245.02403,
                         99031.53623, 99147.69204}),
          .want_get = -1,
          .percent = 100.4,
          .num_updates = (size_t[]){0, 0, 0, 0, 0, 0, 0, 0},
      },
      {
          .fam = create_metric_family_for_test7("test7-percentile-by-name", 4,
                                                193, 47.97),
          .percent = 0,
          .num_updates = (size_t[]){71, 8, 45, 3},
          .updates =
              (double *[]){
                  (double[]){8194,        68.41378,    488,         5904.4289,
                             348.952,     5.41609,     33687,       24959.94718,
                             93646.26915, 1520.5,      14639.697,   7022,
                             281.52,      16146.0281,  1.92362,     52167.56135,
                             29.19779,    45057.14,    3319.7724,   34066.55,
                             1073.62876,  91667.0,     98416.1719,  96030.39534,
                             783.438,     77316.03541, 5659.005,    55984.9258,
                             7.94164,     3384.647,    72593.99,    6628.329,
                             17548.12711, 3.58183,     70.8097,     64922.09787,
                             3822.1765,   67.44964,    819.125,     57421.823,
                             52.8739,     2394.23,     6053.3371,   81659.40367,
                             38240.76,    8542.95662,  54837.08557, 7.92726,
                             69.11685,    5428.26,     6612.06311,  70475.98393,
                             85464.98161, 20740.9,     43986.93,    70876.29799,
                             1037.866,    71.71462,    53813.0019,  4035.658,
                             5811.23089,  582.7,       94178.50533, 12500.30161,
                             81487.188,   690.50582,   49662.044,   73422.52,
                             68386.53142, 91320.37996, 35.92333},
                  (double[]){745.38815, 481.05336, 6.41994, 1.95525, 1156.20533,
                             1268.7098, 1295.84116, 917.62886},
                  (double[]){516.9201,   4849.31623, 7221.34863, 7527.68604,
                             1151.26575, 6524.27975, 705.95819,  4513.90006,
                             5128.38355, 6394.32618, 5547.92822, 5852.66449,
                             1573.28964, 4182.4275,  8472.49003, 2649.24761,
                             9031.21648, 273.19715,  9269.87663, 3111.26556,
                             5555.49909, 1971.95879, 4414.7191,  6246.03113,
                             4236.78126, 8654.59594, 1718.24435, 7858.42398,
                             5748.95136, 1579.52587, 3661.98626, 2142.41081,
                             7568.26255, 5097.90185, 5640.32226, 3454.39562,
                             9409.04172, 4254.11444, 4606.8975,  8475.69705,
                             4081.69481, 6739.58349, 7981.12462, 8370.65351,
                             7414.76374},
                  (double[]){5454.43, 9854.432, 10000.432},
              },
          .want_ret_value = 47.97,
      },
      {
          .fam = create_metric_family_for_test5("test8-percentile-by-name", 5,
                                                74, 1.065, 45.784),
          .percent = 100,
          .num_updates = (size_t[]){42, 0, 67, 5, 89},
          .updates =
              (double *[]){
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
                  (double[]){

                  },
                  (double[]){5586.54111,  3841.94096,  5834.15027,  8247.97183,
                             9499.93153,  7472.58943,  219.47985,   7686.70986,
                             1219.55743,  2415.15816,  8955.29092,  3480.59525,
                             1879.67425,  1275.56004,  3295.58027,  2139.82266,
                             456.72684,   562.50828,   1941.86819,  9780.2701,
                             4234.51139,  1287.71429,  571.7637,    3018.60717,
                             3024.39436,  10317.61782, 3059.65791,  6248.61059,
                             10178.41514, 3430.92157,  5205.66822,  3387.48332,
                             2669.69809,  7532.6,      6981.75202,  2897.25262,
                             8964.52642,  4209.86327,  3735.92037,  878.27845,
                             4097.70763,  10419.35129, 2094.15747,  9162.93322,
                             6691.67488,  1529.17463,  10293.11383, 10388.03797,
                             4898.96937,  9262.156,    833.49287,   1568.14635,
                             1187.62364,  8168.1532,   8849.88077,  8484.26639,
                             10457.05272, 1759.43482,  288.39094,   7405.56667,
                             9778.09951,  6290.33759,  7102.4534,   2172.31562,
                             8277.08193,  8824.99498,  5797.51546},
                  (double[]){56.43, 879.453, 5.324, 765.342, 64.4356},
                  (double[]){1013.25449, 9089.16126, 9854.27631, 9420.94215,
                             509.43821,  7299.78937, 2293.54321, 7308.0138,
                             4066.37637, 6305.31349, 7006.66839, 2503.36452,
                             5787.31125, 4247.16623, 5803.29759, 8303.8097,
                             9543.26322, 2027.47232, 486.42233,  6603.60027,
                             1440.39473, 9132.76392, 4395.65407, 6737.9134,
                             6162.07532, 5855.29759, 802.30289,  10121.34827,
                             10029.5615, 2886.96024, 3185.46451, 517.43518,
                             892.29103,  5515.76823, 8022.44596, 3888.66676,
                             4320.34148, 5751.74699, 5016.10839, 343.68796,
                             2926.28764, 3061.87651, 4463.04963, 5255.43202,
                             4029.81862, 3254.9407,  6082.41397, 10433.08612,
                             2011.06951, 2968.68571, 7948.21252, 4659.80086,
                             8464.0979,  1424.92974, 6664.08464, 7246.80407,
                             4714.69846, 6168.91884, 6315.78325, 9857.07278,
                             5709.06859, 176.3125,   5244.00458, 3999.77758,
                             7247.30445, 9108.6723,  7354.32423, 7918.81314,
                             3666.6743,  7315.87523, 5687.41806, 293.34054,
                             1812.20181, 3585.62552, 6423.85917, 4389.82836,
                             3467.02384, 6070.2832,  8559.86769, 1225.99976,
                             2808.22175, 4888.729,   7613.12619, 414.86679,
                             7449.10677, 6598.8391,  2551.38054, 9092.67749,
                             5626.38473},
              },
          .want_ret_value = INFINITY,
      },
      {
          .fam = create_metric_family_for_test6(
              "test9-percentile-by-name", 3, 26,
              (double[]){7174.31149,  14377.23879, 19392.54763, 21299.07721,
                         24435.75879, 34705.8173,  35494.67336, 45019.09439,
                         48184.06042, 51450.77931, 53363.60859, 55081.31397,
                         55968.203,   64090.62914, 75022.61352, 79168.79336,
                         79769.46266, 79982.26847, 82362.20702, 83499.3666,
                         84368.98886, 86621.73007, 94893.89038, 95883.59771,
                         96327.48458, 97958.59675}),
          .percent = 42.3,
          .num_updates = (size_t[]){38, 0, 2},
          .updates =
              (double *[]){
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
                  (double[]){},
                  (double[]){468.435, 54267.54},
              },
          .want_ret_value = 7174.31149,
      },
  };

  uc_init();
  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); ++i) {
    printf("## Case %zu:\n", i);

    for (size_t k = 0; k < cases[i].fam->metric.num; ++k) {
      for (size_t l = 0; l <= k; ++l) {
        for (size_t j = 0; j < cases[i].num_updates[l]; ++j) {
          distribution_update(cases[i].fam->metric.ptr[k].value.distribution,
                              cases[i].updates[l][j]);
        }
      }
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
  uc_destroy();
  return 0;
}

DEF_TEST(uc_get_percentile) {
  metric_family_t *metric_family_with_null_metric =
      calloc(1, sizeof(metric_family_t));
  metric_family_with_null_metric->type = METRIC_TYPE_COUNTER;
  metric_family_with_null_metric->name = "test-percentile-with-null";

  double tmp;
  struct {
    int want_get;
    double percent;
    gauge_t ret_value;
    gauge_t want_ret_value;
    metric_family_t *fam;
    double **updates;
    size_t *num_updates;
    size_t metric_idx;
  } cases[] = {
      {
          .fam = create_metric_family_for_test1(
              "test1-percentile", 4,
              (gauge_t[]){6.5, 3.423, 5232.523, 432.342, 65.43, 9.7}),
          .want_get = -1,
          .percent = 57.34,
          .num_updates = (size_t[]){0, 0, 0, 0},
      },
      {
          .fam = create_metric_family_for_test2("test2-percentile", &tmp, 2,
                                                (counter_t[]){2, 543},
                                                (uint64_t[]){43, 654}),
          .want_get = -1,
          .percent = 49.23,
          .num_updates = (size_t[]){0, 0},
      },
      {
          .fam = create_metric_family_for_test3(
              "test3-percentile", 3, (gauge_t[]){4234.432, 54364.324, 4324.43}),
          .want_get = -1,
          .percent = 23.54,
          .num_updates = (size_t[]){0, 0, 0},
      },
      {
          .fam = create_metric_family_for_test4("test4-percentile"),
          .percent = 89.73,
          .want_ret_value = NAN,
          .num_updates = (size_t[]){0},
      },
      {
          .fam = create_metric_family_for_test5("test5-percentile", 6, 56, 1.24,
                                                5),
          .want_get = -1,
          .percent = -76,
          .num_updates = (size_t[]){0, 0, 0, 0, 0, 0},
      },
      {
          .fam = create_metric_family_for_test6(
              "test6-percentile", 5, 8,
              (double[]){1.54, 3.543, 7.234, 78.435, 285.5435, 9023.53453,
                         100043.43, 900000.43}),
          .want_get = -1,
          .percent = 100.4,
          .num_updates = (size_t[]){0, 0, 0, 0, 0},
      },
      {
          .fam =
              create_metric_family_for_test7("test7-percentile", 9, 68, 84.543),
          .percent = 0,
          .num_updates = (size_t[]){9, 1, 0, 6, 8, 0, 7, 0, 4},
          .updates =
              (double *[]){
                  (double[]){1.354, 4.2343, 67.543, 7243.2435, 543.2543, 54.43,
                             543.534, 9023.534, 2358453.534534},
                  (double[]){67.543},
                  (double[]){},
                  (double[]){54.4645, 768.435, 6.4534, 986.43, 987.534, 23.5},
                  (double[]){546.654, 1324.324, 457.43, 678.435, 5785.435,
                             895.32, 9.423, 9853.543},
                  (double[]){},
                  (double[]){654.645, 45352.6, 53453.43, 543.435, 76.43, 797.43,
                             76.43},
                  (double[]){},
                  (double[]){83.4653, 943.463, 573.543, 90.543},
              },
          .want_ret_value = 84.543,
          .metric_idx = 5,
      },
      {
          .fam = create_metric_family_for_test5("test8-percentile", 7, 24,
                                                1.345, 9.67),
          .percent = 100,
          .num_updates = (size_t[]){15, 67, 6, 0, 56, 26, 1},
          .updates =
              (double *[]){
                  (double[]){7273.23889, 2332.61737, 5700.55615, 7812.98765,
                             4264.86158, 268.74688,  6486.08937, 6204.44048,
                             7235.73534, 2794.02672, 9288.39283, 4662.6566,
                             6517.20614, 7785.61931, 8087.83614, 1094.24435,
                             8093.33661, 2796.65101, 1425.40209, 2949.08743,
                             3074.2948,  9631.15671, 1448.20895, 9843.30987,
                             5045.33169, 6653.13623},
                  (double[]){
                      8.9836,    44.92963,  3.13412,  72.36738, 107.39301,
                      21.2692,   33.24933,  10.00479, 11.07998, 101.58473,
                      56.7942,   102.57711, 85.23387, 5.50962,  109.7728,
                      25.13287,  82.06147,  7.88873,  65.86362, 68.49312,
                      76.34463,  11.84615,  51.95518, 51.36831, 44.59729,
                      27.56038,  54.12334,  3.14058,  70.23039, 113.51434,
                      100.76535, 1.42056,   98.09661, 70.88242, 55.39125,
                      4.84537,   48.7601,   3.89373,  61.61134, 97.6968,
                      42.15975,  66.19623,  83.75841, 93.82006, 86.68521,
                      47.97931,  83.52405,  79.94514, 52.24084, 34.4199,
                      89.83969,  93.75171,  88.65762, 8.99014,  1.12716,
                      9.23472,   44.18075,  75.32575, 40.50866, 0.62965,
                      109.54525, 63.84394,  19.85427, 20.86203, 42.07358,
                      100.14113, 9.58807},
                  (double[]){65.43, 7.54, 10.42323, 11.543, 20.4235, 109.423},
                  (double[]){},
                  (double[]){
                      40.70721,  111.25481, 58.84637,  130.57275, 12.98124,
                      107.43372, 7.30452,   62.89139,  98.57436,  81.20726,
                      32.09759,  142.48247, 45.98872,  26.61524,  91.13035,
                      105.86144, 110.31826, 43.40806,  46.45192,  23.47531,
                      6.53419,   149.77225, 119.41684, 8.76683,   134.67231,
                      59.93585,  129.2164,  108.17816, 43.76639,  138.4096,
                      44.9372,   146.85231, 35.69273,  91.43816,  50.39418,
                      139.84918, 132.96587, 41.81809,  35.02065,  67.94941,
                      59.01611,  138.43066, 95.45903,  51.71665,  31.17835,
                      125.77984, 24.81209,  43.67169,  59.02432,  77.40476,
                      72.55881,  103.22872, 61.44734,  110.21046, 22.42225,
                      53.0431},
                  (double[]){
                      38.81053, 2.92907,  58.62993, 1.02672,  21.98664,
                      27.78435, 62.01765, 11.04862, 35.70885, 20.19808,
                      48.87309, 16.81291, 46.22889, 50.54755, 59.58231,
                      50.21794, 26.45925, 10.17534, 26.77216, 18.10478,
                      30.72786, 16.07058, 16.60934, 44.67609, 46.58571,
                      66.82775},
                  (double[]){44.32},
              },
          .want_ret_value = 57.2479817070748,
          .metric_idx = 0,
      },
      {
          .fam = create_metric_family_for_test6(
              "test9-percentile", 4, 49,
              (double[]){
                  283.11051,  341.94139,  512.86531,  604.69627,  657.38026,
                  868.54382,  1057.70293, 1441.49331, 1499.54159, 2011.40738,
                  2135.40374, 2411.07421, 2658.56919, 2771.7077,  2913.22869,
                  3171.10203, 3734.33039, 4004.57893, 4194.52351, 4209.34402,
                  4395.85013, 4413.95106, 4575.35795, 4827.7257,  5012.33716,
                  5579.60919, 5857.40106, 6154.67381, 6167.46927, 6183.56511,
                  6247.11633, 6548.84595, 6798.12567, 6915.32327, 6975.11549,
                  7010.67086, 7102.20424, 7296.39035, 7599.84557, 7621.5989,
                  8055.85652, 8514.09805, 8786.67945, 8814.77247, 9421.52142,
                  9584.05069, 9618.27028, 9788.40721, 9862.77031}),
          .percent = 56.3,
          .num_updates = (size_t[]){54, 67, 8, 9},
          .updates =
              (double *[]){
                  (double[]){9473.99522, 9682.46524, 6902.22386, 2632.59974,
                             224.97903,  4425.67438, 1094.25828, 5507.07452,
                             6140.55588, 6586.78403, 3748.51025, 7342.42407,
                             461.98087,  2370.005,   6234.53679, 3630.13252,
                             9190.53656, 2377.16807, 2554.37586, 5973.07804,
                             9459.471,   977.36175,  4707.95627, 9373.90178,
                             5625.67662, 9200.20201, 2503.82766, 5539.63445,
                             8564.03697, 5179.19388, 8530.74263, 3829.30061,
                             9251.19378, 8991.0057,  8464.4043,  7580.73952,
                             9025.62113, 8952.42671, 9287.5265,  8579.52376,
                             2938.24169, 1229.20803, 3995.36776, 2629.81514,
                             222.9282,   321.08365,  958.73825,  393.90684,
                             7396.77622, 2706.27567, 7376.80843, 3028.44747,
                             8684.45493, 8277.39937},
                  (double[]){
                      94.34024,   5369.42045, 106.99225,  4845.37869,
                      3694.95099, 6859.38458, 3403.18808, 9102.51424,
                      4098.48109, 5954.08755, 6146.62894, 3494.39408,
                      4229.73443, 1400.66599, 8942.00647, 6992.12526,
                      3145.67041, 4158.62876, 8337.978,   8311.53907,
                      5934.71194, 6885.4022,  7941.92203, 1636.68227,
                      6086.39492, 1893.09594, 4984.28907, 756.5788,
                      9298.44361, 6460.4752,  8455.53452, 2607.52741,
                      62.34891,   2499.08504, 1370.14777, 7838.19529,
                      4335.81203, 736.71981,  9177.66445, 8299.95565,
                      7745.85028, 5442.34546, 7129.24099, 6034.30483,
                      2666.01496, 5626.89066, 1948.9354,  842.76915,
                      4425.71523, 8022.58775, 3228.76266, 6616.1537,
                      7016.92999, 6867.16531, 4919.9911,  3812.5397,
                      353.57387,  7504.68569, 8575.42736, 9262.40423,
                      6691.10738, 9253.02204, 9171.75229, 6153.56538,
                      8469.24383, 4859.31769, 1161.66019},
                  (double[]){
                      4400.00001, 9833.13863, 100.16011, 6849.80943, 510.31266,
                      2866.47649, 4492.46879, 8692.87943},
                  (double[]){
                      1566.00821, 7594.931, 995.13973, 1289.97209, 9242.79285,
                      9702.15248, 82.44731, 1854.97795, 5236.74648},
              },
          .want_ret_value = 5579.60919,
          .metric_idx = 3,
      },
      {
          .fam = metric_family_with_null_metric,
          .want_get = -1,
      }};

  /* TODO(bkjg): add test cases to the uc_update function that will give an
   * error in distribution_sub function */
  uc_init();
  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); ++i) {
    printf("## Case %zu:\n", i);

    for (size_t k = 0; k < cases[i].fam->metric.num; ++k) {
      for (size_t l = 0; l <= k; ++l) {
        for (size_t j = 0; j < cases[i].num_updates[l]; ++j) {
          distribution_update(cases[i].fam->metric.ptr[k].value.distribution,
                              cases[i].updates[l][j]);
        }
      }
    }

    CHECK_ZERO(uc_update(cases[i].fam));

    EXPECT_EQ_INT(
        cases[i].want_get,
        uc_get_percentile(&cases[i].fam->metric.ptr[cases[i].metric_idx],
                          &cases[i].ret_value, cases[i].percent));

    if (cases[i].want_get != -1) {
      EXPECT_EQ_DOUBLE(cases[i].want_ret_value, cases[i].ret_value);
    }

    CHECK_ZERO(metric_family_metric_reset(cases[i].fam));
    free(cases[i].fam);
  }
  uc_destroy();
  return 0;
}

/* TODO(bkjg): maybe add cases when the name is not in the tree */
DEF_TEST(uc_get_rate_by_name) {
  double want_ret_value_for_test2;
  metric_family_t *fam_test2 = create_metric_family_for_test2(
      "test2-rate-by-name", &want_ret_value_for_test2, 6,
      (counter_t[]){432, 567, 703, 1743, 5623, 20008},
      (uint64_t[]){16743181, 54364233, 432423, 3213321, 53452323, 2365432});

  struct {
    int want_get;
    gauge_t ret_value;
    gauge_t want_ret_value;
    metric_family_t *fam;
    double **updates;
    size_t *num_updates;
  } cases[] = {
      {
          .fam = create_metric_family_for_test1(
              "test1-rate-by-name", 3, (gauge_t[]){69.54, 95.67, 45.87}),
          .want_ret_value = 45.87,
          .num_updates = (size_t[]){0, 0, 0},
      },
      {
          .fam = fam_test2,
          .want_ret_value = want_ret_value_for_test2,
          .num_updates = (size_t[]){0, 0, 0, 0, 0, 0},
      },
      {
          .fam = create_metric_family_for_test3("test3-rate-by-name", 2,
                                                (gauge_t[]){436.54, 543.6}),
          .want_ret_value = 543.6,
          .num_updates = (size_t[]){0, 0},
      },
      {
          .fam = create_metric_family_for_test4("test4-rate-by-name"),
          .want_ret_value = NAN,
          .num_updates = (size_t[]){0},
      },
      {
          .fam = create_metric_family_for_test5("test5-rate-by-name", 4, 48,
                                                1.033, 35),
          .num_updates = (size_t[]){75, 4, 5, 0},
          .updates =
              (double *[]){
                  (double[]){4159.654,   9603.94667, 6060.77104, 20.11649,
                             21.07854,   7963.46534, 179.10527,  44.50653,
                             176.80966,  8881.67928, 5330.64572, 4725.76119,
                             41.95417,   44.85246,   3917.1775,  4252.7976,
                             82.38465,   91.43321,   553.65691,  3162.53235,
                             21.63984,   9750.3053,  39.8265,    3745.03322,
                             565.97145,  2500.5585,  5454.60361, 3249.507,
                             9847.01554, 4695.82556, 307.72055,  60.01705,
                             7245.61576, 1777.3541,  510.62964,  5821.57195,
                             8501.10822, 1017.39535, 5747.66313, 730.39328,
                             2036.318,   600.15378,  4453.83351, 9879.11423,
                             9020.87809, 3403.07313, 9429.83863, 11.328,
                             50.28723,   80.87675,   3841.93743, 2270.33075,
                             862.9549,   83.00705,   7178.40826, 2368.45065,
                             3153.68087, 458.31409,  4949.40529, 862.93844,
                             8778.1962,  95.5752,    8604.12661, 274.7418,
                             2123.17802, 614.29148,  1915.66805, 7996.38788,
                             93.39166,   832.57039,  5094.52733, 58.69212,
                             4746.39854, 722.9694,   6768.01305},
                  (double[]){26.24206, 29.78883, 35.16503, 28.52225},
                  (double[]){45.65, 78.435, 34.324, 57.45, 67.5743},
                  (double[]){}},
          .want_ret_value = NAN,
      },
      {
          .fam = create_metric_family_for_test6(
              "test6-rate-by-name", 6, 47,
              (double[]){15.59219,  15.72268,  46.7186,   47.36406,  71.31454,
                         156.26318, 165.59628, 190.89673, 198.56488, 204.75426,
                         211.34163, 219.18405, 327.3248,  332.87301, 340.41396,
                         352.51638, 397.20861, 427.08085, 432.74217, 488.64796,
                         531.66796, 562.29682, 582.26084, 601.33359, 607.72042,
                         609.88149, 616.78043, 633.19167, 635.81112, 649.09976,
                         718.85353, 760.45129, 770.40098, 782.68583, 785.02281,
                         812.37927, 835.96259, 841.42108, 853.20455, 865.72175,
                         873.74441, 880.88184, 900.20886, 973.97989, 976.72908,
                         979.7941,  991.89657}),
          .num_updates = (size_t[]){68, 8, 0, 0, 9, 11},
          .updates =
              (double *[]){
                  (double[]){4682.42335, 420.549,    565.187,    3954.73905,
                             643.72826,  900.67591,  821.51997,  9915.12095,
                             2913.60702, 645.39755,  6423.67358, 430.46203,
                             87.11382,   238.90511,  509.10209,  720.37444,
                             22.35617,   0.94613,    6630.98218, 833.84097,
                             613.26349,  125.61267,  528,        1371.68056,
                             5650.3303,  8102.43229, 75.91657,   997.04227,
                             7286.45926, 838.14153,  421.39604,  9606.11436,
                             65.73329,   34.49047,   15.50609,   513.17177,
                             57.07583,   6.14731,    6138.069,   9.2008,
                             590.09268,  646.89925,  42.801,     95.51713,
                             64.20834,   953.93277,  590.35192,  994.31,
                             9303.344,   4821.80711, 560.28822,  295.28733,
                             1.69721,    326,        769.3479,   586.82939,
                             756.13839,  9.4598,     240.556,    242.28105,
                             481.93336,  314.77436,  639.6,      3495.76385,
                             226.34926,  6616.96188, 2966.60109, 1549.4343},
                  (double[]){645.43, 745.425, 989432.3423, 89.432, 678.423,
                             903.54, 589.34, 569.54},
                  (double[]){}, (double[]){},
                  (double[]){567.432, 678.43, 543.5346, 895.3, 234.543, 178.453,
                             864.23, 900.543, 4235},
                  (double[]){42342.4324, 543.423, 678.435, 245.325, 8753.6456,
                             906.5435, 6874.634, 674.34, 789.534, 543.543,
                             654.43}},
          .want_ret_value = 718.85353,
      },
      {
          .fam =
              create_metric_family_for_test7("test7-rate-by-name", 10, 532, 98),
          .num_updates = (size_t[]){36, 0, 0, 0, 0, 3, 78, 45, 9, 10},
          .updates =
              (double *[]){
                  (double[]){8563.48484, 7737.26182, 8461.55369, 7345.18085,
                             6922.39556, 9826.36599, 8285.47882, 9242.88802,
                             1879.60733, 2017.17049, 9946.55565, 5047.35425,
                             5956.43188, 3637.33311, 2275.67666, 5241.08879,
                             3752.92924, 4518.3078,  5574.17357, 444.83081,
                             8731.81445, 4428.15217, 5869.54926, 4006.06406,
                             6442.546,   8566.80388, 6627.0215,  2276.11951,
                             7684.56221, 9571.34356, 7074.81794, 3457.6747,
                             747.35085,  9269.56873, 6193.34178, 1339.58513},
                  (double[]){},
                  (double[]){},
                  (double[]){},
                  (double[]){},
                  (double[]){125.543, 642.5345, 7564.25},
                  (double[]){22195.68641, 20118.4779,  27788.0238,  21660.95577,
                             13109.17633, 45481.75486, 60772.61211, 15502.82054,
                             64807.98058, 80269.72468, 17138.73195, 36947.45858,
                             13698.34813, 24327.0788,  2310.4785,   47154.80257,
                             42025.80305, 34341.75166, 50052.9567,  76862.15098,
                             96078.89941, 57201.03172, 9861.25572,  87059.45503,
                             26535.97795, 12066.52634, 35314.64922, 51132.33884,
                             50256.12067, 6097.42143,  92925.59698, 78582.07681,
                             89309.56916, 20259.27171, 3171.51752,  46472.43649,
                             84907.56782, 4987.50946,  89845.81376, 5311.01208,
                             34311.3633,  53558.35518, 91755.7273,  2035.08046,
                             53700.1584,  91605.23363, 38349.06227, 17148.10497,
                             36924.49226, 39106.15963, 60389.16114, 10506.33747,
                             34272.86238, 40229.98104, 29947.99145, 80874.60114,
                             94268.40706, 31144.25066, 2994.40595,  78146.89748,
                             82519.67508, 39423.41723, 23379.64765, 74648.37952,
                             85765.40285, 24009.43512, 69162.25593, 55258.07303,
                             8626.32408,  42564.13693, 33160.6236,  11919.79605,
                             13647.54085, 98696.50118, 18862.64208, 28554.92706,
                             83405.96533, 47459.24177},
                  (double[]){66075.33301, 81245.72895, 79264.1119,  71294.4439,
                             69789.35421, 44400.87995, 61855.39209, 79255.32008,
                             72927.19757, 40667.6036,  72490.18809, 21742.83491,
                             89137.57279, 31740.68086, 97752.72254, 33346.88699,
                             60235.58325, 43753.29565, 58615.5088,  5223.76648,
                             64124.03391, 90914.4585,  12524.41179, 70676.85368,
                             87377.49,    47835.43439, 7397.05884,  59284.78323,
                             5100.00019,  64840.49217, 35805.81813, 75682.15466,
                             1400.07204,  77499.47769, 65685.72436, 5006.66765,
                             35944.44801, 60647.21456, 96690.41787, 26233.1071,
                             40308.1231,  22477.09745, 3575.69941,  55313.28453,
                             44959.21375},
                  (double[]){47817.79309, 61037.21185, 38306.19433, 37380.72465,
                             45474.26121, 35413.80266, 23993.95002, 52741.09605,
                             7207.58457},
                  (double[]){47415.11645, 2098.94152, 91558.98643, 53955.27033,
                             85504.56864, 13830.14932, 9515.16889, 98472.57733,
                             84736.51036, 21878.60046},
              },
          .want_ret_value = 47432,
      },
  };

  uc_init();
  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); ++i) {
    printf("## Case %zu:\n", i);
    for (size_t k = 0; k < cases[i].fam->metric.num; ++k) {
      for (size_t l = 0; l <= k; ++l) {
        for (size_t j = 0; j < cases[i].num_updates[l]; ++j) {
          distribution_update(cases[i].fam->metric.ptr[k].value.distribution,
                              cases[i].updates[l][j]);
        }
      }
    }

    CHECK_ZERO(uc_update(cases[i].fam));

    EXPECT_EQ_INT(cases[i].want_get,
                  uc_get_rate_by_name(cases[i].fam->name, &cases[i].ret_value));

    if (cases[i].want_get != -1) {
      EXPECT_EQ_DOUBLE(cases[i].want_ret_value, cases[i].ret_value);
    }

    CHECK_ZERO(metric_family_metric_reset(cases[i].fam));
    free(cases[i].fam);
  }
  uc_destroy();
  return 0;
}

DEF_TEST(uc_get_rate) {
  double want_ret_value_for_test2;
  metric_family_t *fam_test2 = create_metric_family_for_test2(
      "test2-rate", &want_ret_value_for_test2, 4,
      (counter_t[]){
          23,
          453,
          457,
          890,
      },
      (uint64_t[]){524352, 923052, 4324582, 234133});

  struct {
    int want_get;
    gauge_t ret_value;
    gauge_t want_ret_value;
    metric_family_t *fam;
    double **updates;
    size_t metric_idx;
    size_t num_metrics;
    size_t *num_updates;
  } cases[] = {
      {
          .fam = create_metric_family_for_test1(
              "test1-rate", 6,
              (gauge_t[]){6432.3, 9435.67, 8943.3, 8734.32, 123.4, 932.12}),
          .want_ret_value = 932.12,
          .metric_idx = 1,
      },
      {
          .fam = fam_test2,
          .want_ret_value = want_ret_value_for_test2,
          .metric_idx = 0,
      },
      {
          .fam = create_metric_family_for_test3(
              "test3-rate", 6,
              (gauge_t[]){43.34, 63553.54353, 342.543, 6242.53, 35533.543,
                          964.973}),
          .want_ret_value = 964.973,
          .metric_idx = 4,
      },
      {
          .fam = create_metric_family_for_test4("test4-rate"),
          .want_ret_value = NAN,
          .metric_idx = 0,
      },
      {
          .fam = create_metric_family_for_test5("test5-rate", 9, 27, 1.54, 432),
          .num_metrics = 9,
          .metric_idx = 0,
          .num_updates = (size_t[]){43, 65, 6, 1, 32, 16, 23, 8, 5},
          .updates =
              (double *[]){
                  (double[]){2670.32562, 8.85772,    600.83049,  5703.54538,
                             1739.08133, 2515.0246,  19.30971,   3172.35134,
                             5762.34467, 819.79472,  7311.4073,  511.88458,
                             5.2088,     1846.64877, 9815.45596, 2196.062,
                             5840.99974, 4757.352,   3378.23304, 83.20259,
                             74.24577,   9035.63091, 5.87648,    7912.68349,
                             9414.14394, 6127.345,   8007.79518, 88.23725,
                             7133.74698, 945.98139,  3.566,      7956.41547,
                             4571.666,   1773.844,   8990.56044, 9263.64139,
                             654.785,    76.88244,   78.38784,   6.344,
                             187.1161,   4648.711,   9443.922},
                  (double[]){7109.649,   9842.10087, 26.16383,   49.5497,
                             6484.7,     4074.25438, 6249.461,   6273.29043,
                             8429,       6874.80691, 6507.666,   88.133,
                             519,        68.0467,    804.3971,   1120,
                             396.9779,   99.825,     19.1914,    8582.19094,
                             1446.582,   5764.77423, 71.811,     6161.18363,
                             11.52392,   747.5106,   6971.94,    306.80057,
                             2674.26754, 6.52529,    6663.3,     6694.63362,
                             2706.34432, 401.02831,  874.12718,  2049.45664,
                             7496,       7.58703,    5088.6243,  6498.38184,
                             5641.83539, 1800.19701, 7216.53405, 4909.94,
                             93.30697,   7938.24451, 8.74613,    521.37181,
                             2731.8673,  802.481,    1479.3,     3456.35313,
                             87.97898,   0.27,       90.4322,    4509.87517,
                             8538.09,    8081.1361,  189.77886,  630.56143,
                             8.68015,    710.143,    89.307,     6041.92484,
                             4815.72466},
                  (double[]){9445.11956, 273, 229.71, 904.997, 2003.5755, 4347},
                  (double[]){43.5},
                  (double[]){3.88251,    8622.69754, 5281.17331, 7590.08,
                             6330.26308, 7926.19813, 346.88,     0.6204,
                             80.82698,   84.4731,    1434.652,   6849.41,
                             599.82477,  696.8198,   1344.0898,  6912.6002,
                             2.58452,    95.61217,   1156.538,   672,
                             3375.28,    516.91815,  5.4982,     7137.08,
                             20.23,      31.19527,   6342.179,   4667.27563,
                             40.7383,    763.42794,  8533.636,   3.21732},
                  (double[]){391.12495, 6932.64224, 4437.445, 8946.82128,
                             9927.98719, 6047.43497, 9342.325, 9030.56867,
                             2515.76577, 5858.64953, 8183.33711, 4420.3097,
                             47.9664, 9.70949, 9147.46386, 5.9146},
                  (double[]){7992.3,    729.4834,  839.76978,  1275.01114,
                             5909.43,   513.74586, 5471.122,   711.11551,
                             8832.203,  52.66681,  2287.56469, 8.1332,
                             6489.2,    66.08,     462,        997.04555,
                             8369.355,  4.37942,   4426.76847, 43.21212,
                             3735.8079, 1558.5,    4047.20057},
                  (double[]){6340.265, 7.8, 9926.01518, 20.487, 88.643, 651.966,
                             37.99, 1271.26502},
                  (double[]){1.43, 5.3, 352.35, 23.54, 7.32},
              },
          .want_ret_value = 432,
      },
      {
          .fam = create_metric_family_for_test6(
              "test6-rate", 5, 58,
              (double[]){
                  23.29304,   95.50701,   453.56405,  530.22468,  785.42763,
                  926.37933,  1002.6969,  1060.14069, 1215.11132, 2568.21224,
                  2938.14866, 3300.10118, 3300.33085, 3428.20534, 3472.88349,
                  3580.86563, 3606.64454, 3768.52847, 3842.06928, 3944.87221,
                  4543.6676,  4659.59252, 4829.7276,  4913.371,   5214.12859,
                  5345.23098, 5380.20076, 5823.24732, 6171.12066, 6180.80973,
                  6268.65218, 6290.94962, 6697.52335, 6974.84095, 7054.77017,
                  7261.30442, 7328.29023, 7329.21603, 7603.16742, 7661.76314,
                  7957.54298, 8030.19424, 8360.55865, 8413.20167, 8749.37191,
                  8779.55952, 9106.38549, 9161.68738, 9218.5289,  9226.8517,
                  9249.89252, 9250.26072, 9257.62582, 9381.11376, 9470.46732,
                  9777.59519, 9855.1613,  9902.61134}),
          .num_metrics = 5,
          .metric_idx = 4,
          .num_updates = (size_t[]){43, 0, 3, 87, 9},
          .updates =
              (double *[]){
                  (double[]){
                      586.6818,   2491.64768, 9748.31608, 1331.71514, 34.48175,
                      468.64908,  7624.37857, 3127.91955, 3225.00797, 519.46732,
                      5058.4099,  436.466,    2290.86508, 6.81668,    15.96,
                      956.3,      202.29645,  46.944,     439.80202,  747.99,
                      89.93798,   548.36893,  966.86775,  1727.41981, 857.57512,
                      32.792,     5117.1274,  560.32,     71.9381,    789.55573,
                      1700.031,   6439.5839,  0.70844,    7461.16,    8057.22,
                      3991.35143, 308.257,    83.317,     525.98897,  872.77111,
                      1.105,      9048.3,     4058.63251},
                  (double[]){}, (double[]){934.23937, 1801.99573, 6672.02075},
                  (double[]){6855.7434,  5548.16,    3917.22267, 5034.4843,
                             9142.01725, 7403.0041,  640.93017,  386.3,
                             2283.50043, 33.2602,    5079.2664,  3913.80204,
                             7747.8776,  97.84419,   6884.1773,  1323.2855,
                             9632,       2.54336,    8895.533,   376.67867,
                             403605,     857.94132,  5543.8011,  5553.75,
                             4536,       39.24726,   21.84795,   2208.66299,
                             798.70434,  35.35057,   440.36365,  3912.57189,
                             2439.56096, 6.75315,    111.92087,  7445.8158,
                             9329.35,    5.94265,    6644.66,    3873.61732,
                             6588.7005,  232.93697,  4.93326,    8513.95089,
                             771.48422,  59.46394,   807.64548,  1629.75452,
                             79.55595,   5083.84758, 4.2592,     2098.12329,
                             4641.66571, 78.41357,   6423.27,    7419.08,
                             8126.2226,  6002.10394, 46.66,      6674.81407,
                             3347.08678, 826.47996,  2428.1,     9488.97,
                             91.20643,   49.199,     63.43308,   4960.979,
                             6218.37,    85.81701,   6.16985,    4083.36722,
                             5192.34,    50.05135,   60.386,     8013.11938,
                             4566.7578,  45.91432,   4.36311,    1816.54064,
                             7044.25,    562.31608,  34.968,     6957.69971,
                             4869.61676, 8593.19673, 6.66532},
                  (double[]){57.8407, 34.23938, 5106.707, 227.9787, 7559.3564,
                             5842.238, 72.20576, 9772067, 6875.29363}},
          .want_ret_value = 5214.12859,
      },
      {
          .fam = create_metric_family_for_test7("test7-rate", 7, 35, 743.2),
          .metric_idx = 5,
          .num_metrics = 7,
          .num_updates = (size_t[]){65, 8, 34, 54, 0, 4, 0},
          .updates =
              (double *[]){
                  (double[]){58.46556,   7761.41,    9889.11192, 3604.80996,
                             6020.93,    7067.69377, 9745.73451, 7088.67946,
                             659.21898,  9571.49257, 3233.33374, 986.32218,
                             3811.86,    789.82381,  80.49834,   384.19468,
                             9531.83359, 2869.513,   2780.50138, 1916.27116,
                             481521,     165544,     1483.07793, 140.68081,
                             9629.3361,  8269.13232, 824,        6896.66217,
                             53.13249,   466.31943,  8827.59243, 9229.70371,
                             9350.02409, 94.5,       1000.06,    9178.52648,
                             7185.58,    418.74706,  7525.18694, 564.06668,
                             857.50299,  9437.36479, 4219.05814, 6940.59699,
                             1544.42652, 630.58583,  47.439,     6294.864,
                             3017.36522, 4322,       139.9737,   2855.57402,
                             722,        62,         3918.02204, 1624.54405,
                             9300.84653, 9.296,      990.11229,  7.46077,
                             5664.11455, 29.57293,   88.29354,   6554.45475,
                             6.97814

                  },
                  (double[]){8326.42, 2133.71258, 72125, 671, 2339.36629,
                             9376.804, 2727.196, 3044.52818},
                  (double[]){3044.88726, 854.203,    8006.27492, 3937.21303,
                             4292.884,   1649.40188, 7767.44863, 4300.284,
                             229.74928,  58.95,      257.05554,  9131.4131,
                             15.9456,    3.394,      20.08992,   3859.15014,
                             9271.32071, 2312.505,   6384.75,    4899.42264,
                             55.55931,   31.32,      8904.37297, 6.3231,
                             175109,     8913.9,     7.09671,    348.92836,
                             655.4057,   2.812,      71.62842,   5424.88764,
                             5478.52253, 7.035},
                  (double[]){6276.47216, 1451.31497, 1244.63787, 4555.9668,
                             345.20624,  3.717,      7651.18288, 770.11381,
                             8594.99503, 4955.732,   16.86272,   24.13134,
                             5.96506,    6826.44451, 247.57453,  561.73182,
                             67.94826,   5946.627,   822.5379,   1387.66626,
                             2.20673,    47.176,     2030.56956, 2418.87588,
                             3197.84958, 9591.823,   9975.8583,  1.89939,
                             736.47784,  264.75,     1451.60536, 365.6902,
                             158.66189,  9672.77353, 9.39,       1865.08445,
                             8703.54448, 0.1336,     93.18407,   423.52841,
                             756.11458,  7975.14468, 36.07,      6243.78084,
                             96.536,     5784.7,     1471.79476, 4534.4573,
                             120.53,     629,        1064.8606,  8325.47099,
                             6347.49398, 1},
                  (double[]){},
                  (double[]){9.5663, 34.9285, 282.75, 7719.3},
                  (double[]){},
              },
          .want_ret_value = NAN,
      },
  };

  uc_init();
  for (size_t i = 0; i < (sizeof(cases) / sizeof(cases[0])); ++i) {
    printf("## Case %zu:\n", i);
    for (size_t k = 0; k < cases[i].num_metrics; ++k) {
      for (size_t l = 0; l <= k; ++l) {
        for (size_t j = 0; j < cases[i].num_updates[l]; ++j) {
          distribution_update(cases[i].fam->metric.ptr[k].value.distribution,
                              cases[i].updates[l][j]);
        }
      }
    }

    CHECK_ZERO(uc_update(cases[i].fam));

    EXPECT_EQ_INT(cases[i].want_get,
                  uc_get_rate(&cases[i].fam->metric.ptr[cases[i].metric_idx],
                              &cases[i].ret_value));

    if (cases[i].want_get != -1) {
      EXPECT_EQ_DOUBLE(cases[i].want_ret_value, cases[i].ret_value);
    }

    CHECK_ZERO(metric_family_metric_reset(cases[i].fam));
    free(cases[i].fam);
  }
  uc_destroy();
  return 0;
}

int main() {
  RUN_TEST(uc_update);
  RUN_TEST(uc_get_percentile_by_name);
  RUN_TEST(uc_get_percentile);
  RUN_TEST(uc_get_rate_by_name);
  RUN_TEST(uc_get_rate);

  END_TEST;
}
