/**
 * collectd - src/pcie_errors.c
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

#define plugin_dispatch_notification plugin_dispatch_notification_pcie_test

#include "pcie_errors.c" /* sic */
#include "testing.h"

#define TEST_DOMAIN 1
#define TEST_BUS 5
#define TEST_DEVICE 0xc
#define TEST_FUNCTION 2
#define TEST_DEVICE_STR "0001:05:0c.2"

#define G_BUFF_LEN 4

static notification_t last_notif;
static char g_buff[G_BUFF_LEN];

/* mock functions */
int plugin_dispatch_notification_pcie_test(const notification_t *notif) {
  last_notif = *notif;
  return ENOTSUP;
}

ssize_t pread(__attribute__((unused)) int fd, void *buf, size_t count,
              __attribute__((unused)) off_t offset) {
  if (count == 0 || count > G_BUFF_LEN)
    return -1;

  memcpy(buf, g_buff, count);
  return count;
}
/* end mock functions */

DEF_TEST(clear_dev_list) {
  pcie_clear_list(NULL);

  llist_t *test_list = llist_create();
  CHECK_NOT_NULL(test_list);

  pcie_device_t *dev = calloc(1, sizeof(*dev));
  CHECK_NOT_NULL(dev);

  llentry_t *entry = llentry_create(NULL, dev);
  CHECK_NOT_NULL(entry);

  llist_append(test_list, entry);

  for (llentry_t *e = llist_head(test_list); e != NULL; e = e->next) {
    EXPECT_EQ_PTR(dev, e->value);
  }

  pcie_clear_list(test_list);

  return 0;
}

DEF_TEST(add_to_list) {
  llist_t *test_list = llist_create();
  CHECK_NOT_NULL(test_list);

  int ret = pcie_add_device(test_list, TEST_DOMAIN, TEST_BUS, TEST_DEVICE,
                            TEST_FUNCTION);
  EXPECT_EQ_INT(0, ret);

  llentry_t *e = llist_head(test_list);
  CHECK_NOT_NULL(e);
  OK(NULL == e->next);

  pcie_device_t *dev = e->value;
  CHECK_NOT_NULL(dev);
  EXPECT_EQ_INT(TEST_DOMAIN, dev->domain);
  EXPECT_EQ_INT(TEST_BUS, dev->bus);
  EXPECT_EQ_INT(TEST_DEVICE, dev->device);
  EXPECT_EQ_INT(TEST_FUNCTION, dev->function);
  EXPECT_EQ_INT(-1, dev->cap_exp);
  EXPECT_EQ_INT(-1, dev->ecap_aer);

  pcie_clear_list(test_list);

  return 0;
}

DEF_TEST(pcie_read) {
  int ret;
  pcie_device_t dev = {0};
  uint32_t val = 0;
  g_buff[0] = 4;
  g_buff[1] = 3;
  g_buff[2] = 2;
  g_buff[3] = 1;

  ret = pcie_read(&dev, &val, 1, 0);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(4, val);

  ret = pcie_read(&dev, &val, 2, 0);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(0x304, val);

  ret = pcie_read(&dev, &val, 3, 0);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(0x20304, val);

  ret = pcie_read(&dev, &val, 4, 0);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(0x1020304, val);

  ret = pcie_read(&dev, &val, G_BUFF_LEN + 1, 0);
  EXPECT_EQ_INT(-1, ret);

  pcie_fops.read = pcie_read;

  uint8_t val8 = pcie_read8(&dev, 0);
  EXPECT_EQ_INT(4, val8);

  uint16_t val16 = pcie_read16(&dev, 0);
  EXPECT_EQ_INT(0x304, val16);

  uint32_t val32 = pcie_read32(&dev, 0);
  EXPECT_EQ_INT(0x1020304, val32);

  return 0;
}

