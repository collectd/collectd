/**
 * SMTP module
 *
 * Copyright (C) 2001 Jeffrey Fulmer <jdfulmer@armstrong.com>
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

#include <setup.h>
#include <sock.h>
#include <smtp.h>
#include <util.h> 
#include <stdarg.h>
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

#include "memory.h"
 
#define  MSGBUF 2048 

int smtp_cmd( CONN *C, char *format, ... ); 

int
mysmtp( SMTPDATA *S )
{
  CONN *C;
  char buf[MSGBUF];
  int ret  = 0;
  struct timeval mytime;

  C = (CONN*)xmalloc( sizeof( CONN ));
  C->port    =   25;
  C->timeout = ( S->timeout == 0 )?60:S->timeout; 
 
  /* set the rtt timer */
  (void) gettimeofday( &mytime, (struct timezone *)NULL); 

  if(( C->sock=JOEsocket( C, S->hostname ))    < 0 ){ JOEclose( C ); return -1;  } 
  if(( ret = JOEreadline( C, buf, MSGBUF ))    < 0 ){ JOEclose( C ); return ret; }
  if(( ret = smtp_cmd( C, "%s", "HELO dude" )) < 0 ){ JOEclose( C ); return ret; }
  if(( ret = smtp_cmd( C, "%s", "QUIT"   ))    < 0 ){ JOEclose( C ); return ret; }
  JOEclose( C );

  S->rtt = elapsed_time( &mytime );
  
  return 1;
}

int
smtp_cmd( CONN *C, char *format, ... )
{
  va_list args;
  char buf[MSGBUF];
  char rec[MSGBUF];
  int  ret;
  
  bzero( buf, sizeof( buf ));
  bzero( rec, sizeof( rec));  

  va_start( args, format );
  if(( vsnprintf( buf, MSGBUF-3, format, args )) < 0 ){
    perror( "message too large" );
    exit( 1 );
  }
  
  (void)strcat( buf, "\015\012" );
  
  if( JOEsocket_write( C, buf, sizeof( buf )) < 0 )
    return -1;

  if(( ret = JOEreadline( C, rec, MSGBUF )) <= 0 ){ 
    return -1; 
  }
  *rec='\0';

  va_end( args );
  if(( ret = atoi( rec )) > 400 ){ 
    return -1; 
  } else return 1;
}

int 
pingsmtp( const char *hostname )
{
  SMTPDATA *S;
 
  S = (SMTPDATA*)xmalloc( sizeof( SMTPDATA ));
  S->hostname = (char*)strdup( hostname );
  S->timeout = 0;
 
  return mysmtp( S );
}

int 
pingtsmtp( const char *hostname, int t )
{
  SMTPDATA *S;
 
  S = (SMTPDATA*)xmalloc( sizeof( SMTPDATA ));
  S->hostname = (char*)strdup( hostname );
  S->timeout = t;
 
  return mysmtp( S );
}

int
tpingsmtp( const char *hostname )
{
  SMTPDATA *S;
  int ret;
 
  S = (SMTPDATA*)xmalloc( sizeof( SMTPDATA ));
  S->hostname = (char*)strdup( hostname );
  S->timeout = 0;
 
  ret = mysmtp( S ); 
 
  if( ret > 0 ){ return S->rtt; }
  else         { return ret; }
}
 
int
tpingtsmtp( const char *hostname, int t )
{
  SMTPDATA *S;
  int ret;
 
  S = (SMTPDATA*)xmalloc( sizeof( SMTPDATA ));
  S->hostname = (char*)strdup( hostname );
  S->timeout = t;
 
  ret = mysmtp( S ); 
 
  if( ret > 0 ){ return S->rtt; }
  else         { return ret; }
 
} 
