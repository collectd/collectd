/**
 * collectd - src/vserver.c
 * Copyright (C) 2006,2007  Sebastian Harl
 * Copyright (C) 2007-2010  Florian octo Forster
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
 *   Sebastian Harl <sh at tokkee.org>
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include <dirent.h>
#include <sys/types.h>

#define BUFSIZE 512

#define PROCDIR "/proc/virtual"

#if !KERNEL_LINUX
#error "No applicable input method."
#endif

static int pagesize = 0;

static int vserver_init(void) {
  /* XXX Should we check for getpagesize () in configure?
   * What's the right thing to do, if there is no getpagesize ()? */
  pagesize = getpagesize();

  return 0;
} /* static void vserver_init(void) */

static void traffic_submit(const char *plugin_instance,
                           const char *type_instance, derive_t rx,
                           derive_t tx) {
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[] = {
      {.derive = rx}, {.derive = tx},
  };

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);
  sstrncpy(vl.plugin, "vserver", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "if_octets", sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* void traffic_submit */

static void load_submit(const char *plugin_instance, gauge_t snum, gauge_t mnum,
                        gauge_t lnum) {
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[] = {
      {.gauge = snum}, {.gauge = mnum}, {.gauge = lnum},
  };

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);
  sstrncpy(vl.plugin, "vserver", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "load", sizeof(vl.type));

  plugin_dispatch_values(&vl);
}

static void submit_gauge(const char *plugin_instance, const char *type,
                         const char *type_instance, gauge_t value)

{
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "vserver", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* void submit_gauge */

static derive_t vserver_get_sock_bytes(const char *s) {
  value_t v;
  int status;

  while (s[0] != '/')
    ++s;

  /* Remove '/' */
  ++s;

  status = parse_value(s, &v, DS_TYPE_DERIVE);
  if (status != 0)
    return -1;
  return v.derive;
}

static int vserver_read(void) {
  DIR *proc;

  errno = 0;
  proc = opendir(PROCDIR);
  if (proc == NULL) {
    ERROR("vserver plugin: fopen (%s): %s", PROCDIR, STRERRNO);
    return -1;
  }

  while (42) {
    struct dirent *dent;
    int len;
    char file[BUFSIZE];

    FILE *fh;
    char buffer[BUFSIZE];

    struct stat statbuf;
    char *cols[4];

    int status;

    errno = 0;
    dent = readdir(proc);
    if (dent == NULL) {
      if (errno == 0) /* end of directory */
        break;

      ERROR("vserver plugin: failed to read directory %s: %s", PROCDIR,
            STRERRNO);
      closedir(proc);
      return -1;
    }

    if (dent->d_name[0] == '.')
      continue;

    len = snprintf(file, sizeof(file), PROCDIR "/%s", dent->d_name);
    if ((len < 0) || (len >= BUFSIZE))
      continue;

    status = stat(file, &statbuf);
    if (status != 0) {
      WARNING("vserver plugin: stat (%s) failed: %s", file, STRERRNO);
      continue;
    }

    if (!S_ISDIR(statbuf.st_mode))
      continue;

    /* socket message accounting */
    len = snprintf(file, sizeof(file), PROCDIR "/%s/cacct", dent->d_name);
    if ((len < 0) || ((size_t)len >= sizeof(file)))
      continue;

    if (NULL == (fh = fopen(file, "r"))) {
      ERROR("Cannot open '%s': %s", file, STRERRNO);
    }

    while ((fh != NULL) && (NULL != fgets(buffer, BUFSIZE, fh))) {
      derive_t rx;
      derive_t tx;
      const char *type_instance;

      if (strsplit(buffer, cols, 4) < 4)
        continue;

      if (0 == strcmp(cols[0], "UNIX:"))
        type_instance = "unix";
      else if (0 == strcmp(cols[0], "INET:"))
        type_instance = "inet";
      else if (0 == strcmp(cols[0], "INET6:"))
        type_instance = "inet6";
      else if (0 == strcmp(cols[0], "OTHER:"))
        type_instance = "other";
      else if (0 == strcmp(cols[0], "UNSPEC:"))
        type_instance = "unspec";
      else
        continue;

      rx = vserver_get_sock_bytes(cols[1]);
      tx = vserver_get_sock_bytes(cols[2]);
      /* cols[3] == errors */

      traffic_submit(dent->d_name, type_instance, rx, tx);
    } /* while (fgets) */

    if (fh != NULL) {
      fclose(fh);
      fh = NULL;
    }

    /* thread information and load */
    len = snprintf(file, sizeof(file), PROCDIR "/%s/cvirt", dent->d_name);
    if ((len < 0) || ((size_t)len >= sizeof(file)))
      continue;

    if (NULL == (fh = fopen(file, "r"))) {
      ERROR("Cannot open '%s': %s", file, STRERRNO);
    }

    while ((fh != NULL) && (NULL != fgets(buffer, BUFSIZE, fh))) {
      int n = strsplit(buffer, cols, 4);

      if (2 == n) {
        const char *type_instance;
        gauge_t value;

        if (0 == strcmp(cols[0], "nr_threads:"))
          type_instance = "total";
        else if (0 == strcmp(cols[0], "nr_running:"))
          type_instance = "running";
        else if (0 == strcmp(cols[0], "nr_unintr:"))
          type_instance = "uninterruptable";
        else if (0 == strcmp(cols[0], "nr_onhold:"))
          type_instance = "onhold";
        else
          continue;

        value = atof(cols[1]);
        submit_gauge(dent->d_name, "vs_threads", type_instance, value);
      } else if (4 == n) {
        if (0 == strcmp(cols[0], "loadavg:")) {
          gauge_t snum = atof(cols[1]);
          gauge_t mnum = atof(cols[2]);
          gauge_t lnum = atof(cols[3]);
          load_submit(dent->d_name, snum, mnum, lnum);
        }
      }
    } /* while (fgets) */

    if (fh != NULL) {
      fclose(fh);
      fh = NULL;
    }

    /* processes and memory usage */
    len = snprintf(file, sizeof(file), PROCDIR "/%s/limit", dent->d_name);
    if ((len < 0) || ((size_t)len >= sizeof(file)))
      continue;

    if (NULL == (fh = fopen(file, "r"))) {
      ERROR("Cannot open '%s': %s", file, STRERRNO);
    }

    while ((fh != NULL) && (NULL != fgets(buffer, BUFSIZE, fh))) {
      const char *type = "vs_memory";
      const char *type_instance;
      gauge_t value;

      if (strsplit(buffer, cols, 2) < 2)
        continue;

      if (0 == strcmp(cols[0], "PROC:")) {
        type = "vs_processes";
        type_instance = "";
        value = atof(cols[1]);
      } else {
        if (0 == strcmp(cols[0], "VM:"))
          type_instance = "vm";
        else if (0 == strcmp(cols[0], "VML:"))
          type_instance = "vml";
        else if (0 == strcmp(cols[0], "RSS:"))
          type_instance = "rss";
        else if (0 == strcmp(cols[0], "ANON:"))
          type_instance = "anon";
        else
          continue;

        value = atof(cols[1]) * pagesize;
      }

      submit_gauge(dent->d_name, type, type_instance, value);
    } /* while (fgets) */

    if (fh != NULL) {
      fclose(fh);
      fh = NULL;
    }
  } /* while (readdir) */

  closedir(proc);

  return 0;
} /* int vserver_read */

void module_register(void) {
  plugin_register_init("vserver", vserver_init);
  plugin_register_read("vserver", vserver_read);
} /* void module_register(void) */
