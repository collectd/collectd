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

#include <collectd.h>
#include <common.h>
#include <plugin.h>
#include <utils_format_json.h>

#include <amqp.h>
#include <amqp_framing.h>

#define PLUGIN_NAME "amqp"

static int  port;
static char *host       = NULL;
static char *vhost      = NULL;
static char *user       = NULL;
static char *password   = NULL;
static char *exchange   = NULL;
static char *routingkey = NULL;

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

static void config_free(char *var)
{
    if (var != NULL)
        free(var);
}

static int config_set(char **var, const char *value)
{
    config_free(*var);
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
        port = atoi(value);
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

static int amqp_write(const data_set_t *ds, const value_list_t *vl, user_data_t *user_data)
{
    int error;
    int sockfd;
    size_t bfree;
    size_t bfill;
    char buffer[4096];
    amqp_rpc_reply_t reply;
    amqp_connection_state_t conn;
    amqp_basic_properties_t props;

    conn = amqp_new_connection();
    if ((sockfd = amqp_open_socket(host, port)) < 0)
    {
        amqp_destroy_connection(conn);
        return (1);
    }
    amqp_set_sockfd(conn, sockfd);
    reply = amqp_login(conn, vhost, 0, 131072, 0, AMQP_SASL_METHOD_PLAIN, user, password);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        amqp_destroy_connection(conn);
        close(sockfd);
        return (1);
    }
    amqp_channel_open(conn, 1);
    if (amqp_rpc_reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(conn);
        close(sockfd);
        return (1);
    }
    error = 0;
    memset(buffer, 0, sizeof(buffer));
    bfree = sizeof(buffer);
    bfill = 0;
    format_json_initialize(buffer, &bfill, &bfree);
    format_json_value_list(buffer, &bfill, &bfree, ds, vl);
    format_json_finalize(buffer, &bfill, &bfree);
    props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
    props.content_type = amqp_cstring_bytes("application/json");
    props.delivery_mode = 2; // persistent delivery mode
    error = amqp_basic_publish(conn,
                1,
                amqp_cstring_bytes(exchange),
                amqp_cstring_bytes(routingkey),
                0,
                0,
                &props,
                amqp_cstring_bytes(buffer));
    reply = amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
        error = 1;
    reply = amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
        error = 1;
    amqp_destroy_connection(conn);
    if (close(sockfd) < 0)
        error = 1;
    return (error);
}

static int shutdown(void)
{
    config_free(host);
    config_free(vhost);
    config_free(user);
    config_free(password);
    config_free(exchange);
    config_free(routingkey);
    return (0);
}

void module_register(void)
{
    plugin_register_config(PLUGIN_NAME, config, config_keys, config_keys_num);
    plugin_register_write(PLUGIN_NAME, amqp_write, NULL);
    plugin_register_shutdown(PLUGIN_NAME, shutdown);
}

