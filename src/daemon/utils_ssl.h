/**
 * collectd - src/utils_ssl.h
 * Copyright (C) 2015  Florian octo Forster
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
 * Author:
 *   Florian octo Forster <octo at verplant.org>
 **/

#ifndef UTILS_SSL_H
#define UTILS_SSL_H 1

#define SSL_MT_OK               0
#define SSL_MT_ENOMEM           1
#define SSL_MT_NO_SUPPORT       2

int cd_ssl_multithread_setup(void);
int cd_ssl_multithread_cleanup(void);
/* vim: set shiftwidth=2 softtabstop=2 tabstop=8 : */
#endif /* !UTILS_SSL_H */
