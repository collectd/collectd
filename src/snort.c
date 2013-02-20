/**
 * collectd - src/snort.c
 * Copyright (C) 2013 Kris Nielander
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
 *   Kris Nielander <nielander@fox-it.com>
 *
 * This plugin is based on the snmp plugin by Florian octo Forster.
 *
 **/

#include "collectd.h"
#include "plugin.h" /* plugin_register_*, plugin_dispatch_values */
#include "common.h" /* auxiliary functions */
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

struct metric_definition_s {
    char *name;
    char *type;
    int data_source_type;
    int index;
    struct metric_definition_s *next;
};
typedef struct metric_definition_s metric_definition_t;

struct instance_definition_s {
    char *name;
    char *path;
    metric_definition_t **metric_list;
    int metric_list_len;
    cdtime_t last;
    cdtime_t interval;
    struct instance_definition_s *next;
};
typedef struct instance_definition_s instance_definition_t;

/* Private */
static metric_definition_t *metric_head = NULL;

static int snort_read_submit(instance_definition_t *id, metric_definition_t *md,
    const char *buf){

    /* Registration variables */
    value_t value;
    value_list_t vl = VALUE_LIST_INIT;

    DEBUG("snort plugin: plugin_instance=%s type=%s value=%s", id->name,
        md->type, buf);

    if (buf == NULL)
        return (-1);

    /* Parse value */
    parse_value(buf, &value, md->data_source_type);

    /* Register */
    vl.values_len = 1;
    vl.values = &value;

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "snort", sizeof(vl.plugin));
    sstrncpy(vl.plugin_instance, id->name, sizeof(vl.plugin_instance));
    sstrncpy(vl.type, md->type, sizeof(vl.type));

    vl.time = id->last;
    vl.interval = id->interval;

    DEBUG("snort plugin: -> plugin_dispatch_values (&vl);");
    plugin_dispatch_values(&vl);

    return (0);
}

static int snort_read(user_data_t *ud){
    instance_definition_t *id;
    metric_definition_t *md;

    int i;
    int fd;

    char **metrics;
    int metrics_num;

    struct stat sb;
    char *buf, *buf_ptr;

    /* mmap, char pointers */
    char *p_start;
    char *p_end;

    id = ud->data;
    DEBUG("snort plugin: snort_read (instance = %s)", id->name);

    fd = open(id->path, O_RDONLY);
    if (fd == -1){
        ERROR("snort plugin: Unable to open `%s'.", id->path);
        return (-1);
    }

    if ((fstat(fd, &sb) != 0) || (!S_ISREG(sb.st_mode))){
        ERROR("snort plugin: `%s' is not a file.", id->path);
        return (-1);
    }

    if (sb.st_size == 0){
        ERROR("snort plugin: `%s' is empty.", id->path);
        return (-1);
    }

    p_start = mmap(/* addr = */ NULL, sb.st_size, PROT_READ, MAP_SHARED, fd,
        /* offset = */ 0);
    if (p_start == MAP_FAILED){
        ERROR("snort plugin: mmap error");
        return (-1);
    }

    /* Set the start value count. */
    metrics_num = 1;

    /* Set the pointer to the last line of the file and count the fields.
     (Skip the last two characters of the buffer: `\n' and `\0') */
    for (p_end = (p_start + sb.st_size) - 2; p_end > p_start; --p_end){
        if (*p_end == ','){
            ++metrics_num;
        } else if (*p_end == '\n'){
            ++p_end;
            break;
        }
    }

    if (metrics_num == 1){
        ERROR("snort plugin: last line of `%s' does not contain enough values.", id->path);
        return (-1);
    }

    if (*p_end == '#'){
        ERROR("snort plugin: last line of `%s' is a comment.", id->path);
        return (-1);
    }

    /* Copy the line to the buffer */
    buf = strdup(p_end);

    /* Done with mmap and file pointer */
    close(fd);
    munmap(p_start, sb.st_size);

    /* Create a list of all values */
    metrics = calloc (metrics_num, sizeof (*metrics));
    if (metrics == NULL) {
        ERROR ("snort plugin: calloc failed.");
        return (-1);
    }

    buf_ptr = buf;
    i = 0;
    while (buf_ptr != NULL) {
        char *next = strchr (buf_ptr, ',');
        if (next != NULL) {
            *next = 0;
            next++;
        }
        metrics[i] = buf_ptr;
        buf_ptr = next;
        i++;
    }
    assert (i == metrics_num);

    /* Set last time */
    id->last = TIME_T_TO_CDTIME_T(strtol(*metrics, NULL, 0));

    /* Register values */
    for (i = 0; i < id->metric_list_len; ++i){
        md = id->metric_list[i];

        if (md->index >= metrics_num) {
            ERROR ("snort plugin: Metric \"%s\": Request for index %i when "
                    "only %i fields are available.",
                    md->name, md->index, metrics_num);
            continue;
        }

        snort_read_submit(id, md, metrics[md->index]);
    }

    /* Free up resources */
    free(metrics);
    free(buf);
    return (0);
}

