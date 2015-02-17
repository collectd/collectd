/**
 * collectd - src/logtail.c 
 *
 * Very fast logtail analyzer. 
 *
 * Copyright (C) 2015       Andrei Darashenka
 * 
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
 *   Andrei Darashenka
 *   used parts of tail.c and tail_csv.c (c) Florian octo Forster <octo at collectd.org>
 *
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_tail.h"
#include <glob.h>
#include <float.h>

/*
It works like "tail -f" for your logs. It honours newly created/moved/truncated files. 
It can count all lines, summarize bytes, count lines per code/size/time thresholds, calculate avg of time/size

It handles 100k lines or 25MB without Writers in collectd and Regexps in plugin-config) within 4s on 1-core VIRT with E5-2670 0 @ 2.60GHz



Config example:


   <Plugin logtail>
# many files/filemas can be specified
     <Files "/var/log/apache/website1_access.log*" "/var/log/apache/website1alias_access.log*">
 	Instance "website1"

#        Interval 60

## text or symbols - what should match
## %? - any word(to next separator)
## "%?" - any line in quotes (single or double)
## %r - special keyword: request URI
## %s - size
## %b - bytes
## %D - (micro)seconds
#        Format "%? %? %? %? %? \"%? %r %?\" %s %b \"%?\" \"%?\" %D %? %?"
## todo: Exclude "%? localhost "

# what to match for URI
	<Match>
	  Suffix .jpg .png .css .js
          Prefix "/static/"
          Instance "static"

          <Report>
	      Type "count"
#	      Instance "count"
          </Report>
          <Report>
	      Type "count_code"
              Threshold 200 300 400 404 405 500 501 505
#	       Instance "count_code"
          </Report>
          <Report>
	      Type "count_time"
              Threshold 1000 10000 100000 1000000
#	      Instance "count_time"
          </Report>
          <Report>
	      Type "count_size"
              Threshold 1000 10000 100000 1000000
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
#todo:	  Regex ^/user/
#         ..
	</Match>
	<Match>
	  Prefix /
#         ..
	</Match>

    </Files>
  </Plugin>



 */

struct logtail_config_report_s
{
  char *instance;
  int type;
  size_t *threshold;
  size_t threshold_num;
  void *data;
};
typedef struct logtail_config_report_s logtail_config_report_t;

struct logtail_config_matchset_s
{
  char *mask;
  int type;
};
typedef struct logtail_config_matchset_s logtail_config_matchset_t;

struct logtail_config_match_s
{
  logtail_config_matchset_t **matchset;
  size_t matchset_num;
  logtail_config_report_t** report;
  size_t report_num;
  char *instance;
};
typedef struct logtail_config_match_s logtail_config_match_t;


struct logtail_current_data_s 
{
  long code;
  size_t size;
  size_t time;
  char * path;
};
typedef struct logtail_current_data_s logtail_current_data_t;

struct logtail_config_filemask_s
{
  char *filemask;
  cu_tail_t **tail;
  size_t tail_num;
  logtail_config_match_t** match;
  size_t match_num;
  char *instance;
  cdtime_t interval;
  char *format;
  char *format_parsed;
};
typedef struct logtail_config_filemask_s logtail_config_filemask_t;

#define TA_R_COUNT         1
#define TA_R_COUNT_CODE    2
#define TA_R_COUNT_TIME    3
#define TA_R_COUNT_SIZE    4
#define TA_R_SUM_SIZE      5
#define TA_R_AVG_SIZE      6
#define TA_R_AVG_TIME      7

// for filemask_t->format_parsed. 
#define TA_F_STOP          0
#define TA_F_WORD          1
#define TA_F_QUOTED        2
#define TA_F_SPACES        3
#define TA_F_CODE          4
#define TA_F_SIZE          5
#define TA_F_TIME          6
#define TA_F_PATH          7
#define TA_F_MAXVALUE      TA_F_PATH


// for match
#define TA_M_PREFIX        1
#define TA_M_EQUAL         2
#define TA_M_SUFFIX        3
#define TA_M_SUFFIXNQ      4
#define TA_M_REGEXP        5


logtail_config_filemask_t **logtail_list = NULL;
size_t logtail_list_num = 0;

