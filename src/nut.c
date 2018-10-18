/**
 * collectd - src/nut.c
 * Copyright (C) 2007       Florian octo Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 *   Pavel Rochnyak <pavel2000 ngs.ru>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include <upsclient.h>

#if HAVE_UPSCONN_T
typedef UPSCONN_t collectd_upsconn_t;
#elif HAVE_UPSCONN
typedef UPSCONN collectd_upsconn_t;
#else
#error "Unable to determine the UPS connection type."
#endif

struct nut_ups_s;
typedef struct nut_ups_s nut_ups_t;
struct nut_ups_s {
  collectd_upsconn_t *conn;
  char *upsname;
  char *hostname;
  int port;
  nut_ups_t *next;
};

static const char *config_keys[] = {"UPS", "FORCESSL", "VERIFYPEER", "CAPATH",
                                    "CONNECTTIMEOUT"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);
static int force_ssl;   // Initialized to default of 0 (false)
static int verify_peer; // Initialized to default of 0 (false)
static int ssl_flags = UPSCLI_CONN_TRYSSL;
static int connect_timeout = -1;
static char *ca_path;

static int nut_read(user_data_t *user_data);

static void free_nut_ups_t(void *arg) {
  nut_ups_t *ups = arg;

  if (ups->conn != NULL) {
    upscli_disconnect(ups->conn);
    sfree(ups->conn);
  }
  sfree(ups->hostname);
  sfree(ups->upsname);
  sfree(ups);
} /* void free_nut_ups_t */

static int nut_add_ups(const char *name) {
  nut_ups_t *ups;
  int status;
  char *cb_name;

  DEBUG("nut plugin: nut_add_ups (name = %s);", name);

  ups = calloc(1, sizeof(*ups));
  if (ups == NULL) {
    ERROR("nut plugin: nut_add_ups: calloc failed.");
    return 1;
  }

  status = upscli_splitname(name, &ups->upsname, &ups->hostname, &ups->port);
  if (status != 0) {
    ERROR("nut plugin: nut_add_ups: upscli_splitname (%s) failed.", name);
    free_nut_ups_t(ups);
    return 1;
  }

  cb_name = ssnprintf_alloc("nut/%s", name);

  status = plugin_register_complex_read(
      /* group     = */ "nut",
      /* name      = */ cb_name,
      /* callback  = */ nut_read,
      /* interval  = */ 0,
      /* user_data = */ &(user_data_t){
          .data = ups, .free_func = free_nut_ups_t,
      });

  sfree(cb_name);

  if (status == EINVAL) {
    WARNING("nut plugin: UPS \"%s\" already added. "
            "Please check your configuration.",
            name);
    return -1;
  }

  return 0;
} /* int nut_add_ups */

static int nut_force_ssl(const char *value) {
  if (strcasecmp(value, "true") == 0)
    force_ssl = 1;
  else if (strcasecmp(value, "false") == 0)
    force_ssl = 0; // Should already be set to 0 from initialization
  else {
    force_ssl = 0;
    WARNING("nut plugin: nut_force_ssl: invalid FORCESSL value "
            "found. Defaulting to false.");
  }
  return 0;
} /* int nut_parse_force_ssl */

static int nut_verify_peer(const char *value) {
  if (strcasecmp(value, "true") == 0)
    verify_peer = 1;
  else if (strcasecmp(value, "false") == 0)
    verify_peer = 0; // Should already be set to 0 from initialization
  else {
    verify_peer = 0;
    WARNING("nut plugin: nut_verify_peer: invalid VERIFYPEER value "
            "found. Defaulting to false.");
  }
  return 0;
} /* int nut_verify_peer */

static int nut_ca_path(const char *value) {
  if (value != NULL && strcmp(value, "") != 0) {
    ca_path = strdup(value);
  } else {
    ca_path = NULL; // Should alread be set to NULL from initialization
  }
  return 0;
} /* int nut_ca_path */

static int nut_set_connect_timeout(const char *value) {
#if HAVE_UPSCLI_TRYCONNECT
  long ret;

  errno = 0;
  ret = strtol(value, /* endptr = */ NULL, /* base = */ 10);
  if (errno == 0)
    connect_timeout = ret;
  else
    WARNING("nut plugin: The ConnectTimeout option requires numeric argument. "
            "Setting ignored.");
#else /* #if HAVE_UPSCLI_TRYCONNECT */
  WARNING("nut plugin: Dependency libupsclient version insufficient (<2.6.2) "
          "for ConnectTimeout option support. Setting ignored.");
#endif
  return 0;
} /* int nut_set_connect_timeout */

