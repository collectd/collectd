/**
 * collectd - src/exec.c
 * Copyright (C) 2007,2008  Florian octo Forster
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

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include "utils_cmd_putval.h"
#include "utils_cmd_putnotif.h"

#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>

#include <pthread.h>

#define PL_NORMAL        0x01
#define PL_NOTIF_ACTION  0x02

#define PL_RUNNING       0x10

/*
 * Private data types
 */
/*
 * Access to this structure is serialized using the `pl_lock' lock and the
 * `PL_RUNNING' flag. The execution of notifications is *not* serialized, so
 * all functions used to handle notifications MUST NOT write to this structure.
 * The `pid' and `status' fields are thus unused if the `PL_NOTIF_ACTION' flag
 * is set.
 * The `PL_RUNNING' flag is set in `exec_read' and unset in `exec_read_one'.
 */
struct program_list_s;
typedef struct program_list_s program_list_t;
struct program_list_s
{
  char           *user;
  char           *group;
  char           *exec;
  char          **argv;
  int             pid;
  int             status;
  int             flags;
  program_list_t *next;
};

typedef struct program_list_and_notification_s
{
  program_list_t *pl;
  notification_t n;
} program_list_and_notification_t;

/*
 * Private variables
 */
static program_list_t *pl_head = NULL;
static pthread_mutex_t pl_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Functions
 */
static void sigchld_handler (int signal) /* {{{ */
{
  pid_t pid;
  int status;
  while ((pid = waitpid (-1, &status, WNOHANG)) > 0)
  {
    program_list_t *pl;
    for (pl = pl_head; pl != NULL; pl = pl->next)
      if (pl->pid == pid)
	break;
    if (pl != NULL)
      pl->status = status;
  } /* while (waitpid) */
} /* void sigchld_handler }}} */

