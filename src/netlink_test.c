/**
 * collectd - src/netlink_test.c
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

#define plugin_dispatch_values plugin_dispatch_values_nl_test

#include "netlink.c" /* sic */
#include "testing.h"

#ifdef HAVE_IFLA_VF_STATS
static vf_stats_t g_test_res;
static char g_instance[512];
static int g_type_valid;
static void *g_test_payload;
static uint16_t g_attr_type;

/* mock functions */
int plugin_dispatch_values_nl_test(value_list_t const *vl) {
  if (g_instance[0] == '\0')
    sstrncpy(g_instance, vl->plugin_instance, sizeof(g_instance));

  if (strcmp("vf_link_info", vl->type) == 0 &&
      strcmp("vlan", vl->type_instance) == 0)
    g_test_res.vlan = vl->values[0].gauge;

  if (strcmp("vf_link_info", vl->type) == 0 &&
      strcmp("spoofcheck", vl->type_instance) == 0)
    g_test_res.spoofcheck = vl->values[0].gauge;

  if (strcmp("vf_link_info", vl->type) == 0 &&
      strcmp("link_state", vl->type_instance) == 0)
    g_test_res.link_state = vl->values[0].gauge;

  if (strcmp("vf_broadcast", vl->type) == 0)
    g_test_res.broadcast = vl->values[0].derive;

  if (strcmp("vf_multicast", vl->type) == 0)
    g_test_res.multicast = vl->values[0].derive;

  if (strcmp("vf_packets", vl->type) == 0) {
    g_test_res.rx_packets = vl->values[0].derive;
    g_test_res.tx_packets = vl->values[1].derive;
  }

  if (strcmp("vf_bytes", vl->type) == 0) {
    g_test_res.rx_bytes = vl->values[0].derive;
    g_test_res.tx_bytes = vl->values[1].derive;
  }

  return 0;
}

int mnl_attr_type_valid(__attribute__((unused)) const struct nlattr *attr,
                        __attribute__((unused)) uint16_t maxtype) {
  return g_type_valid;
}

uint16_t mnl_attr_get_type(__attribute__((unused)) const struct nlattr *attr) {
  return g_attr_type;
}

int mnl_attr_validate(const struct nlattr *attr, enum mnl_attr_data_type type) {
  return attr->nla_type == type ? 0 : -1;
}

int mnl_attr_validate2(const struct nlattr *attr, enum mnl_attr_data_type type,
                       __attribute__((unused)) size_t len) {
  return attr->nla_type == type ? 0 : -1;
}

void *mnl_attr_get_payload(__attribute__((unused)) const struct nlattr *attr) {
  return g_test_payload;
}
#else /* HAVE_IFLA_VF_STATS */
int plugin_dispatch_values_nl_test(__attribute__((unused))
                                   value_list_t const *vl) {
  return 0;
}
#endif
/* end mock functions */

#ifdef HAVE_IFLA_VF_STATS
DEF_TEST(plugin_nl_config) {
  EXPECT_EQ_INT(0, collect_vf_stats);
  int ret = ir_config("CollectVFStats", "true");
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(1, collect_vf_stats);
  ret = ir_config("CollectVFStats", "0");
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(0, collect_vf_stats);
  ret = ir_config("CollectVFStats", "true false");
  EXPECT_EQ_INT(-1, ret);
  EXPECT_EQ_INT(0, collect_vf_stats);
  ret = ir_config("CollectVFStats", "false");
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(0, collect_vf_stats);
  ret = ir_config("CollectVFStats", "yes");
  EXPECT_EQ_INT(0, ret);
  EXPECT_EQ_INT(1, collect_vf_stats);

  return 0;
}
#endif

