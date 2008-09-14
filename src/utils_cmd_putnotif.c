/**
 * collectd - src/utils_cms_putnotif.c
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
 * Author:
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include "utils_parse_option.h"

#define print_to_socket(fh, ...) \
  if (fprintf (fh, __VA_ARGS__) < 0) { \
    char errbuf[1024]; \
    WARNING ("handle_putnotif: failed to write to socket #%i: %s", \
	fileno (fh), sstrerror (errno, errbuf, sizeof (errbuf))); \
    return -1; \
  }

static int set_option_severity (notification_t *n, const char *value)
{
  if (strcasecmp (value, "Failure") == 0)
    n->severity = NOTIF_FAILURE;
  else if (strcasecmp (value, "Warning") == 0)
    n->severity = NOTIF_WARNING;
  else if (strcasecmp (value, "Okay") == 0)
    n->severity = NOTIF_OKAY;
  else
    return (-1);

  return (0);
} /* int set_option_severity */

static int set_option_time (notification_t *n, const char *value)
{
  time_t tmp;
  
  tmp = (time_t) atoi (value);
  if (tmp <= 0)
    return (-1);

  n->time = tmp;

  return (0);
} /* int set_option_time */

static int set_option (notification_t *n, const char *option, const char *value)
{
  if ((n == NULL) || (option == NULL) || (value == NULL))
    return (-1);

  DEBUG ("utils_cmd_putnotif: set_option (option = %s, value = %s);",
      option, value);

  if (strcasecmp ("severity", option) == 0)
    return (set_option_severity (n, value));
  else if (strcasecmp ("time", option) == 0)
    return (set_option_time (n, value));
  else if (strcasecmp ("message", option) == 0)
    sstrncpy (n->message, value, sizeof (n->message));
  else if (strcasecmp ("host", option) == 0)
    sstrncpy (n->host, value, sizeof (n->host));
  else if (strcasecmp ("plugin", option) == 0)
    sstrncpy (n->plugin, value, sizeof (n->plugin));
  else if (strcasecmp ("plugin_instance", option) == 0)
    sstrncpy (n->plugin_instance, value, sizeof (n->plugin_instance));
  else if (strcasecmp ("type", option) == 0)
    sstrncpy (n->type, value, sizeof (n->type));
  else if (strcasecmp ("type_instance", option) == 0)
    sstrncpy (n->type_instance, value, sizeof (n->type_instance));
  else
    return (1);

  return (0);
} /* int set_option */

int handle_putnotif (FILE *fh, char *buffer)
{
  char *command;
  notification_t n;
  int status;

  if ((fh == NULL) || (buffer == NULL))
    return (-1);

  DEBUG ("utils_cmd_putnotif: handle_putnotif (fh = %p, buffer = %s);",
      (void *) fh, buffer);

  command = NULL;
  status = parse_string (&buffer, &command);
  if (status != 0)
  {
    print_to_socket (fh, "-1 Cannot parse command.\n");
    return (-1);
  }
  assert (command != NULL);

  if (strcasecmp ("PUTNOTIF", command) != 0)
  {
    print_to_socket (fh, "-1 Unexpected command: `%s'.\n", command);
    return (-1);
  }

  memset (&n, '\0', sizeof (n));

  status = 0;
  while (*buffer != 0)
  {
    char *key;
    char *value;

    status = parse_option (&buffer, &key, &value);
    if (status != 0)
    {
      print_to_socket (fh, "-1 Malformed option.\n");
      break;
    }

    status = set_option (&n, key, value);
    if (status != 0)
    {
      print_to_socket (fh, "-1 Error parsing option `%s'\n", key);
      break;
    }
  } /* for (i) */

  /* Check for required fields and complain if anything is missing. */
  if ((status == 0) && (n.severity == 0))
  {
    print_to_socket (fh, "-1 Option `severity' missing.\n");
    status = -1;
  }
  if ((status == 0) && (n.time == 0))
  {
    print_to_socket (fh, "-1 Option `time' missing.\n");
    status = -1;
  }
  if ((status == 0) && (strlen (n.message) == 0))
  {
    print_to_socket (fh, "-1 No message or message of length 0 given.\n");
    status = -1;
  }

  /* If status is still zero the notification is fine and we can finally
   * dispatch it. */
  if (status == 0)
  {
    plugin_dispatch_notification (&n);
    print_to_socket (fh, "0 Success\n");
  }

  return (0);
} /* int handle_putnotif */

/* vim: set shiftwidth=2 softtabstop=2 tabstop=8 : */
