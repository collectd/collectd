/**
 * collectd - src/stenographer.c
 * Copyright (C) 2015       Google
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
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include <curl/curl.h>

struct stenographer_s
{
   char *url;
   char *name;
   char *host;
   char *cert;
   char *key;
   char *cacert;
   char *stenographer_buffer;
   char stenographer_curl_error[CURL_ERROR_SIZE];
   size_t stenographer_buffer_size;
   size_t stenographer_buffer_fill;
   CURL *curl;
}; /* stenographer_s */

typedef struct stenographer_s stenographer_t;

static int stenographer_read_host (user_data_t *user_data);
 
static void stenographer_free (stenographer_t *st)
{
    if (st == NULL)
        return;

    sfree (st->url);
    sfree (st->name);
    sfree (st->host);
    sfree (st->cert);
    sfree (st->key);
    sfree (st->cacert);
    sfree (st->stenographer_buffer);
    if (st->curl) {
        curl_easy_cleanup(st->curl);
        st->curl = NULL;
    }
} /* stenographer_free */

static size_t stenographer_curl_callback (void *buf, size_t size, size_t nmemb,
        void *user_data)
{
    size_t len = size * nmemb;
    stenographer_t *st;

    st = user_data;
    if (st == NULL)
    {
        ERROR ("stenographer plugin: stenographer_curl_callback: "
                "user_data pointer is NULL.");
        return (0);
    }

    if (len <= 0)
        return (len);

    if ((st->stenographer_buffer_fill + len) >= st->stenographer_buffer_size)
    {
        char *temp;

        temp = (char *) realloc (st->stenographer_buffer,
                st->stenographer_buffer_fill + len + 1);
        if (temp == NULL)
        {
            ERROR ("stenographer plugin: realloc failed.");
            return (0);
        }
        st->stenographer_buffer = temp;
        st->stenographer_buffer_size = st->stenographer_buffer_fill + len + 1;
    }

    memcpy (st->stenographer_buffer + st->stenographer_buffer_fill, (char *) buf, len);
    st->stenographer_buffer_fill += len;
    st->stenographer_buffer[st->stenographer_buffer_fill] = 0;

    return (len);
} /* int stenographer_curl_callback */

/* Configuration handling functiions
 * <Plugin stenographer>
 *   <Instance "instance_name">
 *     URL ...
 *   </Instance>
 *   URL ...
 * </Plugin>
 */
static int config_add (oconfig_item_t *ci)
{
    stenographer_t *st;
    int i;
    int status;

    st = malloc (sizeof (*st));
    if (st == NULL)
    {
        ERROR ("stenographer plugin: malloc failed.");
        return (-1);
    }
    memset (st, 0, sizeof (*st));

    status = cf_util_get_string (ci, &st->name);
    if (status != 0)
    {
        sfree (st);
        return (status);
    }
    assert (st->name != NULL);

    for (i = 0; i < ci->children_num; i++)
    {   
        oconfig_item_t *child = ci->children + i; 
        
        if (strcasecmp ("URL", child->key) == 0)
            status = cf_util_get_string (child, &st->url);
        else if (strcasecmp ("Host", child->key) == 0)
            status = cf_util_get_string (child, &st->host);
        else if (strcasecmp ("Cert", child->key) == 0)
            status = cf_util_get_string (child, &st->cert);
        else if (strcasecmp ("Key", child->key) == 0)
            status = cf_util_get_string (child, &st->key);
        else if (strcasecmp ("CACert", child->key) == 0)
            status = cf_util_get_string (child, &st->cacert);
        else        
        {   
            WARNING ("stenographer plugin: Option `%s' not allowed here.",
                    child->key);
            status = -1;
        }   
    
        if (status != 0)
            break;
    } 

    /* Check if struct is complete.. */
    if ((status == 0) && (st->url == NULL))
    {
        ERROR ("stenographer plugin: Instance `%s': "
                "No URL has been configured.",
                st->name);
        status = -1;
    }

    if (status == 0)
    {
        user_data_t ud;
        char callback_name[3*DATA_MAX_NAME_LEN];

        memset (&ud, 0, sizeof (ud));
        ud.data = st;
        ud.free_func = (void *) stenographer_free;

        memset (callback_name, 0, sizeof (callback_name));
        ssnprintf (callback_name, sizeof (callback_name),
                "stenographer/%s/%s",
                (st->host != NULL) ? st->host : hostname_g,
                (st->name != NULL) ? st->name : "default"),

        status = plugin_register_complex_read (/* group = */ NULL,
                /* name      = */ callback_name,
                /* callback  = */ stenographer_read_host,
                /* interval  = */ NULL,
                /* user_data = */ &ud);
    }

    if (status != 0)
    {
        stenographer_free(st);
        return (-1);
    }

    return (0);
} /* int config_add */

static int config (oconfig_item_t *ci)
{
    int status = 0;
    int i;

    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;

        if (strcasecmp ("Instance", child->key) == 0)
            config_add (child);
        else
            WARNING ("stenographer plugin: The configuration option "
                    "\"%s\" is not allowed here. Did you "
                    "forget to add an <Instance /> block "
                    "around the configuration?",
                    child->key);
    } /* for (ci->children) */

    return (status);
} /* int config */

