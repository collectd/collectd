/**
 * LIBPING socket header file
 *
 * Copyright (C) 2000, 2001, 2002 by
 * Jeffrey Fulmer - <jdfulmer@armstrong.com>
 * This file is distributed as part of libping
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef SOCK_H
#define SOCK_H

#ifdef  HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif/*HAVE_ARPA_INET_H*/
 
#ifdef  HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif/*HAVE_SYS_SOCKET_H*/

#ifdef  HAVE_UNISTD_H
# include <unistd.h>
#endif/*HAVE_UNISTD_H*/ 
 
#ifdef  HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif/*HAVE_NETINET_IN_H*/

#ifdef HAVE_SYS_TIMES_H
# include <sys/times.h>
#endif /*HAVE_SYS_TIMES_H*/
#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif/*TIME_WITH_SYS_TIME*/ 
 
#ifdef  HAVE_NETDB_H
# include <netdb.h>
#endif/*HAVE_NETDB_H*/ 

#ifdef  HAVE_SSL
# include <openssl/ssl.h>
# include <openssl/err.h>
# include <openssl/rsa.h>
# include <openssl/crypto.h>
# include <openssl/x509.h>
# include <openssl/pem.h>
#endif/*HAVE_SSL*/

#include <url.h>

typedef enum
{
  S_CONNECTING = 1,
  S_READING    = 2,
  S_WRITING    = 4,
  S_DONE       = 8
} STATUS; 

typedef enum
{
  READ  = 0,
  WRITE = 1,
  RDWR  = 2
} SDSET;  

typedef struct
{
  int      sock;
  int      port;
  int      timeout;
  STATUS   status;
  PROTOCOL prot;
#ifdef  HAVE_SSL
  SSL      *ssl;
  SSL_CTX  *ctx;
#endif/*HAVE_SSL*/
  fd_set   rset;
  fd_set   wset;
} CONN; 

/**
 * create socket 
 * const char *hostname
 * const char *service
 */
int JOEsocket( CONN *conn, const char *hostname );

/**
 * checks the socket for both
 * readability, writeability or both.
 */
int JOEsocket_check( CONN *C, SDSET S );

/** 
 * write int sock 
 * from char *buf, 
 * unsigned int length 
 */
int JOEsocket_write( CONN *conn, const void *b, size_t n );

/** 
 * read int sock, 
 * into void *buf, 
 * size_t length 
 */
ssize_t JOEread_byte( CONN *conn, void *buf, size_t len ); 
ssize_t JOEreadline( CONN *C, char *ptr, size_t len ); 
ssize_t JOEsocket_read( CONN *conn, void *buf, size_t len ); 

/** 
 * close int socket 
 */
void JOEclose( CONN *C );

#endif /* SOCK_H */

