/*
 * collectd - src/apcups.c
 * Copyright (C) 2006-2015  Florian octo Forster
 * Copyright (C) 2006       Anthony Gialluca <tonyabg at charter.net>
 * Copyright (C) 2000-2004  Kern Sibbald
 * Copyright (C) 1996-1999  Andre M. Hedrick <andre at suse.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General
 * Public License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 *
 * Authors:
 *   Anthony Gialluca <tonyabg at charter.net>
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "common.h" /* rrd_update_file */
#include "plugin.h" /* plugin_register, plugin_submit */

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_NETDB_H
#include <netdb.h>
#endif

#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifndef APCUPS_SERVER_TIMEOUT
#define APCUPS_SERVER_TIMEOUT 15.0
#endif

#ifndef APCUPS_DEFAULT_NODE
#define APCUPS_DEFAULT_NODE "localhost"
#endif

#ifndef APCUPS_DEFAULT_SERVICE
#define APCUPS_DEFAULT_SERVICE "3551"
#endif

/*
 * Private data types
 */
typedef struct {
  gauge_t linev;
  gauge_t loadpct;
  gauge_t bcharge;
  gauge_t timeleft;
  gauge_t outputv;
  gauge_t itemp;
  gauge_t battv;
  gauge_t linefreq;
} apc_detail_t;

/*
 * Private variables
 */
/* Default values for contacting daemon */
static char *conf_node = NULL;
static char *conf_service = NULL;
/* Defaults to false for backwards compatibility. */
static _Bool conf_report_seconds = 0;
static _Bool conf_persistent_conn = 1;

static int global_sockfd = -1;

static int count_retries = 0;
static int count_iterations = 0;

static int net_shutdown(int *fd) {
  uint16_t packet_size = 0;

  if ((fd == NULL) || (*fd < 0))
    return EINVAL;

  (void)swrite(*fd, (void *)&packet_size, sizeof(packet_size));
  close(*fd);
  *fd = -1;

  return 0;
} /* int net_shutdown */

/* Close the network connection */
static int apcups_shutdown(void) {
  if (global_sockfd < 0)
    return 0;

  net_shutdown(&global_sockfd);
  return 0;
} /* int apcups_shutdown */

/*
 * Open a TCP connection to the UPS network server
 * Returns -1 on error
 * Returns socket file descriptor otherwise
 */
static int net_open(char const *node, char const *service) {
  int sd;
  int status;
  struct addrinfo *ai_return;
  struct addrinfo *ai_list;

  /* TODO: Change this to `AF_UNSPEC' if apcupsd can handle IPv6 */
  struct addrinfo ai_hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};

  status = getaddrinfo(node, service, &ai_hints, &ai_return);
  if (status != 0) {
    INFO("apcups plugin: getaddrinfo failed: %s",
         (status == EAI_SYSTEM) ? STRERRNO : gai_strerror(status));
    return -1;
  }

  /* Create socket */
  sd = -1;
  for (ai_list = ai_return; ai_list != NULL; ai_list = ai_list->ai_next) {
    sd = socket(ai_list->ai_family, ai_list->ai_socktype, ai_list->ai_protocol);
    if (sd >= 0)
      break;
  }
  /* `ai_list' still holds the current description of the socket.. */

  if (sd < 0) {
    DEBUG("apcups plugin: Unable to open a socket");
    freeaddrinfo(ai_return);
    return -1;
  }

  status = connect(sd, ai_list->ai_addr, ai_list->ai_addrlen);

  freeaddrinfo(ai_return);

  if (status != 0) /* `connect(2)' failed */
  {
    INFO("apcups plugin: connect failed: %s", STRERRNO);
    close(sd);
    return -1;
  }

  DEBUG("apcups plugin: Done opening a socket %i", sd);

  return sd;
} /* int net_open */

/*
 * Receive a message from the other end. Each message consists of
 * two packets. The first is a header that contains the size
 * of the data that follows in the second packet.
 * Returns number of bytes read
 * Returns 0 on end of file
 * Returns -1 on hard end of file (i.e. network connection close)
 * Returns -2 on error
 */