static void logtail_destroy_report(logtail_config_report_t*rm)
{
    if(rm->instance)sfree(rm->instance);
    rm->instance = 0;

    if(rm->data)
        sfree(rm->data);

    if(rm->threshold)
        sfree(rm->threshold);
    rm->threshold = 0;
    rm->threshold_num=0;

    sfree(rm);
}

static void logtail_destroy_match(logtail_config_match_t*cm)
{
    int i;

    if(cm->instance)sfree(cm->instance);
    cm->instance = 0;

    for(i=0;i<cm->matchset_num;i++)
        sfree(cm->matchset[i]);
    if(cm->matchset)
        sfree(cm->matchset);
    cm->matchset = 0;
    cm->matchset_num=0;

    for(i=0;i<cm->report_num;i++)
        logtail_destroy_report(cm->report[i]);
    if(cm->report)
        sfree(cm->report);
    cm->report = 0;
    cm->report_num=0;

    sfree(cm);
}

static void logtail_destroy_filemask(logtail_config_filemask_t*fm)
{
    int i;

    if(fm->filemask)sfree(fm->filemask);
    fm->filemask = 0;

    if(fm->format)sfree(fm->format);
    fm->format = 0;

    if(fm->format_parsed)sfree(fm->format_parsed);
    fm->format_parsed = 0;

    if(fm->instance)sfree(fm->instance);
    fm->instance = 0;

    for(i=0;i<fm->tail_num;i++)
        cu_tail_destroy(fm->tail[i]);
    if(fm->tail)
        sfree(fm->tail);
    fm->tail = 0;
    fm->tail_num=0;

    for(i=0;i<fm->match_num;i++)
        logtail_destroy_match(fm->match[i]);
    if(fm->match)sfree(fm->match);
    fm->match=0;
    fm->match_num=0;

    sfree(fm);
}




static int logtail_config_add_report_threshold (logtail_config_report_t *rm,
    oconfig_item_t *ci)
{
  int i;
  double prev=-DBL_MAX;
  if (ci->values_num < 1)
  {
    ERROR ("logtail plugin: `Threshold' needs one or more uint arguments.");
    return (-1);
  }
  for( i = 0; i < ci->values_num; i++)
  {
    if( ci->values[i].type != OCONFIG_TYPE_NUMBER)
    {
      ERROR ("logtail plugin: `Threshold' needs uint arguments.");
      return (-1);
    }
  }

  size_t * thr;
  thr = malloc(sizeof(*thr) * ci->values_num);
  if (thr == NULL)
  {
    ERROR ("logtail plugin: malloc  failed.%d",__LINE__);
    return (-1);
  }
  memset(thr, 0, sizeof(*rm));
  
  for( i = 0; i < ci->values_num; i++)
  { 
    thr[i] = ci->values[i].value.number;
    if( i>0 && thr[i] <= prev ){
      ERROR ("logtail plugin: `Threshold' list should be incremental.");
      return (-1);
    }
    prev = thr[i];
  }

  rm->threshold = thr;
  rm->threshold_num = ci->values_num;

  return 0;
} /* int logtail_config_add_report_threshold */




