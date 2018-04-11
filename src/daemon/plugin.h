/**
 * collectd - src/daemon/plugin.h
 * Copyright (C) 2005-2014  Florian octo Forster
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
 *   Sebastian Harl <sh at tokkee.org>
 **/

#ifndef PLUGIN_H
#define PLUGIN_H

#include "collectd.h"

#include "configfile.h"
#include "meta_data.h"
#include "utils_time.h"

#include <pthread.h>

#define DS_TYPE_COUNTER 0
#define DS_TYPE_GAUGE 1
#define DS_TYPE_DERIVE 2
#define DS_TYPE_ABSOLUTE 3

#define DS_TYPE_TO_STRING(t)                                                   \
  (t == DS_TYPE_COUNTER)                                                       \
      ? "counter"                                                              \
      : (t == DS_TYPE_GAUGE)                                                   \
            ? "gauge"                                                          \
            : (t == DS_TYPE_DERIVE)                                            \
                  ? "derive"                                                   \
                  : (t == DS_TYPE_ABSOLUTE) ? "absolute" : "unknown"

#ifndef LOG_ERR
#define LOG_ERR 3
#endif
#ifndef LOG_WARNING
#define LOG_WARNING 4
#endif
#ifndef LOG_NOTICE
#define LOG_NOTICE 5
#endif
#ifndef LOG_INFO
#define LOG_INFO 6
#endif
#ifndef LOG_DEBUG
#define LOG_DEBUG 7
#endif

#define NOTIF_MAX_MSG_LEN 256

#define NOTIF_FAILURE 1
#define NOTIF_WARNING 2
#define NOTIF_OKAY 4

#define plugin_interval (plugin_get_ctx().interval)

/*
 * Public data types
 */
struct identifier_s {
  char *host;
  char *plugin;
  char *plugin_instance;
  char *type;
  char *type_instance;
};
typedef struct identifier_s identifier_t;

typedef unsigned long long counter_t;
typedef double gauge_t;
typedef int64_t derive_t;
typedef uint64_t absolute_t;

union value_u {
  counter_t counter;
  gauge_t gauge;
  derive_t derive;
  absolute_t absolute;
};
typedef union value_u value_t;

struct value_list_s {
  value_t *values;
  size_t values_len;
  cdtime_t time;
  cdtime_t interval;
  char host[DATA_MAX_NAME_LEN];
  char plugin[DATA_MAX_NAME_LEN];
  char plugin_instance[DATA_MAX_NAME_LEN];
  char type[DATA_MAX_NAME_LEN];
  char type_instance[DATA_MAX_NAME_LEN];
  meta_data_t *meta;
};
typedef struct value_list_s value_list_t;

#define VALUE_LIST_INIT                                                        \
  { .values = NULL, .meta = NULL }

struct data_source_s {
  char name[DATA_MAX_NAME_LEN];
  int type;
  double min;
  double max;
};
typedef struct data_source_s data_source_t;

struct data_set_s {
  char type[DATA_MAX_NAME_LEN];
  size_t ds_num;
  data_source_t *ds;
};
typedef struct data_set_s data_set_t;

enum notification_meta_type_e {
  NM_TYPE_STRING,
  NM_TYPE_SIGNED_INT,
  NM_TYPE_UNSIGNED_INT,
  NM_TYPE_DOUBLE,
  NM_TYPE_BOOLEAN,
  NM_TYPE_NESTED
};

typedef struct notification_meta_s {
  char name[DATA_MAX_NAME_LEN];
  enum notification_meta_type_e type;
  union {
    const char *nm_string;
    int64_t nm_signed_int;
    uint64_t nm_unsigned_int;
    double nm_double;
    _Bool nm_boolean;
    struct notification_meta_s *nm_nested;
  } nm_value;
  struct notification_meta_s *next;
} notification_meta_t;

