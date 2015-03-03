/** @file gridfs.h
 *
 *  @brief GridFS declarations
 *
 * */

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

#include "mongo.h"

#ifndef MONGO_GRIDFS_H_
#define MONGO_GRIDFS_H_

enum {DEFAULT_CHUNK_SIZE = 256 * 1024};

typedef uint64_t gridfs_offset;

/* A GridFS represents a single collection of GridFS files in the database. */
typedef struct {
    mongo *client; /**> The client to db-connection. */
    const char *dbname; /**> The root database name */
    const char *prefix; /**> The prefix of the GridFS's collections, default is NULL */
    const char *files_ns; /**> The namespace where the file's metadata is stored */
    const char *chunks_ns; /**. The namespace where the files's data is stored in chunks */
    bson_bool_t caseInsensitive; /**. If true then files are matched in case insensitive fashion */
} gridfs;

/* A GridFile is a single GridFS file. */
typedef struct {
    gridfs *gfs;        /**> The GridFS where the GridFile is located */
    bson *meta;         /**> The GridFile's bson object where all its metadata is located */
    gridfs_offset pos;  /**> The position is the offset in the file */
    bson_oid_t id;      /**> The files_id of the gridfile */
    char *remote_name;  /**> The name of the gridfile as a string */
    char *content_type; /**> The gridfile's content type */
    gridfs_offset length; /**> The length of this gridfile */
    int chunk_num;      /**> The number of the current chunk being written to */
    char *pending_data; /**> A buffer storing data still to be written to chunks */
    size_t pending_len;    /**> Length of pending_data buffer */
    int flags;          /**> Store here special flags such as: No MD5 calculation and Zlib Compression enabled*/
    int chunkSize;   /**> Let's cache here the cache size to avoid accesing it on the Meta mongo object every time is needed */
} gridfile;

enum gridfile_storage_type {
    GRIDFILE_DEFAULT = 0,
    GRIDFILE_NOMD5 = ( 1<<0 )
};

#ifndef _MSC_VER
char *_strupr(char *str);
char *_strlwr(char *str);
#endif

typedef int ( *gridfs_chunk_filter_func )( char** targetBuf, size_t* targetLen, const char* srcBuf, size_t srcLen, int flags );
typedef size_t ( *gridfs_pending_data_size_func ) (int flags);

MONGO_EXPORT gridfs* gridfs_alloc( void );
MONGO_EXPORT void gridfs_dealloc(gridfs* gfs);
MONGO_EXPORT gridfile* gridfile_create( void );
MONGO_EXPORT void gridfile_dealloc(gridfile* gf);
MONGO_EXPORT void gridfile_get_descriptor(gridfile* gf, bson* out);
MONGO_EXPORT void gridfs_set_chunk_filter_funcs(gridfs_chunk_filter_func writeFilter, gridfs_chunk_filter_func readFilter, gridfs_pending_data_size_func pendingDataNeededSize);

/**
 *  Initializes a GridFS object
 *  @param client - db connection
 *  @param dbname - database name
 *  @param prefix - collection prefix, default is fs if NULL or empty
 *  @param gfs - the GridFS object to initialize
 *
 *  @return - MONGO_OK or MONGO_ERROR.
 */
MONGO_EXPORT int gridfs_init( mongo *client, const char *dbname,
                              const char *prefix, gridfs *gfs );

/**
 * Destroys a GridFS object. Call this when finished with
 * the object..
 *
 * @param gfs a grid
 */
MONGO_EXPORT void gridfs_destroy( gridfs *gfs );

/**
 *  Initializes a GridFile containing the GridFS and file bson
 *  @param gfs - the GridFS where the GridFile is located
 *  @param meta - the file object
 *  @param gfile - the output GridFile that is being initialized
 *
 *  @return - MONGO_OK or MONGO_ERROR.
 */
MONGO_EXPORT int gridfile_init( gridfs *gfs, const bson *meta, gridfile *gfile );

/**
 *  Destroys the GridFile
 *
 *  @param oGridFIle - the GridFile being destroyed
 */
MONGO_EXPORT void gridfile_destroy( gridfile *gfile );

/**
 *  Initializes a gridfile for writing incrementally with gridfs_write_buffer.
 *  Once initialized, you can write any number of buffers with gridfs_write_buffer.
 *  When done, you must call gridfs_writer_done to save the file metadata.
 *  +-+-+-+-  This modified version of GridFS allows the file to read/write randomly
 *  +-+-+-+-  when using this function
 *
 */
MONGO_EXPORT int gridfile_writer_init( gridfile *gfile, gridfs *gfs, const char *remote_name,
                                       const char *content_type, int flags );

/**
 *  Write to a GridFS file incrementally. You can call this function any number
 *  of times with a new buffer each time. This allows you to effectively
 *  stream to a GridFS file. When finished, be sure to call gridfs_writer_done.
 * 
 *  @param gfile - GridFile to write to
 *  @param data - Pointer to buffer with data to be written
 *  @param length - Size of buffer with data to be written
 *  @return - Number of bytes written. If different from length assume somethind went wrong
 */
