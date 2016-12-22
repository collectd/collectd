/* helpers.c */

#include "test.h"
#include "mongo.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

void test_index_helper( mongo *conn ) {
    int ret;

    bson b, out;
    bson_iterator it;

    bson_init( &b );
    bson_append_int( &b, "foo", -1 );
    bson_finish( &b );

    mongo_create_index( conn, "test.bar", &b, NULL, MONGO_INDEX_SPARSE | MONGO_INDEX_UNIQUE, &out );

    bson_destroy( &b );
    bson_destroy( &out );

    bson_init( &b );
    bson_append_start_object( &b, "key" );
    bson_append_int( &b, "foo", -1 );
    bson_append_finish_object( &b );

    bson_finish( &b );

    ret = mongo_find_one( conn, "test.system.indexes", &b, NULL, &out );
    ASSERT( ret == MONGO_OK );

    bson_print( &out );

    bson_iterator_init( &it, &out );

    ASSERT( bson_find( &it, &out, "unique" ) );
    ASSERT( bson_find( &it, &out, "sparse" ) );

    bson_destroy( &b );
    bson_destroy( &out );
}

void test_index_helper_invalid( mongo *conn ) {
    bson b, out;

    bson_init( &b );
    bson_append_int( &b, "foo", -1 );
    bson_finish( &b );

    ASSERT( MONGO_ERROR == mongo_create_index( conn, "testbar", &b, NULL, MONGO_INDEX_SPARSE | MONGO_INDEX_UNIQUE, &out ));

    bson_destroy( &b );
    bson_destroy( &out );
}

int main() {

    mongo conn[1];

    INIT_SOCKETS_FOR_WINDOWS;
    CONN_CLIENT_TEST;

    test_index_helper( conn );
    test_index_helper_invalid( conn );

    mongo_destroy( conn );

    return 0;
}
