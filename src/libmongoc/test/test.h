#include "mongo.h"
#include <stdlib.h>

#define ASSERT(x) \
    do{ \
        if(!(x)){ \
            printf("\nFailed ASSERT [%s] (%d):\n     %s\n\n", __FILE__,  __LINE__,  #x); \
            exit(1); \
        }\
    }while(0)

#define ASSERT_EQUAL_STRINGS(x, y) \
    do{ \
        if((strncmp( x, y, strlen( y ) ) != 0 )){ \
            printf("\nFailed ASSERT_EQUAL_STRINGS [%s] (%d):\n  \"%s\" does not equal\n  %s\n", __FILE__,  __LINE__,  x, #y); \
            exit(1); \
        }\
    }while(0)

#ifdef _WIN32
#define INIT_SOCKETS_FOR_WINDOWS mongo_init_sockets();
#else
#define INIT_SOCKETS_FOR_WINDOWS do {} while(0)
#endif

const char *TEST_DB = "test";
const char *TEST_COL = "foo";
const char *TEST_NS = "test.foo";

#define CONN_CLIENT_TEST \
    if( mongo_client( conn, TEST_SERVER, 27017 ) != MONGO_OK ) { \
        printf( "Failed to connect" ); \
        exit( 1 ); \
    } \

MONGO_EXTERN_C_START

int mongo_get_server_version( char *version ) {
    int ret = 0;
    mongo conn[1];
    bson cmd[1], out[1];
    bson_iterator it[1];
    const char *result;

    mongo_client( conn, TEST_SERVER, 27017 );

    bson_init( cmd );
    bson_append_int( cmd, "buildinfo", 1 );
    bson_finish( cmd );

    if( mongo_run_command( conn, "admin", cmd, out ) == MONGO_ERROR ) {
        ret = -1;
    }
    else {
        bson_iterator_init( it, out );
        result = bson_iterator_string( it );

        memcpy( version, result, strlen( result ) );
        bson_destroy( out );
    }
    bson_destroy( cmd );
    mongo_destroy( conn );
    return 0;
}

MONGO_EXTERN_C_END
