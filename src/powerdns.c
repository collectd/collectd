/**
 * collectd - src/powerdns.c
 * Copyright (C) 2007-2008  C-Ware, Inc.
 * Copyright (C) 2008       Florian Forster
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
 *   Luke Heberling <lukeh at c-ware.com>
 *   Florian Forster <octo at collectd.org>
 *
 * DESCRIPTION
 *   Queries a PowerDNS control socket for statistics
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_llist.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX sizeof(((struct sockaddr_un *)0)->sun_path)
#endif
#define FUNC_ERROR(func)                                                       \
  do {                                                                         \
    ERROR("powerdns plugin: %s failed: %s", func, STRERRNO);                   \
  } while (0)
#define SOCK_ERROR(func, sockpath)                                             \
  do {                                                                         \
    ERROR("powerdns plugin: Socket `%s` %s failed: %s", sockpath, func,        \
          STRERRNO);                                                           \
  } while (0)

#define SERVER_SOCKET LOCALSTATEDIR "/run/pdns.controlsocket"
#define SERVER_COMMAND "SHOW * \n"

#define RECURSOR_SOCKET LOCALSTATEDIR "/run/pdns_recursor.controlsocket"
#define RECURSOR_COMMAND                                                       \
  "get noerror-answers nxdomain-answers "                                      \
  "servfail-answers sys-msec user-msec qa-latency cache-entries cache-hits "   \
  "cache-misses questions \n"

struct list_item_s;
typedef struct list_item_s list_item_t;

struct list_item_s {
  enum { SRV_AUTHORITATIVE, SRV_RECURSOR } server_type;
  int (*func)(list_item_t *item);
  char *instance;

  char **fields;
  int fields_num;
  char *command;

  struct sockaddr_un sockaddr;
  int socktype;
};

struct statname_lookup_s {
  const char *name;
  const char *type;
  const char *type_instance;
};
typedef struct statname_lookup_s statname_lookup_t;

/* Description of statistics returned by the recursor: {{{
all-outqueries        counts the number of outgoing UDP queries since starting
answers-slow          counts the number of queries answered after 1 second
answers0-1            counts the number of queries answered within 1 millisecond
answers1-10           counts the number of queries answered within 10
milliseconds
answers10-100         counts the number of queries answered within 100
milliseconds
answers100-1000       counts the number of queries answered within 1 second
cache-bytes           size of the cache in bytes (since 3.3.1)
cache-entries         shows the number of entries in the cache
cache-hits            counts the number of cache hits since starting, this does
not include hits that got answered from the packet-cache
cache-misses          counts the number of cache misses since starting
case-mismatches       counts the number of mismatches in character case since
starting
chain-resends         number of queries chained to existing outstanding query
client-parse-errors   counts number of client packets that could not be parsed
concurrent-queries    shows the number of MThreads currently running
dlg-only-drops        number of records dropped because of delegation only
setting
dont-outqueries       number of outgoing queries dropped because of 'dont-query'
setting (since 3.3)
edns-ping-matches     number of servers that sent a valid EDNS PING respons
edns-ping-mismatches  number of servers that sent an invalid EDNS PING response
failed-host-entries   number of servers that failed to resolve
ipv6-outqueries       number of outgoing queries over IPv6
ipv6-questions        counts all End-user initiated queries with the RD bit set,
received over IPv6 UDP
malloc-bytes          returns the number of bytes allocated by the process
(broken, always returns 0)
max-mthread-stack     maximum amount of thread stack ever used
negcache-entries      shows the number of entries in the Negative answer cache
no-packet-error       number of errorneous received packets
noedns-outqueries     number of queries sent out without EDNS
noerror-answers       counts the number of times it answered NOERROR since
starting
noping-outqueries     number of queries sent out without ENDS PING
nsset-invalidations   number of times an nsset was dropped because it no longer
worked
nsspeeds-entries      shows the number of entries in the NS speeds map
nxdomain-answers      counts the number of times it answered NXDOMAIN since
starting
outgoing-timeouts     counts the number of timeouts on outgoing UDP queries
since starting
over-capacity-drops   questions dropped because over maximum concurrent query
limit (since 3.2)
packetcache-bytes     size of the packet cache in bytes (since 3.3.1)
packetcache-entries   size of packet cache (since 3.2)
packetcache-hits      packet cache hits (since 3.2)
packetcache-misses    packet cache misses (since 3.2)
policy-drops          packets dropped because of (Lua) policy decision
qa-latency            shows the current latency average
questions             counts all end-user initiated queries with the RD bit set
resource-limits       counts number of queries that could not be performed
because of resource limits
security-status       security status based on security polling
server-parse-errors   counts number of server replied packets that could not be
parsed
servfail-answers      counts the number of times it answered SERVFAIL since
starting
spoof-prevents        number of times PowerDNS considered itself spoofed, and
dropped the data
sys-msec              number of CPU milliseconds spent in 'system' mode
tcp-client-overflow   number of times an IP address was denied TCP access
because it already had too many connections
tcp-clients           counts the number of currently active TCP/IP clients
tcp-outqueries        counts the number of outgoing TCP queries since starting
tcp-questions         counts all incoming TCP queries (since starting)
throttle-entries      shows the number of entries in the throttle map
throttled-out         counts the number of throttled outgoing UDP queries since
starting
throttled-outqueries  idem to throttled-out
unauthorized-tcp      number of TCP questions denied because of allow-from
restrictions
unauthorized-udp      number of UDP questions denied because of allow-from
restrictions
unexpected-packets    number of answers from remote servers that were unexpected
(might point to spoofing)
unreachables          number of times nameservers were unreachable since
starting
uptime                number of seconds process has been running (since 3.1.5)
user-msec             number of CPU milliseconds spent in 'user' mode
}}} */

