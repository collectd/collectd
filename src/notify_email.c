/**
 * collectd - src/notify_email.c
 * Copyright (C) 2008  Oleg King
 * Copyright (C) 2010  Florian Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *   Oleg King <king2 at kaluga.ru>
 *   Florian Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <auth-client.h>
#include <libesmtp.h>
#include <pthread.h>

#define MAXSTRING               256

static const char *config_keys[] =
{
  "SMTPServer",
  "SMTPPort",
  "SMTPUser",
  "SMTPPassword",
  "From",
  "Recipient",
  "Subject"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static char **recipients;
static int recipients_len = 0;

static smtp_session_t session;
static pthread_mutex_t session_lock = PTHREAD_MUTEX_INITIALIZER;
static smtp_message_t message;
static auth_context_t authctx = NULL;

static int smtp_port = 25;
static char *smtp_host = NULL;
static char *smtp_user = NULL;
static char *smtp_password = NULL;
static char *email_from = NULL;
static char *email_subject = NULL;

#define DEFAULT_SMTP_HOST	"localhost"
#define DEFAULT_SMTP_FROM	"root@localhost"
#define DEFAULT_SMTP_SUBJECT	"Collectd notify: %s@%s"

/* Callback to get username and password */
static int authinteract (auth_client_request_t request, char **result,
    int fields, void __attribute__((unused)) *arg)
{               
  int i;
  for (i = 0; i < fields; i++)
  {
    if (request[i].flags & AUTH_USER)
      result[i] = smtp_user;
    else if (request[i].flags & AUTH_PASS)
      result[i] = smtp_password;
    else
      return 0;
  }
  return 1;
} /* int authinteract */

/* Callback to print the recipient status */
static void print_recipient_status (smtp_recipient_t recipient,
    const char *mailbox, void __attribute__((unused)) *arg)
{
  const smtp_status_t *status;

  status = smtp_recipient_status (recipient);
  if (status->text[strlen(status->text) - 2] == '\r')
    status->text[strlen(status->text) - 2] = 0;
  INFO ("notify_email: notify sent to %s: %d %s", mailbox, status->code,
      status->text);
} /* void print_recipient_status */

/* Callback to monitor SMTP activity */
static void monitor_cb (const char *buf, int buflen, int writing,
    void __attribute__((unused)) *arg)
{
  char log_str[MAXSTRING];

  sstrncpy (log_str, buf, sizeof (log_str));
  if (buflen > 2)
    log_str[buflen - 2] = 0; /* replace \n with \0 */

  if (writing == SMTP_CB_HEADERS) {
    DEBUG ("notify_email plugin: SMTP --- H: %s", log_str);
    return;
  }
  DEBUG (writing
      ? "notify_email plugin: SMTP >>> C: %s"
      : "notify_email plugin: SMTP <<< S: %s",
      log_str);
} /* void monitor_cb */

static int notify_email_init (void)
{
  char server[MAXSTRING];

  ssnprintf(server, sizeof (server), "%s:%i",
      (smtp_host == NULL) ? DEFAULT_SMTP_HOST : smtp_host,
      smtp_port);

  pthread_mutex_lock (&session_lock);

  auth_client_init();

  session = smtp_create_session ();
  if (session == NULL) {
    pthread_mutex_unlock (&session_lock);
    ERROR ("notify_email plugin: cannot create SMTP session");
    return (-1);
  }

  smtp_set_monitorcb (session, monitor_cb, NULL, 1);
  smtp_set_hostname (session, hostname_g);
  smtp_set_server (session, server);

  if (smtp_user && smtp_password) {
    authctx = auth_create_context ();
    auth_set_mechanism_flags (authctx, AUTH_PLUGIN_PLAIN, 0);
    auth_set_interact_cb (authctx, authinteract, NULL);
  }

  if ( !smtp_auth_set_context (session, authctx)) {
    pthread_mutex_unlock (&session_lock);
    ERROR ("notify_email plugin: cannot set SMTP auth context");
    return (-1);   
  }

  pthread_mutex_unlock (&session_lock);
  return (0);
} /* int notify_email_init */

static int notify_email_shutdown (void)
{
  pthread_mutex_lock (&session_lock);

  if (session != NULL)
    smtp_destroy_session (session);
  session = NULL;

  if (authctx != NULL)
    auth_destroy_context (authctx);
  authctx = NULL;

  auth_client_exit();

  pthread_mutex_unlock (&session_lock);
  return (0);
} /* int notify_email_shutdown */