static int logtail_config_add_report_type (logtail_config_report_t *rm,
    oconfig_item_t *ci)
{
  if (ci->values_num != 1 || ci->values[0].type != OCONFIG_TYPE_STRING)
  {
    WARNING ("logtail plugin: `Type' needs only one string arguments.");
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
} /* int logtail_config_add_report_type */





static int logtail_config_add_report (logtail_config_match_t *cm,
    oconfig_item_t *ci)
{
  logtail_config_report_t *rm;
  int status;
  int i;

  rm = malloc(sizeof(*rm));
  if (rm == NULL)
  {
    ERROR ("logtail plugin: malloc  failed.%d",__LINE__);
    return (-1);
  }
  memset(rm, 0, sizeof(*rm));

  if (ci->values_num != 0)
  {
    WARNING ("logtail plugin: Ignoring arguments for the `Report' block.");
  }

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("Instance", option->key) == 0)
      status = cf_util_get_string (option, &rm->instance);
    else if (strcasecmp ("Type", option->key) == 0)
      status = logtail_config_add_report_type (rm, option);
    else if (strcasecmp ("Threshold", option->key) == 0)
      status = logtail_config_add_report_threshold (rm, option);
    else
    {
      WARNING ("logtail plugin: Option `%s' not allowed here.", option->key);
      status = -1;
    }

    if (status != 0)
    {
      logtail_destroy_report(rm);
      return (-2);
    }
  } /* for (i = 0; i < ci->children_num; i++) */

  if(!rm->instance){
     char * instanceName[] = {
            "(null)",
            "count",
            "count_code",
            "count_time",
            "count_size",
            "sum_size",
            "avg_size",
            "avg_time" };
        
     rm->instance=strdup(instanceName[rm->type]);

  }
  if( (rm->type == TA_R_COUNT_CODE || rm->type == TA_R_COUNT_SIZE || rm->type == TA_R_COUNT_TIME) && rm->threshold_num < 2 ){
      ERROR ("logtail plugin: report_threshold: COUNT_CODE, COUNT_SIZE, COUNT_TIME should have at leat 2 thresholds");
      logtail_destroy_report(rm);
      return (-2);
  }

  if ( rm->type == TA_R_COUNT_CODE || rm->type == TA_R_COUNT_SIZE || rm->type == TA_R_COUNT_TIME ){
     rm->data = malloc(sizeof(counter_t)*rm->threshold_num);
     if(rm->data)   memset(rm->data, 0, sizeof(counter_t)*rm->threshold_num);
     ERROR ("logtail plugin: report_threshold: created array of size %zu, %p",rm->threshold_num,rm->data);
  }else if(rm->type == TA_R_AVG_SIZE || rm->type ==  TA_R_AVG_TIME){
     rm->data = malloc(sizeof(counter_t)*2);
     if(rm->data)   memset(rm->data, 0, sizeof(counter_t)*2);
  }else{
     rm->data = malloc(sizeof(counter_t));
     if(rm->data)   memset(rm->data, 0, sizeof(counter_t));
  }

  if (rm->data == NULL)
  {
    ERROR ("logtail plugin: malloc  failed.%d",__LINE__);
    logtail_destroy_report(rm);
    return (-1);
  }

  logtail_config_report_t **temp;

  temp = (logtail_config_report_t **) realloc (cm->report,
	sizeof (logtail_config_report_t *) * (cm->report_num + 1));
  if (temp == NULL)
  {
    ERROR ("logtail plugin: realloc failed.%d",__LINE__);
    logtail_destroy_report(rm);
    return (-1);
  }

  cm->report = temp;
  cm->report[cm->report_num] = rm;
  cm->report_num++;

  return (0);

} /* int logtail_config_add_report */



