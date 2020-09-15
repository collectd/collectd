/**
 * collectd - src/daemon/curl_stats_test.c
 * Copyright (C) 2020       Barbara 'bkjg' Kaczorowska
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
 *   Barbara 'bkjg' Kaczorowska <bkjg at google.com>
 */

#include "collectd.h"
#include "curl_stats.h"
#include "testing.h"

DEF_TEST(curl_stats_from_config) {
  struct {
    oconfig_item_t ci;
    size_t want_get_num_attr;
    char **want_get_enabled_attr;
    int *want_get_metric_type;
    size_t *want_get_num_metrics;
    distribution_t **want_get_distributions;
    size_t num_enabled_attr;
    const char ***want_get_label_attributes;
  } cases[] =
      {
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "SizeDistributionType",
                                  .values_num = 1,
                                  .values =
                                      (oconfig_value_t[]){
                                          {
                                              .type = OCONFIG_TYPE_STRING,
                                              .value.string = "test",
                                          },
                                      },

                              },
                          },
                      .children_num = 1,
                  },
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "SpeedDistributionType",
                                  .values_num = 1,
                                  .values =
                                      (oconfig_value_t[]){
                                          {
                                              .type = OCONFIG_TYPE_NUMBER,
                                              .value.number = 5,
                                          },
                                      },

                              },
                          },
                      .children_num = 1,
                  },
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "SizeDistributionType",
                                  .values_num = 2,
                                  .values =
                                      (oconfig_value_t[]){
                                          {
                                              .type = OCONFIG_TYPE_STRING,
                                              .value.string = "linear",
                                          },
                                          {
                                              .type = OCONFIG_TYPE_STRING,
                                              .value.string = "exponential",
                                          },
                                      },

                              },
                          },
                      .children_num = 1,
                  },
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "TimeDistributionType",
                                  .values_num = 1,
                                  .values =
                                      (oconfig_value_t[]){
                                          {
                                              .type = OCONFIG_TYPE_STRING,
                                              .value.string = "exponential",
                                          },
                                      },

                              },
                              {.key = "TimeBase",
                               .values_num = 1,
                               .values = (oconfig_value_t[]){{
                                   .type = OCONFIG_TYPE_BOOLEAN,
                                   .value.boolean = 1,
                               }}},
                          },
                      .children_num = 2,
                  },
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "SpeedDistributionType",
                                  .values_num = 1,
                                  .values =
                                      (oconfig_value_t[]){
                                          {
                                              .type = OCONFIG_TYPE_STRING,
                                              .value.string = "custom",
                                          },
                                      },

                              },
                              {.key = "SizeFactor",
                               .values_num = 1,
                               .values = (oconfig_value_t[]){{
                                   .type = OCONFIG_TYPE_NUMBER,
                                   .value.number = 5,
                               }}},
                          },
                      .children_num = 2,
                  },
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "SpeedDistributionType",
                                  .values_num = 1,
                                  .values =
                                      (oconfig_value_t[]){
                                          {
                                              .type = OCONFIG_TYPE_STRING,
                                              .value.string = "custom",
                                          },
                                      },

                              },
                              {.key = "SizeFactor",
                               .values_num = 1,
                               .values = (oconfig_value_t[]){{
                                   .type = OCONFIG_TYPE_NUMBER,
                                   .value.number = 5,
                               }}},
                              {.key = "SpeedBoundaries",
                               .values_num = 3,
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 5,
                                       },
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 7.8,
                                       },
                                       {
                                           .type = OCONFIG_TYPE_STRING,
                                           .value.string = "7",
                                       }}}},
                      .children_num = 3,
                  },
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "TimeDistributionType",
                                  .values_num = 1,
                                  .values =
                                      (oconfig_value_t[]){
                                          {
                                              .type = OCONFIG_TYPE_STRING,
                                              .value.string = "exponential",
                                          },
                                      },

                              },
                              {.key = "TimeFactor",
                               .values_num = 1,
                               .values = (oconfig_value_t[]){{
                                   .type = OCONFIG_TYPE_NUMBER,
                                   .value.number = 5,
                               }}},
                              {.key = "TimeBase",
                               .values_num = 1,
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 0.5,
                                       }}}},
                      .children_num = 3,
                  },
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "AnyAttribute",
                              },
                          },
                      .children_num = 1,
                  },
          },
          {.ci =
               {
                   .children =
                       (oconfig_item_t[]){
                           {
                               .values_num = 1,
                               .key = "SizeDistributionType",
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_STRING,
                                           .value.string = "linear",
                                       }},
                           },
                       },
                   .children_num = 1,
               },
           .want_get_num_attr = 3,
           .want_get_metric_type =
               (int[]){METRIC_TYPE_DISTRIBUTION, METRIC_TYPE_DISTRIBUTION,
                       METRIC_TYPE_DISTRIBUTION},
           .want_get_distributions = (distribution_t *[]){NULL, NULL, NULL},
           .num_enabled_attr = 0,
           .want_get_num_metrics = (size_t[]){0, 0, 0}},
          {.ci =
               {
                   .children =
                       (oconfig_item_t[]){
                           {
                               .values_num = 1,
                               .key = "SizeUpload",
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_BOOLEAN,
                                           .value.boolean = 1,
                                       }},
                           },
                       },
                   .children_num = 1,
               },
           .want_get_num_attr = 3,
           .want_get_metric_type =
               (int[]){METRIC_TYPE_DISTRIBUTION, METRIC_TYPE_DISTRIBUTION,
                       METRIC_TYPE_DISTRIBUTION},
           .want_get_distributions =
               (distribution_t *[]){distribution_new_linear(1024, 8),
                                    distribution_new_linear(1024, 16),
                                    distribution_new_linear(1024, 0.001)},
           .num_enabled_attr = 1,
           .want_get_num_metrics = (size_t[]){1, 0, 0},
           .want_get_enabled_attr = (char *[]){"SizeUpload"},
           .want_get_label_attributes =
               (const char **[]){(const char *[]){"SizeUpload"},
                                 (const char *[]){}, (const char *[]){}}},
          {.ci =
               {
                   .children =
                       (oconfig_item_t[]){
                           {
                               .values_num = 1,
                               .key = "SpeedDownload",
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_BOOLEAN,
                                           .value.boolean = 1,
                                       }},
                           },
                           {
                               .values_num = 1,
                               .key = "ContentLengthDownload",
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_BOOLEAN,
                                           .value.boolean = 1,
                                       }},
                           },
                           {
                               .values_num = 1,
                               .key = "TimeNumBuckets",
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 256,
                                       }},
                           },
                           {
                               .values_num = 1,
                               .key = "SpeedFactor",
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 7.2,
                                       }},
                           },
                           {
                               .values_num = 1,
                               .key = "SizeDistributionType",
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_STRING,
                                           .value.string = "exponential",
                                       }},
                           },
                       },
                   .children_num = 5,
               },
           .want_get_num_attr = 3,
           .want_get_metric_type =
               (int[]){METRIC_TYPE_DISTRIBUTION, METRIC_TYPE_DISTRIBUTION,
                       METRIC_TYPE_DISTRIBUTION},
           .want_get_distributions =
               (distribution_t *[]){
                   distribution_new_exponential(1024, 2.0, 2.0),
                   distribution_new_linear(1024, 7.2),
                   distribution_new_linear(256, 0.001)},
           .num_enabled_attr = 2,
           .want_get_num_metrics = (size_t[]){1, 1, 0},
           .want_get_label_attributes =
               (const char **[]){(const char *[]){"ContentLengthDownload"},
                                 (const char *[]){"SpeedDownload"},
                                 (const char *[]){}},
           .want_get_enabled_attr =
               (char *[]){"SpeedDownload", "ContentLengthDownload"}},
          {.ci =
               {
                   .children =
                       (oconfig_item_t[]){
                           {
                               .values_num = 1,
                               .key = "PretransferTime",
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_BOOLEAN,
                                           .value.boolean = 1,
                                       }},
                           },
                           {
                               .values_num = 1,
                               .key = "StarttransferTime",
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_BOOLEAN,
                                           .value.boolean = 1,
                                       }},
                           },
                           {
                               .values_num = 1,
                               .key = "TimeNumBuckets",
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 256,
                                       }},
                           },
                           {
                               .values_num = 1,
                               .key = "SpeedFactor",
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 4.2,
                                       }},
                           },
                           {
                               .values_num = 1,
                               .key = "TimeDistributionType",
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_STRING,
                                           .value.string = "custom",
                                       }},
                           },
                           {
                               .values_num = 5,
                               .key = "TimeBoundaries",
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 25,
                                       },
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 50,
                                       },
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 100,
                                       },
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 200,
                                       },
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 400,
                                       }},
                           },
                       },
                   .children_num = 6,
               },
           .want_get_num_attr = 3,
           .want_get_metric_type =
               (int[]){METRIC_TYPE_DISTRIBUTION, METRIC_TYPE_DISTRIBUTION,
                       METRIC_TYPE_DISTRIBUTION},
           .want_get_distributions =
               (distribution_t *[]){
                   distribution_new_exponential(1024, 2.0, 2.0),
                   distribution_new_linear(1024, 4.3),
                   distribution_new_custom(5,
                                           (double[]){25, 50, 100, 200, 400})},
           .num_enabled_attr = 2,
           .want_get_num_metrics = (size_t[]){0, 0, 2},
           .want_get_label_attributes =
               (const char **[]){
                   (const char *[]){}, (const char *[]){},
                   (const char *[]){"PretransferTime", "StarttransferTime"}},
           .want_get_enabled_attr =
               (char *[]){"PretransferTime", "StarttransferTime"}},
      };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    printf("## Case %zu: \n", i);
    curl_stats_t *s;

    s = curl_stats_from_config(&cases[i].ci);

    if (cases[i].want_get_metric_type == NULL &&
        cases[i].want_get_enabled_attr == NULL) {
      EXPECT_EQ_PTR(NULL, s);
    } else {
      CHECK_NOT_NULL(s);

      size_t num_attr = 0;
      metric_family_t **fam =
          curl_stats_get_metric_families_for_attributes(s, &num_attr);

      EXPECT_EQ_UINT64(cases[i].want_get_num_attr, num_attr);

      for (size_t family = 0; family < num_attr; ++family) {
        EXPECT_EQ_INT(cases[i].want_get_metric_type[family], fam[family]->type);
        EXPECT_EQ_UINT64(cases[i].want_get_num_metrics[family],
                         fam[family]->metric.num);

        if (fam[family]->type == METRIC_TYPE_DISTRIBUTION) {
          for (size_t j = 0; j < fam[family]->metric.num; ++j) {
            const char *label =
                metric_label_get(&fam[family]->metric.ptr[j], "Attributes");

            EXPECT_EQ_STR(cases[i].want_get_label_attributes[family][j], label);

            buckets_array_t buckets =
                get_buckets(fam[family]->metric.ptr[j].value.distribution);
            buckets_array_t wanted_buckets =
                get_buckets(cases[i].want_get_distributions[family]);

            EXPECT_EQ_UINT64(wanted_buckets.num_buckets, buckets.num_buckets);
            for (size_t k = 0; k < wanted_buckets.num_buckets; ++k) {
              EXPECT_EQ_DOUBLE(wanted_buckets.buckets[k].maximum,
                               buckets.buckets[k].maximum);
              EXPECT_EQ_UINT64(wanted_buckets.buckets[k].bucket_counter,
                               buckets.buckets[k].bucket_counter);
            }

            free(buckets.buckets);
            free(wanted_buckets.buckets);
          }
        }
      }

      size_t num_enabled_attr = 0;
      char **enabled_attr =
          curl_stats_get_enabled_attributes(s, &num_enabled_attr);

      EXPECT_EQ_UINT64(cases[i].num_enabled_attr, num_enabled_attr);

      if (num_enabled_attr > 0) {
        CHECK_NOT_NULL(enabled_attr);
      }

      for (size_t attr = 0; attr < num_enabled_attr; ++attr) {
        EXPECT_EQ_STR(cases[i].want_get_enabled_attr[attr], enabled_attr[attr]);
      }

      for (size_t family = 0; family < num_attr; ++family) {
        metric_family_free(fam[family]);
      }

      free(fam);

      for (size_t dist = 0; dist < cases[i].want_get_num_attr; ++dist) {
        distribution_destroy(cases[i].want_get_distributions[dist]);
      }

      for (size_t attr = 0; attr < num_enabled_attr; ++attr) {
        free(enabled_attr[attr]);
      }

      free(enabled_attr);
      curl_stats_destroy(s);
    }
  }

  return 0;
}

