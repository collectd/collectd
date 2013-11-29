/**
 * collectd - src/utils_rrdcreate.c
 * Copyright (C) 2006-2013  Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "liboconfig/oconfig.h"
#include "common.h"
#include "utils_rrdcreate.h"

#include <pthread.h>
#include <rrd.h>

struct srrd_create_args_s
{
  char *filename;
  unsigned long pdp_step;
  time_t last_up;
  int argc;
  char **argv;
};
typedef struct srrd_create_args_s srrd_create_args_t;

struct async_create_file_s;
typedef struct async_create_file_s async_create_file_t;
struct async_create_file_s
{
  char *filename;
  async_create_file_t *next;
};

/*
 * Private variables
 */
static int rra_timespans[] =
{
  3600,
  86400,
  604800,
  2678400,
  31622400
};
static int rra_timespans_num = STATIC_ARRAY_SIZE (rra_timespans);

static char *rra_types[] =
{
  "AVERAGE",
  "MIN",
  "MAX"
};
static int rra_types_num = STATIC_ARRAY_SIZE (rra_types);

#if !defined(HAVE_THREADSAFE_LIBRRD) || !HAVE_THREADSAFE_LIBRRD
static pthread_mutex_t librrd_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static async_create_file_t *async_creation_list = NULL;
static pthread_mutex_t async_creation_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Private functions
 */
static void rra_free (int rra_num, char **rra_def) /* {{{ */
{
  int i;

  for (i = 0; i < rra_num; i++)
  {
    sfree (rra_def[i]);
  }
  sfree (rra_def);
} /* }}} void rra_free */

static void srrd_create_args_destroy (srrd_create_args_t *args)
{
  if (args == NULL)
    return;

  sfree (args->filename);
  if (args->argv != NULL)
  {
    int i;
    for (i = 0; i < args->argc; i++)
      sfree (args->argv[i]);
    sfree (args->argv);
  }
} /* void srrd_create_args_destroy */

static srrd_create_args_t *srrd_create_args_create (const char *filename,
    unsigned long pdp_step, time_t last_up,
    int argc, const char **argv)
{
  srrd_create_args_t *args;

  args = malloc (sizeof (*args));
  if (args == NULL)
  {
    ERROR ("srrd_create_args_create: malloc failed.");
    return (NULL);
  }
  memset (args, 0, sizeof (*args));
  args->filename = NULL;
  args->pdp_step = pdp_step;
  args->last_up = last_up;
  args->argv = NULL;

  args->filename = strdup (filename);
  if (args->filename == NULL)
  {
    ERROR ("srrd_create_args_create: strdup failed.");
    srrd_create_args_destroy (args);
    return (NULL);
  }

  args->argv = calloc ((size_t) (argc + 1), sizeof (*args->argv));
  if (args->argv == NULL)
  {
    ERROR ("srrd_create_args_create: calloc failed.");
    srrd_create_args_destroy (args);
    return (NULL);
  }

  for (args->argc = 0; args->argc < argc; args->argc++)
  {
    args->argv[args->argc] = strdup (argv[args->argc]);
    if (args->argv[args->argc] == NULL)
    {
      ERROR ("srrd_create_args_create: strdup failed.");
      srrd_create_args_destroy (args);
      return (NULL);
    }
  }
  assert (args->argc == argc);
  args->argv[args->argc] = NULL;

  return (args);
} /* srrd_create_args_t *srrd_create_args_create */

/* * * * * * * * * *
 * WARNING:  Magic *
 * * * * * * * * * */
