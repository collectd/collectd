/**
 * collectd - src/netcmd.c
 * Copyright (C) 2007-2013  Florian octo Forster
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
struct nc_peer_s /* {{{ */
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
}; /* }}} */
typedef struct nc_peer_s nc_peer_t;

#if defined(PAGESIZE)
# define NC_READ_BUFFER_SIZE PAGESIZE
#elif defined(PAGE_SIZE)
# define NC_READ_BUFFER_SIZE PAGE_SIZE
#else
# define NC_READ_BUFFER_SIZE 4096
#endif

struct nc_connection_s /* {{{ */
{
  /* TLS fields */
  int fd;
  char *read_buffer;
  size_t read_buffer_fill;

  /* non-TLS fields */
  FILE *fh_in;
  FILE *fh_out;

  gnutls_session_t tls_session;
  _Bool have_tls_session;
  _Bool tls_verify_peer;
}; /* }}} */
typedef struct nc_connection_s nc_connection_t;

struct nc_proxy_s
{
  int pipe_rx;
  int pipe_tx;

  gnutls_session_t tls_session;
};
typedef struct nc_proxy_s nc_proxy_t;

/*
 * Private variables
 */

/* socket configuration */
static nc_peer_t *peers = NULL;
static size_t     peers_num;

static struct pollfd  *pollfd = NULL;
static size_t          pollfd_num;

static _Bool     listen_thread_loop = 0;
static _Bool     listen_thread_running = 0;
static pthread_t listen_thread;

/*
 * Functions
 */
static const char *nc_verify_status_to_string (gnutls_certificate_status_t status)
{
  if (status == 0)
    return ("Valid");
  else if (status & GNUTLS_CERT_INVALID)
    return ("Invalid");
  else if (status & GNUTLS_CERT_REVOKED)
    return ("Revoked");
  else if (status & GNUTLS_CERT_SIGNER_NOT_FOUND)
    return ("Signer not found");
  else if (status & GNUTLS_CERT_SIGNER_NOT_CA)
    return ("Signer not a CA");
  else if (status & GNUTLS_CERT_INSECURE_ALGORITHM)
    return ("Insecure algorithm");
#if GNUTLS_VERSION_NUMBER >= 0x020708
  else if (status & GNUTLS_CERT_NOT_ACTIVATED)
    return ("Not activated");
  else if (status & GNUTLS_CERT_EXPIRED)
    return ("Expired");
#endif
  else
    return (NULL);
} /* }}} const char *nc_verify_status_to_string */

