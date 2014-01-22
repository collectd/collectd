/**
 * collectd - src/suricata.c 
 * 
 * Copyright (c) 2014 Go Daddy Operating Company, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy 
 * of this software and associated documentation files (the "Software"), to 
 * deal in the Software without restriction, including without limitation the 
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is 
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in 
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *   Domingo Kiser <domingo.kiser@gmail.com> <dkiser@godaddy.com>
 **/

#include <json/json.h>
#include <stdio.h>

/* Include what we need to deal with unix raw sockets. */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <errno.h>

#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/* A define to run this for debugging outside of collectd plugin infra. */
#ifndef SC_STATS_DEBUG_NO_COLLECTD
 #include "collectd.h"
 #include "common.h" /* auxiliary functions */
 #include "plugin.h" /* plugin_register_*, plugin_dispatch_values */
#endif

/* If we are building with < C99 c standard, older versions of the json-c library
 * will not be able to compile the json_object_object_foreach.  So lets fix this
 * for older versions of json-c here
 *
 * This fixes the following error:
 *    error: ‘for’ loop initial declaration used outside C99 mode
 *
 * This is fixed in json-c-0.11 and later.
 */
#if defined(__GNUC__) && !defined(__STRICT_ANSI__) && __STDC_VERSION__ < 199901L \
  && JSON_C_MAJOR_VERSION <= 0 && JSON_C_MINOR_VERSION <= 10 
  #ifdef json_object_object_foreach
    #undef json_object_object_foreach
    # define json_object_object_foreach(obj,key,val) \
        char *key;\
        struct json_object *val; \
        struct lh_entry *entry ## key; \
        struct lh_entry *entry_next ## key = NULL; \
        for(entry ## key = json_object_get_object(obj)->head; \
                (entry ## key ? ( \
                        key = (char*)entry ## key->k, \
                        val = (struct json_object*)entry ## key->v, \
                        entry_next ## key = entry ## key->next, \
                        entry ## key) : 0); \
                entry ## key = entry_next ## key)
  #endif
#endif

/* Default Suricata command socket. */
#define DEFAULT_SOCK "/var/run/suricata/suricata-command.socket"

/* Suricata command version. */
#define SURICATA_CMD_VERSION "0.1"

/* Suricata command protocol 'command' identifier. */
#define SURICATA_CMD_PROTO_CMD_ID "command"

/* Suricata command protocol 'arguments' identifier. */
#define SURICATA_CMD_PROTO_ARG_ID "arguments"

/* Suricata command json wrapper id's. */
#define SURICATA_CMD_PROTO_RES_ID "return"
#define SURICATA_CMD_PROTO_RES_MSG "message"

/* Suricata success/failure command id's. */
#define SURICATA_CMD_SUCCESS "OK"
#define SURICATA_CMD_FAILURE "NOK"

/* Read timeout waiting for Suricata to return data. */
#define READ_TIMEOUT_SECS (1)


/* Include sstrncpy function outside of collectd for use. */
#ifndef COLLECTD_H
char *sstrncpy (char *dest, const char *src, size_t n)
{
        strncpy (dest, src, n);
        dest[n - 1] = '\0';

        return (dest);
} /* char *sstrncpy */

#define STATIC_ARRAY_SIZE(a) (sizeof (a) / sizeof (*(a)))

#endif

/* Define some logging functions for both collectd and debug builds. */
#ifndef COLLECTD_H
# define log_debug(...) fprintf(stdout, "suricata: "__VA_ARGS__)
# define log_err(...) fprintf(stdout, "suricata: "__VA_ARGS__)
# define log_warn(...) fprintf(stdout, "suricata: "__VA_ARGS__)
#else
# define log_debug(...) DEBUG ("suricata: "__VA_ARGS__)
# define log_err(...) ERROR ("suricata: "__VA_ARGS__)
# define log_warn(...) WARNING ("suricata: "__VA_ARGS__)
#endif

/*
 * Private variables
 */
/* valid configuration file keys */
static const char *config_keys[] =
{
  "SocketFile"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

/* Instance name for collectd value_list_t. */
char* instance_name = "";

/* socket configuration */
static char *sock_file  = NULL;
static int sock_fd = 0;

/* Flag for plugin enabled/disabled. */
static int disabled = 0;

#ifdef COLLECTD_H
/* Function Declarations. */
void module_register (void);
static int sc_stats_init (void);
static int sc_stats_shutdown (void);
static int sc_stats_read (void);
static int sc_stats_config (const char *, const char *);
static void sc_stats_submit (const char *, const char *, gauge_t );
#endif

/* Helper function to concat some strings. */
char * concat_strings(const char * first, const char * second)
{
    /* find the size of the string to allocate */
    const size_t first_len = strlen(first), second_len = strlen(second);
    const size_t out_len = first_len + second_len + 1;

    /* allocate a pointer to the new string */
    char *out = malloc(out_len);
    if ( out == NULL)
    {
      log_err("concat_strings: failed to allocate memory!");
      return "";
    }

    /* concat both strings and return */
    memcpy(out, first, first_len);
    memcpy(out + first_len, second, second_len + 1);

    return out;
}

/* Buffer Related Definitions/Functions. */
#define SC_BUFF_T_SIZE (4096)

/* struct object for a buffer type. */
typedef struct
{
  ssize_t size;
  void    *mem;
 
} sc_stats_buffer_t;

/* Helper function to allocate a buffer. */
sc_stats_buffer_t* sc_stats_buffer_alloc()
{
  sc_stats_buffer_t *buffer = malloc(sizeof(sc_stats_buffer_t));
  if ( buffer == NULL)
  {
      log_err("sc_stats_buffer_alloc: failed to allocate memory!");
      return buffer;
  }
  buffer->size = SC_BUFF_T_SIZE;
  buffer->mem = malloc(buffer->size);
  memset(buffer->mem, 0, buffer->size);

  return buffer;
}

/* reset a buffer's memory. */
void sc_stats_buffer_reset(sc_stats_buffer_t* buffer)
{
  memset(buffer->mem, 0, buffer->size);
}

/* free a buffer's memory. */
void sc_stats_buffer_free(sc_stats_buffer_t* buffer)
{
  free(buffer->mem);
  free(buffer);
}

/* reallocate a buffer to a bigger size. */
void sc_stats_buffer_realloc(sc_stats_buffer_t* buffer)
{
  void* mem_ptr = buffer->mem;
  buffer->mem = realloc(buffer->mem, buffer->size+SC_BUFF_T_SIZE);
  if ( buffer->mem == NULL )
  {
    log_err("sc_stats_buffer_realloc: could not realloc any more memory!\n");
    buffer->mem = mem_ptr;
  }
  else
  {
    buffer->size += SC_BUFF_T_SIZE;
  }
}

/* Create hello json to initiate suricata-cmd socket protocol. */
json_object* suricata_stats_cmd_hello()
{
  json_object * jobj = json_object_new_object();
  json_object * jstring = json_object_new_string(SURICATA_CMD_VERSION);
  json_object_object_add (jobj, "version",  jstring);

  return(jobj);
}

/* Create dump-counters json protocol command message. */
json_object* suricata_stats_cmd_dump_counters()
{
  json_object * jobj = json_object_new_object();
  json_object * jstring = json_object_new_string("dump-counters");
  json_object_object_add (jobj, SURICATA_CMD_PROTO_CMD_ID, jstring);

  return(jobj);
}

/* Connect to the unix raw socket. */
int suricata_stats_connect(char* sock_path)
{
  struct sockaddr_un remote;
  int sock;

  if ( sock_path == NULL)
    return (-1);

  /* Set socket type to unix raw socket. */
  remote.sun_family = AF_UNIX;

  /* Copy over the path into the sockaddr_unix_raw type*/
  sstrncpy (remote.sun_path, sock_path, sizeof (remote.sun_path));

  sock = socket (AF_UNIX, SOCK_STREAM, 0);
  if (sock < 0 )
  {
    log_err("suricata_stats_connect: could not allocate socket descriptor!\n");
    return(-1);
  }

  int len = strlen(remote.sun_path) + sizeof(remote.sun_family);
  if ( (connect (sock, (struct sockaddr *)&remote, len)) < 0)
  {
    log_err("connect failed: %s: %s\n", strerror(errno), sock_path);
    close(sock);
    return (-1);
  }
  else
  {
    return sock;
  }

  return(-1);
}

/* Send JSON Objects to the suricata command socket. */
int suricata_stats_sock_send(int sock, json_object* obj)
{
  char * buffer = (char*)json_object_to_json_string(obj);
  ssize_t buff_size = strlen(buffer);
  ssize_t sent_ret;

  /* Loop while we have data to send. */
  while (buff_size > 0)
  {
    /* If there is an error sending data. */
    if ( (sent_ret = send(sock, (const void *)buffer, buff_size, 0)) < 0)
    {
       log_err("suricata_stats_sock_send: could not perform socket send: %s\n",
        strerror(errno));
        //close (sock);
        return (-1);

    }
    /* Else, we sent some data, update buff_size counter and continue. */
    else
    {
      buff_size -= sent_ret;
    }
  }


  if (buff_size == 0 )
  {
    return(0);
  }

  return(-1);
}


/* Read json object from suricata command socket. */
int suricata_stats_sock_read(int sock, sc_stats_buffer_t* buffer)
{
  ssize_t ret = 0;
  ssize_t total_returned = 0;
  int sel = 0;
  fd_set active_fd_set;
  struct timeval timeout;


  int flags = fcntl(sock, F_GETFL, 0);
  fcntl(sock, F_SETFL, flags | O_NONBLOCK);

  FD_ZERO(&active_fd_set);
  FD_SET(sock, &active_fd_set);
 
  timeout.tv_sec = READ_TIMEOUT_SECS;
  timeout.tv_usec = 0;

  do
  {

    /* Select the sock with a timeout to see if suricata sent us anything. */
    sel=select(FD_SETSIZE, &active_fd_set, NULL, NULL, &timeout);
   
    /* If select error. */
    if ( sel < 0)
    {
        log_err("suricata_stats_sock_read: select failure: %s\n", strerror(errno));
        close(sock);
        return (-1);

    }
    /* Else we timeout out waiting for data AND we aleady have received data. */
    else if(sel == 0 && total_returned > 0)
    {
      /* Break out of the loop, suricata must be done sending us data. */
      break;
    }
   
    /* Receive data from suricata. */
    ret = recv (sock, (void *)(buffer->mem+total_returned), buffer->size-total_returned, 0);
    if (ret < 0) {
       log_err("suricata_stats_sock_read: recv failure: %s\n", strerror(errno));
        close(sock);
        break;
    }
    /* Else if connection is closed. */
    else if ( ret == 0)
    {
        log_warn("suricata_stats_sock_read: recv: socket was closed.\n");
        break;

    }
    total_returned += ret;


    /* Allocate more memory if we reached our max size. */
    if (total_returned == buffer->size)
    {
      sc_stats_buffer_realloc(buffer);
      continue;
    }
    /* Else, break out of while loop, no more to receive. */
    else
    {
        break;
    }

  } while (ret > 0);

  /* Set socket back to blocking. */
  flags ^= O_NONBLOCK;
  fcntl(sock, F_SETFL, flags | O_NONBLOCK);
  
  return (total_returned);
}

/* Perform a command/response dance with Suricata. */
json_object* suricata_stats_cmd_proto(int sock, json_object* cmd)
{
  sc_stats_buffer_t *buffer = sc_stats_buffer_alloc();
  enum json_tokener_error jerr;
  json_tokener* tok = json_tokener_new();
  json_object* jobj = NULL;
  int ret = 0;


  /* Send command to suricata. */
  if ( suricata_stats_sock_send(sock, cmd) < 0 )
  {
    log_err("suricata_stats_cmd_proto: failed to send command message: %s\n", 
      json_object_to_json_string(cmd));
    sc_stats_buffer_free(buffer);
    json_tokener_free(tok);
    return (jobj);
  }

  /* Do while tokener says to continue. */
  do {
        /* Read results back from suricata. */
        if ( (ret = suricata_stats_sock_read(sock, buffer)) < 0)
        {
          log_err("suricata_stats_cmd_proto: could not read command response.\n");
          sc_stats_buffer_free(buffer);
          json_tokener_free(tok);
          return (NULL);
        }
        //printf("read back = %d\n",read);


        /* Call tokener with buffer pointer and how many bytes we read. */
        jobj = json_tokener_parse_ex(tok, buffer->mem, ret);

  } while ((jerr = json_tokener_get_error(tok)) == json_tokener_continue);
 
  /* If tokener error code is not success. */
  if (jerr != json_tokener_success)
  {
        log_err("suricata_stats_cmd_proto: %s\n",
          json_tokener_error_desc(jerr));
        sc_stats_buffer_free(buffer);
        json_tokener_free(tok);
        return (NULL);
  }

  /* If we didn't receive success from Suricata. */
  if ( strcmp(json_object_get_string(json_object_object_get(jobj, SURICATA_CMD_PROTO_RES_ID)),
       SURICATA_CMD_SUCCESS) != 0 )
  {
        log_err("suricata_stats_cmd_proto: suricata command proto failure: %s: %s\n",
         SURICATA_CMD_FAILURE,
         json_object_to_json_string(json_object_object_get(jobj, SURICATA_CMD_PROTO_RES_MSG)));
        sc_stats_buffer_free(buffer);
        json_tokener_free(tok);
        return (NULL);
  }


  sc_stats_buffer_free(buffer);
  json_tokener_free(tok);
  return(jobj);
}

/* Dump stat value using a collect name scheme, like "RxPcapeth01.decoder.max_pkt_size" */
void sc_stats_json_value_print(json_object *jobj, char* collectd_str){
  enum json_type type;

#ifndef COLLECTD_H
  log_debug("identifier: %s\n", collectd_str);
#endif
  type = json_object_get_type(jobj); /*Getting the type of the json object*/
  switch (type) {
    case json_type_boolean: 

#ifndef COLLECTD_H
      log_debug("value: %s\n", json_object_get_boolean(jobj)? "true": "false");
#else
      sc_stats_submit(collectd_str, instance_name, json_object_get_boolean(jobj));
#endif
      break;
    case json_type_double: 
#ifndef COLLECTD_H
      log_debug("          value: %lf\n", json_object_get_double(jobj));
#else
      sc_stats_submit(collectd_str, instance_name, json_object_get_double(jobj));
#endif
      break;
    case json_type_int: 
#ifndef COLLECTD_H
      log_debug("          value: %d\n", json_object_get_int(jobj));
#else      
      sc_stats_submit(collectd_str, instance_name, json_object_get_int(jobj));
#endif     
      break;
    case json_type_string:
    case json_type_null:
    case json_type_object:
    case json_type_array:
      log_warn("sc_stats_json_value_print: received json type we never should have!\n");
      ;
  }

}

/* Parse an array json object. */
void sc_stats_json_parse_array( json_object *jobj, char *key) {
  void sc_stats_json_parse(json_object * jobj, char* identifier); /*Forward Declaration*/
  enum json_type type;

  json_object *jarray = jobj; /*Simply get the array*/
  if(key) {
    jarray = json_object_object_get(jobj, key); /*Getting the array if it is a key value pair*/
  }

  int arraylen = json_object_array_length(jarray); /*Getting the length of the array*/
  int i;
  json_object * jvalue;

  for (i=0; i< arraylen; i++){
    jvalue = json_object_array_get_idx(jarray, i); /*Getting the array element at position i*/
    type = json_object_get_type(jvalue);
    if (type == json_type_array) {
      sc_stats_json_parse_array(jvalue, NULL);
    }
    else if (type != json_type_object) {
      sc_stats_json_value_print(jvalue, key);
    }
    else {
      sc_stats_json_parse(jvalue, key);
    }
  }
}

/* Parsing the json object */
void sc_stats_json_parse(json_object * jobj, char* identifier) 
{
  enum json_type type;
  char *id_str = NULL;
  //char *id_str_dot = NULL;

  json_object_object_foreach(jobj, key, val) { /*Passing through every array element*/

    type = json_object_get_type(val);
    switch (type) {
      case json_type_boolean:
      case json_type_double:
      case json_type_int:
      case json_type_string: 
        sc_stats_json_value_print(val, key);
        break;
      case json_type_object:
        /* Create a concatencated string for the type. */ 
        id_str = concat_strings(identifier, key);
        
        /* Save off instance name we will use when printing/submiting. */
        instance_name = id_str;
        sc_stats_json_parse(json_object_object_get(jobj, key), id_str);
        break;
      case json_type_array: 
          sc_stats_json_parse_array(jobj, key);
          break;
      case json_type_null:
        break;
    }
    free(id_str);
  }
}

#ifdef COLLECTD_H

/* Config function for collectd. */
static int sc_stats_config (const char *key, const char *value)
{
  if (0 == strcasecmp (key, "SocketFile")) {
    sock_file = sstrdup (value);
  }
  else
  {
    log_warn("sc_stats_config: 'SocketFile' parameter not given, using default: %s\n", DEFAULT_SOCK);
    sock_file = DEFAULT_SOCK;
  }
  return 0;
}

/* Init function for collectd. */
static int sc_stats_init (void)
{
  if (sock_file == NULL)
  {
    log_warn("sc_stats_init: 'SocketFile' parameter not given, using default: %s\n", DEFAULT_SOCK);
    sock_file = DEFAULT_SOCK;
  }
  
  if ( (sock_fd = suricata_stats_connect(sock_file)) <  0)  {
    disabled = 1;
    log_err ("sc_stats_init: connect failure: %s", strerror(errno));
    return (0);
  }

  /* Create json object for hello message. */
  json_object *cmd =  suricata_stats_cmd_hello();

  /* Perform protocol handshake. */
  json_object* jobj = suricata_stats_cmd_proto(sock_fd, cmd);
  
  /* If handshake failed. */
  if ( jobj == NULL)
  {
    disabled = 1;
    log_err("sc_stats_init: failed to say hello to suricata.\n");
    return (0);
  }
  /* Else we have a good comm channel with suricata's socket. */
  else
  {
    /* Enable plugin. */
    disabled = 0;
  }

  /* Free json objects. */
  json_object_put(jobj);
  json_object_put(cmd);

  return (0);
} /* int sc_stats_init */

/* Function to submit values to collectd. */
static void sc_stats_submit (const char *type, const char *type_instance, gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "suricata", sizeof (vl.plugin));
  /* Concat type name with suricata_ to match what we have in types.db. */
  sstrncpy (vl.type, concat_strings("suricata_", type), sizeof (vl.type));
  sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* void sc_stats_submit */

/* Function for reads for collectd. */
static int sc_stats_read (void)
{
  json_object *cmd_dump = NULL;
  json_object* jobj_ret = NULL; 

  /* If plugin isn't enabled yet. */
  if (disabled)
  {
    /* Try to init again. */
    sc_stats_init();

    /* If we aren't enabled (connected). Error out and wait for next attempt. */
    if (disabled)
      return (-1);
  }

  /* Creat json dump-counters command. */
  cmd_dump = suricata_stats_cmd_dump_counters();


  /* Send command and get results. */
  if ( (jobj_ret = suricata_stats_cmd_proto(sock_fd, cmd_dump)) == NULL)
  {
    log_err("sc_stats_read: failure command protocol.\n");
    json_object_put(cmd_dump);
    return (-1);
  }

  /* Parse out response and dump the stats. */
  sc_stats_json_parse( json_object_object_get(jobj_ret,"message"), "" );

  json_object_put(cmd_dump);
  json_object_put(jobj_ret);

  return (0);
} /* int sc_stats_read */

/* shutdown function for collectd. */
static int sc_stats_shutdown (void)
{
   close(sock_fd);

  return (0);
} /* static int sc_stats_shutdown (void) */

/* registration method for collectd plugin infrastructure. */
void module_register (void)
{
  plugin_register_config ("suricata", sc_stats_config, config_keys, config_keys_num);
  plugin_register_init ("suricata", sc_stats_init);
  plugin_register_read ("suricata", sc_stats_read);
  plugin_register_shutdown ("suricata", sc_stats_shutdown);
} /* void module_register */

#else

/**
 * Main method when compiling/running this outside of collectd. 
 *
 *  gcc -o suricata suricata.c -ljson -DSC_STATS_DEBUG_NO_COLLECTD
 *
 **/
int main()
{
  int ret;
  int sock;
  
  /* Allocate some buffer memory. */
  sc_stats_buffer_t *buffer = sc_stats_buffer_alloc();

  /* Connect to suricata command socket. */
  sock = suricata_stats_connect(DEFAULT_SOCK);

  /* Allocate hello and dump-counters json objects.. */
  json_object *cmd =  suricata_stats_cmd_hello();
  json_object *cmd_dump = suricata_stats_cmd_dump_counters();


  /* Send hello message and dump it. */
  json_object* jobj = suricata_stats_cmd_proto(sock, cmd);
  printf("%s\n", json_object_to_json_string(jobj));
  json_object_put(jobj);


  /* Send dump-counters and dump it. */
  jobj = suricata_stats_cmd_proto(sock, cmd_dump);
  printf("%s\n", json_object_to_json_string(jobj));
  sc_stats_json_parse( json_object_object_get(jobj,"message"), "" );


  json_object_put(cmd);
  json_object_put(cmd_dump);
  json_object_put(jobj);
  sc_stats_buffer_free(buffer);
  close(sock);

}
#endif

