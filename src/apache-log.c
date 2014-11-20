/**
 * collectd - src/apache-log.c
 * Copyright (C) 2014	Toni Moreno 
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
 *   Toni Moreno  <toni.moreno at gmail.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include <libgen.h>
#include <sys/time.h>
#include <unistd.h>

#include <regex.h>
#include "utils_tail.h"

/*
 *  <Plugin apachelog>
 *    <File "/var/log/apache2/access.log*">  //filename Pattern on Rotatelog environment
 *	Instance "www_misite_com"
 *      RenamePluginAs "apache"
 * 	UseApacheRotatedLogs "false" 
 *	ExtendedMetrics "true"  // "false"=normal - "true" = extended Default=false
 *	SetRespTimeField 0 //0=last 1=first 2=second .. etc default=0 ( %D Apachelog Field)
 *      SetHTTPCodeField  9  //0=last 1=first 2=second .. etc default=9 (Only used on ExtendedMetrics=true )
 *
 *    //http://httpd.apache.org/docs/2.2/programs/rotatelogs.html
 *    </File>
 *    <File "/var/log/apache2/access.log">  //filename Name on a fixed log name
 *	Instance "www_misite_com"
 *      RenamePluginAs "apache"
 * 	UseApacheRotatedLogs "false"
 *    //http://httpd.apache.org/docs/2.2/programs/rotatelogs.html
 *    </File>

 *  </Plugin>
 */

#define APACHELOG_MAX_FIELDS	100


struct cu_apachelog_s
{
  _Bool use_rotatelogs;
  //tail struct
  char *filename; 
  char *filename_pattern; 
 
  cu_tail_t *tail;

  //plugin data
  char *plugin_name;
  char *plugin_instance;
  char *rename_plugin_as;

  //util buffer

  char **logline_ptr;

  _Bool stat_mode; // 0=normal - 1 = extended

  // data position

  int response_time_position; //0=last 1=first 2=second .. etc
  int http_code_position;//0=last 1=first 2=second .. etc

  // basic metrics

  //unsigned int hits_count;
  //unsigned int response_time_max;
  //unsigned int response_time_min;
  //unsigned int response_time_sum;

  // extended metrics per HTTP response type.
  /*
   * Total = 0
   * 1XX = 1
   * 2XX = 2
   * 3XX = 3
   * 4XX = 4
   * 5XX = 5
   */


  unsigned int httpxxx_hits_count[6]; 
  unsigned int httpxxx_response_time_max[6];
  unsigned int httpxxx_response_time_min[6];
  unsigned int httpxxx_response_time_sum[6];


  char *httpxxx_suffix[6];
};

typedef struct cu_apachelog_s cu_apachelog_t;




cu_apachelog_t **apachelog_list = NULL;
size_t apachelog_list_num = 0;

static void apachelog_submit_http_perf (char *apache_instance,const char *rename_plugin_as, char *type_instance,unsigned int count, gauge_t max, gauge_t avg,gauge_t min)
{
        value_t values[5];
        value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = (gauge_t) count;  	// hit_x_interval
	values[1].absolute = (absolute_t)count; // rate ( hits/second )
        values[2].gauge = avg/1000.0;		// Response time (ms)
	values[3].gauge = max/1000.0;		// Response time (ms)
	values[4].gauge = min/1000.0;		// Response time (ms)

        vl.values = values;
        vl.values_len = 5;
        sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	if( rename_plugin_as != NULL )
		ssnprintf (vl.plugin, sizeof (vl.plugin),"%s", rename_plugin_as);
	else
		sstrncpy (vl.plugin, "apache-log", sizeof (vl.plugin));
        ssnprintf (vl.plugin_instance, sizeof (vl.plugin_instance),"%s", apache_instance);
        sstrncpy (vl.type, "http_perf", sizeof (vl.type));
	ssnprintf (vl.type_instance, sizeof (vl.type_instance),"%s",type_instance);

        plugin_dispatch_values (&vl);
}


