/**
 * collectd - src/utils_rrdcreate.c
 * Copyright (C) 2006-2013  Florian octo Forster
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

#include "common.h"
#include "utils_rrdcreate.h"

#include <pthread.h>
#include <rrd.h>

struct srrd_create_args_s {
  char *filename;
  unsigned long pdp_step;
  time_t last_up;
  int argc;
  char **argv;
};
typedef struct srrd_create_args_s srrd_create_args_t;

struct async_create_file_s;
typedef struct async_create_file_s async_create_file_t;
struct async_create_file_s {
  char *filename;
  async_create_file_t *next;
};

/*
 * Private variables
 */
static int rra_timespans[] = {3600, 86400, 604800, 2678400, 31622400};
static int rra_timespans_num = STATIC_ARRAY_SIZE(rra_timespans);

static const char *const rra_types[] = {"AVERAGE", "MIN", "MAX"};
static int rra_types_num = STATIC_ARRAY_SIZE(rra_types);

#if !defined(HAVE_THREADSAFE_LIBRRD)
static pthread_mutex_t librrd_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static async_create_file_t *async_creation_list = NULL;
static pthread_mutex_t async_creation_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Private functions
 */
static void rra_free(int rra_num, char **rra_def) /* {{{ */
{
  for (int i = 0; i < rra_num; i++) {
    sfree(rra_def[i]);
  }
  sfree(rra_def);
} /* }}} void rra_free */

static void srrd_create_args_destroy(srrd_create_args_t *args) {
  if (args == NULL)
    return;

  sfree(args->filename);
  if (args->argv != NULL) {
    for (int i = 0; i < args->argc; i++)
      sfree(args->argv[i]);
    sfree(args->argv);
  }
  sfree(args);
} /* void srrd_create_args_destroy */

static srrd_create_args_t *srrd_create_args_create(const char *filename,
                                                   unsigned long pdp_step,
                                                   time_t last_up, int argc,
                                                   const char **argv) {
  srrd_create_args_t *args;

  args = calloc(1, sizeof(*args));
  if (args == NULL) {
    ERROR("srrd_create_args_create: calloc failed.");
    return NULL;
  }
  args->filename = NULL;
  args->pdp_step = pdp_step;
  args->last_up = last_up;
  args->argv = NULL;

  args->filename = strdup(filename);
  if (args->filename == NULL) {
    ERROR("srrd_create_args_create: strdup failed.");
    srrd_create_args_destroy(args);
    return NULL;
  }

  args->argv = calloc((size_t)(argc + 1), sizeof(*args->argv));
  if (args->argv == NULL) {
    ERROR("srrd_create_args_create: calloc failed.");
    srrd_create_args_destroy(args);
    return NULL;
  }

  for (args->argc = 0; args->argc < argc; args->argc++) {
    args->argv[args->argc] = strdup(argv[args->argc]);
    if (args->argv[args->argc] == NULL) {
      ERROR("srrd_create_args_create: strdup failed.");
      srrd_create_args_destroy(args);
      return NULL;
    }
  }
  assert(args->argc == argc);
  args->argv[args->argc] = NULL;

  return args;
} /* srrd_create_args_t *srrd_create_args_create */

/* * * * * * * * * *
 * WARNING:  Magic *
 * * * * * * * * * */
static int rra_get(char ***ret, const value_list_t *vl, /* {{{ */
                   const rrdcreate_config_t *cfg) {
  char **rra_def;
  int rra_num;

  int *rts;
  int rts_num;

  int rra_max;

  int cdp_num;
  int cdp_len;

  /* The stepsize we use here: If it is user-set, use it. If not, use the
   * interval of the value-list. */
  int ss;

  if (cfg->rrarows <= 0) {
    *ret = NULL;
    return -1;
  }

  if ((cfg->xff < 0) || (cfg->xff >= 1.0)) {
    *ret = NULL;
    return -1;
  }

  if (cfg->stepsize > 0)
    ss = cfg->stepsize;
  else
    ss = (int)CDTIME_T_TO_TIME_T(vl->interval);
  if (ss <= 0) {
    *ret = NULL;
    return -1;
  }

  /* Use the configured timespans or fall back to the built-in defaults */
  if (cfg->timespans_num != 0) {
    rts = cfg->timespans;
    rts_num = cfg->timespans_num;
  } else {
    rts = rra_timespans;
    rts_num = rra_timespans_num;
  }

  rra_max = rts_num * rra_types_num;
  assert(rra_max > 0);

  if ((rra_def = calloc(rra_max + 1, sizeof(*rra_def))) == NULL)
    return -1;
  rra_num = 0;

  cdp_len = 0;
  for (int i = 0; i < rts_num; i++) {
    int span = rts[i];

    if ((span / ss) < cfg->rrarows)
      span = ss * cfg->rrarows;

    if (cdp_len == 0)
      cdp_len = 1;
    else
      cdp_len = (int)floor(((double)span) / ((double)(cfg->rrarows * ss)));

    cdp_num = (int)ceil(((double)span) / ((double)(cdp_len * ss)));

    for (int j = 0; j < rra_types_num; j++) {
      char buffer[128];
      int status;

      if (rra_num >= rra_max)
        break;

      status = snprintf(buffer, sizeof(buffer), "RRA:%s:%.10f:%u:%u",
                        rra_types[j], cfg->xff, cdp_len, cdp_num);

      if ((status < 0) || ((size_t)status >= sizeof(buffer))) {
        ERROR("rra_get: Buffer would have been truncated.");
        continue;
      }

      rra_def[rra_num++] = sstrdup(buffer);
    }
  }

  if (rra_num <= 0) {
    sfree(rra_def);
    return 0;
  }

  *ret = rra_def;
  return rra_num;
} /* }}} int rra_get */

