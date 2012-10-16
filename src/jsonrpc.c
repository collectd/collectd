/**
 * collectd - src/jsonrpc.c
 * Copyright (C) 2012 Yves Mettier, Cyril Feraudet
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
 * Author:
 *   Yves Mettier <ymettier at free fr>
 *   Cyril Feraudet <cyril at feraudet com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_cache.h"
#include "jsonrpc.h"

#include <microhttpd.h>

#include <json/json.h>
#include <pthread.h>

#ifdef JSONRPC_USE_BASE
#include "jsonrpc_cb_base.h"
#endif
#ifdef JSONRPC_USE_PERFWATCHER
#include "jsonrpc_cb_perfwatcher.h"
#endif

#define OUTPUT_PREFIX_JSONRPC "JSONRPC plugin : "

#define POSTBUFFERSIZE  512

#define GET             0
#define POST            1

#define MIMETYPE_TEXTHTML "text/html"
#define MIMETYPE_JSONRPC "application/json-rpc"

#define PREPARE_ERROR_PAGE(code, string, mimetype) do { \
		con_info->answercode = (code);       \
		con_info->answerstring = NULL;   \
		con_info->errorpage = (string);   \
		con_info->answer_mimetype = (mimetype);     \
		} while(0)

typedef enum {
	JRE_PLAIN,
	JRE_WWW_FORM_URLENCODED
} jsonrequest_encoding_e;

typedef enum {
	CLOSE_CONNECTION_NO,
	CLOSE_CONNECTION_YES
} close_connection_e;

typedef enum {
	JSONRPC_REQUEST_SUCCEEDED,
	JSONRPC_REQUEST_FAILED
} jsonrpc_request_result_e;

/* Folks without pthread will need to disable this plugin. */

static pthread_mutex_t local_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t update_counters = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t nb_clients_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t nb_new_connections_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t nb_jsonrpc_request_success_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t nb_jsonrpc_request_failed_lock = PTHREAD_MUTEX_INITIALIZER;

#define lock_and_increment(lock,var, n) do { \
			pthread_mutex_lock(&(lock)); \
			(var)+=(n);\
			pthread_mutex_unlock(&(lock)); \
		} while(0)

/*
 * Private variables
 */

typedef struct jsonrpc_method_cb_definition_s {
	const char method[128];
	int (*cb) (struct json_object *, struct json_object *, const char **);
} jsonrpc_method_cb_definition_t;

static jsonrpc_method_cb_definition_t jsonrpc_methods_table [] = 
	{
#ifdef JSONRPC_USE_BASE
		JSONRPC_CB_TABLE_BASE
#endif
#ifdef JSONRPC_USE_PERFWATCHER
		JSONRPC_CB_TABLE_PERFWATCHER
#endif
		{ "", NULL }
	};

static struct MHD_Daemon * jsonrpc_daemon = NULL;
static unsigned int nb_clients = 0;
static unsigned int nb_new_connections = 0;
static unsigned int nb_jsonrpc_request_failed = 0;
static unsigned int nb_jsonrpc_request_success = 0;

const char *busypage =
  "{ \"jsonrpc\": \"2.0\", \"error\": {\"code\": -32400, \"message\": \"Too many connections\"}, \"id\": null}";

/*
const char *completepage =
  "<html><body>Chezmoicamarche</body></html>";
*/

const char *completepage =
  "{\"jsonrpc\": \"2.0\", \"result\": 7, \"error\": null, \"id\": 0}\n";

const char *errorpage =
  "<html><body><h1>Some error occured</h1></body></html>";
const char *parseerrorpage =
  "<html><body><h1>Parse error</h1></body></html>";

const char *servererrorpage =
  "<html><body>An internal server error has occured.</body></html>";

const char *jsonerrorstringformat =
	"{\"jsonrpc\": \"2.0\", \"error\": {\"code\": %d, \"message\": \"%s\"}, \"id\": %d}";

#define JSONRPC_ERROR_32600 "Invalid Request."
#define JSONRPC_ERROR_32601 "Method not found."
#define JSONRPC_ERROR_32602 "Invalid params."
#define JSONRPC_ERROR_32603 "Internal error."

