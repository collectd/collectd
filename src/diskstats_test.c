/**
 * collectd - src/diskstats_test.c
 *
 * Copyright(c) 2020 Intel Corporation. All rights reserved.
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

#define plugin_dispatch_values plugin_dispatch_values_diskstats_test

#include "diskstats.c" /* sic */
#include "testing.h"

/* mock functions */
static value_list_t last_vl;
static value_t last_value;
static char *test_stats = NULL;

int plugin_dispatch_values_diskstats_test(const value_list_t *vl) {
  last_value = vl->values[0];
  last_vl = *vl;
  last_vl.values = &last_value;
  return ENOTSUP;
}

FILE *fopen(__attribute__((unused)) const char *path,
            __attribute__((unused)) const char *mode) {
  static FILE f;
  return &f;
}

char *fgets(char *s, int size, __attribute__((unused)) FILE *stream) {
  char *ret = test_stats;
  test_stats = NULL;
  if (ret != NULL)
    sstrncpy(s, ret, size);
  return ret;
}

int fclose(__attribute__((unused)) FILE *stream) { return 0; }

int fileno(__attribute__((unused)) FILE *stream) { return 0; }

int lstat(__attribute__((unused)) const char *pathname, struct stat *buf) {
  buf->st_dev = 1;
  buf->st_ino = 1;
  buf->st_mode = S_IFREG;
  buf->st_nlink = 1;
  return 0;
}

int fstat(__attribute__((unused)) int fd, struct stat *buf) {
  buf->st_dev = 1;
  buf->st_ino = 1;
  buf->st_nlink = 1;
  return 0;
}
/* end mock functions */

DEF_TEST(plugin_config_fail) {
  oconfig_item_t test_cfg_parent = {"diskstats", NULL, 0, NULL, NULL, 0};
  char value_buff[256] = "sda1";
  char key_buff[256] = "Disks";
  oconfig_value_t test_cfg_value = {{value_buff}, OCONFIG_TYPE_STRING};
  oconfig_item_t test_cfg = {
      key_buff, &test_cfg_value, 1, &test_cfg_parent, NULL, 0};

  test_cfg_parent.children = &test_cfg;
  test_cfg_parent.children_num = 1;

  EXPECT_EQ_PTR(NULL, ignorelist);
  int ret = diskstats_config(&test_cfg_parent);
  OK(0 != ret);
  CHECK_NOT_NULL(ignorelist);

  memset(&test_cfg_value.value, 0, sizeof(test_cfg_value.value));
  sstrncpy(key_buff, "Disk", sizeof(key_buff));
  test_cfg_value.type = OCONFIG_TYPE_BOOLEAN;
  ret = diskstats_config(&test_cfg_parent);
  OK(0 != ret);

  memset(&test_cfg_value.value, 0, sizeof(test_cfg_value.value));
  sstrncpy(key_buff, "IgnoreSelected", sizeof(key_buff));
  sstrncpy(value_buff, "test", sizeof(value_buff));
  test_cfg_value.value.string = value_buff;
  test_cfg_value.type = OCONFIG_TYPE_STRING;
  ret = diskstats_config(&test_cfg_parent);
  OK(0 != ret);

  memset(&test_cfg_value.value, 0, sizeof(test_cfg_value.value));
  test_cfg_value.value.number = 3;
  test_cfg_value.type = OCONFIG_TYPE_STRING;
  sstrncpy(key_buff, "AvgQueueSize", sizeof(key_buff));

  ret = diskstats_config(&test_cfg_parent);
  OK(0 != ret);

  test_cfg_value.type = OCONFIG_TYPE_NUMBER;
  test_cfg_value.value.number = 0;
  ret = diskstats_config(&test_cfg_parent);
  OK(0 != ret);

  test_cfg_value.value.number = -1;
  ret = diskstats_config(&test_cfg_parent);
  OK(0 != ret);

  ret = diskstats_shutdown();
  EXPECT_EQ_INT(0, ret);

  return 0;
}