static void ds_free(int ds_num, char **ds_def) /* {{{ */
{
  for (int i = 0; i < ds_num; i++)
    if (ds_def[i] != NULL)
      free(ds_def[i]);
  free(ds_def);
} /* }}} void ds_free */

static int ds_get(char ***ret, /* {{{ */
                  const data_set_t *ds, const value_list_t *vl,
                  const rrdcreate_config_t *cfg) {
  char **ds_def;
  size_t ds_num;

  char min[32];
  char max[32];
  char buffer[128];

  assert(ds->ds_num > 0);

  ds_def = calloc(ds->ds_num, sizeof(*ds_def));
  if (ds_def == NULL) {
    ERROR("rrdtool plugin: calloc failed: %s", STRERRNO);
    return -1;
  }

  for (ds_num = 0; ds_num < ds->ds_num; ds_num++) {
    data_source_t *d = ds->ds + ds_num;
    const char *type;
    int status;

    ds_def[ds_num] = NULL;

    if (d->type == DS_TYPE_COUNTER)
      type = "COUNTER";
    else if (d->type == DS_TYPE_GAUGE)
      type = "GAUGE";
    else if (d->type == DS_TYPE_DERIVE)
      type = "DERIVE";
    else if (d->type == DS_TYPE_ABSOLUTE)
      type = "ABSOLUTE";
    else {
      ERROR("rrdtool plugin: Unknown DS type: %i", d->type);
      break;
    }

    if (isnan(d->min)) {
      sstrncpy(min, "U", sizeof(min));
    } else
      snprintf(min, sizeof(min), "%f", d->min);

    if (isnan(d->max)) {
      sstrncpy(max, "U", sizeof(max));
    } else
      snprintf(max, sizeof(max), "%f", d->max);

    status = snprintf(
        buffer, sizeof(buffer), "DS:%s:%s:%i:%s:%s", d->name, type,
        (cfg->heartbeat > 0) ? cfg->heartbeat
                             : (int)CDTIME_T_TO_TIME_T(2 * vl->interval),
        min, max);
    if ((status < 1) || ((size_t)status >= sizeof(buffer)))
      break;

    ds_def[ds_num] = sstrdup(buffer);
  } /* for ds_num = 0 .. ds->ds_num */

  if (ds_num != ds->ds_num) {
    ds_free(ds_num, ds_def);
    return -1;
  }

  if (ds_num == 0) {
    sfree(ds_def);
    return 0;
  }

  *ret = ds_def;
  return ds_num;
} /* }}} int ds_get */

#if HAVE_THREADSAFE_LIBRRD
static int srrd_create(const char *filename, /* {{{ */
                       unsigned long pdp_step, time_t last_up, int argc,
                       const char **argv) {
  int status;
  char *filename_copy;

  if ((filename == NULL) || (argv == NULL))
    return -EINVAL;

  /* Some versions of librrd don't have the `const' qualifier for the first
   * argument, so we have to copy the pointer here to avoid warnings. It sucks,
   * but what else can we do? :(  -octo */
  filename_copy = strdup(filename);
  if (filename_copy == NULL) {
    ERROR("srrd_create: strdup failed.");
    return -ENOMEM;
  }

  optind = 0; /* bug in librrd? */
  rrd_clear_error();

  status = rrd_create_r(filename_copy, pdp_step, last_up, argc, (void *)argv);

  if (status != 0) {
    WARNING("rrdtool plugin: rrd_create_r (%s) failed: %s", filename,
            rrd_get_error());
  }

  sfree(filename_copy);

  return status;
} /* }}} int srrd_create */
/* #endif HAVE_THREADSAFE_LIBRRD */

