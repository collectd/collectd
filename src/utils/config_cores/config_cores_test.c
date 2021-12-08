/**
 * collectd - src/utils_config_cores_test.c
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Kamil Wiatrowski <kamilx.wiatrowski@intel.com>
 **/

#include "collectd.h"

#include "testing.h"
#include "utils/config_cores/config_cores.c" /* sic */

oconfig_value_t test_cfg_values[] = {{{"0"}, OCONFIG_TYPE_STRING},
                                     {{"1-2"}, OCONFIG_TYPE_STRING},
                                     {{"[3-4]"}, OCONFIG_TYPE_STRING}};

oconfig_item_t test_cfg = {
    "Cores", test_cfg_values, STATIC_ARRAY_SIZE(test_cfg_values), NULL, NULL,
    0};

static int compare_with_test_config(core_groups_list_t *cgl) {
  if (cgl->num_cgroups == 4 && cgl->cgroups[0].num_cores == 1 &&
      strcmp("0", cgl->cgroups[0].desc) == 0 && cgl->cgroups[0].cores[0] == 0 &&
      cgl->cgroups[1].num_cores == 2 &&
      strcmp("1-2", cgl->cgroups[1].desc) == 0 &&
      cgl->cgroups[1].cores[0] == 1 && cgl->cgroups[1].cores[1] == 2 &&
      cgl->cgroups[2].num_cores == 1 &&
      strcmp("3", cgl->cgroups[2].desc) == 0 && cgl->cgroups[2].cores[0] == 3 &&
      cgl->cgroups[3].num_cores == 1 &&
      strcmp("4", cgl->cgroups[3].desc) == 0 && cgl->cgroups[3].cores[0] == 4)
    return 0;

  return -1;
}

DEF_TEST(string_to_uint) {
  int ret = 0;
  char *s = "13", *s1 = "0xd", *s2 = "g";
  unsigned n = 0;

  ret = str_to_uint(s, &n);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(13, n);

  ret = str_to_uint(s1, &n);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(13, n);

  ret = str_to_uint(s2, &n);
  OK(ret < 0);

  ret = str_to_uint(NULL, &n);
  OK(ret < 0);
  return 0;
}

DEF_TEST(cores_list_to_numbers) {
  size_t n = 0;
  unsigned nums[MAX_CORES];
  char str[64] = "";

  n = str_list_to_nums(str, nums, STATIC_ARRAY_SIZE(nums));
  EXPECT_EQ_INT(0, n);

  strncpy(str, "1", STATIC_ARRAY_SIZE(str));
  n = str_list_to_nums(str, nums, STATIC_ARRAY_SIZE(nums));
  EXPECT_EQ_INT(1, n);
  EXPECT_EQ_INT(1, nums[0]);

  strncpy(str, "0,2-3", STATIC_ARRAY_SIZE(str));
  n = str_list_to_nums(str, nums, STATIC_ARRAY_SIZE(nums));
  EXPECT_EQ_INT(3, n);
  EXPECT_EQ_INT(0, nums[0]);
  EXPECT_EQ_INT(2, nums[1]);
  EXPECT_EQ_INT(3, nums[2]);

  strncpy(str, "11-0xa", STATIC_ARRAY_SIZE(str));
  n = str_list_to_nums(str, nums, STATIC_ARRAY_SIZE(nums));
  EXPECT_EQ_INT(2, n);
  EXPECT_EQ_INT(10, nums[0]);
  EXPECT_EQ_INT(11, nums[1]);

  snprintf(str, sizeof(str), "0-%d", (MAX_CORES - 1));
  n = str_list_to_nums(str, nums, STATIC_ARRAY_SIZE(nums));
  EXPECT_EQ_INT(MAX_CORES, n);
  EXPECT_EQ_INT(0, nums[0]);
  EXPECT_EQ_INT(MAX_CORES - 1, nums[MAX_CORES - 1]);

  /* Should return 0 for incorrect syntax. */
  strncpy(str, "5g", STATIC_ARRAY_SIZE(str));
  n = str_list_to_nums(str, nums, STATIC_ARRAY_SIZE(nums));
  EXPECT_EQ_INT(0, n);
  return 0;
}

DEF_TEST(check_grouped_cores) {
  int ret = 0;
  _Bool grouped;
  char src[64] = "[5-15]";
  char dest[64];

  ret = check_core_grouping(dest, src, sizeof(dest), &grouped);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(0, grouped);
  EXPECT_EQ_STR("5-15", dest);

  strncpy(src, "  5-15", STATIC_ARRAY_SIZE(src));
  ret = check_core_grouping(dest, src, sizeof(dest), &grouped);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(1, grouped);
  EXPECT_EQ_STR("5-15", dest);
  return 0;
}