DEF_TEST(plugin_config) {
  oconfig_item_t test_cfg_parent = {"diskstats", NULL, 0, NULL, NULL, 0};
  char value_buff[256] = "sda1";
  char key_buff[256] = "Disk";
  oconfig_value_t test_cfg_value = {{value_buff}, OCONFIG_TYPE_STRING};
  oconfig_item_t test_cfg = {
      key_buff, &test_cfg_value, 1, &test_cfg_parent, NULL, 0};

  test_cfg_parent.children = &test_cfg;
  test_cfg_parent.children_num = 1;

  int ret = diskstats_config(&test_cfg_parent);
  EXPECT_EQ_INT(0, ret);
  CHECK_NOT_NULL(ignorelist);

  ret = ignorelist_match(ignorelist, "sda1");
  EXPECT_EQ_INT(0, ret);

  ret = ignorelist_match(ignorelist, "sda");
  OK(0 != ret);

  sstrncpy(value_buff, "/^sda/", sizeof(value_buff));
  ret = diskstats_config(&test_cfg_parent);
  EXPECT_EQ_INT(0, ret);

  ret = ignorelist_match(ignorelist, "sda");
  EXPECT_EQ_INT(0, ret);
  ret = ignorelist_match(ignorelist, "sda1");
  EXPECT_EQ_INT(0, ret);
  ret = ignorelist_match(ignorelist, "sda2");
  EXPECT_EQ_INT(0, ret);
  ret = ignorelist_match(ignorelist, "sdaABC");
  EXPECT_EQ_INT(0, ret);

  ret = ignorelist_match(ignorelist, "sdb");
  OK(0 != ret);

  memset(&test_cfg_value.value, 0, sizeof(test_cfg_value.value));
  test_cfg_value.value.boolean = 1;
  test_cfg_value.type = OCONFIG_TYPE_BOOLEAN;
  sstrncpy(key_buff, "IgnoreSelected", sizeof(key_buff));

  ret = diskstats_config(&test_cfg_parent);
  EXPECT_EQ_INT(0, ret);

  ret = ignorelist_match(ignorelist, "sda");
  OK(0 != ret);
  ret = ignorelist_match(ignorelist, "sda1");
  OK(0 != ret);
  ret = ignorelist_match(ignorelist, "sdb");
  EXPECT_EQ_INT(0, ret);

  test_cfg_value.value.boolean = 0;
  ret = diskstats_config(&test_cfg_parent);
  EXPECT_EQ_INT(0, ret);

  ret = ignorelist_match(ignorelist, "sda1");
  EXPECT_EQ_INT(0, ret);

  EXPECT_EQ_INT(DEFAULT_QUEUE_LEN, queue_avg_len);
  memset(&test_cfg_value.value, 0, sizeof(test_cfg_value.value));
  test_cfg_value.value.number = 3;
  test_cfg_value.type = OCONFIG_TYPE_NUMBER;
  sstrncpy(key_buff, "AvgQueueSize", sizeof(key_buff));

  ret = diskstats_config(&test_cfg_parent);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(3, queue_avg_len);

  return 0;
}

DEF_TEST(diskstat_submit) {
  diskstats_submit_gauge("abc_test", "test_type_g", 2.5);
  EXPECT_EQ_INT(1, last_vl.values_len);
  EXPECT_EQ_DOUBLE(2.5, last_vl.values[0].gauge);
  EXPECT_EQ_STR(DISKSTATS_PLUGIN, last_vl.plugin);
  EXPECT_EQ_STR("abc_test", last_vl.plugin_instance);
  EXPECT_EQ_STR("diskstat_gauge", last_vl.type);
  EXPECT_EQ_STR("test_type_g", last_vl.type_instance);

  diskstats_submit_counter("bcd_test", "test_type_c", 11);
  EXPECT_EQ_INT(1, last_vl.values_len);
  EXPECT_EQ_UINT64(11, last_vl.values[0].counter);
  EXPECT_EQ_STR(DISKSTATS_PLUGIN, last_vl.plugin);
  EXPECT_EQ_STR("bcd_test", last_vl.plugin_instance);
  EXPECT_EQ_STR("diskstat_counter", last_vl.type);
  EXPECT_EQ_STR("test_type_c", last_vl.type_instance);

  return 0;
}

