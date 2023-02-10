/**
 * collectd - src/ethstat_test.c
 * MIT License
 *
 * Copyright (C) 2021  Intel Corporation. All rights reserved.
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
 *   Bartlomiej Kotlowski <bartlomiej.kotlowski@intel.com>
 **/
#define plugin_dispatch_metric_family plugin_dispatch_metric_family_ethstat_test

#include "ethstat.c"
#include "testing.h"

#define NPROCS 2
#define MAX_SAVE_DATA_SUBMIT 4

// mock

uint64_t matric_in_ethtool = 0;
int ioctl(int __fd, unsigned long int __request, ...) {
  va_list valist;
  va_start(valist, __request);
  struct ifreq *ifr = va_arg(valist, struct ifreq *);
  va_end(valist);

  uint32_t ethcmd = (uint32_t)*ifr->ifr_data;
  void *addr = (void *)ifr->ifr_data;

  // prepare struct to resposne
  struct ethtool_drvinfo *drvinfo =
      (struct ethtool_drvinfo *)addr; // ETHTOOL_GDRVINFO
  struct ethtool_gstrings *strings =
      (struct ethtool_gstrings *)addr; // ETHTOOL_GSTRINGS
  struct ethtool_stats *stats = (struct ethtool_stats *)addr; // ETHTOOL_GSTATS

  switch (ethcmd) {
  case ETHTOOL_GDRVINFO: {
    drvinfo->n_stats = 1;
    return 1; // OK
  }

  case ETHTOOL_GSTRINGS: {
    memcpy(strings->data, "rx_bytes", 9);
    return 2; // OK
  }

  case ETHTOOL_GSTATS: {
    memcpy(stats->data, &matric_in_ethtool, 8);
    return 3; // OK
  }

  default:
    return -1; // no mock feature
  }
  return -2; // something wrong
};

FILE *fopen(__attribute__((unused)) const char *path,
            __attribute__((unused)) const char *mode) {
  static FILE f;
  return &f;
}
int fclose(FILE *__stream) { return 0; }

// this varable can be changed between tests. The value given here will pretend
// to be the value read from the file
uint64_t matric_in_file = 0;

int fscanf(FILE *stream, const char *format, ...) {
  va_list arg;
  va_start(arg, format);
  uint64_t *pointer;
  pointer = va_arg(arg, uint64_t *);
  *pointer = matric_in_file;
  va_end(arg);
  return 1;
}

int data_submit_iterator = MAX_SAVE_DATA_SUBMIT - 1;
counter_t data_submit[MAX_SAVE_DATA_SUBMIT];

int plugin_dispatch_metric_family_ethstat_test(metric_family_t const *fam) {

  data_submit_iterator++;
  if (data_submit_iterator >= MAX_SAVE_DATA_SUBMIT - 1) {
    data_submit_iterator = 0;
  }

  data_submit[data_submit_iterator] = fam->metric.ptr->value.counter;

  return 0;
}
// END mock

DEF_TEST(getNewNode) {
  node_t *n_ret;
  n_ret = getNewNode(1);
  CHECK_NOT_NULL(n_ret);
  EXPECT_EQ_UINT64(n_ret->val, 1);
  OK1(n_ret->next == NULL, "expect NULL");
  sfree(n_ret);

  n_ret = getNewNode(0);
  CHECK_NOT_NULL(n_ret);
  EXPECT_EQ_UINT64(n_ret->val, 0);
  OK1(n_ret->next == NULL, "expect NULL");
  sfree(n_ret);
  return 0;
}

DEF_TEST(check_name) {
  bool b_ret;
  b_ret = check_name("rx_bytes", 8); // size ok
  OK1(b_ret == VALID_NAME, "expect true (VALID_NAME)");

  b_ret = check_name("rx_bytes", 9); // size too big
  OK1(b_ret == VALID_NAME, "expect true (VALID_NAME)");

  b_ret = check_name("rx_bytes", 7); // size too small
  OK1(b_ret == INVALID_NAME, "expect false (INVALID_NAME)");

  b_ret = check_name("../foo/rx_bytes", 15);
  OK1(b_ret == INVALID_NAME, "expect false (INVALID_NAME)");

  b_ret = check_name(NULL, 11);
  OK1(b_ret == INVALID_NAME, "expect false (INVALID_NAME)");
  return 0;
}