MONGO_EXPORT gridfs_offset gridfile_write_buffer( gridfile *gfile, const char *data, gridfs_offset length );

/**
 *  Signal that writing of this gridfile is complete by
 *  writing any buffered chunks along with the entry in the
 *  files collection.
 *
 *  @return - MONGO_OK or MONGO_ERROR.
 */
MONGO_EXPORT int gridfile_writer_done( gridfile *gfile );

/**
 *  Store a buffer as a GridFS file.
 *  @param gfs - the working GridFS
 *  @param data - pointer to buffer to store in GridFS
 *  @param length - length of the buffer
 *  @param remotename - filename for use in the database
 *  @param contenttype - optional MIME type for this object
 *
 *  @return - MONGO_OK or MONGO_ERROR.
 */
MONGO_EXPORT int gridfs_store_buffer( gridfs *gfs, const char *data, gridfs_offset length,
                          const char *remotename,
                          const char *contenttype, int flags );

/**
 *  Open the file referenced by filename and store it as a GridFS file.
 *  @param gfs - the working GridFS
 *  @param filename - local filename relative to the process
 *  @param remotename - optional filename for use in the database
 *  @param contenttype - optional MIME type for this object
 *
 *  @return - MONGO_OK or MONGO_ERROR.
 */
MONGO_EXPORT int gridfs_store_file( gridfs *gfs, const char *filename,
                        const char *remotename, const char *contenttype, int flags );

/**
 *  Removes the files referenced by filename from the db
 *
 *  @param gfs - the working GridFS
 *  @param filename - the filename of the file/s to be removed
 *
 *  @return MONGO_OK if a matching file was removed, and MONGO_ERROR if
 *    an error occurred or the file did not exist
 */
MONGO_EXPORT int gridfs_remove_filename( gridfs *gfs, const char *filename );

/**
 *  Find the first file matching the provided query within the
 *  GridFS files collection, and return the file as a GridFile.
 *
 *  @param gfs - the working GridFS
 *  @param query - a pointer to the bson with the query data
 *  @param gfile - the output GridFile to be initialized
 *
 *  @return MONGO_OK if successful, MONGO_ERROR otherwise
 */
MONGO_EXPORT int gridfs_find_query( gridfs *gfs, const bson *query, gridfile *gfile );

/**
 *  Find the first file referenced by filename within the GridFS
 *  and return it as a GridFile
 *  @param gfs - the working GridFS
 *  @param filename - filename of the file to find
 *  @param gfile - the output GridFile to be intialized
 *
 *  @return MONGO_OK or MONGO_ERROR.
 */
MONGO_EXPORT int gridfs_find_filename( gridfs *gfs, const char *filename, gridfile *gfile );

/**
 *  Returns whether or not the GridFile exists
 *  @param gfile - the GridFile being examined
 */
MONGO_EXPORT bson_bool_t gridfile_exists( const gridfile *gfile );

/**
 *  Returns the filename of GridFile
 *  @param gfile - the working GridFile
 *
 *  @return - the filename of the Gridfile
 */
MONGO_EXPORT const char *gridfile_get_filename( const gridfile *gfile );

/**
 *  Returns the size of the chunks of the GridFile
 *  @param gfile - the working GridFile
 *
 *  @return - the size of the chunks of the Gridfile
 */
MONGO_EXPORT int gridfile_get_chunksize( const gridfile *gfile );

/**
 *  Returns the length of GridFile's data
 *
 *  @param gfile - the working GridFile
 *
 *  @return - the length of the Gridfile's data
 */
MONGO_EXPORT gridfs_offset gridfile_get_contentlength( const gridfile *gfile );

/**
 *  Returns the MIME type of the GridFile
 *
 *  @param gfile - the working GridFile
 *
 *  @return - the MIME type of the Gridfile
 *            (NULL if no type specified)
 */
MONGO_EXPORT const char *gridfile_get_contenttype( const gridfile *gfile );

/**
 *  Returns the upload date of GridFile
 *
 *  @param gfile - the working GridFile
 *
 *  @return - the upload date of the Gridfile
 */
MONGO_EXPORT bson_date_t gridfile_get_uploaddate( const gridfile *gfile );

/**
 *  Returns the MD5 of GridFile
 *
 *  @param gfile - the working GridFile
 *
 *  @return - the MD5 of the Gridfile
 */
MONGO_EXPORT const char *gridfile_get_md5( const gridfile *gfile );

/**
 *  Returns the _id in GridFile specified by name
 *
 *  @param gfile - the working GridFile
 * 
 *  @return - the _id field in metadata
 */
MONGO_EXPORT bson_oid_t gridfile_get_id( const gridfile *gfile );

/**
 *  Returns the field in GridFile specified by name
 *
 *  @param gfile - the working GridFile
 *  @param name - the name of the field to be returned
 *
 *  @return - the data of the field specified
 *            (NULL if none exists)
 */
