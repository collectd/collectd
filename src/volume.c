/**
 * collectd - src/volume.c
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

static void vg_submit(const char *vg_name, gauge_t used, gauge_t free, gauge_t size)
{
    value_t values[3];
    value_list_t vl = VALUE_LIST_INIT;

    values[0].gauge = used;
    values[1].gauge = free;
    values[2].gauge = size;

    vl.values = values;
    vl.values_len = STATIC_ARRAY_SIZE (values);

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "vol_group", sizeof (vl.plugin));
    sstrncpy(vl.plugin_instance, vg_name, sizeof (vl.plugin_instance));
    sstrncpy(vl.type, "vol_group", sizeof (vl.type));
    sstrncpy(vl.type_instance, vg_name, sizeof (vl.type_instance));

    plugin_dispatch_values (&vl);
}

static void lv_submit(const char *vg_name, const char *lv_name, gauge_t value)
{

    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;

    values[0].gauge = value;

    vl.values = values;
    vl.values_len = STATIC_ARRAY_SIZE (values);

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "vol_group", sizeof (vl.plugin));
    sstrncpy(vl.plugin_instance, vg_name, sizeof (vl.plugin_instance));
    sstrncpy(vl.type, "logical_vol", sizeof (vl.type));
    sstrncpy(vl.type_instance, lv_name, sizeof (vl.type_instance));

    plugin_dispatch_values (&vl); 
}

static int lv_read(vg_t vg, const char *vg_name, unsigned long vg_free, unsigned long vg_size)
{
    struct dm_list *lvs;
    struct lvm_lv_list *lvl;
    unsigned long vg_used = 0;

    vg_used = vg_size - vg_free;
    lvs = lvm_vg_list_lvs(vg);

    vg_submit(vg_name, vg_used, vg_free, vg_size);

    dm_list_iterate_items(lvl, lvs) {
         lv_submit(vg_name, lvm_lv_get_name(lvl->lv), lvm_lv_get_size(lvl->lv));
    }
    return (0);
}

static int vg_read(void)
{
    lvm_t lvm;
    vg_t vg;
    struct dm_list *vg_names;
    struct lvm_str_list *name_list;

    lvm = lvm_init(NULL);
    if (!lvm) {
    	ERROR("volume plugin: lvm_init failed: %s", lvm_errmsg(lvm));
        lvm_quit(lvm);
        exit(-1);
    }

    vg_names = lvm_list_vg_names(lvm);
    if (!vg_names) {
    	ERROR("volume plugin lvm_list_vg_name failed %s", lvm_errmsg(lvm));
        lvm_quit(lvm);
        exit(-1);
    }

    dm_list_iterate_items(name_list, vg_names) {
        vg = lvm_vg_open(lvm, name_list->str, "r", 0);
        lv_read(vg, name_list->str, lvm_vg_get_free_size(vg), lvm_vg_get_size(vg));

        lvm_vg_close(vg);
    }

    lvm_quit(lvm);
    return (0);
} /*vg_read */

void module_register(void)
{
	plugin_register_read("volume", vg_read);
} /* void module_register */