typedef struct connection_info_struct_s
{
	int connectiontype;
	struct MHD_PostProcessor *postprocessor;
	char *jsonrequest;
	size_t jsonrequest_size;
	jsonrequest_encoding_e jsonrequest_encoding;
	char *answerstring;
	const char *errorpage;
	int answercode;
	const char *answer_mimetype;
} connection_info_struct_t;



/* valid configuration file keys */
static const char *config_keys[] =
{
	"Port",
	"MaxClients",
	"JsonrpcCacheExpirationTime"

};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int httpd_server_port=-1;
static int max_clients = 16;

static char **local_cache_names = NULL;
static cdtime_t *local_cache_times = NULL;
static size_t local_cache_number = 0;
static time_t local_cache_update_time = 0;
#define jsonrpc_cache_expiration_time_default 60
static time_t jsonrpc_cache_expiration_time = jsonrpc_cache_expiration_time_default;

/*
 * Functions
 */

int jsonrpc_local_uc_get_names(char ***ret_names, cdtime_t **ret_times, size_t *ret_number) {
	static int local_cache_reader = 0;
	static short local_cache_writer = 0;
	short update_needed = 0;
	short read_is_possible = 0;
	char **local_names;
	cdtime_t *local_times;
	time_t now;
	int i;

	/* Check if update is needed.
	 * If yes, and if nobody updated the local_cache_writer lock,
	 * prepare to update.
	 */
	pthread_mutex_lock (&local_cache_lock);
	update_needed = 0;
	if(local_cache_writer <= 0) {
		now = time(NULL);
		if(local_cache_update_time + jsonrpc_cache_expiration_time < now) {
			update_needed = 1;      /* local variable : we got the lock */
			local_cache_writer = 1; /* shared variable : someone got the lock */
		}
	}
	pthread_mutex_unlock (&local_cache_lock);

/* Check if update is needed. If we have the lock, no need to do it with the
 * mutex locked.
 */
	if(update_needed) {
		while(local_cache_reader > 0) sleep(1); /* First : wait until there is no reader */

		for(i=0; i<local_cache_number; i++) free(local_cache_names[i]);
		free(local_cache_names);
		free(local_cache_times);
		local_cache_number=0;
		if(0 != uc_get_names(&local_cache_names,&local_cache_times,&local_cache_number)) {
			local_cache_names=NULL;
			local_cache_times=NULL;
			local_cache_number = 0;
			local_cache_update_time = 0;
			local_cache_writer = 0;
			return(-1);
		}
		local_cache_update_time = time(NULL);
		local_cache_writer = 0;
	}

/* Check if we can read duplicate the date.
 * Wait until local_cache_writer is nul (not being updated)
 * and then, increment the number of readers.
 */
	read_is_possible = 0;
	while(0 == read_is_possible) {
		pthread_mutex_lock (&local_cache_lock);
		if(0 == local_cache_writer) {
			read_is_possible = 1; /* local variable */
			local_cache_reader++; /* shared variable */
		}
		pthread_mutex_unlock (&local_cache_lock);
	}

	/* Duplicate the tree */
	if(NULL == (local_names = calloc(local_cache_number,sizeof(local_names)))) {
		*ret_names=NULL;
		*ret_times=NULL;
		*ret_number=0;
		pthread_mutex_lock (&local_cache_lock);
		local_cache_reader--; /* shared variable */
		pthread_mutex_unlock (&local_cache_lock);
		return(-1);
	}
	if(NULL == (local_times = calloc(local_cache_number,sizeof(local_times)))) {
		free(local_names);
		*ret_names=NULL;
		*ret_times=NULL;
		*ret_number=0;
		pthread_mutex_lock (&local_cache_lock);
		local_cache_reader--; /* shared variable */
		pthread_mutex_unlock (&local_cache_lock);
		return(-1);
	}
	for(i=0; i<local_cache_number; i++) {
		if(NULL == (local_names[i] = strdup(local_cache_names[i]))) {
			int j;
			for(j=0; j<i; j++) free(local_names[j]);
			free(local_names);
			free(local_times);
			*ret_names=NULL;
			*ret_times=NULL;
			*ret_number=0;
		}
		local_times[i] = local_cache_times[i];
	}
	pthread_mutex_lock (&local_cache_lock);
	local_cache_reader--;
	pthread_mutex_unlock (&local_cache_lock);
	*ret_names=local_names;
	*ret_times=local_times;
	*ret_number=local_cache_number;

	return(0);
}

