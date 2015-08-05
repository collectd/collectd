/* bson.c */

/*    Copyright 2009, 2010 10gen Inc.
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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>

#include "bson.h"
#include "encoding.h"

const int initialBufferSize = 128;

/* only need one of these */
static const int zero = 0;

/* Static data to use with bson_init_empty( ) and bson_shared_empty( ) */
static char bson_shared_empty_data[] = {5,0,0,0,0};

/* Custom standard function pointers. */
void *( *bson_malloc_func )( size_t ) = malloc;
void *( *bson_realloc_func )( void *, size_t ) = realloc;
void  ( *bson_free_func )( void * ) = free;
#ifdef R_SAFETY_NET
bson_printf_func bson_printf;
#else
bson_printf_func bson_printf = printf;
#endif
bson_fprintf_func bson_fprintf = fprintf;
bson_sprintf_func bson_sprintf = sprintf;

static int _bson_errprintf( const char *, ... );
bson_printf_func bson_errprintf = _bson_errprintf;

static void _bson_zero( bson *b );
static size_t _bson_position( const bson *b );

/* ObjectId fuzz functions. */
static int ( *oid_fuzz_func )( void ) = NULL;
static int ( *oid_inc_func )( void )  = NULL;

/* ----------------------------
   READING
   ------------------------------ */

MONGO_EXPORT void bson_init_zero(bson* b) {
    memset(b, 0, sizeof(bson) - sizeof(b->stack));
}

MONGO_EXPORT bson* bson_alloc( void ) {
    return ( bson* )bson_malloc( sizeof( bson ) );
}

MONGO_EXPORT void bson_dealloc( bson* b ) {
    bson_free( b );
}

/* When passed a char * of a BSON data block, returns its reported size */
static int bson_finished_data_size( const char *data ) {
    int i;
    bson_little_endian32( &i, data );
    return i;
}

int bson_init_finished_data( bson *b, char *data, bson_bool_t ownsData ) {
    _bson_zero( b );
    b->data = data;
    b->dataSize = bson_finished_data_size( data );
    b->ownsData = ownsData;
    b->finished = 1;
    return BSON_OK;
}

int bson_init_finished_data_with_copy( bson *b, const char *data ) {
    int dataSize = bson_finished_data_size( data );
    if ( bson_init_size( b, dataSize ) == BSON_ERROR ) return BSON_ERROR;
    memcpy( b->data, data, dataSize );
    b->finished = 1;
    return BSON_OK;
}

MONGO_EXPORT bson_bool_t bson_init_empty( bson *obj ) {
    bson_init_finished_data( obj, bson_shared_empty_data, 0 );
    return BSON_OK;
}

MONGO_EXPORT const bson *bson_shared_empty( void ) {
    static const bson shared_empty = { bson_shared_empty_data, bson_shared_empty_data, 128, 1, 0 };
    return &shared_empty;
}

MONGO_EXPORT int bson_copy( bson *out, const bson *in ) {
    if ( !out || !in ) return BSON_ERROR;
    if ( !in->finished ) return BSON_ERROR;
    return bson_init_finished_data_with_copy( out, in->data );
}

MONGO_EXPORT int bson_size( const bson *b ) {
    int i;
    if ( ! b || ! b->data )
        return 0;
    bson_little_endian32( &i, b->data );
    return i;
}

static size_t _bson_position( const bson *b ) {
    return b->cur - b->data;
}

MONGO_EXPORT size_t bson_buffer_size( const bson *b ) {
    return _bson_position(b) + 1;
}


MONGO_EXPORT const char *bson_data( const bson *b ) {
    return (const char *)b->data;
}

static char hexbyte( char hex ) {
    if (hex >= '0' && hex <= '9')
        return (hex - '0');
    else if (hex >= 'A' && hex <= 'F')
        return (hex - 'A' + 10);
    else if (hex >= 'a' && hex <= 'f')
        return (hex - 'a' + 10);
    else
        return 0x0;
}

MONGO_EXPORT void bson_oid_from_string( bson_oid_t *oid, const char *str ) {
    int i;
    for ( i=0; i<12; i++ ) {
        oid->bytes[i] = ( hexbyte( str[2*i] ) << 4 ) | hexbyte( str[2*i + 1] );
    }
}

MONGO_EXPORT void bson_oid_to_string( const bson_oid_t *oid, char *str ) {
    static const char hex[16] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
    int i;
    for ( i=0; i<12; i++ ) {
        str[2*i]     = hex[( oid->bytes[i] & 0xf0 ) >> 4];
        str[2*i + 1] = hex[ oid->bytes[i] & 0x0f      ];
    }
    str[24] = '\0';
}

MONGO_EXPORT void bson_set_oid_fuzz( int ( *func )( void ) ) {
    oid_fuzz_func = func;
}

