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
#include "utils_avltree.h"
#include "jsonrpc.h"

#include <microhttpd.h>

#include <json/json.h>
#include <pthread.h>

#if HAVE_CRYPT || HAVE_CRYPT_R
#include <crypt.h>
#endif

#ifdef JSONRPC_USE_BASE
#include "jsonrpc_cb_base.h"
#endif
#ifdef JSONRPC_USE_PERFWATCHER
#include "jsonrpc_cb_perfwatcher.h"
#endif
#ifdef JSONRPC_USE_TOPPS
#include "jsonrpc_cb_topps.h"
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
#if HAVE_CRYPT_R
/* crypt_r(3) is reentrant */
#elif HAVE_CRYPT
/* crypt(3) is not reentrant */
static pthread_mutex_t crypt_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

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
#ifdef JSONRPC_USE_TOPPS
		JSONRPC_CB_TABLE_TOPPS
#endif
		{ "", NULL }
	};

static struct MHD_Daemon * jsonrpc_daemon = NULL;
static unsigned int nb_clients = 0;
static unsigned int nb_new_connections = 0;
static unsigned int nb_jsonrpc_request_failed = 0;
static unsigned int nb_jsonrpc_request_success = 0;

#define HTTP_AUTH_REALM_COLLECTD "collectd jsonrpc"

const char *authpagedenied = 
  "<html><body><h1>Access Denied</h1></body></html>";

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
	"Authentication",
	"Authfile",
	"JsonrpcCacheExpirationTime",
	"TopPsDataDir"

};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int httpd_server_port=-1;
static int max_clients = 16;

typedef enum {
	config_authentication_type_none,
	config_authentication_type_basic,
} config_authentication_type_e;

config_authentication_type_e config_authentication_type = config_authentication_type_none;
char *config_authentication_type_basic_filename = NULL;
time_t config_authentication_type_basic_filename_tm_last_read = 0;
c_avl_tree_t *config_authentication_type_basic_filename_cache = NULL;

#ifdef JSONRPC_USE_TOPPS
char toppsdatadir[2048] = "";
#endif


/* Cache of the tree
 * =================
 *
 * This cache is updated with the jsonrpc_read callback.
 * It updates the cache when it is older than
 * JSONRPC_CACHE_EXPIRATION_TIME_DEFAULT seconds.
 *
 * How does it work ?
 * - uc_cache_copy is an array of caches (there a 4 or 5 slots and only 2
 *   should be used, but we never know if we need more).
 * 
 * - jsonrpc_update_cache() updates the caches and free unused older caches.
 *
 * - each time a function needs to use a copy of the cache, it will find and
 *   reference the latest cache (jsonrpc_cache_last_entry_find_and_ref()).
 * - when the cache is no more needed, it dereferences the copy
 *   (jsonrpc_cache_entry_unref()).
 * Example:
 *   int cache_id = jsonrpc_cache_last_entry_find_and_ref(&names,&times,&number);
 *   play with names,times and number
 *   jsonrpc_cache_entry_unref(cache_id);
 *
 */
#define JSONRPC_CACHE_EXPIRATION_TIME_DEFAULT 60
static time_t jsonrpc_cache_expiration_time = JSONRPC_CACHE_EXPIRATION_TIME_DEFAULT;

typedef struct {
	char **names;
	cdtime_t *times;
	size_t number;
	time_t update_time;
	int ref; /* nb of json methods or whatever that are using this copy */
	short ready; /* 1 if this is a valid cache, otherwise 0 */
} uc_cache_copy_t;

/* NB_CACHE_ENTRY_MAX should be < 10.
 * If you want more than 10, check the cache_plugin_instance array.
 */
#define NB_CACHE_ENTRY_MAX 6
static uc_cache_copy_t uc_cache_copy[NB_CACHE_ENTRY_MAX];

