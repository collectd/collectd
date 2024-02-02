/**
 * collectd - src/open_telemetry.cc
 * Copyright (C) 2024       Florian octo Forster
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
 **/

extern "C" {
#include "collectd.h"

#include "daemon/configfile.h"
#include "daemon/plugin.h"
}

int exporter_config(oconfig_item_t *ci);
int receiver_config(oconfig_item_t *ci);

static int ot_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Exporter", child->key) == 0) {
      int err = exporter_config(child);
      if (err) {
        ERROR("open_telemetry plugin: Configuring exporter failed "
              "with status %d",
              err);
        return err;
      }
    } else if (strcasecmp("Receiver", child->key) == 0) {
      int err = receiver_config(child);
      if (err) {
        ERROR("open_telemetry plugin: Configuring receiver failed "
              "with status %d",
              err);
        return err;
      }
    } else {
      ERROR("open_telemetry plugin: invalid config option: \"%s\"", child->key);
      return EINVAL;
    }
  }

  return 0;
}

extern "C" {
void module_register(void) {
  plugin_register_complex_config("open_telemetry", ot_config);
}
}
