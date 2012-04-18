/**
 * libcollectdclient - src/libcollectdclient/client.c
 * Copyright (C) 2008  Florian octo Forster
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

#if HAVE_CONFIG_H
# include "config.h"
#endif

#if !defined(__GNUC__) || !__GNUC__
# define __attribute__(x) /**/
#endif

#include "lcc_features.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <netdb.h>

#include "client.h"

/* NI_MAXHOST has been obsoleted by RFC 3493 which is a reason for SunOS 5.11
 * to no longer define it. We'll use the old, RFC 2553 value here. */
#ifndef NI_MAXHOST
# define NI_MAXHOST 1025
#endif

/* OpenBSD doesn't have EPROTO, FreeBSD doesn't have EILSEQ. Oh what joy! */
#ifndef EILSEQ
# ifdef EPROTO
#  define EILSEQ EPROTO
# else
#  define EILSEQ EINVAL
# endif
#endif

/* Secure/static macros. They work like `strcpy' and `strcat', but assure null
 * termination. They work for static buffers only, because they use `sizeof'.
 * The `SSTRCATF' combines the functionality of `snprintf' and `strcat' which
 * is very useful to add formatted stuff to the end of a buffer. */
#define SSTRCPY(d,s) do { \
    strncpy ((d), (s), sizeof (d)); \
    (d)[sizeof (d) - 1] = 0; \
  } while (0)

#define SSTRCAT(d,s) do { \
    size_t _l = strlen (d); \
    strncpy ((d) + _l, (s), sizeof (d) - _l); \
    (d)[sizeof (d) - 1] = 0; \
  } while (0)

#define SSTRCATF(d, ...) do { \
    char _b[sizeof (d)]; \
    snprintf (_b, sizeof (_b), __VA_ARGS__); \
    _b[sizeof (_b) - 1] = 0; \
    SSTRCAT ((d), _b); \
  } while (0)
    

#define LCC_SET_ERRSTR(c, ...) do { \
  snprintf ((c)->errbuf, sizeof ((c)->errbuf), __VA_ARGS__); \
  (c)->errbuf[sizeof ((c)->errbuf) - 1] = 0; \
} while (0)

#if COLLECT_DEBUG
# define LCC_DEBUG(...) printf (__VA_ARGS__)
#else
# define LCC_DEBUG(...) /**/
#endif

/*
 * Types
 */
struct lcc_connection_s
{
  FILE *fh;
  char errbuf[1024];
};

struct lcc_response_s
{
  int status;
  char message[1024];
  char **lines;
  size_t lines_num;
};
typedef struct lcc_response_s lcc_response_t;

/*
 * Private functions
 */
/* Even though Posix requires "strerror_r" to return an "int",
 * some systems (e.g. the GNU libc) return a "char *" _and_
 * ignore the second argument ... -tokkee */
static char *sstrerror (int errnum, char *buf, size_t buflen)
{
  buf[0] = 0;

#if !HAVE_STRERROR_R
  snprintf (buf, buflen, "Error #%i; strerror_r is not available.", errnum);
/* #endif !HAVE_STRERROR_R */

#elif STRERROR_R_CHAR_P
  {
    char *temp;
    temp = strerror_r (errnum, buf, buflen);
    if (buf[0] == 0)
    {
      if ((temp != NULL) && (temp != buf) && (temp[0] != 0))
        strncpy (buf, temp, buflen);
      else
        strncpy (buf, "strerror_r did not return "
            "an error message", buflen);
    }
  }
/* #endif STRERROR_R_CHAR_P */

#else
  if (strerror_r (errnum, buf, buflen) != 0)
  {
    snprintf (buf, buflen, "Error #%i; "
        "Additionally, strerror_r failed.",
        errnum);
  }
#endif /* STRERROR_R_CHAR_P */

  buf[buflen - 1] = 0;

  return (buf);
} /* char *sstrerror */

static int lcc_set_errno (lcc_connection_t *c, int err) /* {{{ */
{
  if (c == NULL)
    return (-1);

  sstrerror (err, c->errbuf, sizeof (c->errbuf));
  c->errbuf[sizeof (c->errbuf) - 1] = 0;

  return (0);
} /* }}} int lcc_set_errno */

