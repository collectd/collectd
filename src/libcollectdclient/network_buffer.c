/**
 * collectd - src/libcollectdclient/network_buffer.c
 * Copyright (C) 2010-2015  Florian octo Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <errno.h>
#include <arpa/inet.h> /* htons */

#include <pthread.h>

#if HAVE_LIBGCRYPT
# include <pthread.h>
# if defined __APPLE__
/* default xcode compiler throws warnings even when deprecated functionality
 * is not used. -Werror breaks the build because of erroneous warnings.
 * http://stackoverflow.com/questions/10556299/compiler-warnings-with-libgcrypt-v1-5-0/12830209#12830209
 */
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
# endif
/* FreeBSD's copy of libgcrypt extends the existing GCRYPT_NO_DEPRECATED
 * to properly hide all deprecated functionality.
 * http://svnweb.freebsd.org/ports/head/security/libgcrypt/files/patch-src__gcrypt.h.in
 */
# define GCRYPT_NO_DEPRECATED
# include <gcrypt.h>
# if defined __APPLE__
/* Re enable deprecation warnings */
#  pragma GCC diagnostic warning "-Wdeprecated-declarations"
# endif
# if GCRYPT_VERSION_NUMBER < 0x010600
GCRY_THREAD_OPTION_PTHREAD_IMPL;
# endif
#endif

#include "collectd/network_buffer.h"

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

#define PART_SIGNATURE_SHA256_SIZE 36
#define PART_ENCRYPTION_AES256_SIZE 42

#define ADD_GENERIC(nb,srcptr,size) do {         \
  assert ((size) <= (nb)->free);                 \
  memcpy ((nb)->ptr, (srcptr), (size));          \
  (nb)->ptr += (size);                           \
  (nb)->free -= (size);                          \
} while (0)

#define ADD_STATIC(nb,var) \
  ADD_GENERIC(nb,&(var),sizeof(var));

/*
 * Data types
 */
struct lcc_network_buffer_s
{
  char *buffer;
  size_t size;

  lcc_value_list_t state;
  char *ptr;
  size_t free;

  lcc_security_level_t seclevel;
  char *username;
  char *password;

#if HAVE_LIBGCRYPT
  gcry_cipher_hd_t encr_cypher;
  size_t encr_header_len;
  char encr_iv[16];
#endif
};

#define SSTRNCPY(dst,src,sz) do { \
  strncpy ((dst), (src), (sz));   \
  (dst)[(sz) - 1] = 0;            \
} while (0)

/*
 * Private functions
 */
static _Bool have_gcrypt (void) /* {{{ */
{
  static _Bool result = 0;
  static _Bool need_init = 1;

  if (!need_init)
    return (result);
  need_init = 0;

#if HAVE_LIBGCRYPT
# if GCRYPT_VERSION_NUMBER < 0x010600
  gcry_control (GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread);
# endif

  if (!gcry_check_version (GCRYPT_VERSION))
    return (0);

  gcry_control (GCRYCTL_INIT_SECMEM, 32768, 0);
  gcry_control (GCRYCTL_INITIALIZATION_FINISHED, 0);

  result = 1;
  return (1);
#else
  return(0);
#endif
} /* }}} _Bool have_gcrypt */

#ifndef HAVE_HTONLL
static uint64_t htonll (uint64_t val) /* {{{ */
{
  static int config = 0;

  uint32_t hi;
  uint32_t lo;

  if (config == 0)
  {
    uint16_t h = 0x1234;
    uint16_t n = htons (h);

    if (h == n)
      config = 1;
    else
      config = 2;
  }

  if (config == 1)
    return (val);

  hi = (uint32_t) (val >> 32);
  lo = (uint32_t) (val & 0x00000000FFFFFFFF);

  hi = htonl (hi);
  lo = htonl (lo);

  return ((((uint64_t) lo) << 32) | ((uint64_t) hi));
} /* }}} uint64_t htonll */
#endif

