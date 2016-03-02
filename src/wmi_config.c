/*
 * Copyright (c) 2015 Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "configfile.h"
#include "wmi.h"
#include "plugin.h"

static char* config_get_typename (oconfig_item_t *ci)
{
    int i;
    char *typename = NULL;
    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = &ci->children[i];
        if (strcmp ("Type", child->key) == 0)
        {
            if (typename)
            {
                ERROR ("wmi error: Multiple Types provided in one block.");
                free (typename);
                return (NULL);
            }

            if (child->values_num != 1
                    || child->values[0].type != OCONFIG_TYPE_STRING)
            {
                ERROR ("wmi error: Type needs a single string argument,");
                return (NULL);
            }

            typename = strdup (child->values[0].value.string);
        }
    }

    if (!typename)
    {
        ERROR ("wmi error: Type declaration not found in block.");
        return (NULL);
    }

    return (typename);
}

const char *metric_supported_options[] =
{
    "TypeInstance",
    "TypeInstanceSuffixFrom",
    "PluginInstanceSuffixFrom",
    "Value",
    "Type"
};


static int config_get_metric_sanity_check (oconfig_item_t *ci)
{
    int i;

    assert (strcmp ("Metric", ci->key) == 0);

    for (i = 0; i < ci->children_num; i++)
    {
        int j;
        int found = 0;
        oconfig_item_t *child = &ci->children[i];

        for (j = 0; j < COUNTOF (metric_supported_options); j++)
        {
            if (strcmp (metric_supported_options[j], child->key) == 0)
            {
                found = 1;
                break;
            }
        }

        if (!found)
        {
            ERROR ("%s option is not supported in Metric block!", child->key);
            return (-1);
        }
    }

    return (0);
}

/* TODO: doc this. Remember about NULL args */
static metadata_str_t* config_get_metadata_str (oconfig_item_t *ci,
        const char *base_str, const char *part_str)
{
    int i;
    int num_parts, read_parts;
    metadata_str_t* ms = NULL;

    num_parts = 0;
    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = &ci->children[i];
        if (part_str && strcmp (part_str, child->key) == 0)
            num_parts++;
    }

    ms = metadata_str_alloc (num_parts);

    read_parts = 0;
    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = &ci->children[i];
        if (base_str && strcmp (base_str, child->key) == 0)
        {
            if (ms->base)
            {
                ERROR ("wmi error: multiple %ss provided "
                        "in one Metric block,", base_str);
                goto err;
            }

            if (cf_util_get_string (child, &ms->base))
            {
                ERROR ("wmi error: %s needs a single string argument.",
                        base_str);
                goto err;
            }
        }
        else if (part_str && strcmp (part_str, child->key) == 0)
        {
            char *str = NULL;
            if (cf_util_get_string (child, &str))
            {
                ERROR ("wmi error: %s needs a single string argument.",
                        part_str);
                goto err;
            }

            ms->parts[read_parts] = strtowstr (str);
            free (str);
            read_parts++;
        }
    }

    return (ms);

err:
    metadata_str_free (ms);
    return (NULL);
}

static metadata_str_t* config_get_type_instance_str (oconfig_item_t *ci)
{
    return (config_get_metadata_str (ci, "TypeInstance", "TypeInstanceSuffixFrom"));
}

static int config_values_count (oconfig_item_t *ci)
{
    int i;
    int values_num = 0;
    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = &ci->children[i];
        if (strcmp ("Value", child->key) == 0)
        {
            if (child->values_num != 2
                    || child->values[0].type != OCONFIG_TYPE_STRING
                    || child->values[1].type != OCONFIG_TYPE_STRING)
            {
                ERROR ("wmi error: Value expects exactly two string arguments: "
                        "name of the field in the object and the name "
                        "in collectd type.");
                return (-1);
            }
            values_num++;
        }
    }

    if (values_num == 0)
    {
        ERROR ("wmi error: At least one Value in Metric block is needed.");
        return (-1);
    }
    return (values_num);
}