static char *cache_plugin_instance[] = {
	"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
	"10", "11", "12", "13", "14", "15", "16", "17", "18", "19",
	"20", "21", "22", "23", "24", "25", "26", "27", "28", "29",
	"30", "31", "32", "33", "34", "35", "36", "37", "38", "39",
	"40", "41", "42", "43", "44", "45", "46", "47", "48", "49",
	"50", "51", "52", "53", "54", "55", "56", "57", "58", "59",
	"60", "61", "62", "63", "64", "65", "66", "67", "68", "69",
	"70", "71", "72", "73", "74", "75", "76", "77", "78", "79",
	"80", "81", "82", "83", "84", "85", "86", "87", "88", "89",
	"90", "91", "92", "93", "94", "95", "96", "97", "98", "99",
};

/*
 * Functions
 */

int jsonrpc_cache_last_entry_find(void) {
	time_t update_time = 0;
	int last_cache_entry = -1;
	int i;
	for(i=0; i<NB_CACHE_ENTRY_MAX; i++) {
		if(uc_cache_copy[i].ready && (uc_cache_copy[i].update_time  > update_time)) {
			update_time = uc_cache_copy[i].update_time;
			last_cache_entry = i;
		}
	}
	return(last_cache_entry);
}

int jsonrpc_cache_nb_entries(void) {
	int i;
	int n=0;
	for(i=0; i<NB_CACHE_ENTRY_MAX; i++) {
		if(uc_cache_copy[i].ready) {
			n++;
		}
	}
	return(n);
}

int jsonrpc_cache_last_entry_find_and_ref(char ***ret_names, cdtime_t **ret_times, size_t *ret_number) {
	time_t update_time = 0;
	int last_cache_entry = -1;
	int i;

	pthread_mutex_lock (&local_cache_lock);
	for(i=0; i<NB_CACHE_ENTRY_MAX; i++) {
		if(uc_cache_copy[i].ready && (uc_cache_copy[i].update_time  > update_time)) {
			update_time = uc_cache_copy[i].update_time;
			last_cache_entry = i;
		}
	}
	if(-1 != last_cache_entry) {
		uc_cache_copy[last_cache_entry].ref++;
		*ret_names = uc_cache_copy[last_cache_entry].names;
		*ret_times = uc_cache_copy[last_cache_entry].times;
		*ret_number = uc_cache_copy[last_cache_entry].number;
	}
	pthread_mutex_unlock (&local_cache_lock);
	return(last_cache_entry);
}

void jsonrpc_cache_entry_unref(int cache_id) {
	pthread_mutex_lock (&local_cache_lock);
	uc_cache_copy[cache_id].ref--;
	assert(uc_cache_copy[cache_id].ref >= 0);
	pthread_mutex_unlock (&local_cache_lock);
	return;
}

int jsonrpc_update_cache() {
	time_t now;
	int i;
	int last_cache_entry = -1;
	int free_cache_entry = -1;
	short update_needed = 0;

	last_cache_entry = jsonrpc_cache_last_entry_find();
	/*
	 * Free old cache memory
	 */
	for(i=0; i<NB_CACHE_ENTRY_MAX; i++) {
		if(uc_cache_copy[i].ready && (uc_cache_copy[i].ref == 0) && (i != last_cache_entry)) {
			int j;
			uc_cache_copy[i].ready=0;
			for(j=0; j<uc_cache_copy[i].number; j++) free(uc_cache_copy[i].names[j]);
			free(uc_cache_copy[i].names);
			free(uc_cache_copy[i].times);
			uc_cache_copy[i].names=NULL;
			uc_cache_copy[i].times=NULL;
			uc_cache_copy[i].number=0;
			uc_cache_copy[i].update_time=0;
		}
	}

	/* 
	 * Check if update is needed.
	 */
	now = time(NULL);
	if(-1 == last_cache_entry) { 
		update_needed = 1;
	} else if((uc_cache_copy[last_cache_entry].update_time + jsonrpc_cache_expiration_time) < now) {
		update_needed = 1;
	}
	if(0 == update_needed) {
		return(0);
	}

	/*
	 * Find free entry
	 */
	free_cache_entry = -1;
	for(i=0; i<NB_CACHE_ENTRY_MAX; i++) {
		if(0 == uc_cache_copy[i].ready) {
			free_cache_entry = i;
			break;
		}
	}
	if(-1 == free_cache_entry) {
		ERROR(OUTPUT_PREFIX_JSONRPC "Not enough cache entry. This is probably a problem"
				" where restarting is the best solution.");
		assert(free_cache_entry != -1);
	}

	if(0 != uc_get_names(
				&(uc_cache_copy[free_cache_entry].names),
				&(uc_cache_copy[free_cache_entry].times),
				&(uc_cache_copy[free_cache_entry].number)
				)) {
		uc_cache_copy[free_cache_entry].names=NULL;
		uc_cache_copy[free_cache_entry].times=NULL;
		uc_cache_copy[free_cache_entry].number = 0;

		uc_cache_copy[free_cache_entry].update_time=0;
		uc_cache_copy[free_cache_entry].ref=0;
		uc_cache_copy[free_cache_entry].ready=0;
		return(-1);
	}
	uc_cache_copy[free_cache_entry].update_time = time(NULL);
	uc_cache_copy[free_cache_entry].ref = 0;
	uc_cache_copy[free_cache_entry].ready = 1;

	return(0);
}

