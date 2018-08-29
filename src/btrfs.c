/**
 * collectd - src/btrfs.c
 * Copyright (C) 2005-2009  Florian octo Forster
 * Copyright (C) 2009       Paul Sadauskas
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
 *   Paul Sadauskas <psadauskas at gmail.com>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"
#include "utils_mount.h"

#include <btrfs/ioctl.h>


#define PLUGIN_NAME "btrfs"

// see
// https://github.com/kdave/btrfs-progs/blob/master/cmds-device.c

//static int btrfs_dispatch

static int btrfs_read(void) {

// vars
    //derive_t counter = 10000;
    gauge_t gauge = 10;

// value
    value_t value = {
//        .derive = counter,
        .gauge = gauge
    };




    //struct      btrfs_ioctl_get_dev_stats args = {0};
    char        path[BTRFS_DEVICE_PATH_NAME_MAX + 1];

    strncpy( path, "/dev/sda3", BTRFS_DEVICE_PATH_NAME_MAX );
    path[BTRFS_DEVICE_PATH_NAME_MAX] = 0;

    //args.devid = "61ca4c37-dd9c-475c-a673-b6ef5df03603";
    //args.nr_items = BTRFS_DEV_STAT_VALUES_MAX;
    //args.flags = 0;







    value_list_t vl = VALUE_LIST_INIT;

    vl.values = &value;
    vl.values_len = 1;

    //sstrncpy(vl.host, hostname_g, sizeof(vl.host));
    sstrncpy(vl.plugin, PLUGIN_NAME, sizeof(vl.plugin));
    sstrncpy(vl.plugin_instance, "my_plugin_instance", sizeof(vl.plugin_instance));
    sstrncpy(vl.type, "count", sizeof(vl.type));

    plugin_dispatch_values(&vl);


 return 0;
}

void module_register(void) {

    plugin_register_read( "btrfs", btrfs_read );

}