MONGO_EXPORT const char *gridfile_get_field( gridfile *gfile,
                                             const char *name );

/**
 *  Returns the caseInsensitive flag value of gfs
 *  @param gfile - the working GridFile
 *
 *  @return - the caseInsensitive flag of the gfs
 */
MONGO_EXPORT bson_bool_t gridfs_get_caseInsensitive( const gridfs *gfs );

/**
 *  Sets the caseInsensitive flag value of gfs
 *  @param gfs - the working gfs
 *  @param newValue - the new value for the caseInsensitive flag of gfs
 *
 *  @return - void
 */
MONGO_EXPORT void gridfs_set_caseInsensitive(gridfs *gfs, bson_bool_t newValue);

/**
 *  Sets the flags of the GridFile
 *  @param gfile - the working GridFile
 *  @param flags - the value of the flags to set on the provided GridFile
 *
 *  @return - void
 */
MONGO_EXPORT void gridfile_set_flags(gridfile *gfile, int flags);

/**
 *  gets the flags of the GridFile
 *  @param gfile - the working GridFile
  *
 *  @return - void
 */
MONGO_EXPORT int gridfile_get_flags( const gridfile *gfile );

/**
 *  Returns a boolean field in GridFile specified by name
 *  @param gfile - the working GridFile
 *  @param name - the name of the field to be returned
 *
 *  @return - the boolean of the field specified
 *            (NULL if none exists)
 */
MONGO_EXPORT bson_bool_t gridfile_get_boolean( const gridfile *gfile,
                                  const char *name );

/**
 *  Returns the metadata of GridFile. Calls bson_init_empty on metadata
 *  if none exits.
 *
 * @note When copyData is false, the metadata object becomes invalid
 *       when gfile is destroyed. For either value of copyData, you
 *       must pass the metadata object to bson_destroy when you are
 *       done using it.
 *
 *  @param gfile - the working GridFile
 *  @param metadata an uninitialized BSON object to receive the metadata.
 *  @param copyData when true, makes a copy of the scope data which will remain
 *    valid when the grid file is deallocated.
 */
MONGO_EXPORT void gridfile_get_metadata( const gridfile *gfile, bson* metadata, bson_bool_t copyData );

/**
 *  Returns the number of chunks in the GridFile
 *  @param gfile - the working GridFile
 *
 *  @return - the number of chunks in the Gridfile
 */
MONGO_EXPORT int gridfile_get_numchunks( const gridfile *gfile );

/**
 *  Returns chunk n of GridFile
 *  @param gfile - the working GridFile
 *
 *  @return - the nth chunk of the Gridfile
 */
MONGO_EXPORT void gridfile_get_chunk( gridfile *gfile, int n, bson* out );

/**
 *  Returns a mongo_cursor of *size* chunks starting with chunk *start*
 *
 *  @param gfile - the working GridFile
 *  @param start - the first chunk in the cursor
 *  @param size - the number of chunks to be returned
 *
 *  @return - mongo_cursor of the chunks (must be destroyed after use)
 */
MONGO_EXPORT mongo_cursor *gridfile_get_chunks( gridfile *gfile, size_t start, size_t size );

/**
 *  Writes the GridFile to a stream
 *
 *  @param gfile - the working GridFile
 *  @param stream - the file stream to write to
 */
MONGO_EXPORT gridfs_offset gridfile_write_file( gridfile *gfile, FILE *stream );

/**
 *  Reads length bytes from the GridFile to a buffer
 *  and updates the position in the file.
 *  (assumes the buffer is large enough)
 *  (if size is greater than EOF gridfile_read reads until EOF)
 *
 *  @param gfile - the working GridFile
 *  @param size - the amount of bytes to be read
 *  @param buf - the buffer to read to
 *
 *  @return - the number of bytes read
 */
MONGO_EXPORT gridfs_offset gridfile_read_buffer( gridfile *gfile, char *buf, gridfs_offset size );

/**
 *  Updates the position in the file
 *  (If the offset goes beyond the contentlength,
 *  the position is updated to the end of the file.)
 *
 *  @param gfile - the working GridFile
 *  @param offset - the position to update to
 *
 *  @return - resulting offset location
 */
MONGO_EXPORT gridfs_offset gridfile_seek( gridfile *gfile, gridfs_offset offset );

/**
 *  @param gfile - the working GridFile
 *  @param newSize - the new size after truncation
 *
 */
MONGO_EXPORT gridfs_offset gridfile_truncate(gridfile *gfile, gridfs_offset newSize);

/**
 *  @param gfile - the working GridFile
 *  @param bytesToExpand - number of bytes the file will be expanded
 *
 */
MONGO_EXPORT gridfs_offset gridfile_expand(gridfile *gfile, gridfs_offset bytesToExpand);

/**
 *  @param gfile - the working GridFile
 *  @param newSize - the new size of file
 *
 */
MONGO_EXPORT gridfs_offset gridfile_set_size(gridfile *gfile, gridfs_offset newSize);

#endif