static int rra_get (char ***ret, const value_list_t *vl, /* {{{ */
    const rrdcreate_config_t *cfg)
{
  char **rra_def;
  int rra_num;

  int *rts;
  int  rts_num;

  int rra_max;

  int span;

  int cdp_num;
  int cdp_len;
  int i, j;

  char buffer[128];

  /* The stepsize we use here: If it is user-set, use it. If not, use the
   * interval of the value-list. */
  int ss;

  if (cfg->rrarows <= 0)
  {
    *ret = NULL;
    return (-1);
  }

  if ((cfg->xff < 0) || (cfg->xff >= 1.0))
  {
    *ret = NULL;
    return (-1);
  }

  if (cfg->stepsize > 0)
    ss = cfg->stepsize;
  else
    ss = (int) CDTIME_T_TO_TIME_T (vl->interval);
  if (ss <= 0)
  {
    *ret = NULL;
    return (-1);
  }

  /* Use the configured timespans or fall back to the built-in defaults */
  if(cfg->rra_param_num) {
    rts = NULL;
    rts_num = cfg->rra_param_num;
  } else if (cfg->timespans_num != 0) {
    rts = cfg->timespans;
    rts_num = cfg->timespans_num;
  } else {
    rts = rra_timespans;
    rts_num = rra_timespans_num;
  }

  if(cfg->rra_param_num) {
    /* We are using RRADef definitions */
    rra_max = 0;

    for(i=0; i<cfg->rra_param_num; i++) {
      if(0 == cfg->rra_param[i].type[0]) {
        /* Using default RRA */
        int j;
        for(j=0; j<rra_types_num; j++) assert(0 == cfg->rra_param[i].type[j]);
        if(cfg->rra_types) {
          /* Using default RRA specified with RRA keyword */
          for(j=0; j<rra_types_num; j++) {
            if(1 == cfg->rra_types[j]) rra_max += 1;
          }
        } else {
          /* Using default build-in RRA */
          rra_max += rra_types_num;
        }
      } else {
        for(j=0; j<RRA_TYPE_NUM; j++) {
          if(1 == cfg->rra_param[i].type[j]) rra_max += 1;
        }
      }
    }
  } else if(cfg->rra_types) {
    int n = 0;
    for(i=0; i<rra_types_num; i++) {
      if(1 == cfg->rra_types[i]) n++;
    }
    rra_max = rts_num * n;
  } else {
    /* We are using RRATimespan or default definitions */
    rra_max = rts_num * rra_types_num;
  }

  if ((rra_def = (char **) malloc ((rra_max + 1) * sizeof (char *))) == NULL)
    return (-1);
  memset (rra_def, '\0', (rra_max + 1) * sizeof (char *));
  rra_num = 0;

  cdp_len = 0;
  for (i = 0; i < rts_num; i++)
  {
    if(cfg->rra_param_num) {
      span = cfg->rra_param[i].span;
    } else {
      span = rts[i];
    }

    if ((span / ss) < cfg->rrarows)
      span = ss * cfg->rrarows;

    if(cfg->rra_param && (0 != cfg->rra_param[i].pdp_per_row)) {
      cdp_len = cfg->rra_param[i].pdp_per_row;
    } else if (cfg->rra_param && (0 != cfg->rra_param[i].precision)) {
      cdp_len = (int) floor( ((double)cfg->rra_param[i].precision) / ((double)ss) );
      if(0 == cdp_len) cdp_len = 1;
    } else if (cdp_len == 0) {
      /* This happens when we use RRATimespan and when this is the 1st one */
      cdp_len = 1;
    } else {
      /* This happens when we use RRATimespan and when this is not the 1st one */
      cdp_len = (int) floor (((double) span)
          / ((double) (cfg->rrarows * ss)));
    }

    cdp_num = (int) ceil (((double) span)
        / ((double) (cdp_len * ss)));

    for (j = 0; j < rra_types_num; j++)
    {
      int status;
      double xff;

      if (rra_num >= rra_max)
        break;

      if(cfg->rra_param) {
        if(-1 == cfg->rra_param[i].type[j]) continue; /* disabled in RRADef */
        if(0 == cfg->rra_param[i].type[j]) { /* Using default */
          if(cfg->rra_types && (1 != cfg->rra_types[j])) continue; /* Default from RRA line */
          /* If we are here, it was not disabled */
        }
      } else {
        /* This is default or RRATimespan line */
        if(cfg->rra_types && (1 != cfg->rra_types[j])) continue;
      }

      if(cfg->rra_param && (cfg->rra_param[i].xff >= 0.)) {
        xff = cfg->rra_param[i].xff;
      } else {
        xff = cfg->xff;
      }

      status = ssnprintf (buffer, sizeof (buffer), "RRA:%s:%.10f:%u:%u",
          rra_types[j], xff, cdp_len, cdp_num);

      if ((status < 0) || ((size_t) status >= sizeof (buffer)))
      {
        ERROR ("rra_get: Buffer would have been truncated.");
        continue;
      }

      rra_def[rra_num++] = sstrdup (buffer);
    }
  }

  *ret = rra_def;
  return (rra_num);
} /* }}} int rra_get */