static void snort_metric_definition_destroy(void *arg){
    metric_definition_t *md;

    md = arg;
    if (md == NULL)
        return;

    if (md->name != NULL)
        DEBUG("snort plugin: Destroying metric definition `%s'.", md->name);

    sfree(md->name);
    sfree(md->type);
    sfree(md);
}

static int snort_config_add_metric_index(metric_definition_t *md, oconfig_item_t *ci){
    if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER)){
        WARNING("snort plugin: `Index' needs exactly one integer argument.");
        return (-1);
    }

    md->index = (int)ci->values[0].value.number;
    if (md->index <= 0){
        WARNING("snort plugin: `Index' must be higher than 0.");
        return (-1);
    }

    return (0);
}

/* Parse metric  */
static int snort_config_add_metric(oconfig_item_t *ci){
    metric_definition_t *md;
    const data_set_t *ds;
    int status = 0;
    int i;

    if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)){
        WARNING("snort plugin: The `Metric' config option needs exactly one string argument.");
        return (-1);
    }

    md = (metric_definition_t *)malloc(sizeof(*md));
    if (md == NULL)
        return (-1);
    memset(md, 0, sizeof(*md));

    md->name = strdup(ci->values[0].value.string);
    if (md->name == NULL){
        free(md);
        return (-1);
    }

    for (i = 0; i < ci->children_num; ++i){
        oconfig_item_t *option = ci->children + i;
        status = 0;

        if (strcasecmp("Type", option->key) == 0)
            status = cf_util_get_string(option, &md->type);
        else if (strcasecmp("Index", option->key) == 0)
            status = snort_config_add_metric_index(md, option);
        else {
            WARNING("snort plugin: Option `%s' not allowed here.", option->key);
            status = -1;
        }

        if (status != 0)
            break;
    }

    if (status != 0){
        snort_metric_definition_destroy(md);
        return (-1);
    }

    /* Verify all necessary options have been set. */
    if (md->type == NULL){
        WARNING("snort plugin: Option `Type' must be set.");
        status = -1;
    } else if (md->index == 0){
        WARNING("snort plugin: Option `Index' must be set.");
        status = -1;
    }

    if (status != 0){
        snort_metric_definition_destroy(md);
        return (-1);
    }

    /* Retrieve the data source type from the types db. */
    ds = plugin_get_ds(md->type);
    if (ds == NULL){
        WARNING("snort plugin: `Type' must be defined in `types.db'.");
        snort_metric_definition_destroy(md);
        return (-1);
    } else {
        md->data_source_type = ds->ds->type;
    }

    DEBUG("snort plugin: md = { name = %s, type = %s, data_source_type = %d, index = %d }",
        md->name, md->type, md->data_source_type, md->index);

    if (metric_head == NULL)
        metric_head = md;
    else {
        metric_definition_t *last;
        last = metric_head;
        while (last->next != NULL)
            last = last->next;
        last->next = md;
    }

    return (0);
}

static void snort_instance_definition_destroy(void *arg){
    instance_definition_t *id;

    id = arg;
    if (id == NULL)
        return;

    if (id->name != NULL)
        DEBUG("snort plugin: Destroying instance definition `%s'.", id->name);

    sfree(id->name);
    sfree(id->path);
    sfree(id->metric_list);
    sfree(id);
}