/* HTTP stuff */

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
				mode);
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

static void update_config_authentication_type_basic_filename_cache(time_t mtime) {
	char *name;
	char *value;
	FILE *fh;
	char line[1024];

	if(NULL == config_authentication_type_basic_filename_cache) {
		config_authentication_type_basic_filename_cache = c_avl_create((int (*) (const void *, const void *)) strcmp);
	}
	if(NULL == config_authentication_type_basic_filename_cache) return;

	/* Empty the tree */
	while (c_avl_pick (config_authentication_type_basic_filename_cache, (void *) &name, (void *) &value) == 0)
	{
		sfree (value);
		sfree (name);
	}
	config_authentication_type_basic_filename_tm_last_read = 0;

	/* Open the file and fill the tree */
	if(NULL == (fh = fopen(config_authentication_type_basic_filename, "r"))) return;

	while(fgets(line, sizeof(line), fh)) {
		int i,name,pass;
		char *namestr,*passstr;
		/* Remove spaces at beginnning of the file */
		for(i=0; line[i] && ((line[i] == ' ') || (line[i] == '\t')); i++);
		name = i;
		/* Char # is for comments. Ignore anything after. */
		for(; line[i] && (line[i] != '#'); i++);
		line[i] = '\0';
		/* Find name */
		for(i=name; line[i] && (line[i] != ' ') && (line[i] != '\t'); i++);
		if(line[i] == '\0') continue; /* syntax error : ignore line */
		line[i] = '\0';
		/* Find password */
		for(i+=1; line[i] && ((line[i] == ' ') || (line[i] == '\t')); i++);
		if(line[i] == '\0') continue; /* syntax error : ignore line */
		pass = i;
		for(; line[i] && (line[i] != ' ') && (line[i] != '\t') && (line[i] != '\r') && (line[i] != '\n'); i++);
		line[i] = '\0';
		if((line[name] == '\0') || (line[pass] == '\0')) return; /* syntax error : empty name or pass. Ignore line */

		/* Store name and passwd in the tree */
		if(NULL == (namestr = strdup(line+name))) {
			fclose(fh);
			ERROR(OUTPUT_PREFIX_JSONRPC "Could not allocate memory");
			return;
		}
		if(NULL == (passstr = strdup(line+pass))) {
			fclose(fh);
			free(namestr);
			ERROR(OUTPUT_PREFIX_JSONRPC "Could not allocate memory");
			return;
		}
		c_avl_insert(config_authentication_type_basic_filename_cache, namestr, passstr);
	}
	fclose(fh);
	config_authentication_type_basic_filename_tm_last_read = mtime;
}

static int passwd_check(const char *pass, const char *salt, const char *cryptpass, int *err) {
	/* Return 0 if passwd match */
	/* If returned value is not null, and if err is not null, an
	 * error occured */
#if HAVE_CRYPT_R
	struct crypt_data d;
	char *s;
	*err = 0;

	d.initialized = 0;
	s = crypt_r(pass, salt, &d);
	if(NULL == s) {
		*err = 1;
		return(-1);
	}
	return strcmp(s,cryptpass)?1:0;
#elif HAVE_CRYPT
	int r;
	char *s;

	*err = 0;
	pthread_mutex_lock(&crypt_lock);
	s = crypt(pass, salt);
	if(NULL == s) {
		pthread_mutex_unlock(&crypt_lock);
		*err = 1;
		return(-1);
	}
	r = strcmp(s,cryptpass)?1:0;
	pthread_mutex_unlock(&crypt_lock);
	return r;
#else
	*err = 1;
	return(1);
#endif
}


