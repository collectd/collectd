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
#include "plugin.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <wbemidl.h>
#include <windows.h>

#include "wmi.h"
#include "wmi_variant_utils.h"

static LIST_TYPE(plugin_instance_t) *plugin_instances_g;


/* String conversion utils */
wchar_t* strtowstr(const char *source)
{
    int source_len;
    wchar_t *result;

    source_len = strlen (source);
    result = calloc (source_len + 1, sizeof (wchar_t));
    mbstowcs (result, source, source_len + 1);

    return (result);
}

char* wstrtostr (const wchar_t *source)
{
    int source_len;
    char *result;

    source_len = wcslen (source);
    result = calloc (source_len + 1, sizeof (char));
    wcstombs (result, source, source_len + 1);
    return (result);
}

/* metadata_str_t */
metadata_str_t* metadata_str_alloc (int num_parts)
{
    size_t size = sizeof (metadata_str_t) + num_parts * sizeof (char*);
    metadata_str_t *pi = malloc (size);

    memset (pi, 0, size);
    pi->num_parts = num_parts;
    return (pi);
}

void metadata_str_free (metadata_str_t *ms)
{
    if (!ms) return;

    int i;
    for (i = 0; i < ms->num_parts; i++)
        free (ms->parts[i]);
    free (ms);
}

/* WMI specific */
typedef struct wmi_connection_s
{
    IDispatch *dispatcher;
} wmi_connection_t;

typedef struct wmi_result_list_s
{
    IDispatch* results;

    int count;
    int last_result;
} wmi_result_list_t;

typedef struct wmi_result_s
{
    IDispatch* result;
} wmi_result_t;

HRESULT wmi_invoke_method (IDispatch *dispatcher, const wchar_t *method_name,
                           DISPPARAMS *params, VARIANT *result)
{
    HRESULT hr;
    DISPID dispid[1];
    BSTR name;

    name = SysAllocString (method_name);
    hr = dispatcher->lpVtbl->GetIDsOfNames (dispatcher, &IID_NULL, &name, 1,
            LOCALE_SYSTEM_DEFAULT, dispid);
    SysFreeString (name);

    if (FAILED (hr))
        return (hr);

    hr = dispatcher->lpVtbl->Invoke (dispatcher, dispid[0], &IID_NULL,
            LOCALE_SYSTEM_DEFAULT, DISPATCH_METHOD, params, result, NULL, NULL);
    if (FAILED (hr))
        return (hr);

    return (0);
}

int wmi_get_property (IDispatch *dispatcher, const wchar_t *property_name,
        VARIANT *result)
{
    DISPPARAMS params;
    HRESULT hr;
    DISPID dispid[1];
    BSTR name;

    name = SysAllocString (property_name);
    hr = dispatcher->lpVtbl->GetIDsOfNames (dispatcher, &IID_NULL,
            &name, 1, LOCALE_SYSTEM_DEFAULT, dispid);
    SysFreeString(name);

    if (FAILED (hr))
      return (hr);

    params.cArgs = 0;
    params.cNamedArgs = 0;

    hr = dispatcher->lpVtbl->Invoke (dispatcher, dispid[0], &IID_NULL,
            LOCALE_SYSTEM_DEFAULT, DISPATCH_PROPERTYGET, &params, result, NULL, NULL);
    if (FAILED (hr))
      return (hr);

    return (0);
}

wmi_result_list_t* wmi_query (wmi_connection_t *connection, const wchar_t *query)
{
    HRESULT status = 0;
    wmi_result_list_t *res = NULL;
    const char *error_details = NULL;

    VARIANT result;
    VARIANTARG args[1];
    DISPPARAMS params;

    params.cNamedArgs = 0;
    params.rgvarg = args;

    params.cArgs = 1;
    params.rgvarg[0].vt = VT_BSTR;
    params.rgvarg[0].bstrVal = SysAllocString(query);

    status = wmi_invoke_method (connection->dispatcher, L"ExecQuery", &params, &result);
    SysFreeString (params.rgvarg[0].bstrVal);
    if (FAILED (status))
    {
        error_details = "ExecQuery() failed.";
        goto err;
    }

    res = malloc (sizeof (wmi_result_list_t));
    res->results = result.pdispVal;

    status = wmi_get_property (res->results, L"Count", &result);
    if (FAILED (status))
    {
        // It is very likely that the set returned by ExecQuery is empty,
        // which seems to be signaled by a missing 'Count' property.
        // Because of that, we're not going to treat it as an error.
        res->count = 0;
    }
    else
    {
        res->count = result.intVal;
    }
    res->last_result = -1;

    return (res);

err:
    free (res);
    ERROR ("wmi error: Unknown error [0x%x] during query: '%ls'. Error details: %s",
           (unsigned)status, query, error_details);
    return (NULL);
}