static void *nc_proxy_thread (void *args) /* {{{ */
{
  nc_proxy_t *data = args;
  struct pollfd fds[2];
  int gtls_fd;
  long pagesize;

  gtls_fd = (int) gnutls_transport_get_ptr (data->tls_session);
  DEBUG ("netcmd plugin: nc_proxy_thread: pipe_rx = %i; pipe_tx = %i; gtls_fd = %i;",
      data->pipe_rx, data->pipe_tx, gtls_fd);

  memset (fds, 0, sizeof (fds));
  fds[0].fd = data->pipe_rx;
  fds[0].events = POLLIN | POLLPRI;
  fds[1].fd = gtls_fd;
  fds[1].events = POLLIN | POLLPRI;

  pagesize = sysconf (_SC_PAGESIZE);

  while (42)
  {
    char errbuf[1024];
    char buffer[pagesize];
    int status;

    status = poll (fds, STATIC_ARRAY_SIZE(fds), /* timeout = */ -1);
    if (status < 0)
    {
      if ((errno == EINTR) || (errno == EAGAIN))
        continue;
      ERROR ("netcmd plugin: poll(2) failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      break;
    }

    /* pipe -> TLS */
    if (fds[0].revents != 0) /* {{{ */
    {
      ssize_t iostatus;
      size_t buffer_size;
      char *buffer_ptr;

      DEBUG ("netcmd plugin: nc_proxy_thread: Something's up on the pipe.");

      /* Check for hangup, error, ... */
      if ((fds[0].revents & (POLLIN | POLLPRI)) == 0)
        break;

      iostatus = read (fds[0].fd, buffer, sizeof (buffer));
      DEBUG ("netcmd plugin: nc_proxy_thread: Received %zi bytes from pipe.",
          iostatus);
      if (iostatus < 0)
      {
        if ((errno == EINTR) || (errno == EAGAIN))
          continue;
        ERROR ("netcmd plugin: read(2) failed: %s",
            sstrerror (errno, errbuf, sizeof (errbuf)));
        break;
      }
      else if (iostatus == 0)
      {
        break;
      }

      buffer_ptr = buffer;
      buffer_size = (size_t) iostatus;
      while (buffer_size > 0)
      {
        iostatus = gnutls_record_send (data->tls_session,
            buffer, buffer_size);
        DEBUG ("netcmd plugin: nc_proxy_thread: Wrote %zi bytes to GNU-TLS.",
            iostatus);
        if (iostatus < 0)
        {
          ERROR ("netcmd plugin: gnutls_record_send failed: %s",
              gnutls_strerror ((int) iostatus));
          break;
        }

        assert (iostatus <= buffer_size);
        buffer_ptr += iostatus;
        buffer_size -= iostatus;
      } /* while (buffer_size > 0) */

      if (buffer_size != 0)
        break;

      fds[0].revents = 0;
    } /* }}} if (fds[0].revents != 0) */

    /* TLS -> pipe */
    if (fds[1].revents != 0) /* {{{ */
    {
      ssize_t iostatus;
      size_t buffer_size;

      DEBUG ("netcmd plugin: nc_proxy_thread: Something's up on the TLS socket.");

      /* Check for hangup, error, ... */
      if ((fds[1].revents & (POLLIN | POLLPRI)) == 0)
        break;

      iostatus = gnutls_record_recv (data->tls_session, buffer, sizeof (buffer));
      DEBUG ("netcmd plugin: nc_proxy_thread: Received %zi bytes from GNU-TLS.",
          iostatus);
      if (iostatus < 0)
      {
        if ((iostatus == GNUTLS_E_INTERRUPTED)
            || (iostatus == GNUTLS_E_AGAIN))
          continue;
        ERROR ("netcmd plugin: gnutls_record_recv failed: %s",
            gnutls_strerror ((int) iostatus));
        break;
      }
      else if (iostatus == 0)
      {
        break;
      }

      buffer_size = (size_t) iostatus;
      iostatus = swrite (data->pipe_tx, buffer, buffer_size);
      DEBUG ("netcmd plugin: nc_proxy_thread:  Wrote %zi bytes to pipe.",
          iostatus);

      fds[1].revents = 0;
    } /* }}} if (fds[1].revents != 0) */
  } /* while (42) */

  DEBUG ("netcmd plugin: nc_proxy_thread: Shutting down.");
  return (NULL);
} /* }}} void *nc_proxy_thread */

/* Creates two pipes and a separate thread to pass data between two FILE* and
 * the GNUTLS back and forth. This is required because the handle_<cmd>
 * functions expect to be able to write to a FILE*. */
static int nc_start_tls_file_handles (nc_connection_t *conn) /* {{{ */
{
#define BAIL_OUT(status) do { \
  DEBUG ("netcmd plugin: nc_start_tls_file_handles: Bailing out with status %i.", (status)); \
  if (proxy_config->pipe_rx >= 0) { close (proxy_config->pipe_rx); }         \
  if (proxy_config->pipe_tx >= 0) { close (proxy_config->pipe_tx); }         \
  if (conn->fh_in != NULL) { fclose (conn->fh_in); conn->fh_in = NULL; }     \
  if (conn->fh_out != NULL) { fclose (conn->fh_out); conn->fh_out = NULL; }  \
  free (proxy_config);                                                       \
  return (status);                                                           \
} while (0)

  nc_proxy_t *proxy_config;
  int pipe_fd[2];
  int status;

  pthread_attr_t attr;
  pthread_t thread;

  if ((conn->fh_in != NULL) || (conn->fh_out != NULL))
  {
    ERROR ("netcmd plugin: nc_start_tls_file_handles: Connection already connected.");
    return (EEXIST);
  }

  proxy_config = malloc (sizeof (*proxy_config));
  if (proxy_config == NULL)
  {
    ERROR ("netcmd plugin: malloc failed.");
    return (ENOMEM);
  }
  memset (proxy_config, 0, sizeof (*proxy_config));
  proxy_config->pipe_rx = -1;
  proxy_config->pipe_tx = -1;
  proxy_config->tls_session = conn->tls_session;

  pipe_fd[0] = pipe_fd[1] = -1;
  status = pipe (pipe_fd);
  if (status != 0)
  {
    char errmsg[1024];
    ERROR ("netcmd plugin: pipe(2) failed: %s",
        sstrerror (errno, errmsg, sizeof (errmsg)));
    BAIL_OUT (-1);
  }
  proxy_config->pipe_rx = pipe_fd[0];
  conn->fh_out = fdopen (pipe_fd[1], "w");
  if (conn->fh_out == NULL)
  {
    char errmsg[1024];
    ERROR ("netcmd plugin: fdopen(2) failed: %s",
        sstrerror (errno, errmsg, sizeof (errmsg)));
    close (pipe_fd[1]);
    BAIL_OUT (-1);
  }

  pipe_fd[0] = pipe_fd[1] = -1;
  status = pipe (pipe_fd);
  if (status != 0)
  {
    char errmsg[1024];
    ERROR ("netcmd plugin: pipe(2) failed: %s",
        sstrerror (errno, errmsg, sizeof (errmsg)));
    BAIL_OUT (-1);
  }
  proxy_config->pipe_tx = pipe_fd[1];
  conn->fh_in = fdopen (pipe_fd[0], "r");
  if (conn->fh_in == NULL)
  {
    char errmsg[1024];
    ERROR ("netcmd plugin: fdopen(2) failed: %s",
        sstrerror (errno, errmsg, sizeof (errmsg)));
    close (pipe_fd[0]);
    BAIL_OUT (-1);
  }

  pthread_attr_init (&attr);
  pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);

  status = pthread_create (&thread, &attr, nc_proxy_thread, proxy_config);
  pthread_attr_destroy (&attr);
  if (status != 0)
  {
    char errmsg[1024];
    ERROR ("netcmd plugin: pthread_create(2) failed: %s",
        sstrerror (errno, errmsg, sizeof (errmsg)));
    BAIL_OUT (-1);
  }

  DEBUG ("netcmd plugin: nc_start_tls_file_handles: Successfully started proxy thread.");
  return (0);
} /* }}} int nc_start_tls_file_handles */

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