static int net_recv(int *sockfd, char *buf, int buflen) {
  uint16_t packet_size;

  /* get data size -- in short */
  if (sread(*sockfd, (void *)&packet_size, sizeof(packet_size)) != 0) {
    close(*sockfd);
    *sockfd = -1;
    return -1;
  }

  packet_size = ntohs(packet_size);
  if (packet_size > buflen) {
    ERROR("apcups plugin: Received %" PRIu16 " bytes of payload "
          "but have only %i bytes of buffer available.",
          packet_size, buflen);
    close(*sockfd);
    *sockfd = -1;
    return -2;
  }

  if (packet_size == 0)
    return 0;

  /* now read the actual data */
  if (sread(*sockfd, (void *)buf, packet_size) != 0) {
    close(*sockfd);
    *sockfd = -1;
    return -1;
  }

  return (int)packet_size;
} /* static int net_recv (int *sockfd, char *buf, int buflen) */

/*
 * Send a message over the network. The send consists of
 * two network packets. The first is sends a short containing
 * the length of the data packet which follows.
 * Returns zero on success
 * Returns non-zero on error
 */
static int net_send(int *sockfd, const char *buff, int len) {
  uint16_t packet_size;

  assert(len > 0);
  assert(*sockfd >= 0);

  /* send short containing size of data packet */
  packet_size = htons((uint16_t)len);

  if (swrite(*sockfd, (void *)&packet_size, sizeof(packet_size)) != 0) {
    close(*sockfd);
    *sockfd = -1;
    return -1;
  }

  /* send data packet */
  if (swrite(*sockfd, (void *)buff, len) != 0) {
    close(*sockfd);
    *sockfd = -1;
    return -2;
  }

  return 0;
}

/* Get and print status from apcupsd NIS server */
static int apc_query_server(char const *node, char const *service,
                            apc_detail_t *apcups_detail) {
  int n;
  char recvline[1024];
  char *tokptr;
  char *toksaveptr;
  int try
    = 0;
  int status;

#if APCMAIN
#define PRINT_VALUE(name, val)                                                 \
  printf("  Found property: name = %s; value = %f;\n", name, val)
#else
#define PRINT_VALUE(name, val) /**/
#endif

  while (1) {
    if (global_sockfd < 0) {
      global_sockfd = net_open(node, service);
      if (global_sockfd < 0) {
        ERROR("apcups plugin: Connecting to the "
              "apcupsd failed.");
        return -1;
      }
    }

    status = net_send(&global_sockfd, "status", strlen("status"));
    if (status != 0) {
      /* net_send closes the socket on error. */
      assert(global_sockfd < 0);
      if (try == 0) {
        try
          ++;
        count_retries++;
        continue;
      }

      ERROR("apcups plugin: Writing to the socket failed.");
      return -1;
    }

    break;
  } /* while (1) */

  /* When collectd's collection interval is larger than apcupsd's
   * timeout, we would have to retry / re-connect each iteration. Try to
   * detect this situation and shut down the socket gracefully in that
   * case. Otherwise, keep the socket open to avoid overhead. */
  count_iterations++;
  if ((count_iterations == 10) && (count_retries > 2)) {
    NOTICE("apcups plugin: There have been %i retries in the "
           "first %i iterations. Will close the socket "
           "in future iterations.",
           count_retries, count_iterations);
    conf_persistent_conn = 0;
  }

  while ((n = net_recv(&global_sockfd, recvline, sizeof(recvline) - 1)) > 0) {
    assert((size_t)n < sizeof(recvline));
    recvline[n] = 0;
#if APCMAIN
    printf("net_recv = `%s';\n", recvline);
#endif /* if APCMAIN */

    toksaveptr = NULL;
    tokptr = strtok_r(recvline, " :\t", &toksaveptr);
    while (tokptr != NULL) {
      char *key = tokptr;
      if ((tokptr = strtok_r(NULL, " :\t", &toksaveptr)) == NULL)
        continue;

      gauge_t value;
      if (strtogauge(tokptr, &value) != 0)
        continue;

      PRINT_VALUE(key, value);

      if (strcmp("LINEV", key) == 0)
        apcups_detail->linev = value;
      else if (strcmp("BATTV", key) == 0)
        apcups_detail->battv = value;
      else if (strcmp("ITEMP", key) == 0)
        apcups_detail->itemp = value;
      else if (strcmp("LOADPCT", key) == 0)
        apcups_detail->loadpct = value;
      else if (strcmp("BCHARGE", key) == 0)
        apcups_detail->bcharge = value;
      else if (strcmp("OUTPUTV", key) == 0)
        apcups_detail->outputv = value;
      else if (strcmp("LINEFREQ", key) == 0)
        apcups_detail->linefreq = value;
      else if (strcmp("TIMELEFT", key) == 0) {
        /* Convert minutes to seconds if requested by
         * the user. */
        if (conf_report_seconds)
          value *= 60.0;
        apcups_detail->timeleft = value;
      }

      tokptr = strtok_r(NULL, ":", &toksaveptr);
    } /* while (tokptr != NULL) */
  }
  status = errno; /* save errno, net_shutdown() may re-set it. */

  if (!conf_persistent_conn)
    net_shutdown(&global_sockfd);

  if (n < 0) {
    ERROR("apcups plugin: Reading from socket failed: %s", STRERROR(status));
    return -1;
  }

  return 0;
}