static double htond (double val) /* {{{ */
{
  static int config = 0;

  union { uint8_t byte[8]; double floating; } in;
  union { uint8_t byte[8]; double floating; } out;

  if (config == 0)
  {
    double d = 8.642135e130;
    uint8_t c[8];

    memcpy (c, &d, 8);

    if ((c[0] == 0x2f) && (c[1] == 0x25)
        && (c[2] == 0xc0) && (c[3] == 0xc7)
        && (c[4] == 0x43) && (c[5] == 0x2b)
        && (c[6] == 0x1f) && (c[7] == 0x5b))
      config = 1; /* need nothing */
    else if ((c[7] == 0x2f) && (c[6] == 0x25)
        && (c[5] == 0xc0) && (c[4] == 0xc7)
        && (c[3] == 0x43) && (c[2] == 0x2b)
        && (c[1] == 0x1f) && (c[0] == 0x5b))
      config = 2; /* endian flip */
    else if ((c[4] == 0x2f) && (c[5] == 0x25)
        && (c[6] == 0xc0) && (c[7] == 0xc7)
        && (c[0] == 0x43) && (c[1] == 0x2b)
        && (c[2] == 0x1f) && (c[3] == 0x5b))
      config = 3; /* int swap */
    else
      config = 4;
  }

  if (isnan (val))
  {
    out.byte[0] = out.byte[1] = out.byte[2] = out.byte[3] = 0x00;
    out.byte[4] = out.byte[5] = 0x00;
    out.byte[6] = 0xf8;
    out.byte[7] = 0x7f;
    return (out.floating);
  }
  else if (config == 1)
    return (val);
  else if (config == 2)
  {
    in.floating = val;
    out.byte[0] = in.byte[7];
    out.byte[1] = in.byte[6];
    out.byte[2] = in.byte[5];
    out.byte[3] = in.byte[4];
    out.byte[4] = in.byte[3];
    out.byte[5] = in.byte[2];
    out.byte[6] = in.byte[1];
    out.byte[7] = in.byte[0];
    return (out.floating);
  }
  else if (config == 3)
  {
    in.floating = val;
    out.byte[0] = in.byte[4];
    out.byte[1] = in.byte[5];
    out.byte[2] = in.byte[6];
    out.byte[3] = in.byte[7];
    out.byte[4] = in.byte[0];
    out.byte[5] = in.byte[1];
    out.byte[6] = in.byte[2];
    out.byte[7] = in.byte[3];
    return (out.floating);
  }
  else
  {
    /* If in doubt, just copy the value back to the caller. */
    return (val);
  }
} /* }}} double htond */

static int nb_add_values (char **ret_buffer, /* {{{ */
    size_t *ret_buffer_len,
    const lcc_value_list_t *vl)
{
  char *packet_ptr;
  size_t packet_len;

  uint16_t      pkg_type;
  uint16_t      pkg_length;
  uint16_t      pkg_num_values;
  uint8_t       pkg_values_types[vl->values_len];
  value_t       pkg_values[vl->values_len];

  size_t offset;
  size_t i;

  packet_len = sizeof (pkg_type) + sizeof (pkg_length)
    + sizeof (pkg_num_values)
    + sizeof (pkg_values_types)
    + sizeof (pkg_values);

  if (*ret_buffer_len < packet_len)
    return (ENOMEM);

  pkg_type = htons (TYPE_VALUES);
  pkg_length = htons ((uint16_t) packet_len);
  pkg_num_values = htons ((uint16_t) vl->values_len);

  for (i = 0; i < vl->values_len; i++)
  {
    pkg_values_types[i] = (uint8_t) vl->values_types[i];
    switch (vl->values_types[i])
    {
      case LCC_TYPE_COUNTER:
        pkg_values[i].counter = (counter_t) htonll (vl->values[i].counter);
        break;

      case LCC_TYPE_GAUGE:
        pkg_values[i].gauge = (gauge_t) htond (vl->values[i].gauge);
        break;

      case LCC_TYPE_DERIVE:
        pkg_values[i].derive = (derive_t) htonll (vl->values[i].derive);
        break;

      case LCC_TYPE_ABSOLUTE:
        pkg_values[i].absolute = (absolute_t) htonll (vl->values[i].absolute);
        break;

      default:
        return (EINVAL);
    } /* switch (vl->values_types[i]) */
  } /* for (vl->values_len) */

  /*
   * Use `memcpy' to write everything to the buffer, because the pointer
   * may be unaligned and some architectures, such as SPARC, can't handle
   * that.
   */
  packet_ptr = *ret_buffer;
  offset = 0;
  memcpy (packet_ptr + offset, &pkg_type, sizeof (pkg_type));
  offset += sizeof (pkg_type);
  memcpy (packet_ptr + offset, &pkg_length, sizeof (pkg_length));
  offset += sizeof (pkg_length);
  memcpy (packet_ptr + offset, &pkg_num_values, sizeof (pkg_num_values));
  offset += sizeof (pkg_num_values);
  memcpy (packet_ptr + offset, pkg_values_types, sizeof (pkg_values_types));
  offset += sizeof (pkg_values_types);
  memcpy (packet_ptr + offset, pkg_values, sizeof (pkg_values));
  offset += sizeof (pkg_values);

  assert (offset == packet_len);

  *ret_buffer = packet_ptr + packet_len;
  *ret_buffer_len -= packet_len;
  return (0);
} /* }}} int nb_add_values */

