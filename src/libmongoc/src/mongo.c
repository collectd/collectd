/* mongo.c */

/*    Copyright 2009-2012 10gen Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#if _MSC_VER && ! _CRT_SECURE_NO_WARNINGS
  #define _CRT_SECURE_NO_WARNINGS
#endif

#if _MSC_VER
  #define snprintf _snprintf
#endif

#include "mongo.h"
#include "md5.h"
#include "env.h"

#include <string.h>
#include <assert.h>

MONGO_EXPORT mongo* mongo_alloc( void ) {
    return ( mongo* )bson_malloc( sizeof( mongo ) );
}


MONGO_EXPORT void mongo_dealloc(mongo* conn) {
    bson_free( conn );
}

MONGO_EXPORT int mongo_get_err(mongo* conn) {
    return conn->err;
}


MONGO_EXPORT int mongo_is_connected(mongo* conn) {
    return conn->connected != 0;
}


MONGO_EXPORT int mongo_get_op_timeout(mongo* conn) {
    return conn->op_timeout_ms;
}


static const char* _get_host_port(mongo_host_port* hp) {
    static char _hp[sizeof(hp->host)+12];
    bson_sprintf(_hp, "%s:%d", hp->host, hp->port);
    return _hp;
}


MONGO_EXPORT const char* mongo_get_primary(mongo* conn) {
    mongo* conn_ = (mongo*)conn;
    if( !(conn_->connected) || (conn_->primary->host[0] == '\0') )
        return NULL;
    return _get_host_port(conn_->primary);
}


MONGO_EXPORT SOCKET mongo_get_socket(mongo* conn) {
    mongo* conn_ = (mongo*)conn;
    return conn_->sock;
}


MONGO_EXPORT int mongo_get_host_count(mongo* conn) {
    mongo_replica_set* r = conn->replica_set;
    mongo_host_port* hp;
    int count = 0;
    if (!r) return 0;
    for (hp = r->hosts; hp; hp = hp->next)
        ++count;
    return count;
}


MONGO_EXPORT const char* mongo_get_host(mongo* conn, int i) {
    mongo_replica_set* r = conn->replica_set;
    mongo_host_port* hp;
    int count = 0;
    if (!r) return 0;
    for (hp = r->hosts; hp; hp = hp->next) {
        if (count == i)
            return _get_host_port(hp);
        ++count;
    }
    return 0;
}

MONGO_EXPORT mongo_write_concern* mongo_write_concern_alloc( void ) {
    return ( mongo_write_concern* )bson_malloc( sizeof( mongo_write_concern ) );
}


MONGO_EXPORT void mongo_write_concern_dealloc( mongo_write_concern* write_concern ) {
    bson_free( write_concern );
}


MONGO_EXPORT mongo_cursor* mongo_cursor_alloc( void ) {
    return ( mongo_cursor* )bson_malloc( sizeof( mongo_cursor ) );
}


MONGO_EXPORT void mongo_cursor_dealloc( mongo_cursor* cursor ) {
    bson_free( cursor );
}


MONGO_EXPORT int  mongo_get_server_err(mongo* conn) {
    return conn->lasterrcode;
}


MONGO_EXPORT const char*  mongo_get_server_err_string(mongo* conn) {
    return conn->lasterrstr;
}

MONGO_EXPORT void __mongo_set_error( mongo *conn, mongo_error_t err, const char *str,
                                     int errcode ) {
    size_t str_size = 1;
    conn->err = err;
    conn->errcode = errcode;
    if( str ) {
        str_size = strlen( str ) + 1;
        if (str_size > MONGO_ERR_LEN) str_size = MONGO_ERR_LEN;
        memcpy( conn->errstr, str, str_size );
    }
    conn->errstr[str_size-1] = '\0';
}

MONGO_EXPORT void mongo_clear_errors( mongo *conn ) {
    conn->err = MONGO_CONN_SUCCESS;
    conn->errcode = 0;
    conn->lasterrcode = 0;
    conn->errstr[0] = 0;
    conn->lasterrstr[0] = 0;
}

/* Note: this function returns a char* which must be freed. */
static char *mongo_ns_to_cmd_db( const char *ns ) {
    char *current = NULL;
    char *cmd_db_name = NULL;
    int len = 0;

    for( current = (char *)ns; *current != '.'; current++ ) {
        len++;
    }

    cmd_db_name = (char *)bson_malloc( len + 6 );
    strncpy( cmd_db_name, ns, len );
    strncpy( cmd_db_name + len, ".$cmd", 6 );

    return cmd_db_name;
}

MONGO_EXPORT int mongo_validate_ns( mongo *conn, const char *ns ) {
    char *last = NULL;
    char *current = NULL;
    const char *db_name = ns;
    char *collection_name = NULL;
    char errmsg[64];
    int ns_len = 0;

    /* If the first character is a '.', fail. */
    if( *ns == '.' ) {
        __mongo_set_error( conn, MONGO_NS_INVALID, "ns cannot start with a '.'.", 0 );
        return MONGO_ERROR;
    }

    /* Find the division between database and collection names. */
    for( current = (char *)ns; *current != '\0'; current++ ) {
        if( *current == '.' ) {
            current++;
            break;
        }
    }

    /* Fail if the collection part starts with a dot. */
    if( *current == '.' ) {
        __mongo_set_error( conn, MONGO_NS_INVALID, "ns cannot start with a '.'.", 0 );
        return MONGO_ERROR;
    }

    /* Fail if collection length is 0.
     * or the ns doesn't contain a '.'. */
    if( *current == '\0' ) {
        __mongo_set_error( conn, MONGO_NS_INVALID, "Collection name missing.", 0 );
        return MONGO_ERROR;
    }


    /* Point to the beginning of the collection name. */
    collection_name = current;

    /* Ensure that the database name is greater than one char.*/
    if( collection_name - 1 == db_name ) {
        __mongo_set_error( conn, MONGO_NS_INVALID, "Database name missing.", 0 );
        return MONGO_ERROR;
    }

    /* Go back and validate the database name. */
    for( current = (char *)db_name; *current != '.'; current++ ) {
        switch( *current ) {
        case ' ':
        case '$':
        case '/':
        case '\\':
            __mongo_set_error( conn, MONGO_NS_INVALID,
                               "Database name may not contain ' ', '$', '/', or '\\'", 0 );
            return MONGO_ERROR;
        default:
            break;
        }

        ns_len++;
    }

    /* Add one to the length for the '.' character. */
    ns_len++;

    /* Now validate the collection name. */
    for( current = collection_name; *current != '\0'; current++ ) {

        /* Cannot have two consecutive dots. */
        if( last && *last == '.' && *current == '.' ) {
            __mongo_set_error( conn, MONGO_NS_INVALID,
                               "Collection may not contain two consecutive '.'", 0 );
            return MONGO_ERROR;
        }

        /* Cannot contain a '$' */
        if( *current == '$' ) {
            __mongo_set_error( conn, MONGO_NS_INVALID,
                               "Collection may not contain '$'", 0 );
            return MONGO_ERROR;
        }

        last = current;
        ns_len++;
    }

    if( ns_len > 128 ) {
        bson_sprintf( errmsg, "Namespace too long; has %d but must <= 128.",
                      ns_len );
        __mongo_set_error( conn, MONGO_NS_INVALID, errmsg, 0 );
        return MONGO_ERROR;
    }

    /* Cannot end with a '.' */
    if( *(current - 1) == '.' ) {
        __mongo_set_error( conn, MONGO_NS_INVALID,
                           "Collection may not end with '.'", 0 );
        return MONGO_ERROR;
    }

    return MONGO_OK;
}

