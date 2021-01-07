/**
 * collectd - src/unixsock.c
 * Copyright (C) 2007,2008  Florian octo Forster
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

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include "utils/cmds/flush.h"
#include "utils/cmds/getthreshold.h"
#include "utils/cmds/getval.h"
#include "utils/cmds/listval.h"
#include "utils/cmds/putnotif.h"
#include "utils/cmds/putval.h"

#include <sys/stat.h>
#include <sys/un.h>

#include <grp.h>

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX sizeof(((struct sockaddr_un *)0)->sun_path)
#endif

#define US_DEFAULT_PATH LOCALSTATEDIR "/run/" PACKAGE_NAME "-unixsock"

/*
 * Private variables
 */
/* valid configuration file keys */
static const char *config_keys[] = {"SocketFile", "SocketGroup", "SocketPerms",
                                    "DeleteSocket"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static int loop;

/* socket configuration */
static int sock_fd = -1;
static char *sock_file;
static char *sock_group;
static int sock_perms = S_IRWXU | S_IRWXG;
static bool delete_socket;

static pthread_t listen_thread = (pthread_t)0;

/*
 * Functions
 */
static int us_open_socket(void) {
  struct sockaddr_un sa = {0};
  int status;

  sock_fd = socket(PF_UNIX, SOCK_STREAM, 0);
  if (sock_fd < 0) {
    ERROR("unixsock plugin: socket failed: %s", STRERRNO);
    return -1;
  }

  sa.sun_family = AF_UNIX;
  sstrncpy(sa.sun_path, (sock_file != NULL) ? sock_file : US_DEFAULT_PATH,
           sizeof(sa.sun_path));

  DEBUG("unixsock plugin: socket path = %s", sa.sun_path);

  if (delete_socket) {
    errno = 0;
    status = unlink(sa.sun_path);
    if ((status != 0) && (errno != ENOENT)) {
      WARNING("unixsock plugin: Deleting socket file \"%s\" failed: %s",
              sa.sun_path, STRERRNO);
    } else if (status == 0) {
      INFO("unixsock plugin: Successfully deleted socket file \"%s\".",
           sa.sun_path);
    }
  }

  status = bind(sock_fd, (struct sockaddr *)&sa, sizeof(sa));
  if (status != 0) {
    ERROR("unixsock plugin: bind failed: %s", STRERRNO);
    close(sock_fd);
    sock_fd = -1;
    return -1;
  }

  status = chmod(sa.sun_path, sock_perms);
  if (status == -1) {
    ERROR("unixsock plugin: chmod failed: %s", STRERRNO);
    close(sock_fd);
    sock_fd = -1;
    return -1;
  }

  status = listen(sock_fd, 8);
  if (status != 0) {
    ERROR("unixsock plugin: listen failed: %s", STRERRNO);
    close(sock_fd);
    sock_fd = -1;
    return -1;
  }

  do {
    const char *grpname;
    struct group *g;
    struct group sg;

    long int grbuf_size = sysconf(_SC_GETGR_R_SIZE_MAX);
    if (grbuf_size <= 0)
      grbuf_size = sysconf(_SC_PAGESIZE);
    if (grbuf_size <= 0)
      grbuf_size = 4096;
    char grbuf[grbuf_size];

    grpname = (sock_group != NULL) ? sock_group : COLLECTD_GRP_NAME;
    g = NULL;

    status = getgrnam_r(grpname, &sg, grbuf, sizeof(grbuf), &g);
    if (status != 0) {
      WARNING("unixsock plugin: getgrnam_r (%s) failed: %s", grpname,
              STRERROR(status));
      break;
    }
    if (g == NULL) {
      WARNING("unixsock plugin: No such group: `%s'", grpname);
      break;
    }

    if (chown((sock_file != NULL) ? sock_file : US_DEFAULT_PATH, (uid_t)-1,
              g->gr_gid) != 0) {
      WARNING("unixsock plugin: chown (%s, -1, %i) failed: %s",
              (sock_file != NULL) ? sock_file : US_DEFAULT_PATH, (int)g->gr_gid,
              STRERRNO);
    }
  } while (0);

  return 0;
} /* int us_open_socket */

static void *us_handle_client(void *arg) {
  int fdin;
  int fdout;
  FILE *fhin, *fhout;

  fdin = *((int *)arg);
  free(arg);
  arg = NULL;

  DEBUG("unixsock plugin: us_handle_client: Reading from fd #%i", fdin);

  fdout = dup(fdin);
  if (fdout < 0) {
    ERROR("unixsock plugin: dup failed: %s", STRERRNO);
    close(fdin);
    pthread_exit((void *)1);
  }

  fhin = fdopen(fdin, "r");
  if (fhin == NULL) {
    ERROR("unixsock plugin: fdopen failed: %s", STRERRNO);
    close(fdin);
    close(fdout);
    pthread_exit((void *)1);
    return (void *)1;
  }

  fhout = fdopen(fdout, "w");
  if (fhout == NULL) {
    ERROR("unixsock plugin: fdopen failed: %s", STRERRNO);
    fclose(fhin); /* this closes fdin as well */
    close(fdout);
    pthread_exit((void *)1);
    return (void *)1;
  }

  /* change output buffer to line buffered mode */
  if (setvbuf(fhout, NULL, _IOLBF, 0) != 0) {
    ERROR("unixsock plugin: setvbuf failed: %s", STRERRNO);
    fclose(fhin);
    fclose(fhout);
    pthread_exit((void *)1);
    return (void *)0;
  }

  while (42) {
    char buffer[1024];
    char buffer_copy[1024];
    char *fields[128];
    int fields_num;

    errno = 0;
    if (fgets(buffer, sizeof(buffer), fhin) == NULL) {
      if ((errno == EINTR) || (errno == EAGAIN))
        continue;

      if (errno != 0) {
        WARNING("unixsock plugin: failed to read from socket #%i: %s",
                fileno(fhin), STRERRNO);
      }
      break;
    }

    size_t len = strlen(buffer);
    while ((len > 0) &&
           ((buffer[len - 1] == '\n') || (buffer[len - 1] == '\r')))
      buffer[--len] = '\0';

    if (len == 0)
      continue;

    sstrncpy(buffer_copy, buffer, sizeof(buffer_copy));

    fields_num =
        strsplit(buffer_copy, fields, sizeof(fields) / sizeof(fields[0]));
    if (fields_num < 1) {
      fprintf(fhout, "-1 Internal error\n");
      fclose(fhin);
      fclose(fhout);
      pthread_exit((void *)1);
      return (void *)1;
    }

    if (strcasecmp(fields[0], "getval") == 0) {
      cmd_handle_getval(fhout, buffer);
    } else if (strcasecmp(fields[0], "getthreshold") == 0) {
      handle_getthreshold(fhout, buffer);
    } else if (strcasecmp(fields[0], "putval") == 0) {
      cmd_handle_putval(fhout, buffer);
    } else if (strcasecmp(fields[0], "listval") == 0) {
      cmd_handle_listval(fhout, buffer);
    } else if (strcasecmp(fields[0], "putnotif") == 0) {
      handle_putnotif(fhout, buffer);
    } else if (strcasecmp(fields[0], "flush") == 0) {
      cmd_handle_flush(fhout, buffer);
    } else {
      if (fprintf(fhout, "-1 Unknown command: %s\n", fields[0]) < 0) {
        WARNING("unixsock plugin: failed to write to socket #%i: %s",
                fileno(fhout), STRERRNO);
        break;
      }
    }
  } /* while (fgets) */

  DEBUG("unixsock plugin: us_handle_client: Exiting..");
  fclose(fhin);
  fclose(fhout);

  pthread_exit((void *)0);
  return (void *)0;
} /* void *us_handle_client */

static void *us_server_thread(void __attribute__((unused)) * arg) {
  int status;
  int *remote_fd;
  pthread_t th;

  if (us_open_socket() != 0)
    pthread_exit((void *)1);

  while (loop != 0) {
    DEBUG("unixsock plugin: Calling accept..");
    status = accept(sock_fd, NULL, NULL);
    if (status < 0) {

      if (errno == EINTR)
        continue;

      ERROR("unixsock plugin: accept failed: %s", STRERRNO);
      close(sock_fd);
      sock_fd = -1;
      pthread_exit((void *)1);
    }

    remote_fd = malloc(sizeof(*remote_fd));
    if (remote_fd == NULL) {
      WARNING("unixsock plugin: malloc failed: %s", STRERRNO);
      close(status);
      continue;
    }
    *remote_fd = status;

    DEBUG("Spawning child to handle connection on fd #%i", *remote_fd);

    status = plugin_thread_create(&th, us_handle_client, (void *)remote_fd,
                                  "unixsock conn");
    if (status == 0) {
      pthread_detach(th);
    } else {
      WARNING("unixsock plugin: pthread_create failed: %s", STRERRNO);
      close(*remote_fd);
      free(remote_fd);
      continue;
    }
  } /* while (loop) */

  close(sock_fd);
  sock_fd = -1;

  status = unlink((sock_file != NULL) ? sock_file : US_DEFAULT_PATH);
  if (status != 0) {
    NOTICE("unixsock plugin: unlink (%s) failed: %s",
           (sock_file != NULL) ? sock_file : US_DEFAULT_PATH, STRERRNO);
  }

  return (void *)0;
} /* void *us_server_thread */

static int us_config(const char *key, const char *val) {
  if (strcasecmp(key, "SocketFile") == 0) {
    char *new_sock_file = strdup(val);
    if (new_sock_file == NULL)
      return 1;

    sfree(sock_file);
    sock_file = new_sock_file;
  } else if (strcasecmp(key, "SocketGroup") == 0) {
    char *new_sock_group = strdup(val);
    if (new_sock_group == NULL)
      return 1;

    sfree(sock_group);
    sock_group = new_sock_group;
  } else if (strcasecmp(key, "SocketPerms") == 0) {
    sock_perms = (int)strtol(val, NULL, 8);
  } else if (strcasecmp(key, "DeleteSocket") == 0) {
    if (IS_TRUE(val))
      delete_socket = true;
    else
      delete_socket = false;
  } else {
    return -1;
  }

  return 0;
} /* int us_config */

static int us_init(void) {
  static int have_init;

  int status;

  /* Initialize only once. */
  if (have_init != 0)
    return 0;
  have_init = 1;

  loop = 1;

  status = plugin_thread_create(&listen_thread, us_server_thread, NULL,
                                "unixsock listen");
  if (status != 0) {
    ERROR("unixsock plugin: pthread_create failed: %s", STRERRNO);
    return -1;
  }

  return 0;
} /* int us_init */

static int us_shutdown(void) {
  void *ret;

  loop = 0;

  if (listen_thread != (pthread_t)0) {
    pthread_kill(listen_thread, SIGTERM);
    pthread_join(listen_thread, &ret);
    listen_thread = (pthread_t)0;
  }

  plugin_unregister_init("unixsock");
  plugin_unregister_shutdown("unixsock");

  return 0;
} /* int us_shutdown */

void module_register(void) {
  plugin_register_config("unixsock", us_config, config_keys, config_keys_num);
  plugin_register_init("unixsock", us_init);
  plugin_register_shutdown("unixsock", us_shutdown);
} /* void module_register (void) */
