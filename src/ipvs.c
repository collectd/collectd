/**
 * collectd - src/ipvs.c (based on ipvsadm and libipvs)
 * Copyright (C) 1997  Steven Clarke <steven@monmouth.demon.co.uk>
 * Copyright (C) 1998-2004  Wensong Zhang <wensong@linuxvirtualserver.org>
 * Copyright (C) 2003-2004  Peter Kese <peter.kese@ijs.si>
 * Copyright (C) 2007  Sebastian Harl
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors:
 *   Sebastian Harl <sh at tokkee.org>
 **/

/*
 * This plugin collects statistics about IPVS connections. It requires Linux
 * kernels >= 2.6.
 *
 * See http://www.linuxvirtualserver.org/software/index.html for more
 * information about IPVS.
 */

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif /* HAVE_ARPA_INET_H */
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif /* HAVE_NETINET_IN_H */

#include <linux/ip_vs.h>

#define log_err(...) ERROR("ipvs: " __VA_ARGS__)
#define log_info(...) INFO("ipvs: " __VA_ARGS__)

/*
 * private variables
 */
static int sockfd = -1;

/*
 * libipvs API
 */
static struct ip_vs_get_services *ipvs_get_services(void) {
  struct ip_vs_getinfo ipvs_info;
  struct ip_vs_get_services *services;

  socklen_t len = sizeof(ipvs_info);

  if (getsockopt(sockfd, IPPROTO_IP, IP_VS_SO_GET_INFO, &ipvs_info, &len) ==
      -1) {
    log_err("ip_vs_get_services: getsockopt() failed: %s", STRERRNO);
    return NULL;
  }

  len = sizeof(*services) +
        sizeof(struct ip_vs_service_entry) * ipvs_info.num_services;

  services = malloc(len);
  if (services == NULL) {
    log_err("ipvs_get_services: Out of memory.");
    return NULL;
  }

  services->num_services = ipvs_info.num_services;

  if (getsockopt(sockfd, IPPROTO_IP, IP_VS_SO_GET_SERVICES, services, &len) ==
      -1) {
    log_err("ipvs_get_services: getsockopt failed: %s", STRERRNO);

    free(services);
    return NULL;
  }
  return services;
} /* ipvs_get_services */

static struct ip_vs_get_dests *ipvs_get_dests(struct ip_vs_service_entry *se) {
  struct ip_vs_get_dests *dests;

  socklen_t len =
      sizeof(*dests) + sizeof(struct ip_vs_dest_entry) * se->num_dests;

  dests = malloc(len);
  if (dests == NULL) {
    log_err("ipvs_get_dests: Out of memory.");
    return NULL;
  }

  dests->fwmark = se->fwmark;
  dests->protocol = se->protocol;
  dests->addr = se->addr;
  dests->port = se->port;
  dests->num_dests = se->num_dests;

  if (getsockopt(sockfd, IPPROTO_IP, IP_VS_SO_GET_DESTS, dests, &len) == -1) {
    log_err("ipvs_get_dests: getsockopt() failed: %s", STRERRNO);
    free(dests);
    return NULL;
  }
  return dests;
} /* ip_vs_get_dests */

/*
 * collectd plugin API and helper functions
 */
static int cipvs_init(void) {
  struct ip_vs_getinfo ipvs_info;

  if ((sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) == -1) {
    log_err("cipvs_init: socket() failed: %s", STRERRNO);
    return -1;
  }

  socklen_t len = sizeof(ipvs_info);

  if (getsockopt(sockfd, IPPROTO_IP, IP_VS_SO_GET_INFO, &ipvs_info, &len) ==
      -1) {
    log_err("cipvs_init: getsockopt() failed: %s", STRERRNO);
    close(sockfd);
    sockfd = -1;
    return -1;
  }

  /* we need IPVS >= 1.1.4 */
  if (ipvs_info.version < ((1 << 16) + (1 << 8) + 4)) {
    log_err("cipvs_init: IPVS version too old (%d.%d.%d < %d.%d.%d)",
            NVERSION(ipvs_info.version), 1, 1, 4);
    close(sockfd);
    sockfd = -1;
    return -1;
  } else {
    log_info("Successfully connected to IPVS %d.%d.%d",
             NVERSION(ipvs_info.version));
  }
  return 0;
} /* cipvs_init */

