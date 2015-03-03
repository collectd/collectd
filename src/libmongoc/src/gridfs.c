/* gridfs.c */

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

#ifndef MAX
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif
#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

#include "gridfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#ifndef _MSC_VER
#include <ctype.h>
char *_strupr(char *str)
{
   char *s = str;
   while (*s) {
        *s = toupper((unsigned char)*s);
        ++s;
      }
   return str;
}
char *_strlwr(char *str)
{
   char *s = str;
   while (*s) {
        *s = tolower((unsigned char)*s);
        ++s;
   }
   return str;
}
#endif

/* Memory allocation functions */
MONGO_EXPORT gridfs *gridfs_alloc( void ) {
  return ( gridfs* )bson_malloc( sizeof( gridfs ) );
}

MONGO_EXPORT void gridfs_dealloc( gridfs *gfs ) {
  bson_free( gfs );
}

MONGO_EXPORT gridfile *gridfile_create( void ) {
  gridfile* gfile = (gridfile*)bson_malloc(sizeof(gridfile));  
  memset( gfile, 0, sizeof ( gridfile ) );  
  return gfile;
}

MONGO_EXPORT void gridfile_dealloc( gridfile *gf ) {
  bson_free( gf );
}

MONGO_EXPORT void gridfile_get_descriptor(gridfile *gf, bson *out) {
  *out =  *gf->meta;
}

/* Default chunk pre and post processing logic */
static int gridfs_default_chunk_filter(char** targetBuf, size_t* targetLen, const char* srcData, size_t srcLen, int flags) {
  *targetBuf = (char *) srcData;
  *targetLen = srcLen;
  return 0;
}

static size_t gridfs_default_pending_data_size (int flags) {
  return DEFAULT_CHUNK_SIZE;
}
/* End of default functions for chunks pre and post processing */

static gridfs_chunk_filter_func gridfs_write_filter = gridfs_default_chunk_filter;
static gridfs_chunk_filter_func gridfs_read_filter = gridfs_default_chunk_filter;
static gridfs_pending_data_size_func gridfs_pending_data_size = gridfs_default_pending_data_size;

MONGO_EXPORT void gridfs_set_chunk_filter_funcs(gridfs_chunk_filter_func writeFilter, gridfs_chunk_filter_func readFilter, gridfs_pending_data_size_func pendingDataNeededSize) {
  gridfs_write_filter = writeFilter;
  gridfs_read_filter = readFilter;
  gridfs_pending_data_size = pendingDataNeededSize; 
}

static bson *chunk_new(bson_oid_t id, int chunkNumber, char** dataBuf, const char* srcData, size_t len, int flags ) {
  bson *b = bson_alloc();
  size_t dataBufLen = 0;

  if( gridfs_write_filter( dataBuf, &dataBufLen, srcData, len, flags) != 0 ) {
    return NULL;
  }
  bson_init_size(b, (int) dataBufLen + 128); /* a little space for field names, files_id, and n */
  bson_append_oid(b, "files_id", &id);
  bson_append_int(b, "n", chunkNumber);
  bson_append_binary(b, "data", BSON_BIN_BINARY, *dataBuf, (int)dataBufLen);
  bson_finish(b);
  return b;
}

static void chunk_free(bson *oChunk) {
  if( oChunk ) {
    bson_destroy(oChunk);
    bson_dealloc(oChunk);
  }
}
/* End of memory allocation functions */

/* -------------- */
/* gridfs methods */
/* -------------- */

/* gridfs constructor */
MONGO_EXPORT int gridfs_init(mongo *client, const char *dbname, const char *prefix, gridfs *gfs) {

  bson b;

  gfs->caseInsensitive = 0;
  gfs->client = client;

  /* Allocate space to own the dbname */
  gfs->dbname = (const char*)bson_malloc((int)strlen(dbname) + 1);
  strcpy((char*)gfs->dbname, dbname);

  /* Allocate space to own the prefix */
  if (prefix == NULL) {
    prefix = "fs";
  }
  gfs->prefix = (const char*)bson_malloc((int)strlen(prefix) + 1);
  strcpy((char*)gfs->prefix, prefix);

  /* Allocate space to own files_ns */
  gfs->files_ns = (const char*)bson_malloc((int)(strlen(prefix) + strlen(dbname) + strlen(".files") + 2));
  strcpy((char*)gfs->files_ns, dbname);
  strcat((char*)gfs->files_ns, ".");
  strcat((char*)gfs->files_ns, prefix);
  strcat((char*)gfs->files_ns, ".files");

  /* Allocate space to own chunks_ns */
  gfs->chunks_ns = (const char*)bson_malloc((int)(strlen(prefix) + strlen(dbname) + strlen(".chunks") + 2));
  strcpy((char*)gfs->chunks_ns, dbname);
  strcat((char*)gfs->chunks_ns, ".");
  strcat((char*)gfs->chunks_ns, prefix);
  strcat((char*)gfs->chunks_ns, ".chunks");

  bson_init(&b);
  bson_append_int(&b, "filename", 1);
  bson_finish(&b);
  if( mongo_create_index(gfs->client, gfs->files_ns, &b, NULL, 0, NULL) != MONGO_OK) {
    bson_destroy( &b );
    gridfs_destroy( gfs );
    return MONGO_ERROR;
  }
  bson_destroy(&b);

  bson_init(&b);
  bson_append_int(&b, "files_id", 1);
  bson_append_int(&b, "n", 1);
  bson_finish(&b);
  if( mongo_create_index(gfs->client, gfs->chunks_ns, &b, NULL, MONGO_INDEX_UNIQUE, NULL) != MONGO_OK ) {
    bson_destroy(&b);
    gridfs_destroy( gfs );    
    return MONGO_ERROR;
  }
  bson_destroy(&b);

  return MONGO_OK;
}