static int basic_auth_check(const char *user, const char *pass) {
	struct stat st;
#define SALT_BUFFER_SIZE 1024
	char salt_buffer[SALT_BUFFER_SIZE];
	size_t l;
	char *cachedpass;
	int err;

	if(NULL == user) return(1);
	if(NULL == pass) return(1);

	if(0 != lstat(config_authentication_type_basic_filename, &st)) {
		ERROR(OUTPUT_PREFIX_JSONRPC "Missing or unreaddable file '%s'. No authentication can be done.", config_authentication_type_basic_filename); 
		return(1);
	}
	if(st.st_mtime != config_authentication_type_basic_filename_tm_last_read) {
		update_config_authentication_type_basic_filename_cache(st.st_mtime);
	}

	if(NULL == config_authentication_type_basic_filename_cache) return(1);

	if(0 != c_avl_get(config_authentication_type_basic_filename_cache, user, (void *) &cachedpass)) return(1);
/* Check for plain text password */
	if(cachedpass[0] == '"') {
		int i;
		for(i=1; cachedpass[i] && (cachedpass[i] != '"'); i++);
		if(cachedpass[i] != '"') {
			ERROR(OUTPUT_PREFIX_JSONRPC "Syntax error in file '%s' for user '%s'", config_authentication_type_basic_filename, user); 
		} else {
			if(strncmp(cachedpass+1, pass, i-1)) return(1); /* password differ */
			if((i-1) != strlen(pass)) return(1); /* first chars are the same, but password differ on length */
			return(0);
		}
	}
/* Check for crypted password */

	/* Get salt 
	 * format is $id$salt$encrypted (or XXencrypted with XX as the salt with old DES)
	 * see man crypt(3) for more info.
	 */
	l = strlen(cachedpass);
	memcpy(salt_buffer, cachedpass, (l>SALT_BUFFER_SIZE)?SALT_BUFFER_SIZE:l);
	if(salt_buffer[0] == '$') {
		int i;
		int n = 0;
		int salt_pos = 0;
		for(i=1; (i<SALT_BUFFER_SIZE) && (n < 2); i++) {
			if(salt_buffer[i] == '$') {
				if(n == 1) salt_pos = i+1;
				n++;
			}
		}
		if(i>=SALT_BUFFER_SIZE) {
			ERROR(OUTPUT_PREFIX_JSONRPC "Syntax error in file '%s' for user '%s'", config_authentication_type_basic_filename, user); 
			return(1);
		}
		salt_buffer[i-1] = '\0';
	} else {
		salt_buffer[2] = '\0';
		ERROR(OUTPUT_PREFIX_JSONRPC "DES not supported (if this is what you wanted to use). User='%s'", user); 
		/* Note for developers : crypt(3) is not reentrant. And crypt_r is a
		 * GNU extension. So it is not so easy to code.
		 * Have fun searching in how to do with libgcrypt :)
		 */
		return(1);
	}
	if(0 == passwd_check(pass, salt_buffer, cachedpass, &err)) {
		return(0);
	}

	/* If we are here, either password differ, or an unexpected error occured.
	 * In both cases, return an authentication failure.
	 */
	return(1);
}

