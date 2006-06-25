/**
 * POP3 module
 *
 * Copyright (C) 2001, 2002 by
 * Jeffrey Fulmer <jdfulmer@armstrong.com>
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
#include <pop3.h>
#include <setup.h>

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif/*HAVE_CONFIG_H*/

#include <sock.h>
#include <util.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
 
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
#endif /*TIME_WITH_SYS_TIME*/ 

#include <signal.h>
#include <setjmp.h> 

#include "memory.h"

#define MSGBUF  1024

int send_cmd( CONN *C, char *cmd, char *val );

int
mypop3( POP3DATA *P )
{
  CONN *C;
  char buf[MSGBUF];
  int ret  = 0;
  struct timeval mytime;

  C = (CONN*)xmalloc( sizeof( CONN ));
  C->port    =   110;
  C->timeout = ( P->timeout == 0 )?60:P->timeout; 
  
  if(( C->sock = JOEsocket( C, P->hostname )) <= 0 ){
    return -1;
  }   

  /* set the rrt timer */
  (void) gettimeofday( &mytime, (struct timezone *)NULL); 

  if(( ret = JOEreadline( C, buf, MSGBUF )) < 0 ){
    return ret;
  }
  if( !strncmp( buf, "+OK", 3 )) ret = 1; 
    
  if(( ret = send_cmd( C, "QUIT", NULL )) < 0 ){
    return ret; 
  }

  JOEclose( C );
  P->rtt = elapsed_time( &mytime ); 

  return ret;
}

int
send_cmd( CONN *C, char *cmd, char *val )
{
  char buf[256];
  char rec[MSGBUF];

  if( val )
    snprintf( buf, sizeof( buf ), "%s %s\r\n", cmd, val );
  else
    snprintf( buf, sizeof( buf ), "%s\r\n", cmd );

  if( JOEsocket_write( C, buf, sizeof( buf ))  < 0 )
    return -1;

  JOEreadline( C, rec, MSGBUF );
  *rec='\0';

  if( !strncmp( rec, "-ERR", 4 )){
    return -1;
  }

  return 1; 
}

int 
pingpop3( const char *hostname )
{
  POP3DATA *P;
 
  P = (POP3DATA*)xmalloc( sizeof( POP3DATA ));
  P->hostname = (char*)strdup( hostname );
  P->timeout = 0;
 
  return mypop3( P );
}

int
pingtpop3( const char *hostname, int t )
{
  POP3DATA *P;
  int ret;
 
  P = (POP3DATA*)xmalloc( sizeof( POP3DATA ));
  P->hostname = (char*)strdup( hostname );
  P->timeout = t;
 
  ret = mypop3( P ); 

  return mypop3( P );
}

int
tpingpop3( const char *hostname )
{
  POP3DATA *P;
  int ret;
 
  P = (POP3DATA*)xmalloc( sizeof( POP3DATA ));
  P->hostname = (char*)strdup( hostname );
  P->timeout = 0;
 
  ret = mypop3( P ); 

  if( ret > 0 ){ return P->rtt; }
  else         { return ret; }
}
 
int
tpingtpop3( const char *hostname, int t )
{
  POP3DATA *P;
  int ret;
 
  P = (POP3DATA*)xmalloc( sizeof( POP3DATA ));
  P->hostname = (char*)strdup( hostname );
  P->timeout = t; 
  
  ret = mypop3( P );
 
  if( ret > 0 ){ return P->rtt; }
  else         { return ret; }
 
} 
