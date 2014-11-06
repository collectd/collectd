/**
 * collectd - src/lvm.c
 * Copyright (C) 2013       Chad Malfait
 * Copyright (C) 2014       Carnegie Mellon University
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
 *   Chad Malfait <malfaitc at yahoo.com>
 *   Benjamin Gilbert <bgilbert at cs.cmu.edu>
 **/

#include <lvm2app.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#define NO_VALUE UINT64_MAX
#define PERCENT_SCALE_FACTOR 1e-8

static uint64_t get_lv_property_int(lv_t lv, char const *property)
{
    lvm_property_value_t v;

    v = lvm_lv_get_property(lv, property);
    if (!v.is_valid || !v.is_integer)
        return NO_VALUE;
    /* May be NO_VALUE if @property does not apply to this LV */
    return v.value.integer;
}

static char const *get_lv_property_string(lv_t lv, char const *property)
{
    lvm_property_value_t v;

    v = lvm_lv_get_property(lv, property);
    if (!v.is_valid || !v.is_string)
        return NULL;
    return v.value.string;
}

static void lvm_submit (char const *plugin_instance, char const *type_instance,
        uint64_t ivalue)
{
    value_t v;
    value_list_t vl = VALUE_LIST_INIT;

    v.gauge = (gauge_t) ivalue;

    vl.values = &v;
    vl.values_len = 1;

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "lvm", sizeof (vl.plugin));
    sstrncpy(vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));
    sstrncpy(vl.type, "df_complex", sizeof (vl.type));
    sstrncpy(vl.type_instance, type_instance, sizeof (vl.type_instance));

    plugin_dispatch_values (&vl);
}

static void report_lv_utilization(lv_t lv, char const *vg_name,
        char const *lv_name, uint64_t lv_size,
        char const *used_percent_property)
{
    uint64_t used_percent_unscaled;
    uint64_t used_bytes;
    char plugin_instance[DATA_MAX_NAME_LEN];

    used_percent_unscaled = get_lv_property_int(lv, used_percent_property);
    if (used_percent_unscaled == NO_VALUE)
        return;
    used_bytes = lv_size * (used_percent_unscaled * PERCENT_SCALE_FACTOR);

    ssnprintf(plugin_instance, sizeof(plugin_instance), "%s-%s",
            vg_name, lv_name);
    lvm_submit(plugin_instance, "used", used_bytes);
    lvm_submit(plugin_instance, "free", lv_size - used_bytes);
}

static void report_thin_pool_utilization(lv_t lv, char const *vg_name,
        uint64_t lv_size)
{
    char const *data_lv;
    char const *metadata_lv;
    uint64_t metadata_size;

    data_lv = get_lv_property_string(lv, "data_lv");
    metadata_lv = get_lv_property_string(lv, "metadata_lv");
    metadata_size = get_lv_property_int(lv, "lv_metadata_size");
    if (data_lv == NULL || metadata_lv == NULL || metadata_size == NO_VALUE)
        return;

    report_lv_utilization(lv, vg_name, data_lv, lv_size, "data_percent");
    report_lv_utilization(lv, vg_name, metadata_lv, metadata_size,
            "metadata_percent");
}

static void vg_read(vg_t vg, char const *vg_name)
{
    struct dm_list *lvs;
    struct lvm_lv_list *lvl;
    char const *name;
    char const *attrs;
    uint64_t size;

    lvm_submit (vg_name, "free", lvm_vg_get_free_size(vg));

    lvs = lvm_vg_list_lvs(vg);
    if (!lvs) {
        /* no VGs are defined, which is not an error per se */
        return;
    }

    dm_list_iterate_items(lvl, lvs) {
        name = lvm_lv_get_name(lvl->lv);
        attrs = get_lv_property_string(lvl->lv, "lv_attr");
        size = lvm_lv_get_size(lvl->lv);
        if (name == NULL || attrs == NULL || size == NO_VALUE)
            continue;

        /* Condition on volume type.  We want the reported sizes in the
           volume group to sum to the size of the volume group, so we ignore
           virtual volumes.  */
        switch (attrs[0]) {
            case 's':
            case 'S':
                /* Snapshot.  Also report used/free space. */
                report_lv_utilization(lvl->lv, vg_name, name, size,
                        "data_percent");
                break;
            case 't':
                /* Thin pool virtual volume.  We report the underlying data
                   and metadata volumes, not this one.  Report used/free
                   space, then ignore. */
                report_thin_pool_utilization(lvl->lv, vg_name, size);
                continue;
            case 'v':
                /* Virtual volume.  Ignore. */
                continue;
            case 'V':
                /* Thin volume or thin snapshot.  Ignore. */
                continue;
        }
        lvm_submit(vg_name, name, size);
    }
}

static int lvm_read(void)
{
    lvm_t lvm;
    struct dm_list *vg_names;
    struct lvm_str_list *name_list;

    lvm = lvm_init(NULL);
    if (!lvm) {
        ERROR("lvm plugin: lvm_init failed.");
        return (-1);
    }

    vg_names = lvm_list_vg_names(lvm);
    if (!vg_names) {
        ERROR("lvm plugin lvm_list_vg_name failed %s", lvm_errmsg(lvm));
        lvm_quit(lvm);
        return (-1);
    }

    dm_list_iterate_items(name_list, vg_names) {
        vg_t vg;

        vg = lvm_vg_open(lvm, name_list->str, "r", 0);
        if (!vg) {
            ERROR ("lvm plugin: lvm_vg_open (%s) failed: %s",
                    name_list->str, lvm_errmsg(lvm));
            continue;
        }

        vg_read(vg, name_list->str);
        lvm_vg_close(vg);
    }

    lvm_quit(lvm);
    return (0);
} /*lvm_read */

void module_register(void)
{
    plugin_register_read("lvm", lvm_read);
} /* void module_register */