static int logtail_config_add_match_type(logtail_config_match_t *cm,oconfig_item_t *option)
{
  int type;
  int i;

  logtail_config_matchset_t *ms;
  if (option->values_num < 1)
  {
    WARNING ("logtail plugin: '%s' needs one or more string arguments.",option->key);
    return (-1);
  }
  for( i = 0; i < option->values_num; i++)
  {
    if( option->values[i].type != OCONFIG_TYPE_STRING)
    {
      WARNING ("logtail plugin: '%s' needs string arguments.",option->key);
      return (-1);
    }
  }

  if (strcasecmp ("Equal", option->key) == 0)
    type = TA_M_EQUAL;
  else if (strcasecmp ("Prefix", option->key) == 0)
    type = TA_M_PREFIX;
  else if (strcasecmp ("Suffix", option->key) == 0)
    type = TA_M_SUFFIX;
  else if (strcasecmp ("SuffixNoQuery", option->key) == 0)
    type = TA_M_SUFFIXNQ;
  else if (strcasecmp ("Regexp", option->key) == 0)
    type = TA_M_REGEXP;
  else
  {
     ERROR ("Type %s is unknown",option->key);
     return (-2);
  }

  logtail_config_matchset_t **temp;

  temp = (logtail_config_matchset_t **) realloc (cm->matchset,
        sizeof (logtail_config_matchset_t *) * (cm->matchset_num + option->values_num));
  if (temp == NULL)
  {
    ERROR ("logtail plugin: realloc failed.%d",__LINE__);
    return (-1);
  }
  cm->matchset = temp;

  for( i = 0; i < option->values_num; i++)
  {
    ms = malloc(sizeof(*ms));
    if (ms == NULL)
    {
      ERROR ("logtail plugin: malloc  failed.%d",__LINE__);
      return (-1);
    }
    memset(ms, 0, sizeof(*ms));
    ms->type = type;
    ms->mask = strdup(option->values[i].value.string);
    if(!ms->mask){
      ERROR ("logtail plugin: realloc failed.%d",__LINE__);
      return (-1);
    }

    cm->matchset[cm->matchset_num] = ms;
    cm->matchset_num++;
  }


  return 0;
}
static int logtail_config_add_match (logtail_config_filemask_t *fm,
    oconfig_item_t *ci)
{

  logtail_config_match_t *cm;
  int status;
  int i;

  cm = malloc(sizeof(*cm));
  if (cm == NULL)
  {
    ERROR ("logtail plugin: malloc  failed.%d",__LINE__);
    return (-1);
  }
  memset(cm, 0, sizeof(*cm));

  if (ci->values_num != 0)
  {
    WARNING ("logtail plugin: Ignoring arguments for the `Match' block.");
  }

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("Instance", option->key) == 0)
      status = cf_util_get_string (option, &cm->instance);
    else if (
              strcasecmp ("Suffix", option->key) == 0 ||
              strcasecmp ("Prefix", option->key) == 0 ||
              strcasecmp ("Exact", option->key) == 0 ||
              strcasecmp ("ExactNQ", option->key) == 0 ||
              strcasecmp ("Regex", option->key) == 0 
            ) 
      status = logtail_config_add_match_type (cm, option);
    else if (strcasecmp ("Report", option->key) == 0)
      status = logtail_config_add_report (cm, option);
    else
    {
      WARNING ("logtail plugin: Option `%s' not allowed here.", option->key);
      status = -1;
    }

    if (status != 0)
    {
      logtail_destroy_match(cm);
      return (-2);
    }
  } /* for (i = 0; i < ci->children_num; i++) */

  if(!cm->instance){
    ERROR("logtail plugin: 'Match' block has no Instance key");
    logtail_destroy_match(cm);
    return (-2);
  }

  logtail_config_match_t **temp;

  temp = (logtail_config_match_t **) realloc (fm->match,
	sizeof (logtail_config_match_t *) * (fm->match_num + 1));
  if (temp == NULL)
  {
    ERROR ("logtail plugin: realloc failed.%d",__LINE__);
    logtail_destroy_match(cm);
    return (-1);
  }

  fm->match = temp;
  fm->match[fm->match_num] = cm;
  fm->match_num++;

  return (0);
} /* int logtail_config_add_match */

static char* logtail_config_parse_format(const char*format)
{
  char*ret;
  int i;
  ret=strdup(format);
  if ( !ret ){
    ERROR ("logtail plugin: strdup failed.%d",__LINE__);
    return (0);
  }

  for(i=0;i<strlen(ret);i++){
     if(strncmp("\"%?\"",ret+i,4) == 0){
       ret[i]=TA_F_QUOTED;
       memmove(&ret[i+1],&ret[i+4],strlen(&ret[i+4])+1);
     }else if(strncmp("%?",ret+i,2) == 0){
       ret[i]=TA_F_WORD;
       memmove(&ret[i+1],&ret[i+2],strlen(&ret[i+2])+1);
     }else if(strncmp("\\s+",ret+i,3) == 0){
       ret[i]=TA_F_SPACES;
       memmove(&ret[i+1],&ret[i+3],strlen(&ret[i+3])+1);
     }else if(strncmp("%r",ret+i,2) == 0){
       ret[i]=TA_F_PATH;
       memmove(&ret[i+1],&ret[i+2],strlen(&ret[i+2])+1);
     }else if(strncmp("%s",ret+i,2) == 0){
       ret[i]=TA_F_CODE;
       memmove(&ret[i+1],&ret[i+2],strlen(&ret[i+2])+1);
     }else if(strncmp("%b",ret+i,2) == 0){
       ret[i]=TA_F_SIZE;
       memmove(&ret[i+1],&ret[i+2],strlen(&ret[i+2])+1);
     }else if(strncmp("%D",ret+i,2) == 0){
       ret[i]=TA_F_TIME;
       memmove(&ret[i+1],&ret[i+2],strlen(&ret[i+2])+1);
     }else if(!ret[i] && ret[i] < TA_F_MAXVALUE){
       ERROR ("logtail plugin: in format are bad character with code %i", ret[i]);
       return (0);
     }
//    DEBUG ("logtail plugin: %d\n%s",i, ret);

  }
  return ret;

}