static const char *const default_server_fields[] = /* {{{ */
    {
        "latency",           "packetcache-hit",     "packetcache-miss",
        "packetcache-size",  "query-cache-hit",     "query-cache-miss",
        "recursing-answers", "recursing-questions", "tcp-answers",
        "tcp-queries",       "udp-answers",         "udp-queries",
}; /* }}} */
static int default_server_fields_num = STATIC_ARRAY_SIZE(default_server_fields);

static statname_lookup_t lookup_table[] = /* {{{ */
    {
        /*********************
         * Server statistics *
         *********************/
        /* Questions */
        {"recursing-questions", "dns_question", "recurse"},
        {"tcp-queries", "dns_question", "tcp"},
        {"udp-queries", "dns_question", "udp"},
        {"rd-queries", "dns_question", "rd"},

        /* Answers */
        {"recursing-answers", "dns_answer", "recurse"},
        {"tcp-answers", "dns_answer", "tcp"},
        {"udp-answers", "dns_answer", "udp"},
        {"recursion-unanswered", "dns_answer", "recursion-unanswered"},
        {"udp-answers-bytes", "total_bytes", "udp-answers-bytes"},

        /* Cache stuff */
        {"cache-bytes", "cache_size", "cache-bytes"},
        {"packetcache-bytes", "cache_size", "packet-bytes"},
        {"packetcache-entries", "cache_size", "packet-entries"},
        {"packetcache-hit", "cache_result", "packet-hit"},
        {"packetcache-hits", "cache_result", "packet-hit"},
        {"packetcache-miss", "cache_result", "packet-miss"},
        {"packetcache-misses", "cache_result", "packet-miss"},
        {"packetcache-size", "cache_size", "packet"},
        {"key-cache-size", "cache_size", "key"},
        {"meta-cache-size", "cache_size", "meta"},
        {"signature-cache-size", "cache_size", "signature"},
        {"query-cache-hit", "cache_result", "query-hit"},
        {"query-cache-miss", "cache_result", "query-miss"},

        /* Latency */
        {"latency", "latency", NULL},

        /* DNS updates */
        {"dnsupdate-answers", "dns_answer", "dnsupdate-answer"},
        {"dnsupdate-changes", "dns_question", "dnsupdate-changes"},
        {"dnsupdate-queries", "dns_question", "dnsupdate-queries"},
        {"dnsupdate-refused", "dns_answer", "dnsupdate-refused"},

        /* Other stuff.. */
        {"corrupt-packets", "ipt_packets", "corrupt"},
        {"deferred-cache-inserts", "counter", "cache-deferred_insert"},
        {"deferred-cache-lookup", "counter", "cache-deferred_lookup"},
        {"dont-outqueries", "dns_question", "dont-outqueries"},
        {"qsize-a", "cache_size", "answers"},
        {"qsize-q", "cache_size", "questions"},
        {"servfail-packets", "ipt_packets", "servfail"},
        {"timedout-packets", "ipt_packets", "timeout"},
        {"udp4-answers", "dns_answer", "udp4"},
        {"udp4-queries", "dns_question", "queries-udp4"},
        {"udp6-answers", "dns_answer", "udp6"},
        {"udp6-queries", "dns_question", "queries-udp6"},
        {"security-status", "dns_question", "security-status"},
        {"udp-do-queries", "dns_question", "udp-do_queries"},
        {"signatures", "counter", "signatures"},

        /***********************
         * Recursor statistics *
         ***********************/
        /* Answers by return code */
        {"noerror-answers", "dns_rcode", "NOERROR"},
        {"nxdomain-answers", "dns_rcode", "NXDOMAIN"},
        {"servfail-answers", "dns_rcode", "SERVFAIL"},

        /* CPU utilization */
        {"sys-msec", "cpu", "system"},
        {"user-msec", "cpu", "user"},

        /* Question-to-answer latency */
        {"qa-latency", "latency", NULL},

        /* Cache */
        {"cache-entries", "cache_size", NULL},
        {"cache-hits", "cache_result", "hit"},
        {"cache-misses", "cache_result", "miss"},

        /* Total number of questions.. */
        {"questions", "dns_qtype", "total"},

        /* All the other stuff.. */
        {"all-outqueries", "dns_question", "outgoing"},
        {"answers0-1", "dns_answer", "0_1"},
        {"answers1-10", "dns_answer", "1_10"},
        {"answers10-100", "dns_answer", "10_100"},
        {"answers100-1000", "dns_answer", "100_1000"},
        {"answers-slow", "dns_answer", "slow"},
        {"case-mismatches", "counter", "case_mismatches"},
        {"chain-resends", "dns_question", "chained"},
        {"client-parse-errors", "counter", "drops-client_parse_error"},
        {"concurrent-queries", "dns_question", "concurrent"},
        {"dlg-only-drops", "counter", "drops-delegation_only"},
        {"edns-ping-matches", "counter", "edns-ping_matches"},
        {"edns-ping-mismatches", "counter", "edns-ping_mismatches"},
        {"failed-host-entries", "counter", "entries-failed_host"},
        {"ipv6-outqueries", "dns_question", "outgoing-ipv6"},
        {"ipv6-questions", "dns_question", "incoming-ipv6"},
        {"malloc-bytes", "gauge", "malloc_bytes"},
        {"max-mthread-stack", "gauge", "max_mthread_stack"},
        {"no-packet-error", "gauge", "no_packet_error"},
        {"noedns-outqueries", "dns_question", "outgoing-noedns"},
        {"noping-outqueries", "dns_question", "outgoing-noping"},
        {"over-capacity-drops", "dns_question", "incoming-over_capacity"},
        {"negcache-entries", "cache_size", "negative"},
        {"nsspeeds-entries", "gauge", "entries-ns_speeds"},
        {"nsset-invalidations", "counter", "ns_set_invalidation"},
        {"outgoing-timeouts", "counter", "drops-timeout_outgoing"},
        {"policy-drops", "counter", "drops-policy"},
        {"resource-limits", "counter", "drops-resource_limit"},
        {"server-parse-errors", "counter", "drops-server_parse_error"},
        {"spoof-prevents", "counter", "drops-spoofed"},
        {"tcp-client-overflow", "counter", "denied-client_overflow_tcp"},
        {"tcp-clients", "gauge", "clients-tcp"},
        {"tcp-outqueries", "dns_question", "outgoing-tcp"},
        {"tcp-questions", "dns_question", "incoming-tcp"},
        {"throttled-out", "dns_question", "outgoing-throttled"},
        {"throttle-entries", "gauge", "entries-throttle"},
        {"throttled-outqueries", "dns_question", "outgoing-throttle"},
        {"unauthorized-tcp", "counter", "denied-unauthorized_tcp"},
        {"unauthorized-udp", "counter", "denied-unauthorized_udp"},
        {"unexpected-packets", "dns_answer", "unexpected"},
        {"uptime", "uptime", NULL}}; /* }}} */
