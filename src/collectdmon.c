/**
 * collectd - src/collectdmon.c
 * Copyright (C) 2007       Sebastian Harl
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
 *   Sebastian Harl <sh at tokkee.org>
 **/

#if !defined(__GNUC__) || !__GNUC__
#define __attribute__(x) /**/
#endif

#include "config.h"

#include <assert.h>

#include <errno.h>

#include <fcntl.h>

#include <signal.h>

#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include <syslog.h>

#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <time.h>

#include <unistd.h>

#ifndef PREFIX
#define PREFIX "/opt/" PACKAGE_NAME
#endif

#ifndef LOCALSTATEDIR
#define LOCALSTATEDIR PREFIX "/var"
#endif

#ifndef COLLECTDMON_PIDFILE
#define COLLECTDMON_PIDFILE LOCALSTATEDIR "/run/collectdmon.pid"
#endif /* ! COLLECTDMON_PIDFILE */

#ifndef WCOREDUMP
#define WCOREDUMP(s) 0
#endif /* ! WCOREDUMP */

static int loop;
static int quit;
static int restart;

static const char *pidfile;
static pid_t collectd_pid;

__attribute__((noreturn)) static void exit_usage(const char *name) {
  printf("Usage: %s <options> [-- <collectd options>]\n"

         "\nAvailable options:\n"
         "  -h         Display this help and exit.\n"
         "  -c <path>  Path to the collectd binary.\n"
         "  -P <file>  PID-file.\n"

         "\nFor <collectd options> see collectd.conf(5).\n"

         "\n" PACKAGE_NAME " " PACKAGE_VERSION ", http://collectd.org/\n"
         "by Florian octo Forster <octo@collectd.org>\n"
         "for contributions see `AUTHORS'\n",
         name);
  exit(0);
} /* exit_usage */

static int pidfile_create(void) {
  FILE *file;

  if (pidfile == NULL)
    pidfile = COLLECTDMON_PIDFILE;

  if ((file = fopen(pidfile, "w")) == NULL) {
    syslog(LOG_ERR, "Error: couldn't open PID-file (%s) for writing: %s",
           pidfile, strerror(errno));
    return -1;
  }

  fprintf(file, "%d\n", (int)getpid());
  fclose(file);
  return 0;
} /* pidfile_create */

static int pidfile_delete(void) {
  assert(pidfile);

  if (unlink(pidfile) != 0) {
    syslog(LOG_ERR, "Error: couldn't delete PID-file (%s): %s", pidfile,
           strerror(errno));
    return -1;
  }
  return 0;
} /* pidfile_remove */

static int daemonize(void) {
  if (chdir("/") != 0) {
    fprintf(stderr, "Error: chdir() failed: %s\n", strerror(errno));
    return -1;
  }

  struct rlimit rl;
  if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
    fprintf(stderr, "Error: getrlimit() failed: %s\n", strerror(errno));
    return -1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    fprintf(stderr, "Error: fork() failed: %s\n", strerror(errno));
    return -1;
  } else if (pid != 0) {
    exit(0);
  }

  if (pidfile_create() != 0)
    return -1;

  setsid();

  if (rl.rlim_max == RLIM_INFINITY)
    rl.rlim_max = 1024;

  for (int i = 0; i < (int)rl.rlim_max; ++i)
    close(i);

  int dev_null = open("/dev/null", O_RDWR);
  if (dev_null == -1) {
    syslog(LOG_ERR, "Error: couldn't open /dev/null: %s", strerror(errno));
    return -1;
  }

  if (dup2(dev_null, STDIN_FILENO) == -1) {
    close(dev_null);
    syslog(LOG_ERR, "Error: couldn't connect STDIN to /dev/null: %s",
           strerror(errno));
    return -1;
  }

  if (dup2(dev_null, STDOUT_FILENO) == -1) {
    close(dev_null);
    syslog(LOG_ERR, "Error: couldn't connect STDOUT to /dev/null: %s",
           strerror(errno));
    return -1;
  }

  if (dup2(dev_null, STDERR_FILENO) == -1) {
    close(dev_null);
    syslog(LOG_ERR, "Error: couldn't connect STDERR to /dev/null: %s",
           strerror(errno));
    return -1;
  }

  if ((dev_null != STDIN_FILENO) && (dev_null != STDOUT_FILENO) &&
      (dev_null != STDERR_FILENO))
    close(dev_null);

  return 0;
} /* daemonize */

static int collectd_start(char **argv) {
  pid_t pid = fork();

  if (pid < 0) {
    syslog(LOG_ERR, "Error: fork() failed: %s", strerror(errno));
    return -1;
  } else if (pid != 0) {
    collectd_pid = pid;
    return 0;
  }

  execvp(argv[0], argv);
  syslog(LOG_ERR, "Error: execvp(%s) failed: %s", argv[0], strerror(errno));
  exit(-1);
} /* collectd_start */

static int collectd_stop(int signo) {
  if (collectd_pid == 0)
    return 0;

  if (kill(collectd_pid, signo) != 0) {
    syslog(LOG_ERR, "Error: kill() failed: %s", strerror(errno));
    return -1;
  }
  return 0;
} /* collectd_stop */