DEF_TEST(curl_stats_get_metric_families_for_attributes) {
  struct {
    oconfig_item_t ci;
    size_t *num_attr;
    size_t want_get_num_attr;
    metric_family_t *want_get_metric_families;
  } cases[] = {
      {
          .ci =
              {
                  .children =
                      (oconfig_item_t[]){
                          {
                              .values_num = 1,
                              .key = "SpeedUpload",
                              .values = (oconfig_value_t[]){{
                                  .type = OCONFIG_TYPE_BOOLEAN,
                                  .value.boolean = 1,
                              }},
                          },
                          {
                              .values_num = 1,
                              .key = "SpeedDownload",
                              .values = (oconfig_value_t[]){{
                                  .type = OCONFIG_TYPE_BOOLEAN,
                                  .value.boolean = 1,
                              }},
                          },
                      },
                  .children_num = 2,
              },
          .want_get_metric_families = NULL,
      },
      {
          .ci =
              {
                  .children =
                      (oconfig_item_t[]){
                          {
                              .values_num = 1,
                              .key = "HeaderSize",
                              .values = (oconfig_value_t[]){{
                                  .type = OCONFIG_TYPE_BOOLEAN,
                                  .value.boolean = 1,
                              }},
                          },
                          {
                              .values_num = 1,
                              .key = "ContentLengthUpload",
                              .values = (oconfig_value_t[]){{
                                  .type = OCONFIG_TYPE_BOOLEAN,
                                  .value.boolean = 1,
                              }},
                          },
                          {
                              .values_num = 1,
                              .key = "Time",
                              .values = (oconfig_value_t[]){{
                                  .type = OCONFIG_TYPE_BOOLEAN,
                                  .value.boolean = 1,
                              }},
                          },
                      },
                  .children_num = 3,
              },
          .want_get_metric_families = NULL,
          .num_attr = (size_t *)2,
      },
      {
          .ci =
              {
                  .children =
                      (oconfig_item_t[]){
                          {
                              .values_num = 1,
                              .key = "HeaderSize",
                              .values = (oconfig_value_t[]){{
                                  .type = OCONFIG_TYPE_STRING,
                                  .value.string = "1",
                              }},
                          },
                      },
                  .children_num = 1,
              },
          .want_get_metric_families = NULL,
      },
      {
          .ci =
              {
                  .children =
                      (oconfig_item_t[]){
                          {
                              .values_num = 1,
                              .key = "RequestSize",
                              .values = (oconfig_value_t[]){{
                                  .type = OCONFIG_TYPE_BOOLEAN,
                                  .value.boolean = 1,
                              }},
                          },
                          {
                              .values_num = 1,
                              .key = "NamelookupTime",
                              .values = (oconfig_value_t[]){{
                                  .type = OCONFIG_TYPE_BOOLEAN,
                                  .value.boolean = 1,
                              }},
                          },
                          {
                              .values_num = 1,
                              .key = "NumConnects",
                              .values = (oconfig_value_t[]){{
                                  .type = OCONFIG_TYPE_BOOLEAN,
                                  .value.boolean = 1,
                              }},
                          },
                      },
                  .children_num = 3,
              },
          .want_get_metric_families =
              (metric_family_t[]){
                  {.type = METRIC_TYPE_GAUGE, .name = "Count"},
                  {
                      .type = METRIC_TYPE_DISTRIBUTION,
                      .name = "Size",
                      .metric.num = 1,
                      .metric.ptr =
                          (metric_t[]){
                              {.value.distribution =
                                   distribution_new_linear(1024, 8),
                               .label = (label_pair_t[]){{"Attributes",
                                                          "RequestSize"}}}},
                  },
                  {
                      .type = METRIC_TYPE_DISTRIBUTION,
                      .name = "SPEED",
                  },
                  {
                      .type = METRIC_TYPE_DISTRIBUTION,
                      .name = "Time",
                      .metric.num = 1,
                      .metric.ptr =
                          (metric_t[]){
                              {.value.distribution =
                                   distribution_new_linear(1024, 0.001),
                               .label = (label_pair_t[]){{"Attributes",
                                                          "NamelookupTime"}}}},
                  },
              },
          .num_attr = (size_t *)2,
          .want_get_num_attr = 3,
      },
      {
          .ci =
              {
                  .children =
                      (oconfig_item_t[]){
                          {
                              .values_num = 1,
                              .key = "SizeDownload",
                              .values =
                                  (oconfig_value_t[]){
                                      {
                                          .type = OCONFIG_TYPE_BOOLEAN,
                                          .value.boolean = 1,
                                      }},
                          },
                          {
                              .values_num = 1,
                              .key = "RequestSize",
                              .values =
                                  (oconfig_value_t[]){
                                      {
                                          .type = OCONFIG_TYPE_BOOLEAN,
                                          .value.boolean = 1,
                                      }},
                          },
                          {
                              .values_num = 1,
                              .key = "RedirectCount",
                              .values =
                                  (oconfig_value_t[]){
                                      {
                                          .type = OCONFIG_TYPE_BOOLEAN,
                                          .value.boolean = 1,
                                      }},
                          },
                          {
                              .values_num = 1,
                              .key = "SizeDistributionType",
                              .values =
                                  (oconfig_value_t[]){
                                      {
                                          .type = OCONFIG_TYPE_STRING,
                                          .value.string = "Exponential",
                                      }},
                          }},
                  .children_num = 4,
              },
          .want_get_metric_families =
              (metric_family_t[]){
                  {.type = METRIC_TYPE_GAUGE,
                   .name = "Count",
                   .metric.num = 1,
                   .metric.ptr =
                       (metric_t[]){{.label =
                                         (label_pair_t[]){{"Attributes",
                                                           "RedirectCount"}}}}},
                  {
                      .type = METRIC_TYPE_DISTRIBUTION,
                      .name = "Size",
                      .metric.num = 2,
                      .metric.ptr =
                          (metric_t[]){
                              {.value.distribution =
                                   distribution_new_exponential(1024, 2, 2),
                               .label = (label_pair_t[]){{"Attributes",
                                                          "SizeDownload"}}},
                              {.value.distribution =
                                   distribution_new_exponential(1024, 2, 2),
                               .label = (label_pair_t[]){{"Attributes",
                                                          "RequestSize"}}}},
                  },
                  {
                      .type = METRIC_TYPE_DISTRIBUTION,
                      .name = "SPEED",
                  },
                  {
                      .type = METRIC_TYPE_DISTRIBUTION,
                      .name = "Time",
                  },
              },
          .num_attr = (size_t *)2,
          .want_get_num_attr = 3,
      },
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    printf("## Case %zu: \n", i);
    curl_stats_t *s;

    s = curl_stats_from_config(&cases[i].ci);

    metric_family_t **fam =
        curl_stats_get_metric_families_for_attributes(s, cases[i].num_attr);

    if (cases[i].want_get_metric_families == NULL) {
      EXPECT_EQ_PTR(NULL, fam);
    } else {
      CHECK_NOT_NULL(cases[i].want_get_metric_families);
      EXPECT_EQ_UINT64(cases[i].want_get_num_attr, *cases[i].num_attr);

      for (size_t family = 0; family < *cases[i].num_attr; ++family) {
        EXPECT_EQ_INT(cases[i].want_get_metric_families[family].type,
                      fam[family]->type);
        EXPECT_EQ_UINT64(cases[i].want_get_metric_families[family].metric.num,
                         fam[family]->metric.num);
        EXPECT_EQ_STR(cases[i].want_get_metric_families[family].name,
                      fam[family]->name);

        if (fam[family]->type == METRIC_TYPE_DISTRIBUTION) {
          for (size_t j = 0; j < fam[family]->metric.num; ++j) {
            const char *label =
                metric_label_get(&fam[family]->metric.ptr[j], "Attributes");
            const char *want_label = metric_label_get(
                &cases[i].want_get_metric_families[family]->metric.ptr[j],
                "Attributes");
            EXPECT_EQ_STR(want_label, label);

            buckets_array_t buckets =
                get_buckets(fam[family]->metric.ptr[j].value.distribution);
            buckets_array_t wanted_buckets =
                get_buckets(cases[i]
                                .want_get_metric_families[family]
                                .metric.ptr[j]
                                .value.distribution);

            EXPECT_EQ_UINT64(wanted_buckets.num_buckets, buckets.num_buckets);
            for (size_t k = 0; k < wanted_buckets.num_buckets; ++k) {
              EXPECT_EQ_DOUBLE(wanted_buckets.buckets[k].maximum,
                               buckets.buckets[k].maximum);
              EXPECT_EQ_UINT64(wanted_buckets.buckets[k].bucket_counter,
                               buckets.buckets[k].bucket_counter);
            }

            free(buckets.buckets);
            free(wanted_buckets.buckets);
          }
        }
      }
      for (size_t family = 0; family < *cases[i].num_attr; ++family) {
        metric_family_free(fam[family]);
      }

      free(fam);
    }
    curl_stats_destroy(s);
  }

  return 0;
}

