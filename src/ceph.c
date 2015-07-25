/**
 * collectd - src/ceph.c
 * Copyright (C) 2011  New Dream Network
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
 *   Colin McCabe <cmccabe@alumni.cmu.edu>
 *   Dennis Zou <yunzou@cisco.com>
 *   Dan Ryder <daryder@cisco.com>
 **/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <yajl/yajl_parse.h>
#if HAVE_YAJL_YAJL_VERSION_H
#include <yajl/yajl_version.h>
#endif

#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <math.h>
#include <inttypes.h>

#define RETRY_AVGCOUNT -1

#if defined(YAJL_MAJOR) && (YAJL_MAJOR > 1)
# define HAVE_YAJL_V2 1
#endif

#define RETRY_ON_EINTR(ret, expr) \
    while(1) { \
        ret = expr; \
        if(ret >= 0) \
            break; \
        ret = -errno; \
        if(ret != -EINTR) \
            break; \
    }

/** Timeout interval in seconds */
#define CEPH_TIMEOUT_INTERVAL 1

/** Maximum path length for a UNIX domain socket on this system */
#define UNIX_DOMAIN_SOCK_PATH_MAX (sizeof(((struct sockaddr_un*)0)->sun_path))

/** Yajl callback returns */
#define CEPH_CB_CONTINUE 1
#define CEPH_CB_ABORT 0

#if HAVE_YAJL_V2
typedef size_t yajl_len_t;
#else
typedef unsigned int yajl_len_t;
#endif

/** Number of types for ceph defined in types.db */
#define CEPH_DSET_TYPES_NUM 3
/** ceph types enum */
enum ceph_dset_type_d
{
    DSET_LATENCY = 0,
    DSET_BYTES = 1,
    DSET_RATE = 2,
    DSET_TYPE_UNFOUND = 1000
};

/** Valid types for ceph defined in types.db */
const char * ceph_dset_types [CEPH_DSET_TYPES_NUM] =
                                   {"ceph_latency", "ceph_bytes", "ceph_rate"};

/******* ceph_daemon *******/
struct ceph_daemon
{
    /** Version of the admin_socket interface */
    uint32_t version;
    /** daemon name **/
    char name[DATA_MAX_NAME_LEN];

    /** Path to the socket that we use to talk to the ceph daemon */
    char asok_path[UNIX_DOMAIN_SOCK_PATH_MAX];

    /** Number of counters */
    int ds_num;
    /** Track ds types */
    uint32_t *ds_types;
    /** Track ds names to match with types */
    char **ds_names;

    /**
     * Keep track of last data for latency values so we can calculate rate
     * since last poll.
     */
    struct last_data **last_poll_data;
    /** index of last poll data */
    int last_idx;
};

/******* JSON parsing *******/
typedef int (*node_handler_t)(void *, const char*, const char*);

/** Track state and handler while parsing JSON */
struct yajl_struct
{
    node_handler_t handler;
    void * handler_arg;
    struct {
      char key[DATA_MAX_NAME_LEN];
      int key_len;
    } state[YAJL_MAX_DEPTH];
    int depth;
};
typedef struct yajl_struct yajl_struct;

enum perfcounter_type_d
{
    PERFCOUNTER_LATENCY = 0x4, PERFCOUNTER_DERIVE = 0x8,
};

/** Give user option to use default (long run = since daemon started) avg */
static int long_run_latency_avg = 0;

/**
 * Give user option to use default type for special cases -
 * filestore.journal_wr_bytes is currently only metric here. Ceph reports the
 * type as a sum/count pair and will calculate it the same as a latency value.
 * All other "bytes" metrics (excluding the used/capacity bytes for the OSD)
 * use the DERIVE type. Unless user specifies to use given type, convert this
 * metric to use DERIVE.
 */
static int convert_special_metrics = 1;

/** Array of daemons to monitor */
static struct ceph_daemon **g_daemons = NULL;

/** Number of elements in g_daemons */
static int g_num_daemons = 0;

/**
 * A set of data that we build up in memory while parsing the JSON.
 */
struct values_tmp
{
    /** ceph daemon we are processing data for*/
    struct ceph_daemon *d;
    /** track avgcount across counters for avgcount/sum latency pairs */
    uint64_t avgcount;
    /** current index of counters - used to get type of counter */
    int index;
    /** do we already have an avgcount for latency pair */
    int avgcount_exists;
    /**
     * similar to index, but current index of latency type counters -
     * used to get last poll data of counter
     */
    int latency_index;
    /**
     * values list - maintain across counters since
     * host/plugin/plugin instance are always the same
     */
    value_list_t vlist;
};

/**
 * A set of count/sum pairs to keep track of latency types and get difference
 * between this poll data and last poll data.
 */
struct last_data
{
    char ds_name[DATA_MAX_NAME_LEN];
    double last_sum;
    uint64_t last_count;
};

/******* network I/O *******/
enum cstate_t
{
    CSTATE_UNCONNECTED = 0,
    CSTATE_WRITE_REQUEST,
    CSTATE_READ_VERSION,
    CSTATE_READ_AMT,
    CSTATE_READ_JSON,
};

enum request_type_t
{
    ASOK_REQ_VERSION = 0,
    ASOK_REQ_DATA = 1,
    ASOK_REQ_SCHEMA = 2,
    ASOK_REQ_NONE = 1000,
};

struct cconn
{
    /** The Ceph daemon that we're talking to */
    struct ceph_daemon *d;

    /** Request type */
    uint32_t request_type;

    /** The connection state */
    enum cstate_t state;

    /** The socket we use to talk to this daemon */
    int asok;

    /** The amount of data remaining to read / write. */
    uint32_t amt;

    /** Length of the JSON to read */
    uint32_t json_len;

