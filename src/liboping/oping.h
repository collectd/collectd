/**
 * Object oriented C module to send ICMP and ICMPv6 `echo's.
 * Copyright (C) 2006  Florian octo Forster <octo at verplant.org>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef OCTO_PING_H
#define OCTO_PING_H 1

#if HAVE_CONFIG_H
# include <config.h>
#endif

#if HAVE_STDLIB_H
# include <stdlib.h>
#endif
#if HAVE_UNISTD_H
# include <unistd.h>
#endif
#if HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif

/*
 * Type definitions
 */
struct pinghost;
typedef struct pinghost pinghost_t;

typedef pinghost_t pingobj_iter_t;

struct pingobj;
typedef struct pingobj pingobj_t;

#define PING_OPT_TIMEOUT 0x01
#define PING_OPT_TTL     0x02
#define PING_OPT_AF      0x04
#define PING_OPT_DATA    0x08

#define PING_DEF_TIMEOUT 1.0
#define PING_DEF_TTL     255
#define PING_DEF_AF      AF_UNSPEC
#define PING_DEF_DATA    "Florian Forster <octo@verplant.org> http://verplant.org/"

/*
 * Method definitions
 */
pingobj_t *ping_construct (void);
void ping_destroy (pingobj_t *obj);

int ping_setopt (pingobj_t *obj, int option, void *value);

int ping_send (pingobj_t *obj);

int ping_host_add (pingobj_t *obj, const char *host);
int ping_host_remove (pingobj_t *obj, const char *host);

pingobj_iter_t *ping_iterator_get (pingobj_t *obj);
pingobj_iter_t *ping_iterator_next (pingobj_iter_t *iter);

#define PING_INFO_HOSTNAME 1
#define PING_INFO_ADDRESS  2
#define PING_INFO_FAMILY   3
#define PING_INFO_LATENCY  4
#define PING_INFO_SEQUENCE 5
#define PING_INFO_IDENT    6
#define PING_INFO_DATA     7
int ping_iterator_get_info (pingobj_iter_t *iter, int info,
		void *buffer, size_t *buffer_len);

const char *ping_get_error (pingobj_t *obj);

void *ping_iterator_get_context (pingobj_iter_t *iter);
void  ping_iterator_set_context (pingobj_iter_t *iter, void *context);

#endif /* OCTO_PING_H */
