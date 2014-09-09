/**
 * collectd - src/tail_csv.c
 * Copyright (C) 2013 Kris Nielander
 * Copyright (C) 2013 Florian Forster
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
 *   Kris Nielander <nielander at fox-it.com>
 *   Florian Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "plugin.h" /* plugin_register_*, plugin_dispatch_values */
#include "common.h" /* auxiliary functions */
#include "utils_tail.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

struct metric_definition_s {
    char *name;
    char *type;
    char *instance;
    int data_source_type;
    int value_from;
    struct metric_definition_s *next;
};
typedef struct metric_definition_s metric_definition_t;

struct instance_definition_s {
    char *instance;
    char *path;
    cu_tail_t *tail;
    metric_definition_t **metric_list;
    size_t metric_list_len;
    cdtime_t interval;
    int time_from;
    struct instance_definition_s *next;
};
typedef struct instance_definition_s instance_definition_t;

/* Private */
static metric_definition_t *metric_head = NULL;

static int tcsv_submit (instance_definition_t *id,
        metric_definition_t *md,
        value_t v, cdtime_t t)
{
    /* Registration variables */
    value_list_t vl = VALUE_LIST_INIT;

    /* Register */
    vl.values_len = 1;
    vl.values = &v;

    sstrncpy(vl.host, hostname_g, sizeof (vl.host));
    sstrncpy(vl.plugin, "tail_csv", sizeof(vl.plugin));
    if (id->instance != NULL)
        sstrncpy(vl.plugin_instance, id->instance, sizeof(vl.plugin_instance));
    sstrncpy(vl.type, md->type, sizeof(vl.type));
    if (md->instance != NULL)
        sstrncpy(vl.type_instance, md->instance, sizeof(vl.type_instance));

    vl.time = t;
    vl.interval = id->interval;

    return (plugin_dispatch_values(&vl));
}

static cdtime_t parse_time (char const *tbuf)
{
    double t;
    char *endptr = 0;

    errno = 0;
    t = strtod (tbuf, &endptr);
    if ((errno != 0) || (endptr == NULL) || (endptr[0] != 0))
        return (cdtime ());

    return (DOUBLE_TO_CDTIME_T (t));
}

static int tcsv_read_metric (instance_definition_t *id,
        metric_definition_t *md,
        char **fields, size_t fields_num)
{
    value_t v;
    cdtime_t t;
    int status;

    if (md->data_source_type == -1)
        return (EINVAL);

    if ((md->value_from >= fields_num) || (id->time_from >= fields_num))
        return (EINVAL);

    t = 0;
    if (id->time_from >= 0)
        t = parse_time (fields[id->time_from]);

    status = parse_value (fields[md->value_from], &v, md->data_source_type);
    if (status != 0)
        return (status);

    return (tcsv_submit (id, md, v, t));
}

static _Bool tcsv_check_index (int index, size_t fields_num, char const *name)
{
    if (index < 0)
        return 1;
    else if (((size_t) index) < fields_num)
        return 1;

    ERROR ("tail_csv plugin: Metric \"%s\": Request for index %i when "
            "only %zu fields are available.",
            name, index, fields_num);
    return (0);
}

static int tcsv_read_buffer (instance_definition_t *id,
        char *buffer, size_t buffer_size)
{
    char **metrics;
    size_t metrics_num;

    char *ptr;
    size_t i;

    /* Remove newlines at the end of line. */
    while (buffer_size > 0) {
        if ((buffer[buffer_size - 1] == '\n')
                || (buffer[buffer_size - 1] == '\r')) {
            buffer[buffer_size - 1] = 0;
            buffer_size--;
        } else {
            break;
        }
    }

    /* Ignore empty lines. */
    if ((buffer_size == 0) || (buffer[0] == '#'))
        return (0);

    /* Count the number of fields. */
    metrics_num = 1;
    for (i = 0; i < buffer_size; i++) {
        if (buffer[i] == ',')
            metrics_num++;
    }

    if (metrics_num == 1) {
        ERROR("tail_csv plugin: last line of `%s' does not contain "
                "enough values.", id->path);
        return (-1);
    }

    /* Create a list of all values */
    metrics = calloc (metrics_num, sizeof (*metrics));
    if (metrics == NULL) {
        ERROR ("tail_csv plugin: calloc failed.");
        return (ENOMEM);
    }

    ptr = buffer;
    metrics[0] = ptr;
    i = 1;
    for (ptr = buffer; *ptr != 0; ptr++) {
        if (*ptr != ',')
            continue;

        *ptr = 0;
        metrics[i] = ptr + 1;
        i++;
    }
    assert (i == metrics_num);

    /* Register values */
    for (i = 0; i < id->metric_list_len; ++i){
        metric_definition_t *md = id->metric_list[i];

        if (!tcsv_check_index (md->value_from, metrics_num, md->name)
                || !tcsv_check_index (id->time_from, metrics_num, md->name))
            continue;

        tcsv_read_metric (id, md, metrics, metrics_num);
    }

    /* Free up resources */
    sfree (metrics);
    return (0);
}