#else  /* !HAVE_THREADSAFE_LIBRRD */
static int srrd_create(const char *filename, /* {{{ */
                       unsigned long pdp_step, time_t last_up, int argc,
                       const char **argv) {
  int status;

  int new_argc;
  char **new_argv;

  char pdp_step_str[16];
  char last_up_str[16];

  new_argc = 6 + argc;
  new_argv = malloc((new_argc + 1) * sizeof(*new_argv));
  if (new_argv == NULL) {
    ERROR("rrdtool plugin: malloc failed.");
    return -1;
  }

  if (last_up == 0)
    last_up = time(NULL) - 10;

  snprintf(pdp_step_str, sizeof(pdp_step_str), "%lu", pdp_step);
  snprintf(last_up_str, sizeof(last_up_str), "%lu", (unsigned long)last_up);

  new_argv[0] = "create";
  new_argv[1] = (void *)filename;
  new_argv[2] = "-s";
  new_argv[3] = pdp_step_str;
  new_argv[4] = "-b";
  new_argv[5] = last_up_str;

  memcpy(new_argv + 6, argv, argc * sizeof(char *));
  new_argv[new_argc] = NULL;

  pthread_mutex_lock(&librrd_lock);
  optind = 0; /* bug in librrd? */
  rrd_clear_error();

  status = rrd_create(new_argc, new_argv);
  pthread_mutex_unlock(&librrd_lock);

  if (status != 0) {
    WARNING("rrdtool plugin: rrd_create (%s) failed: %s", filename,
            rrd_get_error());
  }

  sfree(new_argv);

  return status;
} /* }}} int srrd_create */
#endif /* !HAVE_THREADSAFE_LIBRRD */

static int lock_file(char const *filename) /* {{{ */
{
  async_create_file_t *ptr;
  struct stat sb;
  int status;

  pthread_mutex_lock(&async_creation_lock);

  for (ptr = async_creation_list; ptr != NULL; ptr = ptr->next)
    if (strcmp(filename, ptr->filename) == 0)
      break;

  if (ptr != NULL) {
    pthread_mutex_unlock(&async_creation_lock);
    return EEXIST;
  }

  status = stat(filename, &sb);
  if ((status == 0) || (errno != ENOENT)) {
    pthread_mutex_unlock(&async_creation_lock);
    return EEXIST;
  }

  ptr = malloc(sizeof(*ptr));
  if (ptr == NULL) {
    pthread_mutex_unlock(&async_creation_lock);
    return ENOMEM;
  }

  ptr->filename = strdup(filename);
  if (ptr->filename == NULL) {
    pthread_mutex_unlock(&async_creation_lock);
    sfree(ptr);
    return ENOMEM;
  }

  ptr->next = async_creation_list;
  async_creation_list = ptr;

  pthread_mutex_unlock(&async_creation_lock);

  return 0;
} /* }}} int lock_file */

static int unlock_file(char const *filename) /* {{{ */
{
  async_create_file_t *this;
  async_create_file_t *prev;

  pthread_mutex_lock(&async_creation_lock);

  prev = NULL;
  for (this = async_creation_list; this != NULL; this = this->next) {
    if (strcmp(filename, this->filename) == 0)
      break;
    prev = this;
  }

  if (this == NULL) {
    pthread_mutex_unlock(&async_creation_lock);
    return ENOENT;
  }

  if (prev == NULL) {
    assert(this == async_creation_list);
    async_creation_list = this->next;
  } else {
    assert(this == prev->next);
    prev->next = this->next;
  }
  this->next = NULL;

  pthread_mutex_unlock(&async_creation_lock);

  sfree(this->filename);
  sfree(this);

  return 0;
} /* }}} int unlock_file */

static void *srrd_create_thread(void *targs) /* {{{ */
{
  srrd_create_args_t *args = targs;
  char tmpfile[PATH_MAX];
  int status;

  status = lock_file(args->filename);
  if (status != 0) {
    if (status == EEXIST)
      NOTICE("srrd_create_thread: File \"%s\" is already being created.",
             args->filename);
    else
      ERROR("srrd_create_thread: Unable to lock file \"%s\".", args->filename);
    srrd_create_args_destroy(args);
    return 0;
  }

  snprintf(tmpfile, sizeof(tmpfile), "%s.async", args->filename);

  status = srrd_create(tmpfile, args->pdp_step, args->last_up, args->argc,
                       (void *)args->argv);
  if (status != 0) {
    WARNING("srrd_create_thread: srrd_create (%s) returned status %i.",
            args->filename, status);
    unlink(tmpfile);
    unlock_file(args->filename);
    srrd_create_args_destroy(args);
    return 0;
  }

  status = rename(tmpfile, args->filename);
  if (status != 0) {
    ERROR("srrd_create_thread: rename (\"%s\", \"%s\") failed: %s", tmpfile,
          args->filename, STRERRNO);
    unlink(tmpfile);
    unlock_file(args->filename);
    srrd_create_args_destroy(args);
    return 0;
  }

  DEBUG("srrd_create_thread: Successfully created RRD file \"%s\".",
        args->filename);

  unlock_file(args->filename);
  srrd_create_args_destroy(args);

  return 0;
} /* }}} void *srrd_create_thread */