static int nb_add_number (char **ret_buffer, /* {{{ */
    size_t *ret_buffer_len,
    uint16_t type, uint64_t value)
{
  char *packet_ptr;
  size_t packet_len;

  uint16_t pkg_type;
  uint16_t pkg_length;
  uint64_t pkg_value;

  size_t offset;

  packet_len = sizeof (pkg_type)
    + sizeof (pkg_length)
    + sizeof (pkg_value);

  if (*ret_buffer_len < packet_len)
    return (ENOMEM);

  pkg_type = htons (type);
  pkg_length = htons ((uint16_t) packet_len);
  pkg_value = htonll (value);

  packet_ptr = *ret_buffer;
  offset = 0;
  memcpy (packet_ptr + offset, &pkg_type, sizeof (pkg_type));
  offset += sizeof (pkg_type);
  memcpy (packet_ptr + offset, &pkg_length, sizeof (pkg_length));
  offset += sizeof (pkg_length);
  memcpy (packet_ptr + offset, &pkg_value, sizeof (pkg_value));
  offset += sizeof (pkg_value);

  assert (offset == packet_len);

  *ret_buffer = packet_ptr + packet_len;
  *ret_buffer_len -= packet_len;
  return (0);
} /* }}} int nb_add_number */

static int nb_add_time (char **ret_buffer, /* {{{ */
    size_t *ret_buffer_len,
    uint16_t type, double value)
{
  /* Convert to collectd's "cdtime" representation. */
  uint64_t cdtime_value = (uint64_t) (value * 1073741824.0);
  return (nb_add_number (ret_buffer, ret_buffer_len, type, cdtime_value));
} /* }}} int nb_add_time */

static int nb_add_string (char **ret_buffer, /* {{{ */
    size_t *ret_buffer_len,
    uint16_t type, const char *str, size_t str_len)
{
  char *packet_ptr;
  size_t packet_len;

  uint16_t pkg_type;
  uint16_t pkg_length;

  size_t offset;

  packet_len = sizeof (pkg_type)
    + sizeof (pkg_length)
    + str_len + 1;
  if (*ret_buffer_len < packet_len)
    return (ENOMEM);

  pkg_type = htons (type);
  pkg_length = htons ((uint16_t) packet_len);

  packet_ptr = *ret_buffer;
  offset = 0;
  memcpy (packet_ptr + offset, &pkg_type, sizeof (pkg_type));
  offset += sizeof (pkg_type);
  memcpy (packet_ptr + offset, &pkg_length, sizeof (pkg_length));
  offset += sizeof (pkg_length);
  memcpy (packet_ptr + offset, str, str_len);
  offset += str_len;
  memset (packet_ptr + offset, 0, 1);
  offset += 1;

  assert (offset == packet_len);

  *ret_buffer = packet_ptr + packet_len;
  *ret_buffer_len -= packet_len;
  return (0);
} /* }}} int nb_add_string */