DEF_TEST(ignorelist_test) {
  ir_ignorelist_invert = 1;
  int ret = add_ignorelist("eno1", "interface", NULL);
  EXPECT_EQ_INT(0, ret);
  ret = add_ignorelist("eno2", "if_detail", NULL);
  EXPECT_EQ_INT(0, ret);

  ret = check_ignorelist("eno1", "interface", NULL);
  EXPECT_EQ_INT(0, ret);
  ret = check_ignorelist("eno2", "if_detail", NULL);
  EXPECT_EQ_INT(0, ret);
  ret = check_ignorelist("eno1", "if_detail", NULL);
  EXPECT_EQ_INT(1, ret);
  ret = check_ignorelist("eno2", "interface", NULL);
  EXPECT_EQ_INT(1, ret);

#if HAVE_REGEX_H
  ret = add_ignorelist("/^eno[1-3]|^eth[0-2|4]/", "interface", NULL);
  EXPECT_EQ_INT(0, ret);
  ret = add_ignorelist("/^ens0[1|3]/", "if_detail", NULL);
  EXPECT_EQ_INT(0, ret);

  ret = check_ignorelist("eno1", "interface", NULL);
  EXPECT_EQ_INT(0, ret);
  ret = check_ignorelist("eno3", "interface", NULL);
  EXPECT_EQ_INT(0, ret);
  ret = check_ignorelist("eth0", "interface", NULL);
  EXPECT_EQ_INT(0, ret);
  ret = check_ignorelist("eth1", "interface", NULL);
  EXPECT_EQ_INT(0, ret);
  ret = check_ignorelist("eth2", "interface", NULL);
  EXPECT_EQ_INT(0, ret);
  ret = check_ignorelist("eth3", "interface", NULL);
  EXPECT_EQ_INT(1, ret);
  ret = check_ignorelist("eth4", "interface", NULL);
  EXPECT_EQ_INT(0, ret);

  ret = check_ignorelist("ens01", "if_detail", NULL);
  EXPECT_EQ_INT(0, ret);
  ret = check_ignorelist("ens02", "if_detail", NULL);
  EXPECT_EQ_INT(1, ret);
  ret = check_ignorelist("ens03", "if_detail", NULL);
  EXPECT_EQ_INT(0, ret);

  ret = check_ignorelist("eth0", "if_detail", NULL);
  EXPECT_EQ_INT(1, ret);
  ret = check_ignorelist("ens01", "interface", NULL);
  EXPECT_EQ_INT(1, ret);
#endif

  ir_ignorelist_invert = 0;
  ret = check_ignorelist("eno1", "interface", NULL);
  EXPECT_EQ_INT(1, ret);
  ret = check_ignorelist("eno2", "if_detail", NULL);
  EXPECT_EQ_INT(1, ret);
  ret = check_ignorelist("abcdf", "if_detail", NULL);
  EXPECT_EQ_INT(0, ret);
  ret = check_ignorelist("abcfdf", "interface", NULL);
  EXPECT_EQ_INT(0, ret);

#if HAVE_REGEX_H
  ret = check_ignorelist("ens03", "if_detail", NULL);
  EXPECT_EQ_INT(1, ret);
#endif
  ir_ignorelist_invert = 1;

  ir_ignorelist_t *next = NULL;
  for (ir_ignorelist_t *i = ir_ignorelist_head; i != NULL; i = next) {
    next = i->next;
#if HAVE_REGEX_H
    if (i->rdevice != NULL) {
      regfree(i->rdevice);
      sfree(i->rdevice);
    }
#endif
    sfree(i->inst);
    sfree(i->type);
    sfree(i->device);
    sfree(i);
  }
  ir_ignorelist_head = NULL;

  return 0;
}

#ifdef HAVE_IFLA_VF_STATS
DEF_TEST(vf_submit_test) {
  const char *test_dev = "eth0";
  vf_stats_t test_stats;
  struct ifla_vf_mac test_mac;
  test_mac.mac[0] = 0x01;
  test_mac.mac[1] = 0x1a;
  test_mac.mac[2] = 0x2b;
  test_mac.mac[3] = 0x3c;
  test_mac.mac[4] = 0x4d;
  test_mac.mac[5] = 0x5e;
  test_mac.vf = 2;
  test_stats.vf_mac = &test_mac;
  test_stats.vlan = 100;
  test_stats.spoofcheck = 1;
  test_stats.link_state = 2;
  test_stats.broadcast = 1234;
  test_stats.multicast = 0;
  test_stats.rx_packets = 21110;
  test_stats.tx_packets = 31110;
  test_stats.rx_bytes = 4294967295;
  test_stats.tx_bytes = 8;

  g_instance[0] = '\0';
  vf_info_submit(test_dev, &test_stats);

  EXPECT_EQ_STR("eth0_vf2_01:1a:2b:3c:4d:5e", g_instance);
  EXPECT_EQ_UINT64(100, g_test_res.vlan);
  EXPECT_EQ_UINT64(1, g_test_res.spoofcheck);
  EXPECT_EQ_UINT64(2, g_test_res.link_state);
  EXPECT_EQ_UINT64(1234, g_test_res.broadcast);
  EXPECT_EQ_UINT64(0, g_test_res.multicast);
  EXPECT_EQ_UINT64(21110, g_test_res.rx_packets);
  EXPECT_EQ_UINT64(31110, g_test_res.tx_packets);
  EXPECT_EQ_UINT64(4294967295, g_test_res.rx_bytes);
  EXPECT_EQ_UINT64(8, g_test_res.tx_bytes);

  return 0;
}