static int exec_config_exec (oconfig_item_t *ci) /* {{{ */
{
  program_list_t *pl;
  char buffer[128];
  int i;

  if (ci->children_num != 0)
  {
    WARNING ("exec plugin: The config option `%s' may not be a block.",
	ci->key);
    return (-1);
  }
  if (ci->values_num < 2)
  {
    WARNING ("exec plugin: The config option `%s' needs at least two "
	"arguments.", ci->key);
    return (-1);
  }
  if ((ci->values[0].type != OCONFIG_TYPE_STRING)
      || (ci->values[1].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("exec plugin: The first two arguments to the `%s' option must "
	"be string arguments.", ci->key);
    return (-1);
  }

  pl = (program_list_t *) malloc (sizeof (program_list_t));
  if (pl == NULL)
  {
    ERROR ("exec plugin: malloc failed.");
    return (-1);
  }
  memset (pl, '\0', sizeof (program_list_t));

  if (strcasecmp ("NotificationExec", ci->key) == 0)
    pl->flags |= PL_NOTIF_ACTION;
  else
    pl->flags |= PL_NORMAL;

  pl->user = strdup (ci->values[0].value.string);
  if (pl->user == NULL)
  {
    ERROR ("exec plugin: strdup failed.");
    sfree (pl);
    return (-1);
  }

  pl->group = strchr (pl->user, ':');
  if (pl->group != NULL)
  {
    *pl->group = '\0';
    pl->group++;
  }

  pl->exec = strdup (ci->values[1].value.string);
  if (pl->exec == NULL)
  {
    ERROR ("exec plugin: strdup failed.");
    sfree (pl->user);
    sfree (pl);
    return (-1);
  }

  pl->argv = (char **) malloc (ci->values_num * sizeof (char *));
  if (pl->argv == NULL)
  {
    ERROR ("exec plugin: malloc failed.");
    sfree (pl->exec);
    sfree (pl->user);
    sfree (pl);
    return (-1);
  }
  memset (pl->argv, '\0', ci->values_num * sizeof (char *));

  {
    char *tmp = strrchr (ci->values[1].value.string, '/');
    if (tmp == NULL)
      strncpy (buffer, ci->values[1].value.string, sizeof (buffer));
    else
      strncpy (buffer, tmp + 1, sizeof (buffer));
    buffer[sizeof (buffer) - 1] = '\0';
  }
  pl->argv[0] = strdup (buffer);
  if (pl->argv[0] == NULL)
  {
    ERROR ("exec plugin: malloc failed.");
    sfree (pl->argv);
    sfree (pl->exec);
    sfree (pl->user);
    sfree (pl);
    return (-1);
  }

  for (i = 1; i < (ci->values_num - 1); i++)
  {
    if (ci->values[i + 1].type == OCONFIG_TYPE_STRING)
    {
      pl->argv[i] = strdup (ci->values[i + 1].value.string);
    }
    else
    {
      if (ci->values[i + 1].type == OCONFIG_TYPE_NUMBER)
      {
	snprintf (buffer, sizeof (buffer), "%lf",
	    ci->values[i + 1].value.number);
      }
      else
      {
	if (ci->values[i + 1].value.boolean)
	  strncpy (buffer, "true", sizeof (buffer));
	else
	  strncpy (buffer, "false", sizeof (buffer));
      }
      buffer[sizeof (buffer) - 1] = '\0';

      pl->argv[i] = strdup (buffer);
    }

    if (pl->argv[i] == NULL)
    {
      ERROR ("exec plugin: strdup failed.");
      break;
    }
  } /* for (i) */

  if (i < (ci->values_num - 1))
  {
    while ((--i) >= 0)
    {
      sfree (pl->argv[i]);
    }
    sfree (pl->argv);
    sfree (pl->exec);
    sfree (pl->user);
    sfree (pl);
    return (-1);
  }

  for (i = 0; pl->argv[i] != NULL; i++)
  {
    DEBUG ("exec plugin: argv[%i] = %s", i, pl->argv[i]);
  }

  pl->next = pl_head;
  pl_head = pl;

  return (0);
} /* int exec_config_exec }}} */

static int exec_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;
    if ((strcasecmp ("Exec", child->key) == 0)
	|| (strcasecmp ("NotificationExec", child->key) == 0))
      exec_config_exec (child);
    else
    {
      WARNING ("exec plugin: Unknown config option `%s'.", child->key);
    }
  } /* for (i) */

  return (0);
} /* int exec_config }}} */

static void exec_child (program_list_t *pl) /* {{{ */
{
  int status;
  int uid;
  int gid;
  int egid;

  struct passwd *sp_ptr;
  struct passwd sp;
  char nambuf[2048];
  char errbuf[1024];

  sp_ptr = NULL;
  status = getpwnam_r (pl->user, &sp, nambuf, sizeof (nambuf), &sp_ptr);
  if (status != 0)
  {
    ERROR ("exec plugin: getpwnam_r failed: %s",
	sstrerror (errno, errbuf, sizeof (errbuf)));
    exit (-1);
  }
  if (sp_ptr == NULL)
  {
    ERROR ("exec plugin: No such user: `%s'", pl->user);
    exit (-1);
  }

  uid = sp.pw_uid;
  gid = sp.pw_gid;
  if (uid == 0)
  {
    ERROR ("exec plugin: Cowardly refusing to exec program as root.");
    exit (-1);
  }

  /* The group configured in the configfile is set as effective group, because
   * this way the forked process can (re-)gain the user's primary group. */
  egid = -1;
  if (NULL != pl->group)
  {
    if ('\0' != *pl->group) {
      struct group *gr_ptr = NULL;
      struct group gr;

      status = getgrnam_r (pl->group, &gr, nambuf, sizeof (nambuf), &gr_ptr);
      if (0 != status)
      {
	ERROR ("exec plugin: getgrnam_r failed: %s",
	    sstrerror (errno, errbuf, sizeof (errbuf)));
	exit (-1);
      }
      if (NULL == gr_ptr)
      {
	ERROR ("exec plugin: No such group: `%s'", pl->group);
	exit (-1);
      }

      egid = gr.gr_gid;
    }
    else
    {
      egid = gid;
    }
  } /* if (pl->group == NULL) */

#if HAVE_SETGROUPS
  if (getuid () == 0)
  {
    gid_t  glist[2];
    size_t glist_len;

    glist[0] = gid;
    glist_len = 1;

    if (gid != egid)
    {
      glist[1] = egid;
      glist_len = 2;
    }

    setgroups (glist_len, glist);
  }
#endif /* HAVE_SETGROUPS */

  status = setgid (gid);
  if (status != 0)
  {
    ERROR ("exec plugin: setgid (%i) failed: %s",
	gid, sstrerror (errno, errbuf, sizeof (errbuf)));
    exit (-1);
  }

  if (egid != -1)
  {
    status = setegid (egid);
    if (status != 0)
    {
      ERROR ("exec plugin: setegid (%i) failed: %s",
	  egid, sstrerror (errno, errbuf, sizeof (errbuf)));
      exit (-1);
    }
  }

  status = setuid (uid);
  if (status != 0)
  {
    ERROR ("exec plugin: setuid (%i) failed: %s",
	uid, sstrerror (errno, errbuf, sizeof (errbuf)));
    exit (-1);
  }

  status = execvp (pl->exec, pl->argv);

  ERROR ("exec plugin: exec failed: %s",
      sstrerror (errno, errbuf, sizeof (errbuf)));
  exit (-1);
} /* void exec_child }}} */

