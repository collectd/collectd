/**
 * PING header
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
#ifndef PING_H
#define PING_H

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif/*HAVE_CONFIG_H*/

#include <stdio.h>

#ifdef  HAVE_UNISTD_H
# include <unistd.h>
#endif/*HAVE_UNISTD_H*/

#ifdef  HAVE_STDLIB_H
# include <stdlib.h>
#endif/*HAVE_STDLIB_H*/

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

#include <string.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>

#ifdef  HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif/*HAVE_SYS_SOCKET_H*/

#ifdef  HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif/*HAVE_ARPA_INET_H*/

#ifdef  HAVE_NETDB_H
# include <netdb.h>
#endif/*HAVE_NETDB_H*/

#if defined( __linux__ )
# include "linux.h"
#else
# include <netinet/ip.h>
# include <netinet/ip_icmp.h>
#endif /* defined(__linux__) */   

int pinghost  ( const char *hostname );
int pingthost ( const char *hostname, int t );
int tpinghost ( const char *hostname );
int tpingthost( const char *hostname, int t );

#endif/*PING_H*/
