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
#define DEFMAXRPTSIZE (8UL << 10)

// Timestamp of latest report
static cdtime_t t = 0;

static void lvm_submit(const char *plug, char const *pi, // {{{
                       char const *dt, char const *ti, gauge_t val) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = val};
  vl.values_len = 1;
  vl.time = t;
  // don't set interval or host
  sstrncpy(vl.plugin, plug, sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, pi, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, dt, sizeof(vl.type));
  sstrncpy(vl.type_instance, ti, sizeof(vl.type_instance));
  // leave meta as NULL

  plugin_dispatch_values(&vl);
} // lvm_submit() }}}

static void inline lvm_vg_submit(const char *vg_name, // {{{
                                 const char *lv_name, double size) {
  lvm_submit("lvm_vg", vg_name, "df_complex", lv_name, size);
} // lvm_vg_submit() }}}

static void lvm_snap_submit(const char *vg_name, const char *lv_name, // {{{
                            double pct_used) {
  char *pi = ssnprintf_alloc("%s_%s", vg_name, lv_name);
  lvm_submit("lvm_snap", pi, "percent_bytes", "used", pct_used);
  lvm_submit("lvm_snap", pi, "percent_bytes", "free", 100.0 - pct_used);
  sfree(pi);
} // lvm_snap_submit() }}}

static void lvm_thinp_submit(const char *vg_name, const char *lv_name, // {{{
                             double pct_data_used, double pct_meta_used) {
  char *pi = ssnprintf_alloc("%s_%s", vg_name, lv_name);
  lvm_submit("lvm_thinp.data", pi, "percent_bytes", "used", pct_data_used);
  lvm_submit("lvm_thinp.data", pi, "percent_bytes", "free",
             100.0 - pct_data_used);
  lvm_submit("lvm_thinp.meta", pi, "percent_bytes", "used", pct_meta_used);
  lvm_submit("lvm_thinp.meta", pi, "percent_bytes", "free",
             100.0 - pct_meta_used);
  sfree(pi);
} // lvm_thinp_submit() }}}

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

  const char *rpts_path[] = {"report", (const char *)0};
  yajl_val rpts = yajl_tree_get(json, rpts_path, yajl_t_array);
  if (!rpts || !YAJL_IS_ARRAY(rpts)) {
    P_WARNING("didn't find any lvm reports in the JSON!");
    return -1;
  }

  for (int rpt_num = 0; rpt_num < rpts->u.array.len; ++rpt_num) {
    yajl_val lvm_rpt = rpts->u.array.values[rpt_num];
    if (!lvm_rpt || !YAJL_IS_OBJECT(lvm_rpt)) {
      P_ERROR("invalid lvm rpt #%d!", rpt_num);
      continue;
    }

    // process the VGs {{{
    const char *vg_path[] = {"vg", (const char *)0};
    yajl_val vgs = yajl_tree_get(lvm_rpt, vg_path, yajl_t_array);
    if (!(vgs && YAJL_IS_ARRAY(vgs))) {
      P_NOTICE("didn't find any VGs in rpt #%d.", rpt_num);
      continue;
    }
    for (int i = 0; i < vgs->u.array.len; ++i) {
      yajl_val vg = vgs->u.array.values[i];
      if (!vg || !YAJL_IS_OBJECT(vg)) {
        P_ERROR("invalid VG #%d in rpt #%d!", i, rpt_num);
        continue;
      }

      char *vg_name = get_json_string(vg, "vg_name");
      char *vg_free_str = get_json_string(vg, "vg_free");
      if (!(vg_name && vg_free_str)) {
        // get_json_string() would have emitted an error message
        continue;
      }
      long long int vg_free = atoll(vg_free_str);
      lvm_vg_submit(vg_name, "free", vg_free);
    } // foreach VG }}}

    // process the LVs {{{
    const char *lv_path[] = {"lv", (const char *)0};
    yajl_val lvs = yajl_tree_get(lvm_rpt, lv_path, yajl_t_array);
    if (!(lvs && YAJL_IS_ARRAY(lvs))) {
      P_NOTICE("didn't find any LVs in rpt #%d.", rpt_num);
      continue;
    }
    for (int i = 0; i < lvs->u.array.len; ++i) {
      yajl_val lv = lvs->u.array.values[i];
      if (!lv || !YAJL_IS_OBJECT(lv)) {
        P_ERROR("invalid LV #%d in rpt #%d!", i, rpt_num);
        continue;
      }

      char *lv_attr = get_json_string(lv, "lv_attr");
      if (!lv_attr)
        continue; // get_json_string() would have emitted an error message

      // Skip virtual/thin LVs that don't use actual space in the VG
      if ('v' == lv_attr[0] || 'V' == lv_attr[0])
        continue;

      // Report usage within thin pools
      if ('t' == lv_attr[0]) {
        char *lv_name = get_json_string(lv, "lv_name");
        char *vg_name = get_json_string(lv, "vg_name");
        char *data_pct_str = get_json_string(lv, "data_percent");
        char *meta_pct_str = get_json_string(lv, "metadata_percent");
        if (!(vg_name && lv_name && data_pct_str && meta_pct_str))
          continue; // get_json_string() would have emitted an error message
        double data_pct = atof(data_pct_str);
        double meta_pct = atof(meta_pct_str);
        lvm_thinp_submit(vg_name, lv_name, data_pct, meta_pct);
        continue; // skip reporting space allocated in the VG to the thin pool
                  // as it is counted in the underlying meta/data LVs
      }

      // Submit the size of the LV as used in the VG
      char *lv_name = get_json_string(lv, "lv_name");
      char *vg_name = get_json_string(lv, "vg_name");
      char *lv_size_str = get_json_string(lv, "lv_size");
      if (!(vg_name && lv_name && lv_size_str))
        continue; // get_json_string() would have emitted an error message
      if (lv_name[0] == '[' && lv_name[strlen(lv_name) - 1] == ']') {
        // remove [] brackets around names of hidden LVs (eg. thin meta/data)
        lv_name[strlen(lv_name) - 1] = '\0';
        lv_name++;
      }
      long long int lv_size = atoll(lv_size_str);
      lvm_vg_submit(vg_name, lv_name, lv_size);

      // Additionally, if it's a snapshot, submit space usage within it
      if ('s' == lv_attr[0] || 'S' == lv_attr[0]) {
        char *lv_datap_str = get_json_string(lv, "data_percent");
        if (lv_datap_str) {
          double lv_datap = atof(lv_datap_str);
          lvm_snap_submit(vg_name, lv_name, lv_datap);
        }
      } // snapshot
    }   // foreach LV }}}
  }     // foreach report

  return 0;
} // lvm_process_report() }}}

static int lvm_get_report_json(yajl_val *json) // {{{
{
  char jsondata[DEFMAXRPTSIZE];

  *json = NULL; // default to error condition (gets set if we make it through)

  // Get the report from lvm into a buffer {{{
  FILE *fp;
  char *jsoncmd =
      "lvm fullreport --all --units=b --nosuffix --reportformat json"
      " --configreport vg -o vg_name,vg_free"
      " --configreport pv -S pv_uuid="
      " --configreport lv -o vg_name,lv_name,lv_size,lv_attr"
      ",data_percent,metadata_percent"
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
  jsondata[rd] = '\0'; // ensure that JSON is NUL-terminated
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

// vim: set ts=2 sw=2 et si fdm=marker :
