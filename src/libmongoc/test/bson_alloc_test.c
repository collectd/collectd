#include "test.h"
#include "bson.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int bson_malloc_allowed = 0;
static int bson_malloc_was_called = 0;
#define ALLOW_AND_REQUIRE_MALLOC_BEGIN bson_malloc_allowed = 1; bson_malloc_was_called = 0
#define ALLOW_AND_REQUIRE_MALLOC_END bson_malloc_allowed = 0; ASSERT( bson_malloc_was_called )

static int bson_free_allowed = 0;
static int bson_free_was_called = 0;
#define ALLOW_AND_REQUIRE_FREE_BEGIN bson_free_allowed = 1; bson_free_was_called = 0
#define ALLOW_AND_REQUIRE_FREE_END bson_free_allowed = 0; ASSERT( bson_free_was_called )

void * malloc_for_tests( size_t size ) {
    if ( ! bson_malloc_allowed ) {
      // Write this way to make it easier to stop here while debugging
      ASSERT( bson_malloc_allowed );      
    }
    bson_malloc_was_called = 1;
    return malloc( size );
}

void * realloc_for_tests( void * ptr, size_t size ) {
    if ( ! bson_malloc_allowed ) {
      // Write this way to make it easier to stop here while debugging
      ASSERT( bson_malloc_allowed );      
    }
    bson_malloc_was_called = 1;
    return realloc( ptr, size );
}

void free_for_tests( void * ptr ) {
    if ( ! bson_free_allowed ) {
      // Write this way to make it easier to stop here while debugging
      ASSERT( bson_free_allowed );      
    }
    bson_free_was_called = 1;
    free( ptr );
}

int test_bson_empty( void ) {
    const bson *empty1;
    empty1 = bson_shared_empty();
    ASSERT( empty1->data );
    ASSERT( bson_size(empty1) > 0 );

    ALLOW_AND_REQUIRE_MALLOC_BEGIN;
    bson * empty2 = bson_alloc();
    ALLOW_AND_REQUIRE_MALLOC_END;
    memset( empty2, 0, sizeof( bson) );
    bson_init_empty( empty2 );
    ASSERT( empty2->data );
    ASSERT( bson_size( empty2 ) > 0 );
    bson_destroy( empty2 );
    ALLOW_AND_REQUIRE_FREE_BEGIN;
    bson_dealloc( empty2 );
    ALLOW_AND_REQUIRE_FREE_END;

    return 0;
}

int test_bson_init_finished( void ) {
    bson b;
    ALLOW_AND_REQUIRE_MALLOC_BEGIN;
    bson_init( &b );
    ALLOW_AND_REQUIRE_MALLOC_END;
    bson_append_double( &b, "d", 3.14 );
    bson_append_string( &b, "s", "hello" );
    bson_finish( &b );
    ASSERT( bson_size( &b ) == 29 ); // 29 determined by running this code

    bson b2;
    bson_init_finished_data( &b2, (char *) bson_data( &b ), 0 );
    ASSERT( bson_size( &b ) == bson_size( &b2 ) );
    bson_destroy( &b2 );

    ALLOW_AND_REQUIRE_MALLOC_BEGIN;
    bson_init_finished_data_with_copy( &b2, (char *) bson_data( &b ) );
    ALLOW_AND_REQUIRE_MALLOC_END;
    ASSERT( bson_size( &b ) == bson_size( &b2 ) );
    ALLOW_AND_REQUIRE_FREE_BEGIN;
    bson_destroy( &b2 );
    ALLOW_AND_REQUIRE_FREE_END;

    bson_init_finished_data( &b2, (char *) bson_data( &b ), 1 );
    ASSERT( bson_size( &b ) == bson_size( &b2 ) );
    ALLOW_AND_REQUIRE_FREE_BEGIN;
    bson_destroy( &b2 );
    ALLOW_AND_REQUIRE_FREE_END;

    return 0;
}

int main() {
  bson_malloc_func = malloc_for_tests;
  bson_realloc_func = realloc_for_tests;
  bson_free_func = free_for_tests;

  test_bson_empty();
  test_bson_init_finished();

  return 0;
}

