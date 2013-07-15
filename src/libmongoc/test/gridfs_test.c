#include "test.h"
#include "md5.h"
#include "mongo.h"
#include "gridfs.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <limits.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#define LARGE 3*1024*1024
#define UPPER 2000*1024
#define MEDIUM 1024*512
#define LOWER 1024*128
#define DELTA 1024*128
#define READ_WRITE_BUF_SIZE 10 * 1024

#define GRIDFILE_COMPRESS 2

#ifdef _MSC_VER
#define gridfs_test_unlink _unlink
#else
#define gridfs_test_unlink unlink
#endif

#define GFS_INIT \
    gridfs_init( conn, "test", "fs", gfs )

void fill_buffer_randomly( char *data, int64_t length ) {
    int64_t i;
    int random;
    char *letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int nletters = (int)strlen( letters );
    char *cur;

    for ( i = 0, cur = data; i < length; i++, cur++ ) {
        random = rand() % nletters;
        *cur = letters[random];
    }
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

void test_gridfile( gridfs *gfs, char *data_before, int64_t length, char *filename, char *content_type ) {
    gridfile gfile[1];
    FILE *stream;
    mongo_md5_state_t pms[1];
    mongo_md5_byte_t digest[16];
    char hex_digest[33];
    int64_t i = length;
    int n;
    char *data_after = (char*)bson_malloc( LARGE );
    int truncBytes;
    char* lowerName;

    ASSERT(gridfs_find_filename( gfs, filename, gfile ) == MONGO_OK);
    ASSERT( gridfile_exists( gfile ) );

    stream = fopen( "output", "w+" );
    gridfile_write_file( gfile, stream );
    fseek( stream, 0, SEEK_SET );
    ASSERT( fread( data_after, (size_t)length, sizeof( char ), stream ) );
    fclose( stream );
    ASSERT( memcmp( data_before, data_after, (size_t)length ) == 0 );

    gridfile_read_buffer( gfile, data_after, length );
    ASSERT( memcmp( data_before, data_after, (size_t)length ) == 0 );

    lowerName = (char*) bson_malloc( (int)strlen( filename ) + 1);    
    strcpy( lowerName, filename );
    _strlwr( lowerName );
    
    ASSERT( strcmp( gridfile_get_filename( gfile ), lowerName ) == 0 );
    bson_free( lowerName );

    ASSERT( gridfile_get_contentlength( gfile ) == (size_t)length );

    ASSERT( gridfile_get_chunksize( gfile ) == DEFAULT_CHUNK_SIZE );

    ASSERT( strcmp( gridfile_get_contenttype( gfile ), content_type ) == 0 ) ;

    ASSERT( memcmp( data_before, data_after, (size_t)length ) == 0 );

    if( !( gfile->flags & GRIDFILE_COMPRESS ) ) {
      mongo_md5_init( pms );

      n = 0;
      while( i > INT_MAX  ) {
          mongo_md5_append( pms, ( const mongo_md5_byte_t * )data_before + ( n * INT_MAX ), INT_MAX );
          i -= INT_MAX;
          n += 1;
      }
      if( i > 0 )
          mongo_md5_append( pms, ( const mongo_md5_byte_t * )data_before + ( n * INT_MAX ), (int)i );

      mongo_md5_finish( pms, digest );
      digest2hex( digest, hex_digest );
      ASSERT( strcmp( gridfile_get_md5( gfile ), hex_digest ) == 0 );
    }

    truncBytes = (int) (length > DEFAULT_CHUNK_SIZE * 4 ? length - DEFAULT_CHUNK_SIZE * 2 - 13 : 23); 
    gridfile_writer_init( gfile, gfs, filename, content_type, GRIDFILE_DEFAULT);
    ASSERT( gridfile_truncate(gfile, (size_t)(length - truncBytes)) == (size_t)(length - truncBytes));
    gridfile_writer_done( gfile );

    gridfile_seek(gfile, 0);
    ASSERT( gridfile_get_contentlength( gfile ) == (size_t)(length - truncBytes) );
    ASSERT( gridfile_read_buffer( gfile, data_after, length ) ==  (size_t)(length - truncBytes));
    ASSERT( memcmp( data_before, data_after, (size_t)(length - truncBytes) ) == 0 );

    gridfile_writer_init( gfile, gfs, filename, content_type, GRIDFILE_DEFAULT);
    gridfile_truncate(gfile, 0);
    gridfile_writer_done( gfile );

    ASSERT( gridfile_get_contentlength( gfile ) == 0 );
    ASSERT( gridfile_read_buffer( gfile, data_after, length ) == 0 );

    gridfile_destroy( gfile );
    ASSERT( gridfs_remove_filename( gfs, filename ) == MONGO_OK );
    free( data_after );
    gridfs_test_unlink( "output" );
}

void test_basic( void ) {
    mongo conn[1];
    gridfs gfs[1];
    char *data_before = (char*)bson_malloc( UPPER );
    int64_t i;
    FILE *fd;

    srand((unsigned int) time( NULL ) );

    INIT_SOCKETS_FOR_WINDOWS;
    CONN_CLIENT_TEST;
    GFS_INIT;

    fill_buffer_randomly( data_before, UPPER );
    for ( i = LOWER; i <= UPPER; i += DELTA ) {

        /* Input from buffer */
        gridfs_store_buffer( gfs, data_before, i, "input-buffer", "text/html", GRIDFILE_COMPRESS );
        test_gridfile( gfs, data_before, i, "input-buffer", "text/html" );

        /* Input from file */
        fd = fopen( "input-file", "w" );
        fwrite( data_before, sizeof( char ), (size_t)i, fd );
        fclose( fd );
        gridfs_store_file( gfs, "input-file", "input-file", "text/html", GRIDFILE_DEFAULT );
        test_gridfile( gfs, data_before, i, "input-file", "text/html" );

        gfs->caseInsensitive = 1;
        gridfs_store_file( gfs, "input-file", "input-file", "text/html", GRIDFILE_DEFAULT );
        test_gridfile( gfs, data_before, i, "inPut-file", "text/html" );
    }

    gridfs_destroy( gfs );
    mongo_disconnect( conn );
    mongo_destroy( conn );
    free( data_before );

    /* Clean up files. */
    gridfs_test_unlink( "input-file" );
    gridfs_test_unlink( "output" );
}

void test_delete( void ) {
    mongo conn[1];
    gridfs gfs[1];
    gridfile gfile[1];
    char *data = (char*)bson_malloc( 1024 );
    const char *testFile = "test-delete";

    INIT_SOCKETS_FOR_WINDOWS;
    CONN_CLIENT_TEST;
    GFS_INIT;

    ASSERT( gridfs_store_buffer( gfs, data, 1024, testFile, "text/html", GRIDFILE_DEFAULT ) == MONGO_OK );
    ASSERT( gridfs_find_filename( gfs, testFile, gfile ) == MONGO_OK );
    gridfile_destroy( gfile );

    ASSERT( gridfs_remove_filename( gfs, testFile ) == MONGO_OK );
    ASSERT( gridfs_find_filename( gfs, testFile, gfile ) == MONGO_ERROR );

    ASSERT( gridfs_find_filename( gfs, "bogus-file-does-not-exist", gfile ) == MONGO_ERROR );
    ASSERT( gridfs_remove_filename( gfs, "bogus-file-does-not-exist" ) == MONGO_ERROR );

    gridfs_destroy( gfs );
    mongo_disconnect( conn );
    mongo_destroy( conn );
    bson_free( data );
}

void test_streaming( void ) {
    mongo conn[1];
    gridfs gfs[1];
    gridfile gfile[1];
    char *medium = (char*)bson_malloc( 2*MEDIUM );
    char *small = (char*)bson_malloc( LOWER );
    char *buf = (char*)bson_malloc( LARGE );
    int n;

    if( buf == NULL || small == NULL ) {
        printf( "Failed to allocate" );
        exit( 1 );
    }

    srand( (unsigned int)time( NULL ) );

    INIT_SOCKETS_FOR_WINDOWS;
    CONN_CLIENT_TEST;

    fill_buffer_randomly( medium, ( int64_t )2 * MEDIUM );
    fill_buffer_randomly( small, ( int64_t )LOWER );
    fill_buffer_randomly( buf, ( int64_t )LARGE );

    GFS_INIT;
    gridfile_init( gfs, NULL, gfile );
    gridfile_writer_init( gfile, gfs, "medium", "text/html", GRIDFILE_DEFAULT );

    ASSERT( gridfile_write_buffer( gfile, medium, MEDIUM ) == MEDIUM);
    ASSERT( gridfile_write_buffer( gfile, medium + MEDIUM, MEDIUM ) == MEDIUM);
    gridfile_writer_done( gfile );
    test_gridfile( gfs, medium, 2 * MEDIUM, "medium", "text/html" );
    gridfs_destroy( gfs );

    GFS_INIT;
    gridfs_store_buffer( gfs, small, LOWER, "small", "text/html", GRIDFILE_DEFAULT );
    test_gridfile( gfs, small, LOWER, "small", "text/html" );
    gridfs_destroy( gfs );

    GFS_INIT;
    gridfs_remove_filename( gfs, "large" );
    gridfile_writer_init( gfile, gfs, "large", "text/html", GRIDFILE_DEFAULT );
    for( n=0; n < ( LARGE / 1024 ); n++ ) {
        ASSERT( gridfile_write_buffer( gfile, buf + ( n * 1024 ), 1024 ) == 1024 );
    }
    gridfile_writer_done( gfile );
    test_gridfile( gfs, buf, LARGE, "large", "text/html" );

    gridfs_destroy( gfs );
    mongo_destroy( conn );
    free( buf );
    free( small );
    free( medium );
}

void test_random_write() {
    mongo conn[1];
    gridfs gfs[1];
    gridfile* gfile;
    char *data_before = (char*)bson_malloc( UPPER );
    char *random_data = (char*)bson_malloc( UPPER );
    char *buf = (char*) bson_malloc( UPPER );
    int64_t i;
    FILE *fd;

    srand((unsigned int) time( NULL ) );

    INIT_SOCKETS_FOR_WINDOWS;
    CONN_CLIENT_TEST;
    GFS_INIT;

    fill_buffer_randomly( data_before, UPPER );
    fill_buffer_randomly( random_data, UPPER );
    for ( i = LOWER; i <= UPPER; i += DELTA ) {
        int64_t j = i / 2 - 3;
        int n, bytes_to_write_first;

        /* Input from buffer */
        gridfs_store_buffer( gfs, data_before, i, "input-buffer", "text/html", GRIDFILE_DEFAULT );
        if ( i > DEFAULT_CHUNK_SIZE * 4 ) {
          n = DEFAULT_CHUNK_SIZE * 3 + 6;
          memcpy(&data_before[j], random_data, n); // Let's overwrite the buffer with bytes crossing multiple chunks
          bytes_to_write_first = 10;
        } else {
          n = 6;
          memcpy(random_data, "123456", n);
          strncpy(&data_before[j], random_data, n); // Let's overwrite the buffer with a few some bytes
          bytes_to_write_first = 0;
        }
        gfile = gridfile_create();
        ASSERT(gridfs_find_filename(gfs, "input-buffer", gfile) == 0);
        gridfile_writer_init(gfile, gfs, "input-buffer", "text/html", GRIDFILE_DEFAULT );
        gridfile_seek(gfile, j); // Seek into the same buffer position within the GridFS file
        if ( bytes_to_write_first ) {
          ASSERT( gridfile_write_buffer(gfile, random_data, bytes_to_write_first) == bytes_to_write_first ); // Let's write 10 bytes first, and later the rest
        }
        ASSERT( gridfile_write_buffer(gfile, &random_data[bytes_to_write_first], n - bytes_to_write_first) == n - bytes_to_write_first ); // Try to write to the existing GridFS file on the position given by j
        gridfile_seek(gfile, j);
        gridfile_read_buffer( gfile, buf, n );
        ASSERT(memcmp( buf, &data_before[j], n) == 0);

        gridfile_writer_done(gfile);
        ASSERT(gfile->pos == j + n);
        gridfile_dealloc(gfile);
        test_gridfile( gfs, data_before, j + n > i ? j + n : i, "input-buffer", "text/html" );

        /* Input from file */
        fd = fopen( "input-file", "w" );
        fwrite( data_before, sizeof( char ), (size_t) (j + n > i ? j + n : i), fd );
        fclose( fd );
        gridfs_store_file( gfs, "input-file", "input-file", "text/html", GRIDFILE_DEFAULT );
        test_gridfile( gfs, data_before, j + n > i ? j + n : i, "input-file", "text/html" );
    }

    gridfs_destroy( gfs );
    mongo_disconnect( conn );
    mongo_destroy( conn );
    free( data_before );
    free( random_data );
    free( buf );

    /* Clean up files. */
    gridfs_test_unlink( "input-file" );
    gridfs_test_unlink( "output" );   
}

void test_random_write2( void ) {
    mongo conn[1];
    gridfs gfs[1];
    gridfile gfile[1];
    bson meta;
    char *buf = (char*)bson_malloc( LARGE );
    char *zeroedbuf = (char*)bson_malloc( LARGE );
    int n;

    if( buf == NULL ) {
        printf( "Failed to allocate" );
        exit( 1 );
    }

    srand( 123 ); // Init with a predictable value

    INIT_SOCKETS_FOR_WINDOWS;
    CONN_CLIENT_TEST;

    fill_buffer_randomly( buf, ( int64_t )LARGE );
    memset( zeroedbuf, 0, LARGE ); 

    bson_init_empty( &meta );

    GFS_INIT;

    /* This portion of the test we will write zeroes by using new API gridfile_set_size
       function gridfile_expand tested implicitally by using gridfile_set_size making file larger */
    gridfile_init( gfs, &meta, gfile );
    gridfile_writer_init( gfile, gfs, "random_access", "text/html", 0 );
    gridfile_set_size( gfile, LARGE ); // New API, this zero fills the file
    gridfile_writer_done( gfile );    
    test_gridfile( gfs, zeroedbuf, LARGE, "random_access", "text/html" ); // Test zero filled file

    /* This portion of the test we will write zeroes by using new API gridfile_set_size then we will truncate the file */
    gridfile_init( gfs, &meta, gfile );
    gridfile_writer_init( gfile, gfs, "random_access", "text/html", 0 );
    gridfile_set_size( gfile, LARGE ); // New API, this zero fills the file with LARGE bytes
    gridfile_truncate( gfile, LARGE / 2 ); // Let's truncate the file now
    gridfile_writer_done( gfile );    
    test_gridfile( gfs, zeroedbuf, LARGE / 2, "random_access", "text/html" ); // Test zero filled file truncated by half

    /* Let's re-create the file, now let's randomly write real data */
    gridfile_init( gfs, &meta, gfile );
    gridfile_writer_init( gfile, gfs, "random_access", "text/html", 0 );   
    gridfile_set_size( gfile, LARGE ); // We need to reserve LARGE bytes on file before writing backwards

    /* Here we will write backwards the file with our random numbers. We will use a 3072 size buffer to cross over buffers
       given the fact 256K is not a multiple of 3072. Let's stress the buffering logic a little bit */
    for( n = LARGE / 3072 - 1; n >= 0; n-- ) {
        gridfile_seek( gfile, 3072 * n );
        ASSERT( gridfile_write_buffer( gfile, buf + ( n * 3072 ), 3072 ) == 3072 );
    }
    gridfile_writer_done( gfile );
    test_gridfile( gfs, buf, LARGE, "random_access", "text/html" );

    gridfs_destroy( gfs );
    mongo_destroy( conn );

    free( buf );
    free( zeroedbuf );
}

void test_large( void ) {
    mongo conn[1];
    gridfs gfs[1];
    gridfile gfile[1];
    FILE *fd;
    size_t i, n;
    char *buffer = (char*)bson_malloc( LARGE );
    char *read_buf = (char*)bson_malloc( LARGE );
    int64_t filesize = ( int64_t )1024 * ( int64_t )LARGE;
    mongo_write_concern wc;    
    bson lastError;
    bson lastErrorCmd;
    
    srand( (unsigned int) time( NULL ) );

    INIT_SOCKETS_FOR_WINDOWS;
    CONN_CLIENT_TEST;
    
    mongo_write_concern_init(&wc);
    wc.j = 1;
    mongo_write_concern_finish(&wc);
    mongo_set_write_concern(conn, &wc);

    GFS_INIT;

    fd = fopen( "bigfile", "r" );
    if( fd ) {
      fclose( fd );
    } else {
      /* Create a very large file */
      fill_buffer_randomly( buffer, ( int64_t )LARGE );
      fd = fopen( "bigfile", "w" );
      for( i=0; i<1024; i++ ) {
        fwrite( buffer, 1, LARGE, fd );
      }
      fclose( fd );
    }

    /* Now read the file into GridFS */
    gridfs_remove_filename( gfs, "bigfile" );
    gridfs_store_file( gfs, "bigfile", "bigfile", "text/html", GRIDFILE_NOMD5 | GRIDFILE_COMPRESS);

    gridfs_find_filename( gfs, "bigfile", gfile );

    ASSERT( strcmp( gridfile_get_filename( gfile ), "bigfile" ) == 0 );
    ASSERT( gridfile_get_contentlength( gfile ) ==  filesize );
    
    fd = fopen( "bigfile", "r" );

    while( ( n = fread( buffer, 1, MEDIUM, fd ) ) != 0 ) {
      ASSERT( gridfile_read_buffer( gfile, read_buf, MEDIUM ) == n );
      ASSERT( memcmp( buffer, read_buf, n ) == 0 );
    }

    fclose( fd );
    gridfile_destroy( gfile );

    /* Read the file using the streaming interface */
    gridfs_remove_filename( gfs, "bigfile" );
    gridfs_remove_filename( gfs, "bigfile-stream" );
    gridfile_writer_init( gfile, gfs, "bigfile-stream", "text/html", GRIDFILE_NOMD5 | GRIDFILE_COMPRESS );

    mongo_write_concern_destroy( &wc );
    mongo_write_concern_init(&wc);
    wc.j = 0; /* Let's reset write concern j field to zero, we will manually call getLastError with j = 1 */
    mongo_write_concern_finish(&wc);
    mongo_set_write_concern(conn, &wc);

    fd = fopen( "bigfile", "r" );
    i = 0;
    while( ( n = fread( buffer, 1, READ_WRITE_BUF_SIZE, fd ) ) != 0 ) {
        ASSERT( gridfile_write_buffer( gfile, buffer, n ) == n );     
        if(i++ % 10 == 0) {
          bson_init( &lastErrorCmd );
          bson_append_int( &lastErrorCmd, "getLastError", 1);
          bson_append_int( &lastErrorCmd, "j", 1);
          bson_finish( &lastErrorCmd );

          bson_init( &lastError );
          mongo_run_command( conn, "test", &lastErrorCmd, &lastError );

          bson_destroy( &lastError );
          bson_destroy( &lastErrorCmd );
        }
    }

    mongo_write_concern_destroy( &wc );
    mongo_write_concern_init(&wc);
    wc.j = 1; /* Let's reset write concern j field to 1 */
    mongo_write_concern_finish(&wc);
    mongo_set_write_concern(conn, &wc);

    fclose( fd );
    gridfile_writer_done( gfile );

    gridfs_find_filename( gfs, "bigfile-stream", gfile );

    ASSERT( strcmp( gridfile_get_filename( gfile ), "bigfile-stream" ) == 0 );
    ASSERT( gridfile_get_contentlength( gfile ) ==  filesize );
    gridfs_remove_filename( gfs, "bigfile-stream" );

    gridfs_destroy( gfs );
    mongo_disconnect( conn );
    mongo_destroy( conn );

    bson_free( buffer );
    bson_free( read_buf );
    mongo_write_concern_destroy( &wc );
}

int main( void ) {
/* See https://jira.mongodb.org/browse/CDRIVER-126
 * on why we exclude this test from running on WIN32 */
 
    test_basic();
    test_delete();
    test_streaming();
    test_random_write();
    test_random_write2();
    
    /* Normally not necessary to run test_large(), as it
     * deals with very large (5GB) files and is therefore slow. */
    /*test_large();*/


    return 0;
}