static int logtail_config_add_filemask (oconfig_item_t *ci,const char*filemask)
{

  logtail_config_filemask_t *fm;
  int num_matches = 0;
  int status;
  int i;



  fm = malloc(sizeof(*fm));
  if (fm == NULL)
  {
    ERROR ("logtail plugin: logtail_create (%s) failed.",
	filemask);
    return (-1);
  }
  memset(fm, 0, sizeof(*fm));
  fm->filemask = strdup(filemask);
  if ( !fm->filemask ){
    ERROR ("logtail plugin: alloc failed.%d",__LINE__);
    logtail_destroy_filemask(fm);
    return (-1);
  }

  fm->format = strdup("%? %? %? %? %? \"%? %r %? %s %b \"%?\" \"%?\" %D");
  if(!fm->format){
      ERROR ("logtail plugin: alloc failed.%d",__LINE__);
    logtail_destroy_filemask(fm);
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
      status = logtail_config_add_match (fm, option);
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

  fm->format_parsed=logtail_config_parse_format(fm->format);

  if ( ! fm->instance ) {
    ERROR ("logtail plugin: No instance keyword for file `%s'.",
	filemask);
    logtail_destroy_filemask(fm);
    return (-1);
  }
  if (num_matches == 0 )
  {
    ERROR ("logtail plugin: No (valid) matches found for file `%s'.",
	filemask);
    logtail_destroy_filemask(fm);
    return (-1);
  }

  logtail_config_filemask_t **temp;

  temp = (logtail_config_filemask_t **) realloc (logtail_list,
	sizeof (logtail_config_filemask_t *) * (logtail_list_num + 1));
  if (temp == NULL)
  {
    ERROR ("logtail plugin: realloc failed.%d",__LINE__);
    logtail_destroy_filemask(fm);
    return (-1);
  }

  logtail_list = temp;
  logtail_list[logtail_list_num] = fm;
  logtail_list_num++;

  return (0);
} /* int logtail_config_add_filemask */


static int logtail_config_add_files (oconfig_item_t *ci)
{
  int i;

  if (ci->values_num < 1)
  {
    WARNING ("logtail plugin: `Files' needs one or more string arguments.");
    return (-1);
  }

  for( i = 0; i < ci->values_num; i++)
  {
    if( ci->values[i].type != OCONFIG_TYPE_STRING)
    {
      WARNING ("logtail plugin: `Files' needs string arguments.");
      return (-1);
    }
  }

  for( i=0; i<ci->values_num; i++)
  {
    logtail_config_add_filemask(ci,ci->values[i].value.string);
  }
  return 0;
}

static int logtail_config (oconfig_item_t *ci)
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("Files", option->key) == 0)
      logtail_config_add_files (option);
    else
    {
      WARNING ("logtail plugin: Option `%s' not allowed here.", option->key);
    }
  } /* for (i = 0; i < ci->children_num; i++) */

  return (0);
} /* int logtail_config */



// really create a new cu_tail_t entry
static int logtail_file_create(logtail_config_filemask_t *fm,char * file)
{
  cu_tail_t **temp;
  cu_tail_t *tm;
  tm = cu_tail_create (file);

  if (tm == NULL)
  {
    ERROR ("logtail plugin: cu_tail_create (\"%s\") failed.",
                    file);
    return (-1);
  }

  temp = (cu_tail_t **) realloc (fm->tail,
	sizeof (cu_tail_t *) * (fm->tail_num + 1));
  if (temp == NULL)
  {
    ERROR ("logtail plugin: realloc failed.%d",__LINE__);
    return (-1);
  }

  fm->tail = temp;
  fm->tail[fm->tail_num] = tm;
  fm->tail_num++;
  INFO ("logtail plugin: Adding new file %s", file);
  return 0;
}




// add a file to fm->tail if not allready present
static int logtail_addfile(logtail_config_filemask_t *fm,char * file)
{
  int i,status;
  if(!fm->tail)
  {
    return logtail_file_create(fm,file);
  }

  for(i=0;i<fm->tail_num;i++){
    cu_tail_t *tm;
    tm=fm->tail[i];
    status =  strcmp( tm->file,file);
    if( status == 0 )
      return 0;
  }

  return logtail_file_create(fm,file);
}