    /** Buffer containing JSON data */
    unsigned char *json;

    /** Keep data important to yajl processing */
    struct yajl_struct yajl;
};

static int ceph_cb_null(void *ctx)
{
    return CEPH_CB_CONTINUE;
}

static int ceph_cb_boolean(void *ctx, int bool_val)
{
    return CEPH_CB_CONTINUE;
}

static int
ceph_cb_number(void *ctx, const char *number_val, yajl_len_t number_len)
{
    yajl_struct *yajl = (yajl_struct*)ctx;
    char buffer[number_len+1];
    int i, latency_type = 0, result;
    char key[128];

    memcpy(buffer, number_val, number_len);
    buffer[sizeof(buffer) - 1] = 0;

    ssnprintf(key, yajl->state[0].key_len, "%s", yajl->state[0].key);
    for(i = 1; i < yajl->depth; i++)
    {
        if((i == yajl->depth-1) && ((strcmp(yajl->state[i].key,"avgcount") == 0)
                || (strcmp(yajl->state[i].key,"sum") == 0)))
        {
            if(convert_special_metrics)
            {
                /**
                 * Special case for filestore:JournalWrBytes. For some reason,
                 * Ceph schema encodes this as a count/sum pair while all
                 * other "Bytes" data (excluding used/capacity bytes for OSD
                 * space) uses a single "Derive" type. To spare further
                 * confusion, keep this KPI as the same type of other "Bytes".
                 * Instead of keeping an "average" or "rate", use the "sum" in
                 * the pair and assign that to the derive value.
                 */
                if((strcmp(yajl->state[i-1].key, "journal_wr_bytes") == 0) &&
                        (strcmp(yajl->state[i-2].key,"filestore") == 0) &&
                        (strcmp(yajl->state[i].key,"avgcount") == 0))
                {
                    DEBUG("ceph plugin: Skipping avgcount for filestore.JournalWrBytes");
                    yajl->depth = (yajl->depth - 1);
                    return CEPH_CB_CONTINUE;
                }
            }
            //probably a avgcount/sum pair. if not - we'll try full key later
            latency_type = 1;
            break;
        }
        strncat(key, ".", 1);
        strncat(key, yajl->state[i].key, yajl->state[i].key_len+1);
    }

    result = yajl->handler(yajl->handler_arg, buffer, key);

    if((result == RETRY_AVGCOUNT) && latency_type)
    {
        strncat(key, ".", 1);
        strncat(key, yajl->state[yajl->depth-1].key,
                yajl->state[yajl->depth-1].key_len+1);
        result = yajl->handler(yajl->handler_arg, buffer, key);
    }

    if(result == -ENOMEM)
    {
        ERROR("ceph plugin: memory allocation failed");
        return CEPH_CB_ABORT;
    }

    yajl->depth = (yajl->depth - 1);
    return CEPH_CB_CONTINUE;
}

static int ceph_cb_string(void *ctx, const unsigned char *string_val,
        yajl_len_t string_len)
{
    return CEPH_CB_CONTINUE;
}

static int ceph_cb_start_map(void *ctx)
{
    return CEPH_CB_CONTINUE;
}

static int
ceph_cb_map_key(void *ctx, const unsigned char *key, yajl_len_t string_len)
{
    yajl_struct *yajl = (yajl_struct*)ctx;

    if((yajl->depth+1)  >= YAJL_MAX_DEPTH)
    {
        ERROR("ceph plugin: depth exceeds max, aborting.");
        return CEPH_CB_ABORT;
    }

    char buffer[string_len+1];

    memcpy(buffer, key, string_len);
    buffer[sizeof(buffer) - 1] = 0;

    snprintf(yajl->state[yajl->depth].key, sizeof(buffer), "%s", buffer);
    yajl->state[yajl->depth].key_len = sizeof(buffer);
    yajl->depth = (yajl->depth + 1);

    return CEPH_CB_CONTINUE;
}

static int ceph_cb_end_map(void *ctx)
{
    yajl_struct *yajl = (yajl_struct*)ctx;

    yajl->depth = (yajl->depth - 1);
    return CEPH_CB_CONTINUE;
}

static int ceph_cb_start_array(void *ctx)
{
    return CEPH_CB_CONTINUE;
}

static int ceph_cb_end_array(void *ctx)
{
    return CEPH_CB_CONTINUE;
}

static yajl_callbacks callbacks = {
        ceph_cb_null,
        ceph_cb_boolean,
        NULL,
        NULL,
        ceph_cb_number,
        ceph_cb_string,
        ceph_cb_start_map,
        ceph_cb_map_key,
        ceph_cb_end_map,
        ceph_cb_start_array,
        ceph_cb_end_array
};

static void ceph_daemon_print(const struct ceph_daemon *d)
{
    DEBUG("ceph plugin: name=%s, asok_path=%s", d->name, d->asok_path);
}

static void ceph_daemons_print(void)
{
    int i;
    for(i = 0; i < g_num_daemons; ++i)
    {
        ceph_daemon_print(g_daemons[i]);
    }
}

static void ceph_daemon_free(struct ceph_daemon *d)
{
    int i = 0;
    for(; i < d->last_idx; i++)
    {
        sfree(d->last_poll_data[i]);
    }
    sfree(d->last_poll_data);
    d->last_poll_data = NULL;
    d->last_idx = 0;
    for(i = 0; i < d->ds_num; i++)
    {
        sfree(d->ds_names[i]);
    }
    sfree(d->ds_types);
    sfree(d->ds_names);
    sfree(d);
}

/**
 * Compact ds name by removing special characters and trimming length to
 * DATA_MAX_NAME_LEN if necessary
 */