MONGO_EXPORT void bson_set_oid_inc( int ( *func )( void ) ) {
    oid_inc_func = func;
}

MONGO_EXPORT void bson_oid_gen( bson_oid_t *oid ) {
    static int incr = 0;
    static int fuzz = 0;
    int i;
    time_t t = time( NULL );

    if( oid_inc_func )
        i = oid_inc_func();
    else
        i = incr++;

    if ( !fuzz ) {
        if ( oid_fuzz_func )
            fuzz = oid_fuzz_func();
        else {
            srand( ( int )t );
            fuzz = rand();
        }
    }

    bson_big_endian32( &oid->ints[0], &t );
    oid->ints[1] = fuzz;
    bson_big_endian32( &oid->ints[2], &i );
}

MONGO_EXPORT time_t bson_oid_generated_time( bson_oid_t *oid ) {
    time_t out = 0;
    bson_big_endian32( &out, &oid->ints[0] );

    return out;
}

MONGO_EXPORT void bson_print( const bson *b ) {
    bson_print_raw( b->data , 0 );
}

MONGO_EXPORT void bson_print_raw( const char *data , int depth ) {
    bson_iterator i;
    const char *key;
    int temp;
    bson_timestamp_t ts;
    char oidhex[25];
    bson scope;
    bson_iterator_from_buffer( &i, data );

    while ( bson_iterator_next( &i ) ) {
        bson_type t = bson_iterator_type( &i );
        if ( t == 0 )
            break;
        key = bson_iterator_key( &i );

        for ( temp=0; temp<=depth; temp++ )
            bson_printf( "\t" );
        bson_printf( "%s : %d \t " , key , t );
        switch ( t ) {
        case BSON_DOUBLE:
            bson_printf( "%f" , bson_iterator_double( &i ) );
            break;
        case BSON_STRING:
            bson_printf( "%s" , bson_iterator_string( &i ) );
            break;
        case BSON_SYMBOL:
            bson_printf( "SYMBOL: %s" , bson_iterator_string( &i ) );
            break;
        case BSON_OID:
            bson_oid_to_string( bson_iterator_oid( &i ), oidhex );
            bson_printf( "%s" , oidhex );
            break;
        case BSON_BOOL:
            bson_printf( "%s" , bson_iterator_bool( &i ) ? "true" : "false" );
            break;
        case BSON_DATE:
            bson_printf( "%ld" , ( long int )bson_iterator_date( &i ) );
            break;
        case BSON_BINDATA:
            bson_printf( "BSON_BINDATA" );
            break;
        case BSON_UNDEFINED:
            bson_printf( "BSON_UNDEFINED" );
            break;
        case BSON_NULL:
            bson_printf( "BSON_NULL" );
            break;
        case BSON_REGEX:
            bson_printf( "BSON_REGEX: %s", bson_iterator_regex( &i ) );
            break;
        case BSON_CODE:
            bson_printf( "BSON_CODE: %s", bson_iterator_code( &i ) );
            break;
        case BSON_CODEWSCOPE:
            bson_printf( "BSON_CODE_W_SCOPE: %s", bson_iterator_code( &i ) );
            bson_iterator_code_scope_init( &i, &scope, 0 );
            bson_printf( "\n\t SCOPE: " );
            bson_print( &scope );
            bson_destroy( &scope );
            break;
        case BSON_INT:
            bson_printf( "%d" , bson_iterator_int( &i ) );
            break;
        case BSON_LONG:
            bson_printf( "%lld" , ( uint64_t )bson_iterator_long( &i ) );
            break;
        case BSON_TIMESTAMP:
            ts = bson_iterator_timestamp( &i );
            bson_printf( "i: %d, t: %d", ts.i, ts.t );
            break;
        case BSON_OBJECT:
        case BSON_ARRAY:
            bson_printf( "\n" );
            bson_print_raw( bson_iterator_value( &i ) , depth + 1 );
            break;
        default:
            bson_errprintf( "can't print type : %d\n" , t );
        }
        bson_printf( "\n" );
    }
}

/* ----------------------------
   ITERATOR
   ------------------------------ */

MONGO_EXPORT bson_iterator* bson_iterator_alloc( void ) {
    return ( bson_iterator* )bson_malloc( sizeof( bson_iterator ) );
}

MONGO_EXPORT void bson_iterator_dealloc( bson_iterator* i ) {
    bson_free( i );
}

MONGO_EXPORT void bson_iterator_init( bson_iterator *i, const bson *b ) {
    i->cur = b->data + 4;
    i->first = 1;
}

MONGO_EXPORT void bson_iterator_from_buffer( bson_iterator *i, const char *buffer ) {
    i->cur = buffer + 4;
    i->first = 1;
}