/* gridfs destructor */
MONGO_EXPORT void gridfs_destroy(gridfs *gfs) {
  if( gfs == NULL ) return;
  if( gfs->dbname ) {
    bson_free((char*)gfs->dbname);
    gfs->dbname = NULL;
  }
  if( gfs->prefix ) {
    bson_free((char*)gfs->prefix);
    gfs->prefix = NULL;
  }
  if( gfs->files_ns ) {
    bson_free((char*)gfs->files_ns);
    gfs->files_ns = NULL;
  }
  if( gfs->chunks_ns ) {
    bson_free((char*)gfs->chunks_ns);
    gfs->chunks_ns = NULL;
  }      
}

/* gridfs accesors */

MONGO_EXPORT bson_bool_t gridfs_get_caseInsensitive( const gridfs *gfs ) {
  return gfs->caseInsensitive;
}

MONGO_EXPORT void gridfs_set_caseInsensitive(gridfs *gfs, bson_bool_t newValue){
  gfs->caseInsensitive = newValue;
}

static int bson_append_string_uppercase( bson *b, const char *name, const char *str, bson_bool_t upperCase ) {
  char *strUpperCase;
  if ( upperCase ) {
    int res; 
    strUpperCase = (char *) bson_malloc( (int) strlen( str ) + 1 );
    strcpy(strUpperCase, str);
    _strupr(strUpperCase);
    res = bson_append_string( b, name, strUpperCase );
    bson_free( strUpperCase );
    return res;
  } else {
    return bson_append_string( b, name, str );
  }
}

static int gridfs_insert_file(gridfs *gfs, const char *name, const bson_oid_t id, gridfs_offset length, const char *contenttype, int flags, int chunkSize) {
  bson command[1];
  bson ret[1];
  bson res[1];
  bson_iterator it[1];
  bson q[1];
  int result;
  int64_t d;

  /* If you don't care about calculating MD5 hash for a particular file, simply pass the GRIDFILE_NOMD5 value on the flag param */
  if( !( flags & GRIDFILE_NOMD5 ) ) {  
    /* Check run md5 */
    bson_init(command);
    bson_append_oid(command, "filemd5", &id);
    bson_append_string(command, "root", gfs->prefix);
    bson_finish(command);
    result = mongo_run_command(gfs->client, gfs->dbname, command, res);
    bson_destroy(command);
    if (result != MONGO_OK) 
      return result;
  } 

  /* Create and insert BSON for file metadata */
  bson_init(ret);
  bson_append_oid(ret, "_id", &id);
  if (name != NULL &&  *name != '\0') {
    bson_append_string_uppercase( ret, "filename", name, gfs->caseInsensitive );
  }
  bson_append_long(ret, "length", length);
  bson_append_int(ret, "chunkSize", chunkSize);
  d = (bson_date_t)1000 * time(NULL);
  bson_append_date(ret, "uploadDate", d);
  if( !( flags & GRIDFILE_NOMD5 ) ) {
    bson_find(it, res, "md5");
    bson_append_string(ret, "md5", bson_iterator_string(it));
    bson_destroy(res);
  } else {
    bson_append_string(ret, "md5", ""); 
  } 
  if (contenttype != NULL &&  *contenttype != '\0') {
    bson_append_string(ret, "contentType", contenttype);
  }
  if ( gfs->caseInsensitive ) {
    bson_append_string(ret, "realFilename", name);
  }
  bson_append_int(ret, "flags", flags);
  bson_finish(ret);

  bson_init(q);
  bson_append_oid(q, "_id", &id);
  bson_finish(q);

  result = mongo_update(gfs->client, gfs->files_ns, q, ret, MONGO_UPDATE_UPSERT, NULL);

  bson_destroy(ret);
  bson_destroy(q);
  
  return result;
}

MONGO_EXPORT int gridfs_store_buffer(gridfs *gfs, const char *data, gridfs_offset length, const char *remotename, const char *contenttype, int flags ) {
  gridfile gfile;
  gridfs_offset bytes_written;
  
  gridfile_init( gfs, NULL, &gfile );
  gridfile_writer_init( &gfile, gfs, remotename, contenttype, flags );
  
  bytes_written = gridfile_write_buffer( &gfile, data, length );

  gridfile_writer_done( &gfile );
  gridfile_destroy( &gfile );

  return bytes_written == length ? MONGO_OK : MONGO_ERROR;
}