static int
send_page (struct MHD_Connection *connection, const char *page,
		int status_code, enum MHD_ResponseMemoryMode mode, const char *mimetype,
		close_connection_e close_connection,jsonrpc_request_result_e result)
{
	int ret;
	struct MHD_Response *response;

	pthread_mutex_lock (&update_counters);
	switch(result) {
		case JSONRPC_REQUEST_FAILED : lock_and_increment(nb_jsonrpc_request_failed_lock, nb_jsonrpc_request_failed, +1); break;
		case JSONRPC_REQUEST_SUCCEEDED : lock_and_increment(nb_jsonrpc_request_success_lock, nb_jsonrpc_request_success, +1); break;
		default : assert (1 == 42);
	}
	pthread_mutex_unlock (&update_counters);

	response =
		MHD_create_response_from_buffer (strlen (page), (void *) page,
				MHD_RESPMEM_PERSISTENT);
	if (!response)
		return MHD_NO;

	MHD_add_response_header(response, "Content-Type", mimetype);
	if(CLOSE_CONNECTION_YES == close_connection) {
		MHD_add_response_header (response, MHD_HTTP_HEADER_CONNECTION, "close");
	}
	ret = MHD_queue_response (connection, status_code, response);
	MHD_destroy_response (response);

	return ret;
}

static void
request_completed (void *cls, struct MHD_Connection *connection,
		void **con_cls, enum MHD_RequestTerminationCode toe)
{
	connection_info_struct_t *con_info = *con_cls;

	if (NULL == con_info)
		return;

	if (con_info->connectiontype == POST)
	{

		if(con_info->jsonrequest) {
			lock_and_increment(nb_clients_lock, nb_clients, -1);
			assert(nb_clients >= 0);
			free (con_info->jsonrequest);
		}
	}
	free (con_info);
	*con_cls = NULL;
}