static int logtail_glob(logtail_config_filemask_t *fm){
  glob_t res;
  int i,ret;
  int flags = GLOB_BRACE|GLOB_TILDE_CHECK|GLOB_ERR;

  ret = glob(fm->filemask,flags, 0, &res);
  if(ret){
    ERROR ("logtail plugin: glob failed %d for %s",ret,fm->filemask);
    return ret;
  }


    
  for(i=0;i<res.gl_pathc;i++)
      logtail_addfile(fm,res.gl_pathv[i]);

  globfree(&res);
  return 0;
}


static int logtail_read_parse(logtail_config_filemask_t *fm,char *buf,logtail_current_data_t *cur_data){
  int x,y,i;
  char quote;
  char *next;
  int escaped=0;
  DEBUG ("logtail plugin: parse\n%s",buf);

  for(x=0,y=0;buf[x]&&fm->format_parsed[y];y++){
//     DEBUG ("logtail plugin: parse at %d %c to %c\n%s",x,buf[x],fm->format_parsed[y],&buf[x]);
     switch(fm->format_parsed[y]){
       case TA_F_SPACES:
          i=strspn(buf+x," \t\n\r\f\v");
          if(i == 0){
            WARNING ("logtail plugin: parse %s failed at %d. expected space/tab/cr/ln, found '%c'",buf,x,buf[x]);
            return(-1);
          }
          x+=i;
          break;
       case TA_F_WORD:
          quote=fm->format_parsed[y+1];
          if(!quote)quote=' ';
          next=strchr(buf+x,quote);
          if(next == 0){
            WARNING ("logtail plugin: parse %s failed at %d. expected non-space, found '%c'",buf,x,buf[x]);
            return(-1);
          }
          x=next-buf;
          break;
       case TA_F_QUOTED:

          i=strspn(&buf[x],"\"'");
          if(i == 0){
            WARNING ("logtail plugin: parse %s failed at %d. expected quote, found '%c'",buf,x,buf[x]);
            return(-1);
          }
          quote=buf[x];
          for(i=1;buf[x+i];i++){
             if(!escaped && buf[x+i] == '\\'){
               escaped=1;
               continue;
             }
             if(escaped){
               escaped=0;
               continue;
             }
             if( buf[x+i] == quote ){
               x+=i+1;
               break;
             }
          }
          break;
       case TA_F_CODE:
         if(!isdigit(buf[x])){
            WARNING ("logtail plugin: parse failed at %d. expected code(digit), found '%c'\n%s\n%s\n",x,buf[x],buf,fm->format);
            return(-1);
         }
         cur_data->code=strtoul(&buf[x],&next,0);
	 x=next-buf;
         break;
       case TA_F_SIZE:
         if(!isdigit(buf[x])){
            WARNING ("logtail plugin: parse failed at %d. expected size(digit), found '%c'\n%s\n%s\n",x,buf[x],buf,fm->format);
            return(-1);
         }
         cur_data->size=strtoul(&buf[x],&next,0);
         x=next-buf;
         break;

       case TA_F_TIME:
         if(!isdigit(buf[x])){
            WARNING ("logtail plugin: parse failed at %d. expected duration time(digit), found '%c'\n%s\n%s\n",x,buf[x],buf,fm->format);
            return(-1);
         }
         cur_data->time=strtoul(&buf[x],&next,0);
         x=next-buf;
         break;

       case TA_F_PATH:
         if(isspace(buf[x])){
            WARNING ("logtail plugin: parse failed at %d. expected path(not space), found '%c'\n%s\n%s\n",x,buf[x],buf,fm->format);
            return(-1);
         }

         i=strcspn(buf+x," \t\n\r\f\v");
         if(i == 0){
            WARNING ("logtail plugin: parse %s failed at %d. expected non-space, found '%c'",buf,x,buf[x]);
            return(-1);
         }
         cur_data->path=strndup(buf+x,i);
         if(!cur_data->path){
            ERROR ("logtail plugin: strndup%d",__LINE__);
            return(-1);
         }
         x+=i;
         break;

       default:
         if(fm->format_parsed[y] != buf[x] ){
           WARNING ("logtail plugin: parse failed  at %d. expected '%c', found '%c'\n%s\n%s\n",x,fm->format_parsed[y],buf[x],buf,fm->format);
           return (-1);
         }
         x++;
     }
  }
  if(!buf[x]&&fm->format_parsed[y]){
    WARNING ("logtail plugin: parse failed at %d. expected '%c', found EOL",x,fm->format_parsed[y]);
    return (-1);
  }
//  DEBUG("logtail plugin: parse: size=%ld time=%ld code=%ld path=%s",cur_data->size,cur_data->time,cur_data->code,cur_data->path);
  return 0;
}
static int logtail_match_one(logtail_config_matchset_t *ms,logtail_current_data_t *data){
  int i,j;
  switch(ms->type){
    case TA_M_EQUAL:
      return (!strcmp(data->path,ms->mask)); 
    case TA_M_PREFIX:
      return(!strncmp(data->path,ms->mask,strlen(ms->mask)));
    case TA_M_SUFFIX:
      i=strlen(ms->mask);
      j=strlen(data->path);
      if(j<i)return 0;
      return( !strcmp(data->path+j-i,ms->mask));
      break;
    case TA_M_SUFFIXNQ:
      ERROR ("logtail plugin: todo: suffixnq ");
      i=strlen(ms->mask);
      j=strlen(data->path);
      if(j<i)return 0;
      return(!strcmp(data->path+j-i,ms->mask));

    case TA_M_REGEXP:
      ERROR ("logtail plugin: todo: regexp ");
      return 0;
    default:
      ERROR ("logtail plugin: match: unknown type %d ",ms->type);
      return 0;
  }
  return 0;
}

