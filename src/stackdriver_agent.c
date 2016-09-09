/**
 * collectd - src/stackdriver_agent.c
 * Copyright 2016 Google Inc. All Rights Reserved.
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
 *   Corey Kosak <kosak at google.com>
 **/

#include <stdlib.h>
#include <unistd.h>

#include "collectd.h"
#include "common.h"
#include "daemon/utils_cache.h"
#include "liboconfig/oconfig.h"
#include "stackdriver-agent-keys.h"

static const char this_plugin_name[] = "stackdriver_agent";

typedef struct {
    cdtime_t start_time;
} context_t;

static context_t *context_create()
{
    context_t *result = calloc(1, sizeof(*result));
    if (result == NULL)
    {
        ERROR("%s: calloc failed.", this_plugin_name);
        return NULL;
    }
    result->start_time = cdtime();
    return result;
}

static void context_destroy(context_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  sfree(ctx);
}

static int sagt_submit_helper(const char *type, const char *type_instance,
    const char *plugin_instance, cdtime_t now, cdtime_t interval, value_t *value,
    meta_data_t *meta_data)
{
    value_list_t vl = {
        .values = value,
        .values_len = 1,
        .time = now,
        .interval = interval,
        .meta = meta_data
    };
    sstrncpy(vl.host, hostname_g, sizeof(vl.host));
    sstrncpy(vl.plugin, "agent", sizeof(vl.plugin));
    sstrncpy(vl.type, type, sizeof(vl.type));
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));
    if (plugin_instance != NULL)
    {
        sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
    }
    if (plugin_dispatch_values(&vl) != 0) {
        ERROR("%s: plugin_dispatch_values failed.", this_plugin_name);
        return -1;
    }
    return 0;
}

static int sagt_submit_gauge(const char *type_instance, const char *plugin_instance,
    cdtime_t now, cdtime_t interval, gauge_t gauge, meta_data_t *meta_data)
{
    value_t v = {.gauge = gauge};
    return sagt_submit_helper("gauge", type_instance, plugin_instance, now, interval, &v,
        meta_data);
}

static int sagt_submit_derive(const char *type_instance, const char *plugin_instance,
    cdtime_t now, cdtime_t interval, derive_t derive, meta_data_t *meta_data)
{
    value_t v = {.derive = derive};
    return sagt_submit_helper("derive", type_instance, plugin_instance, now, interval, &v,
        meta_data);
}


/**
 * Send a variety of agent status/health-related metrics.
 */
static int sagt_read(user_data_t *user_data)
{
    context_t *ctx = user_data->data;
    cdtime_t now = cdtime();
    cdtime_t interval = plugin_get_interval();

    // This value list exists merely for the purpose of harvesting its key fields for the purpose
    // of looking stuff up in the cache.
    value_list_t vl = {};  // zero-init
    sstrncpy(vl.plugin, this_plugin_name, sizeof(vl.plugin));

    // uptime
    {
        meta_data_t *md = meta_data_create();
        if (meta_data_add_string(md, "version", COLLECTD_USERAGENT) == 0)
        {
            derive_t uptime = CDTIME_T_TO_TIME_T(now - ctx->start_time);
            sagt_submit_derive("uptime", NULL, now, interval, uptime, md);
        }
        meta_data_destroy(md);
    }

    // memory used
    {
        FILE *f = fopen("/proc/self/statm", "r");
        if (f)
        {
            size_t vm = 0;
            if (fscanf(f, "%zu", &vm))
            {
                long page_size = sysconf(_SC_PAGESIZE);
                size_t mused = vm * page_size;
                sagt_submit_gauge("memory_usage", NULL, now, interval, mused, NULL);
            }
            fclose(f);
        }
    }

    // Stats for API requests. The corresponding uc_meta_data_set calls are in
    // write_gcm.c.
    {
        uint64_t value;
        if (uc_meta_data_get_unsigned_int(&vl, SAGT_API_REQUESTS_SUCCESS, &value) == 0)
        {
            sagt_submit_derive("api_request_count", "success", now, interval, value, NULL);
        }
        if (uc_meta_data_get_unsigned_int(&vl, SAGT_API_REQUESTS_CONNECTIVITY_FAILURES, &value) == 0)
        {
            sagt_submit_derive("api_request_count", "connectivity_failures", now, interval, value, NULL);
        }
        if (uc_meta_data_get_unsigned_int(&vl, SAGT_API_REQUESTS_ERRORS, &value) == 0)
        {
          sagt_submit_derive("api_request_count", "errors", now, interval, value, NULL);
        }
    }

    // Cloud Monarch-related stats. The corresponding uc_meta_data_set calls are in
    // match_throttle_metadata_keys.c.
    {
        uint64_t streamspace_size;
        _Bool throttling;
        if (uc_meta_data_get_unsigned_int(&vl, SAGT_STREAMSPACE_SIZE, &streamspace_size) == 0)
        {
            sagt_submit_gauge("streamspace_size", NULL, now, interval, streamspace_size, NULL);
        }
        if (uc_meta_data_get_boolean(&vl, SAGT_STREAMSPACE_SIZE_THROTTLING, &throttling) == 0)
        {
            sagt_submit_gauge("streamspace_size_throttling", NULL, now, interval, throttling, NULL);
        }
    }

    return 0;
}

/*
 * The init routine. Creates a context and registers a read callback.
 */
static int sagt_init()
{
    int result = -1;  // Pessimistically assume failure.

    context_t *ctx = context_create();
    if (ctx == NULL)
    {
        goto leave;
    }

    user_data_t user_data = {
        .data = ctx,
        .free_func = (void (*)(void *)) &context_destroy
    };

    if (plugin_register_complex_read(NULL, this_plugin_name,
        &sagt_read, 0, &user_data) != 0)
    {
        ERROR("%s: plugin_register_complex_read failed.", this_plugin_name);
        goto leave;
    }

    ctx = NULL;  // Owned by plugin system now.
    result = 0;  // Success!

  leave:
    context_destroy(ctx);
    return result;
}

/* Register this module with collectd */
void module_register(void)
{
    if (plugin_register_init(this_plugin_name, &sagt_init) != 0)
    {
        ERROR("%s: plugin_register_init failed.", this_plugin_name);
        return;
    }
}