DEF_TEST(add_sysfs_metric_to_readable) {
  int i_ret;
  interface_metrics_t *interface_group;

  interface_group =
      (interface_metrics_t *)calloc(1, sizeof(interface_metrics_t));

  i_ret = add_sysfs_metric_to_readable(interface_group, "rx_bytes_123");

  EXPECT_EQ_INT(i_ret, 0);
  EXPECT_EQ_STR(interface_group->sysfs_metrics[0], "rx_bytes_123");
  EXPECT_EQ_INT(interface_group->sysfs_metrics_num, 1);
  EXPECT_EQ_INT(interface_group->sysfs_metrics_size, 2);

  i_ret = add_sysfs_metric_to_readable(interface_group, NULL);
  EXPECT_EQ_INT(i_ret, -1);
  EXPECT_EQ_STR(interface_group->sysfs_metrics[0], "rx_bytes_123");
  EXPECT_EQ_INT(interface_group->sysfs_metrics_num, 1);
  EXPECT_EQ_INT(interface_group->sysfs_metrics_size, 2);

  i_ret = add_sysfs_metric_to_readable(interface_group, "");
  EXPECT_EQ_INT(-1, i_ret);
  EXPECT_EQ_STR(interface_group->sysfs_metrics[0], "rx_bytes_123");
  EXPECT_EQ_INT(interface_group->sysfs_metrics_num, 1);
  EXPECT_EQ_INT(interface_group->sysfs_metrics_size, 2);

  i_ret =
      add_sysfs_metric_to_readable(interface_group, "../statistic/rx_bytes");
  EXPECT_EQ_INT(-1, i_ret);
  EXPECT_EQ_STR(interface_group->sysfs_metrics[0], "rx_bytes_123");
  EXPECT_EQ_INT(interface_group->sysfs_metrics_num, 1);
  EXPECT_EQ_INT(interface_group->sysfs_metrics_size, 2);

  sfree(interface_group->sysfs_metrics[0]);
  sfree(interface_group->sysfs_metrics);
  sfree(interface_group);
  return 0;
}

DEF_TEST(add_readable_sysfs_metrics_to_ethtool_ignore_list) {
  interface_metrics_t *interface_group;
  int i_ret;
  interface_group =
      (interface_metrics_t *)calloc(1, sizeof(interface_metrics_t));

  i_ret = add_sysfs_metric_to_readable(interface_group, "rx_bytes_123");
  EXPECT_EQ_INT(i_ret, 0);
  EXPECT_EQ_INT(
      0, ignorelist_match(interface_group->ignorelist_ethtool, "rx_bytes_123"));
  add_readable_sysfs_metrics_to_ethtool_ignore_list(interface_group);
  EXPECT_EQ_INT(
      1, ignorelist_match(interface_group->ignorelist_ethtool, "rx_bytes_123"));
  EXPECT_EQ_INT(
      0, ignorelist_match(interface_group->ignorelist_ethtool, "*x_bytes_123"));
  EXPECT_EQ_INT(0,
                ignorelist_match(interface_group->ignorelist_ethtool, "[.]*"));

  sfree(interface_group->sysfs_metrics[0]);
  sfree(interface_group->sysfs_metrics);
  ignorelist_free(interface_group->ignorelist_ethtool);
  sfree(interface_group);
  return 0;
}

DEF_TEST(create_new_interfaces_group) {
  oconfig_item_t *conf;
  conf = (oconfig_item_t *)calloc(1, sizeof(oconfig_item_t));
  conf->values_num = 1;
  conf->values = (oconfig_value_t *)calloc(1, sizeof(oconfig_value_t));
  conf->values[0].value.string = strdup("eth");
  interface_metrics_t *interface_group;
  interface_group =
      (interface_metrics_t *)calloc(2, sizeof(interface_metrics_t));
  create_new_interfaces_group(conf, interface_group);

  EXPECT_EQ_INT(conf->values_num, interface_group->interfaces_num);
  OK1(interface_group->use_sys_class_net == false, "");
  CHECK_NOT_NULL(interface_group->ignorelist_ethtool);
  CHECK_NOT_NULL(interface_group->ignorelist_sysfs);
  EXPECT_EQ_INT(24, interface_group->sysfs_metrics_size);
  CHECK_NOT_NULL(interface_group->sysfs_metrics);
  CHECK_NOT_NULL(interface_group->ethtool_metrics);

  CHECK_NOT_NULL(interface_group->interfaces);
  for (int i = 0; i < conf->values_num; i++) {
    EXPECT_EQ_STR(conf->values[i].value.string, interface_group->interfaces[i]);
  }
  EXPECT_EQ_INT(1, interfaces_group_num);
  EXPECT_EQ_INT(
      0, ignorelist_match(interface_group->ignorelist_ethtool, "rx_bytes"));
  EXPECT_EQ_INT(
      0, ignorelist_match(interface_group->ignorelist_ethtool, "*x_bytes"));
  EXPECT_EQ_INT(0,
                ignorelist_match(interface_group->ignorelist_ethtool, "[.]*"));

  sfree(interface_group->ignorelist_ethtool);
  sfree(interface_group->ignorelist_sysfs);
  sfree(interface_group->interfaces[0]);
  sfree(interface_group->interfaces);
  sfree(interface_group->sysfs_metrics);
  sfree(interface_group->ethtool_metrics);
  sfree(interface_group);

  sfree(conf->values[0].value.string);
  sfree(conf->values);
  sfree(conf);
  return 0;
}