/*
 * Creates two pipes (one for reading, ong for writing), forks a child, sets up
 * the pipes so that fd_in is connected to STDIN of the child and fd_out is
 * connected to STDOUT and STDERR of the child. Then is calls `exec_child'.
 */
static int fork_child (program_list_t *pl, int *fd_in, int *fd_out) /* {{{ */
{
  int fd_pipe_in[2];
  int fd_pipe_out[2];
  int status;
  int pid;

  if (pl->pid != 0)
    return (-1);

  status = pipe (fd_pipe_in);
  if (status != 0)
  {
    char errbuf[1024];
    ERROR ("exec plugin: pipe failed: %s",
	sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  status = pipe (fd_pipe_out);
  if (status != 0)
  {
    char errbuf[1024];
    ERROR ("exec plugin: pipe failed: %s",
	sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  pid = fork ();
  if (pid < 0)
  {
    char errbuf[1024];
    ERROR ("exec plugin: fork failed: %s",
	sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }
  else if (pid == 0)
  {
    close (fd_pipe_in[1]);
    close (fd_pipe_out[0]);

    /* If the `out' pipe has the filedescriptor STDIN we have to be careful
     * with the `dup's below. So, if this is the case we have to handle the
     * `out' pipe first. */
    if (fd_pipe_out[1] == STDIN_FILENO)
    {
      int new_fileno = (fd_pipe_in[0] == STDOUT_FILENO)
	? STDERR_FILENO : STDOUT_FILENO;
      dup2 (fd_pipe_out[1], new_fileno);
      close (fd_pipe_out[1]);
      fd_pipe_out[1] = new_fileno;
    }
    /* Now `fd_pipe_out[1]' is either `STDOUT' or `STDERR', but definitely not
     * `STDIN_FILENO'. */

    /* Connect the `in' pipe to STDIN */
    if (fd_pipe_in[0] != STDIN_FILENO)
    {
      dup2 (fd_pipe_in[0], STDIN_FILENO);
      close (fd_pipe_in[0]);
      fd_pipe_in[0] = STDIN_FILENO;
    }

    /* Now connect the `out' pipe to STDOUT and STDERR */
    if (fd_pipe_out[1] != STDOUT_FILENO)
      dup2 (fd_pipe_out[1], STDOUT_FILENO);
    if (fd_pipe_out[1] != STDERR_FILENO)
      dup2 (fd_pipe_out[1], STDERR_FILENO);

    /* If the pipe has some FD that's something completely different, close it
     * now. */
    if ((fd_pipe_out[1] != STDOUT_FILENO) && (fd_pipe_out[1] != STDERR_FILENO))
    {
      close (fd_pipe_out[1]);
      fd_pipe_out[1] = STDOUT_FILENO;
    }

    exec_child (pl);
    /* does not return */
  }

  close (fd_pipe_in[0]);
  close (fd_pipe_out[1]);

  if (fd_in != NULL)
    *fd_in = fd_pipe_in[1];
  else
    close (fd_pipe_in[1]);

  if (fd_out != NULL)
    *fd_out = fd_pipe_out[0];
  else
    close (fd_pipe_out[0]);

  return (pid);
} /* int fork_child }}} */

static int parse_line (char *buffer) /* {{{ */
{
  char *fields[256];
  int fields_num;

  fields[0] = "PUTVAL";
  fields_num = strsplit (buffer, fields + 1, STATIC_ARRAY_SIZE(fields) - 1);

  if (strcasecmp (fields[1], "putval") == 0)
    return (handle_putval (stdout, fields + 1, fields_num));
  else if (strcasecmp (fields[1], "putnotif") == 0)
    return (handle_putnotif (stdout, fields + 1, fields_num));

  /* compatibility code */
  return (handle_putval (stdout, fields, fields_num + 1));
} /* int parse_line }}} */

static void *exec_read_one (void *arg) /* {{{ */
{
  program_list_t *pl = (program_list_t *) arg;
  int fd;
  FILE *fh;
  char buffer[1024];
  int status;

  status = fork_child (pl, NULL, &fd);
  if (status < 0)
    pthread_exit ((void *) 1);
  pl->pid = status;

  assert (pl->pid != 0);

  fh = fdopen (fd, "r");
  if (fh == NULL)
  {
    char errbuf[1024];
    ERROR ("exec plugin: fdopen (%i) failed: %s", fd,
	sstrerror (errno, errbuf, sizeof (errbuf)));
    kill (pl->pid, SIGTERM);
    pl->pid = 0;
    close (fd);
    pthread_exit ((void *) 1);
  }

  buffer[0] = '\0';
  while (fgets (buffer, sizeof (buffer), fh) != NULL)
  {
    int len;

    len = strlen (buffer);

    /* Remove newline from end. */
    while ((len > 0) && ((buffer[len - 1] == '\n')
	  || (buffer[len - 1] == '\r')))
      buffer[--len] = '\0';

    DEBUG ("exec plugin: exec_read_one: buffer = %s", buffer);

    parse_line (buffer);
  } /* while (fgets) */

  fclose (fh);

  if (waitpid (pl->pid, &status, 0) > 0)
    pl->status = status;

  DEBUG ("exec plugin: Child %i exited with status %i.",
      (int) pl->pid, pl->status);

  pl->pid = 0;

  pthread_mutex_lock (&pl_lock);
  pl->flags &= ~PL_RUNNING;
  pthread_mutex_unlock (&pl_lock);

  pthread_exit ((void *) 0);
  return (NULL);
} /* void *exec_read_one }}} */

static void *exec_notification_one (void *arg) /* {{{ */
{
  program_list_t *pl = ((program_list_and_notification_t *) arg)->pl;
  const notification_t *n = &((program_list_and_notification_t *) arg)->n;
  int fd;
  FILE *fh;
  int pid;
  int status;
  const char *severity;

  pid = fork_child (pl, &fd, NULL);
  if (pid < 0) {
    sfree (arg);
    pthread_exit ((void *) 1);
  }

  fh = fdopen (fd, "w");
  if (fh == NULL)
  {
    char errbuf[1024];
    ERROR ("exec plugin: fdopen (%i) failed: %s", fd,
	sstrerror (errno, errbuf, sizeof (errbuf)));
    kill (pl->pid, SIGTERM);
    pl->pid = 0;
    close (fd);
    sfree (arg);
    pthread_exit ((void *) 1);
  }

  severity = "FAILURE";
  if (n->severity == NOTIF_WARNING)
    severity = "WARNING";
  else if (n->severity == NOTIF_OKAY)
    severity = "OKAY";

  fprintf (fh,
      "Severity: %s\n"
      "Time: %u\n",
      severity, (unsigned int) n->time);

  /* Print the optional fields */
  if (strlen (n->host) > 0)
    fprintf (fh, "Host: %s\n", n->host);
  if (strlen (n->plugin) > 0)
    fprintf (fh, "Plugin: %s\n", n->plugin);
  if (strlen (n->plugin_instance) > 0)
    fprintf (fh, "PluginInstance: %s\n", n->plugin_instance);
  if (strlen (n->type) > 0)
    fprintf (fh, "Type: %s\n", n->type);
  if (strlen (n->type_instance) > 0)
    fprintf (fh, "TypeInstance: %s\n", n->type_instance);

  fprintf (fh, "\n%s\n", n->message);

  fflush (fh);
  fclose (fh);

  waitpid (pid, &status, 0);

  DEBUG ("exec plugin: Child %i exited with status %i.",
      pid, status);

  sfree (arg);
  pthread_exit ((void *) 0);
  return (NULL);
} /* void *exec_notification_one }}} */

static int exec_init (void) /* {{{ */
{
  struct sigaction sa;

  memset (&sa, '\0', sizeof (sa));
  sa.sa_handler = sigchld_handler;
  sigaction (SIGCHLD, &sa, NULL);

  return (0);
} /* int exec_init }}} */

static int exec_read (void) /* {{{ */
{
  program_list_t *pl;

  for (pl = pl_head; pl != NULL; pl = pl->next)
  {
    pthread_t t;
    pthread_attr_t attr;

    /* Only execute `normal' style executables here. */
    if ((pl->flags & PL_NORMAL) == 0)
      continue;

    pthread_mutex_lock (&pl_lock);
    /* Skip if a child is already running. */
    if ((pl->flags & PL_RUNNING) != 0)
    {
      pthread_mutex_unlock (&pl_lock);
      continue;
    }
    pl->flags |= PL_RUNNING;
    pthread_mutex_unlock (&pl_lock);

    pthread_attr_init (&attr);
    pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
    pthread_create (&t, &attr, exec_read_one, (void *) pl);
  } /* for (pl) */

  return (0);
} /* int exec_read }}} */

static int exec_notification (const notification_t *n)
{
  program_list_t *pl;
  program_list_and_notification_t *pln;

  for (pl = pl_head; pl != NULL; pl = pl->next)
  {
    pthread_t t;
    pthread_attr_t attr;

    /* Only execute `notification' style executables here. */
    if ((pl->flags & PL_NOTIF_ACTION) == 0)
      continue;

    /* Skip if a child is already running. */
    if (pl->pid != 0)
      continue;

    pln = (program_list_and_notification_t *) malloc (sizeof
	(program_list_and_notification_t));
    if (pln == NULL)
    {
      ERROR ("exec plugin: malloc failed.");
      continue;
    }

    pln->pl = pl;
    memcpy (&pln->n, n, sizeof (notification_t));

    pthread_attr_init (&attr);
    pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
    pthread_create (&t, &attr, exec_notification_one, (void *) pln);
  } /* for (pl) */

  return (0);
} /* int exec_notification */

static int exec_shutdown (void) /* {{{ */
{
  program_list_t *pl;
  program_list_t *next;

  pl = pl_head;
  while (pl != NULL)
  {
    next = pl->next;

    if (pl->pid > 0)
    {
      kill (pl->pid, SIGTERM);
      INFO ("exec plugin: Sent SIGTERM to %hu", (unsigned short int) pl->pid);
    }

    sfree (pl->user);
    sfree (pl);

    pl = next;
  } /* while (pl) */
  pl_head = NULL;

  return (0);
} /* int exec_shutdown }}} */

void module_register (void)
{
  plugin_register_complex_config ("exec", exec_config);
  plugin_register_init ("exec", exec_init);
  plugin_register_read ("exec", exec_read);
  plugin_register_notification ("exec", exec_notification);
  plugin_register_shutdown ("exec", exec_shutdown);
} /* void module_register */

/*
 * vim:shiftwidth=2:softtabstop=2:tabstop=8:fdm=marker
 */
