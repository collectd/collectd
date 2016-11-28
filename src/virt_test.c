/**
 * collectd-ovirt/virt2.c
 * Copyright (C) 2016 Francesco Romani <fromani at redhat.com>
 * Based on
 * collectd - src/ceph_test.c
 * Copyright (C) 2015      Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "virt_test.h"

#include "testing.h"
#include "virt.c" /* sic */

#include <unistd.h>

enum {
  DATA_MAX_LEN = 4096,
};

static const char minimal_xml[] =
    ""
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<domain type=\"kvm\" xmlns:ovirt=\"http://ovirt.org/vm/tune/1.0\">"
    "  <metadata/>"
    "</domain>";

static const char minimal_metadata_xml[] =
    ""
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<domain type=\"kvm\" xmlns:ovirt=\"http://ovirt.org/vm/tune/1.0\">"
    "  <metadata>"
    "    <ovirtmap:tag "
    "xmlns:ovirtmap=\"http://ovirt.org/ovirtmap/tag/1.0\">virt-0</ovirtmap:tag>"
    "  </metadata>"
    "</domain>";

/* TODO: vminfo unit tests */

#define TAG "virt-0"
DEF_TEST(virt2_domain_get_tag_null_xml) {
  virt2_domain_t vdom;
  memset(&vdom, 0, sizeof(vdom));
  sstrncpy(vdom.uuid, "testing", sizeof(vdom.uuid));

  int err = virt2_domain_get_tag(&vdom, NULL);
  EXPECT_EQ_INT(-1, err);

  return 0;
}

DEF_TEST(virt2_domain_get_tag_empty_xml) {
  virt2_domain_t vdom;
  memset(&vdom, 0, sizeof(vdom));
  sstrncpy(vdom.uuid, "testing", sizeof(vdom.uuid));

  int err = virt2_domain_get_tag(&vdom, "");
  EXPECT_EQ_INT(-1, err);

  return 0;
}

DEF_TEST(virt2_domain_get_tag_no_metadata_xml) {
  virt2_domain_t vdom;
  memset(&vdom, 0, sizeof(vdom));
  sstrncpy(vdom.uuid, "testing", sizeof(vdom.uuid));

  int err = virt2_domain_get_tag(&vdom, minimal_xml);

  EXPECT_EQ_INT(0, err);
  EXPECT_EQ_STR("", vdom.tag);

  return 0;
}

DEF_TEST(virt2_domain_get_tag_valid_xml) {
  virt2_domain_t vdom;
  memset(&vdom, 0, sizeof(vdom));
  sstrncpy(vdom.uuid, "testing", sizeof(vdom.uuid));

  int err = virt2_domain_get_tag(&vdom, minimal_metadata_xml);

  EXPECT_EQ_INT(0, err);
  EXPECT_EQ_STR(TAG, vdom.tag);

  return 0;
}

DEF_TEST(virt_default_instance_include_domain_without_tag) {
  int ret;
  virt2_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.conf.debug_partitioning = 1;
  ctx.state.instances = 4; // random "low" number

  ret = virt2_setup(&ctx);
  EXPECT_EQ_INT(0, ret);

  virt2_domain_t vdom;
  memset(&vdom, 0, sizeof(vdom));
  sstrncpy(vdom.uuid, "testing", sizeof(vdom.uuid));

  virt2_instance_t *inst = &(ctx.user_data[0].inst);
  EXPECT_EQ_STR("virt-0", inst->tag);
  ret = virt2_instance_include_domain(&vdom, inst);
  EXPECT_EQ_INT(1, ret);

  inst = &(ctx.user_data[1].inst);
  EXPECT_EQ_STR("virt-1", inst->tag);
  ret = virt2_instance_include_domain(&vdom, inst);
  EXPECT_EQ_INT(0, ret);

  ret = virt2_teardown(&ctx);
  EXPECT_EQ_INT(0, ret);
  return 0;
}