char* str_concat(char *s1, char *s2,char *s3)
{
    int l1=strlen(s1);
    int l2=strlen(s2);
    int l3=strlen(s3);
    int total_length=l1+l2+l3+2;
    char *result = malloc(total_length);//+1 for the zero-terminator
    if(result == NULL )
     return (NULL);
    memset (result, '\0',total_length);
    //in real code you would check for errors in malloc here
    strncpy(result, s1,l1);
    strncat(result, s2,l2);
    strncat(result, s3,l3);
    return result;
}


char *apachelog_name_filter;

int qs_mtime_pivot(char* dirname,struct dirent **namelist, int izq, int der)
{
    int i;
    int ret;
    int pivote;	
    time_t valor_pivote;
    char *filename;

    struct dirent *aux;
    struct stat sbuf;

    if(izq == der) {
        DEBUG("APACHELOG QuickSort not needed : ");
	ret=1;
        return  ret;
    }


    pivote = izq;

    filename=str_concat(dirname,"/",namelist[pivote]->d_name);
    ret=stat(filename, &sbuf);
    free(filename);
    
    valor_pivote = sbuf.st_mtime;
    DEBUG("APACHELOG QuickSort FILE: %s | %d |ret:%d ",namelist[pivote]->d_name,(int)valor_pivote,ret);

    for (i=izq+1; i<=der; i++){
	filename=str_concat(dirname,"/",namelist[i]->d_name);
	ret=stat(filename, &sbuf);
        free(filename);
	
        if (sbuf.st_mtime < valor_pivote){
		//DEBUG("APACHELOG QuickSort FILE interchange: %s | %d |ret:%d ",namelist[i]->d_name,(int)sbuf.st_mtime,ret);
                pivote++;
                aux=namelist[i];
                namelist[i]=namelist[pivote];
                namelist[pivote]=aux;

        }
    }
    aux=namelist[izq];
    namelist[izq]=namelist[pivote];
    namelist[pivote]=aux;
    return pivote;
}

void qs_mtime(char *dirname,struct dirent **namelist, int izq, int der)
{
     int pivote;
     if(izq < der){
	DEBUG("APACHELOG QuickSort for I:%d D:%d",izq,der);
        pivote=qs_mtime_pivot(dirname,namelist, izq, der);
        qs_mtime(dirname,namelist, izq, pivote-1);
        qs_mtime(dirname,namelist, pivote+1, der);
     }
}


int file_select(const struct dirent *entry)
{
   // if a different than 0 it is ok 
   if(fnmatch(apachelog_name_filter,entry->d_name,0)) return 0;
   return 1;
}

/*
* return a filename in a new allocated array or null if any filename found
*/

char *get_last_apache_modified_file_from_pattern(char *filename_pattern)
{
	struct dirent **namelist;
	int n,i;	
        char *dir_name;
        char *filename;
 	 //get dirname
	char *dirc=strdup(filename_pattern);
	char *basec=strdup(filename_pattern);
	dir_name=dirname(dirc);
	
	DEBUG("APACHELOG SCANDIR %s ",dir_name);
	apachelog_name_filter=basename(basec);
	DEBUG("APACHELOG SCANDIR set filter %s results",apachelog_name_filter);	
	n = scandir(dir_name, &namelist, file_select, 0);
	//any file with this pattern exist return NULL
        if(n) { 
		qs_mtime(dir_name,namelist,0, n-1);	
		DEBUG("APACHELOG SCANDIR found %d results",n);
		filename=str_concat(dir_name,"/",namelist[n-1]->d_name);
		DEBUG("APACHELOG Selected File found %s results",filename);
	} else {
		WARNING("APACHELOG no files found with pattern %s",filename_pattern);
		filename =NULL;
	}

	if(n<0) {
		perror("scandir");
	} else  {
		for (i = 0; i < n; i++) {
			DEBUG("APACHELOG SCANDIR found file :%s",namelist[i]->d_name);
			free(namelist[i]);
			
		}
		free(namelist);
	}
	sfree(dirc);
	sfree(basec);
	apachelog_name_filter=NULL;
	return filename;
}