MONGO_EXPORT bson_type bson_find( bson_iterator *it, const bson *obj, const char *name ) {
    bson_iterator_init( it, (bson *)obj );
    while( bson_iterator_next( it ) ) {
        if ( strcmp( name, bson_iterator_key( it ) ) == 0 )
            break;
    }
    return bson_iterator_type( it );
}

MONGO_EXPORT bson_bool_t bson_iterator_more( const bson_iterator *i ) {
    return *( i->cur );
}

MONGO_EXPORT bson_type bson_iterator_next( bson_iterator *i ) {
    size_t ds;

    if ( i->first ) {
        i->first = 0;
        return ( bson_type )( *i->cur );
    }

    switch ( bson_iterator_type( i ) ) {
    case BSON_EOO:
        return BSON_EOO; /* don't advance */
    case BSON_UNDEFINED:
    case BSON_NULL:
    case BSON_MINKEY:
    case BSON_MAXKEY:
        ds = 0;
        break;
    case BSON_BOOL:
        ds = 1;
        break;
    case BSON_INT:
        ds = 4;
        break;
    case BSON_LONG:
    case BSON_DOUBLE:
    case BSON_TIMESTAMP:
    case BSON_DATE:
        ds = 8;
        break;
    case BSON_OID:
        ds = 12;
        break;
    case BSON_STRING:
    case BSON_SYMBOL:
    case BSON_CODE:
        ds = 4 + bson_iterator_int_raw( i );
        break;
    case BSON_BINDATA:
        ds = 5 + bson_iterator_int_raw( i );
        break;
    case BSON_OBJECT:
    case BSON_ARRAY:
    case BSON_CODEWSCOPE:
        ds = bson_iterator_int_raw( i );
        break;
    case BSON_DBREF:
        ds = 4+12 + bson_iterator_int_raw( i );
        break;
    case BSON_REGEX: {
        const char *s = bson_iterator_value( i );
        const char *p = s;
        p += strlen( p )+1;
        p += strlen( p )+1;
        ds = p-s;
        break;
    }

    default: {
        char msg[] = "unknown type: 000000000000";
        bson_numstr( msg+14, ( unsigned )( i->cur[0] ) );
        bson_fatal_msg( 0, msg );
        return 0;
    }
    }

    i->cur += 1 + strlen( i->cur + 1 ) + 1 + ds;

    return ( bson_type )( *i->cur );
}

MONGO_EXPORT bson_type bson_iterator_type( const bson_iterator *i ) {
    // problem to convert 0xFF to 255
    return ( bson_type )( unsigned char )i->cur[0];
}

MONGO_EXPORT const char *bson_iterator_key( const bson_iterator *i ) {
    return i->cur + 1;
}

MONGO_EXPORT const char *bson_iterator_value( const bson_iterator *i ) {
    const char *t = i->cur + 1;
    t += strlen( t ) + 1;
    return t;
}

/* types */

int bson_iterator_int_raw( const bson_iterator *i ) {
    int out;
    bson_little_endian32( &out, bson_iterator_value( i ) );
    return out;
}

double bson_iterator_double_raw( const bson_iterator *i ) {
    double out;
    bson_little_endian64( &out, bson_iterator_value( i ) );
    return out;
}

int64_t bson_iterator_long_raw( const bson_iterator *i ) {
    int64_t out;
    bson_little_endian64( &out, bson_iterator_value( i ) );
    return out;
}

bson_bool_t bson_iterator_bool_raw( const bson_iterator *i ) {
    return bson_iterator_value( i )[0];
}

MONGO_EXPORT bson_oid_t *bson_iterator_oid( const bson_iterator *i ) {
    return ( bson_oid_t * )bson_iterator_value( i );
}

MONGO_EXPORT int bson_iterator_int( const bson_iterator *i ) {
    switch ( bson_iterator_type( i ) ) {
    case BSON_INT:
        return bson_iterator_int_raw( i );
    case BSON_LONG:
        return ( int )bson_iterator_long_raw( i );
    case BSON_DOUBLE:
        return ( int )bson_iterator_double_raw( i );
    default:
        return 0;
    }
}

MONGO_EXPORT double bson_iterator_double( const bson_iterator *i ) {
    switch ( bson_iterator_type( i ) ) {
    case BSON_INT:
        return bson_iterator_int_raw( i );
    case BSON_LONG:
        return ( double )bson_iterator_long_raw( i );
    case BSON_DOUBLE:
        return bson_iterator_double_raw( i );
    default:
        return 0;
    }
}

MONGO_EXPORT int64_t bson_iterator_long( const bson_iterator *i ) {
    switch ( bson_iterator_type( i ) ) {
    case BSON_INT:
        return bson_iterator_int_raw( i );
    case BSON_LONG:
        return bson_iterator_long_raw( i );
    case BSON_DOUBLE:
        return ( int64_t)bson_iterator_double_raw( i );
    default:
        return 0;
    }
}