static char *lcc_strescape (char *dest, const char *src, size_t dest_size) /* {{{ */
{
  size_t dest_pos;
  size_t src_pos;

  if ((dest == NULL) || (src == NULL))
    return (NULL);

  dest_pos = 0;
  src_pos = 0;

  assert (dest_size >= 3);

  dest[dest_pos] = '"';
  dest_pos++;

  while (42)
  {
    if ((dest_pos == (dest_size - 2))
        || (src[src_pos] == 0))
      break;

    if ((src[src_pos] == '"') || (src[src_pos] == '\\'))
    {
      /* Check if there is enough space for both characters.. */
      if (dest_pos == (dest_size - 3))
        break;

      dest[dest_pos] = '\\';
      dest_pos++;
    }

    dest[dest_pos] = src[src_pos];
    dest_pos++;
    src_pos++;
  }

  assert (dest_pos <= (dest_size - 2));

  dest[dest_pos] = '"';
  dest_pos++;

  dest[dest_pos] = 0;
  dest_pos++;
  src_pos++;

  return (dest);
} /* }}} char *lcc_strescape */

/* lcc_chomp: Removes all control-characters at the end of a string. */
static void lcc_chomp (char *str) /* {{{ */
{
  size_t str_len;

  str_len = strlen (str);
  while (str_len > 0)
  {
    if (str[str_len - 1] >= 32)
      break;
    str[str_len - 1] = 0;
    str_len--;
  }
} /* }}} void lcc_chomp */

static int lcc_identifier_cmp (const void *a, const void *b)
{
  const lcc_identifier_t *ident_a, *ident_b;

  int status;

  ident_a = a;
  ident_b = b;

  status = strcasecmp (ident_a->host, ident_b->host);
  if (status != 0)
    return (status);

  status = strcmp (ident_a->plugin, ident_b->plugin);
  if (status != 0)
    return (status);

  if ((*ident_a->plugin_instance != '\0') || (*ident_b->plugin_instance != '\0'))
  {
    if (*ident_a->plugin_instance == '\0')
      return (-1);
    else if (*ident_b->plugin_instance == '\0')
      return (1);

    status = strcmp (ident_a->plugin_instance, ident_b->plugin_instance);
    if (status != 0)
      return (status);
  }

  status = strcmp (ident_a->type, ident_b->type);
  if (status != 0)
    return (status);

  if ((*ident_a->type_instance != '\0') || (*ident_b->type_instance != '\0'))
  {
    if (*ident_a->type_instance == '\0')
      return (-1);
    else if (*ident_b->type_instance == '\0')
      return (1);

    status = strcmp (ident_a->type_instance, ident_b->type_instance);
    if (status != 0)
      return (status);
  }
  return (0);
} /* }}} int lcc_identifier_cmp */

static void lcc_response_free (lcc_response_t *res) /* {{{ */
{
  size_t i;

  if (res == NULL)
    return;

  for (i = 0; i < res->lines_num; i++)
    free (res->lines[i]);
  free (res->lines);
  res->lines = NULL;
} /* }}} void lcc_response_free */

static int lcc_send (lcc_connection_t *c, const char *command) /* {{{ */
{
  int status;

  LCC_DEBUG ("send:    --> %s\n", command);

  status = fprintf (c->fh, "%s\r\n", command);
  if (status < 0)
  {
    lcc_set_errno (c, errno);
    return (-1);
  }

  return (0);
} /* }}} int lcc_send */

