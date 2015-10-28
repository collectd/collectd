#include <stdbool.h>

#include "config.h"
#include "plugin.h"
#include "utils_format_influxdb.h"
#include "utils_cache.h"
#include "configfile.h"
#include "utils_curl.h"


struct node_s {
    char *name;

    char *host;
    int port;
    char *db;
    char *url;
    char *username;
    char *password;

    bool store_rates;
    influxdb_attrs_t *attrs;
    int bufsize;
    bool int_as_float;
    int timeout;

    buffer_t buf;
    cdtime_t oldest;

    pthread_mutex_t mutex;

    struct node_s *next;
};
typedef struct node_s node_t;


node_t default_config = {
    .bufsize = 65536,
    .timeout = 10000,
};


static node_t *nodes = NULL;
static curl_reactor_t *curl_reactor = NULL;
#if HAVE_CURLMOPT_MAXCONNECTS
static int maxconnects = 0;
#endif
#if HAVE_CURLMOPT_MAX_HOST_CONNECTIONS
static int max_host_connects = 4;
#endif


struct values {
    const data_set_t *ds;
    const value_list_t *vl;
    gauge_t *rates;
    node_t *node;
};


struct curl_xfer {
    const node_t *node;
    char *buf;
    char errbuf[CURL_ERROR_SIZE];
    buffer_t reply_buf;
};


static int get_count (const struct values *v);
static int get_type (const struct values *v, unsigned i);
static value_t get_value (const struct values *v, unsigned i);
static bool value_is_nan (const struct values *v, int i);
static bool has_values (const struct values *v, int i);

static int influxdb_put_value (buffer_t *buf, int type, value_t value, bool int_as_float);
static int influxdb_put_field (buffer_t *buf, const char *name, int type, value_t value, bool int_as_float);
static int influxdb_format_line (buffer_t *buf, const struct values *v, int i);

static size_t curl_write_cb (char *ptr, size_t size, size_t nmemb, void *userdata);
static void influxdb_write_buffer (const node_t *node, char *buf, size_t bufsize, bool *attend_curl);
static void curl_callback (CURL *curl, CURLcode result);
static void influxdb_attend_curl (void);
static void influxdb_submit_line (const struct values *v, int i, bool *attend_curl);
static void influxdb_submit_ds (struct values *v, bool *attend_curl);

static void influxdb_flush_node (node_t *node, cdtime_t ts, bool *attend_curl);

static influxdb_attrs_t *influxdb_create_default_attrs (void);
static int influxdb_config_nodeparam (oconfig_item_t *child, node_t *node);
static int influxdb_config_check_node (const node_t *node);
static int influxdb_config_node (oconfig_item_t *ci);
static int influxdb_config (oconfig_item_t *ci);

static int influxdb_init_node (node_t *node);
static int influxdb_init (void);

static int influxdb_write (const data_set_t *ds, const value_list_t *vl, user_data_t *data);
static int influxdb_flush (cdtime_t timeout, const char *identifier, user_data_t *data);
void module_register (void);


static int
get_count (const struct values *v)
{
    if (v->node->store_rates)
        return v->ds->ds_num;
    else
        return v->vl->values_len;
}


static int
get_type (const struct values *v, unsigned i)
{
    if (v->node->store_rates)
        return DS_TYPE_GAUGE;
    else
        return v->ds->ds[i].type;
}


static value_t
get_value (const struct values *v, unsigned i)
{
    if (v->node->store_rates)
        return (value_t) {.gauge = v->rates[i]};
    else
        return v->vl->values[i];
}


static int
influxdb_put_value (buffer_t *buf, int type, value_t value, bool int_as_float)
{
    const char *const int_suffix = int_as_float ? ".0" : "i";

    switch (type) {
        case DS_TYPE_COUNTER:
            return buffer_printf (buf, "%llu%s", (unsigned long long) value.counter, int_suffix);
        case DS_TYPE_GAUGE:
            return buffer_printf (buf, "%.15e", (double) value.gauge);
        case DS_TYPE_DERIVE:
            return buffer_printf (buf, "%lld%s", (long long) value.counter, int_suffix);
        case DS_TYPE_ABSOLUTE:
            return buffer_printf (buf, "%lld%s", (unsigned long long) value.absolute, int_suffix);
        default:
            return -1;
    }
}