/*
 * ipvs-<virtual IP>_{UDP,TCP}<port>/<type>-total
 * ipvs-<virtual IP>_{UDP,TCP}<port>/<type>-<real IP>_<port>
 */

/* plugin instance */
static int get_pi(struct ip_vs_service_entry *se, char *pi, size_t size) {
  if ((se == NULL) || (pi == NULL))
    return 0;

  struct in_addr addr = {.s_addr = se->addr};

  int len =
      snprintf(pi, size, "%s_%s%u", inet_ntoa(addr),
               (se->protocol == IPPROTO_TCP) ? "TCP" : "UDP", ntohs(se->port));

  if ((len < 0) || (size <= ((size_t)len))) {
    log_err("plugin instance truncated: %s", pi);
    return -1;
  }
  return 0;
} /* get_pi */

/* type instance */
static int get_ti(struct ip_vs_dest_entry *de, char *ti, size_t size) {
  if ((de == NULL) || (ti == NULL))
    return 0;

  struct in_addr addr = {.s_addr = de->addr};

  int len = snprintf(ti, size, "%s_%u", inet_ntoa(addr), ntohs(de->port));

  if ((len < 0) || (size <= ((size_t)len))) {
    log_err("type instance truncated: %s", ti);
    return -1;
  }
  return 0;
} /* get_ti */

static void cipvs_submit_connections(const char *pi, const char *ti,
                                     derive_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.derive = value};
  vl.values_len = 1;

  sstrncpy(vl.plugin, "ipvs", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, pi, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "connections", sizeof(vl.type));
  sstrncpy(vl.type_instance, (ti != NULL) ? ti : "total",
           sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* cipvs_submit_connections */

static void cipvs_submit_if(const char *pi, const char *t, const char *ti,
                            derive_t rx, derive_t tx) {
  value_t values[] = {
      {.derive = rx}, {.derive = tx},
  };
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);

  sstrncpy(vl.plugin, "ipvs", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, pi, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, t, sizeof(vl.type));
  sstrncpy(vl.type_instance, (ti != NULL) ? ti : "total",
           sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* cipvs_submit_if */

static void cipvs_submit_dest(const char *pi, struct ip_vs_dest_entry *de) {
  struct ip_vs_stats_user stats = de->stats;

  char ti[DATA_MAX_NAME_LEN];

  if (get_ti(de, ti, sizeof(ti)) != 0)
    return;

  cipvs_submit_connections(pi, ti, stats.conns);
  cipvs_submit_if(pi, "if_packets", ti, stats.inpkts, stats.outpkts);
  cipvs_submit_if(pi, "if_octets", ti, stats.inbytes, stats.outbytes);
} /* cipvs_submit_dest */

static void cipvs_submit_service(struct ip_vs_service_entry *se) {
  struct ip_vs_stats_user stats = se->stats;
  struct ip_vs_get_dests *dests = ipvs_get_dests(se);

  char pi[DATA_MAX_NAME_LEN];

  if (get_pi(se, pi, sizeof(pi)) != 0) {
    free(dests);
    return;
  }

  cipvs_submit_connections(pi, NULL, stats.conns);
  cipvs_submit_if(pi, "if_packets", NULL, stats.inpkts, stats.outpkts);
  cipvs_submit_if(pi, "if_octets", NULL, stats.inbytes, stats.outbytes);

  for (size_t i = 0; i < dests->num_dests; ++i)
    cipvs_submit_dest(pi, &dests->entrytable[i]);

  free(dests);
  return;
} /* cipvs_submit_service */

static int cipvs_read(void) {
  if (sockfd < 0)
    return -1;

  struct ip_vs_get_services *services = ipvs_get_services();
  if (services == NULL)
    return -1;

  for (size_t i = 0; i < services->num_services; ++i)
    cipvs_submit_service(&services->entrytable[i]);

  free(services);
  return 0;
} /* cipvs_read */

static int cipvs_shutdown(void) {
  if (sockfd >= 0)
    close(sockfd);
  sockfd = -1;

  return 0;
} /* cipvs_shutdown */

void module_register(void) {
  plugin_register_init("ipvs", cipvs_init);
  plugin_register_read("ipvs", cipvs_read);
  plugin_register_shutdown("ipvs", cipvs_shutdown);
  return;
} /* module_register */
