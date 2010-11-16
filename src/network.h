/**
 * collectd - src/network.h
 * Copyright (C) 2005-2008  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 **/

#ifndef NETWORK_H
#define NETWORK_H

/*
 * From RFC2365: Administratively Scoped IP Multicast
 *
 * The IPv4 Organization Local Scope -- 239.192.0.0/14
 *
 * 239.192.0.0/14 is defined to be the IPv4 Organization Local Scope, and is
 * the space from which an organization should allocate sub-ranges when
 * defining scopes for private use.
 *
 * Port 25826 is not assigned as of 2005-09-12
 */

/*
 * From RFC2373: IP Version 6 Addressing Architecture
 *
 * 2.7 Multicast Addresses
 *
 *  |   8    |  4 |  4 |          80 bits          |     32 bits     |
 *  +--------+----+----+---------------------------+-----------------+
 *  |11111111|flgs|scop|   reserved must be zero   |    group ID     |
 *  +--------+----+----+---------------------------+-----------------+
 *
 * flgs = 1 => non-permanently-assigned ("transient") multicast address.
 * scop = 8 => organization-local scope
 *
 * group = efc0:4a42 = 239.192.74.66
 */

#define NET_DEFAULT_V4_ADDR "239.192.74.66"
#define NET_DEFAULT_V6_ADDR "ff18::efc0:4a42"
#define NET_DEFAULT_PORT    "25826"

#define TYPE_HOST            0x0000
#define TYPE_TIME            0x0001
#define TYPE_TIME_HR         0x0008
#define TYPE_PLUGIN          0x0002
#define TYPE_PLUGIN_INSTANCE 0x0003
#define TYPE_TYPE            0x0004
#define TYPE_TYPE_INSTANCE   0x0005
#define TYPE_VALUES          0x0006
#define TYPE_INTERVAL        0x0007
#define TYPE_INTERVAL_HR     0x0009

/* Types to transmit notifications */
#define TYPE_MESSAGE         0x0100
#define TYPE_SEVERITY        0x0101

#define TYPE_SIGN_SHA256     0x0200
#define TYPE_ENCR_AES256     0x0210

#endif /* NETWORK_H */