DEF_TEST(virt_regular_instance_skip_domain_without_tag) {
  int ret;
  virt2_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.conf.debug_partitioning = 1;
  ctx.state.instances = 4; // random "low" number > 1

  ret = virt2_setup(&ctx);
  EXPECT_EQ_INT(0, ret);

  virt2_domain_t vdom;
  memset(&vdom, 0, sizeof(vdom));
  sstrncpy(vdom.uuid, "testing", sizeof(vdom.uuid));

  virt2_instance_t *inst = &(ctx.user_data[1].inst);
  EXPECT_EQ_STR("virt-1", inst->tag);
  ret = virt2_instance_include_domain(&vdom, inst);
  EXPECT_EQ_INT(0, ret);

  ret = virt2_teardown(&ctx);
  EXPECT_EQ_INT(0, ret);
  return 0;
}

DEF_TEST(virt_default_instance_include_domain_with_unknown_tag) {
  int ret;
  virt2_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.conf.debug_partitioning = 1;
  ctx.state.instances = 4; // random "low" number

  ret = virt2_setup(&ctx);
  EXPECT_EQ_INT(0, ret);

  virt2_domain_t vdom;
  memset(&vdom, 0, sizeof(vdom));
  sstrncpy(vdom.uuid, "testing", sizeof(vdom.uuid));
  sstrncpy(vdom.tag, "UnknownFormatTag", sizeof(vdom.tag));

  virt2_instance_t *inst = &(ctx.user_data[0].inst);
  EXPECT_EQ_STR("virt-0", inst->tag);
  ret = virt2_instance_include_domain(&vdom, inst);
  EXPECT_EQ_INT(1, ret);

  ret = virt2_teardown(&ctx);
  EXPECT_EQ_INT(0, ret);
  return 0;
}

DEF_TEST(virt_regular_instance_skip_domain_with_unknown_tag) {
  int ret;
  virt2_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.conf.debug_partitioning = 1;
  ctx.state.instances = 4; // random "low" number > 1

  ret = virt2_setup(&ctx);
  EXPECT_EQ_INT(0, ret);

  virt2_domain_t vdom;
  memset(&vdom, 0, sizeof(vdom));
  sstrncpy(vdom.uuid, "testing", sizeof(vdom.uuid));
  sstrncpy(vdom.tag, "UnknownFormatTag", sizeof(vdom.tag));

  virt2_instance_t *inst = &(ctx.user_data[1].inst);
  EXPECT_EQ_STR("virt-1", inst->tag);
  ret = virt2_instance_include_domain(&vdom, inst);
  EXPECT_EQ_INT(0, ret);

  ret = virt2_teardown(&ctx);
  EXPECT_EQ_INT(0, ret);
  return 0;
}

DEF_TEST(virt_include_domain_matching_tags) {
  int ret;
  virt2_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.conf.debug_partitioning = 1;
  ctx.state.instances = 4; // random "low" number

  ret = virt2_setup(&ctx);
  EXPECT_EQ_INT(0, ret);

  virt2_domain_t vdom;
  memset(&vdom, 0, sizeof(vdom));
  sstrncpy(vdom.uuid, "testing", sizeof(vdom.uuid));
  sstrncpy(vdom.tag, "virt-0", sizeof(vdom.tag));

  virt2_instance_t *inst = &(ctx.user_data[0].inst);
  EXPECT_EQ_STR("virt-0", inst->tag);

  ret = virt2_instance_include_domain(&vdom, inst);
  EXPECT_EQ_INT(1, ret);
  ret = virt2_teardown(&ctx);
  EXPECT_EQ_INT(0, ret);
  return 0;
}

DEF_TEST(virt2_partition_domains_none) {
  int ret;
  virt2_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.conf.debug_partitioning = 1;
  ctx.state.instances = 4; // random "low" number

  ret = virt2_setup(&ctx);
  EXPECT_EQ_INT(0, ret);

  virt2_instance_t *inst = &(ctx.user_data[0].inst);
  EXPECT_EQ_STR("virt-0", inst->tag);

  inst->domains_num = 0;

  GArray *part = virt2_partition_domains(inst);
  EXPECT_EQ_INT(0, part->len);
  g_array_free(part, TRUE);

  ret = virt2_teardown(&ctx);
  EXPECT_EQ_INT(0, ret);
  return 0;
}