static int logtail_match(logtail_config_match_t*cm,logtail_current_data_t *data){
  int x;
  for(x=0;x<cm->matchset_num;x++)
    if(logtail_match_one(cm->matchset[x],data))
      return 1;

  return 0;
}

// select position in threshold array
static int logtail_threshold_getindex(logtail_config_report_t*rm,double val){
  int i;

// todo: make fast dihotomie
  for(i=0; i<rm->threshold_num;i++)
    if(rm->threshold[i] >= val)
      return i;
  return rm->threshold_num-1;
}

static int logtail_update_report(logtail_config_report_t*rm,logtail_current_data_t *data){
  size_t *cnt  = (size_t*)rm->data;
  int i;

  switch(rm->type){
   case TA_R_COUNT:
      (*cnt)++;
      break;
   case TA_R_COUNT_CODE:
      i = logtail_threshold_getindex(rm,data->code);
      (counter_t)cnt[i]++;
      break;
   case TA_R_COUNT_TIME:
      i = logtail_threshold_getindex(rm,data->time);
      (counter_t)cnt[i]++;
      break;
   case TA_R_COUNT_SIZE:
      i = logtail_threshold_getindex(rm,data->size);
      (counter_t)cnt[i]++;
      break;
   case TA_R_SUM_SIZE:
      (*cnt)+=data->size;
      break;
   case TA_R_AVG_SIZE:
      cnt[0]++;
      cnt[1]+=data->size;
      break;
   case TA_R_AVG_TIME:
      cnt[0]++;
      cnt[1]+=data->time;
      break; 
  }
  return 0;
}

static int logtail_read_callback (void *data, char *buf,
    int __attribute__((unused)) buflen)
{
  logtail_config_filemask_t *fm;
  logtail_current_data_t cur_data; // current parsed line
  
  int status,i,match;

  memset(&cur_data,0,sizeof(cur_data));
  fm=(logtail_config_filemask_t *)data;

  status = logtail_read_parse(fm,buf,&cur_data);
  if(status) return status;

  for(i=0;i<fm->match_num;i++)
    if(logtail_match(fm->match[i],&cur_data))
      break;
  if(i>= fm->match_num) // nothing matched
    return 0;
  match=i;

  DEBUG("logtail plugin: matched: %d",match);

  for(i=0;i<fm->match[match]->report_num;i++)
     logtail_update_report(fm->match[match]->report[i],&cur_data);

  if(cur_data.path) sfree(cur_data.path);
  
  return 0;
}