typedef struct notification_s {
  int severity;
  cdtime_t time;
  char message[NOTIF_MAX_MSG_LEN];
  char host[DATA_MAX_NAME_LEN];
  char plugin[DATA_MAX_NAME_LEN];
  char plugin_instance[DATA_MAX_NAME_LEN];
  char type[DATA_MAX_NAME_LEN];
  char type_instance[DATA_MAX_NAME_LEN];
  notification_meta_t *meta;
} notification_t;

struct user_data_s {
  void *data;
  void (*free_func)(void *);
};
typedef struct user_data_s user_data_t;

struct plugin_ctx_s {
  cdtime_t interval;
  cdtime_t flush_interval;
  cdtime_t flush_timeout;
};
typedef struct plugin_ctx_s plugin_ctx_t;

/*
 * Callback types
 */
typedef int (*plugin_init_cb)(void);
typedef int (*plugin_read_cb)(user_data_t *);
typedef int (*plugin_write_cb)(const data_set_t *, const value_list_t *,
                               user_data_t *);
typedef int (*plugin_flush_cb)(cdtime_t timeout, const char *identifier,
                               user_data_t *);
/* "missing" callback. Returns less than zero on failure, zero if other
 * callbacks should be called, greater than zero if no more callbacks should be
 * called. */
typedef int (*plugin_missing_cb)(const value_list_t *, user_data_t *);
typedef void (*plugin_log_cb)(int severity, const char *message, user_data_t *);
typedef int (*plugin_shutdown_cb)(void);
typedef int (*plugin_notification_cb)(const notification_t *, user_data_t *);
/*
 * NAME
 *  plugin_set_dir
 *
 * DESCRIPTION
 *  Sets the current `plugindir'
 *
 * ARGUMENTS
 *  `dir'       Path to the plugin directory
 *
 * NOTES
 *  If `dir' is NULL the compiled in default `PLUGINDIR' is used.
 */
void plugin_set_dir(const char *dir);

/*
 * NAME
 *  plugin_load
 *
 * DESCRIPTION
 *  Searches the current `plugindir' (see `plugin_set_dir') for the plugin
 *  named $type and loads it. Afterwards the plugin's `module_register'
 *  function is called, which then calls `plugin_register' to register callback
 *  functions.
 *
 * ARGUMENTS
 *  `name'      Name of the plugin to load.
 *  `global'    Make this plugins symbols available for other shared libraries.
 *
 * RETURN VALUE
 *  Returns zero upon success, a value greater than zero if no plugin was found
 *  and a value below zero if an error occurs.
 *
 * NOTES
 *  Re-loading an already loaded module is detected and zero is returned in
 *  this case.
 */
int plugin_load(const char *name, _Bool global);

int plugin_init_all(void);
void plugin_read_all(void);
int plugin_read_all_once(void);
int plugin_shutdown_all(void);

/*
 * NAME
 *  plugin_write
 *
 * DESCRIPTION
 *  Calls the write function of the given plugin with the provided data set and
 *  value list. It differs from `plugin_dispatch_value' in that it does not
 *  update the cache, does not do threshold checking, call the chain subsystem
 *  and so on. It looks up the requested plugin and invokes the function, end
 *  of story.
 *
 * ARGUMENTS
 *  plugin     Name of the plugin. If NULL, the value is sent to all registered
 *             write functions.
 *  ds         Pointer to the data_set_t structure. If NULL, the data set is
 *             looked up according to the `type' member in the `vl' argument.
 *  vl         The actual value to be processed. Must not be NULL.
 *
 * RETURN VALUE
 *  Returns zero upon success or non-zero if an error occurred. If `plugin' is
 *  NULL and more than one plugin is called, an error is only returned if *all*
 *  plugins fail.
 *
 * NOTES
 *  This is the function used by the `write' built-in target. May be used by
 *  other target plugins.
 */
int plugin_write(const char *plugin, const data_set_t *ds,
                 const value_list_t *vl);