static int snort_config_add_instance_collect(instance_definition_t *id, oconfig_item_t *ci){
    metric_definition_t *metric;
    int i;

    if (ci->values_num < 1){
        WARNING("snort plugin: The `Collect' config option needs at least one argument.");
        return (-1);
    }

    /* Verify string arguments */
    for (i = 0; i < ci->values_num; ++i)
        if (ci->values[i].type != OCONFIG_TYPE_STRING){
            WARNING("snort plugin: All arguments to `Collect' must be strings.");
            return (-1);
        }

    id->metric_list = (metric_definition_t **)malloc(sizeof(metric_definition_t *) * ci->values_num);
    if (id->metric_list == NULL)
        return (-1);

    for (i = 0; i < ci->values_num; ++i){
        for (metric = metric_head; metric != NULL; metric = metric->next)
            if (strcasecmp(ci->values[i].value.string, metric->name) == 0)
                break;

        if (metric == NULL){
            WARNING("snort plugin: `Collect' argument not found `%s'.", ci->values[i].value.string);
            return (-1);
        }

        DEBUG("snort plugin: id { name=%s md->name=%s }", id->name, metric->name);

        id->metric_list[i] = metric;
        id->metric_list_len++;
    }

    return (0);
}

/* Parse instance  */
static int snort_config_add_instance(oconfig_item_t *ci){

    instance_definition_t* id;
    int status = 0;
    int i;

    /* Registration variables */
    char cb_name[DATA_MAX_NAME_LEN];
    user_data_t cb_data;
    struct timespec cb_interval;

    if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)){
        WARNING("snort plugin: The `Instance' config option needs exactly one string argument.");
        return (-1);
    }

    id = (instance_definition_t *)malloc(sizeof(*id));
    if (id == NULL)
        return (-1);
    memset(id, 0, sizeof(*id));

    id->name = strdup(ci->values[0].value.string);
    if (id->name == NULL){
        free(id);
        return (-1);
    }

    /* Use default interval. */
    id->interval = plugin_get_interval();

    for (i = 0; i < ci->children_num; ++i){
        oconfig_item_t *option = ci->children + i;
        status = 0;

        if (strcasecmp("Path", option->key) == 0)
            status = cf_util_get_string(option, &id->path);
        else if (strcasecmp("Collect", option->key) == 0)
            status = snort_config_add_instance_collect(id, option);
        else if (strcasecmp("Interval", option->key) == 0)
            cf_util_get_cdtime(option, &id->interval);
        else {
            WARNING("snort plugin: Option `%s' not allowed here.", option->key);
            status = -1;
        }

        if (status != 0)
            break;
    }

    if (status != 0){
        snort_instance_definition_destroy(id);
        return (-1);
    }

    /* Verify all necessary options have been set. */
    if (id->path == NULL){
        WARNING("snort plugin: Option `Path' must be set.");
        status = -1;
    } else if (id->metric_list == NULL){
        WARNING("snort plugin: Option `Collect' must be set.");
        status = -1;
   }

    if (status != 0){
        snort_instance_definition_destroy(id);
        return (-1);
    }

    DEBUG("snort plugin: id = { name = %s, path = %s }", id->name, id->path);

    ssnprintf (cb_name, sizeof (cb_name), "snort-%s", id->name);
    memset(&cb_data, 0, sizeof(cb_data));
    cb_data.data = id;
    cb_data.free_func = snort_instance_definition_destroy;
    CDTIME_T_TO_TIMESPEC(id->interval, &cb_interval);
    status = plugin_register_complex_read(NULL, cb_name, snort_read, &cb_interval, &cb_data);

    if (status != 0){
        ERROR("snort plugin: Registering complex read function failed.");
        snort_instance_definition_destroy(id);
        return (-1);
    }

    return (0);
}

/* Parse blocks */
static int snort_config(oconfig_item_t *ci){
    int i;
    for (i = 0; i < ci->children_num; ++i){
        oconfig_item_t *child = ci->children + i;
        if (strcasecmp("Metric", child->key) == 0)
            snort_config_add_metric(child);
        else if (strcasecmp("Instance", child->key) == 0)
            snort_config_add_instance(child);
        else
            WARNING("snort plugin: Ignore unknown config option `%s'.", child->key);
    }

    return (0);
} /* int snort_config */

static int snort_shutdown(void){
    metric_definition_t *metric_this;
    metric_definition_t *metric_next;

    metric_this = metric_head;
    metric_head = NULL;

    while (metric_this != NULL){
        metric_next = metric_this->next;
        snort_metric_definition_destroy(metric_this);
        metric_this = metric_next;
    }

    return (0);
}

void module_register(void){
    plugin_register_complex_config("snort", snort_config);
    plugin_register_shutdown("snort", snort_shutdown);
}

