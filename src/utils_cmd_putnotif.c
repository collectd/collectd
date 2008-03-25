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

static int parse_option_severity (notification_t *n, char *value)
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
} /* int parse_option_severity */

static int parse_option_time (notification_t *n, char *value)
{
  time_t tmp;
  
  tmp = (time_t) atoi (value);
  if (tmp <= 0)
    return (-1);

  n->time = tmp;

  return (0);
} /* int parse_option_time */

static int parse_option (notification_t *n, char *buffer)
{
  char *option = buffer;
  char *value;

  if ((n == NULL) || (option == NULL))
    return (-1);

  value = strchr (option, '=');
  if (value == NULL)
    return (-1);
  *value = '\0'; value++;

  if (strcasecmp ("severity", option) == 0)
    return (parse_option_severity (n, value));
  else if (strcasecmp ("time", option) == 0)
    return (parse_option_time (n, value));
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
} /* int parse_option */

static int parse_message (notification_t *n, char **fields, int fields_num)
{
  int status;

  /* Strip off the leading `message=' */
  fields[0] += strlen ("message=");

  status = strjoin (n->message, sizeof (n->message), fields, fields_num, " ");
  if (status < 0)
    return (-1);

  return (0);
} /* int parse_message */

int handle_putnotif (FILE *fh, char **fields, int fields_num)
{
  notification_t n;
  int status;
  int i;

  /* Required fields: `PUTNOTIF', severity, time, message */
  if (fields_num < 4)
  {
    DEBUG ("cmd putnotif: Wrong number of fields: %i", fields_num);
    fprintf (fh, "-1 Wrong number of fields: Got %i, expected at least 4.\n",
	fields_num);
    fflush (fh);
    return (-1);
  }

  memset (&n, '\0', sizeof (n));

  status = 0;
  for (i = 1; i < fields_num; i++)
  {
    if (strncasecmp (fields[i], "message=", strlen ("message=")) == 0)
    {
      status = parse_message (&n, fields + i, fields_num - i);
      if (status != 0)
      {
	fprintf (fh, "-1 Error parsing the message. Have you hit the "
	    "limit of %u bytes?\n", (unsigned int) sizeof (n.message));
      }
      break;
    }
    else
    {
      status = parse_option (&n, fields[i]);
      if (status != 0)
      {
	fprintf (fh, "-1 Error parsing option `%s'\n", fields[i]);
	break;
      }
    }
  } /* for (i) */

  /* Check for required fields and complain if anything is missing. */
  if ((status == 0) && (n.severity == 0))
  {
    fprintf (fh, "-1 Option `severity' missing.\n");
    status = -1;
  }
  if ((status == 0) && (n.time == 0))
  {
    fprintf (fh, "-1 Option `time' missing.\n");
    status = -1;
  }
  if ((status == 0) && (strlen (n.message) == 0))
  {
    fprintf (fh, "-1 No message or message of length 0 given.\n");
    status = -1;
  }

  /* If status is still zero the notification is fine and we can finally
   * dispatch it. */
  if (status == 0)
  {
    plugin_dispatch_notification (&n);
    fprintf (fh, "0 Success\n");
  }
  fflush (fh);

  return (0);
} /* int handle_putnotif */

/* vim: set shiftwidth=2 softtabstop=2 tabstop=8 : */