void wmi_result_list_release (wmi_result_list_t *results)
{
    if (results)
        results->results->lpVtbl->Release (results->results);
    free (results);
}

wmi_result_t* wmi_get_next_result (wmi_result_list_t *results)
{
    if (!results)
        return (NULL);

    if (results->last_result + 1 >= results->count)
        return (NULL);

    HRESULT hr;
    DISPPARAMS params;
    VARIANTARG args[1];
    VARIANT varResult;

    params.cArgs = 1;
    params.cNamedArgs = 0;

    params.rgvarg = args;
    params.rgvarg[0].vt = VT_UI4;
    params.rgvarg[0].uintVal = results->last_result + 1;

    hr = wmi_invoke_method (results->results, L"ItemIndex", &params, &varResult);
    if (FAILED (hr))
    {
        ERROR ("wmi error: Cannot get next result. Error code 0x%x",
                (unsigned) hr);
        return (NULL);
    }

    results->last_result++;

    wmi_result_t *result = malloc (sizeof (wmi_result_t));
    result->result = varResult.pdispVal;
    return (result);
}

void wmi_result_release (wmi_result_t *result)
{
    if (result)
        result->result->lpVtbl->Release (result->result);
    free (result);
}

int wmi_result_get_value(const wmi_result_t *result, const wchar_t *name, VARIANT *value)
{

    VARIANT varResult;
    HRESULT hr;

    hr = wmi_get_property (result->result, L"Properties_", &varResult);
    if (hr != S_OK)
        goto err;

    DISPPARAMS params;
    VARIANTARG args[1];

    params.cArgs = 1;
    params.cNamedArgs = 0;

    params.rgvarg = args;
    params.rgvarg[0].vt = VT_BSTR;
    params.rgvarg[0].bstrVal = SysAllocString(name);

    hr = wmi_invoke_method (varResult.pdispVal, L"Item", &params, &varResult);
    SysFreeString(params.rgvarg[0].bstrVal);
    if (FAILED (hr))
        goto err;

    hr = wmi_get_property (varResult.pdispVal, L"Value", value);
    if (FAILED (hr))
        goto err;

    return (0);

err:
    // TODO: proper error handling
    switch (hr)
    {
    case WBEM_E_NOT_FOUND:
        ERROR ("wmi error: Property %ls not found.", name);
        break;
    default:
        ERROR ("wmi error: Unknown error 0x%x while fetching property %ls",
              (unsigned) hr, name);
        break;
    }

    return (-1);
}

void wmi_release (wmi_connection_t *connection)
{
    if (!connection)
        return;

    if (connection->dispatcher)
        connection->dispatcher->lpVtbl->Release(connection->dispatcher);

    CoUninitialize();

    free (connection);
}

wmi_connection_t* wmi_connect (void)
{
    wmi_connection_t *connection = malloc (sizeof (wmi_connection_t));
    HRESULT hr = CoInitializeEx (0, COINIT_MULTITHREADED);
    if (FAILED (hr))
        goto err;

    BSTR str = SysAllocString (L"winmgmts:root\\cimv2");
    hr = CoGetObject (str, NULL, &IID_IDispatch, (void**)&connection->dispatcher);
    if (FAILED (hr))
        goto err;

    return connection;

err:
    ERROR("wmi error: Initialization failed. Error code: %x", (unsigned)hr);
    wmi_release (connection);
    return NULL;
}

static wmi_connection_t *wmi;

static int wmi_init (void)
{
    wmi = wmi_connect ();
    return (0);
}

void plugin_instance_free (plugin_instance_t *pi)
{
    if (!pi) return;

    free (pi->base_name);
    LIST_FREE (pi->queries, wmi_query_free);
}

static int wmi_shutdown (void)
{
    LIST_FREE (plugin_instances_g, plugin_instance_free);
    wmi_release (wmi);
    return (0);
}

static void store (VARIANT *src, value_t *dst, int dst_type)
{
    switch (dst_type)
    {
    case DS_TYPE_GAUGE:
        dst->gauge = variant_get_double (src);
        break;

    case DS_TYPE_DERIVE:
        dst->derive = variant_get_int64 (src);
        break;

    case DS_TYPE_ABSOLUTE:
        dst->absolute = variant_get_uint64 (src);
        break;

    case DS_TYPE_COUNTER:
        dst->counter = variant_get_ull (src);
        break;

    default:
        ERROR ("Destination type '%d' is not supported", dst_type);
        break;
    }
}

/* Find position of `name` in `ds */
static int find_index_in_ds (const data_set_t *ds, const char *name)
{
    int i;
    for (i = 0; i < ds->ds_num; i++)
        if (strcmp (ds->ds[i].name, name) == 0)
            return (i);
    return (-1);
}