static int lookup_table_length = STATIC_ARRAY_SIZE(lookup_table);

static llist_t *list = NULL;

#define PDNS_LOCAL_SOCKPATH LOCALSTATEDIR "/run/" PACKAGE_NAME "-powerdns"
static char *local_sockpath = NULL;

/* TODO: Do this before 4.4:
 * - Update the collectd.conf(5) manpage.
 *
 * -octo
 */

/* <https://doc.powerdns.com/md/recursor/stats/> */
static void submit(const char *plugin_instance, /* {{{ */
                   const char *pdns_type, const char *value_str) {
  value_list_t vl = VALUE_LIST_INIT;
  value_t value;

  const char *type = NULL;
  const char *type_instance = NULL;
  const data_set_t *ds;

  int i;

  for (i = 0; i < lookup_table_length; i++)
    if (strcmp(lookup_table[i].name, pdns_type) == 0)
      break;

  if (i >= lookup_table_length) {
    INFO("powerdns plugin: submit: Not found in lookup table: %s = %s;",
         pdns_type, value_str);
    return;
  }

  if (lookup_table[i].type == NULL)
    return;

  type = lookup_table[i].type;
  type_instance = lookup_table[i].type_instance;

  ds = plugin_get_ds(type);
  if (ds == NULL) {
    ERROR("powerdns plugin: The lookup table returned type `%s', "
          "but I cannot find it via `plugin_get_ds'.",
          type);
    return;
  }

  if (ds->ds_num != 1) {
    ERROR("powerdns plugin: type `%s' has %" PRIsz " data sources, "
          "but I can only handle one.",
          type, ds->ds_num);
    return;
  }

  if (0 != parse_value(value_str, &value, ds->ds[0].type)) {
    ERROR("powerdns plugin: Cannot convert `%s' "
          "to a number.",
          value_str);
    return;
  }

  vl.values = &value;
  vl.values_len = 1;
  sstrncpy(vl.plugin, "powerdns", sizeof(vl.plugin));
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_instance != NULL)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));
  sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));

  plugin_dispatch_values(&vl);
} /* }}} static void submit */