static int nut_config(const char *key, const char *value) {
  if (strcasecmp(key, "UPS") == 0)
    return nut_add_ups(value);
  else if (strcasecmp(key, "FORCESSL") == 0)
    return nut_force_ssl(value);
  else if (strcasecmp(key, "VERIFYPEER") == 0)
    return nut_verify_peer(value);
  else if (strcasecmp(key, "CAPATH") == 0)
    return nut_ca_path(value);
  else if (strcasecmp(key, "CONNECTTIMEOUT") == 0)
    return nut_set_connect_timeout(value);
  else
    return -1;
} /* int nut_config */

static void nut_submit(nut_ups_t *ups, const char *type,
                       const char *type_instance, gauge_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  if (strcasecmp(ups->hostname, "localhost") != 0)
    sstrncpy(vl.host, ups->hostname, sizeof(vl.host));
  sstrncpy(vl.plugin, "nut", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, ups->upsname, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* void nut_submit */

static int nut_connect(nut_ups_t *ups) {
  int status, ssl_status;

#if HAVE_UPSCLI_TRYCONNECT
  struct timeval tv;
  tv.tv_sec = connect_timeout / 1000;
  tv.tv_usec = connect_timeout % 1000;

  status =
      upscli_tryconnect(ups->conn, ups->hostname, ups->port, ssl_flags, &tv);
#else /* #if HAVE_UPSCLI_TRYCONNECT */
  status = upscli_connect(ups->conn, ups->hostname, ups->port, ssl_flags);
#endif

  if (status != 0) {
    ERROR("nut plugin: nut_connect: upscli_connect (%s, %i) failed: %s",
          ups->hostname, ups->port, upscli_strerror(ups->conn));
    sfree(ups->conn);
    return -1;
  } /* if (status != 0) */

  INFO("nut plugin: Connection to (%s, %i) established.", ups->hostname,
       ups->port);

  // Output INFO or WARNING based on SSL and VERIFICATION
  ssl_status = upscli_ssl(ups->conn); // 1 for SSL, 0 for not, -1 for error
  if (ssl_status == 1 && verify_peer == 1) {
    INFO("nut plugin: Connection is secured with SSL and certificate "
         "has been verified.");
  } else if (ssl_status == 1) {
    INFO("nut plugin: Connection is secured with SSL with no verification "
         "of server SSL certificate.");
  } else if (ssl_status == 0) {
    WARNING("nut plugin: Connection is unsecured (no SSL).");
  } else {
    ERROR("nut plugin: nut_connect: upscli_ssl failed: %s",
          upscli_strerror(ups->conn));
    sfree(ups->conn);
    return -1;
  } /* if (ssl_status == 1 && verify_peer == 1) */
  return 0;
}

static int nut_read(user_data_t *user_data) {
  nut_ups_t *ups = user_data->data;
  const char *query[3] = {"VAR", ups->upsname, NULL};
  unsigned int query_num = 2;
  char **answer;
  unsigned int answer_num;
  int status;

  /* (Re-)Connect if we have no connection */
  if (ups->conn == NULL) {
    ups->conn = malloc(sizeof(*ups->conn));
    if (ups->conn == NULL) {
      ERROR("nut plugin: malloc failed.");
      return -1;
    }

    status = nut_connect(ups);
    if (status == -1)
      return -1;

  } /* if (ups->conn == NULL) */

  /* nut plugin: nut_read_one: upscli_list_start (adpos) failed: Protocol
   * error */
  status = upscli_list_start(ups->conn, query_num, query);
  if (status != 0) {
    ERROR("nut plugin: nut_read: upscli_list_start (%s) failed: %s",
          ups->upsname, upscli_strerror(ups->conn));
    upscli_disconnect(ups->conn);
    sfree(ups->conn);
    return -1;
  }

  while ((status = upscli_list_next(ups->conn, query_num, query, &answer_num,
                                    &answer)) == 1) {
    char *key;
    double value;

    if (answer_num < 4)
      continue;

    key = answer[2];
    value = atof(answer[3]);

    if (strncmp("ambient.", key, 8) == 0) {
      if (strcmp("ambient.humidity", key) == 0)
        nut_submit(ups, "humidity", "ambient", value);
      else if (strcmp("ambient.temperature", key) == 0)
        nut_submit(ups, "temperature", "ambient", value);
    } else if (strncmp("battery.", key, 8) == 0) {
      if (strcmp("battery.charge", key) == 0)
        nut_submit(ups, "percent", "charge", value);
      else if (strcmp("battery.current", key) == 0)
        nut_submit(ups, "current", "battery", value);
      else if (strcmp("battery.runtime", key) == 0)
        nut_submit(ups, "timeleft", "battery", value);
      else if (strcmp("battery.temperature", key) == 0)
        nut_submit(ups, "temperature", "battery", value);
      else if (strcmp("battery.voltage", key) == 0)
        nut_submit(ups, "voltage", "battery", value);
    } else if (strncmp("input.", key, 6) == 0) {
      if (strcmp("input.frequency", key) == 0)
        nut_submit(ups, "frequency", "input", value);
      else if (strcmp("input.voltage", key) == 0)
        nut_submit(ups, "voltage", "input", value);
    } else if (strncmp("output.", key, 7) == 0) {
      if (strcmp("output.current", key) == 0)
        nut_submit(ups, "current", "output", value);
      else if (strcmp("output.frequency", key) == 0)
        nut_submit(ups, "frequency", "output", value);
      else if (strcmp("output.voltage", key) == 0)
        nut_submit(ups, "voltage", "output", value);
    } else if (strncmp("ups.", key, 4) == 0) {
      if (strcmp("ups.load", key) == 0)
        nut_submit(ups, "percent", "load", value);
      else if (strcmp("ups.power", key) == 0)
        nut_submit(ups, "power", "ups", value);
      else if (strcmp("ups.temperature", key) == 0)
        nut_submit(ups, "temperature", "ups", value);
    }
  } /* while (upscli_list_next) */

  return 0;
} /* int nut_read */

static int nut_init(void) {
#if HAVE_UPSCLI_INIT
  if (verify_peer == 1 && force_ssl == 0) {
    WARNING("nut plugin: nut_connect: VerifyPeer true but ForceSSL "
            "false. Setting ForceSSL to true.");
    force_ssl = 1;
  }

  if (verify_peer == 1 && ca_path == NULL) {
    ERROR("nut plugin: nut_connect: VerifyPeer true but missing "
          "CAPath value.");
    plugin_unregister_read_group("nut");
    return -1;
  }

  if (verify_peer == 1 || force_ssl == 1) {
    int status = upscli_init(verify_peer, ca_path, NULL, NULL);

    if (status != 1) {
      ERROR("nut plugin: upscli_init (%i, %s) failed", verify_peer, ca_path);
      upscli_cleanup();
      plugin_unregister_read_group("nut");
      return -1;
    }
  } /* if (verify_peer == 1) */

  if (verify_peer == 1)
    ssl_flags = (UPSCLI_CONN_REQSSL | UPSCLI_CONN_CERTVERIF);
  else if (force_ssl == 1)
    ssl_flags = UPSCLI_CONN_REQSSL;

#else /* #if HAVE_UPSCLI_INIT */
  if (verify_peer == 1 || ca_path != NULL) {
    WARNING("nut plugin: nut_connect: Dependency libupsclient version "
            "insufficient (<2.7) for VerifyPeer support. Ignoring VerifyPeer "
            "and CAPath.");
    verify_peer = 0;
  }

  if (force_ssl == 1)
    ssl_flags = UPSCLI_CONN_REQSSL;
#endif

  if (connect_timeout <= 0)
    connect_timeout = (long)CDTIME_T_TO_MS(plugin_get_interval());

  return 0;
} /* int nut_init */

static int nut_shutdown(void) {
#if HAVE_UPSCLI_INIT
  upscli_cleanup();
#endif

  return 0;
} /* int nut_shutdown */

void module_register(void) {
  plugin_register_config("nut", nut_config, config_keys, config_keys_num);
  plugin_register_init("nut", nut_init);
  plugin_register_shutdown("nut", nut_shutdown);
} /* void module_register */