static void compact_ds_name(char *source, char *dest)
{
    int keys_num = 0, i;
    char *save_ptr = NULL, *tmp_ptr = source;
    char *keys[16];
    char len_str[3];
    char tmp[DATA_MAX_NAME_LEN];
    size_t key_chars_remaining = (DATA_MAX_NAME_LEN-1);
    int reserved = 0;
    int offset = 0;
    memset(tmp, 0, sizeof(tmp));
    if(source == NULL || dest == NULL || source[0] == '\0' || dest[0] != '\0')
    {
        return;
    }
    size_t src_len = strlen(source);
    snprintf(len_str, sizeof(len_str), "%zu", src_len);
    unsigned char append_status = 0x0;
    append_status |= (source[src_len - 1] == '-') ? 0x1 : 0x0;
    append_status |= (source[src_len - 1] == '+') ? 0x2 : 0x0;
    while ((keys[keys_num] = strtok_r(tmp_ptr, ":_-+", &save_ptr)) != NULL)
    {
        tmp_ptr = NULL;
        /** capitalize 1st char **/
        keys[keys_num][0] = toupper(keys[keys_num][0]);
        keys_num++;
        if(keys_num >= 16)
        {
            break;
        }
    }
    /** concatenate each part of source string **/
    for(i = 0; i < keys_num; i++)
    {
        strncat(tmp, keys[i], key_chars_remaining);
        key_chars_remaining -= strlen(keys[i]);
    }
    tmp[DATA_MAX_NAME_LEN - 1] = '\0';
    /** to coordinate limitation of length of type_instance
     *  we will truncate ds_name
     *  when the its length is more than
     *  DATA_MAX_NAME_LEN
     */
    if(strlen(tmp) > DATA_MAX_NAME_LEN - 1)
    {
        append_status |= 0x4;
        /** we should reserve space for
         * len_str
         */
        reserved += 2;
    }
    if(append_status & 0x1)
    {
        /** we should reserve space for
         * "Minus"
         */
        reserved += 5;
    }
    if(append_status & 0x2)
    {
        /** we should reserve space for
         * "Plus"
         */
        reserved += 4;
    }
    snprintf(dest, DATA_MAX_NAME_LEN - reserved, "%s", tmp);
    offset = strlen(dest);
    switch (append_status)
    {
        case 0x1:
            memcpy(dest + offset, "Minus", 5);
            break;
        case 0x2:
            memcpy(dest + offset, "Plus", 5);
            break;
        case 0x4:
            memcpy(dest + offset, len_str, 2);
            break;
        case 0x5:
            memcpy(dest + offset, "Minus", 5);
            memcpy(dest + offset + 5, len_str, 2);
            break;
        case 0x6:
            memcpy(dest + offset, "Plus", 4);
            memcpy(dest + offset + 4, len_str, 2);
            break;
        default:
            break;
    }
}

/**
 * Parse key to remove "type" if this is for schema and initiate compaction
 */
static int parse_keys(const char *key_str, char *ds_name)
{
    char *ptr, *rptr;
    size_t ds_name_len = 0;
    /**
     * allow up to 100 characters before compaction - compact_ds_name will not
     * allow more than DATA_MAX_NAME_LEN chars
     */
    int max_str_len = 100;
    char tmp_ds_name[max_str_len];
    memset(tmp_ds_name, 0, sizeof(tmp_ds_name));
    if(ds_name == NULL || key_str == NULL ||  key_str[0] == '\0' ||
                                                            ds_name[0] != '\0')
    {
        return -1;
    }
    if((ptr = strchr(key_str, '.')) == NULL
            || (rptr = strrchr(key_str, '.')) == NULL)
    {
        memcpy(tmp_ds_name, key_str, max_str_len - 1);
        goto compact;
    }

    ds_name_len = (rptr - ptr) > max_str_len ? max_str_len : (rptr - ptr);
    if((ds_name_len == 0) || strncmp(rptr + 1, "type", 4))
    { /** copy whole key **/
        memcpy(tmp_ds_name, key_str, max_str_len - 1);
    }
    else
    {/** more than two keys **/
        memcpy(tmp_ds_name, key_str, ((rptr - key_str) > (max_str_len - 1) ?
                (max_str_len - 1) : (rptr - key_str)));
    }

    compact: compact_ds_name(tmp_ds_name, ds_name);
    return 0;
}

/**
 * while parsing ceph admin socket schema, save counter name and type for later
 * data processing
 */
static int ceph_daemon_add_ds_entry(struct ceph_daemon *d, const char *name,
        int pc_type)
{
    uint32_t type;
    char ds_name[DATA_MAX_NAME_LEN];
    memset(ds_name, 0, sizeof(ds_name));

    if(convert_special_metrics)
    {
        /**
         * Special case for filestore:JournalWrBytes. For some reason, Ceph
         * schema encodes this as a count/sum pair while all other "Bytes" data
         * (excluding used/capacity bytes for OSD space) uses a single "Derive"
         * type. To spare further confusion, keep this KPI as the same type of
         * other "Bytes". Instead of keeping an "average" or "rate", use the
         * "sum" in the pair and assign that to the derive value.
         */
        if((strcmp(name,"filestore.journal_wr_bytes.type") == 0))
        {
            pc_type = 10;
        }
    }

    d->ds_names = realloc(d->ds_names, sizeof(char *) * (d->ds_num + 1));
    if(!d->ds_names)
    {
        return -ENOMEM;
    }

    d->ds_types = realloc(d->ds_types, sizeof(uint32_t) * (d->ds_num + 1));
    if(!d->ds_types)
    {
        return -ENOMEM;
    }

    d->ds_names[d->ds_num] = malloc(sizeof(char) * DATA_MAX_NAME_LEN);
    if(!d->ds_names[d->ds_num])
    {
        return -ENOMEM;
    }

    type = (pc_type & PERFCOUNTER_DERIVE) ? DSET_RATE :
            ((pc_type & PERFCOUNTER_LATENCY) ? DSET_LATENCY : DSET_BYTES);
    d->ds_types[d->ds_num] = type;

    if(parse_keys(name, ds_name))
    {
        return 1;
    }

    sstrncpy(d->ds_names[d->ds_num], ds_name, DATA_MAX_NAME_LEN -1);
    d->ds_num = (d->ds_num + 1);

    return 0;
}