static int powerdns_get_data_dgram(list_item_t *item, char **ret_buffer) {
  /* {{{ */
  int sd;
  int status;

  char temp[4096];
  char *buffer = NULL;
  size_t buffer_size = 0;

  struct sockaddr_un sa_unix = {0};

  cdtime_t cdt_timeout;

  sd = socket(PF_UNIX, item->socktype, 0);
  if (sd < 0) {
    FUNC_ERROR("socket");
    return -1;
  }

  sa_unix.sun_family = AF_UNIX;
  sstrncpy(sa_unix.sun_path,
           (local_sockpath != NULL) ? local_sockpath : PDNS_LOCAL_SOCKPATH,
           sizeof(sa_unix.sun_path));

  status = unlink(sa_unix.sun_path);
  if ((status != 0) && (errno != ENOENT)) {
    SOCK_ERROR("unlink", sa_unix.sun_path);
    close(sd);
    return -1;
  }

  do /* while (0) */
  {
    /* We need to bind to a specific path, because this is a datagram socket
     * and otherwise the daemon cannot answer. */
    status = bind(sd, (struct sockaddr *)&sa_unix, sizeof(sa_unix));
    if (status != 0) {
      SOCK_ERROR("bind", sa_unix.sun_path);
      break;
    }

    /* Make the socket writeable by the daemon.. */
    status = chmod(sa_unix.sun_path, 0666);
    if (status != 0) {
      SOCK_ERROR("chmod", sa_unix.sun_path);
      break;
    }

    cdt_timeout = plugin_get_interval() * 3 / 4;
    if (cdt_timeout < TIME_T_TO_CDTIME_T(2))
      cdt_timeout = TIME_T_TO_CDTIME_T(2);

    status =
        setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO,
                   &CDTIME_T_TO_TIMEVAL(cdt_timeout), sizeof(struct timeval));
    if (status != 0) {
      SOCK_ERROR("setsockopt", sa_unix.sun_path);
      break;
    }

    status =
        connect(sd, (struct sockaddr *)&item->sockaddr, sizeof(item->sockaddr));
    if (status != 0) {
      SOCK_ERROR("connect", sa_unix.sun_path);
      break;
    }

    status = send(sd, item->command, strlen(item->command), 0);
    if (status < 0) {
      SOCK_ERROR("send", sa_unix.sun_path);
      break;
    }

    status = recv(sd, temp, sizeof(temp), /* flags = */ 0);
    if (status < 0) {
      SOCK_ERROR("recv", sa_unix.sun_path);
      break;
    }
    buffer_size = status + 1;
    status = 0;
  } while (0);

  close(sd);
  unlink(sa_unix.sun_path);

  if (status != 0)
    return -1;

  assert(buffer_size > 0);
  buffer = malloc(buffer_size);
  if (buffer == NULL) {
    FUNC_ERROR("malloc");
    return -1;
  }

  memcpy(buffer, temp, buffer_size - 1);
  buffer[buffer_size - 1] = 0;

  *ret_buffer = buffer;
  return 0;
} /* }}} int powerdns_get_data_dgram */