static void nc_free_peer (nc_peer_t *p) /* {{{ */
{
  size_t i;
  if (p == NULL)
    return;

  sfree (p->node);
  sfree (p->service);

  for (i = 0; i < p->fds_num; i++)
  {
    if (p->fds[i] >= 0)
      close (p->fds[i]);
    p->fds[i] = -1;
  }
  p->fds_num = 0;
  sfree (p->fds);

  sfree (p->tls_cert_file);
  sfree (p->tls_key_file);
  sfree (p->tls_ca_file);
  sfree (p->tls_crl_file);

  gnutls_certificate_free_credentials (p->tls_credentials);
  gnutls_dh_params_deinit (p->tls_dh_params);
  gnutls_priority_deinit (p->tls_priority);
} /* }}} void nc_free_peer */

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
  int status;

  if (peer == NULL)
    return (EINVAL);

  if (peer->tls_key_file == NULL)
  {
    DEBUG ("netcmd plugin: Not setting up TLS environment for peer.");
    return (0);
  }

  DEBUG ("netcmd plugin: Setting up TLS environment for peer.");

  /* Initialize the structure holding our certificate information. */
  status = gnutls_certificate_allocate_credentials (&peer->tls_credentials);
  if (status != GNUTLS_E_SUCCESS)
  {
    ERROR ("netcmd plugin: gnutls_certificate_allocate_credentials failed: %s",
        gnutls_strerror (status));
    return (status);
  }

  /* Set up the configured certificates. */
  if (peer->tls_ca_file != NULL)
  {
    status = gnutls_certificate_set_x509_trust_file (peer->tls_credentials,
        peer->tls_ca_file, GNUTLS_X509_FMT_PEM);
    if (status < 0)
    {
      ERROR ("netcmd plugin: gnutls_certificate_set_x509_trust_file (%s) "
          "failed: %s",
          peer->tls_ca_file, gnutls_strerror (status));
      return (status);
    }
    else
    {
      DEBUG ("netcmd plugin: Successfully loaded %i CA(s).", status);
    }
  }

  if (peer->tls_crl_file != NULL)
  {
    status = gnutls_certificate_set_x509_crl_file (peer->tls_credentials,
        peer->tls_crl_file, GNUTLS_X509_FMT_PEM);
    if (status < 0)
    {
      ERROR ("netcmd plugin: gnutls_certificate_set_x509_crl_file (%s) "
          "failed: %s",
          peer->tls_crl_file, gnutls_strerror (status));
      return (status);
    }
    else
    {
      DEBUG ("netcmd plugin: Successfully loaded %i CRL(s).", status);
    }
  }

  status = gnutls_certificate_set_x509_key_file (peer->tls_credentials,
      peer->tls_cert_file, peer->tls_key_file, GNUTLS_X509_FMT_PEM);
  if (status != GNUTLS_E_SUCCESS)
  {
    ERROR ("netcmd plugin: gnutls_certificate_set_x509_key_file failed: %s",
        gnutls_strerror (status));
    return (status);
  }

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
  int status;

  if (peer->tls_credentials == NULL)
    return (NULL);

  DEBUG ("netcmd plugin: nc_tls_get_session (%s)", peer->node);

  /* Initialize new session. */
  gnutls_init (&session, GNUTLS_SERVER);

  /* Set cipher priority and credentials based on the information stored with
   * the peer. */
  status = gnutls_priority_set (session, peer->tls_priority);
  if (status != GNUTLS_E_SUCCESS)
  {
    ERROR ("netcmd plugin: gnutls_priority_set failed: %s",
        gnutls_strerror (status));
    gnutls_deinit (session);
    return (NULL);
  }

  status = gnutls_credentials_set (session,
      GNUTLS_CRD_CERTIFICATE, peer->tls_credentials);
  if (status != GNUTLS_E_SUCCESS)
  {
    ERROR ("netcmd plugin: gnutls_credentials_set failed: %s",
        gnutls_strerror (status));
    gnutls_deinit (session);
    return (NULL);
  }

  /* Request the client certificate. If TLSVerifyPeer is set to true,
   * *require* a client certificate. */
  gnutls_certificate_server_set_request (session,
      peer->tls_verify_peer ? GNUTLS_CERT_REQUIRE : GNUTLS_CERT_REQUEST);

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

  if (conn->fh_in != NULL)
  {
    fclose (conn->fh_in);
    conn->fh_in = NULL;
  }

  if (conn->fh_out != NULL)
  {
    fclose (conn->fh_out);
    conn->fh_out = NULL;
  }

  if (conn->have_tls_session)
  {
    gnutls_deinit (conn->tls_session);
    conn->have_tls_session = 0;
  }

  sfree (conn);
} /* }}} void nc_connection_close */

