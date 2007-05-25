/**
 * collectd - src/exec.c
 * Copyright (C) 2007  Florian octo Forster
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

#include <sys/types.h>
#include <pwd.h>
#include <signal.h>

#include <pthread.h>

/*
 * Private data types
 */
struct program_list_s;
typedef struct program_list_s program_list_t;
struct program_list_s
{
  char           *user;
  char           *exec;
  int             pid;
  program_list_t *next;
};

/*
 * Private variables
 */
static const char *config_keys[] =
{
  "Exec"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static program_list_t *pl_head = NULL;

/*
 * Functions
 */
static int exec_config (const char *key, const char *value)
{
  if (strcasecmp ("Exec", key) == 0)
  {
    program_list_t *pl;
    pl = (program_list_t *) malloc (sizeof (program_list_t));
    if (pl == NULL)
      return (1);
    memset (pl, '\0', sizeof (program_list_t));

    pl->user = strdup (value);
    if (pl->user == NULL)
    {
      sfree (pl);
      return (1);
    }

    pl->exec = strchr (pl->user, ' ');
    if (pl->exec == NULL)
    {
      sfree (pl->user);
      sfree (pl);
      return (1);
    }
    while (*pl->exec == ' ')
    {
      *pl->exec = '\0';
      pl->exec++;
    }

    if (*pl->exec == '\0')
    {
      sfree (pl->user);
      sfree (pl);
      return (1);
    }

    pl->next = pl_head;
    pl_head = pl;
  }
  else
  {
    return (-1);
  }

  return (0);
} /* int exec_config */

static void submit_counter (const char *type_instance, counter_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  DEBUG ("type_instance = %s; value = %llu;", type_instance, value);

  values[0].counter = value;

  vl.values = values;
  vl.values_len = 1;
  vl.time = time (NULL);
  strcpy (vl.host, hostname_g);
  strcpy (vl.plugin, "exec");
  strcpy (vl.plugin_instance, "");
  strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  plugin_dispatch_values ("counter", &vl);
} /* void submit_counter */

static void submit_gauge (const char *type_instance, gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  DEBUG ("type_instance = %s; value = %lf;", type_instance, value);

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;
  vl.time = time (NULL);
  strcpy (vl.host, hostname_g);
  strcpy (vl.plugin, "exec");
  strcpy (vl.plugin_instance, "");
  strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  plugin_dispatch_values ("gauge", &vl);
} /* void submit_counter */

static void exec_child (program_list_t *pl)
{
  int status;
  int uid;
  char *arg0;

  struct passwd *sp_ptr;
  struct passwd sp;
  char pwnambuf[2048];
  char errbuf[1024];

  sp_ptr = NULL;
  status = getpwnam_r (pl->user, &sp, pwnambuf, sizeof (pwnambuf), &sp_ptr);
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
  if (uid == 0)
  {
    ERROR ("exec plugin: Cowardly refusing to exec program as root.");
    exit (-1);
  }

  status = setuid (uid);
  if (status != 0)
  {
    ERROR ("exec plugin: setuid failed: %s",
	sstrerror (errno, errbuf, sizeof (errbuf)));
    exit (-1);
  }

  arg0 = strrchr (pl->exec, '/');
  if (arg0 != NULL)
    arg0++;
  if ((arg0 == NULL) || (*arg0 == '\0'))
    arg0 = pl->exec;

  status = execlp (pl->exec, arg0, (char *) 0);

  ERROR ("exec plugin: exec failed: %s",
      sstrerror (errno, errbuf, sizeof (errbuf)));
  exit (-1);
} /* void exec_child */

static int fork_child (program_list_t *pl)
{
  int fd_pipe[2];
  int status;

  if (pl->pid != 0)
    return (-1);

  status = pipe (fd_pipe);
  if (status != 0)
  {
    char errbuf[1024];
    ERROR ("exec plugin: pipe failed: %s",
	sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  pl->pid = fork ();
  if (pl->pid < 0)
  {
    char errbuf[1024];
    ERROR ("exec plugin: fork failed: %s",
	sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }
  else if (pl->pid == 0)
  {
    close (fd_pipe[0]);

    /* Connect the pipe to STDOUT and STDERR */
    if (fd_pipe[1] != STDOUT_FILENO)
      dup2 (fd_pipe[1], STDOUT_FILENO);
    if (fd_pipe[1] != STDERR_FILENO)
      dup2 (fd_pipe[1], STDERR_FILENO);
    if ((fd_pipe[1] != STDOUT_FILENO) && (fd_pipe[1] != STDERR_FILENO))
      close (fd_pipe[1]);

    exec_child (pl);
    /* does not return */
  }

  close (fd_pipe[1]);
  return (fd_pipe[0]);
} /* int fork_child */

static void *exec_read_one (void *arg)
{
  program_list_t *pl = (program_list_t *) arg;
  int fd;
  FILE *fh;
  char buffer[1024];

  fd = fork_child (pl);
  if (fd < 0)
    pthread_exit ((void *) 1);

  assert (pl->pid != 0);

  fh = fdopen (fd, "r");
  if (fh == NULL)
  {
    char errbuf[1024];
    ERROR ("exec plugin: fdopen (%i) failed: %s", fd,
	sstrerror (errno, errbuf, sizeof (errbuf)));
    kill (pl->pid, SIGTERM);
    close (fd);
    pthread_exit ((void *) 1);
  }

  while (fgets (buffer, sizeof (buffer), fh) != NULL)
  {
    int len;
    char *type;
    char *type_instance;
    char *value;

    len = strlen (buffer);

    /* Remove newline from end. */
    while ((len > 0) && ((buffer[len - 1] == '\n')
	  || (buffer[len - 1] == '\r')))
      buffer[--len] = '\0';

    DEBUG ("exec plugin: exec_read_one: buffer = %s", buffer);

    if (len < 5)
      continue;

    if (buffer[0] == '#')
      continue;

    type = buffer;

    type_instance = strchr (type, ',');
    if (type_instance == NULL)
      continue;
    *type_instance = '\0';
    type_instance++;

    if ((strcasecmp ("counter", type) != 0)
	&& (strcasecmp ("gauge", type) != 0))
    {
      WARNING ("exec plugin: Received invalid type: %s", type);
      continue;
    }

    value = strchr (type_instance, ',');
    if (value == NULL)
    {
      WARNING ("exec plugin: type-instance is missing.");
      continue;
    }
    *value = '\0';
    value++;

    DEBUG ("exec plugin: exec_read_one: type = %s; type_instance = %s; "
	"value = %s;", type, type_instance, value);

    if (strcasecmp ("counter", type) == 0)
      submit_counter (type_instance, atoll (value));
    else
      submit_gauge (type_instance, atof (value));
  } /* while (fgets) */

  fclose (fh);
  pl->pid = 0;

  pthread_exit ((void *) 0);
  return (NULL);
} /* void *exec_read_one */

static int exec_read (void)
{
  program_list_t *pl;

  for (pl = pl_head; pl != NULL; pl = pl->next)
  {
    pthread_t t;
    pthread_attr_t attr;

    if (pl->pid != 0)
      continue;

    pthread_attr_init (&attr);
    pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
    pthread_create (&t, &attr, exec_read_one, (void *) pl);
  } /* for (pl) */

  return (0);
} /* int exec_read */

static int exec_shutdown (void)
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
} /* int exec_shutdown */

void module_register (void)
{
  plugin_register_config ("exec", exec_config, config_keys, config_keys_num);
  plugin_register_read ("exec", exec_read);
  plugin_register_shutdown ("exec", exec_shutdown);
} /* void module_register */

/*
 * vim:shiftwidth=2:softtabstop=2:tabstop=8
 */
