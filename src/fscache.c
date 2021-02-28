/**
 * collectd - src/fscache.c
 * Copyright (C) 2009 Edward "Koko" Konetzko
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
 *   Edward "Koko" Konetzko <konetzed at quixoticagony.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include <stdio.h>  /* a header needed for FILE */
#include <stdlib.h> /* used for atoi */
#include <string.h> /* a header needed for scanf function */

/* generated form fscache.gperf */
#include "fscache.h"

#if !KERNEL_LINUX
#error "This module only supports the Linux implementation of fscache"
#endif

#define BUFSIZE 1024

/*
see /proc/fs/fscache/stats
see Documentation/filesystems/caching/fscache.txt in linux kernel >= 2.6.30
*/
static void fscache_submit(const struct fscache_metric *m, counter_t value) {
  metric_family_t fam = {
      .name = m->name,
      .type = m->type,
      .help = m->help,
  };

  metric_t metric = {0};

  if (fam.type == METRIC_TYPE_COUNTER)
    metric.value.counter = value;
  else
    metric.value.gauge = value;

  metric_family_metric_append(&fam, metric);

  int status = plugin_dispatch_metric_family(&fam);
  if (status != 0) {
    ERROR("fscache plugin: plugin_dispatch_metric_family failed: %s",
          STRERROR(status));
  }

  metric_family_metric_reset(&fam);
}

static void fscache_read_stats_file(FILE *fh) {
  char linebuffer[BUFSIZE];
  /*
   *  cat /proc/fs/fscache/stats
   *      FS-Cache statistics
   *      Cookies: idx=0 dat=0 spc=0
   *      Objects: alc=0 nal=0 avl=0 ded=0
   *      ChkAux : non=0 ok=0 upd=0 obs=0
   *      Pages  : mrk=0 unc=0
   *      Acquire: n=0 nul=0 noc=0 ok=0 nbf=0 oom=0
   *      Lookups: n=0 neg=0 pos=0 crt=0 tmo=0
   *      Invals : n=0 run=0
   *      Updates: n=0 nul=0 run=0
   *      Relinqs: n=0 nul=0 wcr=0 rtr=0
   *      AttrChg: n=0 ok=0 nbf=0 oom=0 run=0
   *      Allocs : n=0 ok=0 wt=0 nbf=0 int=0
   *      Allocs : ops=0 owt=0 abt=0
   *      Retrvls: n=0 ok=0 wt=0 nod=0 nbf=0 int=0 oom=0
   *      Retrvls: ops=0 owt=0 abt=0
   *      Stores : n=0 ok=0 agn=0 nbf=0 oom=0
   *      Stores : ops=0 run=0 pgs=0 rxd=0 olm=0
   *      VmScan : nos=0 gon=0 bsy=0 can=0 wt=0
   *      Ops    : pend=0 run=0 enq=0 can=0 rej=0
   *      Ops    : ini=0 dfr=0 rel=0 gc=0
   *      CacheOp: alo=0 luo=0 luc=0 gro=0
   *      CacheOp: inv=0 upo=0 dro=0 pto=0 atc=0 syn=0
   *      CacheOp: rap=0 ras=0 alp=0 als=0 wrp=0 ucp=0 dsp=0
   *      CacheEv: nsp=0 stl=0 rtr=0 cul=0
   */

  /* Read file line by line */
  while (fgets(linebuffer, sizeof(linebuffer), fh) != NULL) {
    char section[DATA_MAX_NAME_LEN];
    char *lineptr;
    char *fields[32];
    int fields_num;

    /* Find the colon and replace it with a null byte */
    lineptr = strchr(linebuffer, ':');
    if (lineptr == NULL)
      continue;
    *lineptr = 0;
    lineptr++;

    /* Copy and clean up the section name */
    sstrncpy(section, linebuffer, sizeof(section));
    size_t section_len = strlen(section);
    while ((section_len > 0) && isspace((int)section[section_len - 1])) {
      section_len--;
      section[section_len] = 0;
    }
    if (section_len == 0)
      continue;

    fields_num = strsplit(lineptr, fields, STATIC_ARRAY_SIZE(fields));
    if (fields_num <= 0)
      continue;

    for (int i = 0; i < fields_num; i++) {
      char *field_name;
      char *field_value_str;
      value_t field_value_cnt;
      int status;

      field_name = fields[i];
      assert(field_name != NULL);

      field_value_str = strchr(field_name, '=');
      if (field_value_str == NULL)
        continue;
      *field_value_str = 0;
      size_t field_name_len = field_value_str - field_name;
      field_value_str++;

      sstrncpy(section + section_len, field_name,
               sizeof(section) - section_len);
      const struct fscache_metric *m =
          fscache_get_key(section, section_len + field_name_len);
      if (m != NULL) {

        status =
            parse_value(field_value_str, &field_value_cnt, DS_TYPE_COUNTER);
        if (status != 0)
          continue;

        fscache_submit(m, field_value_cnt.counter);
      } else {
        DEBUG("fscache plugin: metric not found for: %.*s %s", (int)section_len,
              section, field_name);
      }
    }
  } /* while (fgets) */
} /* void fscache_read_stats_file */

static int fscache_read(void) {
  FILE *fh;
  fh = fopen("/proc/fs/fscache/stats", "r");
  if (fh != NULL) {
    fscache_read_stats_file(fh);
    fclose(fh);
  } else {
    ERROR("fscache plugin: cant open file /proc/fs/fscache/stats");
    return -1;
  }
  return 0;
}

void module_register(void) {
  plugin_register_read("fscache", fscache_read);
} /* void module_register */
