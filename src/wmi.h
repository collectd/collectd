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


/* Example configuration file:
 *
LoadPlugin wmi
<Plugin wmi>
  <Instance "cpu">
    <Query>
      Statement "SELECT * FROM Win32_Processor"
      <Metric>
        Type "cpu"
        Value "LoadPercentage" "value"
        TypeInstance "LoadPercentage"
        PluginInstanceSuffixFrom "ProcessorId"
      </Metric>
    </Query>
  </Instance>

  <Instance "mem">
    <Query>
      Statement "SELECT * FROM Win32_OperatingSystem"
      <Metric>
        Type "memory"
        TypeInstance "FreePhysicalMemory"
        Value "FreePhysicalMemory" "value"
      </Metric>
      <Metric>
        Type "memory"
        TypeInstance "TotalVisibleMemorySize"
        Value "TotalVisibleMemorySize" "value"
      </Metric>
    </Query>
  </Instance>
</Plugin> 
 */
#ifndef WMI_H
#define WMI_H

#define LIST_NODE_DECL(node_type) node_type _node
#define LIST_NEXT_DECL(node_type) node_type *_next
#define LIST_NODE(nodeptr) (nodeptr->_node)
#define LIST_NEXT(nodeptr) (nodeptr->_next)
#define LIST_HEAD(list) (list)

#define LIST_TYPE(type) list_ ## type ## _t

#define LIST_DECL_TYPE(node_type) \
struct list_ ## node_type ## _s; \
typedef struct list_ ## node_type ## _s LIST_TYPE(node_type)

#define LIST_DEF_TYPE(node_type) \
LIST_DECL_TYPE(node_type); \
struct list_ ## node_type ## _s \
{\
    LIST_NODE_DECL(node_type*); \
    LIST_NEXT_DECL(LIST_TYPE(node_type)); \
}


#define LIST_INSERT_FRONT(list, new_node) \
do { \
    __typeof__(list) _n = malloc (sizeof (__typeof__(*list))); \
    LIST_NEXT(_n) = list; \
    LIST_NODE(_n) = new_node; \
    list = _n; \
} while (0)

#define LIST_FREE(list, node_free) \
do { \
    __typeof__(list) _head = list; \
    while (_head != NULL) \
    { \
        __typeof__(_head) _next = LIST_NEXT(_head); \
        node_free (LIST_NODE(_head)); \
        free (_head); \
        _head = _next; \
    } \
} while (0)

#define COUNTOF(x) (sizeof(x)/sizeof(0[x]))


typedef struct metadata_str_s
{
    char *base;
    int num_parts;
    wchar_t *parts[0];
} metadata_str_t;
metadata_str_t* metadata_str_alloc(int num_parts);
void metadata_str_free (metadata_str_t *ms);

struct wmi_query_s;
typedef struct wmi_query_s wmi_query_t;
LIST_DECL_TYPE(wmi_query_t);
typedef struct plugin_instance_s
{
    char *base_name;
    LIST_TYPE(wmi_query_t) *queries;
} plugin_instance_t;
LIST_DEF_TYPE(plugin_instance_t);
void plugin_instance_free (plugin_instance_t *pi);

typedef struct wmi_value_s
{
    wchar_t *source;
    char *dest;
} wmi_value_t;
void wmi_value_free(wmi_value_t *w);

typedef struct wmi_metric_s
{
    char *typename;
    metadata_str_t *type_instance;
    metadata_str_t *plugin_instance;
    int values_num;
    wmi_value_t values[0];
} wmi_metric_t;
LIST_DEF_TYPE(wmi_metric_t);
wmi_metric_t *wmi_metric_alloc(int num_values);
void wmi_metric_free(wmi_metric_t *m);

typedef struct wmi_query_s
{
    wchar_t *statement;
    LIST_TYPE(wmi_metric_t) *metrics;

    const plugin_instance_t *plugin_instance;
} wmi_query_t;
LIST_DEF_TYPE(wmi_query_t);
void wmi_query_free (wmi_query_t *q);

wchar_t* strtowstr (const char *source);
char* wstrtostr (const wchar_t *source);

int wmi_configure (oconfig_item_t *ci,
        LIST_TYPE(plugin_instance_t) **plugin_instances);

#endif /* WMI_H */