static void sig_int_term_handler(int __attribute__((unused)) signo) {
  ++loop;
  return;
} /* sig_int_term_handler */

static void sig_hup_handler(int __attribute__((unused)) signo) {
  ++restart;
  return;
} /* sig_hup_handler */

static void sig_quit_handler(int __attribute__((unused)) signo) {
  ++quit;
  ++loop;
  return;
} /* sig_quit_handler */

static void log_status(int status) {
  if (WIFEXITED(status)) {
    if (WEXITSTATUS(status) == 0)
      syslog(LOG_INFO, "Info: collectd terminated with exit status %i",
             WEXITSTATUS(status));
    else
      syslog(LOG_WARNING, "Warning: collectd terminated with exit status %i",
             WEXITSTATUS(status));
  } else if (WIFSIGNALED(status)) {
    syslog(LOG_WARNING, "Warning: collectd was terminated by signal %i%s",
           WTERMSIG(status), WCOREDUMP(status) ? " (core dumped)" : "");
  }
  return;
} /* log_status */

static void check_respawn(void) {
  time_t t = time(NULL);

  static time_t timestamp;
  static int counter;

  if (timestamp >= t - 120)
    ++counter;
  else {
    timestamp = t;
    counter = 0;
  }

  if (counter >= 10) {
    unsigned int time_left = 300;

    syslog(LOG_ERR, "Error: collectd is respawning too fast - "
                    "disabled for %i seconds",
           time_left);

    while (((time_left = sleep(time_left)) > 0) && loop == 0)
      ;
  }
  return;
} /* check_respawn */

int main(int argc, char **argv) {
  int collectd_argc = 0;
  char *collectd = NULL;
  char **collectd_argv = NULL;

  int i = 0;

  /* parse command line options */
  while (42) {
    int c = getopt(argc, argv, "hc:P:");

    if (c == -1)
      break;

    switch (c) {
    case 'c':
      collectd = optarg;
      break;
    case 'P':
      pidfile = optarg;
      break;
    case 'h':
    default:
      exit_usage(argv[0]);
    }
  }

  for (i = optind; i < argc; ++i)
    if (strcmp(argv[i], "-f") == 0)
      break;

  /* i < argc => -f already present */
  collectd_argc = 1 + argc - optind + ((i < argc) ? 0 : 1);
  collectd_argv = calloc(collectd_argc + 1, sizeof(*collectd_argv));

  if (collectd_argv == NULL) {
    fprintf(stderr, "Out of memory.");
    return 3;
  }

  collectd_argv[0] = (collectd == NULL) ? "collectd" : collectd;

  if (i == argc)
    collectd_argv[collectd_argc - 1] = "-f";

  for (i = optind; i < argc; ++i)
    collectd_argv[i - optind + 1] = argv[i];

  collectd_argv[collectd_argc] = NULL;

  openlog("collectdmon", LOG_CONS | LOG_PID, LOG_DAEMON);

  if (daemonize() == -1) {
    free(collectd_argv);
    return 1;
  }

  struct sigaction sa = {
      .sa_handler = sig_int_term_handler, .sa_flags = 0,
  };
  sigemptyset(&sa.sa_mask);

  if (sigaction(SIGINT, &sa, NULL) != 0) {
    syslog(LOG_ERR, "Error: sigaction() failed: %s", strerror(errno));
    free(collectd_argv);
    return 1;
  }

  if (sigaction(SIGTERM, &sa, NULL) != 0) {
    syslog(LOG_ERR, "Error: sigaction() failed: %s", strerror(errno));
    free(collectd_argv);
    return 1;
  }

  sa.sa_handler = sig_quit_handler;

  if (sigaction(SIGQUIT, &sa, NULL) != 0) {
    syslog(LOG_ERR, "Error: sigaction() failed: %s", strerror(errno));
    free(collectd_argv);
    return 1;
  }

  sa.sa_handler = sig_hup_handler;

  if (sigaction(SIGHUP, &sa, NULL) != 0) {
    syslog(LOG_ERR, "Error: sigaction() failed: %s", strerror(errno));
    free(collectd_argv);
    return 1;
  }

  while (loop == 0) {
    int status = 0;

    if (collectd_start(collectd_argv) != 0) {
      syslog(LOG_ERR, "Error: failed to start collectd.");
      break;
    }

    assert(collectd_pid >= 0);
    while ((collectd_pid != waitpid(collectd_pid, &status, 0)) &&
           errno == EINTR)
      if (loop != 0 || restart != 0) {
        if (quit)
          collectd_stop(SIGKILL);
        else
          collectd_stop(SIGTERM);
      }

    collectd_pid = 0;

    log_status(status);
    check_respawn();

    if (restart != 0) {
      syslog(LOG_INFO, "Info: restarting collectd");
      restart = 0;
    } else if (loop == 0)
      syslog(LOG_WARNING, "Warning: restarting collectd");
  }

  syslog(LOG_INFO, "Info: shutting down collectdmon");

  pidfile_delete();
  closelog();

  free(collectd_argv);
  return 0;
} /* main */