inline int ishex(char c) { return((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')); }


static int decode_from_www_urlencoded(char *s, int l)
{
	char *o;
	char *dest = s;
	char *end = s + l;
	int c;

	for (o = dest; s <= end; o++) {
		c = *s++;
		if (c == '+') c = ' ';
		else if (c == '%' && ( !ishex(*s++) ||
					!ishex(*s++) ||
					!sscanf(s - 2, "%2x", &c)))
			return -1;

		*o = c;
	}

	return o - dest;
}

char *
jsonrpc_build_error_object_string(int id, int code, const char *message) {
	char *result;
	int lmessage;
	const char *defined_message = NULL;
	
	switch(code) {
		case JSONRPC_ERROR_CODE_32600_INVALID_REQUEST  : defined_message = JSONRPC_ERROR_32600; break;
		case JSONRPC_ERROR_CODE_32601_METHOD_NOT_FOUND : defined_message = JSONRPC_ERROR_32601; break;
		case JSONRPC_ERROR_CODE_32602_INVALID_PARAMS   : defined_message = JSONRPC_ERROR_32602; break;
		case JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR   : defined_message = JSONRPC_ERROR_32603; break;
		default: defined_message = message;
	}
	assert(defined_message != NULL);

	lmessage = strlen(defined_message);
	if(NULL == (result = malloc(strlen(jsonerrorstringformat)+lmessage+30))) 
		return(NULL);
	sprintf(result, jsonerrorstringformat, code, defined_message, id);
	return(result);
}

static int
jsonrpc_parse_node(struct json_object *node, char**jsonanswer) {
	const char *errorstring = NULL;
	int errorcode;
	struct json_object *v;
	const char *str;
	int id;
	const char *method;
	struct json_object *params;
	struct json_object *result;
	struct json_object *obj;
	int i;

	*jsonanswer = NULL;

	if(NULL == (v = json_object_object_get(node, "jsonrpc"))) return(-1);
	if(NULL == (str = json_object_get_string(v)))             return(-1);
	if(strcmp("2.0", str))                                    return(-1);

	if(NULL == (v = json_object_object_get(node, "id")))      return(-1);
	errno=0;
	if(0 == (id = json_object_get_int(v))) {
		if(errno) return(-1);
	}

	if(
			(NULL == (v = json_object_object_get(node, "method"))) ||
			(NULL == (method = json_object_get_string(v)))
	  ) {
		*jsonanswer = jsonrpc_build_error_object_string(id, JSONRPC_ERROR_CODE_32600_INVALID_REQUEST, NULL);
		return(*jsonanswer?0:-1);
	}

	params = json_object_object_get(node, "params"); /* May be NULL. NULL means no params */

/* Find the callback */
	for(i=0; jsonrpc_methods_table[i].cb; i++) {
		if(!strcmp(jsonrpc_methods_table[i].method, method)) break;
	}
	if(! jsonrpc_methods_table[i].cb) {
		*jsonanswer = jsonrpc_build_error_object_string(id, JSONRPC_ERROR_CODE_32601_METHOD_NOT_FOUND, NULL);
		return(*jsonanswer?0:-1);
	}
/* Create the result object */
	if(NULL == (result = json_object_new_object())) {
		*jsonanswer = jsonrpc_build_error_object_string(id, JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR, NULL);
		return(*jsonanswer?0:-1);
	}
	if(NULL == (obj = json_object_new_string("2.0"))) {
		json_object_put(result);
		*jsonanswer = jsonrpc_build_error_object_string(id, JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR, NULL);
		return(*jsonanswer?0:-1);
	}
	json_object_object_add(result, "jsonrpc", obj);
/* Execute the callback */
	if(0 != (errorcode = jsonrpc_methods_table[i].cb(params, result, &errorstring)))  {
		json_object_put(result);
		if(errorcode > 0) {
			*jsonanswer = jsonrpc_build_error_object_string(id, JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR, NULL);
		} else {
			*jsonanswer = jsonrpc_build_error_object_string(id, errorcode, errorstring);
		}
		return(*jsonanswer?0:-1);
	}

/* Finish the result object and convert to string */
	if(NULL == (obj = json_object_new_int(id))) {
		json_object_put(result);
		*jsonanswer = jsonrpc_build_error_object_string(id, JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR, NULL);
		return(*jsonanswer?0:-1);
	}
	json_object_object_add(result, "id", obj);

	if(NULL == (str = json_object_to_json_string(result))) {
		json_object_put(result);
		*jsonanswer = jsonrpc_build_error_object_string(id, JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR, NULL);
		return(*jsonanswer?0:-1);
	}

	if(NULL == (*jsonanswer = strdup(str))) {
		json_object_put(result);
		*jsonanswer = jsonrpc_build_error_object_string(id, JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR, NULL);
		return(*jsonanswer?0:-1);
	}

	json_object_put(result);

	return(0);
}

static int jsonrpc_parse_data(connection_info_struct_t *con_info) {
	json_object *node;
	int l;
	if(NULL == con_info) return(-1);

	if(NULL == con_info->jsonrequest) {
		PREPARE_ERROR_PAGE(MHD_HTTP_BAD_REQUEST,errorpage,MIMETYPE_TEXTHTML);
		return(1);
	}

	if(con_info->jsonrequest_encoding == JRE_WWW_FORM_URLENCODED) {
		l = decode_from_www_urlencoded(con_info->jsonrequest, con_info->jsonrequest_size);
		if(l<0) {
			PREPARE_ERROR_PAGE(MHD_HTTP_BAD_REQUEST,parseerrorpage,MIMETYPE_TEXTHTML);
			return(1);
		}
	}

	node = json_tokener_parse( con_info->jsonrequest);
	if(NULL == node) {
		PREPARE_ERROR_PAGE(MHD_HTTP_BAD_REQUEST,parseerrorpage,MIMETYPE_TEXTHTML);
		return(1);
	}
	/* Note : I have some segfault here, in json_object_is_type(), with
	 * json-c-0.9. No more crash with json-c-0.10. So if you experiment
	 * crashes here, check your json-c version.
	 */
	if(json_object_is_type (node, json_type_array)) {
		int i;
		int l;
		struct array_list *al;
		int array_len;

		if(NULL == ( con_info->answerstring = strdup("["))) {
			PREPARE_ERROR_PAGE(MHD_HTTP_INTERNAL_SERVER_ERROR,servererrorpage,MIMETYPE_TEXTHTML);
			json_object_put(node);
			return(1);
		}

		al = json_object_get_array(node);
		assert(NULL != al);
		array_len = json_object_array_length (node);
		for(i=0; i<array_len; i++) {
			struct json_object *child;
			child = json_object_array_get_idx(node, i);
			assert(NULL != child);
			if(json_object_is_type (child, json_type_object)) {
				char *jstr;
				int l1,l2;
				if(jsonrpc_parse_node(child, &jstr)) {
					if(jstr) free(jstr);
					if(con_info->answerstring) free(con_info->answerstring);
					PREPARE_ERROR_PAGE(MHD_HTTP_BAD_REQUEST,parseerrorpage,MIMETYPE_TEXTHTML);
					json_object_put(node);
					return(1);
				}
				l1 = strlen(con_info->answerstring);
				l2 = strlen(jstr);
				/* Realloc: l1 + l2 + 2 (", ") + 1 ("\0") + 1 (anticipate the
				 * last "]" char)
				 * total = l1 + l2 + 2 + 1 + 1 = l1+l2+4
				 */
				if(NULL == ( con_info->answerstring = realloc(con_info->answerstring, l1+l2+4))) {
					if(jstr) free(jstr);
					PREPARE_ERROR_PAGE(MHD_HTTP_INTERNAL_SERVER_ERROR,servererrorpage,MIMETYPE_TEXTHTML);
					json_object_put(node);
					return(1);
				}
				if(i!=0) {
					memcpy(con_info->answerstring+l1, ", ", 2);
					l1+=2;
				}
				memcpy(con_info->answerstring+l1, jstr, l2+1);
				free(jstr);

			} else {
				if(con_info->answerstring) free(con_info->answerstring);
				PREPARE_ERROR_PAGE(MHD_HTTP_BAD_REQUEST,parseerrorpage,MIMETYPE_TEXTHTML);
				json_object_put(node);
				return(1);
			}

		}
		l = strlen(con_info->answerstring);
		con_info->answerstring[l] = ']';
		con_info->answerstring[l+1] = '\0';
		con_info->answercode = MHD_HTTP_OK;
		con_info->answer_mimetype = MIMETYPE_JSONRPC;
		json_object_put(node);
		return(0);

	} else if(json_object_is_type (node, json_type_object)) {
		char *jstr;
		if(!jsonrpc_parse_node(node, &jstr)) {
			if(jstr) {
				con_info->answerstring = jstr;
				con_info->answercode = MHD_HTTP_OK;
				con_info->answer_mimetype = MIMETYPE_JSONRPC;
				con_info->errorpage = NULL;
				json_object_put(node);
				return(0);
			} else {
				PREPARE_ERROR_PAGE(MHD_HTTP_BAD_REQUEST,parseerrorpage,MIMETYPE_TEXTHTML);
				json_object_put(node);
				return(1);
			}
		}
	} else {
		PREPARE_ERROR_PAGE(MHD_HTTP_BAD_REQUEST,parseerrorpage,MIMETYPE_TEXTHTML);
		json_object_put(node);
		return(1);
	}
	PREPARE_ERROR_PAGE(MHD_HTTP_INTERNAL_SERVER_ERROR,parseerrorpage,MIMETYPE_TEXTHTML);
	json_object_put(node);

	return(0);
}

static int
get_headers (void *cls, enum MHD_ValueKind kind, const char *key,
               const char *value)
{
	connection_info_struct_t *con_info = cls;
/* Print headers */
/*
	INFO (OUTPUT_PREFIX_JSONRPC "%s:%d '%s': '%s'\n",  __FILE__, __LINE__, key, value);
*/
	if(!strcmp(key, "Content-Type")) {
		if(!strcmp(value, "application/x-www-form-urlencoded")) {
			con_info->jsonrequest_encoding = JRE_WWW_FORM_URLENCODED;
		}
	}
	return MHD_YES;
}

static int jsonrpc_proceed_request_cb(void * cls,
		struct MHD_Connection * connection,
		const char * url,
		const char * method,
		const char * version,
		const char * upload_data,
		size_t * upload_data_size,
		void ** con_cls) {


	if (NULL == *con_cls) {
		connection_info_struct_t *con_info;

		if (nb_clients >= max_clients)
			return send_page (connection, busypage, MHD_HTTP_SERVICE_UNAVAILABLE, MHD_RESPMEM_PERSISTENT, MIMETYPE_JSONRPC,
					CLOSE_CONNECTION_YES,JSONRPC_REQUEST_FAILED);


		if(NULL == (con_info = malloc (sizeof (connection_info_struct_t)))) 
			return MHD_NO;

		lock_and_increment(nb_new_connections_lock,nb_new_connections,+1);

		if (0 == strcmp (method, "POST"))
		{

			lock_and_increment(nb_clients_lock,nb_clients,+1);

			con_info->jsonrequest_encoding = JRE_PLAIN;
			MHD_get_connection_values (connection, MHD_HEADER_KIND, get_headers, con_info);

			con_info->connectiontype = POST;
			con_info->jsonrequest = NULL;
			con_info->jsonrequest_size = 0;
			con_info->answercode = MHD_HTTP_BAD_REQUEST;
			con_info->errorpage = errorpage;
			con_info->answerstring = NULL;
			con_info->answer_mimetype = MIMETYPE_TEXTHTML;
		} else {
			con_info->connectiontype = GET;
		}

		*con_cls = (void *) con_info;

		return MHD_YES;
	}

	if (0 == strcmp (method, "GET"))
	{
		return send_page (connection, errorpage, MHD_HTTP_BAD_REQUEST, MHD_RESPMEM_PERSISTENT, MIMETYPE_TEXTHTML,
			CLOSE_CONNECTION_YES, JSONRPC_REQUEST_FAILED);
	}

	if (0 == strcmp (method, "POST"))
	{
		connection_info_struct_t *con_info = *con_cls;

		if (0 != *upload_data_size)
		{
			if(NULL == (con_info->jsonrequest = realloc(con_info->jsonrequest, (1+con_info->jsonrequest_size+(*upload_data_size))*sizeof(*con_info->jsonrequest)))) 
				return MHD_NO;

			memcpy(con_info->jsonrequest+ con_info->jsonrequest_size, upload_data, *upload_data_size);
			con_info->jsonrequest_size += *upload_data_size;
			*upload_data_size = 0;


			return MHD_YES;
		}
		else {
			if(NULL == con_info->jsonrequest) {
				return send_page (connection, con_info->errorpage, con_info->answercode, MHD_RESPMEM_PERSISTENT, con_info->answer_mimetype,
				CLOSE_CONNECTION_YES, JSONRPC_REQUEST_FAILED);
			}

			con_info->jsonrequest[con_info->jsonrequest_size] = '\0';

			jsonrpc_parse_data(con_info);
			
			if(con_info->answerstring) {
				return send_page (connection, con_info->answerstring, con_info->answercode, MHD_RESPMEM_MUST_FREE, con_info->answer_mimetype,
				CLOSE_CONNECTION_NO,JSONRPC_REQUEST_SUCCEEDED);
			} else {
				return send_page (connection, con_info->errorpage, con_info->answercode, MHD_RESPMEM_PERSISTENT, con_info->answer_mimetype,
				CLOSE_CONNECTION_YES, JSONRPC_REQUEST_FAILED);
			}
		}
	}

	return send_page (connection, errorpage, MHD_HTTP_BAD_REQUEST, MHD_RESPMEM_PERSISTENT, MIMETYPE_TEXTHTML,
	CLOSE_CONNECTION_YES, JSONRPC_REQUEST_FAILED);
}

static int jsonrpc_config (const char *key, const char *val)
{
	if (strcasecmp (key, "Port") == 0) {
		errno=0;
		httpd_server_port = strtol(val,NULL,10);
		if(errno) {
			ERROR(OUTPUT_PREFIX_JSONRPC "Port '%s' is not a number or could not be parsed", val);
			return(-1);
		}
		if((httpd_server_port < 1) || (httpd_server_port > 65535)) {
			ERROR(OUTPUT_PREFIX_JSONRPC "Port '%d' should be between 1 and 65535", httpd_server_port);
			return(-1);
		}
	} else if (strcasecmp (key, "MaxClients") == 0) {
		errno=0;
		max_clients = strtol(val,NULL,10);
		if(errno) {
			ERROR(OUTPUT_PREFIX_JSONRPC "MaxClients '%s' is not a number or could not be parsed", val);
			return(-1);
		}
		if((max_clients < 1) || (max_clients > 65535)) {
			ERROR(OUTPUT_PREFIX_JSONRPC "MaxClients '%d' should be between 1 and 65535", max_clients);
			return(-1);
		}
	} else if (strcasecmp (key, "JsonrpcCacheExpirationTime") == 0) {
		errno=0;
		jsonrpc_cache_expiration_time = strtol(val,NULL,10);
		if(errno) {
			ERROR(OUTPUT_PREFIX_JSONRPC "JsonrpcCacheExpirationTime '%s' is not a number or could not be parsed", val);
			return(-1);
		}
		if((jsonrpc_cache_expiration_time < 1) || (jsonrpc_cache_expiration_time > 3600)) {
			ERROR(OUTPUT_PREFIX_JSONRPC "JsonrpcCacheExpirationTime '%ld' should be between 1 and 3600 seconds", jsonrpc_cache_expiration_time);
			return(-1);
		}
	} else {
		return (-1);
	}

	return (0);
} /* int us_config */

static int jsonrpc_init (void)
{
	static int have_init = 0;

	/* Initialize only once. */
	if (have_init != 0)
		return (0);
	have_init = 1;

	/* Start the web server */
	jsonrpc_daemon = MHD_start_daemon(
			MHD_USE_THREAD_PER_CONNECTION,
			httpd_server_port,
			NULL,
			NULL,
			&jsonrpc_proceed_request_cb,
			NULL,
			MHD_OPTION_NOTIFY_COMPLETED, request_completed,
			NULL,
			MHD_OPTION_END);

	if (jsonrpc_daemon == NULL)
		return 1;

	return (0);
} /* int jsonrpc_init */

static int submit_data (value_t v, const char *type, const  char *type_instance)
{
	value_list_t vl = VALUE_LIST_INIT;

	vl.values = &v;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "jsonrpc", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
	
	return (0);
} /* int submit_data */

static int submit_gauge (unsigned int n, const char *type, const  char *type_instance)
{
	value_t value;
	value.gauge = n;

	submit_data(value, type, type_instance);
	
	return (0);
} /* int submit_derive */

static int submit_derive (unsigned int n, const char *type, const  char *type_instance)
{
	value_t value;
	value.derive = n;

	submit_data(value, type, type_instance);
	
	return (0);
} /* int submit_derive */

static int jsonrpc_read (void)
{
	submit_gauge(nb_clients, "current_connections", "nb_clients");
	submit_derive(nb_jsonrpc_request_failed, "total_requests", "nb_request_failed");
	submit_derive(nb_jsonrpc_request_success, "total_requests", "nb_request_succeeded");
	submit_derive(nb_new_connections, "http_requests", "nb_connections");
	return (0);
} /* int jsonrpc_read */

static int jsonrpc_shutdown (void)
{
	MHD_stop_daemon(jsonrpc_daemon);
	return (0);
} /* int jsonrpc_shutdown */

void module_register (void)
{
	plugin_register_config ("jsonrpc", jsonrpc_config,
			config_keys, config_keys_num);
	plugin_register_init ("jsonrpc", jsonrpc_init);
	plugin_register_read ("jsonrpc", jsonrpc_read);
	plugin_register_shutdown ("jsonrpc", jsonrpc_shutdown);
} /* void module_register (void) */

/* vim: set sw=4 ts=4 sts=4 tw=78 : */