static int notify_email_config (const char *key, const char *value)
{
  if (strcasecmp (key, "Recipient") == 0)
  {
    char **tmp;

    tmp = (char **) realloc ((void *) recipients, (recipients_len + 1) * sizeof (char *));
    if (tmp == NULL) {
      ERROR ("notify_email: realloc failed.");
      return (-1);
    }

    recipients = tmp;
    recipients[recipients_len] = strdup (value);
    if (recipients[recipients_len] == NULL) {
      ERROR ("notify_email: strdup failed.");
      return (-1);
    }
    recipients_len++;
  }
  else if (0 == strcasecmp (key, "SMTPServer")) {
    sfree (smtp_host);
    smtp_host = strdup (value);
  }
  else if (0 == strcasecmp (key, "SMTPPort")) {
    int port_tmp = atoi (value);
    if (port_tmp < 1 || port_tmp > 65535)
    {
      WARNING ("notify_email plugin: Invalid SMTP port: %i", port_tmp);
      return (1);
    }
    smtp_port = port_tmp;
  }
  else if (0 == strcasecmp (key, "SMTPUser")) {
    sfree (smtp_user);
    smtp_user = strdup (value);
  }
  else if (0 == strcasecmp (key, "SMTPPassword")) {
    sfree (smtp_password);
    smtp_password = strdup (value);
  }
  else if (0 == strcasecmp (key, "From")) {
    sfree (email_from);
    email_from = strdup (value);
  }
  else if (0 == strcasecmp (key, "Subject")) {
    sfree (email_subject);
    email_subject = strdup (value);
  }
  else {
    return -1;
  }
  return 0;
} /* int notify_email_config (const char *, const char *) */

static int notify_email_notification (const notification_t *n,
    user_data_t __attribute__((unused)) *user_data)
{

  time_t tt;
  struct tm timestamp_tm;
  char timestamp_str[64];

  char severity[32];
  char subject[MAXSTRING];

  char buf[4096] = "";
  int  buf_len = sizeof (buf);
  int i;

  ssnprintf (severity, sizeof (severity), "%s",
      (n->severity == NOTIF_FAILURE) ? "FAILURE"
      : ((n->severity == NOTIF_WARNING) ? "WARNING"
        : ((n->severity == NOTIF_OKAY) ? "OKAY" : "UNKNOWN")));

  ssnprintf (subject, sizeof (subject),
      (email_subject == NULL) ? DEFAULT_SMTP_SUBJECT : email_subject,
      severity, n->host);

  tt = CDTIME_T_TO_TIME_T (n->time);
  localtime_r (&tt, &timestamp_tm);
  strftime (timestamp_str, sizeof (timestamp_str), "%Y-%m-%d %H:%M:%S",
      &timestamp_tm);
  timestamp_str[sizeof (timestamp_str) - 1] = '\0';

  /* Let's make RFC822 message text with \r\n EOLs */
  ssnprintf (buf, buf_len,
      "MIME-Version: 1.0\r\n"
      "Content-Type: text/plain;\r\n"
      "Content-Transfer-Encoding: 8bit\r\n"
      "Subject: %s\r\n"
      "\r\n"
      "%s - %s@%s\r\n"
      "\r\n"
      "Message: %s",
      subject,
      timestamp_str,
      severity,
      n->host,
      n->message);

  pthread_mutex_lock (&session_lock);

  if (session == NULL) {
    /* Initialization failed or we're in the process of shutting down. */
    pthread_mutex_unlock (&session_lock);
    return (-1);
  }

  if (!(message = smtp_add_message (session))) {
    pthread_mutex_unlock (&session_lock);
    ERROR ("notify_email plugin: cannot set SMTP message");
    return (-1);   
  }
  smtp_set_reverse_path (message, email_from);
  smtp_set_header (message, "To", NULL, NULL);
  smtp_set_message_str (message, buf);

  for (i = 0; i < recipients_len; i++)
    smtp_add_recipient (message, recipients[i]);

  /* Initiate a connection to the SMTP server and transfer the message. */
  if (!smtp_start_session (session)) {
    char buf[MAXSTRING];
    ERROR ("notify_email plugin: SMTP server problem: %s",
        smtp_strerror (smtp_errno (), buf, sizeof buf));
    pthread_mutex_unlock (&session_lock);
    return (-1);
  } else {
    #if COLLECT_DEBUG
    const smtp_status_t *status;
    /* Report on the success or otherwise of the mail transfer. */
    status = smtp_message_transfer_status (message);
    DEBUG ("notify_email plugin: SMTP server report: %d %s",
      status->code, (status->text != NULL) ? status->text : "\n");
    #endif
    smtp_enumerate_recipients (message, print_recipient_status, NULL);
  }

  pthread_mutex_unlock (&session_lock);
  return (0);
} /* int notify_email_notification */

void module_register (void)
{
  plugin_register_init ("notify_email", notify_email_init);
  plugin_register_shutdown ("notify_email", notify_email_shutdown);
  plugin_register_config ("notify_email", notify_email_config,
      config_keys, config_keys_num);
  plugin_register_notification ("notify_email", notify_email_notification,
      /* user_data = */ NULL);
} /* void module_register (void) */

/* vim: set sw=2 sts=2 ts=8 et : */