static int nc_connection_init_tls (nc_connection_t *conn) /* {{{ */
{
  int status;
  intptr_t fd;

  conn->read_buffer = malloc (NC_READ_BUFFER_SIZE);
  if (conn->read_buffer == NULL)
    return (ENOMEM);
  memset (conn->read_buffer, 0, NC_READ_BUFFER_SIZE);

  /* Make (relatively) sure that 'fd' and 'void*' have the same size to make
   * GCC happy. */
  fd = (intptr_t) conn->fd;
  gnutls_transport_set_ptr (conn->tls_session,
      (gnutls_transport_ptr_t) fd);

  while (42)
  {
    status = gnutls_handshake (conn->tls_session);
    if (status == GNUTLS_E_SUCCESS)
      break;
    else if ((status == GNUTLS_E_AGAIN) || (status == GNUTLS_E_INTERRUPTED))
      continue;
    else
    {
      ERROR ("netcmd plugin: gnutls_handshake failed: %s",
          gnutls_strerror (status));
      return (status);
    }
  }

  if (conn->tls_verify_peer)
  {
    unsigned int verify_status = 0;

    status = gnutls_certificate_verify_peers2 (conn->tls_session,
        &verify_status);
    if (status != GNUTLS_E_SUCCESS)
    {
      ERROR ("netcmd plugin: gnutls_certificate_verify_peers2 failed: %s",
          gnutls_strerror (status));
      return (status);
    }

    if (verify_status != 0)
    {
      const char *reason;

      reason = nc_verify_status_to_string (verify_status);
      if (reason == NULL)
        ERROR ("netcmd plugin: Verification of peer failed with "
            "status %i (%#x)", verify_status, verify_status);
      else
        ERROR ("netcmd plugin: Verification of peer failed with "
            "status %i (%s)", verify_status, reason);

      return (-1);
    }
  } /* if (conn->tls_verify_peer) */

  status = nc_start_tls_file_handles (conn);
  if (status != 0)
  {
    nc_connection_close (conn);
    return (-1);
  }

  return (0);
} /* }}} int nc_connection_init_tls */