DEF_TEST(dispatch_notification) {
  pcie_device_t dev = {0, TEST_DOMAIN, TEST_BUS, TEST_DEVICE, TEST_FUNCTION,
                       0, 0,           0,        0,           0};
  cdtime_t t = cdtime();
  notification_t n = {
      .severity = 1, .time = t, .plugin = "pcie_errors_test", .meta = NULL};

  pcie_dispatch_notification(&dev, &n, "test_type", "test_type_instance");
  EXPECT_EQ_INT(1, last_notif.severity);
  EXPECT_EQ_UINT64(t, last_notif.time);
  EXPECT_EQ_STR("pcie_errors_test", last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(hostname_g, last_notif.host);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR("test_type", last_notif.type);
  EXPECT_EQ_STR("test_type_instance", last_notif.type_instance);

  return 0;
}

DEF_TEST(access_config) {
  pcie_config.use_sysfs = 0;
  pcie_access_config();
  EXPECT_EQ_PTR(pcie_list_devices_proc, pcie_fops.list_devices);
  EXPECT_EQ_PTR(pcie_open_proc, pcie_fops.open);
  EXPECT_EQ_PTR(pcie_close, pcie_fops.close);
  EXPECT_EQ_PTR(pcie_read, pcie_fops.read);
  EXPECT_EQ_STR(PCIE_DEFAULT_PROCDIR, pcie_config.access_dir);

  sstrncpy(pcie_config.access_dir, "Test", sizeof(pcie_config.access_dir));
  pcie_access_config();
  EXPECT_EQ_STR("Test", pcie_config.access_dir);

  pcie_config.use_sysfs = 1;
  pcie_access_config();
  EXPECT_EQ_PTR(pcie_list_devices_sysfs, pcie_fops.list_devices);
  EXPECT_EQ_PTR(pcie_open_sysfs, pcie_fops.open);
  EXPECT_EQ_PTR(pcie_close, pcie_fops.close);
  EXPECT_EQ_PTR(pcie_read, pcie_fops.read);
  EXPECT_EQ_STR("Test", pcie_config.access_dir);

  pcie_config.access_dir[0] = '\0';
  pcie_access_config();
  EXPECT_EQ_STR(PCIE_DEFAULT_SYSFSDIR, pcie_config.access_dir);

  return 0;
}

DEF_TEST(plugin_config_fail) {
  oconfig_item_t test_cfg_parent = {"pcie_errors", NULL, 0, NULL, NULL, 0};
  char value_buff[256] = "procs";
  char key_buff[256] = "Sources";
  oconfig_value_t test_cfg_value = {{value_buff}, OCONFIG_TYPE_STRING};
  oconfig_item_t test_cfg = {
      key_buff, &test_cfg_value, 1, &test_cfg_parent, NULL, 0};

  test_cfg_parent.children = &test_cfg;
  test_cfg_parent.children_num = 1;

  int ret = pcie_plugin_config(&test_cfg_parent);
  EXPECT_EQ_INT(-1, ret);

  sstrncpy(key_buff, "Source", sizeof(key_buff));
  ret = pcie_plugin_config(&test_cfg_parent);
  EXPECT_EQ_INT(-1, ret);

  sstrncpy(value_buff, "proc", sizeof(value_buff));
  test_cfg_value.type = OCONFIG_TYPE_NUMBER;
  ret = pcie_plugin_config(&test_cfg_parent);
  EXPECT_EQ_INT(-1, ret);

  sstrncpy(key_buff, "AccessDir", sizeof(key_buff));
  ret = pcie_plugin_config(&test_cfg_parent);
  EXPECT_EQ_INT(-1, ret);

  return 0;
}

DEF_TEST(plugin_config) {
  oconfig_item_t test_cfg_parent = {"pcie_errors", NULL, 0, NULL, NULL, 0};
  char value_buff[256] = "proc";
  char key_buff[256] = "source";
  oconfig_value_t test_cfg_value = {{value_buff}, OCONFIG_TYPE_STRING};
  oconfig_item_t test_cfg = {
      key_buff, &test_cfg_value, 1, &test_cfg_parent, NULL, 0};

  test_cfg_parent.children = &test_cfg;
  test_cfg_parent.children_num = 1;

  pcie_config.use_sysfs = 1;
  int ret = pcie_plugin_config(&test_cfg_parent);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(0, pcie_config.use_sysfs);

  pcie_config.use_sysfs = 1;
  sstrncpy(value_buff, "sysfs", sizeof(value_buff));
  ret = pcie_plugin_config(&test_cfg_parent);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(1, pcie_config.use_sysfs);

  sstrncpy(key_buff, "AccessDir", sizeof(key_buff));
  sstrncpy(value_buff, "some/test/value", sizeof(value_buff));
  ret = pcie_plugin_config(&test_cfg_parent);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_STR("some/test/value", pcie_config.access_dir);

  memset(&test_cfg_value.value, 0, sizeof(test_cfg_value.value));
  test_cfg_value.value.boolean = 1;
  test_cfg_value.type = OCONFIG_TYPE_BOOLEAN;
  sstrncpy(key_buff, "ReportMasked", sizeof(key_buff));
  ret = pcie_plugin_config(&test_cfg_parent);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(1, pcie_config.notif_masked);

  sstrncpy(key_buff, "PersistentNotifications", sizeof(key_buff));
  ret = pcie_plugin_config(&test_cfg_parent);
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(1, pcie_config.persistent);

  return 0;
}

#define BAD_TLP_SET_MSG "Correctable Error set: Bad TLP Status"
#define BAD_TLP_CLEAR_MSG "Correctable Error cleared: Bad TLP Status"

DEF_TEST(dispatch_correctable_errors) {
  pcie_device_t dev = {0, TEST_DOMAIN, TEST_BUS, TEST_DEVICE, TEST_FUNCTION,
                       0, 0,           0,        0,           0};
  pcie_config.notif_masked = 0;
  pcie_config.persistent = 0;

  pcie_dispatch_correctable_errors(&dev, PCI_ERR_COR_BAD_TLP,
                                   ~(PCI_ERR_COR_BAD_TLP));
  EXPECT_EQ_INT(NOTIF_WARNING, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_CE, last_notif.type_instance);
  EXPECT_EQ_STR(BAD_TLP_SET_MSG, last_notif.message);

  memset(&last_notif, 0, sizeof(last_notif));
  dev.correctable_errors = PCI_ERR_COR_BAD_TLP;
  pcie_dispatch_correctable_errors(&dev, PCI_ERR_COR_BAD_TLP,
                                   ~(PCI_ERR_COR_BAD_TLP));
  EXPECT_EQ_STR("", last_notif.plugin_instance);

  pcie_config.persistent = 1;
  pcie_dispatch_correctable_errors(&dev, PCI_ERR_COR_BAD_TLP,
                                   ~(PCI_ERR_COR_BAD_TLP));
  EXPECT_EQ_INT(NOTIF_WARNING, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_CE, last_notif.type_instance);
  EXPECT_EQ_STR(BAD_TLP_SET_MSG, last_notif.message);

  memset(&last_notif, 0, sizeof(last_notif));
  pcie_dispatch_correctable_errors(&dev, PCI_ERR_COR_BAD_TLP,
                                   PCI_ERR_COR_BAD_TLP);
  EXPECT_EQ_STR("", last_notif.plugin_instance);

  pcie_config.notif_masked = 1;
  pcie_dispatch_correctable_errors(&dev, PCI_ERR_COR_BAD_TLP,
                                   PCI_ERR_COR_BAD_TLP);
  EXPECT_EQ_INT(NOTIF_WARNING, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_CE, last_notif.type_instance);
  EXPECT_EQ_STR(BAD_TLP_SET_MSG, last_notif.message);

  pcie_config.persistent = 0;
  memset(&last_notif, 0, sizeof(last_notif));
  pcie_dispatch_correctable_errors(&dev, PCI_ERR_COR_BAD_TLP,
                                   PCI_ERR_COR_BAD_TLP);
  EXPECT_EQ_STR("", last_notif.plugin_instance);

  dev.correctable_errors = 0;
  pcie_dispatch_correctable_errors(&dev, PCI_ERR_COR_BAD_TLP,
                                   PCI_ERR_COR_BAD_TLP);
  EXPECT_EQ_INT(NOTIF_WARNING, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_CE, last_notif.type_instance);
  EXPECT_EQ_STR(BAD_TLP_SET_MSG, last_notif.message);

  pcie_dispatch_correctable_errors(&dev, PCI_ERR_COR_BAD_TLP,
                                   ~(PCI_ERR_COR_BAD_TLP));
  EXPECT_EQ_INT(NOTIF_WARNING, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_CE, last_notif.type_instance);
  EXPECT_EQ_STR(BAD_TLP_SET_MSG, last_notif.message);

  pcie_config.notif_masked = 0;
  dev.correctable_errors = PCI_ERR_COR_BAD_TLP;
  pcie_dispatch_correctable_errors(&dev, 0, ~(PCI_ERR_COR_BAD_TLP));
  EXPECT_EQ_INT(NOTIF_OKAY, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_CE, last_notif.type_instance);
  EXPECT_EQ_STR(BAD_TLP_CLEAR_MSG, last_notif.message);

  return 0;
}

#define FCP_NF_SET_MSG                                                         \
  "Uncorrectable(non_fatal) Error set: Flow Control Protocol"
#define FCP_F_SET_MSG "Uncorrectable(fatal) Error set: Flow Control Protocol"
#define FCP_NF_CLEAR_MSG                                                       \
  "Uncorrectable(non_fatal) Error cleared: Flow Control Protocol"
#define FCP_F_CLEAR_MSG                                                        \
  "Uncorrectable(fatal) Error cleared: Flow Control Protocol"

DEF_TEST(dispatch_uncorrectable_errors) {
  pcie_device_t dev = {0, TEST_DOMAIN, TEST_BUS, TEST_DEVICE, TEST_FUNCTION,
                       0, 0,           0,        0,           0};
  pcie_config.notif_masked = 0;
  pcie_config.persistent = 0;

  pcie_dispatch_uncorrectable_errors(&dev, PCI_ERR_UNC_FCP, ~(PCI_ERR_UNC_FCP),
                                     ~(PCI_ERR_UNC_FCP));
  EXPECT_EQ_INT(NOTIF_WARNING, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_NOFATAL, last_notif.type_instance);
  EXPECT_EQ_STR(FCP_NF_SET_MSG, last_notif.message);

  pcie_dispatch_uncorrectable_errors(&dev, PCI_ERR_UNC_FCP, ~(PCI_ERR_UNC_FCP),
                                     PCI_ERR_UNC_FCP);
  EXPECT_EQ_INT(NOTIF_FAILURE, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_FATAL, last_notif.type_instance);
  EXPECT_EQ_STR(FCP_F_SET_MSG, last_notif.message);

  memset(&last_notif, 0, sizeof(last_notif));
  dev.uncorrectable_errors = PCI_ERR_UNC_FCP;
  pcie_dispatch_uncorrectable_errors(&dev, PCI_ERR_UNC_FCP, ~(PCI_ERR_UNC_FCP),
                                     PCI_ERR_UNC_FCP);
  EXPECT_EQ_STR("", last_notif.plugin_instance);

  pcie_config.persistent = 1;
  pcie_dispatch_uncorrectable_errors(&dev, PCI_ERR_UNC_FCP, ~(PCI_ERR_UNC_FCP),
                                     PCI_ERR_UNC_FCP);
  EXPECT_EQ_INT(NOTIF_FAILURE, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_FATAL, last_notif.type_instance);
  EXPECT_EQ_STR(FCP_F_SET_MSG, last_notif.message);

  memset(&last_notif, 0, sizeof(last_notif));
  pcie_dispatch_uncorrectable_errors(&dev, PCI_ERR_UNC_FCP, PCI_ERR_UNC_FCP,
                                     PCI_ERR_UNC_FCP);
  EXPECT_EQ_STR("", last_notif.plugin_instance);

  pcie_config.notif_masked = 1;
  pcie_dispatch_uncorrectable_errors(&dev, PCI_ERR_UNC_FCP, PCI_ERR_UNC_FCP,
                                     PCI_ERR_UNC_FCP);
  EXPECT_EQ_INT(NOTIF_FAILURE, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_FATAL, last_notif.type_instance);
  EXPECT_EQ_STR(FCP_F_SET_MSG, last_notif.message);

  pcie_config.persistent = 0;
  dev.uncorrectable_errors = 0;
  memset(&last_notif, 0, sizeof(last_notif));
  pcie_dispatch_uncorrectable_errors(&dev, PCI_ERR_UNC_FCP, ~(PCI_ERR_UNC_FCP),
                                     PCI_ERR_UNC_FCP);
  EXPECT_EQ_INT(NOTIF_FAILURE, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_FATAL, last_notif.type_instance);
  EXPECT_EQ_STR(FCP_F_SET_MSG, last_notif.message);

  pcie_config.notif_masked = 0;
  dev.uncorrectable_errors = PCI_ERR_UNC_FCP;
  pcie_dispatch_uncorrectable_errors(&dev, 0, ~(PCI_ERR_UNC_FCP),
                                     ~(PCI_ERR_UNC_FCP));
  EXPECT_EQ_INT(NOTIF_OKAY, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_NOFATAL, last_notif.type_instance);
  EXPECT_EQ_STR(FCP_NF_CLEAR_MSG, last_notif.message);

  memset(&last_notif, 0, sizeof(last_notif));
  pcie_dispatch_uncorrectable_errors(&dev, 0, ~(PCI_ERR_UNC_FCP),
                                     PCI_ERR_UNC_FCP);
  EXPECT_EQ_INT(NOTIF_OKAY, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_FATAL, last_notif.type_instance);
  EXPECT_EQ_STR(FCP_F_CLEAR_MSG, last_notif.message);

  return 0;
}

#define UR_SET_MSG "Device Status Error set: Unsupported Request"
#define UR_CLEAR_MSG "Device Status Error cleared: Unsupported Request"
#define FE_SET_MSG "Device Status Error set: Fatal Error"
#define FE_CLEAR_MSG "Device Status Error cleared: Fatal Error"

DEF_TEST(device_status_errors) {
  pcie_device_t dev = {0, TEST_DOMAIN, TEST_BUS, TEST_DEVICE, TEST_FUNCTION,
                       0, 0,           0,        0,           0};
  pcie_config.persistent = 0;
  g_buff[0] = (PCI_EXP_DEVSTA_URD & 0xff);

  memset(&last_notif, 0, sizeof(last_notif));
  pcie_check_dev_status(&dev, 0);
  EXPECT_EQ_INT(NOTIF_WARNING, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_NOFATAL, last_notif.type_instance);
  EXPECT_EQ_STR(UR_SET_MSG, last_notif.message);

  memset(&last_notif, 0, sizeof(last_notif));
  pcie_check_dev_status(&dev, 0);
  EXPECT_EQ_STR("", last_notif.plugin_instance);

  pcie_config.persistent = 1;
  pcie_check_dev_status(&dev, 0);
  EXPECT_EQ_INT(NOTIF_WARNING, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_NOFATAL, last_notif.type_instance);
  EXPECT_EQ_STR(UR_SET_MSG, last_notif.message);

  g_buff[0] = 0;
  pcie_check_dev_status(&dev, 0);
  EXPECT_EQ_INT(NOTIF_OKAY, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_NOFATAL, last_notif.type_instance);
  EXPECT_EQ_STR(UR_CLEAR_MSG, last_notif.message);

  pcie_config.persistent = 0;
  dev.device_status = PCI_EXP_DEVSTA_URD;
  pcie_check_dev_status(&dev, 0);
  EXPECT_EQ_INT(NOTIF_OKAY, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_NOFATAL, last_notif.type_instance);
  EXPECT_EQ_STR(UR_CLEAR_MSG, last_notif.message);

  memset(&last_notif, 0, sizeof(last_notif));
  pcie_check_dev_status(&dev, 0);
  EXPECT_EQ_STR("", last_notif.plugin_instance);

  g_buff[0] = (PCI_EXP_DEVSTA_FED & 0xff);
  pcie_check_dev_status(&dev, 0);
  EXPECT_EQ_INT(NOTIF_FAILURE, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_FATAL, last_notif.type_instance);
  EXPECT_EQ_STR(FE_SET_MSG, last_notif.message);

  g_buff[0] = 0;
  pcie_check_dev_status(&dev, 0);
  EXPECT_EQ_INT(NOTIF_OKAY, last_notif.severity);
  EXPECT_EQ_STR(PCIE_ERRORS_PLUGIN, last_notif.plugin);
  OK(NULL == last_notif.meta);
  EXPECT_EQ_STR(TEST_DEVICE_STR, last_notif.plugin_instance);
  EXPECT_EQ_STR(PCIE_ERROR, last_notif.type);
  EXPECT_EQ_STR(PCIE_SEV_FATAL, last_notif.type_instance);
  EXPECT_EQ_STR(FE_CLEAR_MSG, last_notif.message);

  return 0;
}

int main(void) {
  RUN_TEST(clear_dev_list);
  RUN_TEST(add_to_list);
  RUN_TEST(pcie_read);
  RUN_TEST(dispatch_notification);

  RUN_TEST(access_config);
  RUN_TEST(plugin_config_fail);
  RUN_TEST(plugin_config);

  RUN_TEST(dispatch_correctable_errors);
  RUN_TEST(dispatch_uncorrectable_errors);
  RUN_TEST(device_status_errors);

  END_TEST;
}