MONGO_EXPORT bson_timestamp_t bson_iterator_timestamp( const bson_iterator *i ) {
    bson_timestamp_t ts;
    bson_little_endian32( &( ts.i ), bson_iterator_value( i ) );
    bson_little_endian32( &( ts.t ), bson_iterator_value( i ) + 4 );
    return ts;
}


MONGO_EXPORT int bson_iterator_timestamp_time( const bson_iterator *i ) {
    int time;
    bson_little_endian32( &time, bson_iterator_value( i ) + 4 );
    return time;
}


MONGO_EXPORT int bson_iterator_timestamp_increment( const bson_iterator *i ) {
    int increment;
    bson_little_endian32( &increment, bson_iterator_value( i ) );
    return increment;
}


MONGO_EXPORT bson_bool_t bson_iterator_bool( const bson_iterator *i ) {
    switch ( bson_iterator_type( i ) ) {
    case BSON_BOOL:
        return bson_iterator_bool_raw( i );
    case BSON_INT:
        return bson_iterator_int_raw( i ) != 0;
    case BSON_LONG:
        return bson_iterator_long_raw( i ) != 0;
    case BSON_DOUBLE:
        return bson_iterator_double_raw( i ) != 0;
    case BSON_EOO:
    case BSON_NULL:
        return 0;
    default:
        return 1;
    }
}

MONGO_EXPORT const char *bson_iterator_string( const bson_iterator *i ) {
    switch ( bson_iterator_type( i ) ) {
    case BSON_STRING:
    case BSON_SYMBOL:
        return bson_iterator_value( i ) + 4;
    default:
        return "";
    }
}

int bson_iterator_string_len( const bson_iterator *i ) {
    return bson_iterator_int_raw( i );
}

MONGO_EXPORT const char *bson_iterator_code( const bson_iterator *i ) {
    switch ( bson_iterator_type( i ) ) {
    case BSON_STRING:
    case BSON_CODE:
        return bson_iterator_value( i ) + 4;
    case BSON_CODEWSCOPE:
        return bson_iterator_value( i ) + 8;
    default:
        return NULL;
    }
}

MONGO_EXPORT void bson_iterator_code_scope_init( const bson_iterator *i, bson *scope, bson_bool_t copyData ) {
    if ( bson_iterator_type( i ) == BSON_CODEWSCOPE ) {
        int codeLen = bson_finished_data_size( bson_iterator_value( i )+4 );
        const char * scopeData = bson_iterator_value( i )+8+codeLen;
        if( copyData )
            bson_init_finished_data_with_copy( scope, scopeData );
        else
            bson_init_finished_data( scope, (char *)scopeData, 0 );
    }
    else {
        bson_init_empty( scope );
    }
}

MONGO_EXPORT bson_date_t bson_iterator_date( const bson_iterator *i ) {
    return bson_iterator_long_raw( i );
}

MONGO_EXPORT time_t bson_iterator_time_t( const bson_iterator *i ) {
    return bson_iterator_date( i ) / 1000;
}

MONGO_EXPORT int bson_iterator_bin_len( const bson_iterator *i ) {
    return ( bson_iterator_bin_type( i ) == BSON_BIN_BINARY_OLD )
           ? bson_iterator_int_raw( i ) - 4
           : bson_iterator_int_raw( i );
}

MONGO_EXPORT char bson_iterator_bin_type( const bson_iterator *i ) {
    return bson_iterator_value( i )[4];
}

MONGO_EXPORT const char *bson_iterator_bin_data( const bson_iterator *i ) {
    return ( bson_iterator_bin_type( i ) == BSON_BIN_BINARY_OLD )
           ? bson_iterator_value( i ) + 9
           : bson_iterator_value( i ) + 5;
}

MONGO_EXPORT const char *bson_iterator_regex( const bson_iterator *i ) {
    return bson_iterator_value( i );
}

MONGO_EXPORT const char *bson_iterator_regex_opts( const bson_iterator *i ) {
    const char *p = bson_iterator_value( i );
    return p + strlen( p ) + 1;

}

MONGO_EXPORT void bson_iterator_subobject_init( const bson_iterator *i, bson *sub, bson_bool_t copyData ) {
    const char *data = bson_iterator_value( i );
    if( copyData )
        bson_init_finished_data_with_copy( sub, data );
    else
        bson_init_finished_data( sub, (char *)data, 0 );
}

MONGO_EXPORT void bson_iterator_subiterator( const bson_iterator *i, bson_iterator *sub ) {
    bson_iterator_from_buffer( sub, bson_iterator_value( i ) );
}

/* ----------------------------
   BUILDING
   ------------------------------ */

static void _bson_zero( bson *b ) {
    memset( b, 0, sizeof( bson ) );
}

MONGO_EXPORT int bson_init( bson *b ) {
    return bson_init_size( b, initialBufferSize );
}

