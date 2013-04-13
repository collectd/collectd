/**
 * collectd - src/lvm.c
 * Copyright (C) 2013       Chad Malfait
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
 **/

#include <lvm2app.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"

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

static int vg_read(vg_t vg, char const *vg_name)
{
    struct dm_list *lvs;
    struct lvm_lv_list *lvl;

    lvm_submit (vg_name, "free", lvm_vg_get_free_size(vg));

    lvs = lvm_vg_list_lvs(vg);
    dm_list_iterate_items(lvl, lvs) {
         lvm_submit(vg_name, lvm_lv_get_name(lvl->lv), lvm_lv_get_size(lvl->lv));
    }

    return (0);
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