static int lcc_receive (lcc_connection_t *c, /* {{{ */
    lcc_response_t *ret_res)
{
  lcc_response_t res;
  char *ptr;
  char buffer[4096];
  size_t i;

  memset (&res, 0, sizeof (res));

  /* Read the first line, containing the status and a message */
  ptr = fgets (buffer, sizeof (buffer), c->fh);
  if (ptr == NULL)
  {
    lcc_set_errno (c, errno);
    return (-1);
  }
  lcc_chomp (buffer);
  LCC_DEBUG ("receive: <-- %s\n", buffer);

  /* Convert the leading status to an integer and make `ptr' to point to the
   * beginning of the message. */
  ptr = NULL;
  errno = 0;
  res.status = strtol (buffer, &ptr, 0);
  if ((errno != 0) || (ptr == &buffer[0]))
  {
    lcc_set_errno (c, errno);
    return (-1);
  }

  /* Skip white spaces after the status number */
  while ((*ptr == ' ') || (*ptr == '\t'))
    ptr++;

  /* Now copy the message. */
  strncpy (res.message, ptr, sizeof (res.message));
  res.message[sizeof (res.message) - 1] = 0;

  /* Error or no lines follow: We're done. */
  if (res.status <= 0)
  {
    memcpy (ret_res, &res, sizeof (res));
    return (0);
  }

  /* Allocate space for the char-pointers */
  res.lines_num = (size_t) res.status;
  res.status = 0;
  res.lines = (char **) malloc (res.lines_num * sizeof (char *));
  if (res.lines == NULL)
  {
    lcc_set_errno (c, ENOMEM);
    return (-1);
  }

  /* Now receive all the lines */
  for (i = 0; i < res.lines_num; i++)
  {
    ptr = fgets (buffer, sizeof (buffer), c->fh);
    if (ptr == NULL)
    {
      lcc_set_errno (c, errno);
      break;
    }
    lcc_chomp (buffer);
    LCC_DEBUG ("receive: <-- %s\n", buffer);

    res.lines[i] = strdup (buffer);
    if (res.lines[i] == NULL)
    {
      lcc_set_errno (c, ENOMEM);
      break;
    }
  }

  /* Check if the for-loop exited with an error. */
  if (i < res.lines_num)
  {
    while (i > 0)
    {
      i--;
      free (res.lines[i]);
    }
    free (res.lines);
    return (-1);
  }

  memcpy (ret_res, &res, sizeof (res));
  return (0);
} /* }}} int lcc_receive */

static int lcc_sendreceive (lcc_connection_t *c, /* {{{ */
    const char *command, lcc_response_t *ret_res)
{
  lcc_response_t res;
  int status;

  if (c->fh == NULL)
  {
    lcc_set_errno (c, EBADF);
    return (-1);
  }

  status = lcc_send (c, command);
  if (status != 0)
    return (status);

  memset (&res, 0, sizeof (res));
  status = lcc_receive (c, &res);
  if (status == 0)
    memcpy (ret_res, &res, sizeof (*ret_res));

  return (status);
} /* }}} int lcc_sendreceive */

static int lcc_open_unixsocket (lcc_connection_t *c, const char *path) /* {{{ */
{
  struct sockaddr_un sa;
  int fd;
  int status;

  assert (c != NULL);
  assert (c->fh == NULL);
  assert (path != NULL);

  /* Don't use PF_UNIX here, because it's broken on Mac OS X (10.4, possibly
   * others). */
  fd = socket (AF_UNIX, SOCK_STREAM, /* protocol = */ 0);
  if (fd < 0)
  {
    lcc_set_errno (c, errno);
    return (-1);
  }

  memset (&sa, 0, sizeof (sa));
  sa.sun_family = AF_UNIX;
  strncpy (sa.sun_path, path, sizeof (sa.sun_path) - 1);

  status = connect (fd, (struct sockaddr *) &sa, sizeof (sa));
  if (status != 0)
  {
    lcc_set_errno (c, errno);
    close (fd);
    return (-1);
  }

  c->fh = fdopen (fd, "r+");
  if (c->fh == NULL)
  {
    lcc_set_errno (c, errno);
    close (fd);
    return (-1);
  }

  return (0);
} /* }}} int lcc_open_unixsocket */