int bson_init_size( bson *b, int size ) {
    _bson_zero( b );
    if( size != 0 )
    {
        char * data = (char *) bson_malloc( size );
        if (data == NULL) return BSON_ERROR;
        b->data = data;
        b->dataSize = size;
    }
    b->ownsData = 1;
    b->cur = b->data + 4;
    return BSON_OK;
}

int bson_init_unfinished_data( bson *b, char *data, int dataSize, bson_bool_t ownsData ) {
    _bson_zero( b );
    b->data = data;
    b->dataSize = dataSize;
    b->ownsData = ownsData;
    return BSON_OK;
}

static int _bson_append_grow_stack( bson * b ) {
    if ( !b->stackPtr ) {
        // If this is an empty bson structure, initially use the struct-local (fixed-size) stack
        b->stackPtr = b->stack;
        b->stackSize = sizeof( b->stack ) / sizeof( size_t );
    }
    else if ( b->stackPtr == b->stack ) {
        // Once we require additional capacity, set up a dynamically resized stack
        size_t *new_stack = ( size_t * ) bson_malloc( 2 * sizeof( b->stack ) );
        if ( new_stack ) {
            b->stackPtr = new_stack;
            b->stackSize = 2 * sizeof( b->stack ) / sizeof( size_t );
            memcpy( b->stackPtr, b->stack, sizeof( b->stack ) );
        }
        else {
            return BSON_ERROR;
        }
    }
    else {
        // Double the capacity of the dynamically-resized stack
        size_t *new_stack = ( size_t * ) bson_realloc( b->stackPtr, ( b->stackSize * 2 ) * sizeof( size_t ) );
        if ( new_stack ) {
            b->stackPtr = new_stack;
            b->stackSize *= 2;
        }
        else {
            return BSON_ERROR;
        }
    }
    return BSON_OK;
}

static void bson_append_byte( bson *b, char c ) {
    b->cur[0] = c;
    b->cur++;
}

static void bson_append( bson *b, const void *data, size_t len ) {
    memcpy( b->cur , data , len );
    b->cur += len;
}

static void bson_append32( bson *b, const void *data ) {
    bson_little_endian32( b->cur, data );
    b->cur += 4;
}

static void bson_append32_as_int( bson *b, int data ) {
    bson_little_endian32( b->cur, &data );
    b->cur += 4;
}

static void bson_append64( bson *b, const void *data ) {
    bson_little_endian64( b->cur, data );
    b->cur += 8;
}

int bson_ensure_space( bson *b, const size_t bytesNeeded ) {
    size_t pos = _bson_position(b);
    char *orig = b->data;
    int new_size;

    if ( pos + bytesNeeded <= (size_t) b->dataSize )
        return BSON_OK;

    new_size = (int) ( 1.5 * ( b->dataSize + bytesNeeded ) );

    if( new_size < b->dataSize ) {
        if( ( b->dataSize + bytesNeeded ) < INT_MAX )
            new_size = INT_MAX;
        else {
            b->err = BSON_SIZE_OVERFLOW;
            return BSON_ERROR;
        }
    }

    if ( ! b->ownsData ) {
        b->err = BSON_DOES_NOT_OWN_DATA;
        return BSON_ERROR;
    }

    b->data = bson_realloc( b->data, new_size );
    if ( !b->data )
        bson_fatal_msg( !!b->data, "realloc() failed" );

    b->dataSize = new_size;
    b->cur += b->data - orig;

    return BSON_OK;
}

MONGO_EXPORT int bson_finish( bson *b ) {
    int i;

    if( b->err & BSON_NOT_UTF8 )
        return BSON_ERROR;

    if ( ! b->finished ) {
        bson_fatal_msg(!b->stackPos, "Subobject not finished before bson_finish().");
        if ( bson_ensure_space( b, 1 ) == BSON_ERROR ) return BSON_ERROR;
        bson_append_byte( b, 0 );
        if ( _bson_position(b) >= INT32_MAX ) {
            b->err = BSON_SIZE_OVERFLOW;
            return BSON_ERROR;
        }
        i = ( int ) _bson_position(b);
        bson_little_endian32( b->data, &i );
        b->finished = 1;
    }

    return BSON_OK;
}

MONGO_EXPORT void bson_destroy( bson *b ) {
    if ( b ) {
        if ( b->ownsData && b->data != NULL ) {
            bson_free( b->data );
        }
        b->data = NULL;
        b->dataSize = 0;
        b->ownsData = 0;        
        if ( b->stackPtr && b->stackPtr != b->stack ) {
            bson_free( b->stackPtr );
            b->stackPtr = NULL;
        }
        b->stackSize = 0;
        b->stackPos = 0;
        b->err = 0;
        b->cur = 0;
        b->finished = 1;
    }
}