char *
jsonrpc_build_error_object_string(int id, int code, const char *message) {
	char *result;
	int lmessage;
	int len;
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
	len = strlen(jsonerrorstringformat)+lmessage+30;
	if(NULL == (result = malloc(len))) 
		return(NULL);
	snprintf(result, len, jsonerrorstringformat, code, defined_message, id);
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
		DEBUG(OUTPUT_PREFIX_JSONRPC "Internal error %s:%d", __FILE__, __LINE__);
		*jsonanswer = jsonrpc_build_error_object_string(id, JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR, NULL);
		return(*jsonanswer?0:-1);
	}
	if(NULL == (obj = json_object_new_string("2.0"))) {
		json_object_put(result);
		DEBUG(OUTPUT_PREFIX_JSONRPC "Internal error %s:%d", __FILE__, __LINE__);
		*jsonanswer = jsonrpc_build_error_object_string(id, JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR, NULL);
		return(*jsonanswer?0:-1);
	}
	json_object_object_add(result, "jsonrpc", obj);
/* Execute the callback */
	if(0 != (errorcode = jsonrpc_methods_table[i].cb(params, result, &errorstring)))  {
		json_object_put(result);
		if(errorcode > 0) {
			DEBUG(OUTPUT_PREFIX_JSONRPC "Internal error %s:%d", __FILE__, __LINE__);
			*jsonanswer = jsonrpc_build_error_object_string(id, JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR, NULL);
		} else {
			*jsonanswer = jsonrpc_build_error_object_string(id, errorcode, errorstring);
		}
		return(*jsonanswer?0:-1);
	}

/* Finish the result object and convert to string */
	if(NULL == (obj = json_object_new_int(id))) {
		json_object_put(result);
		DEBUG(OUTPUT_PREFIX_JSONRPC "Internal error %s:%d", __FILE__, __LINE__);
		*jsonanswer = jsonrpc_build_error_object_string(id, JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR, NULL);
		return(*jsonanswer?0:-1);
	}
	json_object_object_add(result, "id", obj);

	if(NULL == (str = json_object_to_json_string(result))) {
		json_object_put(result);
		DEBUG(OUTPUT_PREFIX_JSONRPC "Internal error %s:%d", __FILE__, __LINE__);
		*jsonanswer = jsonrpc_build_error_object_string(id, JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR, NULL);
		return(*jsonanswer?0:-1);
	}

	if(NULL == (*jsonanswer = strdup(str))) {
		json_object_put(result);
		DEBUG(OUTPUT_PREFIX_JSONRPC "Internal error %s:%d", __FILE__, __LINE__);
		*jsonanswer = jsonrpc_build_error_object_string(id, JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR, NULL);
		return(*jsonanswer?0:-1);
	}

	json_object_put(result);

	return(0);
}