MONGO_EXPORT int gridfs_store_file(gridfs *gfs, const char *filename, const char *remotename, const char *contenttype, int flags ) {
  char buffer[DEFAULT_CHUNK_SIZE];
  FILE *fd;    
  gridfs_offset chunkLen;
  gridfile gfile;
  gridfs_offset bytes_written = 0;

  /* Open the file and the correct stream */
  if (strcmp(filename, "-") == 0) {
    fd = stdin;
  } else {
    fd = fopen(filename, "rb");
    if (fd == NULL) {
      return MONGO_ERROR;
    } 
  }

  /* Optional Remote Name */
  if (remotename == NULL ||  *remotename == '\0') {
    remotename = filename;
  }

  if( gridfile_init( gfs, NULL, &gfile ) != MONGO_OK ) return MONGO_ERROR;
  if( gridfile_writer_init( &gfile, gfs, remotename, contenttype, flags ) != MONGO_OK ){
    gridfile_destroy( &gfile );
    return MONGO_ERROR; 
  }

  chunkLen = fread(buffer, 1, DEFAULT_CHUNK_SIZE, fd);
  while( chunkLen != 0 ) {
    bytes_written = gridfile_write_buffer( &gfile, buffer, chunkLen );
    if( bytes_written != chunkLen ) break;
    chunkLen = fread(buffer, 1, DEFAULT_CHUNK_SIZE, fd);
  }

  gridfile_writer_done( &gfile );
  gridfile_destroy( &gfile );

  /* Close the file stream */
  if ( fd != stdin ) {
    fclose( fd );
  }   
  return ( chunkLen == 0) || ( bytes_written == chunkLen ) ? MONGO_OK : MONGO_ERROR;  
}

MONGO_EXPORT int gridfs_remove_filename(gridfs *gfs, const char *filename) {
  bson query[1];
  mongo_cursor *files;
  bson file[1];
  bson_iterator it[1];
  bson_oid_t id;
  bson b[1];
  int ret = MONGO_ERROR;

  bson_init(query);
  bson_append_string_uppercase( query, "filename", filename, gfs->caseInsensitive );
  bson_finish(query);
  files = mongo_find(gfs->client, gfs->files_ns, query, NULL, 0, 0, 0);
  bson_destroy(query);

  /* files should be a valid cursor even if the file doesn't exist */
  if ( files == NULL ) return MONGO_ERROR; 

  /* Remove each file and it's chunks from files named filename */
  while (mongo_cursor_next(files) == MONGO_OK) {
    *file = files->current;
    bson_find(it, file, "_id");
    id =  *bson_iterator_oid(it);

    /* Remove the file with the specified id */
    bson_init(b);
    bson_append_oid(b, "_id", &id);
    bson_finish(b);
    mongo_remove(gfs->client, gfs->files_ns, b, NULL);
    bson_destroy(b);

    /* Remove all chunks from the file with the specified id */
    bson_init(b);
    bson_append_oid(b, "files_id", &id);
    bson_finish(b);
    ret = mongo_remove(gfs->client, gfs->chunks_ns, b, NULL);
    bson_destroy(b);
  }

  mongo_cursor_destroy(files);
  return ret;
}

MONGO_EXPORT int gridfs_find_query( gridfs *gfs, const bson *query, gridfile *gfile ) {

  bson uploadDate[1];
  bson finalQuery[1];
  bson out[1];
  int i;

  bson_init(uploadDate);
  bson_append_int(uploadDate, "uploadDate",  - 1);
  bson_finish(uploadDate);

  bson_init(finalQuery);
  bson_append_bson(finalQuery, "query", query);
  bson_append_bson(finalQuery, "orderby", uploadDate);
  bson_finish(finalQuery);

  i = (mongo_find_one(gfs->client, gfs->files_ns,  finalQuery, NULL, out) == MONGO_OK);
  bson_destroy(uploadDate);
  bson_destroy(finalQuery);
  if (!i) {
    return MONGO_ERROR;
  } else {
    gridfile_init(gfs, out, gfile);
    bson_destroy(out);
    return MONGO_OK;
  }
}

MONGO_EXPORT int gridfs_find_filename(gridfs *gfs, const char *filename, gridfile *gfile){
  bson query[1];
  int res;

  bson_init(query);
  bson_append_string_uppercase( query, "filename", filename, gfs->caseInsensitive );
  bson_finish(query);
  res = gridfs_find_query(gfs, query, gfile);
  bson_destroy(query);
  return res;
}

/* ---------------- */
/* gridfile methods */
/* ---------------- */

/* gridfile private methods forward declarations */
static int gridfile_flush_pendingchunk(gridfile *gfile);
static void gridfile_init_flags(gridfile *gfile);
static void gridfile_init_length(gridfile *gfile);
static void gridfile_init_chunkSize(gridfile *gfile);

/* gridfile constructors, destructors and memory management */

MONGO_EXPORT int gridfile_init( gridfs *gfs, const bson *meta, gridfile *gfile ) {
  gfile->gfs = gfs;
  gfile->pos = 0;
  gfile->pending_len = 0;
  gfile->pending_data = NULL;
  gfile->meta = bson_alloc();
  if (gfile->meta == NULL) {
    return MONGO_ERROR;
  } if( meta ) { 
    bson_copy(gfile->meta, meta);
  } else {
    bson_init_empty(gfile->meta);
  }
  gridfile_init_chunkSize( gfile );
  gridfile_init_length( gfile );
  gridfile_init_flags( gfile );
  return MONGO_OK;
}

MONGO_EXPORT int gridfile_writer_done(gridfile *gfile) {

  int response = MONGO_OK;

  if (gfile->pending_len) {
    /* write any remaining pending chunk data.
     * pending data will always take up less than one chunk */
    response = gridfile_flush_pendingchunk(gfile);    
  }
  if( gfile->pending_data ) {
    bson_free(gfile->pending_data);    
    gfile->pending_data = NULL;   
  }
  if( response == MONGO_OK ) {
    /* insert into files collection */
    response = gridfs_insert_file(gfile->gfs, gfile->remote_name, gfile->id, gfile->length, gfile->content_type, gfile->flags, gfile->chunkSize);
  }
  if( gfile->remote_name ) {
    bson_free(gfile->remote_name);
    gfile->remote_name = NULL;
  }
  if( gfile->content_type ) {
    bson_free(gfile->content_type);
    gfile->content_type = NULL;
  }
  return response;
}

