/**
 * collectd - src/tail_apache.c
 * Copyright (C) 2008       Florian octo Forster
 * Copyright (C) 2015       Andrei Darashenka
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
 *   Andrei Darashenka
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_tail_match.h"

/*
   <Plugin tail_apache>
     <Files "/var/log/apache/website1_access.log*">
 	Instance "website1"
        Interval 60
# %r - request
# %s - size
# %b - bytes
# %D - (micro)seconds
# %? - any word (w/o spaces)
# "%?" - any line in quotes
        Format "%? %? %? %? \"%? %r %?\" %s %b \"%?\" \"%?\" %D %? %?"
 
	<Match>
	  Suffix .jpg .png .css .js
          <Report>
	      Type "count"
#	      Instance "count"
          </Report>
          <Report>
	      Type "count_code"
#              Thresholds 200 300 400 404 405 500 501 505
#	       Instance "count_code"
          </Report>
          <Report>
	      Type "count_time"
              Thresholds 1000 10000 100000 1000000
#	      Instance "count_time"
          </Report>
          <Report>
	      Type "count_size"
              Thresholds 1000 10000 100000 1000000
#	      Instance "count_size"
          </Report>
          <Report>
	      Type "sum_size"
#	      Instance "sum_size"
          </Report>
          <Report>
	      Type "avg_size"
#	      Instance "avg_size"
          </Report>
          <Report>
	      Type "avg_time"
#	      Instance "avg_time"
          </Report>
	</Match>
	<Match>
	  Exact /server-status /status
#         ..
	</Match>
	<Match>
	  Regex ^/user/
#         ..
	</Match>
	<Match>
	  Prefix /
#         ..
	</Match>

    </Files>
  </Plugin>



 */

struct tail_apachelog_config_filemask_s
{
  char *filemask;
  cu_tail_t **tail;
  size_t tail_len;
  tail_apachelog_config_match_t** match;
  size_t match_len;
  char *instance;
  cdtime_t interval;
  char *format;
};
typedef struct tail_apachelog_config_filemask_s tail_apachelog_config_filemask_t;

struct tail_apachelog_config_match_s
{
  char *mask;
  int flags;
  tail_apachelog_config_report_t* report;
  char *instance;
};
typedef struct tail_apachelog_config_match_s tail_apachelog_config_match_t;



struct tail_apachelog_config_report_s
{
  char *instance;
  int type;
  size_t *threshold;
  size_t threshold_len;
};
typedef struct tail_apachelog_config_report_s tail_apachelog_config_report_t;

#define TA_R_COUNT         1
#define TA_R_COUNT_CODE    2
#define TA_R_COUNT_TIME    3
#define TA_R_COUNT_SIZE    4
#define TA_R_SUM_SIZE      5
#define TA_R_AVG_SIZE      6
#define TA_R_AVG_TIME      7

tail_apachelog_config_filemask_t **tail_apachelog_list = NULL;
size_t tail_apachelog_list_num = 0;

static void tail_apachelog_destroy_filemask(tail_apachelog_config_filemask_t*fm)
{
    int i;

    if(fm->filemask)sfree(fm->filemask);
    fm->filemask = 0;

    if(fm->format)sfree(fm->format);
    fm->format = 0;

    if(fm->instance)sfree(fm->instance);
    fm->instance = 0;

    for(i=0;i<fm->tail_len;i++)
        desroy(fm->tail[i]);
    if(fm->tail)
        sfree(fm->tail);
    fm->tail = 0;
    fm->tail_len=0;

    for(i=0;i<fm->match_len;i++)
        tail_apachelog_destroy_match(fm->match[i]);
    if(fm->match)sfree(fm->match);
    fm->match=0;
    fm->match_len=0;

    sfree(fm);
}

static void tail_apachelog_destroy_match(tail_apachelog_config_match_t*cm)
{
    int i;

    if(cm->mask)sfree(cm->mask);
    cm->mask = 0;

    if(cm->instance)sfree(cm->instance);
    cm->instance = 0;

    for(i=0;i<cm->report_len;i++)
        tail_apachelog_destroy_report(cm->report[i]);
    if(cm->report)
        sfree(fm->report);
    cm->report = 0;
    cm->report_len=0;

    sfree(cm);
}

static void tail_apachelog_destroy_report(tail_apachelog_config_report_t*rm)
{
    int i;

    if(rm->instance)sfree(rm->instance);
    rm->instance = 0;

    if(rm->threshold)
        sfree(rm->threshold);
    rm->threshold = 0;
    rm->threshold_len=0;

    sfree(rm);
}



