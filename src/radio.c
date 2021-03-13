/**
 * collectd - src/radio.c
 * Copyright (C) 2016 rinigus
 *
 *
 The MIT License (MIT)

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.

 * Authors:
 *	 rinigus <http://github.com/rinigus>

 Radio switches are monitored, as exposed by rfkill interface in Linux

 Sysfs description:
https://www.kernel.org/doc/Documentation/ABI/stable/sysfs-class-rfkill


**/

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"

#define SYSFS_ROOT "/sys/class/rfkill/"
#define BUFFER_SIZE 512

static int num_rfkill;

static int radio_init(void) {
  int status;
  char filename[256];

  num_rfkill = 0;

  while (1) {
    status = ssnprintf(filename, sizeof(filename),
                       SYSFS_ROOT "rfkill%d/"
                                  "hard",
                       num_rfkill);

    if ((status < 1) || ((size_t)status >= sizeof(filename)))
      break;

    if (access(filename, R_OK))
      break;

    num_rfkill++;
  }

  INFO("radio plugin: Found %d radio%s", num_rfkill,
       (num_rfkill == 1) ? "" : "s");

  if (num_rfkill == 0)
    plugin_unregister_read("radio");

  return (0);
}

static void radio_submit(const char *type, const char *plugin_instance,
                         const char *type_instance, gauge_t value) {
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.plugin, "radio", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static _Bool get_value(const char *fname, int *value) {
  FILE *fh;
  char buffer[BUFFER_SIZE];

  if ((fh = fopen(fname, "r")) == NULL)
    return (0);

  if (fgets(buffer, sizeof(buffer), fh) == NULL) {
    fclose(fh);
    return (0); // empty file
  }

  *value = atoi(buffer);

  fclose(fh);

  return (1);
}

static _Bool get_string(const char *fname, char *buffer, size_t buffer_size) {
  FILE *fh;
  if ((fh = fopen(fname, "r")) == NULL)
    return (0);

  if (fgets(buffer, buffer_size, fh) == NULL) {
    fclose(fh);
    return (0); // empty file
  }

  fclose(fh);

  return (1);
}

static void sanitize(char *buffer, size_t buffer_size) {
  size_t i;
  for (i = 0; i < buffer_size; ++i) {
    char c = buffer[i];
    if (c == '\0')
      return;
    if (c == '\n') {
      buffer[i] = '\0';
      return;
    }

    if (i == buffer_size - 1) {
      buffer[i] = '\0';
      return;
    }

    if (c == ' ' || c == '-' || c == '/')
      buffer[i] = '_';
  }
}

static int radio_read(void) {
  char filename[BUFFER_SIZE];
  char type[BUFFER_SIZE];
  char name[BUFFER_SIZE];
  int status;
  int hard;
  int soft;
  int i;

  for (i = 0; i < num_rfkill; ++i) {
    // type
    status = ssnprintf(filename, sizeof(filename),
                       SYSFS_ROOT "rfkill%d/"
                                  "type",
                       i);

    if ((status < 1) || ((size_t)status >= sizeof(filename)))
      return (-1);

    if (!get_string(filename, type, sizeof(type))) {
      ERROR("radio: cannot read value from %s", filename);
      return (-1);
    }

    sanitize(type, sizeof(type));

    // name
    status = ssnprintf(filename, sizeof(filename),
                       SYSFS_ROOT "rfkill%d/"
                                  "name",
                       i);

    if ((status < 1) || ((size_t)status >= sizeof(filename)))
      return (-1);

    if (!get_string(filename, name, sizeof(name))) {
      ERROR("radio: cannot read value from %s", filename);
      return (-1);
    }

    sanitize(name, sizeof(name));

    // hardware switch
    status = ssnprintf(filename, sizeof(filename),
                       SYSFS_ROOT "rfkill%d/"
                                  "hard",
                       i);

    if ((status < 1) || ((size_t)status >= sizeof(filename)))
      return (-1);

    if (!get_value(filename, &hard)) {
      ERROR("radio: cannot read value from %s", filename);
      return (-1);
    }

    if (hard) // radio is killed by hardware switch
    {
      radio_submit("active", type, name, 0);
      continue; // no need to read soft switch
    }

    // software switch
    status = ssnprintf(filename, sizeof(filename),
                       SYSFS_ROOT "rfkill%d/"
                                  "soft",
                       i);

    if ((status < 1) || ((size_t)status >= sizeof(filename)))
      return (-1);

    if (!get_value(filename, &soft)) {
      ERROR("radio: cannot read value from %s", filename);
      return (-1);
    }

    radio_submit("active", type, name, (hard == 0 && soft == 0));
  }

  return (0);
}

void module_register(void) {
  plugin_register_init("radio", radio_init);
  plugin_register_read("radio", radio_read);
} /* void module_register */