_Bool apachelog_test_rotation(cu_apachelog_t *tm)
{
  if (!tm->use_rotatelogs) return 0;
  char *filename_new=get_last_apache_modified_file_from_pattern(tm->filename_pattern);
  // no new file exist and perhaps deleted last
  if ( filename_new == NULL ) {
	WARNING("APACHELOG: OPS!! no file seems to be matching to  %s ",tm->filename_pattern);
	if (tm->tail != NULL) cu_tail_destroy (tm->tail);
	tm->tail=NULL;
	if (tm->filename != NULL) free(tm->filename);
	tm->filename = NULL;
	return 0;
   }
  //firts file found
  if( tm->filename == NULL ) {
	INFO("APACHELOG: DETECTED A NEW FILE  %s ",filename_new);
	tm->filename=filename_new;
	tm->tail = cu_tail_create (tm->filename);
	
	return 1;
  }
  //file rotated
  if(strcmp(tm->filename,filename_new)) {
	INFO("APACHELOG: DETECTED LOG ROTATION (old) %s | (new) %s ",tm->filename,filename_new);
  	if (tm->tail != NULL) cu_tail_destroy (tm->tail);
	if (tm->filename != NULL) free(tm->filename);
	tm->filename=filename_new;
	tm->tail = cu_tail_create (tm->filename);
	//on create tail object don't open file until first read and on new file it seeks to the end.. 
	// this behaviour is causing to loss data so we need to force reopen and seek to the init.
	cu_tail_disable_seek_end_on_newfile(tm->tail);

	return 1;
  }
  return 0;
}


cu_apachelog_t *apachelog_create (const char *filename)
{
  cu_apachelog_t *obj;

  DEBUG("tail match creating for file %s",filename);
  obj = (cu_apachelog_t *) malloc (sizeof (cu_apachelog_t));
  if (obj == NULL)
    return (NULL);
  memset (obj, '\0', sizeof (cu_apachelog_t));
 //config
  obj->use_rotatelogs=0;
  
 //metrics

  obj->httpxxx_suffix[0]=strdup("global");
  obj->httpxxx_suffix[1]=strdup("1XX");
  obj->httpxxx_suffix[2]=strdup("2XX");
  obj->httpxxx_suffix[3]=strdup("3XX");
  obj->httpxxx_suffix[4]=strdup("4XX");
  obj->httpxxx_suffix[5]=strdup("5XX");



  obj->filename_pattern=strdup(filename);
  obj->logline_ptr=(char **) calloc(APACHELOG_MAX_FIELDS,sizeof(char*)); 
  obj->stat_mode=0;
  obj->response_time_position=0;
  obj->http_code_position=9;



  return (obj);
} /* cu_apachelog_t *apachelog_create */

_Bool apachelog_create_tail(cu_apachelog_t *tm)
{
  //filename_base
  if(!tm->use_rotatelogs) { 
	tm->filename=strdup(tm->filename_pattern);
  } else {
	tm->filename=get_last_apache_modified_file_from_pattern(tm->filename_pattern);
  }
  // can be null on rotalelog when no files match 
  if(tm->filename!= NULL) tm->tail = cu_tail_create (tm->filename);
  if (tm->tail == NULL) return 0;
  return 1;
}



void apachelog_destroy (cu_apachelog_t *obj)
{
 // size_t i;
 int i;

  if (obj == NULL)
    return;

   for(i=0;i<6;i++) free(obj->httpxxx_suffix[i]);


  sfree(obj->filename);
  sfree(obj->filename_pattern);
  if (obj->tail != NULL)
  {
    cu_tail_destroy (obj->tail);
    obj->tail = NULL;
  }
  sfree(obj->plugin_name);
  sfree(obj->plugin_instance);
  sfree(obj->rename_plugin_as);
  sfree(obj->logline_ptr);
  sfree(obj);
} /* void apachelog_destroy */