static void mongo_set_last_error( mongo *conn, bson_iterator *it, bson *obj ) {
    bson_iterator iter[1];
    int result_len = bson_iterator_string_len( it );
    const char *result_string = bson_iterator_string( it );
    int len = result_len < MONGO_ERR_LEN ? result_len : MONGO_ERR_LEN;
    memcpy( conn->lasterrstr, result_string, len );
    iter[0] = *it;  // no side effects on the passed iter
    if( bson_find( iter, obj, "code" ) != BSON_NULL )
        conn->lasterrcode = bson_iterator_int( iter );
}

static const int ZERO = 0;
static const int ONE = 1;
static mongo_message *mongo_message_create( size_t len , int id , int responseTo , int op ) {
    mongo_message *mm;

    if( len >= INT32_MAX) {
        return NULL;
    }
    mm = ( mongo_message * )bson_malloc( len );
    if ( !id )
        id = rand();

    /* native endian (converted on send) */
    mm->head.len = ( int )len;
    mm->head.id = id;
    mm->head.responseTo = responseTo;
    mm->head.op = op;

    return mm;
}

/* Always calls bson_free(mm) */
static int mongo_message_send( mongo *conn, mongo_message *mm ) {
    mongo_header head; /* little endian */
    int res;
    bson_little_endian32( &head.len, &mm->head.len );
    bson_little_endian32( &head.id, &mm->head.id );
    bson_little_endian32( &head.responseTo, &mm->head.responseTo );
    bson_little_endian32( &head.op, &mm->head.op );

    res = mongo_env_write_socket( conn, &head, sizeof( head ) );
    if( res != MONGO_OK ) {
        bson_free( mm );
        return res;
    }

    res = mongo_env_write_socket( conn, &mm->data, mm->head.len - sizeof( head ) );
    if( res != MONGO_OK ) {
        bson_free( mm );
        return res;
    }

    bson_free( mm );
    return MONGO_OK;
}

static int mongo_read_response( mongo *conn, mongo_reply **reply ) {
    mongo_header head; /* header from network */
    mongo_reply_fields fields; /* header from network */
    mongo_reply *out;  /* native endian */
    unsigned int len;
    int res;

    if ( ( res = mongo_env_read_socket( conn, &head, sizeof( head ) ) ) != MONGO_OK ||
         ( res = mongo_env_read_socket( conn, &fields, sizeof( fields ) ) ) != MONGO_OK ) {
        return res;
    }

    bson_little_endian32( &len, &head.len );

    if ( len < sizeof( head )+sizeof( fields ) || len > 64*1024*1024 )
        return MONGO_READ_SIZE_ERROR;  /* most likely corruption */

    /*
     * mongo_reply matches the wire for observed environments (MacOS, Linux, Windows VC), but
     * the following incorporates possible differences with type sizes and padding/packing
     *
     * assert( sizeof(mongo_reply) - sizeof(char) - 16 - 20 + len >= len );
     * printf( "sizeof(mongo_reply) - sizeof(char) - 16 - 20 = %ld\n", sizeof(mongo_reply) - sizeof(char) - 16 - 20 );
     */
    out = ( mongo_reply * )bson_malloc( sizeof(mongo_reply) - sizeof(char) + len - 16 - 20 );

    out->head.len = len;
    bson_little_endian32( &out->head.id, &head.id );
    bson_little_endian32( &out->head.responseTo, &head.responseTo );
    bson_little_endian32( &out->head.op, &head.op );

    bson_little_endian32( &out->fields.flag, &fields.flag );
    bson_little_endian64( &out->fields.cursorID, &fields.cursorID );
    bson_little_endian32( &out->fields.start, &fields.start );
    bson_little_endian32( &out->fields.num, &fields.num );

    res = mongo_env_read_socket( conn, &out->objs, len - 16 - 20 ); /* was len-sizeof( head )-sizeof( fields ) */
    if( res != MONGO_OK ) {
        bson_free( out );
        return res;
    }

    *reply = out;

    return MONGO_OK;
}


static char *mongo_data_append( char *start , const void *data , size_t len ) {
    memcpy( start , data , len );
    return start + len;
}

static char *mongo_data_append32( char *start , const void *data ) {
    bson_little_endian32( start , data );
    return start + 4;
}

static char *mongo_data_append64( char *start , const void *data ) {
    bson_little_endian64( start , data );
    return start + 8;
}

/* Connection API */

static int mongo_check_is_master( mongo *conn ) {
    bson out;
    bson_iterator it;
    bson_bool_t ismaster = 0;
    int max_bson_size = MONGO_DEFAULT_MAX_BSON_SIZE;

    if ( mongo_simple_int_command( conn, "admin", "ismaster", 1, &out ) != MONGO_OK )
        return MONGO_ERROR;

    if( bson_find( &it, &out, "ismaster" ) )
        ismaster = bson_iterator_bool( &it );
    if( bson_find( &it, &out, "maxBsonObjectSize" ) )
        max_bson_size = bson_iterator_int( &it );
    conn->max_bson_size = max_bson_size;

    bson_destroy( &out );

    if( ismaster )
        return MONGO_OK;
    else {
        conn->err = MONGO_CONN_NOT_MASTER;
        return MONGO_ERROR;
    }
}

MONGO_EXPORT void mongo_init_sockets( void ) {
    mongo_env_sock_init();
}

/* WC1 is completely static */
static char WC1_data[] = {23,0,0,0,16,103,101,116,108,97,115,116,101,114,114,111,114,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0};
static bson WC1_cmd = {
    WC1_data, WC1_data, 128, 1, 0
};
static mongo_write_concern WC1 = { 1, 0, 0, 0, 0, &WC1_cmd }; /* w = 1 */

MONGO_EXPORT void mongo_init( mongo *conn ) {
    memset( conn, 0, sizeof( mongo ) );
    conn->max_bson_size = MONGO_DEFAULT_MAX_BSON_SIZE;
    mongo_set_write_concern( conn, &WC1 );
}

MONGO_EXPORT int mongo_client( mongo *conn , const char *host, int port ) {
    mongo_init( conn );

    conn->primary = (mongo_host_port*)bson_malloc( sizeof( mongo_host_port ) );
    snprintf( conn->primary->host, MAXHOSTNAMELEN, "%s", host);
    conn->primary->port = port;
    conn->primary->next = NULL;

    if( mongo_env_socket_connect( conn, host, port ) != MONGO_OK )
        return MONGO_ERROR;

    return mongo_check_is_master( conn );
}

MONGO_EXPORT int mongo_connect( mongo *conn , const char *host, int port ) {
    int ret;
    bson_errprintf("WARNING: mongo_connect() is deprecated, please use mongo_client()\n");
    ret = mongo_client( conn, host, port );
    mongo_set_write_concern( conn, 0 );
    return ret;
}

MONGO_EXPORT void mongo_replica_set_init( mongo *conn, const char *name ) {
    mongo_init( conn );

    conn->replica_set = (mongo_replica_set*)bson_malloc( sizeof( mongo_replica_set ) );
    conn->replica_set->primary_connected = 0;
    conn->replica_set->seeds = NULL;
    conn->replica_set->hosts = NULL;
    conn->replica_set->name = ( char * )bson_malloc( strlen( name ) + 1 );
    memcpy( conn->replica_set->name, name, strlen( name ) + 1  );

    conn->primary = (mongo_host_port*)bson_malloc( sizeof( mongo_host_port ) );
    conn->primary->host[0] = '\0';
    conn->primary->next = NULL;
}