DEF_TEST(ethstat_add_map) {
  value_map_t *map = NULL;
  oconfig_item_t *conf;
  conf = (oconfig_item_t *)calloc(1, sizeof(oconfig_item_t));
  conf->values_num = 2;
  conf->values = (oconfig_value_t *)calloc(2, sizeof(oconfig_value_t));
  conf->values[0].value.string = strdup("rx_bytes");
  conf->values[1].value.string = strdup("RX-bytes");
  ethstat_add_map(conf);
  CHECK_NOT_NULL(value_map);
  c_avl_get(value_map, "rx_bytes", (void *)&map);
  EXPECT_EQ_STR("RX-bytes", map->type);
  EXPECT_EQ_STR("", map->type_instance);
  sfree(conf->values[0].value.string);
  sfree(conf->values[1].value.string);
  sfree(conf->values);
  sfree(conf);

  conf = (oconfig_item_t *)calloc(1, sizeof(oconfig_item_t));
  conf->values_num = 3;
  conf->values = (oconfig_value_t *)calloc(3, sizeof(oconfig_value_t));
  conf->values[0].value.string = strdup("tx_bytes");
  conf->values[1].value.string = strdup("TX-bytes");
  conf->values[2].value.string = strdup("foo");
  ethstat_add_map(conf);
  CHECK_NOT_NULL(value_map);
  c_avl_get(value_map, "tx_bytes", (void *)&map);
  EXPECT_EQ_STR("TX-bytes", map->type);
  EXPECT_EQ_STR("foo", map->type_instance);
  sfree(conf->values[0].value.string);
  sfree(conf->values[1].value.string);
  sfree(conf->values[2].value.string);
  sfree(conf->values);
  sfree(conf);
  void *key = NULL;
  void *value = NULL;
  while (c_avl_pick(value_map, &key, &value) == 0) {
    sfree(key);
    sfree(value);
  }
  c_avl_destroy(value_map);
  value_map = NULL;
  return 0;
}

DEF_TEST(read_sysfs_metrics) {

  int i_ret = 0;
  interface_metrics_t *interface_group;

  interface_group =
      (interface_metrics_t *)calloc(1, sizeof(interface_metrics_t));
  oconfig_item_t *conf;
  conf = (oconfig_item_t *)calloc(1, sizeof(oconfig_item_t));
  conf->values_num = 1;
  conf->values = (oconfig_value_t *)calloc(1, sizeof(oconfig_value_t));
  conf->values[0].value.string = strdup("eno112");
  create_new_interfaces_group(conf, interface_group);
  interface_group->sysfs_metrics_num = 1;
  interface_group->sysfs_metrics[0] = strdup("rx_bytes");

  matric_in_file = 0;
  i_ret = read_sysfs_metrics(interface_group->interfaces[0],
                             interface_group->sysfs_metrics,
                             interface_group->sysfs_metrics_num);
  EXPECT_EQ_INT(0, i_ret);
  EXPECT_EQ_INT(0, data_submit[data_submit_iterator]);

  matric_in_file = 999;
  i_ret = read_sysfs_metrics(interface_group->interfaces[0],
                             interface_group->sysfs_metrics,
                             interface_group->sysfs_metrics_num);
  EXPECT_EQ_INT(0, i_ret);
  EXPECT_EQ_INT(999, data_submit[data_submit_iterator]);

  matric_in_file = 0xFFFFFFFFFFFFFFFF;
  i_ret = read_sysfs_metrics(interface_group->interfaces[0],
                             interface_group->sysfs_metrics,
                             interface_group->sysfs_metrics_num);
  EXPECT_EQ_INT(0, i_ret);
  EXPECT_EQ_UINT64(0xFFFFFFFFFFFFFFFF, data_submit[data_submit_iterator]);

  matric_in_file = 0xFFFFFFFFFFFFFFFF;
  matric_in_file++;
  i_ret = read_sysfs_metrics(interface_group->interfaces[0],
                             interface_group->sysfs_metrics,
                             interface_group->sysfs_metrics_num);
  EXPECT_EQ_INT(0, i_ret);
  EXPECT_EQ_UINT64(0, data_submit[data_submit_iterator]);

  sfree(interface_group->sysfs_metrics[0]);
  sfree(interface_group->sysfs_metrics);

  sfree(conf->values[0].value.string);
  sfree(conf->values);
  sfree(conf);
  sfree(interface_group->ignorelist_ethtool);
  sfree(interface_group->ethtool_metrics);
  sfree(interface_group->ignorelist_sysfs);
  sfree(interface_group->interfaces[0]);
  sfree(interface_group->interfaces);
  sfree(interface_group);

  return 0;
}

