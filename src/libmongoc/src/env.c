#if !defined(MONGO_ENV_STANDARD) && (defined(_WIN32) || defined(_WIN64))

/* env_win32.c */

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

/* Networking and other niceties for WIN32. */
#include "env.h"
#include <string.h>

#ifdef _MSC_VER
  #include <ws2tcpip.h>  /* send,recv,socklen_t etc */
  #include <wspiapi.h>   /* addrinfo */
#else
  #include <ws2tcpip.h>  /* send,recv,socklen_t etc */
  #include <winsock2.h>
  typedef int socklen_t;
#endif

#ifndef NI_MAXSERV
# define NI_MAXSERV 32
#endif

int mongo_env_close_socket( SOCKET socket ) {
    return closesocket( socket );
}

int mongo_env_write_socket( mongo *conn, const void *buf, size_t len ) {
    const char *cbuf = (const char*)buf;
    int flags = 0;

    while ( len ) {
        size_t sent = send( conn->sock, cbuf, len, flags );
        if ( sent == -1 ) {
            __mongo_set_error( conn, MONGO_IO_ERROR, NULL, WSAGetLastError() );
            conn->connected = 0;
            return MONGO_ERROR;
        }
        cbuf += sent;
        len -= sent;
    }

    return MONGO_OK;
}

int mongo_env_read_socket( mongo *conn, void *buf, size_t len ) {
    char *cbuf = (char*)buf;

    while ( len ) {
        size_t sent = recv( conn->sock, cbuf, len, 0 );
        if ( sent == 0 || sent == -1 ) {
            __mongo_set_error( conn, MONGO_IO_ERROR, NULL, WSAGetLastError() );
            return MONGO_ERROR;
        }
        cbuf += sent;
        len -= sent;
    }

    return MONGO_OK;
}

int mongo_env_set_socket_op_timeout( mongo *conn, int millis ) {
    if ( setsockopt( conn->sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&millis,
                     sizeof( millis ) ) == -1 ) {
        __mongo_set_error( conn, MONGO_IO_ERROR, "setsockopt SO_RCVTIMEO failed.",
                           WSAGetLastError() );
        return MONGO_ERROR;
    }

    if ( setsockopt( conn->sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&millis,
                     sizeof( millis ) ) == -1 ) {
        __mongo_set_error( conn, MONGO_IO_ERROR, "setsockopt SO_SNDTIMEO failed.",
                           WSAGetLastError() );
        return MONGO_ERROR;
    }

    return MONGO_OK;
}

int mongo_env_socket_connect( mongo *conn, const char *host, int port ) {
    char port_str[NI_MAXSERV];
    char errstr[MONGO_ERR_LEN];
    int status;

    struct addrinfo ai_hints;
    struct addrinfo *ai_list = NULL;
    struct addrinfo *ai_ptr = NULL;

    conn->sock = 0;
    conn->connected = 0;

    bson_sprintf( port_str, "%d", port );

    memset( &ai_hints, 0, sizeof( ai_hints ) );
    ai_hints.ai_family = AF_UNSPEC;
    ai_hints.ai_socktype = SOCK_STREAM;
    ai_hints.ai_protocol = IPPROTO_TCP;

    status = getaddrinfo( host, port_str, &ai_hints, &ai_list );
    if ( status != 0 ) {
        bson_sprintf( errstr, "getaddrinfo failed with error %d", status );
        __mongo_set_error( conn, MONGO_CONN_ADDR_FAIL, errstr, WSAGetLastError() );
        return MONGO_ERROR;
    }

    for ( ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next ) {
        conn->sock = socket( ai_ptr->ai_family, ai_ptr->ai_socktype,
                             ai_ptr->ai_protocol );

        if ( conn->sock == INVALID_SOCKET ) {
            __mongo_set_error( conn, MONGO_SOCKET_ERROR, "socket() failed",
                               WSAGetLastError() );
            conn->sock = 0;
            continue;
        }

        status = connect( conn->sock, ai_ptr->ai_addr, ai_ptr->ai_addrlen );
        if ( status != 0 ) {
            __mongo_set_error( conn, MONGO_SOCKET_ERROR, "connect() failed",
                               WSAGetLastError() );
            mongo_env_close_socket( conn->sock );
            conn->sock = 0;
            continue;
        }

        if ( ai_ptr->ai_protocol == IPPROTO_TCP ) {
            int flag = 1;

            setsockopt( conn->sock, IPPROTO_TCP, TCP_NODELAY,
                        ( const char * ) &flag, sizeof( flag ) );

            if ( conn->op_timeout_ms > 0 )
                mongo_env_set_socket_op_timeout( conn, conn->op_timeout_ms );
        }

        conn->connected = 1;
        break;
    }

    freeaddrinfo( ai_list );

    if ( ! conn->connected ) {
        conn->err = MONGO_CONN_FAIL;
        return MONGO_ERROR;
    }
    else {
        mongo_clear_errors( conn );
        return MONGO_OK;
    }
}