static int
influxdb_put_field (buffer_t *buf, const char *name, int type, value_t value, bool int_as_float)
{
    const size_t orig_pos = buffer_getpos (buf);

    if (buffer_putstr (buf, name) < 0 || buffer_putc (buf, '=') < 0)
        goto fail;
    if (influxdb_put_value (buf, type, value, int_as_float) < 0)
        goto fail;

    return buffer_getpos (buf) - orig_pos;

fail:
    buffer_setpos (buf, orig_pos);
    return -1;
}


static bool
value_is_nan (const struct values *v, int i)
{
    return get_type (v, i) == DS_TYPE_GAUGE && isnan (get_value (v, i).gauge);
}


static int
influxdb_format_line (buffer_t *buf, const struct values *v, int i)
{
    const size_t orig_pos = buffer_getpos (buf);

    if (influxdb_attrs_format (buf, v->node->attrs, v->vl, v->ds->ds[i].name) < 0)
        goto fail;

    if (buffer_putc (buf, ' ') < 0)
        goto fail;

    if (i >= 0) {
        if (influxdb_put_field (buf, "value",
                get_type (v, i), get_value (v, i),
                v->node->int_as_float) < 0)
            goto fail;
    } else {
        bool initial = true;
        for (i = 0; i < get_count (v); i++) {
            if (value_is_nan (v, i))
                continue;

            if (initial)
                initial = false;
            else if (buffer_putc (buf, ',') < 0)
                goto fail;

            if (influxdb_put_field (buf, v->ds->ds[i].name,
                    get_type (v, i), get_value (v, i),
                    v->node->int_as_float) < 0)
                goto fail;
        }
    }

    if (buffer_printf (buf, " %llu\n", (unsigned long long) CDTIME_T_TO_NS (v->vl->time)) < 0)
        goto fail;

    return buffer_getpos (buf) - orig_pos;

fail:
    buffer_setpos (buf, orig_pos);
    return -1;
}


static size_t
curl_write_cb (char *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct curl_xfer *const xfer = userdata;
    const size_t buf_bytes = buffer_space_left (&xfer->reply_buf);
    size_t bytes = size * nmemb;

    if (bytes > buf_bytes)
        bytes = buf_bytes;
    if (bytes != 0)
        buffer_putmem (&xfer->reply_buf, ptr, bytes);

    return nmemb;
}


static void
influxdb_write_buffer (const node_t *node, char *buf, size_t bufsize, bool *attend_curl)
{
    struct curl_xfer *xfer = malloc (sizeof (*xfer));
    if (xfer == NULL) {
        ERROR ("write_influxdb: out of memory, dropping buffer");
        free (buf);
        return;
    }

    if (buffer_init (&xfer->reply_buf, NULL, 64, 1024) < 0)
        ERROR ("write_influxdb: failed to init reply buffer");

    xfer->node = node;
    xfer->buf = buf;

    CURL *curl = curl_easy_init ();
    if (curl == NULL) {
        ERROR ("write_influxdb: Failed to get CURL handle.");
        free (xfer->buf);
        free (xfer);
        return;
    }

    curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, xfer->errbuf);
    curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt (curl, CURLOPT_WRITEDATA, xfer);
    curl_easy_setopt (curl, CURLOPT_URL, node->url);
    curl_easy_setopt (curl, CURLOPT_POSTFIELDS, buf);
    curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, bufsize);

#ifdef HAVE_CURLOPT_TIMEOUT_MS
    curl_easy_setopt (curl, CURLOPT_TIMEOUT_MS, node->timeout);
#else
    curl_easy_setopt (curl, CURLOPT_TIMEOUT, (node->timeout + 500) / 1000);
#endif

#ifdef HAVE_CURLOPT_USERNAME
    if (node->username != NULL) {
        curl_easy_setopt (curl, CURLOPT_USERNAME, node->username);
        curl_easy_setopt (curl, CURLOPT_PASSWORD, node->password);
    }
#endif

    curl_reactor_add (curl_reactor, curl, curl_callback, xfer, attend_curl);
}


