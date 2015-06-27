/**
 * collectd - src/utils_ssl.c
 * Copyright (C) 2015  Toni Moreno
 * Copyright (C) 1998 - 2011, Daniel Stenberg, <daniel@haxx.se>, et al.
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
 *   Toni Moreno <toni.moreno at gmail.com>
 */

 /* Code taken from The example code from Jeremy Brown:
 * http://curl.haxx.se/libcurl/c/opensslthreadlock.html
 **/

#include "collectd.h"

# include "utils_ssl.h"

#ifndef HAVE_OPENSSL
int cd_ssl_multithread_setup(void)
{
	WARNING("collectd has not been compiled with SSL multithread support");
	return SSL_MT_NO_SUPPORT;
}

int cd_ssl_multithread_cleanup(void)
{
	WARNING("collectd has not been compiled with SSL multithread support: Nothing to do");
	return SSL_MT_NO_SUPPORT;
}
#else /*DEFINED HAVE_OPENSSL*/


#include <stdio.h>
#include <pthread.h>
#include <openssl/err.h>
#include <openssl/crypto.h>


/* This array will store all of the mutexes available to OpenSSL. */
static pthread_mutex_t *mutex_buf= NULL;

static void locking_function(int mode, int n, const char * file, int line)
{
  if (mode & CRYPTO_LOCK)
    pthread_mutex_lock(&(mutex_buf[n]));
  else
    pthread_mutex_unlock(&(mutex_buf[n]));
}

static unsigned long id_function(void)
{
  return ((unsigned long)pthread_self());
}

int cd_ssl_multithread_setup(void)
{
  int i;
  /*check if already initialized */
  if(mutex_buf != NULL) return SSL_MT_OK;
  /*if not begin callback initialization*/
  mutex_buf = malloc(CRYPTO_num_locks(  ) * sizeof(pthread_mutex_t));
  if (!mutex_buf)
    return SSL_MT_ENOMEM;
  for (i = 0;  i < CRYPTO_num_locks(  );  i++)
    pthread_mutex_init(&(mutex_buf[i]), NULL);
  CRYPTO_set_id_callback(id_function);
  CRYPTO_set_locking_callback(locking_function);
  return SSL_MT_OK;
}

int cd_ssl_multithread_cleanup(void)
{
  int i;

  if (!mutex_buf)
    return SSL_MT_OK;
  CRYPTO_set_id_callback(NULL);
  CRYPTO_set_locking_callback(NULL);
  for (i = 0;  i < CRYPTO_num_locks(  );  i++)
    pthread_mutex_destroy(&(mutex_buf[i]));
  free(mutex_buf);
  mutex_buf = NULL;
  return SSL_MT_OK;
}
#endif