DEF_TEST(curl_stats_get_enabled_attributes) {
  struct {
    oconfig_item_t ci;
    size_t *num_enabled_attr;
    size_t want_get_num_enabled_attr;
    char **want_get_enabled_attr;
  } cases[] =
      {
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .values_num = 1,
                                  .key = "RequestSize",
                                  .values = (oconfig_value_t[]){{
                                      .type = OCONFIG_TYPE_BOOLEAN,
                                      .value.boolean = 1,
                                  }},
                              },
                          },
                      .children_num = 1,
                  },
              .want_get_enabled_attr = NULL,
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .values_num = 1,
                                  .key = "NumConnects",
                                  .values = (oconfig_value_t[]){{
                                      .type = OCONFIG_TYPE_BOOLEAN,
                                      .value.boolean = 1,
                                  }},
                              },
                              {
                                  .values_num = 1,
                                  .key = "SizeUpload",
                                  .values = (oconfig_value_t[]){{
                                      .type = OCONFIG_TYPE_BOOLEAN,
                                      .value.boolean = 1,
                                  }},
                              },
                          },
                      .children_num = 2,
                  },
              .want_get_enabled_attr = NULL,
              .num_enabled_attr = (size_t *)2,
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .values_num = 1,
                                  .key = "NumConnects",
                                  .values = (oconfig_value_t[]){{
                                      .type = OCONFIG_TYPE_STRING,
                                      .value.string = "1",
                                  }},
                              },
                          },
                      .children_num = 1,
                  },
              .want_get_enabled_attr = NULL,
          },
          {.ci =
               {
                   .children =
                       (oconfig_item_t[]){
                           {
                               .values_num = 1,
                               .key = "ConnectTime",
                               .values = (oconfig_value_t[]){{
                                   .type = OCONFIG_TYPE_BOOLEAN,
                                   .value.boolean = 1,
                               }},
                           },
                           {
                               .values_num = 1,
                               .key = "NamelookupTime",
                               .values = (oconfig_value_t[]){{
                                   .type = OCONFIG_TYPE_BOOLEAN,
                                   .value.boolean = 1,
                               }},
                           },
                           {
                               .values_num = 1,
                               .key = "ContentLengthDownload",
                               .values = (oconfig_value_t[]){{
                                   .type = OCONFIG_TYPE_BOOLEAN,
                                   .value.boolean = 1,
                               }},
                           },
                       },
                   .children_num = 3,
               },

           .num_enabled_attr = (size_t *)2,
           .want_get_num_enabled_attr = 3,
           .want_get_enabled_attr = (char *[]){"ConnectTime", "NamelookupTime",
                                               "ContentLengthDownload"}},
          {.ci =
               {
                   .children =
                       (oconfig_item_t[]){
                           {
                               .values_num = 1,
                               .key = "TotalTime",
                               .values = (oconfig_value_t[]){{
                                   .type = OCONFIG_TYPE_BOOLEAN,
                                   .value.boolean = 1,
                               }},
                           },
                           {
                               .values_num = 1,
                               .key = "StarttransferTime",
                               .values = (oconfig_value_t[]){{
                                   .type = OCONFIG_TYPE_BOOLEAN,
                                   .value.boolean = 1,
                               }},
                           },
                           {
                               .values_num = 1,
                               .key = "SizeFactor",
                               .values = (oconfig_value_t[]){{
                                   .type = OCONFIG_TYPE_NUMBER,
                                   .value.number = 5,
                               }},
                           },
                           {
                               .values_num = 1,
                               .key = "SpeedBase",
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 10,
                                       }},
                           }},
                   .children_num = 4,
               },
           .num_enabled_attr = (size_t *)0,
           .want_get_num_enabled_attr = 2,
           .want_get_enabled_attr =
               (char *[]){
                   "TotalTime",
                   "StarttransferTime",
               }},
      };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    printf("## Case %zu: \n", i);
    curl_stats_t *s;

    s = curl_stats_from_config(&cases[i].ci);

    char **enabled_attr =
        curl_stats_get_enabled_attributes(s, cases[i].num_enabled_attr);

    if (cases[i].want_get_enabled_attr == NULL) {
      EXPECT_EQ_PTR(NULL, *cases[i].num_enabled_attr);
    } else {
      EXPECT_EQ_UINT64(cases[i].want_get_enabled_attr,
                       *cases[i].num_enabled_attr);

      if (*cases[i].num_enabled_attr > 0) {
        CHECK_NOT_NULL(enabled_attr);
      }

      for (size_t attr = 0; attr < *cases[i].num_enabled_attr; ++attr) {
        EXPECT_EQ_STR(cases[i].want_get_enabled_attr[attr], enabled_attr[attr]);
      }

      for (size_t attr = 0; attr < *cases[i].num_enabled_attr; ++attr) {
        free(enabled_attr[attr]);
      }

      free(enabled_attr);
    }
    curl_stats_destroy(s);
  }

  return 0;
}

int main() {
  RUN_TEST(curl_stats_from_config);
  RUN_TEST(curl_stats_get_metric_families_for_attributes);
  RUN_TEST(curl_stats_get_enabled_attributes);
  END_TEST;
}