static void gridfile_init_chunkSize(gridfile *gfile){
    bson_iterator it[1];

    if (bson_find(it, gfile->meta, "chunkSize") != BSON_EOO)
        if (bson_iterator_type(it) == BSON_INT)
            gfile->chunkSize = bson_iterator_int(it);
        else
            gfile->chunkSize = (int)bson_iterator_long(it);
    else
        gfile->chunkSize = DEFAULT_CHUNK_SIZE;
}

static void gridfile_init_length(gridfile *gfile) {
    bson_iterator it[1];

    if (bson_find(it, gfile->meta, "length") != BSON_EOO)
        if (bson_iterator_type(it) == BSON_INT)
            gfile->length = (gridfs_offset)bson_iterator_int(it);
        else
            gfile->length = (gridfs_offset)bson_iterator_long(it);
    else
        gfile->length = 0;
}

static void gridfile_init_flags(gridfile *gfile) {
  bson_iterator it[1];

  if( bson_find(it, gfile->meta, "flags") != BSON_EOO )
    gfile->flags = bson_iterator_int(it);
  else
    gfile->flags = 0;
}

MONGO_EXPORT int gridfile_writer_init(gridfile *gfile, gridfs *gfs, const char *remote_name, const char *content_type, int flags ) {
  gridfile tmpFile;

  gfile->gfs = gfs;
  if (gridfs_find_filename(gfs, remote_name, &tmpFile) == MONGO_OK) {
    if( gridfile_exists(&tmpFile) ) {
      /* If file exists, then let's initialize members dedicated to coordinate writing operations 
       with existing file metadata */
      gfile->id = gridfile_get_id( &tmpFile );
      gridfile_init_length( &tmpFile );            
      gfile->length = tmpFile.length;  
      gfile->chunkSize = gridfile_get_chunksize( gfile );
      if( flags != GRIDFILE_DEFAULT) {
        gfile->flags = flags;
      } else {
        gridfile_init_flags( &tmpFile );
        gfile->flags = tmpFile.flags;
      }
    }
    gridfile_destroy( &tmpFile );
  } else {
    /* File doesn't exist, let's create a new bson id and initialize length to zero */
    bson_oid_gen(&(gfile->id));
    gfile->length = 0;
    /* File doesn't exist, lets use the flags passed as a parameter to this procedure call */
    gfile->flags = flags;
  }  

  /* We initialize chunk_num with zero, but it will get always calculated when calling 
     gridfile_load_pending_data_with_pos_chunk() or when calling gridfile_write_buffer() */
  gfile->chunk_num = 0; 
  gfile->pos = 0;

  gfile->remote_name = (char*)bson_malloc((int)strlen(remote_name) + 1);
  strcpy((char*)gfile->remote_name, remote_name);

  gfile->content_type = (char*)bson_malloc((int)strlen(content_type) + 1);
  strcpy((char*)gfile->content_type, content_type);  

  gfile->pending_len = 0;
  /* Let's pre-allocate DEFAULT_CHUNK_SIZE bytes into pending_data then we don't need to worry 
     about doing realloc everywhere we want use the pending_data buffer */
  gfile->pending_data = (char*) bson_malloc((int)gridfs_pending_data_size(gfile->flags));

  return MONGO_OK;
}

MONGO_EXPORT void gridfile_destroy(gridfile *gfile) {
  if( gfile->meta ) { 
    bson_destroy(gfile->meta);
    bson_dealloc(gfile->meta);
    gfile->meta = NULL;
  }  
}

/* gridfile accessors */

MONGO_EXPORT bson_oid_t gridfile_get_id( const gridfile *gfile ) {
  bson_iterator it[1];

    if (bson_find(it, gfile->meta, "_id") != BSON_EOO)
        if (bson_iterator_type(it) == BSON_OID)
            return *bson_iterator_oid(it);
        else
            return gfile->id;
    else
        return gfile->id;
}

MONGO_EXPORT bson_bool_t gridfile_exists( const gridfile *gfile ) {
  /* File exists if gfile and gfile->meta BOTH are != NULL */
  return (bson_bool_t)(gfile != NULL && gfile->meta != NULL);
}

MONGO_EXPORT const char *gridfile_get_filename( const gridfile *gfile ) {
    bson_iterator it[1];

    if (gfile->gfs->caseInsensitive && bson_find( it, gfile->meta, "realFilename" ) != BSON_EOO)
        return bson_iterator_string(it); 
    if (bson_find(it, gfile->meta, "filename") != BSON_EOO)
        return bson_iterator_string(it);
    else
        return gfile->remote_name;
}

MONGO_EXPORT int gridfile_get_chunksize( const gridfile *gfile ) {
    bson_iterator it[1];

    if (gfile->chunkSize)
        return gfile->chunkSize;
    else if (bson_find(it, gfile->meta, "chunkSize") != BSON_EOO)
        return bson_iterator_int(it);
    else
        return DEFAULT_CHUNK_SIZE;
}