DEF_TEST(diskstat_find_entry) {
  disklist_t *disk = diskstats_find_entry("abcd_test");
  EXPECT_EQ_PTR(NULL, disk);
  EXPECT_EQ_PTR(NULL, disklist);

  disklist_t d1 = {.name = "test1"};
  disklist_t d2 = {.name = "test2"};
  d1.next = &d2;
  disklist = &d1;

  disk = diskstats_find_entry("test1");
  EXPECT_EQ_PTR(&d1, disk);

  disk = diskstats_find_entry("test2");
  EXPECT_EQ_PTR(&d2, disk);

  disklist = NULL;

  return 0;
}

DEF_TEST(diskstat_avg_queue) {
  rolling_array_t q;
  queue_avg_len = 2;

  int ret = rolling_array_init(&q, queue_avg_len);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(0, q.idx);
  EXPECT_EQ_INT(2, q.len);
  EXPECT_EQ_INT(0, q.sum);
  CHECK_NOT_NULL(q.val_list);

  rolling_array_add(&q, 2);
  EXPECT_EQ_INT(1, q.idx);
  EXPECT_EQ_INT(2, q.val_list[0]);
  EXPECT_EQ_INT(2, q.sum);
  rolling_array_add(&q, 3);
  EXPECT_EQ_INT(0, q.idx);
  EXPECT_EQ_INT(3, q.val_list[1]);
  EXPECT_EQ_INT(5, q.sum);

  double res = rolling_array_avg(&q);
  EXPECT_EQ_DOUBLE(2.5, res);

  rolling_array_add(&q, 4);
  EXPECT_EQ_INT(1, q.idx);
  EXPECT_EQ_INT(4, q.val_list[0]);
  EXPECT_EQ_INT(7, q.sum);

  res = rolling_array_avg(&q);
  EXPECT_EQ_DOUBLE(3.5, res);

  rolling_array_t q2;
  ret = rolling_array_init(&q2, queue_avg_len);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(0, q2.idx);
  EXPECT_EQ_INT(2, q2.len);
  EXPECT_EQ_INT(0, q2.sum);
  CHECK_NOT_NULL(q2.val_list);

  res = rolling_arrays_ratio(&q, &q2);
  EXPECT_EQ_DOUBLE(0, res);

  rolling_array_add(&q2, 3);
  rolling_array_add(&q2, 1);
  EXPECT_EQ_INT(4, q2.sum);

  res = rolling_arrays_ratio(&q, &q2);
  EXPECT_EQ_DOUBLE(1.75, res);

  free((void *)q.val_list);
  free((void *)q2.val_list);
  return 0;
}

