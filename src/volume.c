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

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include <lvm2app.h>

static void volume_submit(const char *vol_name, gauge_t size, gauge_t free)
{
    value_t values[2];
    value_list_t vl = VALUE_LIST_INIT;

    values[0].gauge = size;
    values[1].gauge = free;

    vl.values = values;
    vl.values_len = STATIC_ARRAY_SIZE (values);
    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "volume", sizeof (vl.plugin));
    sstrncpy(vl.type, vol_name, sizeof (vl.type));

    plugin_dispatch_values (&vl);
}

static int volume_read(void)
{
    lvm_t lvm;
    vg_t vg;
    struct dm_list *vgnames;
    struct lvm_str_list *strl;

    lvm = lvm_init(NULL);
    if (!lvm) {
        fprintf(stderr, "lvm_init() failed\n");
    }

    vgnames = lvm_list_vg_names(lvm);
    if (!vgnames) {
        fprintf(stderr, "lvm_list_vg_names() failed\n");
    }

    dm_list_iterate_items(strl, vgnames) {
        vg = lvm_vg_open(lvm, strl->str, "r", 0);
        volume_submit(strl->str, lvm_vg_get_size(vg), lvm_vg_get_free_size(vg));
        lvm_vg_close(vg);
    }

    lvm_quit(lvm);
    return (0);
}

void module_register(void)
{
	plugin_register_read("volume", volume_read);
} /* void module_register */