int plugin_flush(const char *plugin, cdtime_t timeout, const char *identifier);

/*
 * The `plugin_register_*' functions are used to make `config', `init',
 * `read', `write' and `shutdown' functions known to the plugin
 * infrastructure. Also, the data-formats are made public like this.
 */
int plugin_register_config(const char *name,
                           int (*callback)(const char *key, const char *val),
                           const char **keys, int keys_num);
int plugin_register_complex_config(const char *type,
                                   int (*callback)(oconfig_item_t *));
int plugin_register_init(const char *name, plugin_init_cb callback);
int plugin_register_read(const char *name, int (*callback)(void));
/* "user_data" will be freed automatically, unless
 * "plugin_register_complex_read" returns an error (non-zero). */
int plugin_register_complex_read(const char *group, const char *name,
                                 plugin_read_cb callback, cdtime_t interval,
                                 user_data_t const *user_data);
int plugin_register_write(const char *name, plugin_write_cb callback,
                          user_data_t const *user_data);
int plugin_register_flush(const char *name, plugin_flush_cb callback,
                          user_data_t const *user_data);
int plugin_register_missing(const char *name, plugin_missing_cb callback,
                            user_data_t const *user_data);
int plugin_register_shutdown(const char *name, plugin_shutdown_cb callback);
int plugin_register_data_set(const data_set_t *ds);
int plugin_register_log(const char *name, plugin_log_cb callback,
                        user_data_t const *user_data);
int plugin_register_notification(const char *name,
                                 plugin_notification_cb callback,
                                 user_data_t const *user_data);

int plugin_unregister_config(const char *name);
int plugin_unregister_complex_config(const char *name);
int plugin_unregister_init(const char *name);
int plugin_unregister_read(const char *name);
int plugin_unregister_read_group(const char *group);
int plugin_unregister_write(const char *name);
int plugin_unregister_flush(const char *name);
int plugin_unregister_missing(const char *name);
int plugin_unregister_shutdown(const char *name);
int plugin_unregister_data_set(const char *name);
int plugin_unregister_log(const char *name);
int plugin_unregister_notification(const char *name);

/*
 * NAME
 *  plugin_log_available_writers
 *
 * DESCRIPTION
 *  This function can be called to output a list of _all_ registered
 *  writers to the logfacility.
 *  Since some writers dynamically build their name it can be hard for
 *  the configuring person to know it. This function will fill this gap.
 */
void plugin_log_available_writers(void);

/*
 * NAME
 *  plugin_dispatch_values
 *
 * DESCRIPTION
 *  This function is called by reading processes with the values they've
 *  aquired. The function fetches the data-set definition (that has been
 *  registered using `plugin_register_data_set') and calls _all_ registered
 *  write-functions.
 *
 * ARGUMENTS
 *  `vl'        Value list of the values that have been read by a `read'
 *              function.
 */
int plugin_dispatch_values(value_list_t const *vl);

/*
 * NAME
 *  plugin_dispatch_multivalue
 *
 * SYNOPSIS
 *  plugin_dispatch_multivalue (vl, 1, DS_TYPE_GAUGE,
 *                              "free", 42.0,
 *                              "used", 58.0,
 *                              NULL);
 *
 * DESCRIPTION
 *  Takes a list of type instances and values and dispatches that in a batch,
 *  making sure that all values have the same time stamp. If "store_percentage"
 *  is set to true, the "type" is set to "percent" and a percentage is
 *  calculated and dispatched, rather than the absolute values. Values that are
 *  NaN are dispatched as NaN and will not influence the total.
 *
 *  The variadic arguments is a list of type_instance / type pairs, that are
 *  interpreted as type "char const *" and type, encoded by their corresponding
 *  "store_type":
 *
 *     - "gauge_t"    when "DS_TYPE_GAUGE"
 *     - "absolute_t" when "DS_TYPE_ABSOLUTE"
 *     - "derive_t"   when "DS_TYPE_DERIVE"
 *     - "counter_t"  when "DS_TYPE_COUNTER"
 *
 *  The last argument must be
 *  a NULL pointer to signal end-of-list.
 *
 * RETURNS
 *  The number of values it failed to dispatch (zero on success).
 */