static int tcsv_read (user_data_t *ud) {
    instance_definition_t *id;
    id = ud->data;

    if (id->tail == NULL)
    {
        id->tail = cu_tail_create (id->path);
        if (id->tail == NULL)
        {
            ERROR ("tail_csv plugin: cu_tail_create (\"%s\") failed.",
                    id->path);
            return (-1);
        }
    }

    while (42)
    {
        char buffer[1024];
        size_t buffer_len;
        int status;

        status = cu_tail_readline (id->tail, buffer, (int) sizeof (buffer));
        if (status != 0)
        {
            ERROR ("tail_csv plugin: File \"%s\": cu_tail_readline failed "
                    "with status %i.", id->path, status);
            return (-1);
        }

        buffer_len = strlen (buffer);
        if (buffer_len == 0)
            break;

        tcsv_read_buffer (id, buffer, buffer_len);
    }

    return (0);
}

static void tcsv_metric_definition_destroy(void *arg){
    metric_definition_t *md;
    metric_definition_t *next;

    md = arg;
    if (md == NULL)
        return;

    next = md->next;
    md->next = NULL;

    sfree(md->name);
    sfree(md->type);
    sfree(md->instance);
    sfree(md);

    tcsv_metric_definition_destroy (next);
}

static int tcsv_config_get_index(oconfig_item_t *ci, int *ret_index) {
    int index;

    if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER)){
        WARNING("tail_csv plugin: The \"%s\" config option needs exactly one "
                "integer argument.", ci->key);
        return (-1);
    }

    index = (int) ci->values[0].value.number;
    if (index < 0) {
        WARNING("tail_csv plugin: The \"%s\" config option must be positive "
                "(or zero).", ci->key);
        return (-1);
    }

    *ret_index = index;
    return (0);
}

/* Parse metric  */
static int tcsv_config_add_metric(oconfig_item_t *ci){
    metric_definition_t *md;
    int status = 0;
    int i;

    md = (metric_definition_t *)malloc(sizeof(*md));
    if (md == NULL)
        return (-1);
    memset(md, 0, sizeof(*md));
    md->name = NULL;
    md->type = NULL;
    md->instance = NULL;
    md->data_source_type = -1;
    md->value_from = -1;
    md->next = NULL;

    status = cf_util_get_string (ci, &md->name);
    if (status != 0) {
        sfree (md);
        return (-1);
    }

    for (i = 0; i < ci->children_num; ++i){
        oconfig_item_t *option = ci->children + i;
        status = 0;

        if (strcasecmp("Type", option->key) == 0)
            status = cf_util_get_string(option, &md->type);
        else if (strcasecmp("Instance", option->key) == 0)
            status = cf_util_get_string(option, &md->instance);
        else if (strcasecmp("ValueFrom", option->key) == 0)
            status = tcsv_config_get_index (option, &md->value_from);
        else {
            WARNING("tail_csv plugin: Option `%s' not allowed here.", option->key);
            status = -1;
        }

        if (status != 0)
            break;
    }

    if (status != 0){
        tcsv_metric_definition_destroy(md);
        return (-1);
    }

    /* Verify all necessary options have been set. */
    if (md->type == NULL) {
        WARNING("tail_csv plugin: Option `Type' must be set.");
        status = -1;
    } else if (md->value_from < 0) {
        WARNING("tail_csv plugin: Option `ValueFrom' must be set.");
        status = -1;
    }
    if (status != 0) {
        tcsv_metric_definition_destroy(md);
        return (status);
    }

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

static void tcsv_instance_definition_destroy(void *arg){
    instance_definition_t *id;

    id = arg;
    if (id == NULL)
        return;

    if (id->tail != NULL)
        cu_tail_destroy (id->tail);
    id->tail = NULL;

    sfree(id->instance);
    sfree(id->path);
    sfree(id->metric_list);
    sfree(id);
}

static int tcsv_config_add_instance_collect(instance_definition_t *id, oconfig_item_t *ci){
    metric_definition_t *metric;
    int i;

    if (ci->values_num < 1){
        WARNING("tail_csv plugin: The `Collect' config option needs at least one argument.");
        return (-1);
    }

    /* Verify string arguments */
    for (i = 0; i < ci->values_num; ++i)
        if (ci->values[i].type != OCONFIG_TYPE_STRING){
            WARNING("tail_csv plugin: All arguments to `Collect' must be strings.");
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
            WARNING("tail_csv plugin: `Collect' argument not found `%s'.", ci->values[i].value.string);
            return (-1);
        }

        id->metric_list[i] = metric;
        id->metric_list_len++;
    }

    return (0);
}