MONGO_EXPORT gridfs_offset gridfile_get_contentlength( const gridfile *gfile ) {
  gridfs_offset estimatedLen;
  estimatedLen = gfile->pending_len ? gfile->chunk_num * gridfile_get_chunksize( gfile ) + gfile->pending_len : gfile->length;
  return MAX( estimatedLen, gfile->length );
}

MONGO_EXPORT const char *gridfile_get_contenttype( const gridfile *gfile ) {
    bson_iterator it[1];

    if ( bson_find(it, gfile->meta, "contentType") != BSON_EOO )
        return bson_iterator_string(it);
    else
        return NULL;
}

MONGO_EXPORT bson_date_t gridfile_get_uploaddate( const gridfile *gfile ) {
    bson_iterator it[1];

    if (bson_find(it, gfile->meta, "uploadDate") != BSON_EOO)
        return bson_iterator_date(it);
    else
        return 0;
}

MONGO_EXPORT const char *gridfile_get_md5( const gridfile *gfile ) {
    bson_iterator it[1];

    if (bson_find(it, gfile->meta, "md5") != BSON_EOO )
        return bson_iterator_string(it);
    else
        return NULL;
}

MONGO_EXPORT void gridfile_set_flags(gridfile *gfile, int flags) {
    gfile->flags = flags;
}

MONGO_EXPORT int gridfile_get_flags( const gridfile *gfile ) {
  return gfile->flags;
}

MONGO_EXPORT const char *gridfile_get_field(gridfile *gfile, const char *name) {
    bson_iterator it[1];

    if (bson_find(it, gfile->meta, name) != BSON_EOO)
        return bson_iterator_value(it);
    else
        return NULL;
}

MONGO_EXPORT bson_bool_t gridfile_get_boolean( const gridfile *gfile, const char *name ) {
    bson_iterator it[1];

    if (bson_find(it, gfile->meta, name) != BSON_EOO)
        return bson_iterator_bool(it);
    else
        return 0;
}

MONGO_EXPORT void gridfile_get_metadata( const gridfile *gfile, bson *out, bson_bool_t copyData ) {
    bson_iterator it[1];

    if (bson_find(it, gfile->meta, "metadata") != BSON_EOO)
        bson_iterator_subobject_init(it, out, copyData);
    else
        bson_init_empty(out);
}

/* ++++++++++++++++++++++++++++++++ */
/* gridfile data management methods */
/* ++++++++++++++++++++++++++++++++ */

MONGO_EXPORT int gridfile_get_numchunks( const gridfile *gfile ) {
    bson_iterator it[1];
    gridfs_offset length;
    gridfs_offset chunkSize;
    double numchunks;

    bson_find(it, gfile->meta, "length");

    if (bson_iterator_type(it) == BSON_INT)
        length = (gridfs_offset)bson_iterator_int(it);
    else
        length = (gridfs_offset)bson_iterator_long(it);
 
    bson_find(it, gfile->meta, "chunkSize");
    chunkSize = bson_iterator_int(it);
    numchunks = ((double)length / (double)chunkSize);
    return (numchunks - (int)numchunks > 0) ? (int)(numchunks + 1): (int)(numchunks);
}

static void gridfile_prepare_chunk_key_bson(bson *q, bson_oid_t *id, int chunk_num) {
  bson_init(q);
  bson_append_int(q, "n", chunk_num);
  bson_append_oid(q, "files_id", id);
  bson_finish(q);
}

static int gridfile_flush_pendingchunk(gridfile *gfile) {
    bson *oChunk;
    bson q[1];
    char* targetBuf = NULL;
    int res = MONGO_OK;

    if (gfile->pending_len) {
        size_t finish_position_after_flush;
        oChunk = chunk_new( gfile->id, gfile->chunk_num, &targetBuf, gfile->pending_data, gfile->pending_len, gfile->flags );
        gridfile_prepare_chunk_key_bson( q, &gfile->id, gfile->chunk_num );    
        res = mongo_update(gfile->gfs->client, gfile->gfs->chunks_ns, q, oChunk, MONGO_UPDATE_UPSERT, NULL);
        bson_destroy(q);
        chunk_free(oChunk);    
        if( res == MONGO_OK ){      
            finish_position_after_flush = (gfile->chunk_num * gfile->chunkSize) + gfile->pending_len;
            if (finish_position_after_flush > gfile->length)
                gfile->length = finish_position_after_flush;
            gfile->chunk_num++;
            gfile->pending_len = 0;
        }
    }
    if (targetBuf && targetBuf != gfile->pending_data)
        bson_free( targetBuf );
    return res;
}

static int gridfile_load_pending_data_with_pos_chunk(gridfile *gfile) {
  int chunk_len;
  const char *chunk_data;
  bson_iterator it[1];
  bson chk;
  char* targetBuffer = NULL;
  size_t targetBufferLen = 0;

  chk.dataSize = 0;
  gridfile_get_chunk(gfile, (int)(gfile->pos / DEFAULT_CHUNK_SIZE), &chk);
  if (chk.dataSize <= 5) {
        if( chk.data ) {
            bson_destroy( &chk );
        }
        return MONGO_ERROR;
  }
  if( bson_find(it, &chk, "data") != BSON_EOO){
    chunk_len = bson_iterator_bin_len(it);
    chunk_data = bson_iterator_bin_data(it);
    gridfs_read_filter( &targetBuffer, &targetBufferLen, chunk_data, (size_t)chunk_len, gfile->flags );
    gfile->pending_len = (int)targetBufferLen;
    gfile->chunk_num = (int)(gfile->pos / DEFAULT_CHUNK_SIZE);
    if( targetBufferLen ) {
      memcpy(gfile->pending_data, targetBuffer, targetBufferLen);
    }
  } else {
    bson_destroy( &chk );
    return MONGO_ERROR;
  }
  bson_destroy( &chk );
  if( targetBuffer && targetBuffer != chunk_data )
    bson_free( targetBuffer );
  return MONGO_OK;
}

