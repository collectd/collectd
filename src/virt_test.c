/**
 * collectd - src/virt_test.c
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

#include "virt.c" /* sic */
#include "testing.h"

#include <unistd.h>

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

struct xml_state {
  xmlDocPtr xml_doc;
  xmlXPathContextPtr xpath_ctx;
  xmlXPathObjectPtr xpath_obj;
  char tag[PARTITION_TAG_MAX_LEN];
};

static int init_state(struct xml_state *st, const char *xml) {
  memset(st, 0, sizeof(*st));

  st->xml_doc = xmlReadDoc((const xmlChar *)xml, NULL, NULL, XML_PARSE_NONET);
  if (st->xml_doc == NULL) {
    return -1;
  }
  st->xpath_ctx = xmlXPathNewContext(st->xml_doc);
  if (st->xpath_ctx == NULL) {
    return -1;
  }
  return 0;
}

static void fini_state(struct xml_state *st) {
  if (st->xpath_ctx) {
    xmlXPathFreeContext(st->xpath_ctx);
    st->xpath_ctx = NULL;
  }
  if (st->xml_doc) {
    xmlFreeDoc(st->xml_doc);
    st->xml_doc = NULL;
  }
}

#define TAG "virt-0"

DEF_TEST(lv_domain_get_tag_no_metadata_xml) {
  int err;
  struct xml_state st;
  err = init_state(&st, minimal_xml);
  EXPECT_EQ_INT(0, err);

  err = lv_domain_get_tag(st.xpath_ctx, "test", st.tag);

  EXPECT_EQ_INT(0, err);
  EXPECT_EQ_STR("", st.tag);

  fini_state(&st);
  return 0;
}

DEF_TEST(lv_domain_get_tag_valid_xml) {
  int err;
  struct xml_state st;
  err = init_state(&st, minimal_metadata_xml);
  EXPECT_EQ_INT(0, err);

  err = lv_domain_get_tag(st.xpath_ctx, "test", st.tag);

  EXPECT_EQ_INT(0, err);
  EXPECT_EQ_STR(TAG, st.tag);

  return 0;
}

DEF_TEST(lv_default_instance_include_domain_without_tag) {
  struct lv_read_instance *inst = NULL;
  int ret;

  ret = lv_init_instance(0, lv_read);
  inst = &(lv_read_user_data[0].inst);
  EXPECT_EQ_STR("virt-0", inst->tag);

  ret = lv_instance_include_domain(inst, "testing", "");
  EXPECT_EQ_INT(1, ret);

  lv_fini_instance(0);
  return 0;
}

DEF_TEST(lv_regular_instance_skip_domain_without_tag) {
  struct lv_read_instance *inst = NULL;
  int ret;

  ret = lv_init_instance(1, lv_read);
  inst = &(lv_read_user_data[1].inst);
  EXPECT_EQ_STR("virt-1", inst->tag);

  ret = lv_instance_include_domain(inst, "testing", "");
  EXPECT_EQ_INT(0, ret);

  lv_fini_instance(0);
  return 0;
}

DEF_TEST(lv_include_domain_matching_tags) {
  struct lv_read_instance *inst = NULL;
  int ret;

  ret = lv_init_instance(0, lv_read);
  inst = &(lv_read_user_data[0].inst);
  EXPECT_EQ_STR("virt-0", inst->tag);

  ret = lv_instance_include_domain(inst, "testing", "virt-0");
  EXPECT_EQ_INT(1, ret);

  ret = lv_init_instance(1, lv_read);
  inst = &(lv_read_user_data[1].inst);
  EXPECT_EQ_STR("virt-1", inst->tag);

  ret = lv_instance_include_domain(inst, "testing", "virt-1");
  EXPECT_EQ_INT(1, ret);

  lv_fini_instance(0);
  lv_fini_instance(1);
  return 0;
}

DEF_TEST(lv_default_instance_include_domain_with_unknown_tag) {
  struct lv_read_instance *inst = NULL;
  int ret;

  ret = lv_init_instance(0, lv_read);
  inst = &(lv_read_user_data[0].inst);
  EXPECT_EQ_STR("virt-0", inst->tag);

  ret = lv_instance_include_domain(inst, "testing", "unknownFormat-tag");
  EXPECT_EQ_INT(1, ret);

  lv_fini_instance(0);
  return 0;
}

DEF_TEST(lv_regular_instance_skip_domain_with_unknown_tag) {
  struct lv_read_instance *inst = NULL;
  int ret;

  ret = lv_init_instance(1, lv_read);
  inst = &(lv_read_user_data[1].inst);
  EXPECT_EQ_STR("virt-1", inst->tag);

  ret = lv_instance_include_domain(inst, "testing", "unknownFormat-tag");
  EXPECT_EQ_INT(0, ret);

  lv_fini_instance(0);
  return 0;
}
#undef TAG

int main(void) {
  RUN_TEST(lv_domain_get_tag_no_metadata_xml);
  RUN_TEST(lv_domain_get_tag_valid_xml);

  RUN_TEST(lv_default_instance_include_domain_without_tag);
  RUN_TEST(lv_regular_instance_skip_domain_without_tag);
  RUN_TEST(lv_include_domain_matching_tags);
  RUN_TEST(lv_default_instance_include_domain_with_unknown_tag);
  RUN_TEST(lv_regular_instance_skip_domain_with_unknown_tag);

  END_TEST;
}

/* vim: set sw=2 sts=2 et : */