__attribute__((sentinel)) int plugin_dispatch_multivalue(value_list_t const *vl,
                                                         _Bool store_percentage,
                                                         int store_type, ...);

int plugin_dispatch_missing(const value_list_t *vl);

int plugin_dispatch_notification(const notification_t *notif);

void plugin_log(int level, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

/* These functions return the parsed severity or less than zero on failure. */
int parse_log_severity(const char *severity);
int parse_notif_severity(const char *severity);

#define ERROR(...) plugin_log(LOG_ERR, __VA_ARGS__)
#define WARNING(...) plugin_log(LOG_WARNING, __VA_ARGS__)
#define NOTICE(...) plugin_log(LOG_NOTICE, __VA_ARGS__)
#define INFO(...) plugin_log(LOG_INFO, __VA_ARGS__)
#if COLLECT_DEBUG
#define DEBUG(...) plugin_log(LOG_DEBUG, __VA_ARGS__)
#else              /* COLLECT_DEBUG */
#define DEBUG(...) /* noop */
#endif             /* ! COLLECT_DEBUG */

const data_set_t *plugin_get_ds(const char *name);

int plugin_notification_meta_append_string(notification_meta_t *m,
                                           const char *name, const char *value);
int plugin_notification_meta_add_string(notification_t *n, const char *name,
                                        const char *value);
int plugin_notification_meta_append_signed_int(notification_meta_t *m,
                                               const char *name, int64_t value);
int plugin_notification_meta_add_signed_int(notification_t *n, const char *name,
                                            int64_t value);
int plugin_notification_meta_append_unsigned_int(notification_meta_t *m,
                                                 const char *name,
                                                 uint64_t value);
int plugin_notification_meta_add_unsigned_int(notification_t *n,
                                              const char *name, uint64_t value);
int plugin_notification_meta_append_double(notification_meta_t *m,
                                           const char *name, double value);
int plugin_notification_meta_add_double(notification_t *n, const char *name,
                                        double value);
int plugin_notification_meta_append_boolean(notification_meta_t *m,
                                            const char *name, _Bool value);
int plugin_notification_meta_add_boolean(notification_t *n, const char *name,
                                         _Bool value);
int plugin_notification_meta_append_nested(notification_meta_t *m,
                                           const char *name);
int plugin_notification_meta_add_nested(notification_t *n, const char *name);

int plugin_notification_meta_get_meta_tail(notification_t *n,
                                           notification_meta_t **tail);

int plugin_notification_meta_get_nested_tail(notification_meta_t *m,
                                             notification_meta_t **tail);

int plugin_notification_meta_copy(notification_t *dst,
                                  const notification_t *src);

int plugin_notification_meta_free(notification_meta_t *n);

/*
 * Plugin context management.
 */

void plugin_init_ctx(void);

plugin_ctx_t plugin_get_ctx(void);
plugin_ctx_t plugin_set_ctx(plugin_ctx_t ctx);

/*
 * NAME
 *  plugin_get_interval
 *
 * DESCRIPTION
 *  This function returns the current value of the plugin's interval. The
 *  return value will be strictly greater than zero in all cases. If
 *  everything else fails, it will fall back to 10 seconds.
 */
cdtime_t plugin_get_interval(void);

/*
 * Context-aware thread management.
 */

int plugin_thread_create(pthread_t *thread, const pthread_attr_t *attr,
                         void *(*start_routine)(void *), void *arg,
                         char const *name);

/*
 * Plugins need to implement this
 */

void module_register(void);

#endif /* PLUGIN_H */
