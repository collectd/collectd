/*
**
** collectd-amqp
** Copyright (c) <2009> <sebastien.pahl@dotcloud.com>
**
** Permission is hereby granted, free of charge, to any person
** obtaining a copy of this software and associated documentation
** files (the "Software"), to deal in the Software without
** restriction, including without limitation the rights to use,
** copy, modify, merge, publish, distribute, sublicense, and/or sell
** copies of the Software, and to permit persons to whom the
** Software is furnished to do so, subject to the following
** conditions:
**
** The above copyright notice and this permission notice shall be
** included in all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
** OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
** NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
** HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
** WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
** OTHER DEALINGS IN THE SOFTWARE.
**
*/

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <pthread.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_format_json.h"

#include <amqp.h>
#include <amqp_framing.h>

static int  port;
static char *host       = NULL;
static char *vhost      = NULL;
static char *user       = NULL;
static char *password   = NULL;
static char *exchange   = NULL;
static char *routingkey = NULL;

static amqp_connection_state_t amqp_conn = NULL;
static pthread_mutex_t amqp_conn_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *config_keys[] =
{
    "Host",
    "Port",
    "VHost",
    "User",
    "Password",
    "Exchange",
    "RoutingKey"
};

static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static int config_set(char **var, const char *value)
{
    sfree(*var);
    if ((*var = strdup(value)) == NULL)
        return (1);
    return (0);
}

static int config(const char *key, const char *value)
{
    if (strcasecmp(key, "host") == 0)
        return (config_set(&host, value));
    else if(strcasecmp(key, "port") == 0)
    {
        int tmp;

        tmp = service_name_to_port_number (value);
        if (tmp <= 0)
        {
            ERROR ("AMQP plugin: Cannot parse `%s' as a "
                    "service name (port number).", value);
            return (1);
        }

        port = tmp;
        return (0);
    }
    else if (strcasecmp(key, "vhost") == 0)
        return (config_set(&vhost, value));
    else if (strcasecmp(key, "user") == 0)
        return (config_set(&user, value));
    else if (strcasecmp(key, "password") == 0)
        return (config_set(&password, value));
    else if (strcasecmp(key, "exchange") == 0)
        return (config_set(&exchange, value));
    else if (strcasecmp(key, "routingkey") == 0)
        return (config_set(&routingkey, value));
    return (-1);
}

static int amqp_write_locked (const char *buffer)
{
    amqp_rpc_reply_t reply;
    amqp_basic_properties_t props;
    int status;

    if (amqp_conn == NULL)
    {
        int sockfd;

        amqp_conn = amqp_new_connection ();
        if (amqp_conn == NULL)
        {
            ERROR ("amqp plugin: amqp_new_connection failed.");
            return (ENOMEM);
        }

        sockfd = amqp_open_socket (host, port);
        if (sockfd < 0)
        {
            char errbuf[1024];
            status = (-1) * sockfd;
            ERROR ("amqp plugin: amqp_open_socket failed: %s",
                    sstrerror (status, errbuf, sizeof (errbuf)));
            amqp_destroy_connection(amqp_conn);
            amqp_conn = NULL;
            return (status);
        }

        amqp_set_sockfd (amqp_conn, sockfd);

        reply = amqp_login(amqp_conn, vhost,
                /* channel max = */      0,
                /* frame max = */   131072,
                /* heartbeat = */        0,
                /* authentication: */ AMQP_SASL_METHOD_PLAIN, user, password);
        if (reply.reply_type != AMQP_RESPONSE_NORMAL)
        {
            ERROR ("amqp plugin: amqp_login (vhost = %s, user = %s) failed.",
                    vhost, user);
            amqp_destroy_connection(amqp_conn);
            close(sockfd);
            amqp_conn = NULL;
            return (1);
        }

        amqp_channel_open (amqp_conn, /* channel = */ 1);
        /* FIXME: Is checking "reply.reply_type" really correct here? How does
         * it get set? --octo */
        if (reply.reply_type != AMQP_RESPONSE_NORMAL)
        {
            ERROR ("amqp plugin: amqp_channel_open failed.");
            amqp_connection_close (amqp_conn, AMQP_REPLY_SUCCESS);
            amqp_destroy_connection(amqp_conn);
            close(sockfd);
            amqp_conn = NULL;
            return (1);
        }

        INFO ("amqp plugin: Successfully opened connection to vhost \"%s\" "
                "on %s:%i.", vhost, host, port);
    } /* if (amqp_conn == NULL) */

    props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
    props.content_type = amqp_cstring_bytes("application/json");
    props.delivery_mode = 2; /* persistent delivery mode */

    status = amqp_basic_publish(amqp_conn,
                /* channel = */ 1,
                amqp_cstring_bytes(exchange),
                amqp_cstring_bytes(routingkey),
                /* mandatory = */ 0,
                /* immediate = */ 0,
                &props,
                amqp_cstring_bytes(buffer));
    if (status != 0)
    {
        int sockfd;

        ERROR ("amqp plugin: amqp_basic_publish failed with status %i.",
                status);

        sockfd = amqp_get_sockfd (amqp_conn);
        amqp_channel_close (amqp_conn, 1, AMQP_REPLY_SUCCESS);
        amqp_connection_close (amqp_conn, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection (amqp_conn);
        close(sockfd);
        amqp_conn = NULL;
    }

    return (status);
} /* int amqp_write_locked */

static int amqp_write (const data_set_t *ds, const value_list_t *vl,
        __attribute__((unused)) user_data_t *user_data)
{
    char buffer[4096];
    size_t bfree;
    size_t bfill;
    int status;

    if ((ds == NULL) || (vl == NULL))
        return (EINVAL);

    memset (buffer, 0, sizeof (buffer));
    bfree = sizeof (buffer);
    bfill = 0;

    format_json_initialize(buffer, &bfill, &bfree);
    /* TODO: Possibly add a config option "StoreRates" and pass the value along here. */
    format_json_value_list(buffer, &bfill, &bfree, ds, vl, /* rates = */ 0);
    format_json_finalize(buffer, &bfill, &bfree);

    pthread_mutex_lock (&amqp_conn_lock);
    status = amqp_write_locked (buffer);
    pthread_mutex_unlock (&amqp_conn_lock);

    return (status);
} /* int amqp_write */

static int shutdown(void)
{
    pthread_mutex_lock (&amqp_conn_lock);
    if (amqp_conn != NULL)
    {
        int sockfd;

        sockfd = amqp_get_sockfd (amqp_conn);
        amqp_channel_close (amqp_conn, 1, AMQP_REPLY_SUCCESS);
        amqp_connection_close (amqp_conn, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection (amqp_conn);
        close(sockfd);
        amqp_conn = NULL;
    }
    pthread_mutex_unlock (&amqp_conn_lock);

    sfree(host);
    sfree(vhost);
    sfree(user);
    sfree(password);
    sfree(exchange);
    sfree(routingkey);

    return (0);
}

void module_register(void)
{
    plugin_register_config("amqp", config, config_keys, config_keys_num);
    plugin_register_write("amqp", amqp_write, NULL);
    plugin_register_shutdown("amqp", shutdown);
}

/* vim: set sw=4 sts=4 et : */