static void
curl_callback (CURL *curl, CURLcode result)
{
    struct curl_xfer *xfer = NULL;
    curl_easy_getinfo (curl, CURLINFO_PRIVATE, (char **) &xfer);

    if (result != CURLE_OK) {
        ERROR ("write_influxdb: %s: %s: %s", xfer->node->name,
                curl_easy_strerror (result), xfer->errbuf);
    } else {
        long rcode;
        curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &rcode);

        if (rcode >= 300)
            ERROR ("write_influxdb: %s: HTTP error %ld%s%s",
                xfer->node->name, rcode,
                (buffer_getpos (&xfer->reply_buf) != 0) ? ": " : "",
                buffer_getstr (&xfer->reply_buf));
    }

    buffer_clear (&xfer->reply_buf);
    free (xfer->buf);
    free (xfer);
    curl_easy_cleanup (curl);
}


static void
influxdb_attend_curl (void)
{
    curl_reactor_run (curl_reactor);
}


static bool
has_values (const struct values *v, int i)
{
    if (i >= 0)
        return !value_is_nan (v, i);

    for (i = 0; i < get_count (v); i++)
        if (!value_is_nan (v, i))
            return true;
    return false;
}


static void
influxdb_submit_line (const struct values *v, int i, bool *attend_curl)
{
    node_t *const node = v->node;

    if (!has_values (v, i))
        return; /* nothing to do, it's all NaN */

    int rc = pthread_mutex_lock (&node->mutex);
    if (rc != 0) {
        ERROR ("write_influxdb: pthread_mutex_lock: %s", strerror (rc));
        return;
    }

    if (node->oldest == 0 || node->oldest > v->vl->time)
        node->oldest = v->vl->time;

    if (influxdb_format_line (&node->buf, v, i) >= 0)
        goto unlock;

    /*
     * The current buffer didn't have enough space for the line.
     * Cycle it for a fresh one and retry. If the line doesn't
     * even fit into an empty buffer, it's simply too big and we
     * drop it.
     * Keep a pointer the old buffer around to send after we release
     * the mutex so we don't block everything if submission is slow.
     */

    char *oldbuf;
    size_t bufsize;
    if (buffer_cycle (&node->buf, &oldbuf, &bufsize) < 0)
        goto unlock;

    if (influxdb_format_line (&node->buf, v, i) < 0)
        ERROR ("write_influxdb: Cannot fit line in buffer, dropping.");

    pthread_mutex_unlock (&node->mutex);

    influxdb_write_buffer (node, oldbuf, bufsize, attend_curl);
    return;

unlock:
    pthread_mutex_unlock (&node->mutex);
}


static void
influxdb_flush_node (node_t *node, cdtime_t ts, bool *attend_curl)
{
    pthread_mutex_lock (&node->mutex);

    char *oldbuf = NULL;
    size_t bufsize = 0;
    int rc = -1;

    if (node->oldest != 0 && (ts == 0 || node->oldest < ts))
        rc = buffer_cycle (&node->buf, &oldbuf, &bufsize);

    pthread_mutex_unlock (&node->mutex);

    if (rc >= 0) /* cycle was done and successful */
        influxdb_write_buffer (node, oldbuf, bufsize, attend_curl);
    else
        ERROR ("write_influxdb: out of memory");
}


static void
influxdb_submit_ds (struct values *v, bool *attend_curl)
{
    node_t *const node = v->node;

    if (!(influxdb_attrs_flags (node->attrs) & INFLUXDB_FORMAT_HAS_FIELDNAME)) {
        influxdb_submit_line (v, -1, attend_curl);
    } else {
        int i;
        for (i = 0; i < v->vl->values_len; i++)
            influxdb_submit_line (v, i, attend_curl);
    }
}


static influxdb_attrs_t *
influxdb_create_default_attrs (void)
{
    influxdb_attrs_t *attrs = influxdb_attrs_create ("%p_%f");
    if (attrs == NULL)
        return NULL;

    if (influxdb_attrs_add (attrs, "host", "%h") < 0)
        goto fail;
    if (influxdb_attrs_add (attrs, "instance", "%i") < 0)
        goto fail;
    if (influxdb_attrs_add (attrs, "type", "%t") < 0)
        goto fail;
    if (influxdb_attrs_add (attrs, "type_instance", "%j") < 0)
        goto fail;

    return attrs;

fail:
    influxdb_attrs_free (attrs);
    return NULL;
}