MONGO_EXPORT int mongo_env_sock_init( void ) {

    WSADATA wsaData;
    WORD wVers;
    static int called_once;
    static int retval;

    if (called_once) return retval;

    called_once = 1;
    wVers = MAKEWORD(1, 1);
    retval = (WSAStartup(wVers, &wsaData) == 0);

    return retval;
}


#elif !defined(MONGO_ENV_STANDARD) && (defined(__APPLE__) || defined(__linux) || defined(__unix) || defined(__posix))

/* env_posix.c */

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

/* Networking and other niceties for POSIX systems. */
#include "env.h"
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef NI_MAXSERV
# define NI_MAXSERV 32
#endif

int mongo_env_close_socket( SOCKET socket ) {
    return close( socket );
}

int mongo_env_sock_init( void ) {
    return 0;
}

int mongo_env_write_socket( mongo *conn, const void *buf, size_t len ) {
    const char *cbuf = buf;
#ifdef __APPLE__
    int flags = 0;
#else
    int flags = MSG_NOSIGNAL;
#endif

    while ( len ) {
        size_t sent = send( conn->sock, cbuf, len, flags );
        if ( sent == -1 ) {
            if (errno == EPIPE)
                conn->connected = 0;
            __mongo_set_error( conn, MONGO_IO_ERROR, strerror( errno ), errno );
            return MONGO_ERROR;
        }
        cbuf += sent;
        len -= sent;
    }

    return MONGO_OK;
}

int mongo_env_read_socket( mongo *conn, void *buf, size_t len ) {
    char *cbuf = buf;
    while ( len ) {
        size_t sent = recv( conn->sock, cbuf, len, 0 );
        if ( sent == 0 || sent == -1 ) {
            __mongo_set_error( conn, MONGO_IO_ERROR, strerror( errno ), errno );
            return MONGO_ERROR;
        }
        cbuf += sent;
        len -= sent;
    }

    return MONGO_OK;
}

int mongo_env_set_socket_op_timeout( mongo *conn, int millis ) {
    struct timeval tv;
    tv.tv_sec = millis / 1000;
    tv.tv_usec = ( millis % 1000 ) * 1000;

    if ( setsockopt( conn->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof( tv ) ) == -1 ) {
        conn->err = MONGO_IO_ERROR;
        __mongo_set_error( conn, MONGO_IO_ERROR, "setsockopt SO_RCVTIMEO failed.", errno );
        return MONGO_ERROR;
    }

    if ( setsockopt( conn->sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof( tv ) ) == -1 ) {
        __mongo_set_error( conn, MONGO_IO_ERROR, "setsockopt SO_SNDTIMEO failed.", errno );
        return MONGO_ERROR;
    }

    return MONGO_OK;
}

static int mongo_env_unix_socket_connect( mongo *conn, const char *sock_path ) {
    struct sockaddr_un addr;
    int status, len;

    conn->connected = 0;

    conn->sock = socket( AF_UNIX, SOCK_STREAM, 0 );

    if ( conn->sock == INVALID_SOCKET ) {
        return MONGO_ERROR;
    }
    
    addr.sun_family = AF_UNIX;
    strncpy( addr.sun_path, sock_path, sizeof(addr.sun_path) - 1 );
    len = sizeof( addr );

    status = connect( conn->sock, (struct sockaddr *) &addr, len );
    if( status < 0 ) {
        mongo_env_close_socket( conn->sock );
        conn->sock = 0;
        conn->err = MONGO_CONN_FAIL;
        return MONGO_ERROR;
    }

    conn->connected = 1;

    return MONGO_OK;
}

int mongo_env_socket_connect( mongo *conn, const char *host, int port ) {
    char port_str[NI_MAXSERV];
    int status;

    struct addrinfo ai_hints;
    struct addrinfo *ai_list = NULL;
    struct addrinfo *ai_ptr = NULL;

    if ( port < 0 ) {
        return mongo_env_unix_socket_connect( conn, host );
    }

    conn->sock = 0;
    conn->connected = 0;
    sprintf(port_str,"%d",port);

    bson_sprintf( port_str, "%d", port );

    memset( &ai_hints, 0, sizeof( ai_hints ) );
#ifdef AI_ADDRCONFIG
    ai_hints.ai_flags = AI_ADDRCONFIG;
#endif
    ai_hints.ai_family = AF_UNSPEC;
    ai_hints.ai_socktype = SOCK_STREAM;

    status = getaddrinfo( host, port_str, &ai_hints, &ai_list );
    if ( status != 0 ) {
        bson_errprintf( "getaddrinfo failed: %s", gai_strerror( status ) );
        conn->err = MONGO_CONN_ADDR_FAIL;
        return MONGO_ERROR;
    }

    for ( ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next ) {
        conn->sock = socket( ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol );
        if ( conn->sock == INVALID_SOCKET ) {
            continue;
        }
        
        status = connect( conn->sock, ai_ptr->ai_addr, ai_ptr->ai_addrlen );
        if ( status != 0 ) {
            mongo_env_close_socket( conn->sock );
            conn->sock = 0;
            continue;
        }
#if __APPLE__
        {
            int flag = 1;
            setsockopt( conn->sock, SOL_SOCKET, SO_NOSIGPIPE,
                       ( void * ) &flag, sizeof( flag ) );
        }
#endif

        if ( ai_ptr->ai_protocol == IPPROTO_TCP ) {
            int flag = 1;

            setsockopt( conn->sock, IPPROTO_TCP, TCP_NODELAY,
                        ( void * ) &flag, sizeof( flag ) );
            if ( conn->op_timeout_ms > 0 )
                mongo_env_set_socket_op_timeout( conn, conn->op_timeout_ms );
        }

        conn->connected = 1;
        break;
    }

    freeaddrinfo( ai_list );

    if ( ! conn->connected ) {
        conn->err = MONGO_CONN_FAIL;
        return MONGO_ERROR;
    }

    return MONGO_OK;
}

