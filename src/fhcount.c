/**
 *
 * collectd - src/fhcount.c
 * Copyright (c) 2015, Jiri Tyr <jiri.tyr at gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"


static const char *config_keys[] = {
  "ValuesAbsolute",
  "ValuesPercentage"
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static _Bool values_absolute = 1;
static _Bool values_percentage = 0;


static int fhcount_config(const char *key, const char *value) {
  int ret = -1;

  if (strcasecmp(key, "ValuesAbsolute") == 0) {
    if (IS_TRUE(value)) {
      values_absolute = 1;
    } else {
      values_absolute = 0;
    }

    ret = 0;
  } else if (strcasecmp(key, "ValuesPercentage") == 0) {
    if (IS_TRUE(value)) {
      values_percentage = 1;
    } else {
      values_percentage = 0;
    }

    ret = 0;
  }

  return(ret);
}


static void fhcount_submit(
    const char *type, const char *type_instance, gauge_t value) {

  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;

  // Compose the metric
  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.plugin, "fhcount", sizeof(vl.plugin));
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  // Dispatch the metric
  plugin_dispatch_values(&vl);
}


static int fhcount_read(void) {
  int numfields = 0;
  int buffer_len = 60;
  gauge_t used, unused, max;
  int prc_used, prc_unused;
  char *fields[3];
  char buffer[buffer_len];
  char errbuf[1024];
  FILE *fp;

  // Open file
  fp = fopen("/proc/sys/fs/file-nr" , "r");
  if (fp == NULL) {
    ERROR("fhcount: fopen: %s", sstrerror(errno, errbuf, sizeof(errbuf)));
    return(EXIT_FAILURE);
  }
  if (fgets(buffer, buffer_len, fp) == NULL) {
    ERROR("fhcount: fgets: %s", sstrerror(errno, errbuf, sizeof(errbuf)));
    return(EXIT_FAILURE);
  }
  fclose(fp);

  // Tokenize string
  numfields = strsplit(buffer, fields, STATIC_ARRAY_SIZE(fields));

  if (numfields != 3) {
    ERROR("fhcount: Line doesn't contain 3 fields");
    return(EXIT_FAILURE);
  }

  // Define the values
  strtogauge(fields[0], &used);
  strtogauge(fields[1], &unused);
  strtogauge(fields[2], &max);
  prc_used = (gauge_t) used/max*100;
  prc_unused = (gauge_t) unused/max*100;

  // Submit values
  if (values_absolute) {
    fhcount_submit("file_handles", "used", (gauge_t) used);
    fhcount_submit("file_handles", "unused", (gauge_t) unused);
    fhcount_submit("file_handles", "max", (gauge_t) max);
  }
  if (values_percentage) {
    fhcount_submit("percent", "used", (gauge_t) prc_used);
    fhcount_submit("percent", "unused", (gauge_t) prc_unused);
  }

  return(0);
}


void module_register(void) {
  plugin_register_config(
    "fhcount", fhcount_config, config_keys, config_keys_num);
  plugin_register_read("fhcount", fhcount_read);
}