static int nb_add_value_list (lcc_network_buffer_t *nb, /* {{{ */
    const lcc_value_list_t *vl)
{
  char *buffer = nb->ptr;
  size_t buffer_size = nb->free;

  const lcc_identifier_t *ident_src;
  lcc_identifier_t *ident_dst;

  ident_src = &vl->identifier;
  ident_dst = &nb->state.identifier;

  if (strcmp (ident_dst->host, ident_src->host) != 0)
  {
    if (nb_add_string (&buffer, &buffer_size, TYPE_HOST,
          ident_src->host, strlen (ident_src->host)) != 0)
      return (-1);
    SSTRNCPY (ident_dst->host, ident_src->host, sizeof (ident_dst->host));
  }

  if (strcmp (ident_dst->plugin, ident_src->plugin) != 0)
  {
    if (nb_add_string (&buffer, &buffer_size, TYPE_PLUGIN,
          ident_src->plugin, strlen (ident_src->plugin)) != 0)
      return (-1);
    SSTRNCPY (ident_dst->plugin, ident_src->plugin,
        sizeof (ident_dst->plugin));
  }

  if (strcmp (ident_dst->plugin_instance,
        ident_src->plugin_instance) != 0)
  {
    if (nb_add_string (&buffer, &buffer_size, TYPE_PLUGIN_INSTANCE,
          ident_src->plugin_instance,
          strlen (ident_src->plugin_instance)) != 0)
      return (-1);
    SSTRNCPY (ident_dst->plugin_instance, ident_src->plugin_instance,
        sizeof (ident_dst->plugin_instance));
  }

  if (strcmp (ident_dst->type, ident_src->type) != 0)
  {
    if (nb_add_string (&buffer, &buffer_size, TYPE_TYPE,
          ident_src->type, strlen (ident_src->type)) != 0)
      return (-1);
    SSTRNCPY (ident_dst->type, ident_src->type, sizeof (ident_dst->type));
  }

  if (strcmp (ident_dst->type_instance,
        ident_src->type_instance) != 0)
  {
    if (nb_add_string (&buffer, &buffer_size, TYPE_TYPE_INSTANCE,
          ident_src->type_instance,
          strlen (ident_src->type_instance)) != 0)
      return (-1);
    SSTRNCPY (ident_dst->type_instance, ident_src->type_instance,
        sizeof (ident_dst->type_instance));
  }

  if (nb->state.time != vl->time)
  {
    if (nb_add_time (&buffer, &buffer_size, TYPE_TIME_HR, vl->time))
      return (-1);
    nb->state.time = vl->time;
  }

  if (nb->state.interval != vl->interval)
  {
    if (nb_add_time (&buffer, &buffer_size, TYPE_INTERVAL_HR, vl->interval))
      return (-1);
    nb->state.interval = vl->interval;
  }

  if (nb_add_values (&buffer, &buffer_size, vl) != 0)
    return (-1);

  nb->ptr = buffer;
  nb->free = buffer_size;
  return (0);
} /* }}} int nb_add_value_list */

#if HAVE_LIBGCRYPT
static int nb_add_signature (lcc_network_buffer_t *nb) /* {{{ */
{
  char *buffer;
  size_t buffer_size;

  gcry_md_hd_t hd;
  gcry_error_t err;
  unsigned char *hash;
  const size_t hash_length = 32;

  /* The type, length and username have already been filled in by
   * "lcc_network_buffer_initialize". All we do here is calculate the hash over
   * the username and the data and add the hash value to the buffer. */

  buffer = nb->buffer + PART_SIGNATURE_SHA256_SIZE;
  assert (nb->size >= (nb->free + PART_SIGNATURE_SHA256_SIZE));
  buffer_size = nb->size - (nb->free + PART_SIGNATURE_SHA256_SIZE);

  hd = NULL;
  err = gcry_md_open (&hd, GCRY_MD_SHA256, GCRY_MD_FLAG_HMAC);
  if (err != 0)
    return (-1);

  assert (nb->password != NULL);
  err = gcry_md_setkey (hd, nb->password, strlen (nb->password));
  if (err != 0)
  {
    gcry_md_close (hd);
    return (-1);
  }

  gcry_md_write (hd, buffer, buffer_size);
  hash = gcry_md_read (hd, GCRY_MD_SHA256);
  if (hash == NULL)
  {
    gcry_md_close (hd);
    return (-1);
  }

  assert (((2 * sizeof (uint16_t)) + hash_length) == PART_SIGNATURE_SHA256_SIZE);
  memcpy (nb->buffer + (2 * sizeof (uint16_t)), hash, hash_length);

  gcry_md_close (hd);
  return (0);
} /* }}} int nb_add_signature */