static int capachelog_config_add_file (oconfig_item_t *ci)
{
  cu_apachelog_t *tm;
  int status;
  int i;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("apachelog plugin: `File' needs exactly one string argument.");
    return (-1);
  }

  DEBUG("APACHELOG: capachelog_config_add_file adding %s",ci->values[0].value.string);
  tm = apachelog_create (ci->values[0].value.string);
  if (tm == NULL)
  {
    ERROR ("apachelog plugin: create (%s) failed.",
	ci->values[0].value.string);
    return (-1);
  }

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;
    if (strcasecmp ("Instance", option->key) == 0)
      status = cf_util_get_string (option, &tm->plugin_instance);
    else if (strcasecmp ("RenamePluginAs", option->key) == 0)
       status = cf_util_get_string (option, &tm->rename_plugin_as);
    else if (strcasecmp ("UseApacheRotatedLogs", option->key) == 0)
       status = cf_util_get_boolean (option, &tm->use_rotatelogs);
    else if (strcasecmp ("ExtendedMetrics", option->key) == 0)
       status = cf_util_get_boolean (option, &tm->stat_mode);
    else if (strcasecmp ("SetRespTimeField", option->key) == 0)
	status = cf_util_get_int (option, &tm->response_time_position);
    else if (strcasecmp ("SetHTTPCodeField", option->key) == 0)
	status = cf_util_get_int (option, &tm->http_code_position);
    else
    {
      WARNING ("apachelog plugin: Option `%s' not allowed here.", option->key);
      status = -1;
    }
   
    /*tm->plugin_instance=plugin_instance;
    tm->rename_plugin_as=rename_plugin_as;*/


    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

   apachelog_create_tail(tm);

    cu_apachelog_t **temp;

    temp = (cu_apachelog_t **) realloc (apachelog_list,
	sizeof (cu_apachelog_t *) * (apachelog_list_num + 1));
    if (temp == NULL)
    {
      ERROR ("apachelog plugin: realloc failed.");
      apachelog_destroy (tm);
      return (-1);
    }

    apachelog_list = temp;
    apachelog_list[apachelog_list_num] = tm;
    apachelog_list_num++;

  return (0);
} /* int capachelog_config_add_file */

static int capachelog_config (oconfig_item_t *ci)
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("File", option->key) == 0)
      capachelog_config_add_file (option);
    else
    {
      WARNING ("apachelog plugin: Option `%s' not allowed here.", option->key);
    }
  } /* for (i = 0; i < ci->children_num; i++) */

  return (0);
} /* int capachelog_config */

static int capachelog_init (void)
{
  if (apachelog_list_num == 0)
  {
    WARNING ("apachelog plugin: File list is empty. Returning an error.");
    return (-1);
  }

  return (0);
} /* int capachelog_init */



int  apachelog_split_line(char *buf,char **array)
{
  char **ptrptr;
  int i;
  char *p;
  i = 0;
  ptrptr=calloc(APACHELOG_MAX_FIELDS,sizeof(char*));
  p = strtok_r (buf," ",ptrptr);  
  while (p != NULL)
  {
    array[i++] = p;
    p = strtok_r (NULL," ",ptrptr);
  }
  free(ptrptr);
  return i;
}

inline unsigned int max(unsigned int a, unsigned int b) {
  return a > b ? a : b;
}

inline unsigned int min(unsigned int a, unsigned int b) {
  return a < b ? a : b;
}


static int apachelog_basic_split_callback (void *data, char *buf, int __attribute__((unused)) buflen)
{
  cu_apachelog_t *obj = (cu_apachelog_t *) data;
  int n;
  int response_time_microsecs;

  
  n=apachelog_split_line(buf,obj->logline_ptr);

  if(obj->response_time_position == 0) {
	//last position
  	response_time_microsecs=atoi(obj->logline_ptr[n-1]);
  }else { 
	//n postition 1..N (less 1 in array)
	response_time_microsecs=atoi(obj->logline_ptr[obj->response_time_position-1]);
  }


  response_time_microsecs=atoi(obj->logline_ptr[n-1]);

  //DEBUG("APACHELOG : response time detected  :  %d",response_time_microsecs);

/*
  DEBUG("APACHELOG: NUMBER OF FIELDS %d ",n);
  for (i=0;i<n; i++) {
    DEBUG("APACHELOG: split 2 : %d : %s", i,line_ptr[i]);
  }
  free(line_ptr);*/


   /* if (response_time_microsecs==0) 
 	DEBUG("APACHELOG : WARNING 0 TIME response time detected  : (int) %d |(ascii) %s | (http_request) %s",response_time_microsecs,response_time_string,buf);*/

  obj->httpxxx_hits_count[0]++;
  obj->httpxxx_response_time_sum[0]+=response_time_microsecs;
  obj->httpxxx_response_time_max[0]=max(obj->httpxxx_response_time_max[0],response_time_microsecs);
  obj->httpxxx_response_time_min[0]=min(obj->httpxxx_response_time_min[0],response_time_microsecs);

 
  return (0);

}  // int apachelog_basic_split_callback 