static void submit_value (const char *type, const char *name,
                value_t value)
{
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &value;
  vl.values_len = 1;

  sstrncpy(vl.host, hostname_g, sizeof (vl.host));
  sstrncpy(vl.plugin, "logtail", sizeof(vl.plugin));
      
  sstrncpy (vl.plugin_instance, name, sizeof (vl.plugin_instance));

  sstrncpy (vl.type, type, sizeof (vl.type));
//  if (type_instance != NULL)
//    sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* void submit_value */


static void submit_gauge (const char *type, const char *type_instance,
                gauge_t g)
{
        value_t v;
        v.gauge = g;
        submit_value (type, type_instance, v);
} /* void submit_gauge */

static void submit_counter (const char *type, const char *type_instance,
                counter_t c)
{
        value_t v;
        v.counter = c;
        submit_value (type, type_instance, v);
} /* void submit_gauge */

static void logtail_send(logtail_config_filemask_t *fm)
{
  int i,j,k;
  char name1[1024],name2[1024];
  gauge_t g;
  counter_t *data;

  for(i=0;i<fm->match_num;i++){
    for(j=0;j<fm->match[i]->report_num;j++){
      snprintf(name1, sizeof (name1),"%s.%s",fm->match[i]->instance,fm->match[i]->report[j]->instance);
      data=(counter_t*) fm->match[i]->report[j]->data;

      switch(fm->match[i]->report[j]->type){
        case TA_R_COUNT:
        case TA_R_SUM_SIZE:
          submit_counter( (fm->match[i]->report[j]->type == TA_R_COUNT ? "objects" : "bytes") , name1, *data );
          break;

        case TA_R_COUNT_CODE:
        case TA_R_COUNT_TIME:
        case TA_R_COUNT_SIZE:
          for(k=0;k<fm->match[i]->report[j]->threshold_num;k++){
            snprintf(name2,sizeof(name2),"%s.%zu",name1,fm->match[i]->report[j]->threshold[k]);
            submit_gauge("objects", name2, data[k] ); 
          }
          memset(data,0,sizeof(counter_t) * fm->match[i]->report[j]->threshold_num);
          break;

        case TA_R_AVG_SIZE:
        case TA_R_AVG_TIME:
          g= data[0] ? 1.0 * data[1] / data[0] : 0.0;
          submit_gauge(fm->match[i]->report[j]->type == TA_R_AVG_SIZE ? "bytes" : "duration", name1, g );
          memset(data,0,sizeof(counter_t)*2);
          break;
      }
    }
  }


}

static int logtail_read (user_data_t *ud)
{
  int status,i,j;
  char buffer[66000];

  logtail_config_filemask_t *fm;
  fm=(logtail_config_filemask_t *)ud->data;

  status = logtail_glob(fm);
  if(!fm->tail){
    ERROR ("logtail plugin: no files to tail for %s",fm->filemask);
    return (-1);
  }

  for(i=0;i<fm->tail_num;i++)
  {
    status = cu_tail_read(fm->tail[i], buffer, (int) sizeof (buffer), logtail_read_callback, (void*)fm);
    if (status != 0)
    {
      INFO ("logtail plugin: logtail_read failed for %s",fm->tail[i]->file);
      cu_tail_destroy(fm->tail[i]);
      for(j=i+1;j<fm->tail_num;j++)
        fm->tail[j-1] = fm->tail[j];
      fm->tail_num--;
      i--;
    }
  }

  logtail_send(fm);

  return (0);
} /* int logtail_read */

static int logtail_init (void)
{
  struct timespec cb_interval;
  char str[255];
  user_data_t ud;
  size_t i;

  if (logtail_list_num == 0)
  {
    WARNING ("logtail plugin: File list is empty. Returning an error.");
    return (-1);
  }

  for (i = 0; i < logtail_list_num; i++)
  {
    ud.data = (void *)logtail_list[i];
    ssnprintf(str, sizeof(str), "tail-%zu", i);
    CDTIME_T_TO_TIMESPEC (logtail_list[i]->interval, &cb_interval);
    plugin_register_complex_read (NULL, str, logtail_read, &cb_interval, &ud);
  }

  return (0);
} /* int logtail_init */

static int logtail_shutdown (void)
{
  size_t i;

  for (i = 0; i < logtail_list_num; i++)
  {
    logtail_destroy_filemask (logtail_list[i]);
    logtail_list[i] = NULL;
  }
  sfree (logtail_list);
  logtail_list_num = 0;

  return (0);
} /* int logtail_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("logtail", logtail_config);
  plugin_register_init ("logtail", logtail_init);
  plugin_register_shutdown ("logtail", logtail_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 ts=8 : */
