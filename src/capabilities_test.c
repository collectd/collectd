/*
 * collectd - src/capabilities_test.c
 *
 * Copyright(c) 2019 Intel Corporation. All rights reserved.
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
 */

#include "capabilities.c" /* sic */
#include "testing.h"

#define RESULT_STRING_JSON                                                     \
  "{\"TEST_TYPE\":[{\"Name\":{\"MapName1\":\"MapValue1\",\"ListName1\":["      \
  "\"ListValue1\",\"ListValue2\"],\"MapName2\":\"MapValue2\"}},{\"Name\":{"    \
  "\"MapName1\":\"MapValue1\"}}]}"

static int idx = 0;
static char *test_dmi[][2] = {{NULL, NULL},
                              {"Name", NULL},
                              {"MapName1", "MapValue1"},
                              {"ListName1", NULL},
                              {NULL, "ListValue1"},
                              {NULL, "ListValue2"},
                              {"MapName2", "MapValue2"},
                              {NULL, NULL},
                              {"Name", NULL},
                              {"MapName1", "MapValue1"},
                              {NULL, NULL}};
static entry_type entry[] = {
    DMI_ENTRY_NONE,      DMI_ENTRY_NAME,       DMI_ENTRY_MAP,
    DMI_ENTRY_LIST_NAME, DMI_ENTRY_LIST_VALUE, DMI_ENTRY_LIST_VALUE,
    DMI_ENTRY_MAP,       DMI_ENTRY_NONE,       DMI_ENTRY_NAME,
    DMI_ENTRY_MAP,       DMI_ENTRY_END};
static size_t len = STATIC_ARRAY_SIZE(entry);

static struct MHD_Response *mhd_res = NULL;

/* mock functions */
int dmi_reader_init(dmi_reader_t *reader, const dmi_type type) {
  reader->current_type = DMI_ENTRY_NONE;
  return DMI_OK;
}

void dmi_reader_clean(dmi_reader_t *reader) {}

int dmi_read_next(dmi_reader_t *reader) {
  if (idx >= len)
    return DMI_ERROR;
  reader->current_type = entry[idx];
  reader->name = test_dmi[idx][0];
  reader->value = test_dmi[idx][1];
  idx++;
  return DMI_OK;
}

struct MHD_Daemon *MHD_start_daemon(unsigned int flags, unsigned short port,
                                    MHD_AcceptPolicyCallback apc, void *apc_cls,
                                    MHD_AccessHandlerCallback dh, void *dh_cls,
                                    ...) {
  return NULL;
}

void MHD_stop_daemon(struct MHD_Daemon *daemon) {}

struct MHD_Response *
MHD_create_response_from_buffer(size_t size, void *data,
                                enum MHD_ResponseMemoryMode mode) {
  return mhd_res;
}

struct MHD_Response *MHD_create_response_from_data(size_t size, void *data,
                                                   int must_free,
                                                   int must_copy) {
  return mhd_res;
}

MHD_RESULT MHD_add_response_header(struct MHD_Response *response,
                                   const char *header, const char *content) {
  return 0;
}

MHD_RESULT MHD_queue_response(struct MHD_Connection *connection,
                              unsigned int status_code,
                              struct MHD_Response *response) {
  return MHD_HTTP_OK;
}

void MHD_destroy_response(struct MHD_Response *response) {}
/* end mock functions */

DEF_TEST(plugin_config) {
  oconfig_item_t test_cfg_parent = {"capabilities", NULL, 0, NULL, NULL, 0};
  char value_buff[256] = "1234";
  char key_buff[256] = "port";
  oconfig_value_t test_cfg_value = {{value_buff}, OCONFIG_TYPE_STRING};
  oconfig_item_t test_cfg = {
      key_buff, &test_cfg_value, 1, &test_cfg_parent, NULL, 0};

  test_cfg_parent.children = &test_cfg;
  test_cfg_parent.children_num = 1;

  int ret = cap_config(&test_cfg_parent);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(1234, httpd_port);
  OK(NULL == httpd_host);

  strncpy(value_buff, "1", STATIC_ARRAY_SIZE(value_buff));
  ret = cap_config(&test_cfg_parent);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(1, httpd_port);

  strncpy(value_buff, "65535", STATIC_ARRAY_SIZE(value_buff));
  ret = cap_config(&test_cfg_parent);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(65535, httpd_port);

#if defined(MHD_VERSION) && MHD_VERSION >= 0x00090000
  strncpy(value_buff, "127.0.0.1", STATIC_ARRAY_SIZE(value_buff));
  strncpy(key_buff, "host", STATIC_ARRAY_SIZE(key_buff));

  ret = cap_config(&test_cfg_parent);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR("127.0.0.1", httpd_host);
  EXPECT_EQ_INT(65535, httpd_port);

  free(httpd_host);
  strncpy(key_buff, "port", STATIC_ARRAY_SIZE(key_buff));
#endif

  double port_value = 65535;
  oconfig_value_t test_cfg_value2 = {{.number = port_value},
                                     OCONFIG_TYPE_NUMBER};
  test_cfg.values = &test_cfg_value2;
  ret = cap_config(&test_cfg_parent);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(65535, httpd_port);

  return 0;
}