static int apachelog_extended_split_callback (void *data, char *buf, int __attribute__((unused)) buflen)
{
  cu_apachelog_t *obj = (cu_apachelog_t *) data;
  int n;
  int response_time_microsecs;
  int stat_code;
  int stat_pos;

  
  n=apachelog_split_line(buf,obj->logline_ptr);

  if(obj->response_time_position == 0) {
	//last position
  	response_time_microsecs=atoi(obj->logline_ptr[n-1]);
  }else {
	//n postition 1..N (less 1 in array)
	response_time_microsecs=atoi(obj->logline_ptr[obj->response_time_position-1]);
  }

  //DEBUG("APACHELOG : response time detected  :  %d",response_time_microsecs);


  //DEBUG("APACHELOG: NUMBER OF FIELDS %d ",n);
  //for (i=0;i<n; i++) {
  //  DEBUG("APACHELOG: split 2 : %d : %s", i,line_ptr[i]);
  //}
  //free(line_ptr);


    //if (response_time_microsecs==0) 
 //	DEBUG("APACHELOG : WARNING 0 TIME response time detected  : (int) %d |(ascii) %s | (http_request) %s",response_time_microsecs,response_time_string,buf);

 //TOTAL

  obj->httpxxx_hits_count[0]++;
  obj->httpxxx_response_time_sum[0]+=response_time_microsecs;
  obj->httpxxx_response_time_max[0]=max(obj->httpxxx_response_time_max[0],response_time_microsecs);
  obj->httpxxx_response_time_min[0]=min(obj->httpxxx_response_time_min[0],response_time_microsecs);

  //http_code

  stat_code=atoi(obj->logline_ptr[obj->http_code_position-1]);
  stat_pos=(int)stat_code/100;

  if(stat_pos<0 || stat_pos > 6 ) {
	WARNING("ERROR in HTTP CODE detected %d from %s",stat_code,obj->logline_ptr[obj->http_code_position-1]);
	return 1;	
  }
	
 
  //PER HTTP CODE

  obj->httpxxx_hits_count[stat_pos]++;
  obj->httpxxx_response_time_sum[stat_pos]+=response_time_microsecs;
  obj->httpxxx_response_time_max[stat_pos]=max(obj->httpxxx_response_time_max[stat_pos],response_time_microsecs);
  obj->httpxxx_response_time_min[stat_pos]=min(obj->httpxxx_response_time_min[stat_pos],response_time_microsecs);
  
  return (0);

}  // int apachelog_basix_split_callback 




inline unsigned int elapsed_useconds(struct timeval *t1,struct timeval *t2)
{
 unsigned int  usec_diff = (t2->tv_usec + 1000000 * t2->tv_sec) - (t1->tv_usec + 1000000 * t1->tv_sec);
 return usec_diff;
}

inline double elapsed_seconds(struct timeval *t1,struct timeval *t2)
{
 unsigned int  usec_diff = (t2->tv_usec + 1000000 * t2->tv_sec) - (t1->tv_usec + 1000000 * t1->tv_sec);
 return usec_diff/1000000.0;
}

#define MAX_BUFFER_SIZE 16384
/*
Be sure the MAX_BUFFER_SIZE < MAX line Size for the hole log file .
You can stimate the grestest line size by computing cat *log | wc -L 
We have cases of Apache log files with 8342 bytes 
*/