static int lcc_open_netsocket (lcc_connection_t *c, /* {{{ */
    const char *addr_orig)
{
  struct addrinfo ai_hints;
  struct addrinfo *ai_res;
  struct addrinfo *ai_ptr;
  char addr_copy[NI_MAXHOST];
  char *addr;
  char *port;
  int fd;
  int status;

  assert (c != NULL);
  assert (c->fh == NULL);
  assert (addr_orig != NULL);

  strncpy(addr_copy, addr_orig, sizeof(addr_copy));
  addr_copy[sizeof(addr_copy) - 1] = '\0';
  addr = addr_copy;

  memset (&ai_hints, 0, sizeof (ai_hints));
  ai_hints.ai_flags = 0;
#ifdef AI_ADDRCONFIG
  ai_hints.ai_flags |= AI_ADDRCONFIG;
#endif
  ai_hints.ai_family = AF_UNSPEC;
  ai_hints.ai_socktype = SOCK_STREAM;

  port = NULL;
  if (*addr == '[') /* IPv6+port format */
  {
    /* `addr' is something like "[2001:780:104:2:211:24ff:feab:26f8]:12345" */
    addr++;

    port = strchr (addr, ']');
    if (port == NULL)
    {
      LCC_SET_ERRSTR (c, "malformed address: %s", addr_orig);
      return (-1);
    }
    *port = 0;
    port++;

    if (*port == ':')
      port++;
    else if (*port == 0)
      port = NULL;
    else
    {
      LCC_SET_ERRSTR (c, "garbage after address: %s", port);
      return (-1);
    }
  } /* if (*addr = ']') */
  else if (strchr (addr, '.') != NULL) /* Hostname or IPv4 */
  {
    port = strrchr (addr, ':');
    if (port != NULL)
    {
      *port = 0;
      port++;
    }
  }

  ai_res = NULL;
  status = getaddrinfo (addr,
                        port == NULL ? LCC_DEFAULT_PORT : port,
                        &ai_hints, &ai_res);
  if (status != 0)
  {
    LCC_SET_ERRSTR (c, "getaddrinfo: %s", gai_strerror (status));
    return (-1);
  }

  for (ai_ptr = ai_res; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
  {
    fd = socket (ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
    if (fd < 0)
    {
      status = errno;
      fd = -1;
      continue;
    }

    status = connect (fd, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
    if (status != 0)
    {
      status = errno;
      close (fd);
      fd = -1;
      continue;
    }

    c->fh = fdopen (fd, "r+");
    if (c->fh == NULL)
    {
      status = errno;
      close (fd);
      fd = -1;
      continue;
    }

    assert (status == 0);
    break;
  } /* for (ai_ptr) */

  if (status != 0)
  {
    lcc_set_errno (c, status);
    return (-1);
  }

  return (0);
} /* }}} int lcc_open_netsocket */

static int lcc_open_socket (lcc_connection_t *c, const char *addr) /* {{{ */
{
  int status = 0;

  if (addr == NULL)
    return (-1);

  assert (c != NULL);
  assert (c->fh == NULL);
  assert (addr != NULL);

  if (strncmp ("unix:", addr, strlen ("unix:")) == 0)
    status = lcc_open_unixsocket (c, addr + strlen ("unix:"));
  else if (addr[0] == '/')
    status = lcc_open_unixsocket (c, addr);
  else
    status = lcc_open_netsocket (c, addr);

  return (status);
} /* }}} int lcc_open_socket */

/*
 * Public functions
 */
unsigned int lcc_version (void) /* {{{ */
{
  return (LCC_VERSION);
} /* }}} unsigned int lcc_version */

const char *lcc_version_string (void) /* {{{ */
{
  return (LCC_VERSION_STRING);
} /* }}} const char *lcc_version_string */

const char *lcc_version_extra (void) /* {{{ */
{
  return (LCC_VERSION_EXTRA);
} /* }}} const char *lcc_version_extra */

int lcc_connect (const char *address, lcc_connection_t **ret_con) /* {{{ */
{
  lcc_connection_t *c;
  int status;

  if (address == NULL)
    return (-1);

  if (ret_con == NULL)
    return (-1);

  c = (lcc_connection_t *) malloc (sizeof (*c));
  if (c == NULL)
    return (-1);
  memset (c, 0, sizeof (*c));

  status = lcc_open_socket (c, address);
  if (status != 0)
  {
    lcc_disconnect (c);
    return (status);
  }

  *ret_con = c;
  return (0);
} /* }}} int lcc_connect */

int lcc_disconnect (lcc_connection_t *c) /* {{{ */
{
  if (c == NULL)
    return (-1);

  if (c->fh != NULL)
  {
    fclose (c->fh);
    c->fh = NULL;
  }

  free (c);
  return (0);
} /* }}} int lcc_disconnect */

int lcc_getval (lcc_connection_t *c, lcc_identifier_t *ident, /* {{{ */
    size_t *ret_values_num, gauge_t **ret_values, char ***ret_values_names)
{
  char ident_str[6 * LCC_NAME_LEN];
  char ident_esc[12 * LCC_NAME_LEN];
  char command[14 * LCC_NAME_LEN];

  lcc_response_t res;
  size_t   values_num;
  gauge_t *values = NULL;
  char   **values_names = NULL;

  size_t i;
  int status;

  if (c == NULL)
    return (-1);

  if (ident == NULL)
  {
    lcc_set_errno (c, EINVAL);
    return (-1);
  }

  /* Build a commend with an escaped version of the identifier string. */
  status = lcc_identifier_to_string (c, ident_str, sizeof (ident_str), ident);
  if (status != 0)
    return (status);

  snprintf (command, sizeof (command), "GETVAL %s",
      lcc_strescape (ident_esc, ident_str, sizeof (ident_esc)));
  command[sizeof (command) - 1] = 0;

  /* Send talk to the daemon.. */
  status = lcc_sendreceive (c, command, &res);
  if (status != 0)
    return (status);

  if (res.status != 0)
  {
    LCC_SET_ERRSTR (c, "Server error: %s", res.message);
    lcc_response_free (&res);
    return (-1);
  }

  values_num = res.lines_num;

#define BAIL_OUT(e) do { \
  lcc_set_errno (c, (e)); \
  free (values); \
  if (values_names != NULL) { \
    for (i = 0; i < values_num; i++) { \
      free (values_names[i]); \
    } \
  } \
  free (values_names); \
  lcc_response_free (&res); \
  return (-1); \
} while (0)

  /* If neither the values nor the names are requested, return here.. */
  if ((ret_values == NULL) && (ret_values_names == NULL))
  {
    if (ret_values_num != NULL)
      *ret_values_num = values_num;
    lcc_response_free (&res);
    return (0);
  }

  /* Allocate space for the values */
  if (ret_values != NULL)
  {
    values = (gauge_t *) malloc (values_num * sizeof (*values));
    if (values == NULL)
      BAIL_OUT (ENOMEM);
  }

  if (ret_values_names != NULL)
  {
    values_names = (char **) calloc (values_num, sizeof (*values_names));
    if (values_names == NULL)
      BAIL_OUT (ENOMEM);
  }

  for (i = 0; i < res.lines_num; i++)
  {
    char *key;
    char *value;
    char *endptr;

    key = res.lines[i];
    value = strchr (key, '=');
    if (value == NULL)
      BAIL_OUT (EILSEQ);

    *value = 0;
    value++;

    if (values != NULL)
    {
      endptr = NULL;
      errno = 0;
      values[i] = strtod (value, &endptr);

      if ((endptr == value) || (errno != 0))
        BAIL_OUT (errno);
    }

    if (values_names != NULL)
    {
      values_names[i] = strdup (key);
      if (values_names[i] == NULL)
        BAIL_OUT (ENOMEM);
    }
  } /* for (i = 0; i < res.lines_num; i++) */

  if (ret_values_num != NULL)
    *ret_values_num = values_num;
  if (ret_values != NULL)
    *ret_values = values;
  if (ret_values_names != NULL)
    *ret_values_names = values_names;

  lcc_response_free (&res);

  return (0);
} /* }}} int lcc_getval */

int lcc_putval (lcc_connection_t *c, const lcc_value_list_t *vl) /* {{{ */
{
  char ident_str[6 * LCC_NAME_LEN];
  char ident_esc[12 * LCC_NAME_LEN];
  char command[1024] = "";
  lcc_response_t res;
  int status;
  size_t i;

  if ((c == NULL) || (vl == NULL) || (vl->values_len < 1)
      || (vl->values == NULL) || (vl->values_types == NULL))
  {
    lcc_set_errno (c, EINVAL);
    return (-1);
  }

  status = lcc_identifier_to_string (c, ident_str, sizeof (ident_str),
      &vl->identifier);
  if (status != 0)
    return (status);

  SSTRCATF (command, "PUTVAL %s",
      lcc_strescape (ident_esc, ident_str, sizeof (ident_esc)));

  if (vl->interval > 0)
    SSTRCATF (command, " interval=%i", vl->interval);

  if (vl->time > 0)
    SSTRCATF (command, " %u", (unsigned int) vl->time);
  else
    SSTRCAT (command, " N");

  for (i = 0; i < vl->values_len; i++)
  {
    if (vl->values_types[i] == LCC_TYPE_COUNTER)
      SSTRCATF (command, ":%"PRIu64, vl->values[i].counter);
    else if (vl->values_types[i] == LCC_TYPE_GAUGE)
    {
      if (isnan (vl->values[i].gauge))
        SSTRCATF (command, ":U");
      else
        SSTRCATF (command, ":%g", vl->values[i].gauge);
    }
    else if (vl->values_types[i] == LCC_TYPE_DERIVE)
	SSTRCATF (command, ":%"PRIu64, vl->values[i].derive);
    else if (vl->values_types[i] == LCC_TYPE_ABSOLUTE)
	SSTRCATF (command, ":%"PRIu64, vl->values[i].absolute);

  } /* for (i = 0; i < vl->values_len; i++) */

  status = lcc_sendreceive (c, command, &res);
  if (status != 0)
    return (status);

  if (res.status != 0)
  {
    LCC_SET_ERRSTR (c, "Server error: %s", res.message);
    lcc_response_free (&res);
    return (-1);
  }

  lcc_response_free (&res);
  return (0);
} /* }}} int lcc_putval */

int lcc_flush (lcc_connection_t *c, const char *plugin, /* {{{ */
    lcc_identifier_t *ident, int timeout)
{
  char command[1024] = "";
  lcc_response_t res;
  int status;

  if (c == NULL)
  {
    lcc_set_errno (c, EINVAL);
    return (-1);
  }

  SSTRCPY (command, "FLUSH");

  if (timeout > 0)
    SSTRCATF (command, " timeout=%i", timeout);

  if (plugin != NULL)
  {
    char buffer[2 * LCC_NAME_LEN];
    SSTRCATF (command, " plugin=%s",
        lcc_strescape (buffer, plugin, sizeof (buffer)));
  }

  if (ident != NULL)
  {
    char ident_str[6 * LCC_NAME_LEN];
    char ident_esc[12 * LCC_NAME_LEN];

    status = lcc_identifier_to_string (c, ident_str, sizeof (ident_str), ident);
    if (status != 0)
      return (status);

    SSTRCATF (command, " identifier=%s",
        lcc_strescape (ident_esc, ident_str, sizeof (ident_esc)));
  }

  status = lcc_sendreceive (c, command, &res);
  if (status != 0)
    return (status);

  if (res.status != 0)
  {
    LCC_SET_ERRSTR (c, "Server error: %s", res.message);
    lcc_response_free (&res);
    return (-1);
  }

  lcc_response_free (&res);
  return (0);
} /* }}} int lcc_flush */

/* TODO: Implement lcc_putnotif */

int lcc_listval (lcc_connection_t *c, /* {{{ */
    lcc_identifier_t **ret_ident, size_t *ret_ident_num)
{
  lcc_response_t res;
  size_t i;
  int status;

  lcc_identifier_t *ident;
  size_t ident_num;

  if (c == NULL)
    return (-1);

  if ((ret_ident == NULL) || (ret_ident_num == NULL))
  {
    lcc_set_errno (c, EINVAL);
    return (-1);
  }

  status = lcc_sendreceive (c, "LISTVAL", &res);
  if (status != 0)
    return (status);

  if (res.status != 0)
  {
    LCC_SET_ERRSTR (c, "Server error: %s", res.message);
    lcc_response_free (&res);
    return (-1);
  }

  ident_num = res.lines_num;
  ident = (lcc_identifier_t *) malloc (ident_num * sizeof (*ident));
  if (ident == NULL)
  {
    lcc_response_free (&res);
    lcc_set_errno (c, ENOMEM);
    return (-1);
  }

  for (i = 0; i < res.lines_num; i++)
  {
    char *time_str;
    char *ident_str;

    /* First field is the time. */
    time_str = res.lines[i];

    /* Set `ident_str' to the beginning of the second field. */
    ident_str = time_str;
    while ((*ident_str != ' ') && (*ident_str != '\t') && (*ident_str != 0))
      ident_str++;
    while ((*ident_str == ' ') || (*ident_str == '\t'))
    {
      *ident_str = 0;
      ident_str++;
    }

    if (*ident_str == 0)
    {
      lcc_set_errno (c, EILSEQ);
      status = -1;
      break;
    }

    status = lcc_string_to_identifier (c, ident + i, ident_str);
    if (status != 0)
      break;
  }

  lcc_response_free (&res);

  if (status != 0)
  {
    free (ident);
    return (-1);
  }

  *ret_ident = ident;
  *ret_ident_num = ident_num;

  return (0);
} /* }}} int lcc_listval */

const char *lcc_strerror (lcc_connection_t *c) /* {{{ */
{
  if (c == NULL)
    return ("Invalid object");
  return (c->errbuf);
} /* }}} const char *lcc_strerror */

int lcc_identifier_to_string (lcc_connection_t *c, /* {{{ */
    char *string, size_t string_size, const lcc_identifier_t *ident)
{
  if ((string == NULL) || (string_size < 6) || (ident == NULL))
  {
    lcc_set_errno (c, EINVAL);
    return (-1);
  }

  if (ident->plugin_instance[0] == 0)
  {
    if (ident->type_instance[0] == 0)
      snprintf (string, string_size, "%s/%s/%s",
          ident->host,
          ident->plugin,
          ident->type);
    else
      snprintf (string, string_size, "%s/%s/%s-%s",
          ident->host,
          ident->plugin,
          ident->type,
          ident->type_instance);
  }
  else
  {
    if (ident->type_instance[0] == 0)
      snprintf (string, string_size, "%s/%s-%s/%s",
          ident->host,
          ident->plugin,
          ident->plugin_instance,
          ident->type);
    else
      snprintf (string, string_size, "%s/%s-%s/%s-%s",
          ident->host,
          ident->plugin,
          ident->plugin_instance,
          ident->type,
          ident->type_instance);
  }

  string[string_size - 1] = 0;
  return (0);
} /* }}} int lcc_identifier_to_string */

int lcc_string_to_identifier (lcc_connection_t *c, /* {{{ */
    lcc_identifier_t *ident, const char *string)
{
  char *string_copy;
  char *host;
  char *plugin;
  char *plugin_instance;
  char *type;
  char *type_instance;

  string_copy = strdup (string);
  if (string_copy == NULL)
  {
    lcc_set_errno (c, ENOMEM);
    return (-1);
  }

  host = string_copy;
  plugin = strchr (host, '/');
  if (plugin == NULL)
  {
    LCC_SET_ERRSTR (c, "Malformed identifier string: %s", string);
    free (string_copy);
    return (-1);
  }
  *plugin = 0;
  plugin++;

  type = strchr (plugin, '/');
  if (type == NULL)
  {
    LCC_SET_ERRSTR (c, "Malformed identifier string: %s", string);
    free (string_copy);
    return (-1);
  }
  *type = 0;
  type++;

  plugin_instance = strchr (plugin, '-');
  if (plugin_instance != NULL)
  {
    *plugin_instance = 0;
    plugin_instance++;
  }

  type_instance = strchr (type, '-');
  if (type_instance != NULL)
  {
    *type_instance = 0;
    type_instance++;
  }

  memset (ident, 0, sizeof (*ident));

  SSTRCPY (ident->host, host);
  SSTRCPY (ident->plugin, plugin);
  if (plugin_instance != NULL)
    SSTRCPY (ident->plugin_instance, plugin_instance);
  SSTRCPY (ident->type, type);
  if (type_instance != NULL)
    SSTRCPY (ident->type_instance, type_instance);

  free (string_copy);
  return (0);
} /* }}} int lcc_string_to_identifier */

int lcc_sort_identifiers (lcc_connection_t *c, /* {{{ */
    lcc_identifier_t *idents, size_t idents_num)
{
  if (idents == NULL)
  {
    lcc_set_errno (c, EINVAL);
    return (-1);
  }

  qsort (idents, idents_num, sizeof (*idents), lcc_identifier_cmp);
  return (0);
} /* }}} int lcc_sort_identifiers */

/* vim: set sw=2 sts=2 et fdm=marker : */
