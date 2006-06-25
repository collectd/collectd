/**
 * UTILITY module
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
#endif /* HAVE_CONFIG_H */

#include <util.h>

#ifdef STDC_HEADERS
# include <string.h>
#else
# ifndef HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif /* HAVE_STRCHR */
char *strchr (), *strrchr ();
# ifndef HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
#  define memmove(d, s, n) bcopy ((s), (d), (n))
# endif /* HAVE_MEMCPY  */
#endif  /* STDC_HEADERS */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include "memory.h"

/**
 * elapsed_time
 * returns an int value for the difference
 * between now and starttime in milliseconds.
 */
int
elapsed_time( struct timeval *starttime ){
  struct timeval *newtime;
  int elapsed;
  newtime = (struct timeval*)malloc( sizeof(struct timeval));
  gettimeofday(newtime,NULL);
  elapsed = 0;
  
  if(( newtime->tv_usec - starttime->tv_usec) > 0 ){
    elapsed += (newtime->tv_usec - starttime->tv_usec)/1000 ;
  }
  else{
    elapsed += ( 1000000 + newtime->tv_usec - starttime->tv_usec ) /1000;
    newtime->tv_sec--;
  }
  if(( newtime->tv_sec - starttime->tv_sec ) > 0 ){
    elapsed += 1000 * ( newtime->tv_sec - starttime->tv_sec );
  }
  if( elapsed < 1 )
    elapsed = 1;
  
  free( newtime );
  return( elapsed );
} 

/**
 * substring        
 * returns a char pointer from int start 
 * to int length.
 */
char * 
substring (char *buffer, int start, int length)
{
  char *sub;
  sub = xmalloc (sizeof (char) * (length + 1));
	
  if ((length < 1) || (start < 0) || (start > strlen (buffer)))
    return NULL;

  if (strlen (buffer) < length){
    sub = (char*) strdup (buffer);
    return buffer;
  }

  if (sub == NULL){
    perror( "insufficient memory." );
    exit( 1 );
  }

  memset (sub, 0, length + 1);

  buffer += start;
  memcpy (sub, buffer, length);

  return sub;
}