static int tail_apachelog_config_add_match_dstype (tail_apachelog_config_match_t *cm,
    oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("tail_apachelog plugin: `DSType' needs exactly one string argument.");
    return (-1);
  }

  if (strncasecmp ("Gauge", ci->values[0].value.string, strlen ("Gauge")) == 0)
  {
    cm->flags = UTILS_MATCH_DS_TYPE_GAUGE;
    if (strcasecmp ("GaugeAverage", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_GAUGE_AVERAGE;
    else if (strcasecmp ("GaugeMin", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_GAUGE_MIN;
    else if (strcasecmp ("GaugeMax", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_GAUGE_MAX;
    else if (strcasecmp ("GaugeLast", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_GAUGE_LAST;
    else if (strcasecmp ("GaugeInc", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_GAUGE_INC;
    else if (strcasecmp ("GaugeAdd", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_GAUGE_ADD;
    else
      cm->flags = 0;
  }
  else if (strncasecmp ("Counter", ci->values[0].value.string, strlen ("Counter")) == 0)
  {
    cm->flags = UTILS_MATCH_DS_TYPE_COUNTER;
    if (strcasecmp ("CounterSet", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_COUNTER_SET;
    else if (strcasecmp ("CounterAdd", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_COUNTER_ADD;
    else if (strcasecmp ("CounterInc", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_COUNTER_INC;
    else
      cm->flags = 0;
  }
  else if (strncasecmp ("Derive", ci->values[0].value.string, strlen ("Derive")) == 0)
  {
    cm->flags = UTILS_MATCH_DS_TYPE_DERIVE;
    if (strcasecmp ("DeriveSet", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_DERIVE_SET;
    else if (strcasecmp ("DeriveAdd", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_DERIVE_ADD;
    else if (strcasecmp ("DeriveInc", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_DERIVE_INC;
    else
      cm->flags = 0;
  }
  else if (strncasecmp ("Absolute", ci->values[0].value.string, strlen ("Absolute")) == 0)
  {
    cm->flags = UTILS_MATCH_DS_TYPE_ABSOLUTE;
    if (strcasecmp ("AbsoluteSet", ci->values[0].value.string) == 0)
      cm->flags |= UTILS_MATCH_CF_ABSOLUTE_SET;
    else
      cm->flags = 0;
  }
  else
  {
    cm->flags = 0;
  }

  if (cm->flags == 0)
  {
    WARNING ("tail_apachelog plugin: `%s' is not a valid argument to `DSType'.",
	ci->values[0].value.string);
    return (-1);
  }

  return (0);
} /* int tail_apachelog_config_add_match_dstype */




static int tail_apachelog_config_add_report_threshold (tail_apachelog_config_report_t *rm,
    oconfig_item_t *ci)
{
  if (ci->values_num < 1)
  {
    WARNING ("tail_apachelog plugin: `Threshold' needs one or more float arguments.");
    return (-1);
  }
  for( i = 0; i < ci->values_num; i++)
  {
    if( ci->values[i].type != OCONFIG_TYPE_NUMBER)
    {
      WARNING ("tail_apachelog plugin: `Threshold' needs float arguments.");
      return (-1);
    }
  }

  double * thr;
  thr = malloc(sizeof(*thr) * ci->values_num);
  if (thr == NULL)
  {
    ERROR ("tail_apachelog plugin: malloc  failed.");
    return (-1);
  }
  memset(thr, 0, sizeof(*rm));
  
  for( i = 0; i < ci->values_num; i++)
  {
    thr[i] = ci->values[i].value.number;
  }

  rm->threshold = thr;
  return 0;
} /* int tail_apachelog_config_add_report_threshold */




static int tail_apachelog_config_add_report_type (tail_apachelog_config_report_t *rm,
    oconfig_item_t *ci)
{
  if (ci->values_num != 1 || ci->values[0].type != OCONFIG_TYPE_STRING)
  {
    WARNING ("tail_apachelog plugin: `Type' needs only one string arguments.");
    return (-1);
  }

  if (strcasecmp ("Count", ci->values[0].value.string) == 0)
    rm->type = TA_R_COUNT;
  else if (strcasecmp ("Count_code", ci->values[0].value.string) == 0)
    rm->type = TA_R_COUNT_CODE;
  else if (strcasecmp ("Count_size", ci->values[0].value.string) == 0)
    rm->type = TA_R_COUNT_SIZE;
  else if (strcasecmp ("Count_time", ci->values[0].value.string) == 0)
    rm->type = TA_R_COUNT_TIME;
  else if (strcasecmp ("Sum_size", ci->values[0].value.string) == 0)
    rm->type = TA_R_SUM_SIZE;
  else if (strcasecmp ("Avg_size", ci->values[0].value.string) == 0)
    rm->type = TA_R_AVG_SIZE;
  else if (strcasecmp ("Avg_time", ci->values[0].value.string) == 0)
    rm->type = TA_R_AVG_TIME;
  else
  {
     ERROR ("Type %s is unknown",ci->values[0].value.string);
     return (-2);
  }
  return 0;
} /* int tail_apachelog_config_add_report_type */





static int tail_apachelog_config_add_report (tail_apachelog_config_match_t *cm,
    oconfig_item_t *ci)
{
  tail_apachelog_config_match_t *rm;
  int status;
  int i;

  rm = malloc(sizeof(*rm));
  if (rm == NULL)
  {
    ERROR ("tail_apachelog plugin: malloc  failed.");
    return (-1);
  }
  memset(rm, 0, sizeof(*rm));

  if (ci->values_num != 0)
  {
    WARNING ("tail_apachelog plugin: Ignoring arguments for the `Report' block.");
  }

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("Instance", option->key) == 0)
      status = cf_util_get_string (option, &rm->instance);
    else if (strcasecmp ("Type", option->key) == 0)
      status = tail_apachelog_config_add_report_type (rm, option);
    else if (strcasecmp ("Threshold", option->key) == 0)
      status = tail_apachelog_config_add_report_threshold (rm, option);
    else
    {
      WARNING ("tail_apachelog plugin: Option `%s' not allowed here.", option->key);
      status = -1;
    }

    if (status != 0)
    {
      tail_apachelog_destroy_report(rm);
      return (-2);
    }
  } /* for (i = 0; i < ci->children_num; i++) */


  tail_apachelog_config_report_t **temp;

  temp = (tail_apachelog_config_report_t **) realloc (cm->report,
	sizeof (tail_apachelog_config_report_t *) * (cm->report_num + 1));
  if (temp == NULL)
  {
    ERROR ("tail_apachelog plugin: realloc failed.");
    tail_apachelog_destroy_report(rm);
    return (-1);
  }

  cm->report = temp;
  cm->report[cm->report_num] = rm;
  cm->report_num++;

  return (0);

} /* int tail_apachelog_config_add_report */




static int tail_apachelog_config_add_match (tail_apachelog_config_filemask_t *fm,
    oconfig_item_t *ci)
{

  tail_apachelog_config_match_t *cm;
  int status;
  int i;

  cm = malloc(sizeof(*cm));
  if (cm == NULL)
  {
    ERROR ("tail_apachelog plugin: malloc  failed.");
    return (-1);
  }
  memset(cm, 0, sizeof(*cm));

  if (ci->values_num != 0)
  {
    WARNING ("tail_apachelog plugin: Ignoring arguments for the `Match' block.");
  }

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("Instance", option->key) == 0)
      status = cf_util_get_string (option, &cm->instance);
    else if (strcasecmp ("Suffix", option->key) == 0)
      status = tail_apachelog_config_add_match_type (cm, option);
    else if (strcasecmp ("Prefix", option->key) == 0)
      status = tail_apachelog_config_add_match_type (cm, option);
    else if (strcasecmp ("Exact", option->key) == 0)
      status = tail_apachelog_config_add_match_type (cm, option);
    else if (strcasecmp ("Regex", option->key) == 0)
      status = tail_apachelog_config_add_match_type (cm, option);
    else if (strcasecmp ("Report", option->key) == 0)
      status = tail_apachelog_config_add_report (cm, option);
    else
    {
      WARNING ("tail_apachelog plugin: Option `%s' not allowed here.", option->key);
      status = -1;
    }

    if (status != 0)
    {
      tail_apachelog_destroy_match(cm);
      return (-2);
    }
  } /* for (i = 0; i < ci->children_num; i++) */


  tail_apachelog_config_match_t **temp;

  temp = (tail_apachelog_config_match_t **) realloc (fm->match,
	sizeof (tail_apachelog_config_match_t *) * (fm->match_num + 1));
  if (temp == NULL)
  {
    ERROR ("tail_apachelog plugin: realloc failed.");
    tail_apachelog_destroy_match(cm);
    return (-1);
  }

  fm->match = temp;
  fm->match[fm->match_num] = cm;
  fm->match_num++;

  return (0);
} /* int tail_apachelog_config_add_match */

static int tail_apachelog_config_add_files (oconfig_item_t *ci)
{
  int i;

  if (ci->values_num < 1)
  {
    WARNING ("tail_apachelog plugin: `Files' needs one or more string arguments.");
    return (-1);
  }

  for( i = 0; i < ci->values_num; i++)
  {
    if( ci->values[i].type != OCONFIG_TYPE_STRING)
    {
      WARNING ("tail_apachelog plugin: `Files' needs string arguments.");
      return (-1);
    }
  }

  for( i=0; i<ci->values_num; i++)
  {
    tail_apachelog_config_add_filemask(ci,ci->values[i].value.string);
  }
  return 0;
}

static int tail_apachelog_config_add_filemask (oconfig_item_t *ci,const char*filemask)
{

  tail_apachelog_config_filemask_t *fm;
  cdtime_t interval = 0;
  char *plugin_instance = NULL;
  int num_matches = 0;
  int status;
  int i;



  fm = malloc(sizeof(*fm));
  if (fm == NULL)
  {
    ERROR ("tail_apachelog plugin: tail_apachelog_create (%s) failed.",
	filemask);
    return (-1);
  }
  memset(fm, 0, sizeof(*id));
  fm->filemask = strdup(filemask);
  if ( !fm->filemask ){
    ERROR ("tail_apachelog plugin: memory for file `%s'.",
	filemask);
    tail_apachelog_destroy_filemask(fm);
    return (-1);
  }


  fm->interval = plugin_get_interval();
  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("Instance", option->key) == 0)
      status = cf_util_get_string (option, &fm->instance);
    else if (strcasecmp ("Format", option->key) == 0)
      status = cf_util_get_string (option, &fm->format);
    else if (strcasecmp ("Interval", option->key) == 0)
      cf_util_get_cdtime (option, &fm->interval);
    else if (strcasecmp ("Match", option->key) == 0)
    {
      status = tail_apachelog_config_add_match (fm, option);
      if (status == 0)
	num_matches++;
      /* Be mild with failed matches.. */
//      status = 0;
    }
    else
    {
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  if ( ! fm->instance ) {
    ERROR ("tail_apachelog plugin: No instance keyword for file `%s'.",
	filemask);
    tail_apachelog_destroy_filemask(fm);
    return (-1);
  }
  if (num_matches == 0 )
  {
    ERROR ("tail_apachelog plugin: No (valid) matches found for file `%s'.",
	filemask);
    tail_apachelog_destroy_filemask(fm);
    return (-1);
  }

  tail_apachelog_config_filemask_t **temp;

  temp = (tail_apachelog_config_filemask_t **) realloc (tail_apachelog_list,
	sizeof (tail_apachelog_config_filemask_t *) * (tail_apachelog_list_num + 1));
  if (temp == NULL)
  {
    ERROR ("tail_apachelog plugin: realloc failed.");
    tail_apachelog_destroy_filemask(fm);
    return (-1);
  }

  tail_apachelog_list = temp;
  tail_apachelog_list[tail_apachelog_list_num] = tm;
  tail_apachelog_list_num++;

  return (0);
} /* int tail_apachelog_config_add_filemask */

static int tail_apachelog_config (oconfig_item_t *ci)
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("Files", option->key) == 0)
      tail_apachelog_config_add_files (option);
    else
    {
      WARNING ("tail_apachelog plugin: Option `%s' not allowed here.", option->key);
    }
  } /* for (i = 0; i < ci->children_num; i++) */

  return (0);
} /* int tail_apachelog_config */



// parse line
// go per-report to make_report


// make_report:
// 



static int tail_apachelog_read (user_data_t *ud)
{
  int status;

  tail_apachelog_config_filemask_t *fm;
  fm=(tail_apachelog_config_filemask_t *)ud->data;

// glob
// parse globbed, find neu, delete old

// read list



  status = tail_apachelog_read ((tail_apachelog_config_filemask_t *)ud->data);
  if (status != 0)
  {
    ERROR ("tail_apachelog plugin: tail_apachelog_read failed.");
    return (-1);
  }

  return (0);
} /* int tail_apachelog_read */

static int tail_apachelog_init (void)
{
  struct timespec cb_interval;
  char str[255];
  user_data_t ud;
  size_t i;

  if (tail_apachelog_list_num == 0)
  {
    WARNING ("tail_apachelog plugin: File list is empty. Returning an error.");
    return (-1);
  }

  for (i = 0; i < tail_apachelog_list_num; i++)
  {
    ud.data = (void *)tail_apachelog_list[i];
    ssnprintf(str, sizeof(str), "tail-%zu", i);
    CDTIME_T_TO_TIMESPEC (tail_apachelog_list[i]->interval, &cb_interval);
    plugin_register_complex_read (NULL, str, tail_apachelog_read, &cb_interval, &ud);
  }

  return (0);
} /* int tail_apachelog_init */

static int tail_apachelog_shutdown (void)
{
  size_t i;

  for (i = 0; i < tail_apachelog_list_num; i++)
  {
    tail_apachelog_destroy (tail_apachelog_list[i]);
    tail_apachelog_list[i] = NULL;
  }
  sfree (tail_apachelog_list);
  tail_apachelog_list_num = 0;

  return (0);
} /* int tail_apachelog_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("tail_apachelog", tail_apachelog_config);
  plugin_register_init ("tail_apachelog", tail_apachelog_init);
  plugin_register_shutdown ("tail_apachelog", tail_apachelog_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 ts=8 : */