static int apcups_config(oconfig_item_t *ci) {
  _Bool persistent_conn_set = 0;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp(child->key, "Host") == 0)
      cf_util_get_string(child, &conf_node);
    else if (strcasecmp(child->key, "Port") == 0)
      cf_util_get_service(child, &conf_service);
    else if (strcasecmp(child->key, "ReportSeconds") == 0)
      cf_util_get_boolean(child, &conf_report_seconds);
    else if (strcasecmp(child->key, "PersistentConnection") == 0) {
      cf_util_get_boolean(child, &conf_persistent_conn);
      persistent_conn_set = 1;
    } else
      ERROR("apcups plugin: Unknown config option \"%s\".", child->key);
  }

  if (!persistent_conn_set) {
    double interval = CDTIME_T_TO_DOUBLE(plugin_get_interval());
    if (interval > APCUPS_SERVER_TIMEOUT) {
      NOTICE("apcups plugin: Plugin poll interval set to %.3f seconds. "
             "Apcupsd NIS socket timeout is %.3f seconds, "
             "PersistentConnection disabled by default.",
             interval, APCUPS_SERVER_TIMEOUT);
      conf_persistent_conn = 0;
    }
  }

  return 0;
} /* int apcups_config */

static void apc_submit_generic(const char *type, const char *type_inst,
                               gauge_t value) {
  if (isnan(value))
    return;

  value_list_t vl = VALUE_LIST_INIT;
  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "apcups", sizeof(vl.plugin));
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_inst, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static void apc_submit(apc_detail_t const *apcups_detail) {
  apc_submit_generic("voltage", "input", apcups_detail->linev);
  apc_submit_generic("voltage", "output", apcups_detail->outputv);
  apc_submit_generic("voltage", "battery", apcups_detail->battv);
  apc_submit_generic("charge", "", apcups_detail->bcharge);
  apc_submit_generic("percent", "load", apcups_detail->loadpct);
  apc_submit_generic("timeleft", "", apcups_detail->timeleft);
  apc_submit_generic("temperature", "", apcups_detail->itemp);
  apc_submit_generic("frequency", "input", apcups_detail->linefreq);
}

static int apcups_read(void) {
  apc_detail_t apcups_detail = {
      .linev = NAN,
      .outputv = NAN,
      .battv = NAN,
      .loadpct = NAN,
      .bcharge = NAN,
      .timeleft = NAN,
      .itemp = NAN,
      .linefreq = NAN,
  };

  int status = apc_query_server(conf_node, conf_service, &apcups_detail);

  if (status != 0) {
    DEBUG("apcups plugin: apc_query_server (\"%s\", \"%s\") = %d", conf_node,
          conf_service, status);
    return status;
  }

  apc_submit(&apcups_detail);

  return 0;
} /* apcups_read */

static int apcups_init(void) {
  if (conf_node == NULL)
    conf_node = APCUPS_DEFAULT_NODE;

  if (conf_service == NULL)
    conf_service = APCUPS_DEFAULT_SERVICE;

  return 0;
} /* apcups_init */

void module_register(void) {
  plugin_register_complex_config("apcups", apcups_config);
  plugin_register_init("apcups", apcups_init);
  plugin_register_read("apcups", apcups_read);
  plugin_register_shutdown("apcups", apcups_shutdown);
} /* void module_register */