/* initialize curl for each host */
static int init_host (stenographer_t *st) /* {{{ */
{
    assert (st->url != NULL);
    /* (Assured by `config_add') */

    if (st->curl != NULL)
    {
        curl_easy_cleanup (st->curl);
        st->curl = NULL;
    }

    if ((st->curl = curl_easy_init ()) == NULL)
    {
        ERROR ("stenographer plugin: init_host: `curl_easy_init' failed.");
        return (-1);
    }

    curl_easy_setopt (st->curl, CURLOPT_ERRORBUFFER, st->stenographer_curl_error);
    curl_easy_setopt (st->curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt (st->curl, CURLOPT_WRITEFUNCTION, stenographer_curl_callback);
    curl_easy_setopt (st->curl, CURLOPT_WRITEDATA, st);
    curl_easy_setopt (st->curl, CURLOPT_URL, st->url);
    curl_easy_setopt (st->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt (st->curl, CURLOPT_MAXREDIRS, 50L);
    curl_easy_setopt (st->curl, CURLOPT_SSLCERT, st->cert);
    curl_easy_setopt (st->curl, CURLOPT_SSLKEY, st->key);
    curl_easy_setopt (st->curl, CURLOPT_CAINFO, st->cacert);
    curl_easy_setopt (st->curl, CURLOPT_TCP_KEEPALIVE, 1L);

    return (0);
} /* }}} int init_host */

static void submit_value (const char *type, const char *type_instance,
        value_t value, stenographer_t *st)
{
    value_list_t vl = VALUE_LIST_INIT;

    vl.values = &value;
    vl.values_len = 1;

    sstrncpy (vl.host, (st->host != NULL) ? st->host : hostname_g,
            sizeof (vl.host));

    sstrncpy (vl.plugin, "stenographer", sizeof (vl.plugin));
    if (st->name != NULL)
        sstrncpy (vl.plugin_instance, st->name,
                sizeof (vl.plugin_instance));

    sstrncpy (vl.type, type, sizeof (vl.type));
    if (type_instance != NULL)
        sstrncpy (vl.type_instance, type_instance,
                sizeof (vl.type_instance));
    plugin_dispatch_values (&vl);
} /* void submit_value */

static void submit_counter (const char *type, const char *type_instance,
        counter_t c, stenographer_t *st)
{
    value_t v;
    v.counter = c;
    submit_value (type, type_instance, v, st);
} /* void submit_counter */

static int stenographer_read_host (user_data_t *user_data) /* {{{ */
{
    int  lines_num = 0;
    int  curl_lines = 21; // number of steno stats in types.db
    int  num_stat_fields = 2; // key with tab seperated  value 
    char *line_ptr;
    char *line_save_ptr;
    char *stat_save_ptr;
    char *lines[curl_lines];

    stenographer_t *st;
    st = user_data->data;
    assert (st->url != NULL);

    if (st->curl == NULL)
    {
        int status;

        status = init_host (st);
        if (status != 0)
            return (-1);
    }
    
    assert (st->curl != NULL);
    st->stenographer_buffer_fill = 0;

    // curl stats from stenographer server
    if (curl_easy_perform (st->curl) != CURLE_OK)
    {
        ERROR ("stenographer: curl_easy_perform failed: %s",
                st->stenographer_curl_error);
        return (-1);
    }

    // parse stats
    line_ptr = st->stenographer_buffer;
    line_save_ptr = NULL;

    while ((lines[lines_num] = strtok_r (line_ptr, "\n", &line_save_ptr)) != NULL)
    {
        line_ptr = NULL;
        char *stat[num_stat_fields];
        int stat_num = 0;
        stat_save_ptr = NULL;
        while((stat[stat_num] = strtok_r(lines[lines_num], "\t", &stat_save_ptr)) != NULL)
        {
           lines[lines_num] = NULL;

           stat_num++;
           if (stat_num >= num_stat_fields)
               break;
        }

        char *prefix = "stenographer_";
        int titleSize = strlen(prefix) + strlen(stat[0]) + 1;
        char *title = (char *)malloc(titleSize);
        if (title == NULL)
        {
            ERROR("stenographer plugin: prefix malloc failed.");
            return (-1);
        }
        strcpy(title, prefix);
        strcat(title, stat[0]);
        submit_counter(title, NULL, strtod(stat[1], NULL), st);
        free(title);

        lines_num++;
        if (lines_num >= curl_lines)
            break;
    }

    st->stenographer_buffer_fill = 0;
    return (0);
} /* }}} int stenographer_read_host */


static int stenographer_init (void) /* {{{ */
{
    /* Call this while collectd is still single-threaded to avoid
     * initialization issues in libgcrypt. */
    curl_global_init (CURL_GLOBAL_SSL);
    return (0);
}  /* }}} int stenographer_init */

void module_register (void)
{
    plugin_register_complex_config ("stenographer", config);
    plugin_register_init ("stenographer", stenographer_init);
} /* void module_register */