static int powerdns_get_data_stream(list_item_t *item, char **ret_buffer) {
  /* {{{ */
  int sd;
  int status;

  char temp[4096];
  char *buffer = NULL;
  size_t buffer_size = 0;

  sd = socket(PF_UNIX, item->socktype, 0);
  if (sd < 0) {
    FUNC_ERROR("socket");
    return -1;
  }

  struct timeval timeout;
  timeout.tv_sec = 5;
  timeout.tv_usec = 0;
  status = setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  if (status != 0) {
    FUNC_ERROR("setsockopt");
    close(sd);
    return -1;
  }

  status =
      connect(sd, (struct sockaddr *)&item->sockaddr, sizeof(item->sockaddr));
  if (status != 0) {
    SOCK_ERROR("connect", item->sockaddr.sun_path);
    close(sd);
    return -1;
  }

  /* strlen + 1, because we need to send the terminating NULL byte, too. */
  status = send(sd, item->command, strlen(item->command) + 1,
                /* flags = */ 0);
  if (status < 0) {
    SOCK_ERROR("send", item->sockaddr.sun_path);
    close(sd);
    return -1;
  }

  while (42) {
    char *buffer_new;

    status = recv(sd, temp, sizeof(temp), /* flags = */ 0);
    if (status < 0) {
      SOCK_ERROR("recv", item->sockaddr.sun_path);
      break;
    } else if (status == 0) {
      break;
    }

    buffer_new = realloc(buffer, buffer_size + status + 1);
    if (buffer_new == NULL) {
      FUNC_ERROR("realloc");
      status = ENOMEM;
      break;
    }
    buffer = buffer_new;

    memcpy(buffer + buffer_size, temp, status);
    buffer_size += status;
    buffer[buffer_size] = 0;
  } /* while (42) */
  close(sd);

  if (status != 0) {
    sfree(buffer);
    return status;
  }

  *ret_buffer = buffer;
  return 0;
} /* }}} int powerdns_get_data_stream */