static int nb_add_encryption (lcc_network_buffer_t *nb) /* {{{ */
{
  size_t package_length;
  char *encr_ptr; /* pointer to data being encrypted */
  size_t encr_size;

  char *hash_ptr; /* pointer to data being hashed */
  size_t hash_size;
  char hash[20];

  uint16_t pkg_length;
  gcry_error_t err;

  /* Fill in the package length */
  package_length = nb->size - nb->free;
  pkg_length = htons ((uint16_t) package_length);
  memcpy (nb->buffer + 2, &pkg_length, sizeof (pkg_length));

  /* Calculate what to hash */
  hash_ptr = nb->buffer + PART_ENCRYPTION_AES256_SIZE;
  hash_size = package_length - nb->encr_header_len;

  /* Calculate what to encrypt */
  encr_ptr = hash_ptr - sizeof (hash);
  encr_size = hash_size + sizeof (hash);

  /* Calculate the SHA-1 hash */
  gcry_md_hash_buffer (GCRY_MD_SHA1, hash, hash_ptr, hash_size);
  memcpy (encr_ptr, hash, sizeof (hash));

  if (nb->encr_cypher == NULL)
  {
    unsigned char password_hash[32];

    err = gcry_cipher_open (&nb->encr_cypher,
        GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_OFB, /* flags = */ 0);
    if (err != 0)
      return (-1);

    /* Calculate our 256bit key used for AES */
    gcry_md_hash_buffer (GCRY_MD_SHA256, password_hash,
        nb->password, strlen (nb->password));

    err = gcry_cipher_setkey (nb->encr_cypher,
        password_hash, sizeof (password_hash));
    if (err != 0)
    {
      gcry_cipher_close (nb->encr_cypher);
      nb->encr_cypher = NULL;
      return (-1);
    }
  }
  else /* if (nb->encr_cypher != NULL) */
  {
    gcry_cipher_reset (nb->encr_cypher);
  }

  /* Set the initialization vector */
  err = gcry_cipher_setiv (nb->encr_cypher,
      nb->encr_iv, sizeof (nb->encr_iv));
  if (err != 0)
  {
    gcry_cipher_close (nb->encr_cypher);
    nb->encr_cypher = NULL;
    return (-1);
  }

  /* Encrypt the buffer in-place */
  err = gcry_cipher_encrypt (nb->encr_cypher,
      encr_ptr, encr_size,
      /* in = */ NULL, /* in len = */ 0);
  if (err != 0)
  {
    gcry_cipher_close (nb->encr_cypher);
    nb->encr_cypher = NULL;
    return (-1);
  }

  return (0);
} /* }}} int nb_add_encryption */
#endif

/*
 * Public functions
 */
lcc_network_buffer_t *lcc_network_buffer_create (size_t size) /* {{{ */
{
  lcc_network_buffer_t *nb;

  if (size == 0)
    size = LCC_NETWORK_BUFFER_SIZE_DEFAULT;

  if (size < 128)
  {
    errno = EINVAL;
    return (NULL);
  }

  nb = malloc (sizeof (*nb));
  if (nb == NULL)
    return (NULL);
  memset (nb, 0, sizeof (*nb));

  nb->size = size;
  nb->buffer = malloc (nb->size);
  if (nb->buffer == NULL)
  {
    free (nb);
    return (NULL);
  }
  memset (nb->buffer, 0, nb->size);

  nb->ptr = nb->buffer;
  nb->free = nb->size;

  nb->seclevel = NONE;
  nb->username = NULL;
  nb->password = NULL;

  return (nb);
} /* }}} lcc_network_buffer_t *lcc_network_buffer_create */

void lcc_network_buffer_destroy (lcc_network_buffer_t *nb) /* {{{ */
{
  if (nb == NULL)
    return;

  free (nb->buffer);
  free (nb);
} /* }}} void lcc_network_buffer_destroy */

int lcc_network_buffer_set_security_level (lcc_network_buffer_t *nb, /* {{{ */
    lcc_security_level_t level,
    const char *username, const char *password)
{
  char *username_copy;
  char *password_copy;

  if (level == NONE)
  {
    free (nb->username);
    free (nb->password);
    nb->username = NULL;
    nb->password = NULL;
    nb->seclevel = NONE;
    lcc_network_buffer_initialize (nb);
    return (0);
  }

  if (!have_gcrypt ())
    return (ENOTSUP);

  username_copy = strdup (username);
  password_copy = strdup (password);
  if ((username_copy == NULL) || (password_copy == NULL))
  {
    free (username_copy);
    free (password_copy);
    return (ENOMEM);
  }

  free (nb->username);
  free (nb->password);
  nb->username = username_copy;
  nb->password = password_copy;
  nb->seclevel = level;

  lcc_network_buffer_initialize (nb);
  return (0);
} /* }}} int lcc_network_buffer_set_security_level */