static wmi_metric_t* config_get_metric (oconfig_item_t *ci)
{
    int i;
    int values_num;
    char *typename = NULL;
    metadata_str_t *type_instance = NULL;
    metadata_str_t *plugin_instance = NULL;
    wmi_metric_t *metric = NULL;

    if (config_get_metric_sanity_check (ci))
        return (NULL);

    if ((typename = config_get_typename (ci)) == NULL)
        goto err;

    if ((type_instance = config_get_type_instance_str (ci)) == NULL)
        goto err;

    if ((values_num = config_values_count (ci)) <= 0)
        goto err;

    if ((plugin_instance =
         config_get_metadata_str (ci, NULL, "PluginInstanceSuffixFrom")) == NULL)
        goto err;

    metric = wmi_metric_alloc (values_num);
    values_num = 0;
    metric->typename = typename;
    metric->type_instance = type_instance;
    metric->plugin_instance = plugin_instance;

    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = &ci->children[i];
        if (strcmp ("Value", child->key) == 0)
        {

            metric->values[values_num].source =
                strtowstr (child->values[0].value.string);
            metric->values[values_num].dest =
                strdup (child->values[1].value.string);
            values_num++;
        }
    }

    return (metric);

err:
    free (typename);
    metadata_str_free (type_instance);
    return (NULL);
}

static wmi_query_t* config_get_query (oconfig_item_t *ci, plugin_instance_t *pi)
{
    int i;
    int status = 0;
    char *stmt = NULL;
    wmi_query_t *query = NULL;
    LIST_TYPE(wmi_metric_t) *metrics = NULL;

    assert (strcmp ("Query", ci->key) == 0);

    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = &ci->children[i];
        if (strcmp ("Statement", child->key) == 0)
        {
            if (stmt)
            {
                ERROR ("wmi error: Multiple Statements in one Query block."
                       " Previous: %s", stmt);
                status = -1;
                break;
            }
            if (cf_util_get_string (child, &stmt))
            {
                ERROR ("wmi error: Statement requires a single "
                       "string as an argument.");
                status = -1;
                break;
            }
        }
        else if (strcmp ("Metric", child->key) == 0)
        {
            wmi_metric_t *m = config_get_metric (child);
            if (m) LIST_INSERT_FRONT (metrics, m);
        }
        else
        {
            status = -1;
            ERROR ("wmi error: unknown option: %s", child->key);
            break;
        }
    }

    if (stmt == NULL || metrics == NULL)
        status = -1;

    if (status)
    {
        LIST_FREE (metrics, wmi_metric_free);
        free (stmt);
        return (NULL);
    }

    query = malloc (sizeof (wmi_query_t));
    query->statement = strtowstr (stmt);
    free (stmt);
    query->metrics = metrics;
    query->plugin_instance = pi;
    return (query);
}

static int add_instance (oconfig_item_t *ci,
        LIST_TYPE(plugin_instance_t) **plugin_instances)
{
    int i;
    assert (strcmp ("Instance", ci->key) == 0);

    /* Get instance name */
    plugin_instance_t *pi = malloc (sizeof (plugin_instance_t));
    pi->base_name = NULL;
    pi->queries = NULL;
    if (cf_util_get_string (ci, &pi->base_name))
    {
        free (pi);
        return (-1);
    }

    /* Fill the instance with queries */
    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = &ci->children[i];
        if (strcmp ("Query", child->key) == 0)
        {
            wmi_query_t *q = config_get_query(child, pi);
            if (!q)
                continue;

            LIST_INSERT_FRONT (pi->queries, q);
        }
    }

    LIST_INSERT_FRONT (*plugin_instances, pi);

    return (0);
}

int wmi_configure (oconfig_item_t *ci,
        LIST_TYPE(plugin_instance_t) **plugin_instances)
{
    int i;
    int status;
    int success = 0;

    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = &ci->children[i];

        if (strcmp ("Instance", child->key) == 0)
        {
            status = add_instance (child, plugin_instances);
            if (!status)
                success = 1;
        }
    }

    if (success)
    {
        return (0);
    }
    else
    {
        ERROR("wmi error: No Instance has been added.");
        return (-1);
    }
}

void wmi_query_free (wmi_query_t *q)
{
    if (!q) return;

    free (q->statement);
    LIST_FREE (q->metrics, wmi_metric_free);
    free (q);
}

wmi_metric_t *wmi_metric_alloc (int num_values)
{
    int size = sizeof (wmi_metric_t) + num_values * sizeof (wmi_value_t);
    wmi_metric_t *m = malloc (size);
    memset (m, 0, size);
    m->values_num = num_values;
    return (m);
}

void wmi_metric_free (wmi_metric_t *m)
{
    if (!m) return;

    int i;
    for (i = 0; i < m->values_num; i++)
    {
        free (m->values[i].source);
        free (m->values[i].dest);
    }
    free (m->typename);
    free (m->type_instance);
    free (m->plugin_instance);
    free (m);
}
