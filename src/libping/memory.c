/**
 * MEMORY module
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
#include <stdio.h>
#include <memory.h>
#include <strings.h>

/**
 * xrealloc    replaces realloc
 */
void * 
xrealloc( void *object, size_t size )
{
  void *new;
  if( object )
    new = realloc( object, size );
  else
    new = malloc( size );
  if( !new ){
    perror( "Memory exhausted" );
    exit( 1 );
  }
  return new;
} /** end xrealloc **/

/**
 * xmalloc     replaces malloc
 */
void *
xmalloc( size_t size )
{
  void *new  =  malloc( size );
  if( !new ){
    perror( "Memory exhausted" );
    exit( 1 );
  }
  
  return new;
} /** end of xmalloc **/

/**
 * xcalloc     replaces calloc
 */
void *
xcalloc( size_t num, size_t size )
{
  void *new  =  xmalloc( num * size );
  bzero( new, num * size );

  return new;
} /** end of xcalloc **/