static int powerdns_get_data(list_item_t *item, char **ret_buffer) {
  if (item->socktype == SOCK_DGRAM)
    return powerdns_get_data_dgram(item, ret_buffer);
  else if (item->socktype == SOCK_STREAM)
    return powerdns_get_data_stream(item, ret_buffer);
  else {
    ERROR("powerdns plugin: Unknown socket type: %i", (int)item->socktype);
    return -1;
  }
} /* int powerdns_get_data */

static int powerdns_read_server(list_item_t *item) /* {{{ */
{
  if (item->command == NULL)
    item->command = strdup(SERVER_COMMAND);
  if (item->command == NULL) {
    ERROR("powerdns plugin: strdup failed.");
    return -1;
  }

  char *buffer = NULL;
  int status = powerdns_get_data(item, &buffer);
  if (status != 0) {
    ERROR("powerdns plugin: powerdns_get_data failed.");
    return status;
  }
  if (buffer == NULL) {
    return EINVAL;
  }

  const char *const *fields = default_server_fields;
  int fields_num = default_server_fields_num;
  if (item->fields_num != 0) {
    fields = (const char *const *)item->fields;
    fields_num = item->fields_num;
  }

  assert(fields != NULL);
  assert(fields_num > 0);

  /* corrupt-packets=0,deferred-cache-inserts=0,deferred-cache-lookup=0,latency=0,packetcache-hit=0,packetcache-miss=0,packetcache-size=0,qsize-q=0,query-cache-hit=0,query-cache-miss=0,recursing-answers=0,recursing-questions=0,servfail-packets=0,tcp-answers=0,tcp-queries=0,timedout-packets=0,udp-answers=0,udp-queries=0,udp4-answers=0,udp4-queries=0,udp6-answers=0,udp6-queries=0,
   */
  char *dummy = buffer;
  char *saveptr = NULL;
  char *key;
  while ((key = strtok_r(dummy, ",", &saveptr)) != NULL) {
    dummy = NULL;

    char *value = strchr(key, '=');
    if (value == NULL)
      break;

    *value = '\0';
    value++;

    if (value[0] == '\0')
      continue;

    /* Check if this item was requested. */
    int i;
    for (i = 0; i < fields_num; i++)
      if (strcasecmp(key, fields[i]) == 0)
        break;
    if (i >= fields_num)
      continue;

    submit(item->instance, key, value);
  } /* while (strtok_r) */

  sfree(buffer);

  return 0;
} /* }}} int powerdns_read_server */

/*
 * powerdns_update_recursor_command
 *
 * Creates a string that holds the command to be sent to the recursor. This
 * string is stores in the `command' member of the `list_item_t' passed to the
 * function. This function is called by `powerdns_read_recursor'.
 */
static int powerdns_update_recursor_command(list_item_t *li) /* {{{ */
{
  char buffer[4096];
  int status;

  if (li == NULL)
    return 0;

  if (li->fields_num < 1) {
    sstrncpy(buffer, RECURSOR_COMMAND, sizeof(buffer));
  } else {
    sstrncpy(buffer, "get ", sizeof(buffer));
    status = strjoin(&buffer[strlen("get ")], sizeof(buffer) - strlen("get "),
                     li->fields, li->fields_num,
                     /* seperator = */ " ");
    if (status < 0) {
      ERROR("powerdns plugin: strjoin failed.");
      return -1;
    }
    buffer[sizeof(buffer) - 1] = 0;
    size_t len = strlen(buffer);
    if (len < sizeof(buffer) - 2) {
      buffer[len++] = ' ';
      buffer[len++] = '\n';
      buffer[len++] = '\0';
    }
  }

  buffer[sizeof(buffer) - 1] = 0;
  li->command = strdup(buffer);
  if (li->command == NULL) {
    ERROR("powerdns plugin: strdup failed.");
    return -1;
  }

  return 0;
} /* }}} int powerdns_update_recursor_command */

