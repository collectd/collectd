/**
 * collectd - src/netcmd.c
 * Copyright (C) 2007-2011  Florian octo Forster
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
 * Author:
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include "utils_cmd_flush.h"
#include "utils_cmd_getval.h"
#include "utils_cmd_listval.h"
#include "utils_cmd_putval.h"
#include "utils_cmd_putnotif.h"

/* Folks without pthread will need to disable this plugin. */
#include <pthread.h>

#include <sys/socket.h>
#include <sys/poll.h>
#include <netdb.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <grp.h>

#include <gnutls/gnutls.h>

#define NC_DEFAULT_SERVICE "25826"
#define NC_TLS_DH_BITS 1024

/*
 * Private data structures
 */
struct nc_peer_s
{
  char *node;
  char *service;
  int *fds;
  size_t fds_num;

  char *tls_cert_file;
  char *tls_key_file;
  char *tls_ca_file;
  char *tls_crl_file;
  _Bool tls_verify_peer;

  gnutls_certificate_credentials_t tls_credentials;
  gnutls_dh_params_t tls_dh_params;
  gnutls_priority_t tls_priority;

};
typedef struct nc_peer_s nc_peer_t;

struct nc_connection_s
{
  int fd;

  gnutls_session_t tls_session;
  _Bool have_tls_session;
};
typedef struct nc_connection_s nc_connection_t;

/*
 * Private variables
 */

/* socket configuration */
static nc_peer_t *peers = NULL;
static size_t     peers_num;

static struct pollfd  *pollfd = NULL;
static size_t          pollfd_num;

static int       listen_thread_loop = 0;
static int       listen_thread_running = 0;
static pthread_t listen_thread;

/*
 * Functions
 */
static nc_peer_t *nc_fd_to_peer (int fd) /* {{{ */
{
  size_t i;

  for (i = 0; i < peers_num; i++)
  {
    size_t j;

    for (j = 0; j < peers[i].fds_num; j++)
      if (peers[i].fds[j] == fd)
        return (peers + i);
  }

  return (NULL);
} /* }}} nc_peer_t *nc_fd_to_peer */

static int nc_register_fd (nc_peer_t *peer, int fd) /* {{{ */
{
  struct pollfd *poll_ptr;
  int *fd_ptr;

  poll_ptr = realloc (pollfd, (pollfd_num + 1) * sizeof (*pollfd));
  if (poll_ptr == NULL)
  {
    ERROR ("netcmd plugin: realloc failed.");
    return (-1);
  }
  pollfd = poll_ptr;

  memset (&pollfd[pollfd_num], 0, sizeof (pollfd[pollfd_num]));
  pollfd[pollfd_num].fd = fd;
  pollfd[pollfd_num].events = POLLIN | POLLPRI;
  pollfd[pollfd_num].revents = 0;
  pollfd_num++;

  if (peer == NULL)
    return (0);

  fd_ptr = realloc (peer->fds, (peer->fds_num + 1) * sizeof (*peer->fds));
  if (fd_ptr == NULL)
  {
    ERROR ("netcmd plugin: realloc failed.");
    return (-1);
  }
  peer->fds = fd_ptr;
  peer->fds[peer->fds_num] = fd;
  peer->fds_num++;

  return (0);
} /* }}} int nc_register_fd */

static int nc_tls_init (nc_peer_t *peer) /* {{{ */
{
  if (peer == NULL)
    return (EINVAL);

  if ((peer->tls_cert_file == NULL)
      || (peer->tls_key_file == NULL))
    return (0);

  /* Initialize the structure holding our certificate information. */
  gnutls_certificate_allocate_credentials (&peer->tls_credentials);

  /* Set up the configured certificates. */
  if (peer->tls_ca_file != NULL)
    gnutls_certificate_set_x509_trust_file (peer->tls_credentials,
        peer->tls_ca_file, GNUTLS_X509_FMT_PEM);
  if (peer->tls_crl_file != NULL)
      gnutls_certificate_set_x509_crl_file (peer->tls_credentials,
          peer->tls_crl_file, GNUTLS_X509_FMT_PEM);
  gnutls_certificate_set_x509_key_file (peer->tls_credentials,
      peer->tls_cert_file, peer->tls_key_file, GNUTLS_X509_FMT_PEM);

  /* Initialize Diffie-Hellman parameters. */
  gnutls_dh_params_init (&peer->tls_dh_params);
  gnutls_dh_params_generate2 (peer->tls_dh_params, NC_TLS_DH_BITS);
  gnutls_certificate_set_dh_params (peer->tls_credentials,
      peer->tls_dh_params);

  /* Initialize a "priority cache". This will tell GNUTLS which algorithms to
   * use and which to avoid. We use the "NORMAL" method for now. */
  gnutls_priority_init (&peer->tls_priority,
     /* priority = */ "NORMAL", /* errpos = */ NULL);

  return (0);
} /* }}} int nc_tls_init */