static int bson_append_estart( bson *b, int type, const char *name, const size_t dataSize ) {
    const size_t len = strlen( name ) + 1;

    if ( b->finished ) {
        b->err |= BSON_ALREADY_FINISHED;
        return BSON_ERROR;
    }

    if ( bson_ensure_space( b, 1 + len + dataSize ) == BSON_ERROR ) {
        return BSON_ERROR;
    }

    if( bson_check_field_name( b, ( const char * )name, len - 1 ) == BSON_ERROR ) {
        bson_builder_error( b );
        return BSON_ERROR;
    }

    bson_append_byte( b, ( char )type );
    bson_append( b, name, len );
    return BSON_OK;
}

/* ----------------------------
   BUILDING TYPES
   ------------------------------ */

MONGO_EXPORT int bson_append_int( bson *b, const char *name, const int i ) {
    if ( bson_append_estart( b, BSON_INT, name, 4 ) == BSON_ERROR )
        return BSON_ERROR;
    bson_append32( b , &i );
    return BSON_OK;
}

MONGO_EXPORT int bson_append_long( bson *b, const char *name, const int64_t i ) {
    if ( bson_append_estart( b , BSON_LONG, name, 8 ) == BSON_ERROR )
        return BSON_ERROR;
    bson_append64( b , &i );
    return BSON_OK;
}

MONGO_EXPORT int bson_append_double( bson *b, const char *name, const double d ) {
    if ( bson_append_estart( b, BSON_DOUBLE, name, 8 ) == BSON_ERROR )
        return BSON_ERROR;
    bson_append64( b , &d );
    return BSON_OK;
}

MONGO_EXPORT int bson_append_bool( bson *b, const char *name, const bson_bool_t i ) {
    if ( bson_append_estart( b, BSON_BOOL, name, 1 ) == BSON_ERROR )
        return BSON_ERROR;
    bson_append_byte( b , i != 0 );
    return BSON_OK;
}

MONGO_EXPORT int bson_append_null( bson *b, const char *name ) {
    if ( bson_append_estart( b , BSON_NULL, name, 0 ) == BSON_ERROR )
        return BSON_ERROR;
    return BSON_OK;
}

MONGO_EXPORT int bson_append_undefined( bson *b, const char *name ) {
    if ( bson_append_estart( b, BSON_UNDEFINED, name, 0 ) == BSON_ERROR )
        return BSON_ERROR;
    return BSON_OK;
}

MONGO_EXPORT int bson_append_maxkey( bson *b, const char *name ) {
    if ( bson_append_estart( b, BSON_MAXKEY, name, 0 ) == BSON_ERROR )
        return BSON_ERROR;
    return BSON_OK;
}

MONGO_EXPORT int bson_append_minkey( bson *b, const char *name ) {
    if ( bson_append_estart( b, BSON_MINKEY, name, 0 ) == BSON_ERROR )
        return BSON_ERROR;
    return BSON_OK;
}

static int bson_append_string_base( bson *b, const char *name,
                                    const char *value, size_t len, bson_type type ) {

    size_t sl = len + 1;
    if ( sl > INT32_MAX ) {
        b->err = BSON_SIZE_OVERFLOW;
        /* string too long */
        return BSON_ERROR;
    }
    if ( bson_check_string( b, ( const char * )value, sl - 1 ) == BSON_ERROR )
        return BSON_ERROR;
    if ( bson_append_estart( b, type, name, 4 + sl ) == BSON_ERROR ) {
        return BSON_ERROR;
    }
    bson_append32_as_int( b , ( int )sl );
    bson_append( b , value , sl - 1 );
    bson_append( b , "\0" , 1 );
    return BSON_OK;
}

MONGO_EXPORT int bson_append_string( bson *b, const char *name, const char *value ) {
    return bson_append_string_base( b, name, value, strlen ( value ), BSON_STRING );
}

MONGO_EXPORT int bson_append_symbol( bson *b, const char *name, const char *value ) {
    return bson_append_string_base( b, name, value, strlen ( value ), BSON_SYMBOL );
}

MONGO_EXPORT int bson_append_code( bson *b, const char *name, const char *value ) {
    return bson_append_string_base( b, name, value, strlen ( value ), BSON_CODE );
}

MONGO_EXPORT int bson_append_string_n( bson *b, const char *name, const char *value, size_t len ) {
    return bson_append_string_base( b, name, value, len, BSON_STRING );
}

MONGO_EXPORT int bson_append_symbol_n( bson *b, const char *name, const char *value, size_t len ) {
    return bson_append_string_base( b, name, value, len, BSON_SYMBOL );
}

MONGO_EXPORT int bson_append_code_n( bson *b, const char *name, const char *value, size_t len ) {
    return bson_append_string_base( b, name, value, len, BSON_CODE );
}

MONGO_EXPORT int bson_append_code_w_scope_n( bson *b, const char *name,
        const char *code, size_t len, const bson *scope ) {

    size_t sl, size;
    if ( !scope ) return BSON_ERROR;
    sl = len + 1;
    size = 4 + 4 + sl + bson_size( scope );
    if ( size > (size_t)INT32_MAX ) {
        b->err = BSON_SIZE_OVERFLOW;
        return BSON_ERROR;
    }
    if ( bson_append_estart( b, BSON_CODEWSCOPE, name, size ) == BSON_ERROR )
        return BSON_ERROR;
    bson_append32_as_int( b, ( int )size );
    bson_append32( b, &sl );
    bson_append( b, code, sl );
    bson_append( b, scope->data, bson_size( scope ) );
    return BSON_OK;
}

MONGO_EXPORT int bson_append_code_w_scope( bson *b, const char *name, const char *code, const bson *scope ) {
    return bson_append_code_w_scope_n( b, name, code, strlen ( code ), scope );
}

MONGO_EXPORT int bson_append_binary( bson *b, const char *name, char type, const char *str, size_t len ) {
    if ( type == BSON_BIN_BINARY_OLD ) {
        size_t subtwolen = len + 4;
        if ( bson_append_estart( b, BSON_BINDATA, name, 4+1+4+len ) == BSON_ERROR )
            return BSON_ERROR;
        bson_append32_as_int( b, ( int )subtwolen );
        bson_append_byte( b, type );
        bson_append32_as_int( b, ( int )len );
        bson_append( b, str, len );
    }
    else {
        if ( bson_append_estart( b, BSON_BINDATA, name, 4+1+len ) == BSON_ERROR )
            return BSON_ERROR;
        bson_append32_as_int( b, ( int )len );
        bson_append_byte( b, type );
        bson_append( b, str, len );
    }
    return BSON_OK;
}

MONGO_EXPORT int bson_append_oid( bson *b, const char *name, const bson_oid_t *oid ) {
    if ( bson_append_estart( b, BSON_OID, name, 12 ) == BSON_ERROR )
        return BSON_ERROR;
    bson_append( b , oid , 12 );
    return BSON_OK;
}

MONGO_EXPORT int bson_append_new_oid( bson *b, const char *name ) {
    bson_oid_t oid;
    bson_oid_gen( &oid );
    return bson_append_oid( b, name, &oid );
}

MONGO_EXPORT int bson_append_regex( bson *b, const char *name, const char *pattern, const char *opts ) {
    const size_t plen = strlen( pattern )+1;
    const size_t olen = strlen( opts )+1;
    if ( bson_append_estart( b, BSON_REGEX, name, plen + olen ) == BSON_ERROR )
        return BSON_ERROR;
    if ( bson_check_string( b, pattern, plen - 1 ) == BSON_ERROR )
        return BSON_ERROR;
    bson_append( b , pattern , plen );
    bson_append( b , opts , olen );
    return BSON_OK;
}

MONGO_EXPORT int bson_append_bson( bson *b, const char *name, const bson *bson ) {
    if ( !bson ) return BSON_ERROR;
    if ( bson_append_estart( b, BSON_OBJECT, name, bson_size( bson ) ) == BSON_ERROR )
        return BSON_ERROR;
    bson_append( b , bson->data , bson_size( bson ) );
    return BSON_OK;
}

MONGO_EXPORT int bson_append_element( bson *b, const char *name_or_null, const bson_iterator *elem ) {
    bson_iterator next = *elem;
    size_t size;

    bson_iterator_next( &next );
    size = next.cur - elem->cur;

    if ( name_or_null == NULL ) {
        if( bson_ensure_space( b, size ) == BSON_ERROR )
            return BSON_ERROR;
        bson_append( b, elem->cur, size );
    }
    else {
        size_t data_size = size - 2 - strlen( bson_iterator_key( elem ) );
        bson_append_estart( b, elem->cur[0], name_or_null, data_size );
        bson_append( b, bson_iterator_value( elem ), data_size );
    }

    return BSON_OK;
}

MONGO_EXPORT int bson_append_timestamp( bson *b, const char *name, bson_timestamp_t *ts ) {
    if ( bson_append_estart( b, BSON_TIMESTAMP, name, 8 ) == BSON_ERROR ) return BSON_ERROR;

    bson_append32( b , &( ts->i ) );
    bson_append32( b , &( ts->t ) );

    return BSON_OK;
}

MONGO_EXPORT int bson_append_timestamp2( bson *b, const char *name, int time, int increment ) {
    if ( bson_append_estart( b, BSON_TIMESTAMP, name, 8 ) == BSON_ERROR ) return BSON_ERROR;

    bson_append32( b , &increment );
    bson_append32( b , &time );
    return BSON_OK;
}

MONGO_EXPORT int bson_append_date( bson *b, const char *name, bson_date_t millis ) {
    if ( bson_append_estart( b, BSON_DATE, name, 8 ) == BSON_ERROR ) return BSON_ERROR;
    bson_append64( b , &millis );
    return BSON_OK;
}

MONGO_EXPORT int bson_append_time_t( bson *b, const char *name, time_t secs ) {
    return bson_append_date( b, name, ( bson_date_t )secs * 1000 );
}

MONGO_EXPORT int bson_append_start_object( bson *b, const char *name ) {
    if ( bson_append_estart( b, BSON_OBJECT, name, 5 ) == BSON_ERROR ) return BSON_ERROR;
    if ( b->stackPos >= b->stackSize && _bson_append_grow_stack( b ) == BSON_ERROR ) return BSON_ERROR;
    b->stackPtr[ b->stackPos++ ] = _bson_position(b);
    bson_append32( b , &zero );
    return BSON_OK;
}

MONGO_EXPORT int bson_append_start_array( bson *b, const char *name ) {
    if ( bson_append_estart( b, BSON_ARRAY, name, 5 ) == BSON_ERROR ) return BSON_ERROR;
    if ( b->stackPos >= b->stackSize && _bson_append_grow_stack( b ) == BSON_ERROR ) return BSON_ERROR;
    b->stackPtr[ b->stackPos++ ] = _bson_position(b);
    bson_append32( b , &zero );
    return BSON_OK;
}

MONGO_EXPORT int bson_append_finish_object( bson *b ) {
    char *start;
    int i;
    if (!b) return BSON_ERROR;
    if (!b->stackPos) { b->err = BSON_NOT_IN_SUBOBJECT; return BSON_ERROR; }
    if ( bson_ensure_space( b, 1 ) == BSON_ERROR ) return BSON_ERROR;
    bson_append_byte( b , 0 );

    start = b->data + b->stackPtr[ --b->stackPos ];
    if ( b->cur - start >= INT32_MAX ) {
        b->err = BSON_SIZE_OVERFLOW;
        return BSON_ERROR;
    }
    i = ( int )( b->cur - start );
    bson_little_endian32( start, &i );

    return BSON_OK;
}

MONGO_EXPORT double bson_int64_to_double( int64_t i64 ) {
    return (double)i64;
}

MONGO_EXPORT int bson_append_finish_array( bson *b ) {
    return bson_append_finish_object( b );
}

/* Error handling and allocators. */

static bson_err_handler err_handler = NULL;

MONGO_EXPORT bson_err_handler set_bson_err_handler( bson_err_handler func ) {
    bson_err_handler old = err_handler;
    err_handler = func;
    return old;
}

MONGO_EXPORT void bson_free( void *ptr ) {
    bson_free_func( ptr );
}

MONGO_EXPORT void *bson_malloc( size_t size ) {
    void *p;
    p = bson_malloc_func( size );
    bson_fatal_msg( !!p, "malloc() failed" );
    return p;
}

void *bson_realloc( void *ptr, size_t size ) {
    void *p;
    p = bson_realloc_func( ptr, size );
    bson_fatal_msg( !!p, "realloc() failed" );
    return p;
}

int _bson_errprintf( const char *format, ... ) {
    va_list ap;
    int ret = 0;
    va_start( ap, format );
#ifndef R_SAFETY_NET
    ret = vfprintf( stderr, format, ap );
#endif
    va_end( ap );

    return ret;
}

/**
 * This method is invoked when a non-fatal bson error is encountered.
 * Calls the error handler if available.
 *
 *  @param
 */
void bson_builder_error( bson *b ) {
    if( err_handler )
        err_handler( "BSON error." );
}

void bson_fatal( int ok ) {
    bson_fatal_msg( ok, "" );
}

void bson_fatal_msg( int ok , const char *msg ) {
    if ( ok )
        return;

    if ( err_handler ) {
        err_handler( msg );
    }
#ifndef R_SAFETY_NET
    bson_errprintf( "error: %s\n" , msg );
    exit( -5 );
#endif
}


/* Efficiently copy an integer to a string. */
extern const char bson_numstrs[1000][4];

void bson_numstr( char *str, int i ) {
    if( i < 1000 )
        memcpy( str, bson_numstrs[i], 4 );
    else
        bson_sprintf( str,"%d", i );
}

MONGO_EXPORT void bson_swap_endian64( void *outp, const void *inp ) {
    const char *in = ( const char * )inp;
    char *out = ( char * )outp;

    out[0] = in[7];
    out[1] = in[6];
    out[2] = in[5];
    out[3] = in[4];
    out[4] = in[3];
    out[5] = in[2];
    out[6] = in[1];
    out[7] = in[0];

}

MONGO_EXPORT void bson_swap_endian32( void *outp, const void *inp ) {
    const char *in = ( const char * )inp;
    char *out = ( char * )outp;

    out[0] = in[3];
    out[1] = in[2];
    out[2] = in[1];
    out[3] = in[0];
}
