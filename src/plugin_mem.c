/**
 * collectd - src/plugin_memwatch.c - simple one off for grabbing memory usage of plugins
 * Copyright (C) 2013       John (J5) Palmieir
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *   John (J5) Palmieri <j5 at stackdriver.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_llist.h"

#include <string.h>
#include <stdlib.h>
#include <linux/limits.h>

static const char *config_keys[] = {
    "PluginDir",
    "PidFile"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static char *plugin_dir = NULL;
static char *collectd_pid_file = NULL;

static int pm_config (const char *key, const char *value)
{
    if (strcasecmp("PluginDir", key) == 0) {
        size_t dir_len;
        sfree (plugin_dir);
        plugin_dir = strdup(value);
        if (plugin_dir == NULL) {
            ERROR ("plugin mem plugin: OOM");
            return (-1);
        }

        dir_len = strlen (plugin_dir);
        /* add a slash to the end if one is not present */
        if (plugin_dir[dir_len - 1] != '/') {
            plugin_dir = realloc (plugin_dir, dir_len + 1);
            if (plugin_dir == NULL) {
                ERROR ("plugin mem plugin: OOM");
                return (-1);
            }
            plugin_dir[dir_len + 1] = '\0';
            plugin_dir[dir_len] = '/';
        }
    }
    else if (strcasecmp("PidFile", key) == 0) {
        sfree (collectd_pid_file);
        collectd_pid_file = strdup(value);
        if (collectd_pid_file == NULL) {
            ERROR ("plugin mem plugin: OOM");
            return (-1);
        }
    }
    else
    {
        ERROR ("plugin mem plugin: Unknown config option: %s", key);
        return (-1);
    }

    return (0);
}

static void submit (const char *type, const char *instance,
        value_t *values, size_t values_len)
{
    value_list_t v = VALUE_LIST_INIT;

    v.values = values;
    v.values_len = values_len;

    sstrncpy (v.host, hostname_g, sizeof(v.host));
    sstrncpy (v.plugin, "plugin_mem", sizeof(v.plugin));
    sstrncpy (v.type, type, sizeof(v.type));

    if (instance != NULL)
        sstrncpy (v.type_instance, instance, sizeof (v.type_instance));

    plugin_dispatch_values (&v);
}

static void submit_measurments (llist_t *mem_list) {
    llentry_t *e_this;
    llentry_t *e_next;

    if (mem_list == NULL)
        return;

    for (e_this = llist_head(mem_list); e_this != NULL; e_this = e_next)
    {
        value_t v;

        e_next = e_this->next;
        v.gauge = (size_t) e_this->value;
        submit ("gauge", e_this->key, &v, 1);
    }

}

static void free_measurements (llist_t *mem_list) {
    llentry_t *e_this;
    llentry_t *e_next;

    if (mem_list == NULL)
        return;

    for (e_this = llist_head(mem_list); e_this != NULL; e_this = e_next)
    {
        e_next = e_this->next;
        sfree (e_this->key);
        llentry_destroy (e_this);
    }

    free (mem_list);
}

static int pm_read (void) {
    FILE *pid_f;
    FILE *maps_f;
    char maps_path[PATH_MAX];
    char maps_line[PATH_MAX];
    int pid;
    int plugin_dir_len = strlen(plugin_dir);
    llist_t *mem_list = llist_create();

    if( (pid_f = fopen(collectd_pid_file, "r")) == NULL) {
        ERROR ("plugin mem plugin: pid file %s can not be opened", collectd_pid_file);
        return (-1);
    }

    if (fscanf(pid_f, "%d", &pid) != 1) {
        ERROR ("plugin mem plugin: error reading pid from file %s", collectd_pid_file);
        return (-1);
    }

    fclose (pid_f);

    sprintf(maps_path, "/proc/%d/maps", pid);

    if( (maps_f = fopen(maps_path, "r")) == NULL) {
        ERROR ("plugin mem plugin: maps file %s can not be opened", maps_path);
        return (-1);
    }

    while (fgets (maps_line, PATH_MAX, maps_f) != NULL) {
        unsigned long start;
        unsigned long end;
        char r, w, e, p;
        unsigned long long offset;
        unsigned int dev_maj;
        unsigned int dev_min;
        unsigned long inode;
        char path[PATH_MAX];

        if (sscanf (maps_line, "%16lx-%16lx %c%c%c%c %08llx %02x:%02x %lu %s", &start, &end, &r, &w, &e, &p, &offset, &dev_maj, &dev_min, &inode, path) == 11) {
            if (strncmp (plugin_dir, path, plugin_dir_len) == 0) {
                char *plugin = path + plugin_dir_len;
                llentry_t *entry = llist_search (mem_list, plugin);
                if (entry == NULL) {
                    char *plugin_cpy;
                    plugin_cpy = strdup(plugin);
                    if ( plugin_cpy == NULL) {
                        ERROR ("plugin mem plugin: OOM");
                        return (-1);
                    }

                    entry = llentry_create (plugin_cpy, (void *)0);
                    llist_append (mem_list, entry);
                }
                entry->value += end - start;
            }
        }
    }

    submit_measurments (mem_list);
    free_measurements (mem_list);
    return (0);
}

static int pm_shutdown (void)
{
    sfree (plugin_dir);
    sfree (collectd_pid_file);

    return (0);
}

void module_register (void)
{
    plugin_register_config ("plugin_mem", pm_config,
                            config_keys, config_keys_num);

    plugin_register_read ("plugin_mem", pm_read);
    plugin_register_shutdown ("plugin_mem", pm_shutdown);
}
/* vim: set sw=4 sts=4 et fdm=marker : */