MONGO_EXPORT gridfs_offset gridfile_write_buffer(gridfile *gfile, const char *data, gridfs_offset length) {

  bson *oChunk;
  bson q[1];
  size_t buf_pos, buf_bytes_to_write;    
  gridfs_offset bytes_left = length;
  char* targetBuf = NULL;
  int memAllocated = 0;

  gfile->chunk_num = (int)(gfile->pos / DEFAULT_CHUNK_SIZE);
  buf_pos = (int)(gfile->pos - (gfile->pos / DEFAULT_CHUNK_SIZE) * DEFAULT_CHUNK_SIZE);
  /* First let's see if our current position is an an offset > 0 from the beginning of the current chunk. 
     If so, then we need to preload current chunk and merge the data into it using the pending_data field
     of the gridfile gfile object. We will flush the data if we fill in the chunk */
  if( buf_pos ) {
    if( !gfile->pending_len && gridfile_load_pending_data_with_pos_chunk( gfile ) != MONGO_OK ) return 0;           
    buf_bytes_to_write = (size_t)MIN( length, DEFAULT_CHUNK_SIZE - buf_pos );
    memcpy( &gfile->pending_data[buf_pos], data, buf_bytes_to_write);
    if ( buf_bytes_to_write + buf_pos > gfile->pending_len ) {
      gfile->pending_len = buf_bytes_to_write + buf_pos;
    }
    gfile->pos += buf_bytes_to_write;
    if( buf_bytes_to_write + buf_pos >= DEFAULT_CHUNK_SIZE && gridfile_flush_pendingchunk(gfile) != MONGO_OK ) return 0;
    bytes_left -= buf_bytes_to_write;
    data += buf_bytes_to_write;
  }

  /* If there's still more data to be written and they happen to be full chunks, we will loop thru and 
     write all full chunks without the need for preloading the existing chunk */
  while( bytes_left >= DEFAULT_CHUNK_SIZE ) {
    int res; 
    if( (oChunk = chunk_new( gfile->id, gfile->chunk_num, &targetBuf, data, DEFAULT_CHUNK_SIZE, gfile->flags )) == NULL) return length - bytes_left;
    memAllocated = targetBuf != data;
    gridfile_prepare_chunk_key_bson(q, &gfile->id, gfile->chunk_num);
    res = mongo_update(gfile->gfs->client, gfile->gfs->chunks_ns, q, oChunk, MONGO_UPDATE_UPSERT, NULL);
    bson_destroy(q );
    chunk_free(oChunk);
    if( res != MONGO_OK ) return length - bytes_left;
    bytes_left -= DEFAULT_CHUNK_SIZE;
    gfile->chunk_num++;
    gfile->pos += DEFAULT_CHUNK_SIZE;
    if (gfile->pos > gfile->length) {
      gfile->length = gfile->pos;
    }
    data += DEFAULT_CHUNK_SIZE;
  }  

  /* Finally, if there's still remaining bytes left to write, we will preload the current chunk and merge the 
     remaining bytes into pending_data buffer */
  if ( bytes_left > 0 ) {
    /* Let's preload the chunk we are writing IF the current chunk is not already in memory
       AND if after writing the remaining buffer there's should be trailing data that we don't
       want to loose */ 
    if( !gfile->pending_len && gfile->pos + bytes_left < gfile->length && gridfile_load_pending_data_with_pos_chunk( gfile ) != MONGO_OK ) 
      return length - bytes_left;
    memcpy( gfile->pending_data, data, (size_t) bytes_left );
    if(  bytes_left > gfile->pending_len )
      gfile->pending_len = (int) bytes_left;
    gfile->pos += bytes_left;  
  }

  if( memAllocated ){
    bson_free( targetBuf );
  }
  return length;
}

MONGO_EXPORT void gridfile_get_chunk(gridfile *gfile, int n, bson *out) {
  bson query[1];

  bson_oid_t id;
  int result;

  bson_init(query);  
  id = gridfile_get_id( gfile );
  bson_append_oid(query, "files_id", &id);
  bson_append_int(query, "n", n);
  bson_finish(query);

  result = (mongo_find_one(gfile->gfs->client, gfile->gfs->chunks_ns, query, NULL, out) == MONGO_OK);
  bson_destroy(query);
  if (!result)
    bson_copy(out, bson_shared_empty());
}