DEF_TEST(cores_option_parse) {
  int ret = 0;
  core_groups_list_t cgl = {0};

  ret = config_cores_parse(&test_cfg, &cgl);
  EXPECT_EQ_INT(0, ret);
  CHECK_NOT_NULL(cgl.cgroups);
  EXPECT_EQ_INT(0, compare_with_test_config(&cgl));

  config_cores_cleanup(&cgl);
  return 0;
}

DEF_TEST(cores_option_parse_fail) {
  int ret = 0;
  core_groups_list_t cgl = {0};
  /* Wrong value, missing closing bracket ] */
  oconfig_value_t values = {{"[0-15"}, OCONFIG_TYPE_STRING};
  oconfig_item_t cfg = {"Cores", &values, 1, NULL, NULL, 0};

  ret = config_cores_parse(&cfg, &cgl);
  EXPECT_EQ_INT(-EINVAL, ret);
  EXPECT_EQ_INT(0, cgl.num_cgroups);
  OK(NULL == cgl.cgroups);
  return 0;
}

DEF_TEST(cores_default_list) {
  int ret = 0;
  core_groups_list_t cgl = {0};

  ret = config_cores_default(2, &cgl);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(2, cgl.num_cgroups);
  CHECK_NOT_NULL(cgl.cgroups);

  CHECK_NOT_NULL(cgl.cgroups[0].cores);
  CHECK_NOT_NULL(cgl.cgroups[0].desc);
  EXPECT_EQ_STR("0", cgl.cgroups[0].desc);
  EXPECT_EQ_INT(1, cgl.cgroups[0].num_cores);
  EXPECT_EQ_INT(0, cgl.cgroups[0].cores[0]);

  CHECK_NOT_NULL(cgl.cgroups[1].cores);
  CHECK_NOT_NULL(cgl.cgroups[1].desc);
  EXPECT_EQ_STR("1", cgl.cgroups[1].desc);
  EXPECT_EQ_INT(1, cgl.cgroups[1].num_cores);
  EXPECT_EQ_INT(1, cgl.cgroups[1].cores[0]);

  config_cores_cleanup(&cgl);
  return 0;
}

DEF_TEST(cores_default_list_fail) {
  int ret = 0;
  core_groups_list_t cgl = {0};

  ret = config_cores_default(-1, &cgl);
  OK(ret < 0);
  ret = config_cores_default(MAX_CORES + 1, &cgl);
  OK(ret < 0);
  ret = config_cores_default(1, NULL);
  OK(ret < 0);
  return 0;
}

DEF_TEST(cores_group_cleanup) {
  core_groups_list_t cgl;
  cgl.cgroups = calloc(1, sizeof(*cgl.cgroups));
  CHECK_NOT_NULL(cgl.cgroups);
  cgl.num_cgroups = 1;
  cgl.cgroups[0].desc = strdup("1");
  cgl.cgroups[0].cores = calloc(1, sizeof(*cgl.cgroups[0].cores));
  CHECK_NOT_NULL(cgl.cgroups[0].cores);
  cgl.cgroups[0].cores[0] = 1;
  cgl.cgroups[0].num_cores = 1;

  config_cores_cleanup(&cgl);
  OK(NULL == cgl.cgroups);
  EXPECT_EQ_INT(0, cgl.num_cgroups);
  return 0;
}

DEF_TEST(cores_group_cmp) {
  unsigned cores_mock[] = {0, 1, 2};
  core_group_t group_mock = {"0,1,2", cores_mock, 3};
  unsigned cores_mock_2[] = {2, 3};
  core_group_t group_mock_2 = {"2,3", cores_mock_2, 2};

  int ret = config_cores_cmp_cgroups(&group_mock, &group_mock);
  EXPECT_EQ_INT(1, ret);

  ret = config_cores_cmp_cgroups(&group_mock, &group_mock_2);
  EXPECT_EQ_INT(-1, ret);

  cores_mock_2[0] = 4;
  ret = config_cores_cmp_cgroups(&group_mock, &group_mock_2);
  EXPECT_EQ_INT(0, ret);
  return 0;
}

int main(void) {
  RUN_TEST(string_to_uint);
  RUN_TEST(cores_list_to_numbers);
  RUN_TEST(check_grouped_cores);

  RUN_TEST(cores_group_cleanup);
  RUN_TEST(cores_option_parse);
  RUN_TEST(cores_option_parse_fail);
  RUN_TEST(cores_default_list);
  RUN_TEST(cores_default_list_fail);

  RUN_TEST(cores_group_cmp);

  END_TEST;
}