/* <File /> block */
static int tcsv_config_add_file(oconfig_item_t *ci)
{
    instance_definition_t* id;
    int status = 0;
    int i;

    /* Registration variables */
    char cb_name[DATA_MAX_NAME_LEN];
    user_data_t cb_data;
    struct timespec cb_interval;

    id = malloc(sizeof(*id));
    if (id == NULL)
        return (-1);
    memset(id, 0, sizeof(*id));
    id->instance = NULL;
    id->path = NULL;
    id->metric_list = NULL;
    id->time_from = -1;
    id->next = NULL;

    status = cf_util_get_string (ci, &id->path);
    if (status != 0) {
        sfree (id);
        return (status);
    }

    /* Use default interval. */
    id->interval = plugin_get_interval();

    for (i = 0; i < ci->children_num; ++i){
        oconfig_item_t *option = ci->children + i;
        status = 0;

        if (strcasecmp("Instance", option->key) == 0)
            status = cf_util_get_string(option, &id->instance);
        else if (strcasecmp("Collect", option->key) == 0)
            status = tcsv_config_add_instance_collect(id, option);
        else if (strcasecmp("Interval", option->key) == 0)
            cf_util_get_cdtime(option, &id->interval);
        else if (strcasecmp("TimeFrom", option->key) == 0)
            status = tcsv_config_get_index (option, &id->time_from);
        else {
            WARNING("tail_csv plugin: Option `%s' not allowed here.", option->key);
            status = -1;
        }

        if (status != 0)
            break;
    }

    if (status != 0){
        tcsv_instance_definition_destroy(id);
        return (-1);
    }

    /* Verify all necessary options have been set. */
    if (id->path == NULL){
        WARNING("tail_csv plugin: Option `Path' must be set.");
        status = -1;
    } else if (id->metric_list == NULL){
        WARNING("tail_csv plugin: Option `Collect' must be set.");
        status = -1;
   }

    if (status != 0){
        tcsv_instance_definition_destroy(id);
        return (-1);
    }

    ssnprintf (cb_name, sizeof (cb_name), "tail_csv/%s", id->path);
    memset(&cb_data, 0, sizeof(cb_data));
    cb_data.data = id;
    cb_data.free_func = tcsv_instance_definition_destroy;
    CDTIME_T_TO_TIMESPEC(id->interval, &cb_interval);
    status = plugin_register_complex_read(NULL, cb_name, tcsv_read, &cb_interval, &cb_data);

    if (status != 0){
        ERROR("tail_csv plugin: Registering complex read function failed.");
        tcsv_instance_definition_destroy(id);
        return (-1);
    }

    return (0);
}

/* Parse blocks */
static int tcsv_config(oconfig_item_t *ci){
    int i;
    for (i = 0; i < ci->children_num; ++i){
        oconfig_item_t *child = ci->children + i;
        if (strcasecmp("Metric", child->key) == 0)
            tcsv_config_add_metric(child);
        else if (strcasecmp("File", child->key) == 0)
            tcsv_config_add_file(child);
        else
            WARNING("tail_csv plugin: Ignore unknown config option `%s'.", child->key);
    }

    return (0);
} /* int tcsv_config */

static int tcsv_init(void) { /* {{{ */
    static _Bool have_init = 0;
    metric_definition_t *md;

    if (have_init)
        return (0);

    for (md = metric_head; md != NULL; md = md->next) {
        data_set_t const *ds;

        /* Retrieve the data source type from the types db. */
        ds = plugin_get_ds(md->type);
        if (ds == NULL)
        {
            ERROR ("tail_csv plugin: Failed to look up type \"%s\" for "
                    "metric \"%s\". It may not be defined in the types.db "
                    "file. Please read the types.db(5) manual page for more "
                    "details.",
                    md->type, md->name);
            continue;
        }
        else if (ds->ds_num != 1)
        {
            ERROR ("tail_csv plugin: The type \"%s\" has %i data sources. "
                    "Only types with a single data soure are supported.",
                    ds->type, ds->ds_num);
            continue;
        }

        md->data_source_type = ds->ds->type;
    }

    return (0);
} /* }}} int tcsv_init */

static int tcsv_shutdown (void) {
    tcsv_metric_definition_destroy (metric_head);
    metric_head = NULL;

    return (0);
}

void module_register(void){
    plugin_register_complex_config("tail_csv", tcsv_config);
    plugin_register_init("tail_csv", tcsv_init);
    plugin_register_shutdown("tail_csv", tcsv_shutdown);
}

/* vim: set sw=4 sts=4 et : */
