/** @file env.h */

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

/* Header for generic net.h */
#ifndef MONGO_ENV_H_
#define MONGO_ENV_H_

#include "mongo.h"

MONGO_EXTERN_C_START

#if !defined(MONGO_ENV_STANDARD) && (defined(__APPLE__) || defined(__linux) || defined(__unix) || defined(__posix))
  #define INVALID_SOCKET (-1) 
#endif

/* This is a no-op in the generic implementation. */
int mongo_env_set_socket_op_timeout( mongo *conn, int millis );
int mongo_env_read_socket( mongo *conn, void *buf, size_t len );
int mongo_env_write_socket( mongo *conn, const void *buf, size_t len );
int mongo_env_socket_connect( mongo *conn, const char *host, int port );

/* Initialize socket services */
MONGO_EXPORT int mongo_env_sock_init( void );

/* Close a socket */
MONGO_EXPORT int mongo_env_close_socket( SOCKET socket );

MONGO_EXTERN_C_END
#endif