DEF_TEST(plugin_config_fail) {
  oconfig_item_t test_cfg_parent = {"capabilities", NULL, 0, NULL, NULL, 0};
  char value_buff[256] = "1";
  char key_buff[256] = "aport";
  oconfig_value_t test_cfg_value = {{value_buff}, OCONFIG_TYPE_STRING};
  oconfig_item_t test_cfg = {
      key_buff, &test_cfg_value, 1, &test_cfg_parent, NULL, 0};

  test_cfg_parent.children = &test_cfg;
  test_cfg_parent.children_num = 1;

  unsigned short default_port = httpd_port;
  int ret = cap_config(&test_cfg_parent);
  EXPECT_EQ_INT(-1, ret);
  EXPECT_EQ_INT(default_port, httpd_port);
  OK(NULL == httpd_host);

  /* Correct port range is 1 - 65535 */
  strncpy(key_buff, "port", STATIC_ARRAY_SIZE(key_buff));
  strncpy(value_buff, "-1", STATIC_ARRAY_SIZE(value_buff));
  ret = cap_config(&test_cfg_parent);
  EXPECT_EQ_INT(-1, ret);
  EXPECT_EQ_INT(default_port, httpd_port);
  OK(NULL == httpd_host);

  strncpy(value_buff, "65536", STATIC_ARRAY_SIZE(value_buff));
  ret = cap_config(&test_cfg_parent);
  EXPECT_EQ_INT(-1, ret);
  EXPECT_EQ_INT(default_port, httpd_port);
  OK(NULL == httpd_host);

#if defined(MHD_VERSION) && MHD_VERSION >= 0x00090000
  strncpy(value_buff, "127.0.0.1", STATIC_ARRAY_SIZE(value_buff));
  strncpy(key_buff, "host", STATIC_ARRAY_SIZE(key_buff));
  test_cfg_value.type = OCONFIG_TYPE_NUMBER;
  ret = cap_config(&test_cfg_parent);
  EXPECT_EQ_INT(-1, ret);
  EXPECT_EQ_INT(default_port, httpd_port);
  OK(NULL == httpd_host);

  strncpy(key_buff, "port", STATIC_ARRAY_SIZE(key_buff));
#endif

  double port_value = 65536;
  oconfig_value_t test_cfg_value2 = {{.number = port_value},
                                     OCONFIG_TYPE_NUMBER};
  test_cfg.values = &test_cfg_value2;
  ret = cap_config(&test_cfg_parent);
  EXPECT_EQ_INT(-1, ret);
  EXPECT_EQ_INT(default_port, httpd_port);
  OK(NULL == httpd_host);

  return 0;
}

DEF_TEST(http_handler) {
  void *state = NULL;
  g_cap_json = "TEST";
  int ret = cap_http_handler(NULL, NULL, NULL, MHD_HTTP_METHOD_PUT, NULL, NULL,
                             NULL, &state);
  EXPECT_EQ_INT(MHD_NO, ret);
  OK(NULL == state);

  ret = cap_http_handler(NULL, NULL, NULL, MHD_HTTP_METHOD_GET, NULL, NULL,
                         NULL, &state);
  EXPECT_EQ_INT(MHD_YES, ret);
  CHECK_NOT_NULL(state);

  ret = cap_http_handler(NULL, NULL, NULL, MHD_HTTP_METHOD_GET, NULL, NULL,
                         NULL, &state);
  EXPECT_EQ_INT(MHD_NO, ret);
  CHECK_NOT_NULL(state);

  /* mock not NULL pointer */
  mhd_res = (struct MHD_Response *)&(int){0};
  ret = cap_http_handler(NULL, NULL, NULL, MHD_HTTP_METHOD_GET, NULL, NULL,
                         NULL, &state);
  EXPECT_EQ_INT(MHD_HTTP_OK, ret);
  CHECK_NOT_NULL(state);

  g_cap_json = NULL;
  mhd_res = NULL;

  return 0;
}

DEF_TEST(get_dmi_variables) {
  json_t *root = json_object();
  CHECK_NOT_NULL(root);

  int ret = cap_get_dmi_variables(root, 0, "TEST_TYPE");
  EXPECT_EQ_INT(0, ret);

  char *test_str = json_dumps(root, JSON_COMPACT | JSON_PRESERVE_ORDER);
  CHECK_NOT_NULL(test_str);
  json_decref(root);
  EXPECT_EQ_STR(RESULT_STRING_JSON, test_str);

  free(test_str);

  root = json_object();
  CHECK_NOT_NULL(root);
  ret = cap_get_dmi_variables(root, 1, "TEST_TYPE2");
  EXPECT_EQ_INT(-1, ret);

  test_str = json_dumps(root, JSON_COMPACT | JSON_PRESERVE_ORDER);
  CHECK_NOT_NULL(test_str);
  json_decref(root);
  EXPECT_EQ_STR("{\"TEST_TYPE2\":[]}", test_str);

  free(test_str);

  return 0;
}

int main(void) {
  RUN_TEST(plugin_config_fail);
  RUN_TEST(plugin_config);

  RUN_TEST(http_handler);
  RUN_TEST(get_dmi_variables);

  END_TEST;
}
