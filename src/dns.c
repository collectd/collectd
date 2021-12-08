/**
 * collectd - src/dns.c
 * Copyright (C) 2006-2011  Florian octo Forster
 * Copyright (C) 2009       Mirko Buffoni
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
 *   Florian octo Forster <octo at collectd.org>
 *   Mirko Buffoni <briareos at eswat.org>
 **/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include "utils/dns/dns.h"
#include <poll.h>

#include <pcap.h>

#ifdef HAVE_SYS_CAPABILITY_H
#include <sys/capability.h>
#endif

/*
 * Private data types
 */
struct counter_list_s {
  unsigned int key;
  unsigned int value;
  struct counter_list_s *next;
};
typedef struct counter_list_s counter_list_t;

/*
 * Private variables
 */
static const char *config_keys[] = {"Interface", "IgnoreSource",
                                    "SelectNumericQueryTypes"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);
static int select_numeric_qtype = 1;

#define PCAP_SNAPLEN 1460
static char *pcap_device;

static derive_t tr_queries;
static derive_t tr_responses;
static counter_list_t *qtype_list;
static counter_list_t *opcode_list;
static counter_list_t *rcode_list;

static pthread_t listen_thread;
static int listen_thread_init;
/* The `traffic' mutex if for `tr_queries' and `tr_responses' */
static pthread_mutex_t traffic_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t qtype_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t opcode_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t rcode_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Private functions
 */
static counter_list_t *counter_list_search(counter_list_t **list,
                                           unsigned int key) {
  counter_list_t *entry;

  for (entry = *list; entry != NULL; entry = entry->next)
    if (entry->key == key)
      break;

  return entry;
}

static counter_list_t *counter_list_create(counter_list_t **list,
                                           unsigned int key,
                                           unsigned int value) {
  counter_list_t *entry;

  entry = calloc(1, sizeof(*entry));
  if (entry == NULL)
    return NULL;

  entry->key = key;
  entry->value = value;

  if (*list == NULL) {
    *list = entry;
  } else {
    counter_list_t *last;

    last = *list;
    while (last->next != NULL)
      last = last->next;

    last->next = entry;
  }

  return entry;
}

static void counter_list_add(counter_list_t **list, unsigned int key,
                             unsigned int increment) {
  counter_list_t *entry;

  entry = counter_list_search(list, key);

  if (entry != NULL) {
    entry->value += increment;
  } else {
    counter_list_create(list, key, increment);
  }
}

static int dns_config(const char *key, const char *value) {
  if (strcasecmp(key, "Interface") == 0) {
    if (pcap_device != NULL)
      free(pcap_device);
    if ((pcap_device = strdup(value)) == NULL)
      return 1;
  } else if (strcasecmp(key, "IgnoreSource") == 0) {
    if (value != NULL)
      ignore_list_add_name(value);
  } else if (strcasecmp(key, "SelectNumericQueryTypes") == 0) {
    if ((value != NULL) && IS_FALSE(value))
      select_numeric_qtype = 0;
    else
      select_numeric_qtype = 1;
  } else {
    return -1;
  }

  return 0;
}

static void dns_child_callback(const rfc1035_header_t *dns) {
  if (dns->qr == 0) {
    /* This is a query */
    int skip = 0;
    if (!select_numeric_qtype) {
      const char *str = qtype_str(dns->qtype);
      if ((str == NULL) || (str[0] == '#'))
        skip = 1;
    }

    pthread_mutex_lock(&traffic_mutex);
    tr_queries += dns->length;
    pthread_mutex_unlock(&traffic_mutex);

    if (skip == 0) {
      pthread_mutex_lock(&qtype_mutex);
      counter_list_add(&qtype_list, dns->qtype, 1);
      pthread_mutex_unlock(&qtype_mutex);
    }
  } else {
    /* This is a reply */
    pthread_mutex_lock(&traffic_mutex);
    tr_responses += dns->length;
    pthread_mutex_unlock(&traffic_mutex);

    pthread_mutex_lock(&rcode_mutex);
    counter_list_add(&rcode_list, dns->rcode, 1);
    pthread_mutex_unlock(&rcode_mutex);
  }

  /* FIXME: Are queries, replies or both interesting? */
  pthread_mutex_lock(&opcode_mutex);
  counter_list_add(&opcode_list, dns->opcode, 1);
  pthread_mutex_unlock(&opcode_mutex);
}

static int dns_run_pcap_loop(void) {
  pcap_t *pcap_obj;
  char pcap_error[PCAP_ERRBUF_SIZE];
  struct bpf_program fp = {0};

  int status;

  /* Don't block any signals */
  {
    sigset_t sigmask;
    sigemptyset(&sigmask);
    pthread_sigmask(SIG_SETMASK, &sigmask, NULL);
  }

  /* Passing `pcap_device == NULL' is okay and the same as passign "any" */
  DEBUG("dns plugin: Creating PCAP object..");
  pcap_obj = pcap_open_live((pcap_device != NULL) ? pcap_device : "any",
                            PCAP_SNAPLEN, 0 /* Not promiscuous */,
                            (int)CDTIME_T_TO_MS(plugin_get_interval() / 2),
                            pcap_error);
  if (pcap_obj == NULL) {
    ERROR("dns plugin: Opening interface `%s' "
          "failed: %s",
          (pcap_device != NULL) ? pcap_device : "any", pcap_error);
    return PCAP_ERROR;
  }

  status = pcap_compile(pcap_obj, &fp, "udp port 53", 1, 0);
  if (status < 0) {
    ERROR("dns plugin: pcap_compile failed: %s", pcap_statustostr(status));
    return status;
  }

  status = pcap_setfilter(pcap_obj, &fp);
  if (status < 0) {
    ERROR("dns plugin: pcap_setfilter failed: %s", pcap_statustostr(status));
    return status;
  }

  DEBUG("dns plugin: PCAP object created.");

  dnstop_set_pcap_obj(pcap_obj);
  dnstop_set_callback(dns_child_callback);

  status = pcap_loop(pcap_obj, -1 /* loop forever */,
                     handle_pcap /* callback */, NULL /* user data */);
  INFO("dns plugin: pcap_loop exited with status %i.", status);
  /* We need to handle "PCAP_ERROR" specially because libpcap currently
   * doesn't return PCAP_ERROR_IFACE_NOT_UP for compatibility reasons. */
  if (status == PCAP_ERROR)
    status = PCAP_ERROR_IFACE_NOT_UP;

  pcap_close(pcap_obj);
  return status;
} /* int dns_run_pcap_loop */

