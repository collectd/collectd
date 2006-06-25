/**
 * JOE http header file
 *
 * Copyright (C) 2000, 2001, 2002 by
 * Jeffrey Fulmer - <jdfulmer@armstrong.com>
 * This file is distributed as part of Siege 
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
#ifndef HTTP_H
#define HTTP_H

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <sock.h>

#ifndef HAVE_SNPRINTF
# define portable_snprintf  snprintf
# define portable_vsnprintf vsnprintf
#endif

typedef struct
{
  char   *url;
  int    rtt;
  int    timeout;
} HTTPDATA;

typedef struct headers
{
  char                head[64];
  int                 code;
  unsigned long int   length;
  char                cookie[128];
  char                redirection[128];
  int                 keepalive;
} HEADERS;

int myhttp( HTTPDATA *H ); 

/**
 * construct a HTTP GET request with "my" 
 * data and send it to the server host.
 */
void JOEhttp_send( CONN *C, char *host, char *path );

/**
 * construct a HTTP POST request with "my"
 * data and send it to server host.
 */
void JOEhttp_post( CONN *C, char *host, char *path, char *data, size_t len );

/**
 * read http headers
 * int socket
 */
HEADERS * JOEhttp_read_headers( CONN *C, char * );

/**
 * read http content
 * int socket
 */
ssize_t JOEhttp_read( CONN *C, int len );

#endif /* HTTP_H */