int lcc_network_buffer_initialize (lcc_network_buffer_t *nb) /* {{{ */
{
  if (nb == NULL)
    return (EINVAL);

  memset (nb->buffer, 0, nb->size);
  memset (&nb->state, 0, sizeof (nb->state));
  nb->ptr = nb->buffer;
  nb->free = nb->size;

#if HAVE_LIBGCRYPT
  if (nb->seclevel == SIGN)
  {
    size_t username_len;
    uint16_t pkg_type = htons (TYPE_SIGN_SHA256);
    uint16_t pkg_length = PART_SIGNATURE_SHA256_SIZE;

    assert (nb->username != NULL);
    username_len = strlen (nb->username);
    pkg_length = htons (pkg_length + ((uint16_t) username_len));

    /* Fill in everything but the hash value here. */
    memcpy (nb->ptr, &pkg_type, sizeof (pkg_type));
    memcpy (nb->ptr + sizeof (pkg_type), &pkg_length, sizeof (pkg_length));
    nb->ptr += PART_SIGNATURE_SHA256_SIZE;
    nb->free -= PART_SIGNATURE_SHA256_SIZE;

    memcpy (nb->ptr, nb->username, username_len);
    nb->ptr += username_len;
    nb->free -= username_len;
  }
  else if (nb->seclevel == ENCRYPT)
  {
    size_t username_length = strlen (nb->username);
    uint16_t pkg_type = htons (TYPE_ENCR_AES256);
    uint16_t pkg_length = 0; /* Filled in in finalize. */
    uint16_t pkg_user_len = htons ((uint16_t) username_length);
    char hash[20];

    nb->encr_header_len = username_length;
    nb->encr_header_len += PART_ENCRYPTION_AES256_SIZE;

    gcry_randomize ((void *) &nb->encr_iv, sizeof (nb->encr_iv),
        GCRY_STRONG_RANDOM);

    /* Filled in in finalize. */
    memset (hash, 0, sizeof (hash));

    ADD_STATIC (nb, pkg_type);
    ADD_STATIC (nb, pkg_length);
    ADD_STATIC (nb, pkg_user_len);
    ADD_GENERIC (nb, nb->username, username_length);
    ADD_GENERIC (nb, nb->encr_iv, sizeof (nb->encr_iv));
    ADD_GENERIC (nb, hash, sizeof (hash));
    assert ((nb->encr_header_len + nb->free) == nb->size);
  }
#endif

  return (0);
} /* }}} int lcc_network_buffer_initialize */

int lcc_network_buffer_finalize (lcc_network_buffer_t *nb) /* {{{ */
{
  if (nb == NULL)
    return (EINVAL);

#if HAVE_LIBGCRYPT
  if (nb->seclevel == SIGN)
    return nb_add_signature (nb);
  else if (nb->seclevel == ENCRYPT)
    return nb_add_encryption (nb);
#endif

  return (0);
} /* }}} int lcc_network_buffer_finalize */

int lcc_network_buffer_add_value (lcc_network_buffer_t *nb, /* {{{ */
    const lcc_value_list_t *vl)
{
  int status;

  if ((nb == NULL) || (vl == NULL))
    return (EINVAL);

  status = nb_add_value_list (nb, vl);
  return (status);
} /* }}} int lcc_network_buffer_add_value */

int lcc_network_buffer_get (lcc_network_buffer_t *nb, /* {{{ */
    void *buffer, size_t *buffer_size)
{
  size_t sz_required;
  size_t sz_available;

  if ((nb == NULL) || (buffer_size == NULL))
    return (EINVAL);

  assert (nb->size >= nb->free);
  sz_required = nb->size - nb->free;
  sz_available = *buffer_size;

  *buffer_size = sz_required;
  if (buffer != NULL)
    memcpy (buffer, nb->buffer,
        (sz_available < sz_required) ? sz_available : sz_required);

  return (0);
} /* }}} int lcc_network_buffer_get */

/* vim: set sw=2 sts=2 et fdm=marker : */