/******* ceph_config *******/
static int cc_handle_str(struct oconfig_item_s *item, char *dest, int dest_len)
{
    const char *val;
    if(item->values_num != 1)
    {
        return -ENOTSUP;
    }
    if(item->values[0].type != OCONFIG_TYPE_STRING)
    {
        return -ENOTSUP;
    }
    val = item->values[0].value.string;
    if(snprintf(dest, dest_len, "%s", val) > (dest_len - 1))
    {
        ERROR("ceph plugin: configuration parameter '%s' is too long.\n",
                item->key);
        return -ENAMETOOLONG;
    }
    return 0;
}

static int cc_handle_bool(struct oconfig_item_s *item, int *dest)
{
    if(item->values_num != 1)
    {
        return -ENOTSUP;
    }

    if(item->values[0].type != OCONFIG_TYPE_BOOLEAN)
    {
        return -ENOTSUP;
    }

    *dest = (item->values[0].value.boolean) ? 1 : 0;
    return 0;
}

static int cc_add_daemon_config(oconfig_item_t *ci)
{
    int ret, i;
    struct ceph_daemon *nd, cd;
    struct ceph_daemon **tmp;
    memset(&cd, 0, sizeof(struct ceph_daemon));

    if((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
    {
        WARNING("ceph plugin: `Daemon' blocks need exactly one string "
                "argument.");
        return (-1);
    }

    ret = cc_handle_str(ci, cd.name, DATA_MAX_NAME_LEN);
    if(ret)
    {
        return ret;
    }

    for(i=0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;

        if(strcasecmp("SocketPath", child->key) == 0)
        {
            ret = cc_handle_str(child, cd.asok_path, sizeof(cd.asok_path));
            if(ret)
            {
                return ret;
            }
        }
        else
        {
            WARNING("ceph plugin: ignoring unknown option %s", child->key);
        }
    }
    if(cd.name[0] == '\0')
    {
        ERROR("ceph plugin: you must configure a daemon name.\n");
        return -EINVAL;
    }
    else if(cd.asok_path[0] == '\0')
    {
        ERROR("ceph plugin(name=%s): you must configure an administrative "
        "socket path.\n", cd.name);
        return -EINVAL;
    }
    else if(!((cd.asok_path[0] == '/') ||
            (cd.asok_path[0] == '.' && cd.asok_path[1] == '/')))
    {
        ERROR("ceph plugin(name=%s): administrative socket paths must begin "
                "with '/' or './' Can't parse: '%s'\n", cd.name, cd.asok_path);
        return -EINVAL;
    }

    tmp = realloc(g_daemons, (g_num_daemons+1) * sizeof(*g_daemons));
    if(tmp == NULL)
    {
        /* The positive return value here indicates that this is a
         * runtime error, not a configuration error.  */
        return ENOMEM;
    }
    g_daemons = tmp;

    nd = malloc(sizeof(*nd));
    if(!nd)
    {
        return ENOMEM;
    }
    memcpy(nd, &cd, sizeof(*nd));
    g_daemons[g_num_daemons++] = nd;
    return 0;
}

static int ceph_config(oconfig_item_t *ci)
{
    int ret, i;

    for(i = 0; i < ci->children_num; ++i)
    {
        oconfig_item_t *child = ci->children + i;
        if(strcasecmp("Daemon", child->key) == 0)
        {
            ret = cc_add_daemon_config(child);
            if(ret == ENOMEM)
            {
                ERROR("ceph plugin: Couldn't allocate memory");
                return ret;
            }
            else if(ret)
            {
                //process other daemons and ignore this one
                continue;
            }
        }
        else if(strcasecmp("LongRunAvgLatency", child->key) == 0)
        {
            ret = cc_handle_bool(child, &long_run_latency_avg);
            if(ret)
            {
                return ret;
            }
        }
        else if(strcasecmp("ConvertSpecialMetricTypes", child->key) == 0)
        {
            ret = cc_handle_bool(child, &convert_special_metrics);
            if(ret)
            {
                return ret;
            }
        }
        else
        {
            WARNING("ceph plugin: ignoring unknown option %s", child->key);
        }
    }
    return 0;
}

/**
 * Parse JSON and get error message if present
 */
static int
traverse_json(const unsigned char *json, uint32_t json_len, yajl_handle hand)
{
    yajl_status status = yajl_parse(hand, json, json_len);
    unsigned char *msg;

    switch(status)
    {
        case yajl_status_error:
            msg = yajl_get_error(hand, /* verbose = */ 1,
                                       /* jsonText = */ (unsigned char *) json,
                                                      (unsigned int) json_len);
            ERROR ("ceph plugin: yajl_parse failed: %s", msg);
            yajl_free_error(hand, msg);
            return 1;
        case yajl_status_client_canceled:
            return 1;
        default:
            return 0;
    }
}

/**
 * Add entry for each counter while parsing schema
 */
static int
node_handler_define_schema(void *arg, const char *val, const char *key)
{
    struct ceph_daemon *d = (struct ceph_daemon *) arg;
    int pc_type;
    pc_type = atoi(val);
    return ceph_daemon_add_ds_entry(d, key, pc_type);
}

/**
 * Latency counter does not yet have an entry in last poll data - add it.
 */
static int add_last(struct ceph_daemon *d, const char *ds_n, double cur_sum,
        uint64_t cur_count)
{
    d->last_poll_data[d->last_idx] = malloc(1 * sizeof(struct last_data));
    if(!d->last_poll_data[d->last_idx])
    {
        return -ENOMEM;
    }
    sstrncpy(d->last_poll_data[d->last_idx]->ds_name,ds_n,
            sizeof(d->last_poll_data[d->last_idx]->ds_name));
    d->last_poll_data[d->last_idx]->last_sum = cur_sum;
    d->last_poll_data[d->last_idx]->last_count = cur_count;
    d->last_idx = (d->last_idx + 1);
    return 0;
}

/**
 * Update latency counter or add new entry if it doesn't exist
 */
static int update_last(struct ceph_daemon *d, const char *ds_n, int index,
        double cur_sum, uint64_t cur_count)
{
    if((d->last_idx > index) && (strcmp(d->last_poll_data[index]->ds_name, ds_n) == 0))
    {
        d->last_poll_data[index]->last_sum = cur_sum;
        d->last_poll_data[index]->last_count = cur_count;
        return 0;
    }

    if(!d->last_poll_data)
    {
        d->last_poll_data = malloc(1 * sizeof(struct last_data *));
        if(!d->last_poll_data)
        {
            return -ENOMEM;
        }
    }
    else
    {
        struct last_data **tmp_last = realloc(d->last_poll_data,
                ((d->last_idx+1) * sizeof(struct last_data *)));
        if(!tmp_last)
        {
            return -ENOMEM;
        }
        d->last_poll_data = tmp_last;
    }
    return add_last(d, ds_n, cur_sum, cur_count);
}

/**
 * If using index guess failed (shouldn't happen, but possible if counters
 * get rearranged), resort to searching for counter name
 */
static int backup_search_for_last_avg(struct ceph_daemon *d, const char *ds_n)
{
    int i = 0;
    for(; i < d->last_idx; i++)
    {
        if(strcmp(d->last_poll_data[i]->ds_name, ds_n) == 0)
        {
            return i;
        }
    }
    return -1;
}

/**
 * Calculate average b/t current data and last poll data
 * if last poll data exists
 */
static double get_last_avg(struct ceph_daemon *d, const char *ds_n, int index,
        double cur_sum, uint64_t cur_count)
{
    double result = -1.1, sum_delt = 0.0;
    uint64_t count_delt = 0;
    int tmp_index = 0;
    if(d->last_idx > index)
    {
        if(strcmp(d->last_poll_data[index]->ds_name, ds_n) == 0)
        {
            tmp_index = index;
        }
        //test previous index
        else if((index > 0) && (strcmp(d->last_poll_data[index-1]->ds_name, ds_n) == 0))
        {
            tmp_index = (index - 1);
        }
        else
        {
            tmp_index = backup_search_for_last_avg(d, ds_n);
        }

        if((tmp_index > -1) && (cur_count > d->last_poll_data[tmp_index]->last_count))
        {
            sum_delt = (cur_sum - d->last_poll_data[tmp_index]->last_sum);
            count_delt = (cur_count - d->last_poll_data[tmp_index]->last_count);
            result = (sum_delt / count_delt);
        }
    }

    if(result == -1.1)
    {
        result = NAN;
    }
    if(update_last(d, ds_n, tmp_index, cur_sum, cur_count) == -ENOMEM)
    {
        return -ENOMEM;
    }
    return result;
}

/**
 * If using index guess failed, resort to searching for counter name
 */
static uint32_t backup_search_for_type(struct ceph_daemon *d, char *ds_name)
{
    int idx = 0;
    for(; idx < d->ds_num; idx++)
    {
        if(strcmp(d->ds_names[idx], ds_name) == 0)
        {
            return d->ds_types[idx];
        }
    }
    return DSET_TYPE_UNFOUND;
}

/**
 * Process counter data and dispatch values
 */
static int node_handler_fetch_data(void *arg, const char *val, const char *key)
{
    value_t uv;
    double tmp_d;
    uint64_t tmp_u;
    struct values_tmp *vtmp = (struct values_tmp*) arg;
    uint32_t type = DSET_TYPE_UNFOUND;
    int index = vtmp->index;

    char ds_name[DATA_MAX_NAME_LEN];
    memset(ds_name, 0, sizeof(ds_name));

    if(parse_keys(key, ds_name))
    {
        return 1;
    }

    if(index >= vtmp->d->ds_num)
    {
        //don't overflow bounds of array
        index = (vtmp->d->ds_num - 1);
    }

    /**
     * counters should remain in same order we parsed schema... we maintain the
     * index variable to keep track of current point in list of counters. first
     * use index to guess point in array for retrieving type. if that doesn't
     * work, use the old way to get the counter type
     */
    if(strcmp(ds_name, vtmp->d->ds_names[index]) == 0)
    {
        //found match
        type = vtmp->d->ds_types[index];
    }
    else if((index > 0) && (strcmp(ds_name, vtmp->d->ds_names[index-1]) == 0))
    {
        //try previous key
        type = vtmp->d->ds_types[index-1];
    }

    if(type == DSET_TYPE_UNFOUND)
    {
        //couldn't find right type by guessing, check the old way
        type = backup_search_for_type(vtmp->d, ds_name);
    }

    switch(type)
    {
        case DSET_LATENCY:
            if(vtmp->avgcount_exists == -1)
            {
                sscanf(val, "%" PRIu64, &vtmp->avgcount);
                vtmp->avgcount_exists = 0;
                //return after saving avgcount - don't dispatch value
                //until latency calculation
                return 0;
            }
            else
            {
                double sum, result;
                sscanf(val, "%lf", &sum);

                if(vtmp->avgcount == 0)
                {
                    vtmp->avgcount = 1;
                }

                /** User wants latency values as long run avg */
                if(long_run_latency_avg)
                {
                    result = (sum / vtmp->avgcount);
                }
                else
                {
                    result = get_last_avg(vtmp->d, ds_name, vtmp->latency_index, sum, vtmp->avgcount);
                    if(result == -ENOMEM)
                    {
                        return -ENOMEM;
                    }
                }

                uv.gauge = result;
                vtmp->avgcount_exists = -1;
                vtmp->latency_index = (vtmp->latency_index + 1);
            }
            break;
        case DSET_BYTES:
            sscanf(val, "%lf", &tmp_d);
            uv.gauge = tmp_d;
            break;
        case DSET_RATE:
            sscanf(val, "%" PRIu64, &tmp_u);
            uv.derive = tmp_u;
            break;
        case DSET_TYPE_UNFOUND:
        default:
            ERROR("ceph plugin: ds %s was not properly initialized.", ds_name);
            return -1;
    }

    sstrncpy(vtmp->vlist.type, ceph_dset_types[type], sizeof(vtmp->vlist.type));
    sstrncpy(vtmp->vlist.type_instance, ds_name, sizeof(vtmp->vlist.type_instance));
    vtmp->vlist.values = &uv;
    vtmp->vlist.values_len = 1;

    vtmp->index = (vtmp->index + 1);
    plugin_dispatch_values(&vtmp->vlist);

    return 0;
}

static int cconn_connect(struct cconn *io)
{
    struct sockaddr_un address;
    int flags, fd, err;
    if(io->state != CSTATE_UNCONNECTED)
    {
        ERROR("ceph plugin: cconn_connect: io->state != CSTATE_UNCONNECTED");
        return -EDOM;
    }
    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if(fd < 0)
    {
        int err = -errno;
        ERROR("ceph plugin: cconn_connect: socket(PF_UNIX, SOCK_STREAM, 0) "
            "failed: error %d", err);
        return err;
    }
    memset(&address, 0, sizeof(struct sockaddr_un));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s",
            io->d->asok_path);
    RETRY_ON_EINTR(err,
        connect(fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un)));
    if(err < 0)
    {
        ERROR("ceph plugin: cconn_connect: connect(%d) failed: error %d",
            fd, err);
        return err;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
    {
        err = -errno;
        ERROR("ceph plugin: cconn_connect: fcntl(%d, O_NONBLOCK) error %d",
            fd, err);
        return err;
    }
    io->asok = fd;
    io->state = CSTATE_WRITE_REQUEST;
    io->amt = 0;
    io->json_len = 0;
    io->json = NULL;
    return 0;
}

static void cconn_close(struct cconn *io)
{
    io->state = CSTATE_UNCONNECTED;
    if(io->asok != -1)
    {
        int res;
        RETRY_ON_EINTR(res, close(io->asok));
    }
    io->asok = -1;
    io->amt = 0;
    io->json_len = 0;
    sfree(io->json);
    io->json = NULL;
}

/* Process incoming JSON counter data */
static int
cconn_process_data(struct cconn *io, yajl_struct *yajl, yajl_handle hand)
{
    int ret;
    struct values_tmp *vtmp = calloc(1, sizeof(struct values_tmp) * 1);
    if(!vtmp)
    {
        return -ENOMEM;
    }

    vtmp->vlist = (value_list_t)VALUE_LIST_INIT;
    sstrncpy(vtmp->vlist.host, hostname_g, sizeof(vtmp->vlist.host));
    sstrncpy(vtmp->vlist.plugin, "ceph", sizeof(vtmp->vlist.plugin));
    sstrncpy(vtmp->vlist.plugin_instance, io->d->name, sizeof(vtmp->vlist.plugin_instance));

    vtmp->d = io->d;
    vtmp->avgcount_exists = -1;
    vtmp->latency_index = 0;
    vtmp->index = 0;
    yajl->handler_arg = vtmp;
    ret = traverse_json(io->json, io->json_len, hand);
    sfree(vtmp);
    return ret;
}

/**
 * Initiate JSON parsing and print error if one occurs
 */
static int cconn_process_json(struct cconn *io)
{
    if((io->request_type != ASOK_REQ_DATA) &&
            (io->request_type != ASOK_REQ_SCHEMA))
    {
        return -EDOM;
    }

    int result = 1;
    yajl_handle hand;
    yajl_status status;

    hand = yajl_alloc(&callbacks,
#if HAVE_YAJL_V2
      /* alloc funcs = */ NULL,
#else
      /* alloc funcs = */ NULL, NULL,
#endif
      /* context = */ (void *)(&io->yajl));

    if(!hand)
    {
        ERROR ("ceph plugin: yajl_alloc failed.");
        return ENOMEM;
    }

    io->yajl.depth = 0;

    switch(io->request_type)
    {
        case ASOK_REQ_DATA:
            io->yajl.handler = node_handler_fetch_data;
            result = cconn_process_data(io, &io->yajl, hand);
            break;
        case ASOK_REQ_SCHEMA:
            //init daemon specific variables
            io->d->ds_num = 0;
            io->d->last_idx = 0;
            io->d->last_poll_data = NULL;
            io->yajl.handler = node_handler_define_schema;
            io->yajl.handler_arg = io->d;
            result = traverse_json(io->json, io->json_len, hand);
            break;
    }

    if(result)
    {
        goto done;
    }

#if HAVE_YAJL_V2
    status = yajl_complete_parse(hand);
#else
    status = yajl_parse_complete(hand);
#endif

    if (status != yajl_status_ok)
    {
      unsigned char *errmsg = yajl_get_error (hand, /* verbose = */ 0,
          /* jsonText = */ NULL, /* jsonTextLen = */ 0);
      ERROR ("ceph plugin: yajl_parse_complete failed: %s",
          (char *) errmsg);
      yajl_free_error (hand, errmsg);
      yajl_free (hand);
      return 1;
    }

    done:
    yajl_free (hand);
    return result;
}

static int cconn_validate_revents(struct cconn *io, int revents)
{
    if(revents & POLLERR)
    {
        ERROR("ceph plugin: cconn_validate_revents(name=%s): got POLLERR",
            io->d->name);
        return -EIO;
    }
    switch (io->state)
    {
        case CSTATE_WRITE_REQUEST:
            return (revents & POLLOUT) ? 0 : -EINVAL;
        case CSTATE_READ_VERSION:
        case CSTATE_READ_AMT:
        case CSTATE_READ_JSON:
            return (revents & POLLIN) ? 0 : -EINVAL;
        default:
            ERROR("ceph plugin: cconn_validate_revents(name=%s) got to "
                "illegal state on line %d", io->d->name, __LINE__);
            return -EDOM;
    }
}

/** Handle a network event for a connection */
static int cconn_handle_event(struct cconn *io)
{
    int ret;
    switch (io->state)
    {
        case CSTATE_UNCONNECTED:
            ERROR("ceph plugin: cconn_handle_event(name=%s) got to illegal "
                "state on line %d", io->d->name, __LINE__);

            return -EDOM;
        case CSTATE_WRITE_REQUEST:
        {
            char cmd[32];
            snprintf(cmd, sizeof(cmd), "%s%d%s", "{ \"prefix\": \"",
                    io->request_type, "\" }\n");
            size_t cmd_len = strlen(cmd);
            RETRY_ON_EINTR(ret,
                  write(io->asok, ((char*)&cmd) + io->amt, cmd_len - io->amt));
            DEBUG("ceph plugin: cconn_handle_event(name=%s,state=%d,amt=%d,ret=%d)",
                    io->d->name, io->state, io->amt, ret);
            if(ret < 0)
            {
                return ret;
            }
            io->amt += ret;
            if(io->amt >= cmd_len)
            {
                io->amt = 0;
                switch (io->request_type)
                {
                    case ASOK_REQ_VERSION:
                        io->state = CSTATE_READ_VERSION;
                        break;
                    default:
                        io->state = CSTATE_READ_AMT;
                        break;
                }
            }
            return 0;
        }
        case CSTATE_READ_VERSION:
        {
            RETRY_ON_EINTR(ret,
                    read(io->asok, ((char*)(&io->d->version)) + io->amt,
                            sizeof(io->d->version) - io->amt));
            DEBUG("ceph plugin: cconn_handle_event(name=%s,state=%d,ret=%d)",
                    io->d->name, io->state, ret);
            if(ret < 0)
            {
                return ret;
            }
            io->amt += ret;
            if(io->amt >= sizeof(io->d->version))
            {
                io->d->version = ntohl(io->d->version);
                if(io->d->version != 1)
                {
                    ERROR("ceph plugin: cconn_handle_event(name=%s) not "
                        "expecting version %d!", io->d->name, io->d->version);
                    return -ENOTSUP;
                }
                DEBUG("ceph plugin: cconn_handle_event(name=%s): identified as "
                        "version %d", io->d->name, io->d->version);
                io->amt = 0;
                cconn_close(io);
                io->request_type = ASOK_REQ_SCHEMA;
            }
            return 0;
        }
        case CSTATE_READ_AMT:
        {
            RETRY_ON_EINTR(ret,
                    read(io->asok, ((char*)(&io->json_len)) + io->amt,
                            sizeof(io->json_len) - io->amt));
            DEBUG("ceph plugin: cconn_handle_event(name=%s,state=%d,ret=%d)",
                    io->d->name, io->state, ret);
            if(ret < 0)
            {
                return ret;
            }
            io->amt += ret;
            if(io->amt >= sizeof(io->json_len))
            {
                io->json_len = ntohl(io->json_len);
                io->amt = 0;
                io->state = CSTATE_READ_JSON;
                io->json = calloc(1, io->json_len + 1);
                if(!io->json)
                {
                    ERROR("ceph plugin: error callocing io->json");
                    return -ENOMEM;
                }
            }
            return 0;
        }
        case CSTATE_READ_JSON:
        {
            RETRY_ON_EINTR(ret,
                   read(io->asok, io->json + io->amt, io->json_len - io->amt));
            DEBUG("ceph plugin: cconn_handle_event(name=%s,state=%d,ret=%d)",
                    io->d->name, io->state, ret);
            if(ret < 0)
            {
                return ret;
            }
            io->amt += ret;
            if(io->amt >= io->json_len)
            {
                ret = cconn_process_json(io);
                if(ret)
                {
                    return ret;
                }
                cconn_close(io);
                io->request_type = ASOK_REQ_NONE;
            }
            return 0;
        }
        default:
            ERROR("ceph plugin: cconn_handle_event(name=%s) got to illegal "
                "state on line %d", io->d->name, __LINE__);
            return -EDOM;
    }
}

static int cconn_prepare(struct cconn *io, struct pollfd* fds)
{
    int ret;
    if(io->request_type == ASOK_REQ_NONE)
    {
        /* The request has already been serviced. */
        return 0;
    }
    else if((io->request_type == ASOK_REQ_DATA) && (io->d->ds_num == 0))
    {
        /* If there are no counters to report on, don't bother
         * connecting */
        return 0;
    }

    switch (io->state)
    {
        case CSTATE_UNCONNECTED:
            ret = cconn_connect(io);
            if(ret > 0)
            {
                return -ret;
            }
            else if(ret < 0)
            {
                return ret;
            }
            fds->fd = io->asok;
            fds->events = POLLOUT;
            return 1;
        case CSTATE_WRITE_REQUEST:
            fds->fd = io->asok;
            fds->events = POLLOUT;
            return 1;
        case CSTATE_READ_VERSION:
        case CSTATE_READ_AMT:
        case CSTATE_READ_JSON:
            fds->fd = io->asok;
            fds->events = POLLIN;
            return 1;
        default:
            ERROR("ceph plugin: cconn_prepare(name=%s) got to illegal state "
                "on line %d", io->d->name, __LINE__);
            return -EDOM;
    }
}

/** Returns the difference between two struct timevals in milliseconds.
 * On overflow, we return max/min int.
 */
static int milli_diff(const struct timeval *t1, const struct timeval *t2)
{
    int64_t ret;
    int sec_diff = t1->tv_sec - t2->tv_sec;
    int usec_diff = t1->tv_usec - t2->tv_usec;
    ret = usec_diff / 1000;
    ret += (sec_diff * 1000);
    return (ret > INT_MAX) ? INT_MAX : ((ret < INT_MIN) ? INT_MIN : (int)ret);
}

/** This handles the actual network I/O to talk to the Ceph daemons.
 */
static int cconn_main_loop(uint32_t request_type)
{
    int i, ret, some_unreachable = 0;
    struct timeval end_tv;
    struct cconn io_array[g_num_daemons];

    DEBUG("ceph plugin: entering cconn_main_loop(request_type = %d)", request_type);

    /* create cconn array */
    memset(io_array, 0, sizeof(io_array));
    for(i = 0; i < g_num_daemons; ++i)
    {
        io_array[i].d = g_daemons[i];
        io_array[i].request_type = request_type;
        io_array[i].state = CSTATE_UNCONNECTED;
    }

    /** Calculate the time at which we should give up */
    gettimeofday(&end_tv, NULL);
    end_tv.tv_sec += CEPH_TIMEOUT_INTERVAL;

    while (1)
    {
        int nfds, diff;
        struct timeval tv;
        struct cconn *polled_io_array[g_num_daemons];
        struct pollfd fds[g_num_daemons];
        memset(fds, 0, sizeof(fds));
        nfds = 0;
        for(i = 0; i < g_num_daemons; ++i)
        {
            struct cconn *io = io_array + i;
            ret = cconn_prepare(io, fds + nfds);
            if(ret < 0)
            {
                WARNING("ceph plugin: cconn_prepare(name=%s,i=%d,st=%d)=%d",
                        io->d->name, i, io->state, ret);
                cconn_close(io);
                io->request_type = ASOK_REQ_NONE;
                some_unreachable = 1;
            }
            else if(ret == 1)
            {
                polled_io_array[nfds++] = io_array + i;
            }
        }
        if(nfds == 0)
        {
            /* finished */
            ret = 0;
            goto done;
        }
        gettimeofday(&tv, NULL);
        diff = milli_diff(&end_tv, &tv);
        if(diff <= 0)
        {
            /* Timed out */
            ret = -ETIMEDOUT;
            WARNING("ceph plugin: cconn_main_loop: timed out.");
            goto done;
        }
        RETRY_ON_EINTR(ret, poll(fds, nfds, diff));
        if(ret < 0)
        {
            ERROR("ceph plugin: poll(2) error: %d", ret);
            goto done;
        }
        for(i = 0; i < nfds; ++i)
        {
            struct cconn *io = polled_io_array[i];
            int revents = fds[i].revents;
            if(revents == 0)
            {
                /* do nothing */
            }
            else if(cconn_validate_revents(io, revents))
            {
                WARNING("ceph plugin: cconn(name=%s,i=%d,st=%d): "
                "revents validation error: "
                "revents=0x%08x", io->d->name, i, io->state, revents);
                cconn_close(io);
                io->request_type = ASOK_REQ_NONE;
                some_unreachable = 1;
            }
            else
            {
                int ret = cconn_handle_event(io);
                if(ret)
                {
                    WARNING("ceph plugin: cconn_handle_event(name=%s,"
                    "i=%d,st=%d): error %d", io->d->name, i, io->state, ret);
                    cconn_close(io);
                    io->request_type = ASOK_REQ_NONE;
                    some_unreachable = 1;
                }
            }
        }
    }
    done: for(i = 0; i < g_num_daemons; ++i)
    {
        cconn_close(io_array + i);
    }
    if(some_unreachable)
    {
        DEBUG("ceph plugin: cconn_main_loop: some Ceph daemons were unreachable.");
    }
    else
    {
        DEBUG("ceph plugin: cconn_main_loop: reached all Ceph daemons :)");
    }
    return ret;
}

static int ceph_read(void)
{
    return cconn_main_loop(ASOK_REQ_DATA);
}

/******* lifecycle *******/
static int ceph_init(void)
{
    int ret;
    ceph_daemons_print();

    ret = cconn_main_loop(ASOK_REQ_VERSION);

    return (ret) ? ret : 0;
}

static int ceph_shutdown(void)
{
    int i;
    for(i = 0; i < g_num_daemons; ++i)
    {
        ceph_daemon_free(g_daemons[i]);
    }
    sfree(g_daemons);
    g_daemons = NULL;
    g_num_daemons = 0;
    DEBUG("ceph plugin: finished ceph_shutdown");
    return 0;
}

void module_register(void)
{
    plugin_register_complex_config("ceph", ceph_config);
    plugin_register_init("ceph", ceph_init);
    plugin_register_read("ceph", ceph_read);
    plugin_register_shutdown("ceph", ceph_shutdown);
}