#else
/* env_standard.c */

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

/* Vanilla networking designed to work on all systems. */
#include "env.h"
#include <errno.h>
#include <string.h>

#ifdef _WIN32
#ifdef _MSC_VER
#include <ws2tcpip.h>  /* send,recv,socklen_t etc */
#include <wspiapi.h>   /* addrinfo */
#else
#include <windows.h>
#include <winsock.h>
typedef int socklen_t;
#endif
#else
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#ifndef NI_MAXSERV
# define NI_MAXSERV 32
#endif

int mongo_env_close_socket( SOCKET socket ) {
#ifdef _WIN32
    return closesocket( socket );
#else
    return close( socket );
#endif
}

int mongo_env_write_socket( mongo *conn, const void *buf, size_t len ) {
    const char *cbuf = buf;
#ifdef _WIN32
    int flags = 0;
#else
#ifdef __APPLE__
    int flags = 0;
#else
    int flags = MSG_NOSIGNAL;
#endif
#endif

    while ( len ) {
        size_t sent = send( conn->sock, cbuf, len, flags );
        if ( sent == -1 ) {
            if (errno == EPIPE)
                conn->connected = 0;
            conn->err = MONGO_IO_ERROR;
            return MONGO_ERROR;
        }
        cbuf += sent;
        len -= sent;
    }

    return MONGO_OK;
}

int mongo_env_read_socket( mongo *conn, void *buf, size_t len ) {
    char *cbuf = buf;
    while ( len ) {
        size_t sent = recv( conn->sock, cbuf, len, 0 );
        if ( sent == 0 || sent == -1 ) {
            conn->err = MONGO_IO_ERROR;
            return MONGO_ERROR;
        }
        cbuf += sent;
        len -= sent;
    }

    return MONGO_OK;
}

/* This is a no-op in the generic implementation. */
int mongo_env_set_socket_op_timeout( mongo *conn, int millis ) {
    return MONGO_OK;
}

int mongo_env_socket_connect( mongo *conn, const char *host, int port ) {
    struct sockaddr_in sa;
    socklen_t addressSize;
    int flag = 1;

    if ( ( conn->sock = socket( AF_INET, SOCK_STREAM, 0 ) ) == INVALID_SOCKET ) {
        conn->sock = 0;
        conn->err = MONGO_CONN_NO_SOCKET;
        return MONGO_ERROR;
    }

    memset( sa.sin_zero , 0 , sizeof( sa.sin_zero ) );
    sa.sin_family = AF_INET;
    sa.sin_port = htons( port );
    sa.sin_addr.s_addr = inet_addr( host );
    addressSize = sizeof( sa );

    if ( connect( conn->sock, ( struct sockaddr * )&sa, addressSize ) == INVALID_SOCKET ) {
        mongo_env_close_socket( conn->sock );
        conn->connected = 0;
        conn->sock = 0;
        conn->err = MONGO_CONN_FAIL;
        return MONGO_ERROR;
    }

    setsockopt( conn->sock, IPPROTO_TCP, TCP_NODELAY, ( char * ) &flag, sizeof( flag ) );

    if( conn->op_timeout_ms > 0 )
        mongo_env_set_socket_op_timeout( conn, conn->op_timeout_ms );

    conn->connected = 1;

    return MONGO_OK;
}

MONGO_EXPORT int mongo_env_sock_init( void ) {

#if defined(_WIN32)
    WSADATA wsaData;
    WORD wVers;
#elif defined(SIGPIPE)
    struct sigaction act;
#endif

    static int called_once;
    static int retval;
    if (called_once) return retval;
    called_once = 1;

#if defined(_WIN32)
    wVers = MAKEWORD(1, 1);
    retval = (WSAStartup(wVers, &wsaData) == 0);
#elif defined(MACINTOSH)
    GUSISetup(GUSIwithInternetSockets);
    retval = 1;
#elif defined(SIGPIPE)
    retval = 1;
    if (sigaction(SIGPIPE, (struct sigaction *)NULL, &act) < 0)
        retval = 0;
    else if (act.sa_handler == SIG_DFL) {
        act.sa_handler = SIG_IGN;
        if (sigaction(SIGPIPE, &act, (struct sigaction *)NULL) < 0)
            retval = 0;
    }
#endif
    return retval;
}

#endif