int apachelog_read (cu_apachelog_t *obj)
{
  char buffer[MAX_BUFFER_SIZE];
  int status=0,i,N;
  struct timeval tv_start, tv_end;
  
   
  DEBUG("APACHELOG: apachelog_read for file %s",(char*) obj->filename);

    // reset struct

  for (i=0;i<6;i++){
  	obj->httpxxx_hits_count[i]=0;
  	obj->httpxxx_response_time_max[i]=0;
  	obj->httpxxx_response_time_sum[i]=0;
  	obj->httpxxx_response_time_min[i]=20000000;
  }


  gettimeofday(&tv_start,NULL);
  //check if file exist first
  if(obj->filename!=NULL && obj->tail!= NULL ) {

	if(obj->stat_mode) {
		//extended
		status = cu_tail_read (obj->tail, buffer, sizeof (buffer), apachelog_extended_split_callback,(void *) obj);
	} else {
		//basic
		status = cu_tail_read (obj->tail, buffer, sizeof (buffer), apachelog_basic_split_callback,(void *) obj);
	}
  }
  DEBUG("APACHELOG: collected values for file %s are: REQUEST: %d | TimeSUM: %d | TimeMax: %d | TimeMin: %d | tail read status %d ",
	obj->filename_pattern,
	obj->httpxxx_hits_count[0],
	obj->httpxxx_response_time_sum[0],
	obj->httpxxx_response_time_max[0],
	obj->httpxxx_response_time_min[0],
	status);


  if(apachelog_test_rotation(obj)) {
	if(obj->stat_mode) {
		//extended
		status = cu_tail_read (obj->tail, buffer, sizeof (buffer), apachelog_extended_split_callback,(void *) obj);
	} else {
		//basic
		status = cu_tail_read (obj->tail, buffer, sizeof (buffer), apachelog_basic_split_callback,(void *) obj);
	}
	DEBUG("APACHELOG AFTER ROTATION: collected values for file %s are: REQUEST: %d | TimeSUM: %d | TimeMax: %d | TimeMin: %d | tail read status %d",
	obj->filename_pattern,
	obj->httpxxx_hits_count[0],
	obj->httpxxx_response_time_sum[0],
	obj->httpxxx_response_time_max[0],
	obj->httpxxx_response_time_min[0],
	status);

  }

  gettimeofday(&tv_end,NULL);
  DEBUG("APACHELOG log read in %g seconds for file %s",elapsed_seconds(&tv_start,&tv_end),obj->filename_pattern);


  //SUBMIT

   if(obj->stat_mode) 	N=6; //Extended 
   else 		N=1; //Simple

  for(i=0; i<N; i++) {

  	if(obj->httpxxx_hits_count[i]) { 
  		apachelog_submit_http_perf (	obj->plugin_instance,
						obj->rename_plugin_as,
						obj->httpxxx_suffix[i],
						obj->httpxxx_hits_count[i],
						(gauge_t) obj->httpxxx_response_time_max[i], 
						(gauge_t)((double)obj->httpxxx_response_time_sum[i]/(double)obj->httpxxx_hits_count[i]),
						(gauge_t) obj->httpxxx_response_time_min[i]);
  	} else {
		apachelog_submit_http_perf( 	obj->plugin_instance,
						obj->rename_plugin_as,
						obj->httpxxx_suffix[i],
						obj->httpxxx_hits_count[i],
						NAN,NAN,NAN);
  	}
  }

  return (status);
} /* int apachelog_read */

static int capachelog_read (void)
{
  int success = 0;
  size_t i;

  DEBUG("APACHELOG: read %d ",(int)apachelog_list_num);
  for (i = 0; i < apachelog_list_num; i++)
  {
    int status;


    status = apachelog_read (apachelog_list[i]);
    if (status != 0)
    {
      ERROR ("apachelog plugin: read[%zu] failed.", i);
    }
    else
    {
      success++;
    }
  }

  if (success == 0)
    return (-1);
  return (0);
} /* int capachelog_read */

static int capachelog_shutdown (void)
{
  size_t i;

 DEBUG("APACHELOG: shutdown %d",(int)apachelog_list_num);

  for (i = 0; i < apachelog_list_num; i++)
  {
    apachelog_destroy (apachelog_list[i]);
    apachelog_list[i] = NULL;
  }
  sfree (apachelog_list);
  apachelog_list_num = 0;

  return (0);
} /* int capachelog_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("apachelog", capachelog_config);
  plugin_register_init ("apachelog", capachelog_init);
  plugin_register_read ("apachelog", capachelog_read);
  plugin_register_shutdown ("apachelog", capachelog_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 ts=8 : */
