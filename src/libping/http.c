/**
 * HTTP/HTTPS protocol support 
 *
 * Copyright (C) 2000, 2001, 2002 by
 * Jeffrey Fulmer - <jdfulmer@armstrong.com>
 * This file is distributed as part of Libping
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif/*HAVE_CONFIG_H*/
 
#include <sock.h>
#include <http.h>
#include <util.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <setup.h>

#include "memory.h"

#define MAXFILE 10240

int
myhttp( HTTPDATA *H )
{
  CONN    *C;    /* threadsafe connection */
  URL     U;     /* defined in url.h      */
  HEADERS *head; /* HTTP header structure */ 
  int     bytes; /* bytes read from srv.  */
  char    *tmp;  /* tmp storage for url   */
  struct  timeval mytime; 

  tmp = strdup( H->url ); 
  U = add_url( tmp );
 
  C = (CONN*)xmalloc( sizeof( CONN ));
  C->port    = U.port;
  C->timeout = ( H->timeout == 0 )?60:H->timeout;

  (void) gettimeofday( &mytime, (struct timezone *)NULL);
 
  if(( C->sock = JOEsocket( C, U.hostname )) <= 0 ){
    return -1;
  }

  JOEhttp_send( C, U.hostname, U.pathname );
  head  = JOEhttp_read_headers( C, U.hostname );
  if( !head ){ 
    JOEclose( C ); 
    free( C ); 
    return -1; 
  }
  
  bytes =  JOEhttp_read( C, 0 );
  if( bytes < 1 ){ 
    JOEclose( C ); 
    free( C ); 
    free( head ); 
    return -1; 
  }
  
  JOEclose( C );

  H->rtt = elapsed_time( &mytime ); 
 
  if( head->code > 499 ){ return -1; }
  else                  { return  1; } 
}

/**
 * returns int, ( < 0 == error )
 * formats and sends an HTTP/1.0 request
 */
void
JOEhttp_send( CONN *C, char *host, char *path )
{
  int rlen;
  char *protocol; 
  char *keepalive;
  char request[1024]; 
  char fullpath[2048];

  sprintf( fullpath, "%s", path );

  memset( request, 0, sizeof( request ));

  /* HTTP protocol string */
  protocol  = "HTTP/1.0";
  keepalive = "close";
  
  rlen=snprintf(
    request, sizeof( request ),
    "GET %s %s\015\012"
    "Host: %s\015\012"
    "Accept: */*\015\012"
    "Accept-Encoding: * \015\012"
    "User-Agent: JoeDog 1.00 [libping]\015\012"
    "Connection: %s\015\012\015\012",
    fullpath, protocol, host, keepalive
  );
  
  if( rlen < 0 || rlen > sizeof(request)  ){ 
    perror("http_send: request buffer overrun!"); 
    exit( 1 ); 
  }

  if(( JOEsocket_check( C, WRITE )) < 0 ){
    perror( "JOEsocket: not writeable" );
    return;
  } 

  JOEsocket_write( C, request, rlen );

  return;
}

/**
 * returns int, ( < 0 == error )
 * formats and sends an HTTP/1.0 request
 */
void
JOEhttp_post( CONN *C, char *host, char *path, char *data, size_t len )
{
  int  rlen;
  char request[1024]; 
  char *protocol; 
  char *keepalive;
  char fullpath[2048];
  
  sprintf( fullpath, "%s", path );

  memset( request, 0, sizeof( request ));

  /* HTTP protocol string */
  protocol  = "HTTP/1.0";
  keepalive = "close";
    
  rlen=snprintf(
    request, sizeof( request ),
    "POST %s %s\015\012"
    "Host: %s\015\012"
    "Accept: */*\015\012"
    "Accept-Encoding: * \015\012"
    "User-Agent: JoeDog 1.00 [libping]\015\012"
    "Connection: %s\015\012"
    "Content-type: application/x-www-form-urlencoded\015\012"
    "Content-length: %d\015\012\015\012"
    "%*.*s\015\012",
    fullpath, protocol, host, keepalive, len, len, len, data
  );

  if( rlen < 0 || rlen > sizeof(request) ){ 
    perror("http_post: request buffer overrun!"); 
    exit( 1 ); 
  }

  if(( JOEsocket_check( C, WRITE )) < 0 ){
    perror( "JOEsocket: not writeable" );
    return;
  } 

  JOEsocket_write( C, request, rlen );

  return;
}

