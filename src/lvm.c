/**
 * collectd - src/lvm.c
 * Copyright (C) 2020       Joseph Nahmias
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
 *   Joseph Nahmias <joe at nahmias.net>
 **/

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"

#include <yajl/yajl_tree.h>

#define P_DEBUG(...) DEBUG("plugin lvm: " __VA_ARGS__)

// Default maximum size of lvm report (8kb)
#define DEFMAXRPTSIZE (8 << 10)

// Timestamp of latest report
static cdtime_t t = 0;

static void lvm_submit(char const *pi, char const *ti, uint64_t val) // {{{
{
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = (gauge_t)val};
  vl.values_len = 1;
  vl.time = t;
  // don't set interval or host
  sstrncpy(vl.plugin, "lvm", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, pi, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "df_complex", sizeof(vl.type));
  sstrncpy(vl.type_instance, ti, sizeof(vl.type_instance));
  // leave meta as NULL

  plugin_dispatch_values(&vl);
} // lvm_submit() }}}

static char *get_json_string(yajl_val json, const char *key) // {{{
{
  const char *path[] = {NULL, NULL};
  path[0] = key;
  yajl_val json_val = yajl_tree_get(json, path, yajl_t_string);
  char *str = YAJL_GET_STRING(json_val);
  if (NULL == str)
    P_ERROR("get_json_string(): Error: couldn't find '%s' in the JSON data!",
            key);
  return str;
} // get_json_string() }}}

static int lvm_process_report(yajl_val json) // {{{
{
  yajl_val first_rpt;

  // find the first report in the JSON {{{
  const char *rpts_path[] = {"report", (const char *)0};
  yajl_val rpts = yajl_tree_get(json, rpts_path, yajl_t_array);
  if (!rpts || !YAJL_IS_ARRAY(rpts)) {
    P_ERROR("didn't find any lvm reports in the JSON!");
    return -1;
  }
  first_rpt = rpts->u.array.values[0];
  if (!first_rpt || !YAJL_IS_OBJECT(first_rpt)) {
    P_ERROR("didn't find any JSON content in first lvm report!");
    return -2;
  }
  // find the first report in the JSON }}}

  // process the VGs {{{
  const char *vg_path[] = {"vg", (const char *)0};
  yajl_val vgs = yajl_tree_get(first_rpt, vg_path, yajl_t_array);
  if (!(vgs && YAJL_IS_ARRAY(vgs))) {
    P_NOTICE("didn't find any VGs.");
    return 0;
  }
  for (int i = 0; i < vgs->u.array.len; ++i) {
    yajl_val vg = vgs->u.array.values[i];
    if (!vg || !YAJL_IS_OBJECT(vg)) {
      P_WARNING("invalid VG #%d!", i);
      continue;
    }

    char *vg_name = get_json_string(vg, "vg_name");
    char *vg_free_str = get_json_string(vg, "vg_free");
    if (!(vg_name && vg_free_str)) {
      // get_json_string() would have emitted an error message
      continue;
    }
    long long int vg_free = atoll(vg_free_str);
    lvm_submit(vg_name, "free", vg_free);
  }
  // process the VGs }}}

  // process the LVs {{{
  const char *lv_path[] = {"lv", (const char *)0};
  yajl_val lvs = yajl_tree_get(first_rpt, lv_path, yajl_t_array);
  if (!(lvs && YAJL_IS_ARRAY(lvs))) {
    P_NOTICE("didn't find any LVs.");
    return 0;
  }
  for (int i = 0; i < lvs->u.array.len; ++i) {
    yajl_val lv = lvs->u.array.values[i];
    if (!lv || !YAJL_IS_OBJECT(lv)) {
      P_WARNING("invalid LV #%d!", i);
      continue;
    }

    char *lv_name = get_json_string(lv, "lv_name");
    char *vg_name = get_json_string(lv, "vg_name");
    char *lv_size_str = get_json_string(lv, "lv_size");
    char *lv_attr = get_json_string(lv, "lv_attr");
    if (!(vg_name && lv_name && lv_size_str && lv_attr)) {
      // get_json_string() would have emitted an error message
      continue;
    }
    long long int lv_size = atoll(lv_size_str);
    lvm_submit(vg_name, lv_name, lv_size);
  }
  // process the LVs }}}

  return 0;
} // lvm_process_report() }}}

static int lvm_get_report_json(yajl_val *json) // {{{
{
  char jsondata[DEFMAXRPTSIZE];

  *json = NULL; // default to error condition (gets set if we make it through)

  // Get the report from lvm into a buffer {{{
  FILE *fp;
  char *jsoncmd =
      "lvm fullreport --units=b --nosuffix --reportformat json"
      " --configreport vg -o vg_name,vg_size,vg_free"
      " --configreport pv -S pv_uuid="
      " --configreport lv -o vg_name,lv_name,lv_size,lv_attr"
      ",data_percent,data_lv,metadata_lv,lv_metadata_size,metadata_percent"
      " --configreport pvseg -S pv_uuid="
      " --configreport seg -S lv_uuid="
      " 2>/dev/null";
  size_t rd;
  jsondata[0] = '\0';
  if (NULL == (fp = popen(jsoncmd, "r"))) {
    P_ERROR("popen(): %s", STRERRNO);
    return -1;
  }
  rd = fread((void *)jsondata, 1, sizeof(jsondata) - 1, fp);
  if (ferror(fp) || 0 == rd) {
    P_ERROR("fread(): %s", STRERRNO);
    return -2;
  } else if (rd >= sizeof(jsondata) - 1) {
    P_ERROR("too much json data returned [>=%zu]!", sizeof(jsondata));
    return -2;
  }
  if (feof(fp))
    P_DEBUG("lvm_get_report_json(): reached EOF.");
  P_DEBUG("lvm_get_report_json(): read %zu bytes from lvm fullreport.", rd);
  int rc = pclose(fp);
  if (-1 == rc && ECHILD != errno) {
    P_ERROR("pclose(): %s", STRERRNO);
    return -3;
  } else if (-1 == rc && ECHILD == errno) {
    P_DEBUG("pclose(): unable to obtain status of lvm fullreport; assuming "
            "that all went okay.");
  } else {
    P_DEBUG("pclose(): lvm fullreport returned %d.", rc);
  }
  // Get the lvm report }}}

  // parse the report as JSON and return it {{{
  char errbuf[BUFSIZ];
  errbuf[0] = '\0';
  yajl_val node = NULL;
  node = yajl_tree_parse((const char *)jsondata, errbuf, sizeof(errbuf));
  if (NULL == node) {
    if (strlen(errbuf))
      P_ERROR("yajl_tree_parse(): %s", errbuf);
    else
      P_ERROR("yajl_tree_parse(): unknown error.");
    return -5;
  }
  *json = node; // send back the first report JSON
  return 0;
  // parse the JSON and return it }}}

} // lvm_get_report() }}}

static int lvm_read(void) // {{{
{
  int rc;
  yajl_val lvm_rpt;

  t = 0;
  if ((rc = lvm_get_report_json(&lvm_rpt)))
    // propogate error reported in lvm_get_report_json()
    return rc;
  t = cdtime(); // save time of lvm report so that all the metrics sent to
                // collectd from this report are consistent

  rc = lvm_process_report(lvm_rpt);
  yajl_tree_free(lvm_rpt);

  return rc;
} // lvm_read() }}}

void module_register(void) // {{{
{
  plugin_register_read("lvm", lvm_read);
} // module_register() }}}

// vim: set ts=4 sw=4 et si fdm=marker :