static int
influxdb_config_nodeparam (oconfig_item_t *child, node_t *node)
{
    if (strcasecmp (child->key, "host") == 0)
        cf_util_get_string (child, &node->host);
    else if (strcasecmp (child->key, "port") == 0)
        cf_util_get_int (child, &node->port);
    else if (strcasecmp (child->key, "database") == 0)
        cf_util_get_string (child, &node->db);
    else if (strcasecmp (child->key, "url") == 0)
        cf_util_get_string (child, &node->url);
    else if (strcasecmp (child->key, "username") == 0)
        cf_util_get_string (child, &node->username);
    else if (strcasecmp (child->key, "password") == 0)
        cf_util_get_string (child, &node->password);
    else if (strcasecmp (child->key, "format") == 0)
        node->attrs = influxdb_config_format (child);
    else if (strcasecmp (child->key, "storerates") == 0)
        cf_util_get_boolean (child, &node->store_rates);
    else if (strcasecmp (child->key, "intasfloat") == 0)
        cf_util_get_boolean (child, &node->int_as_float);
    else if (strcasecmp (child->key, "requesttimeout") == 0)
        cf_util_get_int (child, &node->timeout);
    else if (strcasecmp (child->key, "buffersize") == 0)
        cf_util_get_int (child, &node->bufsize);
    else
        return -1;

    return 0;
}


static int
influxdb_config_check_node (const node_t *node)
{
    if (node->name == NULL) {
        ERROR ("write_influxdb: Must specify a name in <Node> block");
        return -1;
    }

    if (node->url != NULL) {
        if (node->host != NULL) {
            ERROR ("write_influxdb: Host cannot be given if URL is overridden");
            return -1;
        }

        if (node->port != 0) {
            ERROR ("write_influxdb: Port cannot be given if URL is overridden");
            return -1;
        }

        if (node->db != 0) {
            ERROR ("write_influxdb: Database cannot be given if URL is overridden");
            return -1;
        }
    } else {
        if (node->host == NULL) {
            ERROR ("write_influxdb: No host name given for node");
            return -1;
        }

        if (node->port < 0 || node->port > 65535) {
            ERROR ("write_influxdb: Invalid port given for node");
            return -1;
        }

        if (node->db == NULL) {
            ERROR ("write_influxdb: No database given for node");
            return -1;
        }
    }

    if (node->username != NULL && node->password == NULL) {
        ERROR ("write_influxdb: Username cannot be given without Password");
        return -1;
    }

    if (node->username == NULL && node->password != NULL) {
        ERROR ("write_influxdb: Password cannot be given without Username");
        return -1;
    }

    if (node->timeout <= 0) {
        ERROR ("write_influxdb: RequestTimeout must be a positive integer");
        return -1;
    }

    if (node->bufsize < 256) {
        ERROR ("write_influxdb: Buffer size must be at least 256 bytes");
        return -1;
    }

    return 0;
}


static int
influxdb_config_node (oconfig_item_t *ci)
{
    int i;

    node_t *node = malloc (sizeof (*node));
    if (node == NULL) {
        ERROR ("write_influxdb: out of memory");
        return -1;
    }
    *node = default_config;

    cf_util_get_string (ci, &node->name);

    for (i = 0; i < ci->children_num; i++) {
        oconfig_item_t *child = ci->children + i;
        if (influxdb_config_nodeparam (child, node) < 0)
            ERROR ("write_influxdb: Unknown config parameter: %s", child->key);
    }

    if (influxdb_config_check_node (node) < 0)
        return -1;

    node->next = nodes;
    nodes = node;

    char cb_name[256];
    ssnprintf (cb_name, sizeof (cb_name), "write_influxdb/%s", node->name);
    user_data_t ud = {node};
    plugin_register_write (cb_name, influxdb_write, &ud);
    plugin_register_flush (cb_name, influxdb_flush, &ud);

    return 0;
}