DEF_TEST(complete_list_of_metrics_read_by_ethtool) {
  int i_ret;

  interface_metrics_t *interface_group;

  interface_group =
      (interface_metrics_t *)calloc(1, sizeof(interface_metrics_t));
  oconfig_item_t *conf;
  conf = (oconfig_item_t *)calloc(1, sizeof(oconfig_item_t));
  conf->values_num = 1;
  conf->values = (oconfig_value_t *)calloc(1, sizeof(oconfig_value_t));
  conf->values[0].value.string = strdup("eno112");
  create_new_interfaces_group(conf, interface_group);

  i_ret = complete_list_of_metrics_read_by_ethtool(
      interface_group->interfaces[0], interface_group->ignorelist_ethtool,
      &interface_group->ethtool_metrics[0]);
  EXPECT_EQ_INT(0, i_ret);

  ignorelist_free(interface_group->ignorelist_ethtool);
  node_t *current = interface_group->ethtool_metrics[0];
  while (current != NULL) {
    node_t *to_remove = current;
    current = current->next;
    sfree(to_remove);
  }
  sfree(interface_group->ethtool_metrics);

  sfree(interface_group->sysfs_metrics[0]);
  sfree(interface_group->sysfs_metrics);

  sfree(conf->values[0].value.string);
  sfree(conf->values);
  sfree(conf);
  sfree(interface_group->ignorelist_sysfs);
  sfree(interface_group->interfaces[0]);
  sfree(interface_group->interfaces);
  sfree(interface_group);

  return 0;
}

DEF_TEST(ethstat_read_interface) {
  int i_ret;

  interface_metrics_t *interface_group;

  interface_group =
      (interface_metrics_t *)calloc(1, sizeof(interface_metrics_t));
  oconfig_item_t *conf;
  conf = (oconfig_item_t *)calloc(1, sizeof(oconfig_item_t));
  conf->values_num = 1;
  conf->values = (oconfig_value_t *)calloc(1, sizeof(oconfig_value_t));
  conf->values[0].value.string = strdup("eno112");
  create_new_interfaces_group(conf, interface_group);

  i_ret = complete_list_of_metrics_read_by_ethtool(
      interface_group->interfaces[0], interface_group->ignorelist_ethtool,
      &interface_group->ethtool_metrics[0]);
  EXPECT_EQ_INT(0, i_ret);
  matric_in_ethtool = 1234;
  i_ret = ethstat_read_interface(interface_group->interfaces[0],
                                 interface_group->ethtool_metrics[0]);
  EXPECT_EQ_INT(0, i_ret);
  EXPECT_EQ_UINT64(1234, data_submit[data_submit_iterator]);

  ignorelist_free(interface_group->ignorelist_ethtool);
  node_t *current = interface_group->ethtool_metrics[0];
  while (current != NULL) {
    node_t *to_remove = current;
    current = current->next;
    sfree(to_remove);
  }
  sfree(interface_group->ethtool_metrics);

  sfree(interface_group->sysfs_metrics[0]);
  sfree(interface_group->sysfs_metrics);

  sfree(conf->values[0].value.string);
  sfree(conf->values);
  sfree(conf);
  sfree(interface_group->ignorelist_sysfs);
  sfree(interface_group->interfaces[0]);
  sfree(interface_group->interfaces);
  sfree(interface_group);

  return 0;
}

int main(void) {
  RUN_TEST(getNewNode);
  RUN_TEST(check_name);
  RUN_TEST(add_readable_sysfs_metrics_to_ethtool_ignore_list);
  RUN_TEST(create_new_interfaces_group);
  RUN_TEST(ethstat_add_map);
  RUN_TEST(add_sysfs_metric_to_readable);
  RUN_TEST(read_sysfs_metrics);
  RUN_TEST(complete_list_of_metrics_read_by_ethtool);
  RUN_TEST(ethstat_read_interface);
  END_TEST;
}