static int dns_sleep_one_interval(void) /* {{{ */
{
  struct timespec ts = CDTIME_T_TO_TIMESPEC(plugin_get_interval());
  while (nanosleep(&ts, &ts) != 0) {
    if ((errno == EINTR) || (errno == EAGAIN))
      continue;

    return errno;
  }

  return 0;
} /* }}} int dns_sleep_one_interval */

static void *dns_child_loop(__attribute__((unused)) void *dummy) /* {{{ */
{
  int status;

  while (42) {
    status = dns_run_pcap_loop();
    if (status != PCAP_ERROR_IFACE_NOT_UP)
      break;

    dns_sleep_one_interval();
  }

  if (status != PCAP_ERROR_BREAK)
    ERROR("dns plugin: PCAP returned error %s.", pcap_statustostr(status));

  listen_thread_init = 0;
  return NULL;
} /* }}} void *dns_child_loop */

static int dns_init(void) {
  /* clean up an old thread */
  int status;

  pthread_mutex_lock(&traffic_mutex);
  tr_queries = 0;
  tr_responses = 0;
  pthread_mutex_unlock(&traffic_mutex);

  if (listen_thread_init != 0)
    return -1;

  status = plugin_thread_create(&listen_thread, dns_child_loop, (void *)0,
                                "dns listen");
  if (status != 0) {
    ERROR("dns plugin: pthread_create failed: %s", STRERRNO);
    return -1;
  }

  listen_thread_init = 1;

#if defined(HAVE_SYS_CAPABILITY_H) && defined(CAP_NET_RAW)
  if (check_capability(CAP_NET_RAW) != 0) {
    if (getuid() == 0)
      WARNING("dns plugin: Running collectd as root, but the CAP_NET_RAW "
              "capability is missing. The plugin's read function will probably "
              "fail. Is your init system dropping capabilities?");
    else
      WARNING("dns plugin: collectd doesn't have the CAP_NET_RAW capability. "
              "If you don't want to run collectd as root, try running \"setcap "
              "cap_net_raw=ep\" on the collectd binary.");
  }
#endif

  return 0;
} /* int dns_init */

static void submit_derive(const char *type, const char *type_instance,
                          derive_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.derive = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "dns", sizeof(vl.plugin));
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* void submit_derive */

static void submit_octets(derive_t queries, derive_t responses) {
  value_t values[] = {
      {.derive = queries},
      {.derive = responses},
  };
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);
  sstrncpy(vl.plugin, "dns", sizeof(vl.plugin));
  sstrncpy(vl.type, "dns_octets", sizeof(vl.type));

  plugin_dispatch_values(&vl);
} /* void submit_octets */

static int dns_read(void) {
  unsigned int keys[T_MAX];
  unsigned int values[T_MAX];
  int len;

  counter_list_t *ptr;

  pthread_mutex_lock(&traffic_mutex);
  values[0] = tr_queries;
  values[1] = tr_responses;
  pthread_mutex_unlock(&traffic_mutex);

  if ((values[0] != 0) || (values[1] != 0))
    submit_octets(values[0], values[1]);

  pthread_mutex_lock(&qtype_mutex);
  for (ptr = qtype_list, len = 0; (ptr != NULL) && (len < T_MAX);
       ptr = ptr->next, len++) {
    keys[len] = ptr->key;
    values[len] = ptr->value;
  }
  pthread_mutex_unlock(&qtype_mutex);

  for (int i = 0; i < len; i++) {
    DEBUG("dns plugin: qtype = %u; counter = %u;", keys[i], values[i]);
    submit_derive("dns_qtype", qtype_str(keys[i]), values[i]);
  }

  pthread_mutex_lock(&opcode_mutex);
  for (ptr = opcode_list, len = 0; (ptr != NULL) && (len < T_MAX);
       ptr = ptr->next, len++) {
    keys[len] = ptr->key;
    values[len] = ptr->value;
  }
  pthread_mutex_unlock(&opcode_mutex);

  for (int i = 0; i < len; i++) {
    DEBUG("dns plugin: opcode = %u; counter = %u;", keys[i], values[i]);
    submit_derive("dns_opcode", opcode_str(keys[i]), values[i]);
  }

  pthread_mutex_lock(&rcode_mutex);
  for (ptr = rcode_list, len = 0; (ptr != NULL) && (len < T_MAX);
       ptr = ptr->next, len++) {
    keys[len] = ptr->key;
    values[len] = ptr->value;
  }
  pthread_mutex_unlock(&rcode_mutex);

  for (int i = 0; i < len; i++) {
    DEBUG("dns plugin: rcode = %u; counter = %u;", keys[i], values[i]);
    submit_derive("dns_rcode", rcode_str(keys[i]), values[i]);
  }

  return 0;
} /* int dns_read */

void module_register(void) {
  plugin_register_config("dns", dns_config, config_keys, config_keys_num);
  plugin_register_init("dns", dns_init);
  plugin_register_read("dns", dns_read);
} /* void module_register */