static int jsonrpc_parse_data(connection_info_struct_t *con_info) {
	json_object *node;
	int l;
	enum json_tokener_error jerr;

	if(NULL == con_info) return(-1);

	if(NULL == con_info->jsonrequest) {
		PREPARE_ERROR_PAGE(MHD_HTTP_BAD_REQUEST,errorpage,MIMETYPE_TEXTHTML);
		DEBUG(OUTPUT_PREFIX_JSONRPC "Request failed (%s:%d)", __FILE__, __LINE__);
		return(1);
	}

	if(con_info->jsonrequest_encoding == JRE_WWW_FORM_URLENCODED) {
		l = decode_from_www_urlencoded(con_info->jsonrequest, con_info->jsonrequest_size);
		if(l<0) {
			PREPARE_ERROR_PAGE(MHD_HTTP_BAD_REQUEST,parseerrorpage,MIMETYPE_TEXTHTML);
			DEBUG(OUTPUT_PREFIX_JSONRPC "Request failed : could not decode from wwwurlencoded");
			con_info->jsonrequest[con_info->jsonrequest_size] = '\0';
			DEBUG(OUTPUT_PREFIX_JSONRPC "Request was (maybe truncated to 1024 chars) :  %s", con_info->jsonrequest);
			return(1);
		}
	}

	jerr = 0;
	node = json_tokener_parse_verbose( con_info->jsonrequest, &jerr);
	if(NULL == node) {
		PREPARE_ERROR_PAGE(MHD_HTTP_BAD_REQUEST,parseerrorpage,MIMETYPE_TEXTHTML);
		DEBUG(OUTPUT_PREFIX_JSONRPC "Request failed : Parse error (%s)", json_tokener_error_desc(jerr));
		con_info->jsonrequest[con_info->jsonrequest_size] = '\0';
		DEBUG(OUTPUT_PREFIX_JSONRPC "Request was (maybe truncated to 1024 chars) :  %s", con_info->jsonrequest);
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
			DEBUG(OUTPUT_PREFIX_JSONRPC "Request failed : not enough memory (%s:%d)", __FILE__,__LINE__);
			con_info->jsonrequest[con_info->jsonrequest_size] = '\0';
			DEBUG(OUTPUT_PREFIX_JSONRPC "Request was (maybe truncated to 1024 chars) :  %s", con_info->jsonrequest);
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
					DEBUG(OUTPUT_PREFIX_JSONRPC "Request failed : could not parse a node (%s:%d)", __FILE__,__LINE__);
					con_info->jsonrequest[con_info->jsonrequest_size] = '\0';
					DEBUG(OUTPUT_PREFIX_JSONRPC "Request was (maybe truncated to 1024 chars) :  %s", con_info->jsonrequest);
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
					DEBUG(OUTPUT_PREFIX_JSONRPC "Request failed : not enough memory (%s:%d)", __FILE__,__LINE__);
					con_info->jsonrequest[con_info->jsonrequest_size] = '\0';
					DEBUG(OUTPUT_PREFIX_JSONRPC "Request was (maybe truncated to 1024 chars) :  %s", con_info->jsonrequest);
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
				DEBUG(OUTPUT_PREFIX_JSONRPC "Request failed : wrong type, expected object (%s:%d)", __FILE__,__LINE__);
				con_info->jsonrequest[con_info->jsonrequest_size] = '\0';
				DEBUG(OUTPUT_PREFIX_JSONRPC "Request was (maybe truncated to 1024 chars) :  %s", con_info->jsonrequest);
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
				DEBUG(OUTPUT_PREFIX_JSONRPC "Request failed : could not parse a node (%s:%d)", __FILE__,__LINE__);
				con_info->jsonrequest[con_info->jsonrequest_size] = '\0';
				DEBUG(OUTPUT_PREFIX_JSONRPC "Request was (maybe truncated to 1024 chars) :  %s", con_info->jsonrequest);
				return(1);
			}
		}
	} else {
		PREPARE_ERROR_PAGE(MHD_HTTP_BAD_REQUEST,parseerrorpage,MIMETYPE_TEXTHTML);
		json_object_put(node);
		DEBUG(OUTPUT_PREFIX_JSONRPC "Request failed : wrong type, expected array or object (%s:%d)", __FILE__,__LINE__);
		con_info->jsonrequest[con_info->jsonrequest_size] = '\0';
		DEBUG(OUTPUT_PREFIX_JSONRPC "Request was (maybe truncated to 1024 chars) :  %s", con_info->jsonrequest);
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
		char *basic_auth_user = NULL;
		char *basic_auth_password = NULL;
		int basic_auth_status;

		if (nb_clients >= max_clients) {
			DEBUG(OUTPUT_PREFIX_JSONRPC "Request failed : nb clients (%d) > %d", nb_clients,max_clients);
			return send_page (connection, busypage, MHD_HTTP_SERVICE_UNAVAILABLE, MHD_RESPMEM_PERSISTENT, MIMETYPE_JSONRPC,
					CLOSE_CONNECTION_YES,JSONRPC_REQUEST_FAILED);
		}

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
		} else if (0 == strcmp (method, "GET")) {
			con_info->connectiontype = GET;
		} else {
			free(con_info);
			return MHD_NO;
		}

		*con_cls = (void *) con_info;

		if(config_authentication_type == config_authentication_type_basic) {
			basic_auth_user = MHD_basic_auth_get_username_password (connection, &basic_auth_password);
			basic_auth_status = basic_auth_check(basic_auth_user,basic_auth_password);
			if(basic_auth_password) free (basic_auth_password);

			if(0 != basic_auth_status) {
				int ret;
				struct MHD_Response *response;

				response = MHD_create_response_from_buffer (strlen (authpagedenied), (void *) authpagedenied, MHD_RESPMEM_PERSISTENT);
				if (!response) {
					*con_cls = NULL;
					free(con_info);
					return MHD_NO;
				}

				ret = MHD_queue_basic_auth_fail_response(connection, HTTP_AUTH_REALM_COLLECTD, response);
				if(MHD_NO == ret) {
					*con_cls = NULL;
					free(con_info);
					return MHD_NO;
				}
				MHD_destroy_response (response);
				return(ret);
			}
		}
		return MHD_YES;
	}

	if (0 == strcmp (method, "GET"))
	{
		DEBUG(OUTPUT_PREFIX_JSONRPC "Request failed : got GET request");
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

	DEBUG(OUTPUT_PREFIX_JSONRPC "Request failed : unknown method (%s)", method);
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
	} else if (strcasecmp (key, "Authentication") == 0) {
		if( !strcasecmp(val, "None") ) {
			config_authentication_type = config_authentication_type_none;
		} else if( !strcasecmp(val, "Basic") ) {
			config_authentication_type = config_authentication_type_basic;
		} else {
			ERROR(OUTPUT_PREFIX_JSONRPC "Authentication type '%s' not supported. Currently supported : 'None' and 'Basic' only", key);
		}
	} else if (strcasecmp (key, "Authfile") == 0) {
		char *s = strdup (val);
		if (s == NULL) return (1);
		if(config_authentication_type_basic_filename) free (config_authentication_type_basic_filename);
		config_authentication_type_basic_filename = s;
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
	} else if (strcasecmp (key, "TopPsDataDir") == 0) {
#ifdef JSONRPC_USE_TOPPS
		errno=0;
		strncpy(toppsdatadir, val, sizeof(toppsdatadir));
#else
			WARNING(OUTPUT_PREFIX_JSONRPC "TopPsDataDir specified but this mudule was not compiled to use it.");
#endif
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

	/* Check some args */
	if(config_authentication_type == config_authentication_type_basic) {
		if(NULL == config_authentication_type_basic_filename) {
			ERROR(OUTPUT_PREFIX_JSONRPC "Authentication is Basic but no Authfile was specified.");
			return 1;
		}
	}

	/* Initialize the local caches */
	memset(uc_cache_copy, '\0', sizeof(uc_cache_copy));

	/* Verification for developers who would set a too big value here */
	if(NB_CACHE_ENTRY_MAX > (sizeof(cache_plugin_instance)/sizeof(*cache_plugin_instance))) {
		ERROR(OUTPUT_PREFIX_JSONRPC "NB_CACHE_ENTRY_MAX should not be so big (you specified %d at compilation time)", NB_CACHE_ENTRY_MAX);
		assert(NB_CACHE_ENTRY_MAX <= (sizeof(cache_plugin_instance)/sizeof(*cache_plugin_instance)));
	}

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
	static int first_time = 1;
	unsigned int nb_entries_in_last_cache = 0;
	time_t update_time = 0;
	int i;
	if(first_time) {
		INFO(OUTPUT_PREFIX_JSONRPC "Compilation time : %s %s", __DATE__, __TIME__);
		first_time = 0;
	}
	submit_gauge(nb_clients, "current_connections", "nb_clients");
	submit_derive(nb_jsonrpc_request_failed, "total_requests", "nb_request_failed");
	submit_derive(nb_jsonrpc_request_success, "total_requests", "nb_request_succeeded");
	submit_derive(nb_new_connections, "http_requests", "nb_connections");

	jsonrpc_update_cache();
	submit_gauge(jsonrpc_cache_nb_entries(), "cache_size", "nb_used_cached");
	for(i=0; i<NB_CACHE_ENTRY_MAX; i++) {
		int n;
		pthread_mutex_lock (&local_cache_lock);
		n = uc_cache_copy[i].ready?uc_cache_copy[i].ref:0;
		if(uc_cache_copy[i].ready && (uc_cache_copy[i].update_time  > update_time)) {
			update_time = uc_cache_copy[i].update_time;
			nb_entries_in_last_cache = uc_cache_copy[i].number;
		}
		pthread_mutex_unlock (&local_cache_lock);
		submit_gauge(n, "cache_entries", cache_plugin_instance[i]);
	}
	submit_gauge(nb_entries_in_last_cache, "nb_values", "");

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