static int nc_connection_init (nc_connection_t *conn) /* {{{ */
{
  int fd_copy;
  char errbuf[1024];

  if (conn->have_tls_session)
    return (nc_connection_init_tls (conn));

  /* Duplicate the file descriptor. We need two file descriptors, because we
   * create two FILE* objects. If they pointed to the same FD and we called
   * fclose() on each, that would call close() twice on the same FD. If
   * another file is opened in between those two calls, it could get assigned
   * that FD and weird stuff would happen. */
  fd_copy = dup (conn->fd);
  if (fd_copy < 0)
  {
    ERROR ("netcmd plugin: dup(2) failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  conn->fh_in  = fdopen (conn->fd, "r");
  if (conn->fh_in == NULL)
  {
    ERROR ("netcmd plugin: fdopen failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }
  /* Prevent other code from using the FD directly. */
  conn->fd = -1;

  conn->fh_out = fdopen (fd_copy, "w");
  /* Prevent nc_connection_close from calling close(2) on this fd. */
  if (conn->fh_out == NULL)
  {
    ERROR ("netcmd plugin: fdopen failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  /* change output buffer to line buffered mode */
  if (setvbuf (conn->fh_out, NULL, _IOLBF, 0) != 0)
  {
    ERROR ("netcmd plugin: setvbuf failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    nc_connection_close (conn);
    return (-1);
  }

  return (0);
} /* }}} int nc_connection_init */

static char *nc_connection_gets (nc_connection_t *conn, /* {{{ */
    char *buffer, size_t buffer_size)
{
  ssize_t status;
  char *orig_buffer = buffer;

  if (conn == NULL)
  {
    errno = EINVAL;
    return (NULL);
  }

  if (!conn->have_tls_session)
    return (fgets (buffer, (int) buffer_size, conn->fh_in));

  if ((buffer == NULL) || (buffer_size < 2))
  {
    errno = EINVAL;
    return (NULL);
  }

  /* ensure null termination */
  memset (buffer, 0, buffer_size);
  buffer_size--;

  while (42)
  {
    size_t max_copy_bytes;
    size_t newline_pos;
    _Bool found_newline;
    size_t i;

    /* If there's no more data in the read buffer, read another chunk from the
     * socket. */
    if (conn->read_buffer_fill < 1)
    {
      status = gnutls_record_recv (conn->tls_session,
          conn->read_buffer, NC_READ_BUFFER_SIZE);
      if (status < 0) /* error */
      {
        ERROR ("netcmd plugin: Error while reading from TLS stream.");
        return (NULL);
      }
      else if (status == 0) /* we reached end of file */
      {
        if (orig_buffer == buffer) /* nothing has been written to the buffer yet */
          return (NULL); /* end of file */
        else
          return (orig_buffer);
      }
      else
      {
        conn->read_buffer_fill = (size_t) status;
      }
    }
    assert (conn->read_buffer_fill > 0);

    /* Determine where the first newline character is in the buffer. We're not
     * using strcspn(3) here, becaus the buffer is possibly not
     * null-terminated. */
    newline_pos = conn->read_buffer_fill;
    found_newline = 0;
    for (i = 0; i < conn->read_buffer_fill; i++)
    {
      if (conn->read_buffer[i] == '\n')
      {
        newline_pos = i;
        found_newline = 1;
        break;
      }
    }

    /* Determine how many bytes to copy at most. This is MIN(buffer available,
     * read buffer size, characters to newline). */
    max_copy_bytes = buffer_size;
    if (max_copy_bytes > conn->read_buffer_fill)
      max_copy_bytes = conn->read_buffer_fill;
    if (max_copy_bytes > (newline_pos + 1))
      max_copy_bytes = newline_pos + 1;
    assert (max_copy_bytes > 0);

    /* Copy bytes to the output buffer. */
    memcpy (buffer, conn->read_buffer, max_copy_bytes);
    buffer += max_copy_bytes;
    assert (buffer_size >= max_copy_bytes);
    buffer_size -= max_copy_bytes;

    /* If there is data left in the read buffer, move it to the front of the
     * buffer. */
    if (max_copy_bytes < conn->read_buffer_fill)
    {
      size_t data_left_size = conn->read_buffer_fill - max_copy_bytes;
      memmove (conn->read_buffer, conn->read_buffer + max_copy_bytes,
          data_left_size);
      conn->read_buffer_fill -= max_copy_bytes;
    }
    else
    {
      assert (max_copy_bytes == conn->read_buffer_fill);
      conn->read_buffer_fill = 0;
    }

    if (found_newline)
      break;

    if (buffer_size == 0) /* no more space in the output buffer */
      break;
  }

  return (orig_buffer);
} /* }}} char *nc_connection_gets */

static void *nc_handle_client (void *arg) /* {{{ */
{
  nc_connection_t *conn;
  char errbuf[1024];
  int status;

  conn = arg;

  DEBUG ("netcmd plugin: nc_handle_client: Reading from fd #%i", conn->fd);

  status = nc_connection_init (conn);
  if (status != 0)
  {
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
    if (nc_connection_gets (conn, buffer, sizeof (buffer)) == NULL)
    {
      if (errno != 0)
      {
        WARNING ("netcmd plugin: failed to read from socket #%i: %s",
            fileno (conn->fh_in),
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
      handle_getval (conn->fh_out, buffer);
    }
    else if (strcasecmp (fields[0], "putval") == 0)
    {
      handle_putval (conn->fh_out, buffer);
    }
    else if (strcasecmp (fields[0], "listval") == 0)
    {
      handle_listval (conn->fh_out, buffer);
    }
    else if (strcasecmp (fields[0], "putnotif") == 0)
    {
      handle_putnotif (conn->fh_out, buffer);
    }
    else if (strcasecmp (fields[0], "flush") == 0)
    {
      handle_flush (conn->fh_out, buffer);
    }
    else
    {
      if (fprintf (conn->fh_out, "-1 Unknown command: %s\n", fields[0]) < 0)
      {
        WARNING ("netcmd plugin: failed to write to socket #%i: %s",
            fileno (conn->fh_out),
            sstrerror (errno, errbuf, sizeof (errbuf)));
        break;
      }
    }
  } /* while (fgets) */

  DEBUG ("netcmd plugin: nc_handle_client: Exiting..");
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

  while (listen_thread_loop)
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
      conn->fh_in = NULL;
      conn->fh_out = NULL;

      conn->fd = status;

      /* Start up the TLS session if the required configuration options have
       * been given. */
      if ((peer != NULL)
          && (peer->tls_key_file != NULL))
      {
        DEBUG ("netcmd plugin: Starting TLS session on a connection "
            "via [%s]:%s",
            (peer->node != NULL) ? peer->node : "any",
            (peer->service != NULL) ? peer->service : NC_DEFAULT_SERVICE);
        conn->tls_session = nc_tls_get_session (peer);
        if (conn->tls_session == NULL)
        {
          ERROR ("netcmd plugin: Creating TLS session on a connection via "
              "[%s]:%s failed. For security reasons this connection will be "
              "terminated.",
              (peer->node != NULL) ? peer->node : "any",
              (peer->service != NULL) ? peer->service : NC_DEFAULT_SERVICE);
          nc_connection_close (conn);
          continue;
        }
        conn->have_tls_session = 1;
        conn->tls_verify_peer = peer->tls_verify_peer;
      }

      DEBUG ("netcmd plugin: Spawning child to handle connection on fd #%i",
          conn->fd);

      pthread_attr_init (&th_attr);
      pthread_attr_setdetachstate (&th_attr, PTHREAD_CREATE_DETACHED);

      status = pthread_create (&th, &th_attr, nc_handle_client, conn);
      pthread_attr_destroy (&th_attr);
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
  _Bool success;
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
  p->tls_verify_peer = 0;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Address", child->key) == 0)
      cf_util_get_string (child, &p->node);
    else if (strcasecmp ("Port", child->key) == 0)
      cf_util_get_service (child, &p->service);
    else if (strcasecmp ("TLSCertFile", child->key) == 0)
      cf_util_get_string (child, &p->tls_cert_file);
    else if (strcasecmp ("TLSKeyFile", child->key) == 0)
      cf_util_get_string (child, &p->tls_key_file);
    else if (strcasecmp ("TLSCAFile", child->key) == 0)
      cf_util_get_string (child, &p->tls_ca_file);
    else if (strcasecmp ("TLSCRLFile", child->key) == 0)
      cf_util_get_string (child, &p->tls_crl_file);
    else if (strcasecmp ("TLSVerifyPeer", child->key) == 0)
      cf_util_get_boolean (child, &p->tls_verify_peer);
    else
      WARNING ("netcmd plugin: The option \"%s\" is not recognized within "
          "a \"%s\" block.", child->key, ci->key);
  }

  /* TLS is confusing for many people. Be verbose on mis-configurations to
   * help people set up encryption correctly. */
  success = 1;
  if (p->tls_key_file == NULL)
  {
    if (p->tls_cert_file != NULL)
    {
      WARNING ("netcmd plugin: The \"TLSCertFile\" option is only valid in "
          "combination with the \"TLSKeyFile\" option.");
      success = 0;
    }
    if (p->tls_ca_file != NULL)
    {
      WARNING ("netcmd plugin: The \"TLSCAFile\" option is only valid when "
          "the \"TLSKeyFile\" option has been specified.");
      success = 0;
    }
    if (p->tls_crl_file != NULL)
    {
      WARNING ("netcmd plugin: The \"TLSCRLFile\" option is only valid when "
          "the \"TLSKeyFile\" option has been specified.");
      success = 0;
    }
  }
  else if (p->tls_cert_file == NULL)
  {
    WARNING ("netcmd plugin: The \"TLSKeyFile\" option is only valid in "
        "combination with the \"TLSCertFile\" option.");
    success = 0;
  }

  if (!success)
  {
    ERROR ("netcmd plugin: Problems in the security settings have been "
        "detected in the <Listen /> block for [%s]:%s. The entire block "
        "will be ignored to prevent unauthorized access.",
        (p->node == NULL) ? "::0" : p->node,
        (p->service == NULL) ? NC_DEFAULT_SERVICE : p->service);
    nc_free_peer (p);
    return (-1);
  }

  DEBUG ("netcmd plugin: node = \"%s\"; service = \"%s\";", p->node, p->service);

  peers_num++;

  return (0);
} /* }}} int nc_config_peer */

static int nc_config (oconfig_item_t *ci) /* {{{ */
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
} /* }}} int nc_config */

static int nc_init (void) /* {{{ */
{
  static int have_init = 0;

  int status;

  /* Initialize only once. */
  if (have_init != 0)
    return (0);
  have_init = 1;

  gnutls_global_init ();

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
} /* }}} int nc_init */

static int nc_shutdown (void) /* {{{ */
{
  size_t i;

  listen_thread_loop = 0;

  if (listen_thread != (pthread_t) 0)
  {
    void *ret;

    pthread_kill (listen_thread, SIGTERM);
    pthread_join (listen_thread, &ret);
    listen_thread = (pthread_t) 0;
  }

  plugin_unregister_init ("netcmd");
  plugin_unregister_shutdown ("netcmd");

  for (i = 0; i < peers_num; i++)
    nc_free_peer (peers + i);
  peers_num = 0;
  sfree (peers);

  return (0);
} /* }}} int nc_shutdown */

void module_register (void) /* {{{ */
{
  plugin_register_complex_config ("netcmd", nc_config);
  plugin_register_init ("netcmd", nc_init);
  plugin_register_shutdown ("netcmd", nc_shutdown);
} /* }}} void module_register (void) */

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */
