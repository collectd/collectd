/**
 * collectd - src/daemon/resource.c
 * Copyright (C) 2023       Florian octo Forster
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

#include "collectd.h"
#include "daemon/resource.h"

#include "utils/common/common.h"

static bool default_resource_initialized = false;
static label_set_t default_resource;

static void otel_service_name(void) {
  char *sn = getenv("OTEL_SERVICE_NAME");
  if (sn != NULL) {
    label_set_add(&default_resource, "service.name", sn);
    return;
  }

  strbuf_t buf = STRBUF_CREATE;
  strbuf_print(&buf, "unknown_service:" PACKAGE_NAME);
  label_set_add(&default_resource, "service.name", buf.ptr);
  STRBUF_DESTROY(buf);
}

static void otel_resource_attributes(void) {
  char *ra = getenv("OTEL_RESOURCE_ATTRIBUTES");
  if (ra == NULL) {
    return;
  }

  size_t tmp_sz = strlen(ra) + 2;
  char tmp[tmp_sz];
  sstrncpy(tmp, ra, sizeof(tmp));

  char *str = &tmp[0];
  char *saveptr = NULL;
  char *key;
  while ((key = strtok_r(str, ",", &saveptr)) != NULL) {
    str = NULL;
    char *value = strchr(key, '=');
    if (value == NULL) {
      continue;
    }
    *value = 0;
    value++;

    label_set_add(&default_resource, key, value);
  }
}

static void host_name(void) {
  if (strlen(hostname_g) > 0) {
    label_set_add(&default_resource, "host.name", hostname_g);
  }
}

static int machine_id(void) {
  char *files[] = {
      "/etc/machine-id",
      "/etc/hostid",
      "/var/lib/dbus/machine-id",
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(files); i++) {
    char *f = files[i];
    if (access(f, R_OK) != 0) {
      continue;
    }

    char buf[1024] = {0};
    ssize_t status = read_text_file_contents(f, buf, sizeof(buf));
    if (status <= 0) {
      NOTICE("machine_id: reading \"%s\" failed: %zd", f, status);
      continue;
    }
    strstripnewline(buf);

    label_set_add(&default_resource, "host.id", buf);
    return 0;
  }

  return ENOENT;
}

static void resource_host_init(void) {
  if (default_resource_initialized) {
    return;
  }

  otel_service_name();
  otel_resource_attributes();
  host_name();
  machine_id();
  default_resource_initialized = true;
}

static void resource_generic_init(void) {
  if (default_resource_initialized) {
    return;
  }

  otel_service_name();
  otel_resource_attributes();
  default_resource_initialized = true;
}

int resource_attributes_init(char const *type) {
  if (strcasecmp("Host", type) == 0) {
    resource_host_init();
    return 0;
  } else if (strcasecmp("Generic", type) == 0) {
    resource_generic_init();
    return 0;
  }
  ERROR("resource: The resource type \"%s\" is unknown.", type);
  return ENOENT;
}

int resource_attribute_update(char const *key, char const *value) {
  resource_host_init();
  return label_set_add(&default_resource, key, value);
}

label_set_t default_resource_attributes(void) {
  resource_host_init();

  return default_resource;
}