DEF_TEST(vf_info_attr_cb_test) {
  struct nlattr attr;
  vf_stats_t test_stats = {0};

  attr.nla_type = -1;
  g_type_valid = -1;
  int ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_OK, ret);

  struct ifla_vf_mac test_mac;
  g_test_payload = &test_mac;
  g_type_valid = 0;
  g_attr_type = IFLA_VF_MAC;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_ERROR, ret);

  attr.nla_type = MNL_TYPE_UNSPEC;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_OK, ret);
  EXPECT_EQ_PTR(&test_mac, test_stats.vf_mac);

  struct ifla_vf_vlan test_vlan = {.vlan = 1024, .qos = 2};
  g_test_payload = &test_vlan;
  g_attr_type = IFLA_VF_VLAN;
  attr.nla_type = -1;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_ERROR, ret);

  attr.nla_type = MNL_TYPE_UNSPEC;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_OK, ret);
  EXPECT_EQ_UINT64(1024, test_stats.vlan);
  EXPECT_EQ_UINT64(2, test_stats.qos);

  struct ifla_vf_tx_rate test_tx_rate = {.rate = 100};
  g_test_payload = &test_tx_rate;
  g_attr_type = IFLA_VF_TX_RATE;
  attr.nla_type = -1;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_ERROR, ret);

  attr.nla_type = MNL_TYPE_UNSPEC;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_OK, ret);
  EXPECT_EQ_UINT64(100, test_stats.txrate);

  struct ifla_vf_spoofchk test_spoofchk = {.setting = 1};
  g_test_payload = &test_spoofchk;
  g_attr_type = IFLA_VF_SPOOFCHK;
  attr.nla_type = -1;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_ERROR, ret);

  attr.nla_type = MNL_TYPE_UNSPEC;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_OK, ret);
  EXPECT_EQ_UINT64(1, test_stats.spoofcheck);

  struct ifla_vf_link_state test_link_state = {.link_state = 2};
  g_test_payload = &test_link_state;
  g_attr_type = IFLA_VF_LINK_STATE;
  attr.nla_type = -1;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_ERROR, ret);

  attr.nla_type = MNL_TYPE_UNSPEC;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_OK, ret);
  EXPECT_EQ_UINT64(2, test_stats.link_state);

  struct ifla_vf_rate test_rate = {.min_tx_rate = 1000, .max_tx_rate = 2001};
  g_test_payload = &test_rate;
  g_attr_type = IFLA_VF_RATE;
  attr.nla_type = -1;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_ERROR, ret);

  attr.nla_type = MNL_TYPE_UNSPEC;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_OK, ret);
  EXPECT_EQ_UINT64(1000, test_stats.min_txrate);
  EXPECT_EQ_UINT64(2001, test_stats.max_txrate);

  struct ifla_vf_rss_query_en test_query_en = {.setting = 1};
  g_test_payload = &test_query_en;
  g_attr_type = IFLA_VF_RSS_QUERY_EN;
  attr.nla_type = -1;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_ERROR, ret);

  attr.nla_type = MNL_TYPE_UNSPEC;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_OK, ret);
  EXPECT_EQ_UINT64(1, test_stats.rss_query_en);

  struct ifla_vf_trust test_trust = {.setting = 1};
  g_test_payload = &test_trust;
  g_attr_type = IFLA_VF_TRUST;
  attr.nla_type = -1;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_ERROR, ret);

  attr.nla_type = MNL_TYPE_UNSPEC;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_OK, ret);
  EXPECT_EQ_UINT64(1, test_stats.trust);

  g_attr_type = IFLA_VF_STATS;
  ret = vf_info_attr_cb(&attr, &test_stats);
  EXPECT_EQ_INT(MNL_CB_ERROR, ret);

  return 0;
}
#endif /* HAVE_IFLA_VF_STATS */

int main(void) {
#ifdef HAVE_IFLA_VF_STATS
  RUN_TEST(plugin_nl_config);
#endif
  RUN_TEST(ignorelist_test);

#ifdef HAVE_IFLA_VF_STATS
  RUN_TEST(vf_submit_test);
  RUN_TEST(vf_info_attr_cb_test);
#endif
  END_TEST;
}