static int
influxdb_config (oconfig_item_t *ci)
{
    default_config.attrs = influxdb_create_default_attrs ();
    if (default_config.attrs == NULL) {
        ERROR ("write_influxdb: Failed to create default attribute descriptor");
        return -1;
    }

    int i;
    for (i = 0; i < ci->children_num; i++) {
        oconfig_item_t *child = ci->children + i;

        if (strcasecmp (child->key, "node") == 0)
            influxdb_config_node (child);
#if HAVE_CURLMOPT_MAXCONNECTS
        else if (strcasecmp (child->key, "maxconnections") == 0)
            cf_util_get_int (child, &maxconnects);
#endif
#if HAVE_CURLMOPT_MAX_HOST_CONNECTIONS
        else if (strcasecmp (child->key, "maxhostconnections") == 0)
            cf_util_get_int (child, &max_host_connects);
#endif
        else if (influxdb_config_nodeparam (child, &default_config) < 0)
            ERROR ("write_influxdb: Invalid config option: %s", child->key);
    }

#if HAVE_CURLMOPT_MAXCONNECTS
    if (maxconnects < 0) {
        ERROR ("write_influxdb: MaxConnections cannot be negative");
        return -1;
    }
#endif
#if HAVE_CURLMOPT_MAX_HOST_CONNECTIONS
    if (max_host_connects <= 0) {
        ERROR ("write_influxdb: MaxHostConnections must be positive");
        return -1;
    }
#endif

    return 0;
}


static int
influxdb_init_node (node_t *node)
{
    int rc = pthread_mutex_init (&node->mutex, NULL);
    if (rc != 0) {
        ERROR ("write_influxdb: pthread_mutex_init: %s", strerror (rc));
        return -1;
    }

    if (node->url == NULL) {
        if (node->port == 0)
            node->port = 8086;
        node->url = ssnprintf_alloc ("http://%s:%d/write?db=%s", node->host, node->port, node->db);
        if (node->url == NULL) {
            ERROR ("write_influxdb: out of memory");
            return -1;
        }
    }

    if (buffer_init (&node->buf, NULL, node->bufsize, node->bufsize) < 0) {
        ERROR ("write_influxdb: out of memory");
        return -1;
    }

    return 0;
}


static int
influxdb_init (void)
{
    //curl_global_init (CURL_GLOBAL_SSL);
    curl_reactor = curl_reactor_create ();
    if (curl_reactor == NULL)
        return -1;

    CURLM *curlm = curl_reactor_curlm (curl_reactor);
#if HAVE_CURLMOPT_PIPELINING
    curl_multi_setopt (curlm, CURLMOPT_PIPELINING, 1L);
#endif
#if HAVE_CURLMOPT_MAXCONNECTS
    if (maxconnects != 0)
        curl_multi_setopt (curlm, CURLMOPT_MAXCONNECTS, (long) maxconnects);
#endif
#if HAVE_CURLMOPT_MAX_HOST_CONNECTIONS
    if (max_host_connects != 0)
        curl_multi_setopt (curlm, CURLMOPT_MAX_HOST_CONNECTIONS, (long) max_host_connects);
#endif

    node_t *node;
    for (node = nodes; node != NULL; node = node->next) {
        if (influxdb_init_node (node) < 0)
            return -1;
    }

    return 0;
}


static int
influxdb_write (const data_set_t *ds, const value_list_t *vl, user_data_t *data)
{
    node_t *const node = data->data;

    struct values v = {ds, vl, NULL, node};
    if (node->store_rates) {
        v.rates = uc_get_rate (ds, vl);
        if (v.rates == NULL)
            return -1;
    }

    bool attend_curl = false;
    influxdb_submit_ds (&v, &attend_curl);

    if (v.rates != NULL)
        free (v.rates);

    if (attend_curl)
        influxdb_attend_curl ();

    return 0;
}


static int
influxdb_flush (cdtime_t timeout, const char *identifier, user_data_t *data)
{
    node_t *const node = data->data;
    bool attend_curl = false;
    cdtime_t ts = 0;

    if (timeout != 0)
        ts = cdtime () - timeout;

    influxdb_flush_node (node, ts, &attend_curl);

    if (attend_curl)
        influxdb_attend_curl ();

    return 0;
}


void
module_register (void)
{
    /* XXX this should be done in influxdb_init(), but that seems to
     * be too late. Looks like collectd is already multithreaded
     * at that stage and this screws things up. (This is partly
     * a guess but the only sensible explanation I have right now.
     */
    curl_global_init (CURL_GLOBAL_SSL);

    plugin_register_complex_config ("write_influxdb", influxdb_config);
    plugin_register_init ("write_influxdb", influxdb_init);
}
