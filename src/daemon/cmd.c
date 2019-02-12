/**
 * collectd - src/daemon/cmd.c
 * Copyright (C) 2005-2007  Florian octo Forster
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
 **/

#include "cmd.h"
#include "collectd.h"

#include "utils/common/common.h"
#include <sys/un.h>

static void *do_flush(void __attribute__((unused)) * arg) {
  INFO("Flushing all data.");
  plugin_flush(/* plugin = */ NULL,
               /* timeout = */ 0,
               /* ident = */ NULL);
  INFO("Finished flushing all data.");
  pthread_exit(NULL);
  return NULL;
}

static void sig_int_handler(int __attribute__((unused)) signal) {
  stop_collectd();
}

static void sig_term_handler(int __attribute__((unused)) signal) {
  stop_collectd();
}

static void sig_usr1_handler(int __attribute__((unused)) signal) {
  pthread_t thread;
  pthread_attr_t attr;

  /* flushing the data might take a while,
   * so it should be done asynchronously */
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&thread, &attr, do_flush, NULL);
  pthread_attr_destroy(&attr);
}

#if COLLECT_DAEMON
static int pidfile_create(void) {
  FILE *fh;
  const char *file = global_option_get("PIDFile");

  if ((fh = fopen(file, "w")) == NULL) {
    ERROR("fopen (%s): %s", file, STRERRNO);
    return 1;
  }

  fprintf(fh, "%i\n", (int)getpid());
  fclose(fh);

  return 0;
} /* static int pidfile_create (const char *file) */

static int pidfile_remove(void) {
  const char *file = global_option_get("PIDFile");
  if (file == NULL)
    return 0;

  return unlink(file);
} /* static int pidfile_remove (const char *file) */
#endif /* COLLECT_DAEMON */

#ifdef KERNEL_LINUX
static int notify_upstart(void) {
  char const *upstart_job = getenv("UPSTART_JOB");

  if (upstart_job == NULL)
    return 0;

  if (strcmp(upstart_job, "collectd") != 0) {
    WARNING("Environment specifies unexpected UPSTART_JOB=\"%s\", expected "
            "\"collectd\". Ignoring the variable.",
            upstart_job);
    return 0;
  }

  NOTICE("Upstart detected, stopping now to signal readiness.");
  raise(SIGSTOP);
  unsetenv("UPSTART_JOB");

  return 1;
}

static int notify_systemd(void) {
  size_t su_size;
  const char *notifysocket = getenv("NOTIFY_SOCKET");
  if (notifysocket == NULL)
    return 0;

  if ((strlen(notifysocket) < 2) ||
      ((notifysocket[0] != '@') && (notifysocket[0] != '/'))) {
    ERROR("invalid notification socket NOTIFY_SOCKET=\"%s\": path must be "
          "absolute",
          notifysocket);
    return 0;
  }
  NOTICE("Systemd detected, trying to signal readiness.");

  unsetenv("NOTIFY_SOCKET");

  int fd;
#if defined(SOCK_CLOEXEC)
  fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, /* protocol = */ 0);
#else
  fd = socket(AF_UNIX, SOCK_DGRAM, /* protocol = */ 0);
#endif
  if (fd < 0) {
    ERROR("creating UNIX socket failed: %s", STRERRNO);
    return 0;
  }

  struct sockaddr_un su = {0};
  su.sun_family = AF_UNIX;
  if (notifysocket[0] != '@') {
    /* regular UNIX socket */
    sstrncpy(su.sun_path, notifysocket, sizeof(su.sun_path));
    su_size = sizeof(su);
  } else {
    /* Linux abstract namespace socket: specify address as "\0foo", i.e.
     * start with a null byte. Since null bytes have no special meaning in
     * that case, we have to set su_size correctly to cover only the bytes
     * that are part of the address. */
    sstrncpy(su.sun_path, notifysocket, sizeof(su.sun_path));
    su.sun_path[0] = 0;
    su_size = sizeof(sa_family_t) + strlen(notifysocket);
    if (su_size > sizeof(su))
      su_size = sizeof(su);
  }

  const char buffer[] = "READY=1\n";
  if (sendto(fd, buffer, strlen(buffer), MSG_NOSIGNAL, (void *)&su,
             (socklen_t)su_size) < 0) {
    ERROR("sendto(\"%s\") failed: %s", notifysocket, STRERRNO);
    close(fd);
    return 0;
  }

  unsetenv("NOTIFY_SOCKET");
  close(fd);
  return 1;
}
#endif /* KERNEL_LINUX */

int main(int argc, char **argv) {
  struct cmdline_config config = init_config(argc, argv);

#if COLLECT_DAEMON
  /*
   * fork off child
   */
  struct sigaction sig_chld_action = {.sa_handler = SIG_IGN};

  sigaction(SIGCHLD, &sig_chld_action, NULL);

  /*
   * Only daemonize if we're not being supervised
   * by upstart or systemd (when using Linux).
   */
  if (config.daemonize
#ifdef KERNEL_LINUX
      && notify_upstart() == 0 && notify_systemd() == 0
#endif
      ) {
    pid_t pid;
    if ((pid = fork()) == -1) {
      /* error */
      fprintf(stderr, "fork: %s", STRERRNO);
      return 1;
    } else if (pid != 0) {
      /* parent */
      /* printf ("Running (PID %i)\n", pid); */
      return 0;
    }

    /* Detach from session */
    setsid();

    /* Write pidfile */
    if (pidfile_create())
      exit(2);

    /* close standard descriptors */
    close(2);
    close(1);
    close(0);

    int status = open("/dev/null", O_RDWR);
    if (status != 0) {
      ERROR("Error: Could not connect `STDIN' to `/dev/null' (status %d)",
            status);
      return 1;
    }

    status = dup(0);
    if (status != 1) {
      ERROR("Error: Could not connect `STDOUT' to `/dev/null' (status %d)",
            status);
      return 1;
    }

    status = dup(0);
    if (status != 2) {
      ERROR("Error: Could not connect `STDERR' to `/dev/null', (status %d)",
            status);
      return 1;
    }
  }    /* if (config.daemonize) */
#endif /* COLLECT_DAEMON */

  struct sigaction sig_pipe_action = {.sa_handler = SIG_IGN};

  sigaction(SIGPIPE, &sig_pipe_action, NULL);

  /*
   * install signal handlers
   */
  struct sigaction sig_int_action = {.sa_handler = sig_int_handler};

  if (sigaction(SIGINT, &sig_int_action, NULL) != 0) {
    ERROR("Error: Failed to install a signal handler for signal INT: %s",
          STRERRNO);
    return 1;
  }

  struct sigaction sig_term_action = {.sa_handler = sig_term_handler};

  if (sigaction(SIGTERM, &sig_term_action, NULL) != 0) {
    ERROR("Error: Failed to install a signal handler for signal TERM: %s",
          STRERRNO);
    return 1;
  }

  struct sigaction sig_usr1_action = {.sa_handler = sig_usr1_handler};

  if (sigaction(SIGUSR1, &sig_usr1_action, NULL) != 0) {
    ERROR("Error: Failed to install a signal handler for signal USR1: %s",
          STRERRNO);
    return 1;
  }

  int exit_status = run_loop(config.test_readall);

#if COLLECT_DAEMON
  if (config.daemonize)
    pidfile_remove();
#endif /* COLLECT_DAEMON */

  return exit_status;
} /* int main */