MONGO_EXPORT void mongo_replset_init( mongo *conn, const char *name ) {
    bson_errprintf("WARNING: mongo_replset_init() is deprecated, please use mongo_replica_set_init()\n");
    mongo_replica_set_init( conn, name );
}

static void mongo_replica_set_add_node( mongo_host_port **list, const char *host, int port ) {
    mongo_host_port *host_port = (mongo_host_port*)bson_malloc( sizeof( mongo_host_port ) );
    host_port->port = port;
    host_port->next = NULL;
    snprintf( host_port->host, MAXHOSTNAMELEN, "%s", host);

    if( *list == NULL )
        *list = host_port;
    else {
        mongo_host_port *p = *list;
        while( p->next != NULL )
            p = p->next;
        p->next = host_port;
    }
}

static void mongo_replica_set_free_list( mongo_host_port **list ) {
    mongo_host_port *node = *list;
    mongo_host_port *prev;

    while( node != NULL ) {
        prev = node;
        node = node->next;
        bson_free( prev );
    }

    *list = NULL;
}

MONGO_EXPORT void mongo_replica_set_add_seed( mongo *conn, const char *host, int port ) {
    mongo_replica_set_add_node( &conn->replica_set->seeds, host, port );
}

MONGO_EXPORT void mongo_replset_add_seed( mongo *conn, const char *host, int port ) {
    bson_errprintf("WARNING: mongo_replset_add_seed() is deprecated, please use mongo_replica_set_add_seed()\n");
    mongo_replica_set_add_node( &conn->replica_set->seeds, host, port );
}

void mongo_parse_host( const char *host_string, mongo_host_port *host_port ) {
    int len, idx, split;
    len = split = idx = 0;

    /* Split the host_port string at the ':' */
    while( 1 ) {
        if( *( host_string + len ) == '\0' )
            break;
        if( *( host_string + len ) == ':' )
            split = len;

        len++;
    }

    /* If 'split' is set, we know the that port exists;
     * Otherwise, we set the default port. */
    idx = split ? split : len;
    memcpy( host_port->host, host_string, idx );
    memcpy( host_port->host + idx, "\0", 1 );
    if( split )
        host_port->port = atoi( host_string + idx + 1 );
    else
        host_port->port = MONGO_DEFAULT_PORT;
}

static void mongo_replica_set_check_seed( mongo *conn ) {
    bson out[1];
    const char *data;
    bson_iterator it[1];
    bson_iterator it_sub[1];
    const char *host_string;
    mongo_host_port *host_port = NULL;

    if( mongo_simple_int_command( conn, "admin", "ismaster", 1, out ) == MONGO_OK ) {

        if( bson_find( it, out, "hosts" ) ) {
            data = bson_iterator_value( it );
            bson_iterator_from_buffer( it_sub, data );

            /* Iterate over host list, adding each host to the
             * connection's host list. */
            while( bson_iterator_next( it_sub ) ) {
                host_string = bson_iterator_string( it_sub );

                host_port = (mongo_host_port*)bson_malloc( sizeof( mongo_host_port ) );

                if( host_port ) {
                    mongo_parse_host( host_string, host_port );
                    mongo_replica_set_add_node( &conn->replica_set->hosts,
                                                host_port->host, host_port->port );

                    bson_free( host_port );
                    host_port = NULL;
                }
            }
        }
    }

    bson_destroy(out);
    mongo_env_close_socket( conn->sock );
    conn->sock = 0;
    conn->connected = 0;
}

/* Find out whether the current connected node is master, and
 * verify that the node's replica set name matched the provided name
 */
static int mongo_replica_set_check_host( mongo *conn ) {

    bson out[1];
    bson_iterator it[1];
    bson_bool_t ismaster = 0;
    const char *set_name;
    int max_bson_size = MONGO_DEFAULT_MAX_BSON_SIZE;

    if ( mongo_simple_int_command( conn, "admin", "ismaster", 1, out ) == MONGO_OK ) {
        if( bson_find( it, out, "ismaster" ) )
            ismaster = bson_iterator_bool( it );

        if( bson_find( it, out, "maxBsonObjectSize" ) )
            max_bson_size = bson_iterator_int( it );
        conn->max_bson_size = max_bson_size;

        if( bson_find( it, out, "setName" ) ) {
            set_name = bson_iterator_string( it );
            if( strcmp( set_name, conn->replica_set->name ) != 0 ) {
                bson_destroy( out );
                conn->err = MONGO_CONN_BAD_SET_NAME;
                return MONGO_ERROR;
            }
        }
    }

    bson_destroy( out );

    if( ismaster ) {
        conn->replica_set->primary_connected = 1;
    }
    else {
        mongo_env_close_socket( conn->sock );
    }

    return MONGO_OK;
}

MONGO_EXPORT int mongo_replica_set_client( mongo *conn ) {

    int res = 0;
    mongo_host_port *node;

    conn->sock = 0;
    conn->connected = 0;

    /* First iterate over the seed nodes to get the canonical list of hosts
     * from the replica set. Break out once we have a host list.
     */
    node = conn->replica_set->seeds;
    while( node != NULL ) {
        res = mongo_env_socket_connect( conn, ( const char * )&node->host, node->port );
        if( res == MONGO_OK ) {
            mongo_replica_set_check_seed( conn );
            if( conn->replica_set->hosts )
                break;
        }
        node = node->next;
    }

    /* Iterate over the host list, checking for the primary node. */
    if( !conn->replica_set->hosts ) {
        conn->err = MONGO_CONN_NO_PRIMARY;
        return MONGO_ERROR;
    }
    else {
        node = conn->replica_set->hosts;

        while( node != NULL ) {
            res = mongo_env_socket_connect( conn, ( const char * )&node->host, node->port );

            if( res == MONGO_OK ) {
                if( mongo_replica_set_check_host( conn ) != MONGO_OK )
                    return MONGO_ERROR;

                /* Primary found, so return. */
                else if( conn->replica_set->primary_connected ) {
                    conn->primary = bson_malloc( sizeof( mongo_host_port ) );
                    snprintf( conn->primary->host, MAXHOSTNAMELEN, "%s", node->host );
                    conn->primary->port = node->port;
                    return MONGO_OK;
                }

                /* No primary, so close the connection. */
                else {
                    mongo_env_close_socket( conn->sock );
                    conn->sock = 0;
                    conn->connected = 0;
                }
            }

            node = node->next;
        }
    }


    conn->err = MONGO_CONN_NO_PRIMARY;
    return MONGO_ERROR;
}

MONGO_EXPORT int mongo_replset_connect( mongo *conn ) {
    int ret;
    bson_errprintf("WARNING: mongo_replset_connect() is deprecated, please use mongo_replica_set_client()\n");
    ret = mongo_replica_set_client( conn );
    mongo_set_write_concern( conn, 0 );
    return ret;
}

MONGO_EXPORT int mongo_set_op_timeout( mongo *conn, int millis ) {
    conn->op_timeout_ms = millis;
    if( conn->sock && conn->connected )
        mongo_env_set_socket_op_timeout( conn, millis );

    return MONGO_OK;
}

MONGO_EXPORT int mongo_reconnect( mongo *conn ) {
    int res;
    mongo_disconnect( conn );

    if( conn->replica_set ) {
        conn->replica_set->primary_connected = 0;
        mongo_replica_set_free_list( &conn->replica_set->hosts );
        conn->replica_set->hosts = NULL;
        res = mongo_replica_set_client( conn );
        return res;
    }
    else
        return mongo_env_socket_connect( conn, conn->primary->host, conn->primary->port );
}

MONGO_EXPORT int mongo_check_connection( mongo *conn ) {
    if( ! conn->connected )
        return MONGO_ERROR;

    return mongo_simple_int_command( conn, "admin", "ping", 1, NULL );
}

MONGO_EXPORT void mongo_disconnect( mongo *conn ) {
    if( ! conn->connected )
        return;

    if( conn->replica_set ) {
        conn->replica_set->primary_connected = 0;
        mongo_replica_set_free_list( &conn->replica_set->hosts );
        conn->replica_set->hosts = NULL;
    }

    mongo_env_close_socket( conn->sock );

    conn->sock = 0;
    conn->connected = 0;
}

MONGO_EXPORT void mongo_destroy( mongo *conn ) {
    mongo_disconnect( conn );

    if( conn->replica_set ) {
        mongo_replica_set_free_list( &conn->replica_set->seeds );
        mongo_replica_set_free_list( &conn->replica_set->hosts );
        bson_free( conn->replica_set->name );
        bson_free( conn->replica_set );
        conn->replica_set = NULL;
    }

    bson_free( conn->primary );

    mongo_clear_errors( conn );
}

/* Determine whether this BSON object is valid for the given operation.  */
static int mongo_bson_valid( mongo *conn, const bson *bson, int write ) {
    int size;

    if( ! bson->finished ) {
        conn->err = MONGO_BSON_NOT_FINISHED;
        return MONGO_ERROR;
    }

    size = bson_size( bson );
    if( size > conn->max_bson_size ) {
        conn->err = MONGO_BSON_TOO_LARGE;
        return MONGO_ERROR;
    }

    if( bson->err & BSON_NOT_UTF8 ) {
        conn->err = MONGO_BSON_INVALID;
        return MONGO_ERROR;
    }

    if( write ) {
        if( ( bson->err & BSON_FIELD_HAS_DOT ) ||
                ( bson->err & BSON_FIELD_INIT_DOLLAR ) ) {

            conn->err = MONGO_BSON_INVALID;
            return MONGO_ERROR;

        }
    }

    conn->err = MONGO_CONN_SUCCESS;

    return MONGO_OK;
}

/* Determine whether this BSON object is valid for the given operation.  */
static int mongo_cursor_bson_valid( mongo_cursor *cursor, const bson *bson ) {
    if( ! bson->finished ) {
        cursor->err = MONGO_CURSOR_BSON_ERROR;
        cursor->conn->err = MONGO_BSON_NOT_FINISHED;
        return MONGO_ERROR;
    }

    if( bson->err & BSON_NOT_UTF8 ) {
        cursor->err = MONGO_CURSOR_BSON_ERROR;
        cursor->conn->err = MONGO_BSON_INVALID;
        return MONGO_ERROR;
    }

    return MONGO_OK;
}

static int mongo_check_last_error( mongo *conn, const char *ns,
                                   mongo_write_concern *write_concern ) {
    bson response[1];
    bson_iterator it[1];
    int res = 0;
    char *cmd_ns = mongo_ns_to_cmd_db( ns );

    res = mongo_find_one( conn, cmd_ns, write_concern->cmd, bson_shared_empty( ), response );
    bson_free( cmd_ns );

    if (res == MONGO_OK &&
        (bson_find( it, response, "$err" ) == BSON_STRING ||
         bson_find( it, response, "err" ) == BSON_STRING)) {

        __mongo_set_error( conn, MONGO_WRITE_ERROR,
                           "See conn->lasterrstr for details.", 0 );
        mongo_set_last_error( conn, it, response );
        res = MONGO_ERROR;
    }

    bson_destroy( response );
    return res;
}

static int mongo_choose_write_concern( mongo *conn,
                                       mongo_write_concern *custom_write_concern,
                                       mongo_write_concern **write_concern ) {

    if( custom_write_concern ) {
        *write_concern = custom_write_concern;
    }
    else if( conn->write_concern ) {
        *write_concern = conn->write_concern;
    }
    if ( *write_concern && (*write_concern)->w < 1 ) {
        *write_concern = 0; /* do not generate getLastError request */
    }
    if( *write_concern && !((*write_concern)->cmd) ) {
        __mongo_set_error( conn, MONGO_WRITE_CONCERN_INVALID,
                           "Must call mongo_write_concern_finish() before using *write_concern.", 0 );
        return MONGO_ERROR;
    }
    else
        return MONGO_OK;
}


/*********************************************************************
CRUD API
**********************************************************************/

static int mongo_message_send_and_check_write_concern( mongo *conn, const char *ns, mongo_message *mm, mongo_write_concern *write_concern ) {
   if( write_concern ) {
        if( mongo_message_send( conn, mm ) == MONGO_ERROR ) {
            return MONGO_ERROR;
        }

        return mongo_check_last_error( conn, ns, write_concern );
    }
    else {
        return mongo_message_send( conn, mm );
    }
}

MONGO_EXPORT int mongo_insert( mongo *conn, const char *ns,
                               const bson *bson, mongo_write_concern *custom_write_concern ) {

    char *data;
    mongo_message *mm;
    mongo_write_concern *write_concern = NULL;

    if( mongo_validate_ns( conn, ns ) != MONGO_OK )
        return MONGO_ERROR;

    if( mongo_bson_valid( conn, bson, 1 ) != MONGO_OK ) {
        return MONGO_ERROR;
    }

    if( mongo_choose_write_concern( conn, custom_write_concern,
                                    &write_concern ) == MONGO_ERROR ) {
        return MONGO_ERROR;
    }

    mm = mongo_message_create( 16 /* header */
                               + 4 /* ZERO */
                               + strlen( ns )
                               + 1 + bson_size( bson )
                               , 0, 0, MONGO_OP_INSERT );
    if( mm == NULL ) {
        conn->err = MONGO_BSON_TOO_LARGE;
        return MONGO_ERROR;
    }

    data = &mm->data;
    data = mongo_data_append32( data, &ZERO );
    data = mongo_data_append( data, ns, strlen( ns ) + 1 );
    mongo_data_append( data, bson->data, bson_size( bson ) );

    return mongo_message_send_and_check_write_concern( conn, ns, mm, write_concern ); 
}

MONGO_EXPORT int mongo_insert_batch( mongo *conn, const char *ns,
                                     const bson **bsons, int count, mongo_write_concern *custom_write_concern,
                                     int flags ) {

    mongo_message *mm;
    mongo_write_concern *write_concern = NULL;
    int i;
    char *data;
    size_t overhead =  16 + 4 + strlen( ns ) + 1;
    size_t size = overhead;

    if( mongo_validate_ns( conn, ns ) != MONGO_OK )
        return MONGO_ERROR;

    for( i=0; i<count; i++ ) {
        size += bson_size( bsons[i] );
        if( mongo_bson_valid( conn, bsons[i], 1 ) != MONGO_OK )
            return MONGO_ERROR;
    }

    if( ( size - overhead ) > (size_t)conn->max_bson_size ) {
        conn->err = MONGO_BSON_TOO_LARGE;
        return MONGO_ERROR;
    }

    if( mongo_choose_write_concern( conn, custom_write_concern,
                                    &write_concern ) == MONGO_ERROR ) {
        return MONGO_ERROR;
    }

    mm = mongo_message_create( size , 0 , 0 , MONGO_OP_INSERT );
    if( mm == NULL ) {
        conn->err = MONGO_BSON_TOO_LARGE;
        return MONGO_ERROR;
    }

    data = &mm->data;
    if( flags & MONGO_CONTINUE_ON_ERROR )
        data = mongo_data_append32( data, &ONE );
    else
        data = mongo_data_append32( data, &ZERO );
    data = mongo_data_append( data, ns, strlen( ns ) + 1 );

    for( i=0; i<count; i++ ) {
        data = mongo_data_append( data, bsons[i]->data, bson_size( bsons[i] ) );
    }

    return mongo_message_send_and_check_write_concern( conn, ns, mm, write_concern ); 
}

MONGO_EXPORT int mongo_update( mongo *conn, const char *ns, const bson *cond,
                               const bson *op, int flags, mongo_write_concern *custom_write_concern ) {

    char *data;
    mongo_message *mm;
    mongo_write_concern *write_concern = NULL;

    /* Make sure that the op BSON is valid UTF-8.
     * TODO: decide whether to check cond as well.
     * */
    if( mongo_bson_valid( conn, ( bson * )op, 0 ) != MONGO_OK ) {
        return MONGO_ERROR;
    }

    if( mongo_choose_write_concern( conn, custom_write_concern,
                                    &write_concern ) == MONGO_ERROR ) {
        return MONGO_ERROR;
    }

    mm = mongo_message_create( 16 /* header */
                               + 4  /* ZERO */
                               + strlen( ns ) + 1
                               + 4  /* flags */
                               + bson_size( cond )
                               + bson_size( op )
                               , 0 , 0 , MONGO_OP_UPDATE );
    if( mm == NULL ) {
        conn->err = MONGO_BSON_TOO_LARGE;
        return MONGO_ERROR;
    }

    data = &mm->data;
    data = mongo_data_append32( data, &ZERO );
    data = mongo_data_append( data, ns, strlen( ns ) + 1 );
    data = mongo_data_append32( data, &flags );
    data = mongo_data_append( data, cond->data, bson_size( cond ) );
    mongo_data_append( data, op->data, bson_size( op ) );

    return mongo_message_send_and_check_write_concern( conn, ns, mm, write_concern ); 
}

MONGO_EXPORT int mongo_remove( mongo *conn, const char *ns, const bson *cond,
                               mongo_write_concern *custom_write_concern ) {

    char *data;
    mongo_message *mm;
    mongo_write_concern *write_concern = NULL;

    /* Make sure that the BSON is valid UTF-8.
     * TODO: decide whether to check cond as well.
     * */
    if( mongo_bson_valid( conn, ( bson * )cond, 0 ) != MONGO_OK ) {
        return MONGO_ERROR;
    }

    if( mongo_choose_write_concern( conn, custom_write_concern,
                                    &write_concern ) == MONGO_ERROR ) {
        return MONGO_ERROR;
    }

    mm = mongo_message_create( 16  /* header */
                               + 4  /* ZERO */
                               + strlen( ns ) + 1
                               + 4  /* ZERO */
                               + bson_size( cond )
                               , 0 , 0 , MONGO_OP_DELETE );
    if( mm == NULL ) {
        conn->err = MONGO_BSON_TOO_LARGE;
        return MONGO_ERROR;
    }

    data = &mm->data;
    data = mongo_data_append32( data, &ZERO );
    data = mongo_data_append( data, ns, strlen( ns ) + 1 );
    data = mongo_data_append32( data, &ZERO );
    mongo_data_append( data, cond->data, bson_size( cond ) );

    return mongo_message_send_and_check_write_concern( conn, ns, mm, write_concern ); 
}


/*********************************************************************
Write Concern API
**********************************************************************/

MONGO_EXPORT void mongo_write_concern_init( mongo_write_concern *write_concern ) {
    memset( write_concern, 0, sizeof( mongo_write_concern ) );
}

MONGO_EXPORT int mongo_write_concern_finish( mongo_write_concern *write_concern ) {
    bson *command;

    /* Destory any existing serialized write concern object and reuse it. */
    if( write_concern->cmd ) {
        bson_destroy( write_concern->cmd );
        command = write_concern->cmd;
    }
    else
        command = bson_alloc();

    if( !command ) {
        return MONGO_ERROR;
    }

    bson_init( command );

    bson_append_int( command, "getlasterror", 1 );

    if( write_concern->mode ) {
        bson_append_string( command, "w", write_concern->mode );
    }

    else if( write_concern->w && write_concern->w > 1 ) {
        bson_append_int( command, "w", write_concern->w );
    }

    if( write_concern->wtimeout ) {
        bson_append_int( command, "wtimeout", write_concern->wtimeout );
    }

    if( write_concern->j ) {
        bson_append_int( command, "j", write_concern->j );
    }

    if( write_concern->fsync ) {
        bson_append_int( command, "fsync", write_concern->fsync );
    }

    bson_finish( command );

    /* write_concern now owns the BSON command object.
     * This is freed in mongo_write_concern_destroy(). */
    write_concern->cmd = command;

    return MONGO_OK;
}

/**
 * Free the write_concern object (specifically, the BSON object that it holds).
 */
MONGO_EXPORT void mongo_write_concern_destroy( mongo_write_concern *write_concern ) {
    if( !write_concern )
        return;

    if( write_concern->cmd ) {
        bson_destroy( write_concern->cmd );
        bson_dealloc( write_concern->cmd );
        write_concern->cmd = NULL;
    }
}

MONGO_EXPORT void mongo_set_write_concern( mongo *conn,
        mongo_write_concern *write_concern ) {

    conn->write_concern = write_concern;
}

MONGO_EXPORT int mongo_write_concern_get_w( mongo_write_concern *write_concern ){    
    return write_concern->w;
}

MONGO_EXPORT int mongo_write_concern_get_wtimeout( mongo_write_concern *write_concern ){    
    return write_concern->wtimeout;
}

MONGO_EXPORT int mongo_write_concern_get_j( mongo_write_concern *write_concern ){    
    return write_concern->j;
}

MONGO_EXPORT int mongo_write_concern_get_fsync( mongo_write_concern *write_concern ){    
    return write_concern->fsync;
}

MONGO_EXPORT const char* mongo_write_concern_get_mode( mongo_write_concern *write_concern ){    
    return write_concern->mode;
}

MONGO_EXPORT bson* mongo_write_concern_get_cmd( mongo_write_concern *write_concern ){    
    return write_concern->cmd;
}

MONGO_EXPORT void mongo_write_concern_set_w( mongo_write_concern *write_concern, int w ){    
    write_concern->w = w;
}

MONGO_EXPORT void mongo_write_concern_set_wtimeout( mongo_write_concern *write_concern, int wtimeout ){    
    write_concern->wtimeout = wtimeout;

}

MONGO_EXPORT void mongo_write_concern_set_j( mongo_write_concern *write_concern, int j ){    
    write_concern->j = j;
}

MONGO_EXPORT void mongo_write_concern_set_fsync( mongo_write_concern *write_concern, int fsync ){    
    write_concern->fsync = fsync;

}

MONGO_EXPORT void mongo_write_concern_set_mode( mongo_write_concern *write_concern, const char* mode ){    
    write_concern->mode = mode;
}

static int mongo_cursor_op_query( mongo_cursor *cursor ) {
    int res;
    char *data;
    mongo_message *mm;
    bson temp;
    bson_iterator it;

    /* Clear any errors. */
    mongo_clear_errors( cursor->conn );

    /* Set up default values for query and fields, if necessary. */
    if( ! cursor->query )
        cursor->query = bson_shared_empty( );
    else if( mongo_cursor_bson_valid( cursor, cursor->query ) != MONGO_OK )
        return MONGO_ERROR;

    if( ! cursor->fields )
        cursor->fields = bson_shared_empty( );
    else if( mongo_cursor_bson_valid( cursor, cursor->fields ) != MONGO_OK )
        return MONGO_ERROR;

    mm = mongo_message_create( 16 + /* header */
                               4 + /*  options */
                               strlen( cursor->ns ) + 1 + /* ns */
                               4 + 4 + /* skip,return */
                               bson_size( cursor->query ) +
                               bson_size( cursor->fields ) ,
                               0 , 0 , MONGO_OP_QUERY );
    if( mm == NULL ) {
        return MONGO_ERROR;
    }

    data = &mm->data;
    data = mongo_data_append32( data , &cursor->options );
    data = mongo_data_append( data , cursor->ns , strlen( cursor->ns ) + 1 );
    data = mongo_data_append32( data , &cursor->skip );
    data = mongo_data_append32( data , &cursor->limit );
    data = mongo_data_append( data , cursor->query->data , bson_size( cursor->query ) );
    if ( cursor->fields )
        data = mongo_data_append( data , cursor->fields->data , bson_size( cursor->fields ) );

    bson_fatal_msg( ( data == ( ( char * )mm ) + mm->head.len ), "query building fail!" );

    res = mongo_message_send( cursor->conn , mm );
    if( res != MONGO_OK ) {
        return MONGO_ERROR;
    }

    res = mongo_read_response( cursor->conn, ( mongo_reply ** )&( cursor->reply ) );
    if( res != MONGO_OK ) {
        return MONGO_ERROR;
    }

    if( cursor->reply->fields.num == 1 ) {
        bson_init_finished_data( &temp, &cursor->reply->objs, 0 );
        if( bson_find( &it, &temp, "$err" ) ) {
            mongo_set_last_error( cursor->conn, &it, &temp );
            cursor->err = MONGO_CURSOR_QUERY_FAIL;
            return MONGO_ERROR;
        }
    }

    cursor->seen += cursor->reply->fields.num;
    cursor->flags |= MONGO_CURSOR_QUERY_SENT;
    return MONGO_OK;
}

static int mongo_cursor_get_more( mongo_cursor *cursor ) {
    int res;

    if( cursor->limit > 0 && cursor->seen >= cursor->limit ) {
        cursor->err = MONGO_CURSOR_EXHAUSTED;
        return MONGO_ERROR;
    }
    else if( ! cursor->reply ) {
        cursor->err = MONGO_CURSOR_INVALID;
        return MONGO_ERROR;
    }
    else if( ! cursor->reply->fields.cursorID ) {
        cursor->err = MONGO_CURSOR_EXHAUSTED;
        return MONGO_ERROR;
    }
    else {
        char *data;
        size_t sl = strlen( cursor->ns )+1;
        int limit = 0;
        mongo_message *mm;

        if( cursor->limit > 0 )
            limit = cursor->limit - cursor->seen;

        mm = mongo_message_create( 16 /*header*/
                                   +4 /*ZERO*/
                                   +sl
                                   +4 /*numToReturn*/
                                   +8 /*cursorID*/
                                   , 0, 0, MONGO_OP_GET_MORE );
        if( mm == NULL ) {
            return MONGO_ERROR;
        }

        data = &mm->data;
        data = mongo_data_append32( data, &ZERO );
        data = mongo_data_append( data, cursor->ns, sl );
        data = mongo_data_append32( data, &limit );
        mongo_data_append64( data, &cursor->reply->fields.cursorID );

        bson_free( cursor->reply );
        res = mongo_message_send( cursor->conn, mm );
        if( res != MONGO_OK ) {
            mongo_cursor_destroy( cursor );
            return MONGO_ERROR;
        }

        res = mongo_read_response( cursor->conn, &( cursor->reply ) );
        if( res != MONGO_OK ) {
            mongo_cursor_destroy( cursor );
            return MONGO_ERROR;
        }
        cursor->current.data = NULL;
        cursor->seen += cursor->reply->fields.num;

        return MONGO_OK;
    }
}

MONGO_EXPORT mongo_cursor *mongo_find( mongo *conn, const char *ns, const bson *query,
                                       const bson *fields, int limit, int skip, int options ) {

    mongo_cursor *cursor = mongo_cursor_alloc();
    mongo_cursor_init( cursor, conn, ns );
    cursor->flags |= MONGO_CURSOR_MUST_FREE;

    mongo_cursor_set_query( cursor, query );
    mongo_cursor_set_fields( cursor, fields );
    mongo_cursor_set_limit( cursor, limit );
    mongo_cursor_set_skip( cursor, skip );
    mongo_cursor_set_options( cursor, options );

    if( mongo_cursor_op_query( cursor ) == MONGO_OK )
        return cursor;
    else {
        mongo_cursor_destroy( cursor );
        return NULL;
    }
}

MONGO_EXPORT int mongo_find_one( mongo *conn, const char *ns, const bson *query,
                                 const bson *fields, bson *out ) {
    int ret;
    mongo_cursor cursor[1];
    mongo_cursor_init( cursor, conn, ns );
    mongo_cursor_set_query( cursor, query );
    mongo_cursor_set_fields( cursor, fields );
    mongo_cursor_set_limit( cursor, 1 );

    ret = mongo_cursor_next(cursor);
    if (ret == MONGO_OK && out)
        ret = bson_copy(out, &cursor->current);
    if (ret != MONGO_OK && out)
        bson_init_zero(out);

    mongo_cursor_destroy( cursor );
    return ret;
}

MONGO_EXPORT void mongo_cursor_init( mongo_cursor *cursor, mongo *conn, const char *ns ) {
    memset( cursor, 0, sizeof( mongo_cursor ) );
    cursor->conn = conn;
    cursor->ns = ( const char * )bson_malloc( strlen( ns ) + 1 );
    strncpy( ( char * )cursor->ns, ns, strlen( ns ) + 1 );
    cursor->current.data = NULL;
}

MONGO_EXPORT void mongo_cursor_set_query( mongo_cursor *cursor, const bson *query ) {
    cursor->query = query;
}

MONGO_EXPORT void mongo_cursor_set_fields( mongo_cursor *cursor, const bson *fields ) {
    cursor->fields = fields;
}

MONGO_EXPORT void mongo_cursor_set_skip( mongo_cursor *cursor, int skip ) {
    cursor->skip = skip;
}

MONGO_EXPORT void mongo_cursor_set_limit( mongo_cursor *cursor, int limit ) {
    cursor->limit = limit;
}

MONGO_EXPORT void mongo_cursor_set_options( mongo_cursor *cursor, int options ) {
    cursor->options = options;
}

MONGO_EXPORT const char *mongo_cursor_data( mongo_cursor *cursor ) {
    return cursor->current.data;
}

MONGO_EXPORT const bson *mongo_cursor_bson( mongo_cursor *cursor ) {
    return (const bson *)&(cursor->current);
}

MONGO_EXPORT int mongo_cursor_next( mongo_cursor *cursor ) {
    char *next_object;
    char *message_end;

    if( cursor == NULL ) return MONGO_ERROR;

    if( ! ( cursor->flags & MONGO_CURSOR_QUERY_SENT ) )
        if( mongo_cursor_op_query( cursor ) != MONGO_OK )
            return MONGO_ERROR;

    if( !cursor->reply )
        return MONGO_ERROR;

    /* no data */
    if ( cursor->reply->fields.num == 0 ) {

        /* Special case for tailable cursors. */
        if( cursor->reply->fields.cursorID ) {
            if( ( mongo_cursor_get_more( cursor ) != MONGO_OK ) ||
                    cursor->reply->fields.num == 0 ) {
                return MONGO_ERROR;
            }
        }

        else
            return MONGO_ERROR;
    }

    /* first */
    if ( cursor->current.data == NULL ) {
        bson_init_finished_data( &cursor->current, &cursor->reply->objs, 0 );
        return MONGO_OK;
    }

    next_object = cursor->current.data + bson_size( &cursor->current );
    message_end = ( char * )cursor->reply + cursor->reply->head.len;

    if ( next_object >= message_end ) {
        if( mongo_cursor_get_more( cursor ) != MONGO_OK )
            return MONGO_ERROR;

        if ( cursor->reply->fields.num == 0 ) {
            /* Special case for tailable cursors. */
            if ( cursor->reply->fields.cursorID ) {
                cursor->err = MONGO_CURSOR_PENDING;
                return MONGO_ERROR;
            }
            else
                return MONGO_ERROR;
        }

        bson_init_finished_data( &cursor->current, &cursor->reply->objs, 0 );
    }
    else {
        bson_init_finished_data( &cursor->current, next_object, 0 );
    }

    return MONGO_OK;
}

MONGO_EXPORT int mongo_cursor_destroy( mongo_cursor *cursor ) {
    int result = MONGO_OK;
    char *data;

    if ( !cursor ) return result;

    /* Kill cursor if live. */
    if ( cursor->reply && cursor->reply->fields.cursorID ) {
        mongo *conn = cursor->conn;
        mongo_message *mm = mongo_message_create( 16 /*header*/
                            +4 /*ZERO*/
                            +4 /*numCursors*/
                            +8 /*cursorID*/
                            , 0, 0, MONGO_OP_KILL_CURSORS );
        if( mm == NULL ) {
            return MONGO_ERROR;
        }
        data = &mm->data;
        data = mongo_data_append32( data, &ZERO );
        data = mongo_data_append32( data, &ONE );
        mongo_data_append64( data, &cursor->reply->fields.cursorID );

        result = mongo_message_send( conn, mm );
    }

    bson_free( cursor->reply );
    bson_free( ( void * )cursor->ns );

    if( cursor->flags & MONGO_CURSOR_MUST_FREE )
        bson_free( cursor );

    return result;
}

/* MongoDB Helper Functions */

#define INDEX_NAME_BUFFER_SIZE 255
#define INDEX_NAME_MAX_LENGTH (INDEX_NAME_BUFFER_SIZE - 1)

MONGO_EXPORT int mongo_create_index( mongo *conn, const char *ns, const bson *key, const char *name, int options, bson *out ) {
    bson b;
    bson_iterator it;
    char default_name[INDEX_NAME_BUFFER_SIZE] = {'\0'};
    size_t len = 0;
    size_t remaining;
    char idxns[1024];
    char *p = NULL;

    if ( !name ) {
        bson_iterator_init( &it, key );
        while( len < INDEX_NAME_MAX_LENGTH && bson_iterator_next( &it ) ) {
            remaining = INDEX_NAME_MAX_LENGTH - len;
            strncat( default_name, bson_iterator_key( &it ), remaining );
            len = strlen( default_name );
            remaining = INDEX_NAME_MAX_LENGTH - len;
            strncat( default_name, ( bson_iterator_int( &it ) < 0 ) ? "_-1" : "_1", remaining );
            len = strlen( default_name );
        }
    }

    bson_init( &b );
    bson_append_bson( &b, "key", key );
    bson_append_string( &b, "ns", ns );
    bson_append_string( &b, "name", name ? name : default_name );
    if ( options & MONGO_INDEX_UNIQUE )
        bson_append_bool( &b, "unique", 1 );
    if ( options & MONGO_INDEX_DROP_DUPS )
        bson_append_bool( &b, "dropDups", 1 );
    if ( options & MONGO_INDEX_BACKGROUND )
        bson_append_bool( &b, "background", 1 );
    if ( options & MONGO_INDEX_SPARSE )
        bson_append_bool( &b, "sparse", 1 );
    bson_finish( &b );

    strncpy( idxns, ns, 1024-16 );
    p = strchr( idxns, '.' );
    if ( !p ) {
	    bson_destroy( &b );
	    return MONGO_ERROR;
    }
    strcpy( p, ".system.indexes" );
    if ( mongo_insert( conn, idxns, &b, NULL ) != MONGO_OK) {
	    bson_destroy( &b );
	    return MONGO_ERROR;
    }
    bson_destroy( &b );

    *strchr( idxns, '.' ) = '\0'; /* just db not ns */
    return mongo_cmd_get_last_error( conn, idxns, out );
}

MONGO_EXPORT bson_bool_t mongo_create_simple_index( mongo *conn, const char *ns, const char *field, int options, bson *out ) {
    bson b[1];
    bson_bool_t success;

    bson_init( b );
    bson_append_int( b, field, 1 );
    bson_finish( b );

    success = mongo_create_index( conn, ns, b, NULL, options, out );
    bson_destroy( b );
    return success;
}

MONGO_EXPORT int mongo_create_capped_collection( mongo *conn, const char *db,
        const char *collection, int size, int max, bson *out ) {

    bson b[1];
    int result;

    bson_init( b );
    bson_append_string( b, "create", collection );
    bson_append_bool( b, "capped", 1 );
    bson_append_int( b, "size", size );
    if( max > 0 )
        bson_append_int( b, "max", size );
    bson_finish( b );

    result = mongo_run_command( conn, db, b, out );

    bson_destroy( b );

    return result;
}

MONGO_EXPORT double mongo_count( mongo *conn, const char *db, const char *coll, const bson *query ) {
    bson cmd[1];
    bson out[1];
    double count = MONGO_ERROR;  // -1

    bson_init( cmd );
    bson_append_string( cmd, "count", coll );
    if ( query && bson_size( query ) > 5 ) /* not empty */
        bson_append_bson( cmd, "query", query );
    bson_finish( cmd );

    if( mongo_run_command( conn, db, cmd, out ) == MONGO_OK ) {
        bson_iterator it[1];
        if( bson_find( it, out, "n" ) )
            count = bson_iterator_double( it );
    }
    bson_destroy( out );
    bson_destroy( cmd );
    return count;
}

MONGO_EXPORT int mongo_run_command( mongo *conn, const char *db, const bson *command,
                                    bson *out ) {
    bson response[1];
    bson_iterator it[1];
    size_t sl = strlen( db );
    char *ns = (char*) bson_malloc( sl + 5 + 1 ); /* ".$cmd" + nul */
    int res = 0;

    strcpy( ns, db );
    strcpy( ns+sl, ".$cmd" );

    res = mongo_find_one( conn, ns, command, bson_shared_empty( ), response );
    bson_free( ns );

    if (res == MONGO_OK && (!bson_find( it, response, "ok" ) || !bson_iterator_bool( it )) ) {
        conn->err = MONGO_COMMAND_FAILED;
        bson_destroy( response );
        res = MONGO_ERROR;
    }

    if (out)
        if (res == MONGO_OK)
            *out = *response;
        else
            bson_init_zero(out);
    else if (res == MONGO_OK)
        bson_destroy(response);

    return res;
}

MONGO_EXPORT int mongo_simple_int_command( mongo *conn, const char *db,
        const char *cmdstr, int arg, bson *out ) {

    bson cmd[1];
    int result;

    bson_init( cmd );
    bson_append_int( cmd, cmdstr, arg );
    bson_finish( cmd );

    result = mongo_run_command( conn, db, cmd, out );

    bson_destroy( cmd );

    return result;
}

MONGO_EXPORT int mongo_simple_str_command( mongo *conn, const char *db,
        const char *cmdstr, const char *arg, bson *out ) {

    int result;
    bson cmd;
    bson_init( &cmd );
    bson_append_string( &cmd, cmdstr, arg );
    bson_finish( &cmd );

    result = mongo_run_command( conn, db, &cmd, out );

    bson_destroy( &cmd );

    return result;
}

MONGO_EXPORT int mongo_cmd_drop_db( mongo *conn, const char *db ) {
    return mongo_simple_int_command( conn, db, "dropDatabase", 1, NULL );
}

MONGO_EXPORT int mongo_cmd_drop_collection( mongo *conn, const char *db, const char *collection, bson *out ) {
    return mongo_simple_str_command( conn, db, "drop", collection, out );
}

MONGO_EXPORT void mongo_cmd_reset_error( mongo *conn, const char *db ) {
    mongo_simple_int_command( conn, db, "reseterror", 1, NULL );
}

static int mongo_cmd_get_error_helper( mongo *conn, const char *db,
                                       bson *realout, const char *cmdtype ) {

    bson out[1];
    bson_bool_t haserror = 0;

    /* Reset last error codes. */
    mongo_clear_errors( conn );
    bson_init_zero(out);

    /* If there's an error, store its code and string in the connection object. */
    if( mongo_simple_int_command( conn, db, cmdtype, 1, out ) == MONGO_OK ) {
        bson_iterator it[1];
        haserror = ( bson_find( it, out, "err" ) != BSON_NULL );
        if( haserror ) mongo_set_last_error( conn, it, out );
    }

    if( realout )
        *realout = *out; /* transfer of ownership */
    else
        bson_destroy( out );

    if( haserror )
        return MONGO_ERROR;
    else
        return MONGO_OK;
}

MONGO_EXPORT int mongo_cmd_get_prev_error( mongo *conn, const char *db, bson *out ) {
    return mongo_cmd_get_error_helper( conn, db, out, "getpreverror" );
}

MONGO_EXPORT int mongo_cmd_get_last_error( mongo *conn, const char *db, bson *out ) {
    return mongo_cmd_get_error_helper( conn, db, out, "getlasterror" );
}

MONGO_EXPORT bson_bool_t mongo_cmd_ismaster( mongo *conn, bson *realout ) {
    bson out[1];
    bson_bool_t ismaster = 0;

    int res = mongo_simple_int_command( conn, "admin", "ismaster", 1, out );
    if (res == MONGO_OK) {
        bson_iterator it[1];
        bson_find( it, out, "ismaster" );
        ismaster = bson_iterator_bool( it );
        if (realout)
            *realout = *out; /* transfer of ownership */
        else
            bson_destroy( out );
    } else if (realout)
        bson_init_zero(realout);

    return ismaster;
}

static void digest2hex( mongo_md5_byte_t digest[16], char hex_digest[33] ) {
    static const char hex[16] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
    int i;
    for ( i=0; i<16; i++ ) {
        hex_digest[2*i]     = hex[( digest[i] & 0xf0 ) >> 4];
        hex_digest[2*i + 1] = hex[ digest[i] & 0x0f      ];
    }
    hex_digest[32] = '\0';
}

static int mongo_pass_digest( mongo *conn, const char *user, const char *pass, char hex_digest[33] ) {
    mongo_md5_state_t st;
    mongo_md5_byte_t digest[16];

    if( strlen( user ) >= INT32_MAX || strlen( pass ) >= INT32_MAX ) {
        conn->err = MONGO_BSON_TOO_LARGE;
        return MONGO_ERROR;
    }
    mongo_md5_init( &st );
    mongo_md5_append( &st, ( const mongo_md5_byte_t * )user, ( int )strlen( user ) );
    mongo_md5_append( &st, ( const mongo_md5_byte_t * )":mongo:", 7 );
    mongo_md5_append( &st, ( const mongo_md5_byte_t * )pass, ( int )strlen( pass ) );
    mongo_md5_finish( &st, digest );
    digest2hex( digest, hex_digest );
    
    return MONGO_OK;
}

MONGO_EXPORT int mongo_cmd_add_user( mongo *conn, const char *db, const char *user, const char *pass ) {
    bson user_obj;
    bson pass_obj;
    char hex_digest[33];
    char *ns = bson_malloc( strlen( db ) + strlen( ".system.users" ) + 1 );
    int res;

    strcpy( ns, db );
    strcpy( ns+strlen( db ), ".system.users" );

    res = mongo_pass_digest( conn, user, pass, hex_digest );
    if (res != MONGO_OK) {
        free(ns);
        return res;
    }

    bson_init( &user_obj );
    bson_append_string( &user_obj, "user", user );
    bson_finish( &user_obj );

    bson_init( &pass_obj );
    bson_append_start_object( &pass_obj, "$set" );
    bson_append_string( &pass_obj, "pwd", hex_digest );
    bson_append_finish_object( &pass_obj );
    bson_finish( &pass_obj );

    res = mongo_update( conn, ns, &user_obj, &pass_obj, MONGO_UPDATE_UPSERT, NULL );

    bson_free( ns );
    bson_destroy( &user_obj );
    bson_destroy( &pass_obj );

    return res;
}

MONGO_EXPORT bson_bool_t mongo_cmd_authenticate( mongo *conn, const char *db, const char *user, const char *pass ) {
    bson from_db;
    bson cmd;
    const char *nonce;
    int result;
    bson_iterator it;

    mongo_md5_state_t st;
    mongo_md5_byte_t digest[16];
    char hex_digest[33];

    if( mongo_simple_int_command( conn, db, "getnonce", 1, &from_db ) != MONGO_OK )
        return MONGO_ERROR;
    
    bson_find( &it, &from_db, "nonce" );
    nonce = bson_iterator_string( &it );

    result = mongo_pass_digest( conn, user, pass, hex_digest );
    if( result != MONGO_OK ) {
        return result;
    }

    if( strlen( nonce ) >= INT32_MAX || strlen( user ) >= INT32_MAX ) {
        conn->err = MONGO_BSON_TOO_LARGE;
        return MONGO_ERROR;
    }

    mongo_md5_init( &st );
    mongo_md5_append( &st, ( const mongo_md5_byte_t * )nonce, ( int )strlen( nonce ) );
    mongo_md5_append( &st, ( const mongo_md5_byte_t * )user, ( int )strlen( user ) );
    mongo_md5_append( &st, ( const mongo_md5_byte_t * )hex_digest, 32 );
    mongo_md5_finish( &st, digest );
    digest2hex( digest, hex_digest );

    bson_init( &cmd );
    bson_append_int( &cmd, "authenticate", 1 );
    bson_append_string( &cmd, "user", user );
    bson_append_string( &cmd, "nonce", nonce );
    bson_append_string( &cmd, "key", hex_digest );
    bson_finish( &cmd );

    result = mongo_run_command( conn, db, &cmd, NULL );

    bson_destroy( &from_db );
    bson_destroy( &cmd );

    return result;
}