MONGO_EXPORT mongo_cursor *gridfile_get_chunks(gridfile *gfile, size_t start, size_t size) {
  bson_iterator it[1];
  bson_oid_t id;
  bson gte[1];
  bson query[1];
  bson orderby[1];
  bson command[1];
  mongo_cursor *cursor;

  if( bson_find(it, gfile->meta, "_id") != BSON_EOO)
    id =  *bson_iterator_oid(it);
  else
    id = gfile->id;

  bson_init(query);
  bson_append_oid(query, "files_id", &id);
  if (size == 1) {
    bson_append_int(query, "n", (int)start);
  } else {
    bson_init(gte);
    bson_append_int(gte, "$gte", (int)start);
    bson_finish(gte);
    bson_append_bson(query, "n", gte);
    bson_destroy(gte);
  }
  bson_finish(query);

  bson_init(orderby);
  bson_append_int(orderby, "n", 1);
  bson_finish(orderby);

  bson_init(command);
  bson_append_bson(command, "query", query);
  bson_append_bson(command, "orderby", orderby);
  bson_finish(command);

  cursor = mongo_find(gfile->gfs->client, gfile->gfs->chunks_ns,  command, NULL, (int)size, 0, 0);

  bson_destroy(command);
  bson_destroy(query);
  bson_destroy(orderby);

  return cursor;
}

static gridfs_offset gridfile_read_from_pending_buffer(gridfile *gfile, gridfs_offset totalBytesToRead, char* buf, int *first_chunk);
static gridfs_offset gridfile_load_from_chunks(gridfile *gfile, int total_chunks, gridfs_offset chunksize, mongo_cursor *chunks, char* buf, 
                                               gridfs_offset bytes_left);

MONGO_EXPORT gridfs_offset gridfile_read_buffer( gridfile *gfile, char *buf, gridfs_offset size ) {
  mongo_cursor *chunks;  

  int first_chunk;  
  int total_chunks;
  gridfs_offset chunksize;
  gridfs_offset contentlength;
  gridfs_offset bytes_left;
  gridfs_offset realSize = 0;

  contentlength = gridfile_get_contentlength(gfile);
  chunksize = gridfile_get_chunksize(gfile);
  size = MIN( contentlength - gfile->pos, size );
  bytes_left = size;

  first_chunk = (int)((gfile->pos) / chunksize);  
  total_chunks = (int)((gfile->pos + size - 1) / chunksize) - first_chunk + 1;
  
  if( (realSize = gridfile_read_from_pending_buffer( gfile, bytes_left, buf, &first_chunk )) > 0 ) {
    gfile->pos += realSize;    
    if( --total_chunks <= 0) {
      return realSize;
    }
    buf += realSize;
    bytes_left -= realSize;
    if( gridfile_flush_pendingchunk( gfile ) != MONGO_OK ){
      /* Let's abort the read operation here because we could not flush the buffer */
      return realSize; 
    }
  }; 

  chunks = gridfile_get_chunks(gfile, first_chunk, total_chunks);
  realSize += gridfile_load_from_chunks( gfile, total_chunks, chunksize, chunks, buf, bytes_left);  
  mongo_cursor_destroy(chunks);

  gfile->pos += realSize;

  return realSize;
}

static gridfs_offset gridfile_read_from_pending_buffer(gridfile *gfile, gridfs_offset totalBytesToRead, char* buf, 
                                                       int *first_chunk){
  gridfs_offset realSize = 0;
  if( gfile->pending_len > 0 && *first_chunk == gfile->chunk_num) {    
    char *chunk_data;
    gridfs_offset chunksize = gridfile_get_chunksize(gfile);
    gridfs_offset ofs = gfile->pos - gfile->chunk_num * chunksize;
    realSize = MIN( totalBytesToRead, gfile->pending_len - ofs );
    chunk_data = gfile->pending_data + ofs;
    memcpy( buf, chunk_data, (size_t)realSize );                
    (*first_chunk)++; 
  }; 
  return realSize;     
}

static gridfs_offset gridfile_fill_buf_from_chunk(gridfile *gfile, const bson *chunk, gridfs_offset chunksize, char **buf, int *allocatedMem, char **targetBuf, 
                                                  size_t *targetBufLen, gridfs_offset *bytes_left, int chunkNo);

static gridfs_offset gridfile_load_from_chunks(gridfile *gfile, int total_chunks, gridfs_offset chunksize, mongo_cursor *chunks, char* buf, 
                                               gridfs_offset bytes_left){
  int i;
  char* targetBuf = NULL; 
  size_t targetBufLen = 0;
  int allocatedMem = 0;
  gridfs_offset realSize = 0;
  
  for (i = 0; i < total_chunks; i++) {
    if( mongo_cursor_next(chunks) != MONGO_OK ){
      break;
    }
    realSize += gridfile_fill_buf_from_chunk( gfile, &chunks->current, chunksize, &buf, &allocatedMem, &targetBuf, &targetBufLen, &bytes_left, i); 
  }
  if( allocatedMem ) {
    bson_free( targetBuf );
  }
  return realSize;
}

static gridfs_offset gridfile_fill_buf_from_chunk(gridfile *gfile, const bson *chunk, gridfs_offset chunksize, char **buf, int *allocatedMem, char **targetBuf, 
                                                  size_t *targetBufLen, gridfs_offset *bytes_left, int chunkNo){
  bson_iterator it[1];
  gridfs_offset chunk_len;
  const char *chunk_data;

  if( bson_find(it, chunk, "data") != BSON_EOO ) {
    chunk_len = bson_iterator_bin_len(it);
    chunk_data = bson_iterator_bin_data(it);  
    if( gridfs_read_filter( targetBuf, targetBufLen, chunk_data, (size_t)chunk_len, gfile->flags ) != 0) return 0;
    *allocatedMem = *targetBuf != chunk_data;
    chunk_data = *targetBuf;
    if (chunkNo == 0) {      
      chunk_data += (gfile->pos) % chunksize;
      *targetBufLen -= (size_t)( (gfile->pos) % chunksize );
    } 
    if (*bytes_left > *targetBufLen) {
      memcpy(*buf, chunk_data, *targetBufLen);
      *bytes_left -= *targetBufLen; 
      *buf += *targetBufLen;
      return *targetBufLen;
    } else {
      memcpy(*buf, chunk_data, (size_t)(*bytes_left));
      return *bytes_left;
    }
  } else {
    bson_fatal_msg( 0, "Chunk object doesn't have 'data' attribute" );
    return 0;
  }
}