DEF_TEST(plugin_read_stats) {
  queue_avg_len = 3;
  await_avg_len = 3;
  disklist_t *d1 = diskstats_create_entry("sda", 512);

  disklist_t *d2 = calloc(1, sizeof(*d2));
  d2->name = strdup("no_disk");
  rolling_array_init(&d2->avg_queue, queue_avg_len);
  CHECK_NOT_NULL(d2->avg_queue.val_list);
  rolling_array_init(&d2->sum_time_ios, await_avg_len);
  CHECK_NOT_NULL(&d2->sum_time_ios.val_list);
  rolling_array_init(&d2->sum_nr_ios, await_avg_len);
  CHECK_NOT_NULL(&d2->sum_nr_ios.val_list);
  d2->sectors_to_mb = 0.000512;
  d2->prev = DS_NOT_SET;
  d2->next = d1;
  disklist = d2;

  test_stats = "   8       0 sda 5";
  d1->in_progress = 1;
  int ret = diskstats_read(NULL);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(0, d1->in_progress);
  EXPECT_EQ_INT(DS_NOT_SET, d1->prev);
  CHECK_NOT_NULL(disklist);
  EXPECT_EQ_PTR(d1, disklist);
  EXPECT_EQ_PTR(NULL, d1->next);

  test_stats = "   8       0 sda 55 44";
  d1->in_progress = 1;
  ret = diskstats_read(NULL);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(0, d1->in_progress);
  EXPECT_EQ_INT(DS_NOT_SET, d1->prev);

  test_stats = "   8       0 sda 467 23 14994 208 20 3 152 4 1 64 212";
  ret = diskstats_read(NULL);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(0, d1->prev);
  EXPECT_EQ_UINT64(467, d1->stats[0].reads_completed);
  EXPECT_EQ_UINT64(23, d1->stats[0].reads_merged);
  EXPECT_EQ_UINT64(14994, d1->stats[0].sectors_read);
  EXPECT_EQ_UINT64(208, d1->stats[0].ms_spent_reading);
  EXPECT_EQ_UINT64(20, d1->stats[0].writes_completed);
  EXPECT_EQ_UINT64(3, d1->stats[0].writes_merged);
  EXPECT_EQ_UINT64(152, d1->stats[0].sectors_written);
  EXPECT_EQ_UINT64(4, d1->stats[0].ms_spent_writing);
  EXPECT_EQ_UINT64(1, d1->stats[0].ios_in_progress);
  EXPECT_EQ_UINT64(64, d1->stats[0].ms_spent_ios);
  EXPECT_EQ_UINT64(212, d1->stats[0].weighted_ms_spent_ios);
  EXPECT_EQ_UINT64(1, d1->avg_queue.val_list[0]);

  test_stats = "   8       0 sda 767 35 24889 508 30 8 252 5 3 74 312";
  ret = diskstats_read(NULL);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(1, d1->prev);
  EXPECT_EQ_UINT64(467, d1->stats[0].reads_completed);
  EXPECT_EQ_UINT64(23, d1->stats[0].reads_merged);
  EXPECT_EQ_UINT64(14994, d1->stats[0].sectors_read);
  EXPECT_EQ_UINT64(208, d1->stats[0].ms_spent_reading);
  EXPECT_EQ_UINT64(20, d1->stats[0].writes_completed);
  EXPECT_EQ_UINT64(3, d1->stats[0].writes_merged);
  EXPECT_EQ_UINT64(152, d1->stats[0].sectors_written);
  EXPECT_EQ_UINT64(4, d1->stats[0].ms_spent_writing);
  EXPECT_EQ_UINT64(1, d1->stats[0].ios_in_progress);
  EXPECT_EQ_UINT64(64, d1->stats[0].ms_spent_ios);
  EXPECT_EQ_UINT64(212, d1->stats[0].weighted_ms_spent_ios);
  EXPECT_EQ_UINT64(767, d1->stats[1].reads_completed);
  EXPECT_EQ_UINT64(35, d1->stats[1].reads_merged);
  EXPECT_EQ_UINT64(24889, d1->stats[1].sectors_read);
  EXPECT_EQ_UINT64(508, d1->stats[1].ms_spent_reading);
  EXPECT_EQ_UINT64(30, d1->stats[1].writes_completed);
  EXPECT_EQ_UINT64(8, d1->stats[1].writes_merged);
  EXPECT_EQ_UINT64(252, d1->stats[1].sectors_written);
  EXPECT_EQ_UINT64(5, d1->stats[1].ms_spent_writing);
  EXPECT_EQ_UINT64(3, d1->stats[1].ios_in_progress);
  EXPECT_EQ_UINT64(74, d1->stats[1].ms_spent_ios);
  EXPECT_EQ_UINT64(312, d1->stats[1].weighted_ms_spent_ios);
  EXPECT_EQ_UINT64(1, d1->avg_queue.val_list[0]);
  EXPECT_EQ_UINT64(3, d1->avg_queue.val_list[1]);

  test_stats = "   8       0 sda t3st 35 24889 508 30 8 252 5 3 74 312";
  ret = diskstats_read(NULL);
  EXPECT_EQ_INT(-1, ret);

  return 0;
}

DEF_TEST(plugin_shutdown) {
  disklist_t *d = diskstats_create_entry("test_disk", 256);
  EXPECT_EQ_PTR(d, disklist);

  int ret = diskstats_shutdown();
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_PTR(NULL, ignorelist);
  EXPECT_EQ_PTR(NULL, disklist);

  return 0;
}

int main(void) {
  RUN_TEST(plugin_config_fail);
  RUN_TEST(plugin_config);
  RUN_TEST(diskstat_submit);
  RUN_TEST(diskstat_find_entry);
  RUN_TEST(diskstat_avg_queue);
  RUN_TEST(plugin_read_stats);
  RUN_TEST(plugin_shutdown);

  END_TEST;
}