static int powerdns_read_recursor(list_item_t *item) /* {{{ */
{
  char *buffer = NULL;
  int status;

  char *dummy;

  char *keys_list;
  char *key;
  char *key_saveptr;
  char *value;
  char *value_saveptr;

  if (item->command == NULL) {
    status = powerdns_update_recursor_command(item);
    if (status != 0) {
      ERROR("powerdns plugin: powerdns_update_recursor_command failed.");
      return -1;
    }

    DEBUG("powerdns plugin: powerdns_read_recursor: item->command = %s;",
          item->command);
  }
  assert(item->command != NULL);

  status = powerdns_get_data(item, &buffer);
  if (status != 0) {
    ERROR("powerdns plugin: powerdns_get_data failed.");
    return -1;
  }

  keys_list = strdup(item->command);
  if (keys_list == NULL) {
    FUNC_ERROR("strdup");
    sfree(buffer);
    return -1;
  }

  key_saveptr = NULL;
  value_saveptr = NULL;

  /* Skip the `get' at the beginning */
  strtok_r(keys_list, " \t", &key_saveptr);

  dummy = buffer;
  while ((value = strtok_r(dummy, " \t\n\r", &value_saveptr)) != NULL) {
    dummy = NULL;

    key = strtok_r(NULL, " \t", &key_saveptr);
    if (key == NULL)
      break;

    submit(item->instance, key, value);
  } /* while (strtok_r) */

  sfree(buffer);
  sfree(keys_list);

  return 0;
} /* }}} int powerdns_read_recursor */

static int powerdns_config_add_collect(list_item_t *li, /* {{{ */
                                       oconfig_item_t *ci) {
  char **temp;

  if (ci->values_num < 1) {
    WARNING("powerdns plugin: The `Collect' option needs "
            "at least one argument.");
    return -1;
  }

  for (int i = 0; i < ci->values_num; i++)
    if (ci->values[i].type != OCONFIG_TYPE_STRING) {
      WARNING("powerdns plugin: Only string arguments are allowed to "
              "the `Collect' option.");
      return -1;
    }

  temp =
      realloc(li->fields, sizeof(char *) * (li->fields_num + ci->values_num));
  if (temp == NULL) {
    WARNING("powerdns plugin: realloc failed.");
    return -1;
  }
  li->fields = temp;

  for (int i = 0; i < ci->values_num; i++) {
    li->fields[li->fields_num] = strdup(ci->values[i].value.string);
    if (li->fields[li->fields_num] == NULL) {
      WARNING("powerdns plugin: strdup failed.");
      continue;
    }
    li->fields_num++;
  }

  /* Invalidate a previously computed command */
  sfree(li->command);

  return 0;
} /* }}} int powerdns_config_add_collect */