static void ds_free (int ds_num, char **ds_def) /* {{{ */
{
  int i;

  for (i = 0; i < ds_num; i++)
    if (ds_def[i] != NULL)
      free (ds_def[i]);
  free (ds_def);
} /* }}} void ds_free */

static int ds_get (char ***ret, /* {{{ */
    const data_set_t *ds, const value_list_t *vl,
    const rrdcreate_config_t *cfg)
{
  char **ds_def;
  int ds_num;

  char min[32];
  char max[32];
  char buffer[128];

  ds_def = (char **) malloc (ds->ds_num * sizeof (char *));
  if (ds_def == NULL)
  {
    char errbuf[1024];
    ERROR ("rrdtool plugin: malloc failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }
  memset (ds_def, '\0', ds->ds_num * sizeof (char *));

  for (ds_num = 0; ds_num < ds->ds_num; ds_num++)
  {
    data_source_t *d = ds->ds + ds_num;
    char *type;
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
    else
    {
      ERROR ("rrdtool plugin: Unknown DS type: %i",
          d->type);
      break;
    }

    if (isnan (d->min))
    {
      sstrncpy (min, "U", sizeof (min));
    }
    else
      ssnprintf (min, sizeof (min), "%f", d->min);

    if (isnan (d->max))
    {
      sstrncpy (max, "U", sizeof (max));
    }
    else
      ssnprintf (max, sizeof (max), "%f", d->max);

    status = ssnprintf (buffer, sizeof (buffer),
        "DS:%s:%s:%i:%s:%s",
        d->name, type,
        (cfg->heartbeat > 0)
        ? cfg->heartbeat
        : (int) CDTIME_T_TO_TIME_T (2 * vl->interval),
        min, max);
    if ((status < 1) || ((size_t) status >= sizeof (buffer)))
      break;

    ds_def[ds_num] = sstrdup (buffer);
  } /* for ds_num = 0 .. ds->ds_num */

  if (ds_num != ds->ds_num)
  {
    ds_free (ds_num, ds_def);
    return (-1);
  }

  *ret = ds_def;
  return (ds_num);
} /* }}} int ds_get */

#if HAVE_THREADSAFE_LIBRRD
static int srrd_create (const char *filename, /* {{{ */
    unsigned long pdp_step, time_t last_up,
    int argc, const char **argv)
{
  int status;
  char *filename_copy;

  if ((filename == NULL) || (argv == NULL))
    return (-EINVAL);

  /* Some versions of librrd don't have the `const' qualifier for the first
   * argument, so we have to copy the pointer here to avoid warnings. It sucks,
   * but what else can we do? :(  -octo */
  filename_copy = strdup (filename);
  if (filename_copy == NULL)
  {
    ERROR ("srrd_create: strdup failed.");
    return (-ENOMEM);
  }

  optind = 0; /* bug in librrd? */
  rrd_clear_error ();

  status = rrd_create_r (filename_copy, pdp_step, last_up,
      argc, (void *) argv);

  if (status != 0)
  {
    WARNING ("rrdtool plugin: rrd_create_r (%s) failed: %s",
        filename, rrd_get_error ());
  }

  sfree (filename_copy);

  return (status);
} /* }}} int srrd_create */
/* #endif HAVE_THREADSAFE_LIBRRD */

#else /* !HAVE_THREADSAFE_LIBRRD */
static int srrd_create (const char *filename, /* {{{ */
    unsigned long pdp_step, time_t last_up,
    int argc, const char **argv)
{
  int status;

  int new_argc;
  char **new_argv;

  char pdp_step_str[16];
  char last_up_str[16];

  new_argc = 6 + argc;
  new_argv = (char **) malloc ((new_argc + 1) * sizeof (char *));
  if (new_argv == NULL)
  {
    ERROR ("rrdtool plugin: malloc failed.");
    return (-1);
  }

  if (last_up == 0)
    last_up = time (NULL) - 10;

  ssnprintf (pdp_step_str, sizeof (pdp_step_str), "%lu", pdp_step);
  ssnprintf (last_up_str, sizeof (last_up_str), "%lu", (unsigned long) last_up);

  new_argv[0] = "create";
  new_argv[1] = (void *) filename;
  new_argv[2] = "-s";
  new_argv[3] = pdp_step_str;
  new_argv[4] = "-b";
  new_argv[5] = last_up_str;

  memcpy (new_argv + 6, argv, argc * sizeof (char *));
  new_argv[new_argc] = NULL;

  pthread_mutex_lock (&librrd_lock);
  optind = 0; /* bug in librrd? */
  rrd_clear_error ();

  status = rrd_create (new_argc, new_argv);
  pthread_mutex_unlock (&librrd_lock);

  if (status != 0)
  {
    WARNING ("rrdtool plugin: rrd_create (%s) failed: %s",
        filename, rrd_get_error ());
  }

  sfree (new_argv);

  return (status);
} /* }}} int srrd_create */
#endif /* !HAVE_THREADSAFE_LIBRRD */

static int lock_file (char const *filename) /* {{{ */
{
  async_create_file_t *ptr;
  struct stat sb;
  int status;

  pthread_mutex_lock (&async_creation_lock);

  for (ptr = async_creation_list; ptr != NULL; ptr = ptr->next)
    if (strcmp (filename, ptr->filename) == 0)
      break;

  if (ptr != NULL)
  {
    pthread_mutex_unlock (&async_creation_lock);
    return (EEXIST);
  }

  status = stat (filename, &sb);
  if ((status == 0) || (errno != ENOENT))
  {
    pthread_mutex_unlock (&async_creation_lock);
    return (EEXIST);
  }

  ptr = malloc (sizeof (*ptr));
  if (ptr == NULL)
  {
    pthread_mutex_unlock (&async_creation_lock);
    return (ENOMEM);
  }

  ptr->filename = strdup (filename);
  if (ptr->filename == NULL)
  {
    pthread_mutex_unlock (&async_creation_lock);
    sfree (ptr);
    return (ENOMEM);
  }

  ptr->next = async_creation_list;
  async_creation_list = ptr;

  pthread_mutex_unlock (&async_creation_lock);

  return (0);
} /* }}} int lock_file */

static int unlock_file (char const *filename) /* {{{ */
{
  async_create_file_t *this;
  async_create_file_t *prev;


  pthread_mutex_lock (&async_creation_lock);

  prev = NULL;
  for (this = async_creation_list; this != NULL; this = this->next)
  {
    if (strcmp (filename, this->filename) == 0)
      break;
    prev = this;
  }

  if (this == NULL)
  {
    pthread_mutex_unlock (&async_creation_lock);
    return (ENOENT);
  }

  if (prev == NULL)
  {
    assert (this == async_creation_list);
    async_creation_list = this->next;
  }
  else
  {
    assert (this == prev->next);
    prev->next = this->next;
  }
  this->next = NULL;

  pthread_mutex_unlock (&async_creation_lock);

  sfree (this->filename);
  sfree (this);

  return (0);
} /* }}} int unlock_file */

static void *srrd_create_thread (void *targs) /* {{{ */
{
  srrd_create_args_t *args = targs;
  char tmpfile[PATH_MAX];
  int status;

  status = lock_file (args->filename);
  if (status != 0)
  {
    if (status == EEXIST)
      NOTICE ("srrd_create_thread: File \"%s\" is already being created.",
          args->filename);
    else
      ERROR ("srrd_create_thread: Unable to lock file \"%s\".",
          args->filename);
    srrd_create_args_destroy (args);
    return (0);
  }

  ssnprintf (tmpfile, sizeof (tmpfile), "%s.async", args->filename);

  status = srrd_create (tmpfile, args->pdp_step, args->last_up,
      args->argc, (void *) args->argv);
  if (status != 0)
  {
    WARNING ("srrd_create_thread: srrd_create (%s) returned status %i.",
        args->filename, status);
    unlink (tmpfile);
    unlock_file (args->filename);
    srrd_create_args_destroy (args);
    return (0);
  }

  status = rename (tmpfile, args->filename);
  if (status != 0)
  {
    char errbuf[1024];
    ERROR ("srrd_create_thread: rename (\"%s\", \"%s\") failed: %s",
        tmpfile, args->filename,
        sstrerror (errno, errbuf, sizeof (errbuf)));
    unlink (tmpfile);
    unlock_file (args->filename);
    srrd_create_args_destroy (args);
    return (0);
  }

  DEBUG ("srrd_create_thread: Successfully created RRD file \"%s\".",
      args->filename);

  unlock_file (args->filename);
  srrd_create_args_destroy (args);

  return (0);
} /* }}} void *srrd_create_thread */

static int srrd_create_async (const char *filename, /* {{{ */
    unsigned long pdp_step, time_t last_up,
    int argc, const char **argv)
{
  srrd_create_args_t *args;
  pthread_t thread;
  pthread_attr_t attr;
  int status;

  DEBUG ("srrd_create_async: Creating \"%s\" in the background.", filename);

  args = srrd_create_args_create (filename, pdp_step, last_up, argc, argv);
  if (args == NULL)
    return (-1);

  status = pthread_attr_init (&attr);
  if (status != 0)
  {
    srrd_create_args_destroy (args);
    return (-1);
  }

  status = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  if (status != 0)
  {
    pthread_attr_destroy (&attr);
    srrd_create_args_destroy (args);
    return (-1);
  }

  status = pthread_create (&thread, &attr, srrd_create_thread, args);
  if (status != 0)
  {
    char errbuf[1024];
    ERROR ("srrd_create_async: pthread_create failed: %s",
        sstrerror (status, errbuf, sizeof (errbuf)));
    pthread_attr_destroy (&attr);
    srrd_create_args_destroy (args);
    return (status);
  }

  pthread_attr_destroy (&attr);
  /* args is freed in srrd_create_thread(). */
  return (0);
} /* }}} int srrd_create_async */

static int rrd_compare_numeric (const void *a_ptr, const void *b_ptr)
{
	int a = *((int *) a_ptr);
	int b = *((int *) b_ptr);

	if (a < b)
		return (-1);
	else if (a > b)
		return (1);
	else
		return (0);
} /* int rrd_compare_numeric */

static int rrd_compare_rra_param (const void *a_ptr, const void *b_ptr)
{
	typeof(((rra_param_t *)a_ptr)->span) a = ((rra_param_t *)a_ptr)->span;
	typeof(((rra_param_t *)b_ptr)->span) b = ((rra_param_t *)b_ptr)->span;

	if (a < b)
		return (-1);
	else if (a > b)
		return (1);
	else
		return (0);
} /* int rrd_compare_rra_param */

/*
 * Public functions
 */
int rc_config_get_int_positive (oconfig_item_t const *ci, int *ret) /* {{{ */
{
  int status;
  int tmp = 0;

  status = cf_util_get_int (ci, &tmp);
  if (status != 0)
    return (status);
  if (tmp < 0)
    return (EINVAL);

  *ret = tmp;
  return (0);
} /* }}} int rc_config_get_int_positive */

int rc_config_get_xff (oconfig_item_t const *ci, double *ret) /* {{{ */
{
  double value;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
  {
    ERROR ("rrdcached plugin: The \"%s\" needs exactly one numeric argument "
        "in the range [0.0, 1.0)", ci->key);
    return (EINVAL);
  }

  value = ci->values[0].value.number;
  if ((value >= 0.0) && (value < 1.0))
  {
    *ret = value;
    return (0);
  }

  ERROR ("rrdcached plugin: The \"%s\" needs exactly one numeric argument "
      "in the range [0.0, 1.0)", ci->key);
  return (EINVAL);
} /* }}} int rc_config_get_xff */

int rc_config_add_timespan (int timespan, rrdcreate_config_t *cfg) /* {{{ */
{
  int *tmp;

  if (timespan <= 0)
    return (EINVAL);

  tmp = realloc (cfg->timespans,
      sizeof (*cfg->timespans)
      * (cfg->timespans_num + 1));
  if (tmp == NULL)
    return (ENOMEM);
  cfg->timespans = tmp;

  cfg->timespans[cfg->timespans_num] = timespan;
  cfg->timespans_num++;

  return (0);
} /* }}} int rc_config_add_timespan */

int cu_rrd_rra_types_set(const oconfig_item_t *ci, rrdcreate_config_t *cfg) { /* {{{ */
  int i;

  if(ci->values_num < 1) {
    ERROR ("rrdtool plugin: The %s option requires "
        "1 to 3 string arguments", ci->key);
    return(-1);
  }

  /* Alloc as much space as possible if not done yet (should be not so many...) */
  if(NULL == cfg->rra_types) {
    if(NULL == (cfg->rra_types = calloc(sizeof(*cfg->rra_types), rra_types_num))) {
      ERROR ("rrdtool plugin: malloc failed.");
      return (-1);
    }
  }

  /* For each value in the rra_types[] array, find out if the value was defined
   * in the config string (value). If yes, cfg->rra_types[] will point to it
   */
  for(i=0; i<ci->values_num; i++) {
    int j;
    if(ci->values[i].type != OCONFIG_TYPE_STRING) {
      ERROR ("rrdtool plugin: The %s option requires "
          "1 to 3 string arguments. Argument %d is not a string", ci->key, i);
      return(-1);
    }
    for(j=0; j<rra_types_num; j++) {
      if(!strcasecmp(rra_types[j], ci->values[i].value.string)) {
        cfg->rra_types[j] = 1;
        break;
      }
    }
  }

  return(0);
} /* }}} cu_rrd_rra_types_set */

int cu_rrd_rra_param_append(const oconfig_item_t *ci, rrdcreate_config_t *cfg) { /* {{{ */
  int pos;

  rra_param_t rra_param = {
    /* types       = */ {0, 0, 0},
    /* span        = */ 0,
    /* pdp_per_row = */ 0,
    /* precision   = */ 0,
    /* xff         = */ -1.
  };

  if(ci->values_num < 1) {
    ERROR ("rrdtool plugin: The %s option requires "
        "at least 1 int argument", ci->key);
    return(-1);
  }

  pos = 0;

  /* Timespan */
  if(ci->values[pos].type != OCONFIG_TYPE_NUMBER) {
    ERROR ("rrdtool plugin: Argument %d for %s should be an INT", pos+1, ci->key);
    return(-1);
  }
  rra_param.span = (int) ci->values[pos].value.number;
  pos += 1;

  /* pdp_per_row */
  if(pos < ci->values_num) {
    if(ci->values[pos].type != OCONFIG_TYPE_NUMBER) {
      ERROR ("rrdtool plugin: Argument %d for %s should be an INT", pos+1, ci->key);
      return(-1);
    }
    rra_param.pdp_per_row = (int) ci->values[pos].value.number;
    pos += 1;
  }

  /* precision */
  if(pos < ci->values_num) {
    if(ci->values[pos].type != OCONFIG_TYPE_NUMBER) {
      ERROR ("rrdtool plugin: Argument %d for %s should be an INT", pos+1, ci->key);
      return(-1);
    }
    rra_param.precision = (int) ci->values[pos].value.number;
    pos += 1;
  }

  /* MIN,MAX,AVERAGE (or default) */
  if(pos < ci->values_num) {
    if(ci->values[pos].type != OCONFIG_TYPE_STRING) {
      ERROR ("rrdtool plugin: Argument %d for %s should be a STRING", pos+1, ci->key);
      return(-1);
    }
    if(0 == strcasecmp("default", ci->values[pos].value.string)) {
      pos += 1;
    } else {
      int i;
      for(i=0; i<rra_types_num; i++) {
        rra_param.type[i] = -1;
      }
      while((pos < ci->values_num) && (ci->values[pos].type == OCONFIG_TYPE_STRING)) {
        for(i=0; i<rra_types_num; i++) {
          if(!strcasecmp(rra_types[i], ci->values[pos].value.string)) {
            rra_param.type[i] = 1;
            break;
          }
        }
        pos += 1;
      }
    }
  }

  /* XFF */
  if(pos < ci->values_num) {
    if(ci->values[pos].type != OCONFIG_TYPE_NUMBER) {
      ERROR ("rrdtool plugin: Argument %d for %s should be a NUMBER", pos+1, ci->key);
      return(-1);
    }
    rra_param.xff = ci->values[pos].value.number;
    pos += 1;
  }

  /* Last argument check */
  if(pos < ci->values_num) {
    ERROR ("rrdtool plugin: Too many arguments for %s", ci->key);
    return(-1);
  }

  /* Append the line to the config */
  if(0 != rra_param.span) {
    pos = cfg->rra_param_num;
    cfg->rra_param_num += 1;
    if(NULL == (cfg->rra_param = realloc(cfg->rra_param, sizeof(*cfg->rra_param)*cfg->rra_param_num))) {
      ERROR ("rrdtool plugin: malloc failed.");
      return (-1);
    }
    memcpy(&(cfg->rra_param[pos]), &rra_param, sizeof(rra_param));
  }

  return(0);
} /* }}} cu_rrd_rra_param_append */

int cu_rrd_sort_config_items(rrdcreate_config_t *cfg) { /* {{{ */
  if(cfg->timespans) {
    qsort (/* base = */ cfg->timespans,
        /* nmemb  = */ cfg->timespans_num,
        /* size   = */ sizeof (cfg->timespans[0]),
        /* compar = */ rrd_compare_numeric);
  }
  if(cfg->rra_param) {
    qsort (/* base = */ cfg->rra_param,
        /* nmemb  = */ cfg->rra_param_num,
        /* size   = */ sizeof (cfg->rra_param[0]),
        /* compar = */ rrd_compare_rra_param);
  }
  return(0);
} /* }}} int cu_rrd_sort_config_items */

int cu_rrd_create_file (const char *filename, /* {{{ */
    const data_set_t *ds, const value_list_t *vl,
    const rrdcreate_config_t *cfg)
{
  char **argv;
  int argc;
  char **rra_def;
  int rra_num;
  char **ds_def;
  int ds_num;
  int status = 0;
  time_t last_up;
  unsigned long stepsize;

  if (check_create_dir (filename))
    return (-1);

  if ((rra_num = rra_get (&rra_def, vl, cfg)) < 1)
  {
    ERROR ("cu_rrd_create_file failed: Could not calculate RRAs");
    return (-1);
  }

  if ((ds_num = ds_get (&ds_def, ds, vl, cfg)) < 1)
  {
    ERROR ("cu_rrd_create_file failed: Could not calculate DSes");
    return (-1);
  }

  argc = ds_num + rra_num;

  if ((argv = (char **) malloc (sizeof (char *) * (argc + 1))) == NULL)
  {
    char errbuf[1024];
    ERROR ("cu_rrd_create_file failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  memcpy (argv, ds_def, ds_num * sizeof (char *));
  memcpy (argv + ds_num, rra_def, rra_num * sizeof (char *));
  argv[ds_num + rra_num] = NULL;

  last_up = CDTIME_T_TO_TIME_T (vl->time);
  if (last_up <= 0)
    last_up = time (NULL);
  last_up -= 1;

  if (cfg->stepsize > 0)
    stepsize = cfg->stepsize;
  else
    stepsize = (unsigned long) CDTIME_T_TO_TIME_T (vl->interval);

  if (cfg->async)
  {
    status = srrd_create_async (filename, stepsize, last_up,
        argc, (const char **) argv);
    if (status != 0)
      WARNING ("cu_rrd_create_file: srrd_create_async (%s) "
          "returned status %i.",
          filename, status);
  }
  else /* synchronous */
  {
    status = srrd_create (filename, stepsize, last_up,
        argc, (const char **) argv);

    if (status != 0)
    {
      WARNING ("cu_rrd_create_file: srrd_create (%s) returned status %i.",
          filename, status);
    }
    else
    {
      DEBUG ("cu_rrd_create_file: Successfully created RRD file \"%s\".",
          filename);
    }
  }

  free (argv);
  ds_free (ds_num, ds_def);
  rra_free (rra_num, rra_def);

  return (status);
} /* }}} int cu_rrd_create_file */

/* vim: set sw=2 sts=2 et fdm=marker : */
