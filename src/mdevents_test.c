/**
 * collectd - src/intel_md_events_test.c
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
 *   Krzysztof Kazimierczak <krzysztof.kazimierczak@intel.com>
 *   Maciej Fijalkowski <maciej.fijalkowski@intel.com>
 **/

#include "mdevents.c"
#include "testing.h"

DEF_TEST(classify_event) {
  int ret;
  ret = md_events_classify_event("Fail");
  EXPECT_EQ_INT(NOTIF_FAILURE, ret);

  ret = md_events_classify_event("SparesMissing");
  EXPECT_EQ_INT(NOTIF_WARNING, ret);

  ret = md_events_classify_event("NewArray");
  EXPECT_EQ_INT(NOTIF_OKAY, ret);

  ret = md_events_classify_event("UnsupportedEvent");
  EXPECT_EQ_INT(0, ret);

  return 0;
}

DEF_TEST(compile_regex) {
  int ret;
  regex_t r;

  ret = md_events_compile_regex(&r, "^(test|example).+regex[0-9]$");
  EXPECT_EQ_INT(ret, 0);

  // Compiling invalid regex causes memory leak:
  // https://bugzilla.redhat.com/show_bug.cgi?id=1049743
  // ret = md_events_compile_regex(&r, "^$^(oooo|MmaA nnn,d[[[14-1]");
  // EXPECT_EQ_INT(ret, -1);

  regfree(&r);
  return 0;
}

DEF_TEST(config) {
  int ret;
  ret = md_events_config("Event", "DeviceDisappeared");
  EXPECT_EQ_INT(ret, 0);
  ret = md_events_config("Event", "WrongEvent");
  EXPECT_EQ_INT(ret, -1);
  ret = md_events_config("Array", "/dev/md0");
  EXPECT_EQ_INT(ret, 0);
  ret = md_events_config("Array", "WrongArrayName");
  EXPECT_EQ_INT(ret, -1);

  ret = md_events_config("IgnoreArray", "True");
  EXPECT_EQ_INT(ret, 0);
  ret = md_events_config("IgnoreArray", "False");
  EXPECT_EQ_INT(ret, 0);
  ret = md_events_config("IgnoreArray", "Talse");
  EXPECT_EQ_INT(ret, -1);

  ret = md_events_config("IgnoreEvent", "True");
  EXPECT_EQ_INT(ret, 0);
  ret = md_events_config("IgnoreEvent", "False");
  EXPECT_EQ_INT(ret, 0);
  ret = md_events_config("IgnoreEvent", "Frue");
  EXPECT_EQ_INT(ret, -1);

  return 0;
}

DEF_TEST(copy_match) {
  char *test_str = "This is a test string to be used as an input for "
                   "md_events_copy_match";
  regmatch_t match = {.rm_so = 10, .rm_eo = 20};
  md_events_event_t event = {};

  md_events_copy_match(event.event_name, test_str, match);
  EXPECT_EQ_STR(event.event_name, "test strin");

  // check the out of bounds

  return 0;
}

DEF_TEST(dispatch_notification) {
  int ret;
  char buf[130];

  memset(buf, 'a', 129);
  buf[129] = '\0';

  ret = md_events_dispatch_notification(NULL, NULL);
  EXPECT_EQ_INT(ret, -1);

  md_events_event_t e = {.event_name = "Fail",
                         .md_device = "/dev/md1",
                         .component_device = "/dev/sda1"};

  ret = md_events_dispatch_notification(&e, NULL);
  EXPECT_EQ_INT(ret, -1);

  notification_t n = {.severity = NOTIF_FAILURE,
                      .time = cdtime(),
                      .plugin = MD_EVENTS_PLUGIN,
                      .type_instance = ""};

  ret = md_events_dispatch_notification(NULL, &n);
  EXPECT_EQ_INT(ret, -1);

  ret = md_events_dispatch_notification(&e, &n);
  EXPECT_EQ_INT(ret, 0);

  return 0;
}

DEF_TEST(get_max_len) {
  size_t max_name_len = 4;
  regmatch_t match = {.rm_eo = 2, .rm_so = 1};
  int ret;

  ret = md_events_get_max_len(match, max_name_len);
  EXPECT_EQ_INT(ret, 1);

  match.rm_eo *= 10;
  ret = md_events_get_max_len(match, max_name_len);
  EXPECT_EQ_INT(ret, max_name_len - 1);

  return 0;
}

DEF_TEST(match_regex) {
  regex_t r;
  int ret;
  static char regex_pattern[] =
      "mdadm[\\[0-9]+\\]: ([a-zA-Z]+) event detected on md"
      " device ([a-z0-9\\/\\.\\-]+)[^\\/\n]*([a-z0-9\\/\\.\\-]+)?";
  const char matching_string[] =
      "Jan 17 05:24:27 ubuntu-sadev02 mdadm[1848]: "
      "DeviceDisappeared event detected on md device /dev/md0";
  const char unmatching_string[] =
      "mdm[4016] RebuildStarted event detected on md"
      " device /dev/md127, component device $/dev/sdb";
  const char unclass_event[] =
      "Jan 17 05:24:27 ubuntu-sadev02 mdadm[1848]: "
      "Unclassified event detected on md device /dev/md0";

  md_events_compile_regex(&r, regex_pattern);

  ret = md_events_match_regex(&r, matching_string);
  EXPECT_EQ_INT(ret, 0);

  ret = md_events_match_regex(&r, unmatching_string);
  EXPECT_EQ_INT(ret, -1);

  ret = md_events_match_regex(&r, unclass_event);
  EXPECT_EQ_INT(ret, -1);

  regfree(&r);

  return 0;
}

DEF_TEST(parse_events) {
  struct {
    const char *event;
    size_t len;
    int ret;
  } events[] = {{"Fail SpareActive RebuildFinished", 32, 0},
                {"\0", 1, -1},
                {"", 0, -1},
                {"MoveSpare", 9, 0},
                {"MoveSpare UnclassedEvent", 23, -1},
                {"MvoeSpare Fail", 14, -1}};

  int ret;
  for (int i = 0; i < STATIC_ARRAY_SIZE(events); i++) {
    ret = md_events_parse_events(events[i].event, events[i].len);
    EXPECT_EQ_INT(ret, events[i].ret);
  }

  return 0;
}

int main(void) {
  RUN_TEST(classify_event);
  // RUN_TEST(compile_regex);
  RUN_TEST(config);
  RUN_TEST(copy_match);
  RUN_TEST(dispatch_notification);
  RUN_TEST(get_max_len);
  RUN_TEST(match_regex);
  RUN_TEST(parse_events);
  END_TEST;
}