static int powerdns_config_add_server(oconfig_item_t *ci) /* {{{ */
{
  char *socket_temp;

  list_item_t *item;
  int status;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("powerdns plugin: `%s' needs exactly one string argument.",
            ci->key);
    return -1;
  }

  item = calloc(1, sizeof(*item));
  if (item == NULL) {
    ERROR("powerdns plugin: calloc failed.");
    return -1;
  }

  item->instance = strdup(ci->values[0].value.string);
  if (item->instance == NULL) {
    ERROR("powerdns plugin: strdup failed.");
    sfree(item);
    return -1;
  }

  /*
   * Set default values for the members of list_item_t
   */
  if (strcasecmp("Server", ci->key) == 0) {
    item->server_type = SRV_AUTHORITATIVE;
    item->func = powerdns_read_server;
    item->socktype = SOCK_STREAM;
    socket_temp = strdup(SERVER_SOCKET);
  } else if (strcasecmp("Recursor", ci->key) == 0) {
    item->server_type = SRV_RECURSOR;
    item->func = powerdns_read_recursor;
    item->socktype = SOCK_DGRAM;
    socket_temp = strdup(RECURSOR_SOCKET);
  } else {
    /* We must never get here.. */
    assert(0);
    return -1;
  }

  status = 0;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Collect", option->key) == 0)
      status = powerdns_config_add_collect(item, option);
    else if (strcasecmp("Socket", option->key) == 0)
      status = cf_util_get_string(option, &socket_temp);
    else {
      ERROR("powerdns plugin: Option `%s' not allowed here.", option->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  while (status == 0) {
    llentry_t *e;

    if (socket_temp == NULL) {
      ERROR("powerdns plugin: socket_temp == NULL.");
      status = -1;
      break;
    }

    item->sockaddr.sun_family = AF_UNIX;
    sstrncpy(item->sockaddr.sun_path, socket_temp,
             sizeof(item->sockaddr.sun_path));

    e = llentry_create(item->instance, item);
    if (e == NULL) {
      ERROR("powerdns plugin: llentry_create failed.");
      status = -1;
      break;
    }
    llist_append(list, e);

    break;
  }

  if (status != 0) {
    sfree(socket_temp);
    sfree(item);
    return -1;
  }

  DEBUG("powerdns plugin: Add server: instance = %s;", item->instance);

  sfree(socket_temp);
  return 0;
} /* }}} int powerdns_config_add_server */

static int powerdns_config(oconfig_item_t *ci) /* {{{ */
{
  DEBUG("powerdns plugin: powerdns_config (ci = %p);", (void *)ci);

  if (list == NULL) {
    list = llist_create();

    if (list == NULL) {
      ERROR("powerdns plugin: `llist_create' failed.");
      return -1;
    }
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if ((strcasecmp("Server", option->key) == 0) ||
        (strcasecmp("Recursor", option->key) == 0))
      powerdns_config_add_server(option);
    else if (strcasecmp("LocalSocket", option->key) == 0) {
      if ((option->values_num != 1) ||
          (option->values[0].type != OCONFIG_TYPE_STRING)) {
        WARNING("powerdns plugin: `%s' needs exactly one string argument.",
                option->key);
      } else {
        char *temp = strdup(option->values[0].value.string);
        if (temp == NULL)
          return 1;
        sfree(local_sockpath);
        local_sockpath = temp;
      }
    } else {
      ERROR("powerdns plugin: Option `%s' not allowed here.", option->key);
    }
  } /* for (i = 0; i < ci->children_num; i++) */

  return 0;
} /* }}} int powerdns_config */

static int powerdns_read(void) {
  for (llentry_t *e = llist_head(list); e != NULL; e = e->next) {
    list_item_t *item = e->value;
    item->func(item);
  }

  return 0;
} /* static int powerdns_read */

static int powerdns_shutdown(void) {
  if (list == NULL)
    return 0;

  for (llentry_t *e = llist_head(list); e != NULL; e = e->next) {
    list_item_t *item = (list_item_t *)e->value;
    e->value = NULL;

    sfree(item->instance);
    sfree(item->command);
    sfree(item);
  }

  llist_destroy(list);
  list = NULL;

  return 0;
} /* static int powerdns_shutdown */

void module_register(void) {
  plugin_register_complex_config("powerdns", powerdns_config);
  plugin_register_read("powerdns", powerdns_read);
  plugin_register_shutdown("powerdns", powerdns_shutdown);
} /* void module_register */