static int srrd_create_async(const char *filename, /* {{{ */
                             unsigned long pdp_step, time_t last_up, int argc,
                             const char **argv) {
  srrd_create_args_t *args;
  pthread_t thread;
  pthread_attr_t attr;
  int status;

  DEBUG("srrd_create_async: Creating \"%s\" in the background.", filename);

  args = srrd_create_args_create(filename, pdp_step, last_up, argc, argv);
  if (args == NULL)
    return -1;

  status = pthread_attr_init(&attr);
  if (status != 0) {
    srrd_create_args_destroy(args);
    return -1;
  }

  status = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  if (status != 0) {
    pthread_attr_destroy(&attr);
    srrd_create_args_destroy(args);
    return -1;
  }

  status = pthread_create(&thread, &attr, srrd_create_thread, args);
  if (status != 0) {
    ERROR("srrd_create_async: pthread_create failed: %s", STRERROR(status));
    pthread_attr_destroy(&attr);
    srrd_create_args_destroy(args);
    return status;
  }

  pthread_attr_destroy(&attr);
  /* args is freed in srrd_create_thread(). */
  return 0;
} /* }}} int srrd_create_async */

/*
 * Public functions
 */
int cu_rrd_create_file(const char *filename, /* {{{ */
                       const data_set_t *ds, const value_list_t *vl,
                       const rrdcreate_config_t *cfg) {
  char **argv;
  int argc;
  char **rra_def = NULL;
  int rra_num;
  char **ds_def = NULL;
  int ds_num;
  int status = 0;
  time_t last_up;
  unsigned long stepsize;

  if (check_create_dir(filename))
    return -1;

  if ((rra_num = rra_get(&rra_def, vl, cfg)) < 1) {
    ERROR("cu_rrd_create_file failed: Could not calculate RRAs");
    return -1;
  }

  if ((ds_num = ds_get(&ds_def, ds, vl, cfg)) < 1) {
    ERROR("cu_rrd_create_file failed: Could not calculate DSes");
    rra_free(rra_num, rra_def);
    return -1;
  }

  argc = ds_num + rra_num;

  if ((argv = malloc(sizeof(*argv) * (argc + 1))) == NULL) {
    ERROR("cu_rrd_create_file failed: %s", STRERRNO);
    rra_free(rra_num, rra_def);
    ds_free(ds_num, ds_def);
    return -1;
  }

  memcpy(argv, ds_def, ds_num * sizeof(char *));
  memcpy(argv + ds_num, rra_def, rra_num * sizeof(char *));
  argv[ds_num + rra_num] = NULL;

  last_up = CDTIME_T_TO_TIME_T(vl->time);
  if (last_up <= 0)
    last_up = time(NULL);
  last_up -= 1;

  if (cfg->stepsize > 0)
    stepsize = cfg->stepsize;
  else
    stepsize = (unsigned long)CDTIME_T_TO_TIME_T(vl->interval);

  if (cfg->async) {
    status = srrd_create_async(filename, stepsize, last_up, argc,
                               (const char **)argv);
    if (status != 0)
      WARNING("cu_rrd_create_file: srrd_create_async (%s) "
              "returned status %i.",
              filename, status);
  } else /* synchronous */
  {
    status = lock_file(filename);
    if (status != 0) {
      if (status == EEXIST)
        NOTICE("cu_rrd_create_file: File \"%s\" is already being created.",
               filename);
      else
        ERROR("cu_rrd_create_file: Unable to lock file \"%s\".", filename);
    } else {
      status =
          srrd_create(filename, stepsize, last_up, argc, (const char **)argv);

      if (status != 0) {
        WARNING("cu_rrd_create_file: srrd_create (%s) returned status %i.",
                filename, status);
      } else {
        DEBUG("cu_rrd_create_file: Successfully created RRD file \"%s\".",
              filename);
      }
      unlock_file(filename);
    }
  }

  free(argv);
  ds_free(ds_num, ds_def);
  rra_free(rra_num, rra_def);

  return status;
} /* }}} int cu_rrd_create_file */
