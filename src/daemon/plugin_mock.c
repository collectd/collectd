/**
 * collectd - src/tests/mock/plugin.c
 * Copyright (C) 2013       Florian octo Forster
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
 */

#include "plugin.h"

#if HAVE_KSTAT_H
#include <kstat.h>
#endif

#if HAVE_LIBKSTAT
kstat_ctl_t *kc = NULL;
#endif /* HAVE_LIBKSTAT */

char *hostname_g = "example.com";

void plugin_set_dir(const char *dir) { /* nop */
}

int plugin_load(const char *name, bool global) { return ENOTSUP; }

int plugin_register_config(const char *name,
                           int (*callback)(const char *key, const char *val),
                           const char **keys, int keys_num) {
  return ENOTSUP;
}

int plugin_register_complex_config(const char *type,
                                   int (*callback)(oconfig_item_t *)) {
  return ENOTSUP;
}

int plugin_register_init(const char *name, plugin_init_cb callback) {
  return ENOTSUP;
}

int plugin_register_read(__attribute__((unused)) const char *name,
                         __attribute__((unused)) int (*callback)(void)) {
  return ENOTSUP;
}

int plugin_register_write(__attribute__((unused)) const char *name,
                          __attribute__((unused)) plugin_write_cb callback,
                          __attribute__((unused)) user_data_t const *ud) {
  return ENOTSUP;
}

int plugin_register_missing(const char *name, plugin_missing_cb callback,
                            user_data_t const *ud) {
  return ENOTSUP;
}

int plugin_register_complex_read(const char *group, const char *name,
                                 int (*callback)(user_data_t *),
                                 cdtime_t interval,
                                 user_data_t const *user_data) {
  return ENOTSUP;
}

int plugin_register_shutdown(const char *name, int (*callback)(void)) {
  return ENOTSUP;
}

int plugin_register_data_set(const data_set_t *ds) { return ENOTSUP; }

int plugin_dispatch_values(value_list_t const *vl) { return ENOTSUP; }

int plugin_dispatch_notification(__attribute__((unused))
                                 const notification_t *notif) {
  return ENOTSUP;
}

int plugin_notification_meta_add_string(__attribute__((unused))
                                        notification_t *n,
                                        __attribute__((unused))
                                        const char *name,
                                        __attribute__((unused))
                                        const char *value) {
  return ENOTSUP;
}

int plugin_notification_meta_add_signed_int(__attribute__((unused))
                                            notification_t *n,
                                            __attribute__((unused))
                                            const char *name,
                                            __attribute__((unused))
                                            int64_t value) {
  return ENOTSUP;
}

int plugin_notification_meta_add_unsigned_int(__attribute__((unused))
                                              notification_t *n,
                                              __attribute__((unused))
                                              const char *name,
                                              __attribute__((unused))
                                              uint64_t value) {
  return ENOTSUP;
}

int plugin_notification_meta_add_double(__attribute__((unused))
                                        notification_t *n,
                                        __attribute__((unused))
                                        const char *name,
                                        __attribute__((unused)) double value) {
  return ENOTSUP;
}

int plugin_notification_meta_add_boolean(__attribute__((unused))
                                         notification_t *n,
                                         __attribute__((unused))
                                         const char *name,
                                         __attribute__((unused)) _Bool value) {
  return ENOTSUP;
}

int plugin_notification_meta_copy(__attribute__((unused)) notification_t *dst,
                                  __attribute__((unused))
                                  const notification_t *src) {
  return ENOTSUP;
}

int plugin_notification_meta_free(__attribute__((unused))
                                  notification_meta_t *n) {
  return ENOTSUP;
}

int plugin_flush(const char *plugin, cdtime_t timeout, const char *identifier) {
  return ENOTSUP;
}

static data_source_t magic_ds[] = {{"value", DS_TYPE_DERIVE, 0.0, NAN}};
static data_set_t magic = {"MAGIC", 1, magic_ds};
const data_set_t *plugin_get_ds(const char *name) {
  if (strcmp(name, "MAGIC"))
    return NULL;

  return &magic;
}

void plugin_log(int level, char const *format, ...) {
  char buffer[1024];
  va_list ap;

  va_start(ap, format);
  vsnprintf(buffer, sizeof(buffer), format, ap);
  va_end(ap);

  printf("plugin_log (%i, \"%s\");\n", level, buffer);
}

void daemon_log(int level, char const *format, ...) {
  char buffer[1024];
  va_list ap;

  va_start(ap, format);
  vsnprintf(buffer, sizeof(buffer), format, ap);
  va_end(ap);

  printf("daemon_log (%i, \"%s\");\n", level, buffer);
}

void plugin_init_ctx(void) { /* nop */
}

plugin_ctx_t mock_context = {
    .interval = TIME_T_TO_CDTIME_T_STATIC(10),
};

plugin_ctx_t plugin_get_ctx(void) { return mock_context; }

plugin_ctx_t plugin_set_ctx(plugin_ctx_t ctx) {
  plugin_ctx_t prev = mock_context;
  mock_context = ctx;
  return prev;
}

cdtime_t plugin_get_interval(void) { return mock_context.interval; }

/* TODO(octo): this function is actually from filter_chain.h, but in order not
 * to tumble down that rabbit hole, we're declaring it here. A better solution
 * would be to hard-code the top-level config keys in daemon/collectd.c to avoid
 * having these references in daemon/configfile.c. */
int fc_configure(const oconfig_item_t *ci) { return ENOTSUP; }
