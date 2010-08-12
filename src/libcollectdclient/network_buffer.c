/**
 * collectd - src/libcollectdclient/network_buffer.c
 * Copyright (C) 2010  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; only version 2.1 of the License is
 * applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 **/

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <errno.h>
#include <arpa/inet.h> /* htons */

#include "collectd/network_buffer.h"

#define TYPE_HOST            0x0000
#define TYPE_TIME            0x0001
#define TYPE_PLUGIN          0x0002
#define TYPE_PLUGIN_INSTANCE 0x0003
#define TYPE_TYPE            0x0004
#define TYPE_TYPE_INSTANCE   0x0005
#define TYPE_VALUES          0x0006
#define TYPE_INTERVAL        0x0007

/* Types to transmit notifications */
#define TYPE_MESSAGE         0x0100
#define TYPE_SEVERITY        0x0101

#define TYPE_SIGN_SHA256     0x0200
#define TYPE_ENCR_AES256     0x0210

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
};

#define SSTRNCPY(dst,src,sz) do { \
  strncpy ((dst), (src), (sz));   \
  (dst)[(sz) - 1] = 0;            \
} while (0)

/*
 * Private functions
 */
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
    if (nb_add_number (&buffer, &buffer_size, TYPE_TIME,
          (uint64_t) vl->time))
      return (-1);
    nb->state.time = vl->time;
  }

  if (nb->state.interval != vl->interval)
  {
    if (nb_add_number (&buffer, &buffer_size, TYPE_INTERVAL,
          (uint64_t) vl->interval))
      return (-1);
    nb->state.interval = vl->interval;
  }

  if (nb_add_values (&buffer, &buffer_size, vl) != 0)
    return (-1);

  nb->ptr = buffer;
  nb->free = buffer_size;
  return (0);
} /* }}} int nb_add_value_list */

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
    const char *user, const char *password)
{
  /* FIXME: Not yet implemented */
  return (-1);
} /* }}} int lcc_network_buffer_set_security_level */

int lcc_network_buffer_initialize (lcc_network_buffer_t *nb) /* {{{ */
{
  if (nb == NULL)
    return (EINVAL);

  memset (nb->buffer, 0, nb->size);
  memset (&nb->state, 0, sizeof (nb->state));
  nb->ptr = nb->buffer;
  nb->free = nb->size;

  /* FIXME: If security is enabled, reserve space for the signature /
   * encryption block here. */

  return (0);
} /* }}} int lcc_network_buffer_initialize */

int lcc_network_buffer_finalize (lcc_network_buffer_t *nb) /* {{{ */
{
  if (nb == NULL)
    return (EINVAL);

  /* FIXME: If security is enabled, sign or encrypt the packet here. */

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