/**
 * returns HEADERS struct
 * reads from http/https socket and parses
 * header information into the struct.
 */
HEADERS *
JOEhttp_read_headers( CONN *C, char *host )
{ 
  int  x;           /* while loop index      */
  int  n;           /* assign socket_read    */
  char c;           /* assign char read      */
  char line[512];   /* assign chars read     */
  HEADERS *h;       /* struct to hold it all */
  h = (HEADERS*)malloc( sizeof(HEADERS));
  memset( h, 0, sizeof( HEADERS ));
  
  if(( JOEsocket_check( C, READ )) < 0 ){
    perror( "JOEsocket: not readable" );
    return NULL;
  } 

  h->redirection[0]=0;

  while( TRUE ){
    x = 0;
    memset( &line, 0, sizeof( line ));
    while(( n = JOEsocket_read( C, &c, 1 )) == 1 ){
      line[x] = c; 
      if(( line[0] == '\n' ) || ( line[1] == '\n' )){ 
        return h;
      }
      if( line[x] == '\n' ) break;
      x ++;
    }
    line[x]=0;
    /* strip trailing CR */
    if (x > 0 && line[x-1] == '\r') line[x-1]=0;
    if( strncasecmp( line, "http", 4 ) == 0 ){
      strncpy( h->head, line, 8 );
      h->code = atoi( line + 9 ); 
    }
    if( strncasecmp( line, "content-length: ", 16 ) == 0 ){ 
      h->length = atol( line + 16 ); 
    }
    if( strncasecmp( line, "connection: ", 12 ) == 0 ){
      if ( strncasecmp( line+12, "keep-alive", 10 ) == 0 ){
        h->keepalive = 1;
      }
      else if( strncasecmp( line+12, "close", 5 ) == 0 ){
        h->keepalive = 0;
      }
    }

    if( strncasecmp(line, "location: ", 10) == 0) {
      if (strlen(line) - 10 > sizeof(h->redirection) - 1) {
	perror( "redirection URL too long, ignored");
      }
      else {
        strcpy(h->redirection, line+10);
      }  
    }

    if( n <  0 ){ 
      perror("JOEhttp_read_headers"); 
      return( NULL ); 
    } /* socket closed */
  } /* end of while TRUE */

  return h;
}

/**
 * returns int
 * reads a http/https socket
 * ( you know what I mean :)
 */
ssize_t
JOEhttp_read( CONN *C, int len )
{ 
  int  n;
  size_t bytes=0;
  char body[MAXFILE];

  if(( JOEsocket_check( C, READ )) < 0 ){
    perror( "JOEsocket: not readable" );
    return -1;
  } 

  memset( &body, 0, MAXFILE );  
  while( TRUE ){
    if(( n = JOEsocket_read( C, body, MAXFILE)) == 0 ){
      break;
    } 
    /* IGV: should be accumulating bytes read, not just 
       recording those in the last packet */
    bytes += n;
  }
  return( bytes );
}

int
pinghttp( char *hostname )
{
  HTTPDATA *H;
 
  H = (HTTPDATA*)xmalloc( sizeof( HTTPDATA ));
  H->url     = (char*)strdup( hostname );
  H->timeout = 0;
 
  return( myhttp( H )); 
}
 
int
pingthttp( char *hostname, int t )
{
  HTTPDATA *H;
 
  H = (HTTPDATA*)xmalloc( sizeof( HTTPDATA ));
  H->url     = (char*)strdup( hostname );
  H->timeout = t;
 
  return( myhttp( H )); 
}
 
int
tpinghttp( char *hostname )
{
  HTTPDATA *H;
  int ret;
 
  H = (HTTPDATA*)xmalloc( sizeof( HTTPDATA ));
  H->url     = (char*)strdup( hostname );
  H->timeout = 0;
 
  ret = myhttp( H ); 

  if( ret > 0 ){ return H->rtt; }
  else         { return ret; }
}
 
int
tpingthttp( char *hostname, int t )
{
  HTTPDATA *H;
  int ret;
 
  H = (HTTPDATA*)xmalloc( sizeof( HTTPDATA ));
  H->url     = (char*)strdup( hostname );
  H->timeout = t;
 
  ret = myhttp( H );

  if( ret > 0 ){ return H->rtt; }
  else         { return ret;    }
} 