static gnutls_session_t nc_tls_get_session (nc_peer_t *peer) /* {{{ */
{
  gnutls_session_t session;

  if (peer->tls_credentials == NULL)
    return (NULL);

  /* Initialize new session. */
  gnutls_init (&session, GNUTLS_SERVER);

  /* Set cipher priority and credentials based on the information stored with
   * the peer. */
  gnutls_priority_set (session, peer->tls_priority);
  gnutls_credentials_set (session,
      GNUTLS_CRD_CERTIFICATE, peer->tls_credentials);

  /* Request the client certificate. */
  gnutls_certificate_server_set_request (session, GNUTLS_CERT_REQUEST);

  return (session);
} /* }}} gnutls_session_t nc_tls_get_session */

static int nc_open_socket (nc_peer_t *peer) /* {{{ */
{
  struct addrinfo ai_hints;
  struct addrinfo *ai_list;
  struct addrinfo *ai_ptr;
  int status;

  const char *node = NULL;
  const char *service = NULL;

  if (peer != NULL)
  {
    node = peer->node;
    service = peer->service;
  }

  if (service == NULL)
    service = NC_DEFAULT_SERVICE;

  memset (&ai_hints, 0, sizeof (ai_hints));
#ifdef AI_PASSIVE
  ai_hints.ai_flags |= AI_PASSIVE;
#endif
#ifdef AI_ADDRCONFIG
  ai_hints.ai_flags |= AI_ADDRCONFIG;
#endif
  ai_hints.ai_family = AF_UNSPEC;
  ai_hints.ai_socktype = SOCK_STREAM;

  ai_list = NULL;

  if (service == NULL)
    service = NC_DEFAULT_SERVICE;

  status = getaddrinfo (node, service, &ai_hints, &ai_list);
  if (status != 0)
  {
    ERROR ("netcmd plugin: getaddrinfo failed: %s",
        gai_strerror (status));
    return (-1);
  }

  for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
  {
    char errbuf[1024];
    int fd;

    fd = socket (ai_ptr->ai_family, ai_ptr->ai_socktype,
        ai_ptr->ai_protocol);
    if (fd < 0)
    {
      ERROR ("netcmd plugin: socket(2) failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      continue;
    }

    status = bind (fd, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
    if (status != 0)
    {
      close (fd);
      ERROR ("netcmd plugin: bind(2) failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      continue;
    }

    status = listen (fd, /* backlog = */ 8);
    if (status != 0)
    {
      close (fd);
      ERROR ("netcmd plugin: listen(2) failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      continue;
    }

    status = nc_register_fd (peer, fd);
    if (status != 0)
    {
      close (fd);
      continue;
    }
  } /* for (ai_next) */

  freeaddrinfo (ai_list);

  return (nc_tls_init (peer));
} /* }}} int nc_open_socket */

static void nc_connection_close (nc_connection_t *conn) /* {{{ */
{
  if (conn == NULL)
    return;

  if (conn->fd >= 0)
  {
    close (conn->fd);
    conn->fd = -1;
  }

  if (conn->have_tls_session)
  {
    gnutls_deinit (conn->tls_session);
    conn->have_tls_session = 0;
  }

  sfree (conn);
} /* }}} void nc_connection_close */

static void *nc_handle_client (void *arg) /* {{{ */
{
  nc_connection_t *conn;
  FILE *fhin, *fhout;
  char errbuf[1024];

  conn = arg;

  DEBUG ("netcmd plugin: nc_handle_client: Reading from fd #%i", conn->fd);

  fhin  = fdopen (conn->fd, "r");
  if (fhin == NULL)
  {
    ERROR ("netcmd plugin: fdopen failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    nc_connection_close (conn);
    pthread_exit ((void *) 1);
  }

  /* FIXME: dup conn->fd before calling fdopen! */
  fhout = fdopen (conn->fd, "w");
  /* Prevent nc_connection_close from calling close(2) on this fd. */
  conn->fd = -1;
  if (fhout == NULL)
  {
    ERROR ("netcmd plugin: fdopen failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    fclose (fhin); /* this closes fd as well */
    nc_connection_close (conn);
    pthread_exit ((void *) 1);
  }

  /* change output buffer to line buffered mode */
  if (setvbuf (fhout, NULL, _IOLBF, 0) != 0)
  {
    ERROR ("netcmd plugin: setvbuf failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    nc_connection_close (conn);
    pthread_exit ((void *) 1);
  }

  while (42)
  {
    char buffer[1024];
    char buffer_copy[1024];
    char *fields[128];
    int   fields_num;
    int   len;

    errno = 0;
    if (fgets (buffer, sizeof (buffer), fhin) == NULL)
    {
      if (errno != 0)
      {
        WARNING ("netcmd plugin: failed to read from socket #%i: %s",
            fileno (fhin),
            sstrerror (errno, errbuf, sizeof (errbuf)));
      }
      break;
    }

    len = strlen (buffer);
    while ((len > 0)
        && ((buffer[len - 1] == '\n') || (buffer[len - 1] == '\r')))
      buffer[--len] = '\0';

    if (len == 0)
      continue;

    sstrncpy (buffer_copy, buffer, sizeof (buffer_copy));

    fields_num = strsplit (buffer_copy, fields,
        sizeof (fields) / sizeof (fields[0]));

    if (fields_num < 1)
    {
      nc_connection_close (conn);
      break;
    }

    if (strcasecmp (fields[0], "getval") == 0)
    {
      handle_getval (fhout, buffer);
    }
    else if (strcasecmp (fields[0], "putval") == 0)
    {
      handle_putval (fhout, buffer);
    }
    else if (strcasecmp (fields[0], "listval") == 0)
    {
      handle_listval (fhout, buffer);
    }
    else if (strcasecmp (fields[0], "putnotif") == 0)
    {
      handle_putnotif (fhout, buffer);
    }
    else if (strcasecmp (fields[0], "flush") == 0)
    {
      handle_flush (fhout, buffer);
    }
    else
    {
      if (fprintf (fhout, "-1 Unknown command: %s\n", fields[0]) < 0)
      {
        WARNING ("netcmd plugin: failed to write to socket #%i: %s",
            fileno (fhout),
            sstrerror (errno, errbuf, sizeof (errbuf)));
        break;
      }
    }
  } /* while (fgets) */

  DEBUG ("netcmd plugin: nc_handle_client: Exiting..");
  /* XXX: Is this calling close on the same FD twice? */
  fclose (fhin);
  fclose (fhout);
  nc_connection_close (conn);

  pthread_exit ((void *) 0);
  return ((void *) 0);
} /* }}} void *nc_handle_client */

static void *nc_server_thread (void __attribute__((unused)) *arg) /* {{{ */
{
  int  status;
  pthread_t th;
  pthread_attr_t th_attr;
  char errbuf[1024];
  size_t i;

  for (i = 0; i < peers_num; i++)
    nc_open_socket (peers + i);

  if (peers_num == 0)
    nc_open_socket (NULL);

  if (pollfd_num == 0)
  {
    ERROR ("netcmd plugin: No sockets could be opened.");
    pthread_exit ((void *) -1);
  }

  while (listen_thread_loop != 0)
  {
    status = poll (pollfd, (nfds_t) pollfd_num, /* timeout = */ -1);
    if (status < 0)
    {
      if ((errno == EINTR) || (errno == EAGAIN))
        continue;

      ERROR ("netcmd plugin: poll(2) failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      listen_thread_loop = 0;
      continue;
    }

    for (i = 0; i < pollfd_num; i++)
    {
      nc_peer_t *peer;
      nc_connection_t *conn;

      if (pollfd[i].revents == 0)
      {
        continue;
      }
      else if ((pollfd[i].revents & (POLLERR | POLLHUP | POLLNVAL))
          != 0)
      {
        WARNING ("netcmd plugin: File descriptor %i failed.",
            pollfd[i].fd);
        close (pollfd[i].fd);
        pollfd[i].fd = -1;
        pollfd[i].events = 0;
        pollfd[i].revents = 0;
        continue;
      }
      pollfd[i].revents = 0;

      peer = nc_fd_to_peer (pollfd[i].fd);
      if (peer == NULL)
      {
        ERROR ("netcmd plugin: Unable to find peer structure for file "
            "descriptor #%i.", pollfd[i].fd);
        continue;
      }

      status = accept (pollfd[i].fd,
          /* sockaddr = */ NULL,
          /* sockaddr_len = */ NULL);
      if (status < 0)
      {
        if (errno != EINTR)
          ERROR ("netcmd plugin: accept failed: %s",
              sstrerror (errno, errbuf, sizeof (errbuf)));
        continue;
      }

      conn = malloc (sizeof (*conn));
      if (conn == NULL)
      {
        ERROR ("netcmd plugin: malloc failed.");
        close (status);
        continue;
      }
      memset (conn, 0, sizeof (*conn));

      conn->fd = status;
      if ((peer != NULL)
          && (peer->tls_cert_file != NULL))
      {
        DEBUG ("netcmd plugin: Starting TLS session on [%s]:%s",
            (peer->node != NULL) ? peer->node : "any",
            (peer->service != NULL) ? peer->service : NC_DEFAULT_SERVICE);
        conn->tls_session = nc_tls_get_session (peer);
        conn->have_tls_session = 1;
      }

      DEBUG ("Spawning child to handle connection on fd %i", conn->fd);

      pthread_attr_init (&th_attr);
      pthread_attr_setdetachstate (&th_attr, PTHREAD_CREATE_DETACHED);

      status = pthread_create (&th, &th_attr, nc_handle_client,
          conn);
      if (status != 0)
      {
        WARNING ("netcmd plugin: pthread_create failed: %s",
            sstrerror (errno, errbuf, sizeof (errbuf)));
        nc_connection_close (conn);
        continue;
      }
    }
  } /* while (listen_thread_loop) */

  for (i = 0; i < pollfd_num; i++)
  {
    if (pollfd[i].fd < 0)
      continue;

    close (pollfd[i].fd);
    pollfd[i].fd = -1;
    pollfd[i].events = 0;
    pollfd[i].revents = 0;
  }

  sfree (pollfd);
  pollfd_num = 0;

  return ((void *) 0);
} /* }}} void *nc_server_thread */

/*
 * <Plugin netcmd>
 *   <Listen>
 *     Address "::1"
 *     Port "1234"
 *     TLSCertFile "/path/to/cert"
 *     TLSKeyFile  "/path/to/key"
 *     TLSCAFile   "/path/to/ca"
 *     TLSCRLFile  "/path/to/crl"
 *     TLSVerifyPeer yes|no
 *   </Listen>
 * </Plugin>
 */
static int nc_config_peer (const oconfig_item_t *ci) /* {{{ */
{
  nc_peer_t *p;
  int i;

  p = realloc (peers, sizeof (*peers) * (peers_num + 1));
  if (p == NULL)
  {
    ERROR ("netcmd plugin: realloc failed.");
    return (ENOMEM);
  }
  peers = p;
  p = peers + peers_num;
  memset (p, 0, sizeof (*p));
  p->node = NULL;
  p->service = NULL;
  p->tls_cert_file = NULL;
  p->tls_key_file = NULL;
  p->tls_ca_file = NULL;
  p->tls_crl_file = NULL;
  p->tls_verify_peer = 1;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Address", child->key) == 0)
      cf_util_get_string (child, &p->node);
    else if (strcasecmp ("Port", child->key) == 0)
      cf_util_get_string (child, &p->service);
    else if (strcasecmp ("TLSCertFile", child->key) == 0)
      cf_util_get_string (child, &p->tls_cert_file);
    else if (strcasecmp ("TLSKeyFile", child->key) == 0)
      cf_util_get_string (child, &p->tls_key_file);
    else if (strcasecmp ("TLSCAFile", child->key) == 0)
      cf_util_get_string (child, &p->tls_ca_file);
    else if (strcasecmp ("TLSCRLFile", child->key) == 0)
      cf_util_get_string (child, &p->tls_crl_file);
    else
      WARNING ("netcmd plugin: The option \"%s\" is not recognized within "
          "a \"%s\" block.", child->key, ci->key);
  }

  DEBUG ("netcmd plugin: node = \"%s\"; service = \"%s\";", p->node, p->service);

  peers_num++;

  return (0);
} /* }}} int nc_config_peer */

static int nc_config (oconfig_item_t *ci)
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Listen", child->key) == 0)
      nc_config_peer (child);
    else
      WARNING ("netcmd plugin: The option \"%s\" is not recognized.",
          child->key);
  }

  return (0);
} /* int nc_config */

static int nc_init (void)
{
  static int have_init = 0;

  int status;

  /* Initialize only once. */
  if (have_init != 0)
    return (0);
  have_init = 1;

  listen_thread_loop = 1;

  status = pthread_create (&listen_thread, NULL, nc_server_thread, NULL);
  if (status != 0)
  {
    char errbuf[1024];
    listen_thread_loop = 0;
    listen_thread_running = 0;
    ERROR ("netcmd plugin: pthread_create failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  listen_thread_running = 1;
  return (0);
} /* int nc_init */

static int nc_shutdown (void)
{
  void *ret;

  listen_thread_loop = 0;

  if (listen_thread != (pthread_t) 0)
  {
    pthread_kill (listen_thread, SIGTERM);
    pthread_join (listen_thread, &ret);
    listen_thread = (pthread_t) 0;
  }

  plugin_unregister_init ("netcmd");
  plugin_unregister_shutdown ("netcmd");

  return (0);
} /* int nc_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("netcmd", nc_config);
  plugin_register_init ("netcmd", nc_init);
  plugin_register_shutdown ("netcmd", nc_shutdown);
} /* void module_register (void) */

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */
