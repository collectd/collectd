/**
 * collectd - src/utils_rrdcreate.c
 * Copyright (C) 2006-2008  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "utils_rrdcreate.h"

#include <pthread.h>
#include <rrd.h>

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
  if (cfg->timespans_num != 0)
  {
    rts = cfg->timespans;
    rts_num = cfg->timespans_num;
  }
  else
  {
    rts = rra_timespans;
    rts_num = rra_timespans_num;
  }

  rra_max = rts_num * rra_types_num;

  if ((rra_def = (char **) malloc ((rra_max + 1) * sizeof (char *))) == NULL)
    return (-1);
  memset (rra_def, '\0', (rra_max + 1) * sizeof (char *));
  rra_num = 0;

  cdp_len = 0;
  for (i = 0; i < rts_num; i++)
  {
    span = rts[i];

    if ((span / ss) < cfg->rrarows)
      span = ss * cfg->rrarows;

    if (cdp_len == 0)
      cdp_len = 1;
    else
      cdp_len = (int) floor (((double) span)
          / ((double) (cfg->rrarows * ss)));

    cdp_num = (int) ceil (((double) span)
        / ((double) (cdp_len * ss)));

    for (j = 0; j < rra_types_num; j++)
    {
      int status;

      if (rra_num >= rra_max)
        break;

      status = ssnprintf (buffer, sizeof (buffer), "RRA:%s:%.10f:%u:%u",
          rra_types[j], cfg->xff, cdp_len, cdp_num);

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

/*
 * Public functions
 */
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
  if (last_up <= 10)
    last_up = time (NULL);
  last_up -= 10;

  if (cfg->stepsize > 0)
    stepsize = cfg->stepsize;
  else
    stepsize = (unsigned long) CDTIME_T_TO_TIME_T (vl->interval);

  status = srrd_create (filename, stepsize, last_up,
      argc, (const char **) argv);

  free (argv);
  ds_free (ds_num, ds_def);
  rra_free (rra_num, rra_def);

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

  return (status);
} /* }}} int cu_rrd_create_file */

/* vim: set sw=2 sts=2 et fdm=marker : */