static void sanitize_string (char *s)
{
    int i;
    for (i = 0; s[i]; i++)
        if (!isalnum (s[i]) && s[i] != '-')
            s[i] = '_';
}

static void append_metadata_string (char *dest, int size, const metadata_str_t *ms,
        const wmi_result_t *result)
{
    int i;
    int status = 0;
    VARIANT v;

    int dest_len = strlen (dest);
    int size_left = size - dest_len;

    if (ms->base)
    {
        if (dest_len > 0)
            status = ssnprintf(&dest[dest_len], size_left, "-%s", ms->base);
        else
            status = ssnprintf(&dest[dest_len], size_left, "%s", ms->base);

        dest_len = strlen (dest);
        size_left = size - dest_len;
    }

    if (!result) return;

    for (i = 0; i < ms->num_parts; i++)
    {
        dest_len = strlen (dest);
        size_left = size - dest_len;

        wmi_result_get_value (result, ms->parts[i], &v);
        char *part = wstrtostr (v.bstrVal);
        sanitize_string (part);

        if (dest_len > 0)
            status = ssnprintf (&dest[dest_len], size_left, "-%s", part);
        else
            status = ssnprintf (&dest[dest_len], size_left, "%s", part);

        if (status < 0 || status >= size_left)
        {
            WARNING ("wmi warning: fetched value \"%s\" did not "
                     "fit into metadata (which is of size %d).",
                     part, size);
        }

        free (part);
    }
}

static int wmi_exec_query (wmi_query_t *q)
{
    wmi_result_list_t *results;
    value_list_t vl = VALUE_LIST_INIT;

    sstrncpy (vl.host, hostname_g, sizeof (vl.host));
    sstrncpy (vl.plugin, "wmi", sizeof (vl.plugin));

    sstrncpy (vl.plugin_instance, q->plugin_instance->base_name, sizeof (vl.plugin_instance));

    results = wmi_query (wmi, q->statement);

    if (results->count == 0)
    {
        WARNING ("wmi warning: There are no results for query %ls.",
                 q->statement);
        wmi_result_list_release (results);
        return (0);
    }

    wmi_result_t *result;
    while ((result = wmi_get_next_result (results)))
    {
        LIST_TYPE(wmi_metric_t) *mn;
        for (mn = q->metrics; mn != NULL; mn = LIST_NEXT(mn))
        {
            value_t *values;
            const data_set_t *ds;
            int i;
            VARIANT v;
            wmi_metric_t *m = LIST_NODE(mn);

            /* Getting values */
            values = calloc (m->values_num, sizeof (value_t));
            ds = plugin_get_ds (m->typename);
            for (i = 0; i < m->values_num; i++)
            {
                int index_in_ds;
                wmi_result_get_value (result, m->values[i].source, &v);

                index_in_ds = find_index_in_ds (ds, m->values[i].dest);
                if (index_in_ds != -1)
                    store (&v, &values[i], ds->ds[index_in_ds].type);
                else
                    WARNING ("wmi warning: Cannot find field %s in type %s.",
                            m->values[i].dest, ds->type);
            }
            vl.values_len = m->values_num;
            vl.values = values;

            vl.type_instance[0] = '\0';
            append_metadata_string (vl.type_instance, sizeof (vl.type_instance),
                    m->type_instance, result);

            append_metadata_string (vl.plugin_instance, sizeof (vl.plugin_instance),
                    m->plugin_instance, result);

            sstrncpy (vl.type, m->typename, sizeof (vl.type));

            plugin_dispatch_values (&vl);
            free (values);
        }
        wmi_result_release (result);
    }
    wmi_result_list_release (results);

    return (0);
}

static int wmi_read (void)
{
    LIST_TYPE(plugin_instance_t) *pn;
    for (pn = plugin_instances_g; pn != NULL; pn = LIST_NEXT(pn))
    {
        plugin_instance_t *pi = LIST_NODE(pn);

        LIST_TYPE(wmi_query_t) *qn;
        for (qn = pi->queries; qn != NULL; qn = LIST_NEXT(qn))
        {
            int status = wmi_exec_query (LIST_NODE (qn));
            if (status)
                return (status);
        }
    }
    return (0);
}

static int wmi_configure_wrapper (oconfig_item_t *ci)
{
    // TODO: change it to multiple read callback registrations,
    // one per instance
    return (wmi_configure (ci, &plugin_instances_g));
}

void module_register (void)
{
    plugin_register_complex_config ("wmi", wmi_configure_wrapper);
    plugin_register_init ("wmi", wmi_init);
    plugin_register_read ("wmi", wmi_read);
    plugin_register_shutdown ("wmi", wmi_shutdown);
}