/* we are cheating with pointers anyway, so I'm intentionally using void * */
static void *alloc_domain(const char *name, const char *uuid,
                          const char *xml_data) {
  fakeVirDomainPtr dom = calloc(1, sizeof(struct fakeVirDomain));
  dom->name = strdup(name);
  strncpy(dom->uuid, "testing", sizeof(dom->uuid));
  dom->xml = strdup(xml_data);
  return dom;
}

static void free_domain(void *_dom) {
  fakeVirDomainPtr dom = _dom;
  free(dom->name);
  free(dom->xml);
  free(dom);
}

DEF_TEST(virt2_partition_domains_one_untagged) {
  int ret;
  virt2_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.conf.debug_partitioning = 1;
  ctx.state.instances = 4; // random "low" number

  ret = virt2_setup(&ctx);
  EXPECT_EQ_INT(0, ret);

  virt2_instance_t *inst = &(ctx.user_data[0].inst);
  EXPECT_EQ_STR("virt-0", inst->tag);

  inst->domains_num = 1;
  inst->domains_all = calloc(1, sizeof(virDomainPtr));
  inst->domains_all[0] = alloc_domain("test", "testing", minimal_xml);

  GArray *part = virt2_partition_domains(inst);
  EXPECT_EQ_INT(1, part->len);

  void *_dom = g_array_index(part, virDomainPtr, 0);
  fakeVirDomainPtr fake_dom = _dom;
  EXPECT_EQ_STR("testing", fake_dom->uuid);

  g_array_free(part, TRUE);

  free_domain(inst->domains_all[0]);
  free(inst->domains_all);

  ret = virt2_teardown(&ctx);
  EXPECT_EQ_INT(0, ret);
  return 0;
}

DEF_TEST(virt2_partition_domains_one_untagged_unpicked) {
  int ret;
  virt2_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.conf.debug_partitioning = 1;
  ctx.state.instances = 4; // random "low" number

  ret = virt2_setup(&ctx);
  EXPECT_EQ_INT(0, ret);

  virt2_instance_t *inst = &(ctx.user_data[1].inst);
  EXPECT_EQ_STR("virt-1", inst->tag);

  inst->domains_num = 1;
  inst->domains_all = calloc(1, sizeof(virDomainPtr));
  inst->domains_all[0] = alloc_domain("test", "testing", minimal_xml);

  GArray *part = virt2_partition_domains(inst);
  EXPECT_EQ_INT(0, part->len);
  g_array_free(part, TRUE);

  free_domain(inst->domains_all[0]);
  free(inst->domains_all);

  ret = virt2_teardown(&ctx);
  EXPECT_EQ_INT(0, ret);
  return 0;
}

#undef TAG

int main(void) {
  RUN_TEST(virt2_domain_get_tag_null_xml);
  RUN_TEST(virt2_domain_get_tag_empty_xml);
  RUN_TEST(virt2_domain_get_tag_no_metadata_xml);
  RUN_TEST(virt2_domain_get_tag_valid_xml);

  RUN_TEST(virt_include_domain_matching_tags);
  RUN_TEST(virt_default_instance_include_domain_without_tag);
  RUN_TEST(virt_regular_instance_skip_domain_without_tag);
  RUN_TEST(virt_default_instance_include_domain_with_unknown_tag);
  RUN_TEST(virt_regular_instance_skip_domain_with_unknown_tag);

  RUN_TEST(virt2_partition_domains_none);
  RUN_TEST(virt2_partition_domains_one_untagged);
  RUN_TEST(virt2_partition_domains_one_untagged_unpicked);

  END_TEST;
}

/* vim: set sw=2 sts=2 et : */
