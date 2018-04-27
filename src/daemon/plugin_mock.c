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

int plugin_load(const char *name, _Bool global) { return ENOTSUP; }

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

int plugin_register_read(const char *name, int (*callback)(void)) {
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
