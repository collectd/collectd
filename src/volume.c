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

static void volume_submit(const char *vol_name, const char *type, const char type_instance, gauge_t value)
{
    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;

    values[0].gauge = value;

    vl.values = values;
    vl.values_len = STATIC_ARRAY_SIZE (values);
    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.type, type, sizeof (vl.type));
    sstrncpy(vl.type_instance, type_instacne, sizeof (vl.type_instance));

    plugin_dispatch_values (&vl);
}

static int volume_read(void)
{
    lvm_t lvm;
    vg_t vg;
    int status = 0;
    struct dm_list *vg_names;
    struct lvm_str_list *name_list;

    lvm = lvm_init(NULL);
    if (!lvm) {
    	status = lvm_errno(lvm);
    	ERROR("volume plugin: lvm_init failed: %s", lvm_errmsg(lvm));
    }

    vg_names = lvm_list_vg_names(lvm);
    if (!vg_names) {
    	status = lvm_errno(lvm);
    	ERROR("volume plugin lvm_list_vg_name failed %s", lvm_errmsg(lvm));
    }

    dm_list_iterate_items(name_list, vg_names) {
        vg = lvm_vg_open(lvm, name_list->str, "r", 0);
        volume_submit(name_list->str, "df_complex", "size", lvm_vg_get_size(vg));
        volume_submit(name_list->str, "df_complex", "free", lvm_vg_get_free_size(vg));

        lvm_vg_close(vg);
    }

    lvm_quit(lvm);
    return (0);
}

void module_register(void)
{
	plugin_register_read("volume", volume_read);
} /* void module_register */
