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

#if HAVE_LIBKSTAT
kstat_ctl_t *kc = NULL;
#endif /* HAVE_LIBKSTAT */

char hostname_g[] = "example.com";

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

int plugin_register_shutdown(const char *name, int (*callback)(void)) {
  return ENOTSUP;
}

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

cdtime_t plugin_get_interval(void) { return TIME_T_TO_CDTIME_T(10); }

/* vim: set sw=2 sts=2 et : */
