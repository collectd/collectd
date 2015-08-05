Write Concern
=============

The ``mongo_client`` and ``mongo_replica_set_client`` functions set a default write concern
at the top level specifying acknowledged writes.

In addition to reporting write errors, write concern also allows you to ensure
that your write are replicated to a particular number of servers to a set
of servers tagged with a given value.

Or for very-high write performance, write concern can specify writes without acknowledgment.
This is recommended only for cases that can tolerate the potential loss of a few writes
such as logging, analytics, etc.

The old ``mongo_connect`` and ``mongo_replica_set_connect`` functions have a default write concern
where writes are unacknowledged.
They are deprecated and temporarily available for backward compatibility to smooth transition to
the ``mongo_client`` and ``mongo_replica_set_client`` functions.

Implementation and API
----------------------

Write concern is implemented by appending a call to the ``getlasterror``
command after each write.  You can certainly do this manually, but nearly all of the drivers
provide a write concern API for simplicty. To read about the options for ``getlasterror``,
and hence the options for write concern,
`see the MongoDB getlasterror docs <http://www.mongodb.org/display/DOCS/getLastError+Command>`_.

The MongoDB C driver supports write concern on two levels. You can set the write
concern on a ``mongo`` connection object, in which case that write concern level will
be used for every write. You can also specify a write concern for any individual
write operation (``mongo_insert()``, ``mongo_insert_batch()``, ``mongo_update()``,
or ``mongo_remove``). This will override any default write concern set on the
connection level.

Here are specific parameters, values, and meanings:

    w = -1           : errors ignored
    w = 0            : unacknowledged
    w = 1            : acknowledged
    w = 2            : replica acknowledged
    j = 1 (true)     : journaled
    fsync = 1 (true) : fsynced

Example
-------

.. code-block:: c

   #include <mongo.h>
   #include <stdio.h>

   #define ASSERT(x) \
    do{ \
        if(!(x)){ \
            printf("\nFailed ASSERT [%s] (%d):\n     %s\n\n", __FILE__,  __LINE__,  #x); \
            exit(1); \
        }\
    }while(0)

   int main() {
       mongo conn[1];
       mongo_write_concern write_concern[1];
       bson b[1];

       if( mongo_client( conn, "127.0.0.1", 27017 ) == MONGO_ERROR ) {
           printf( "Failed to connect!\n" );
           exit(1);
       }

       mongo_cmd_drop_collection( conn, "test", "foo", NULL );

       /* Initialize the write concern object.*/
       mongo_write_concern_init( write_concern );
       write_concern->w = 1;
       mongo_write_concern_finish( write_concern );

       bson_init( b );
       bson_append_new_oid( b );
       bson_finish( b );

       ASSERT( mongo_insert( conn, "test.foo", b, write_concern ) == MONGO_OK );

       /* If we try to insert the same document again,
          we'll get an error due to the unique index on _id.*/
       ASSERT( mongo_insert( conn, "test.foo", b, write_concern ) == MONGO_ERROR );
       ASSERT( conn->err == MONGO_WRITE_ERROR );
       printf( "Error message: %s\n", conn->lasterrstr );

       /* Clear any stored errors.*/
       mongo_clear_errors( conn );

       /* We'll get the same error if we set a default write concern
          on the connection object but don't set it on insert.*/
       mongo_set_write_concern( conn, write_concern );
       ASSERT( mongo_insert( conn, "test.foo", b, 0 ) == MONGO_ERROR );
       ASSERT( conn->err == MONGO_WRITE_ERROR );
       printf( "Error message: %s\n", conn->lasterrstr );

       mongo_write_concern_destroy( write_concern );
       bson_destroy( b );
       mongo_destroy( conn );

       return 0;
  }

Notes
-----

As you'll see in the code sample, the process for creating a write concern object
is to initialize it, manually set any write concern values (e.g., ``w``, ``wtimeout``
for values of ``w`` greater than 1, ``j``, etc.), and then call ``mongo_write_concern_finish()``
on it. This will effectively create the equivalent ``getlasterror`` command. Note you must call
``mongo_write_concern_destroy()`` when you're finished with the write concern object.

And for a longer example, see the
`C driver's write concern tests <https://github.com/mongodb/mongo-c-driver/blob/master/test/write_concern_test.c>`_.