MONGO_EXPORT gridfs_offset gridfile_seek(gridfile *gfile, gridfs_offset offset) {
  gridfs_offset length;
  gridfs_offset chunkSize;
  gridfs_offset newPos;

  chunkSize = gridfile_get_chunksize( gfile );
  length = gridfile_get_contentlength( gfile );
  newPos = MIN( length, offset );

  /* If we are seeking to the next chunk or prior to the current chunks let's flush the pending chunk */
  if (gfile->pending_len && (newPos >= (gfile->chunk_num + 1) * chunkSize || newPos < gfile->chunk_num * chunkSize) &&
    gridfile_flush_pendingchunk( gfile ) != MONGO_OK ) return gfile->pos;  
  gfile->pos = newPos;
  return newPos;
}

MONGO_EXPORT gridfs_offset gridfile_write_file(gridfile *gfile, FILE *stream) {
  char buffer[DEFAULT_CHUNK_SIZE];
  size_t data_read, data_written = 0;  
  gridfs_offset total_written = 0;

  do {
    data_read = (size_t)gridfile_read_buffer( gfile, buffer, DEFAULT_CHUNK_SIZE );
    if( data_read > 0 ){
      data_written = fwrite( buffer, sizeof(char), data_read, stream );
      total_written += data_written;              
    }    
  } while(( data_read > 0 ) && ( data_written == data_read ));

  return total_written;
}

static int gridfile_remove_chunks( gridfile *gfile, int deleteFromChunk){
  bson q[1];
  bson_oid_t id = gridfile_get_id( gfile );
  int res;

  bson_init( q );
  bson_append_oid(q, "files_id", &id);
  if( deleteFromChunk >= 0 ) {
    bson_append_start_object( q, "n" );
    bson_append_int( q, "$gte", deleteFromChunk );
    bson_append_finish_object( q );
  }
  bson_finish( q );
  res = mongo_remove( gfile->gfs->client, gfile->gfs->chunks_ns, q, NULL);
  bson_destroy( q );
  return res;
}

MONGO_EXPORT gridfs_offset gridfile_truncate(gridfile *gfile, gridfs_offset newSize) {

  int deleteFromChunk;

  if ( newSize > gridfile_get_contentlength( gfile ) ) {
    return gridfile_seek( gfile, gridfile_get_contentlength( gfile ) );    
  }
  if( newSize > 0 ) {
    deleteFromChunk = (int)(newSize / gridfile_get_chunksize( gfile )); 
    if( gridfile_seek(gfile, newSize) != newSize ) return gfile->length;
    if( gfile->pos % gridfile_get_chunksize( gfile ) ) {
      if( !gfile->pending_len && gridfile_load_pending_data_with_pos_chunk( gfile ) != MONGO_OK ) return gfile->length;
      gfile->pending_len = gfile->pos % gridfile_get_chunksize( gfile ); /* This will truncate the preloaded chunk */
      if( gridfile_flush_pendingchunk( gfile ) != MONGO_OK ) return gfile->length;
      deleteFromChunk++;
    }
    /* Now let's remove the trailing chunks resulting from truncation */
    if( gridfile_remove_chunks( gfile, deleteFromChunk ) != MONGO_OK ) return gfile->length;
    gfile->length = newSize;
  } else {
    /* Expected file size is zero. We will remove ALL chunks */
    if( gridfile_remove_chunks( gfile, -1 ) != MONGO_OK) return gfile->length;    
    gfile->length = 0;
    gfile->pos = 0;
  }
  return gfile->length;
}

MONGO_EXPORT gridfs_offset gridfile_expand(gridfile *gfile, gridfs_offset bytesToExpand) {
  gridfs_offset fileSize, newSize, curPos, toWrite, bufSize;  

  char* buf;

  fileSize = gridfile_get_contentlength( gfile );
  newSize = fileSize + bytesToExpand;
  curPos = fileSize;
  bufSize = gridfile_get_chunksize ( gfile );
  buf = (char*)bson_malloc( (size_t)bufSize );
  
  memset( buf, 0, (size_t)bufSize );
  gridfile_seek( gfile, fileSize );

  while( curPos < newSize ) {
    toWrite = bufSize - curPos % bufSize;
    if( toWrite + curPos > newSize ) {
      toWrite = newSize - curPos;
    }
    /* If driver doesn't write all data request, we will cancel expansion and return how far we got... */
    if( gridfile_write_buffer( gfile, (const char*)buf, toWrite ) != toWrite) return curPos;
    curPos += toWrite;
  }

  bson_free( buf );
  return newSize;
}

MONGO_EXPORT gridfs_offset gridfile_set_size(gridfile *gfile, gridfs_offset newSize) {
  gridfs_offset fileSize;

  fileSize = gridfile_get_contentlength( gfile );
  if( newSize <= fileSize ) {
    return gridfile_truncate( gfile, newSize );
  } else {            
    return gridfile_expand( gfile, newSize - fileSize );
  }
}
