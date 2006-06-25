/**
 * ECHO module
 *
 * Copyright (C) 2001,2002 Jeffrey Fulmer <jdfulmer@armstrong.com>
 * This file is part of LIBPING
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
 *
 */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif/*HAVE_CONFIG_H*/ 

#include <sock.h>
#include <echo.h>
#include <util.h>
#include <stdlib.h>
#include <stdio.h>  
#include <errno.h>
#include <setup.h>
#include <string.h>

#include "memory.h"

#define MAXLINE  81921

int 
send_data( CONN *C, int bytes )
{
  char buf[MAXLINE];
  char rec[MAXLINE];
  int  ret;
  int  x, n;

  bytes = (bytes>MAXLINE-3)?MAXLINE-3:bytes;

  memset( buf, 0, bytes+3 );
  memset( rec, 0, bytes+3 );

  for( x = 0; x < bytes; x++ )
    buf[x] = '#';

  (void)strcat( buf, "\015\012" );

  if( JOEsocket_write( C, buf, sizeof( buf )) < 0 ){
    return -1; 
  }

  if(( n = JOEreadline( C, rec, sizeof( rec ))) < 0 ){
    return -1;
  }

  if(( ret = strlen( rec )) > 0 ){ 
    return 1;
  }
  else
    return -1;
}

int
myecho( ECHODATA *E )
{
  CONN *C;               /* threadsafe connection */
  int ret;               /* return conditional    */
  struct timeval mytime;
 
  C = (CONN*)xmalloc( sizeof( CONN )); 
  C->port    = 7; 
  C->timeout = ( E->timeout == 0 )?60:E->timeout;  

  (void) gettimeofday( &mytime, (struct timezone *)NULL);  

  if(( C->sock = JOEsocket( C, E->hostname )) < 0 ){
    return -1;
  }

  ret = send_data( C, E->bytes );
  
  E->rtt = elapsed_time( &mytime );
  return ret;
}

int
echohost( const char *hostname, int b )
{
  ECHODATA *E;
 
  E = (ECHODATA*)xmalloc( sizeof( ECHODATA ));
  E->hostname = (char*)strdup( hostname );
  E->bytes   = b;
  E->timeout = 0;  
  return ( myecho( E ));
}
 
int
echothost( const char *hostname, int b, int t )
{
  ECHODATA *E;
 
  E = (ECHODATA*)xmalloc( sizeof( ECHODATA ));
  E->hostname = (char*)strdup( hostname );
  E->bytes   = b;
  E->timeout = t; 
 
  return ( myecho( E ));
}

int
techohost( const char *hostname, int b )
{
  ECHODATA *E;
  int ret;
 
  E = (ECHODATA*)xmalloc( sizeof( ECHODATA ));
  E->hostname = (char*)strdup( hostname );
  E->bytes   = b;
  E->timeout = 0; 
 
  ret = myecho( E );
 
  if( ret > 0 ){ return E->rtt; }
  else         { return ret; }
}
 
int
techothost( const char *hostname, int b, int t )
{
  ECHODATA *E; 
  int ret;
  
  E = (ECHODATA*)xmalloc( sizeof( ECHODATA )); 
  E->hostname = (char*)strdup( hostname );
  E->bytes   = b;
  E->timeout = t; 

  ret = myecho( E );
  if( ret > 0 ){ return E->rtt; }
  else         { return ret; }
 
} 

