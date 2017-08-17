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



//add our structures
#include <ipvs.h>

#define log_err(...) ERROR("ipvs: " __VA_ARGS__)
#define log_info(...) INFO("ipvs: " __VA_ARGS__)

//#define nl_sock nl_handle
//#define nl_socket_alloc nl_handle_alloc
//#define nl_socket_free nl_handle_destroy

#include <libnl3/netlink/genl/ctrl.h>
#include <libnl3/netlink/genl/genl.h>
#include <libnl3/netlink/msg.h>
#include <libnl3/netlink/netlink.h>
#include <libnl3/netlink/socket.h>



static struct nl_sock *sock = NULL;
static int family;

/*
 * private variables
 */
//static int sockfd = -1;
struct ip_vs_getinfo ipvs_info;

//
// static struct ip_vs_get_dests *ipvs_get_dests(struct ip_vs_service_entry *se) {
//   struct ip_vs_get_dests *dests;
//
//   socklen_t len =
//       sizeof(*dests) + sizeof(struct ip_vs_dest_entry) * se->num_dests;
//
//   dests = malloc(len);
//   if (dests == NULL) {
//     log_err("ipvs_get_dests: Out of memory.");
//     return NULL;
//   }
//
//   dests->fwmark = se->fwmark;
//   dests->protocol = se->protocol;
//   dests->addr = se->addr;
//   dests->port = se->port;
//   dests->num_dests = se->num_dests;
//
//   if (getsockopt(sockfd, IPPROTO_IP, IP_VS_SO_GET_DESTS, dests, &len) == -1) {
//     char errbuf[1024];
//     log_err("ipvs_get_dests: getsockopt() failed: %s",
//             sstrerror(errno, errbuf, sizeof(errbuf)));
//     free(dests);
//     return NULL;
//   }
//   return dests;
// } /* ip_vs_get_dests */
//

/*
 * libipvs API
 */
struct nl_msg *ipvs_nl_message(int cmd, int flags) {
  struct nl_msg *msg;

  msg = nlmsg_alloc();
  if (!msg)
    return NULL;

  genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, flags, cmd,
              IPVS_GENL_VERSION);

  return msg;
}

static int ipvs_nl_send_message(struct nl_msg *msg, nl_recvmsg_msg_cb_t func,
                                void *arg) {
  int err = EINVAL;

  sock = nl_socket_alloc();
  if (!sock) {
    nlmsg_free(msg);
    return -1;
  }

  if (genl_connect(sock) < 0)
    goto fail_genl;

  family = genl_ctrl_resolve(sock, IPVS_GENL_NAME);
  if (family < 0)
    goto fail_genl;

  // To test connections and set the family
  if (msg == NULL) {
    nl_socket_free(sock);
    sock = NULL;
    return 0;
  }

  if (nl_socket_modify_cb(sock, NL_CB_VALID, NL_CB_CUSTOM, func, arg) != 0)
    goto fail_genl;

  if (nl_send_auto_complete(sock, msg) < 0)
    goto fail_genl;

  if ((err = -nl_recvmsgs_default(sock)) > 0)
    goto fail_genl;

  nlmsg_free(msg);

  nl_socket_free(sock);

  return 0;

fail_genl:
  nl_socket_free(sock);
  sock = NULL;
  nlmsg_free(msg);
  errno = err;
  return -1;
}

static int ipvs_getinfo_parse_cb(struct nl_msg *msg, void *arg) {
  struct nlmsghdr *nlh = nlmsg_hdr(msg);
  struct nlattr *attrs[IPVS_INFO_ATTR_MAX + 1];

  if (genlmsg_parse(nlh, 0, attrs, IPVS_INFO_ATTR_MAX, ipvs_info_policy) != 0)
    return -1;

  if (!(attrs[IPVS_INFO_ATTR_VERSION] && attrs[IPVS_INFO_ATTR_CONN_TAB_SIZE]))
    return -1;

  ipvs_info.version = nla_get_u32(attrs[IPVS_INFO_ATTR_VERSION]);
  ipvs_info.size = nla_get_u32(attrs[IPVS_INFO_ATTR_CONN_TAB_SIZE]);

  return NL_OK;
}


static int ipvs_services_parse_cb(struct nl_msg *msg, void *arg) {
  struct nlmsghdr *nlh = nlmsg_hdr(msg);
  struct nlattr *attrs[IPVS_CMD_ATTR_MAX + 1];
  struct nlattr *svc_attrs[IPVS_SVC_ATTR_MAX + 1];
  struct ip_vs_get_services_nl **getp = (struct ip_vs_get_services_nl **)arg;
  struct ip_vs_get_services_nl *get = (struct ip_vs_get_services_nl *)*getp;
  struct ip_vs_flags flags;
  int i = get->num_services;

  if (genlmsg_parse(nlh, 0, attrs, IPVS_CMD_ATTR_MAX, ipvs_cmd_policy) != 0)
    return -1;

  if (!attrs[IPVS_CMD_ATTR_SERVICE])
    return -1;

  if (nla_parse_nested(svc_attrs, IPVS_SVC_ATTR_MAX,
                       attrs[IPVS_CMD_ATTR_SERVICE], ipvs_service_policy))
    return -1;

  memset(&(get->entrytable[i]), 0, sizeof(get->entrytable[i]));

  if (!(svc_attrs[IPVS_SVC_ATTR_AF] &&
        (svc_attrs[IPVS_SVC_ATTR_FWMARK] ||
         (svc_attrs[IPVS_SVC_ATTR_PROTOCOL] && svc_attrs[IPVS_SVC_ATTR_ADDR] &&
          svc_attrs[IPVS_SVC_ATTR_PORT])) &&
        svc_attrs[IPVS_SVC_ATTR_SCHED_NAME] &&
        svc_attrs[IPVS_SVC_ATTR_NETMASK] && svc_attrs[IPVS_SVC_ATTR_TIMEOUT] &&
        svc_attrs[IPVS_SVC_ATTR_FLAGS]))
    return -1;

  get->entrytable[i].af = nla_get_u16(svc_attrs[IPVS_SVC_ATTR_AF]);

  if (svc_attrs[IPVS_SVC_ATTR_FWMARK])
    get->entrytable[i].fwmark = nla_get_u32(svc_attrs[IPVS_SVC_ATTR_FWMARK]);
  else {
    get->entrytable[i].protocol =
        nla_get_u16(svc_attrs[IPVS_SVC_ATTR_PROTOCOL]);
    memcpy(&(get->entrytable[i].addr), nla_data(svc_attrs[IPVS_SVC_ATTR_ADDR]),
           sizeof(get->entrytable[i].addr));
    get->entrytable[i].port = nla_get_u16(svc_attrs[IPVS_SVC_ATTR_PORT]);
  }

  strncpy(get->entrytable[i].sched_name,
          nla_get_string(svc_attrs[IPVS_SVC_ATTR_SCHED_NAME]),
          IP_VS_SCHEDNAME_MAXLEN);

  if (svc_attrs[IPVS_SVC_ATTR_PE_NAME])
    strncpy(get->entrytable[i].pe_name,
            nla_get_string(svc_attrs[IPVS_SVC_ATTR_PE_NAME]),
       i  IP_VS_PENAME_MAXLEN);

  get->entrytable[i].netmask = nla_get_u32(svc_attrs[IPVS_SVC_ATTR_NETMASK]);
  get->entrytable[i].timeout = nla_get_u32(svc_attrs[IPVS_SVC_ATTR_TIMEOUT]);
  nla_memcpy(&flags, svc_attrs[IPVS_SVC_ATTR_FLAGS], sizeof(flags));
  get->entrytable[i].flags = flags.flags & flags.mask;

  if (svc_attrs[IPVS_SVC_ATTR_STATS64]) {
    if (ipvs_parse_stats64(&get->entrytable[i].stats64,
                           svc_attrs[IPVS_SVC_ATTR_STATS64]) != 0)
      return -1;
  } else if (svc_attrs[IPVS_SVC_ATTR_STATS]) {
    if (ipvs_parse_stats(&get->entrytable[i].stats64,
                         svc_attrs[IPVS_SVC_ATTR_STATS]) != 0)
      return -1;
  }

  get->entrytable[i].num_dests = 0;

  i++;

  get->num_services = i;
  get = realloc(get, sizeof(*get) +
                         sizeof(struct ip_vs_service_entry_nl) *
                             (get->num_services + 1));
  *getp = get;
  
return 0;
}
/*
 *  * libipvs API
 *   */
static struct ip_vs_get_services_nl *ipvs_get_services(void) {
          // struct ip_vs_getinfo ipvs_info;
           struct ip_vs_get_services_nl *services;

        socklen_t len;

 struct nl_msg *msg;
    len = sizeof(*services) + sizeof(struct ip_vs_service_entry_nl);
    if (!(services = malloc(len)))
      return NULL;

    services->num_services = 0;

    msg = ipvs_nl_message(IPVS_CMD_GET_SERVICE, NLM_F_DUMP);

    if (msg && (ipvs_nl_send_message(msg, ipvs_services_parse_cb, &services) == 0)) {
      return services; //todo: check we free this elsewhere
    }
    free(services);
    return NULL;
} /* ipvs_get_services */

/*
* collectd plugin API and helper functions
*/
static int cipvs_init(void) {

  /*Test we can use netlink*/
  if (ipvs_nl_send_message(NULL, NULL, NULL) != 0 ) {
	log_err("Netlink test failed");
	return -1;
  }
    //TODO: error here
    struct nl_msg *msg;
    msg = ipvs_nl_message(IPVS_CMD_GET_INFO, 0);
    if (msg) {
      ipvs_nl_send_message(msg, ipvs_getinfo_parse_cb, NULL);
    } else {
      return -1;
    }

/* we need IPVS >= 1.1.4 */
  if (ipvs_info.version < ((1 << 16) + (1 << 8) + 4)) {
    log_err("cipvs_init: IPVS version too old (%d.%d.%d < %d.%d.%d)",
            NVERSION(ipvs_info.version), 1, 1, 4);
    return -1;
  } else {
    log_info("Successfully connected to IPVS %d.%d.%d",
             NVERSION(ipvs_info.version));
  }
  return 0;

} /* cipvs_init */



//
// /*
//  * ipvs-<virtual IP>_{UDP,TCP}<port>/<type>-total
//  * ipvs-<virtual IP>_{UDP,TCP}<port>/<type>-<real IP>_<port>
//  */
//
// /* plugin instance */
// static int get_pi(struct ip_vs_service_entry *se, char *pi, size_t size) {
//   if ((se == NULL) || (pi == NULL))
//     return 0;
//
//   struct in_addr addr = {.s_addr = se->addr};
//
//   int len =
//   if ((len < 0) || (size <= ((size_t)len))) {
//     log_err("plugin instance truncated: %s", pi);
//     return -1;
//   }
//   return 0;
// } /* get_pi */
//
// /* type instance */
// static int get_ti(struct ip_vs_dest_entry *de, char *ti, size_t size) {
//   if ((de == NULL) || (ti == NULL))
//     return 0;
//
//   struct in_addr addr = {.s_addr = de->addr};
//
//   int len = snprintf(ti, size, "%s_%u", inet_ntoa(addr), ntohs(de->port));
//
//   if ((len < 0) || (size <= ((size_t)len))) {
//     log_err("type instance truncated: %s", ti);
//     return -1;
//   }
//   return 0;
// } /* get_ti */
//
// static void cipvs_submit_connections(const char *pi, const char *ti,
//                                      derive_t value) {
//   value_list_t vl = VALUE_LIST_INIT;
//
//   vl.values = &(value_t){.derive = value};
//   vl.values_len = 1;
//
//   sstrncpy(vl.plugin, "ipvs", sizeof(vl.plugin));
//   sstrncpy(vl.plugin_instance, pi, sizeof(vl.plugin_instance));
//   sstrncpy(vl.type, "connections", sizeof(vl.type));
//   sstrncpy(vl.type_instance, (ti != NULL) ? ti : "total",
//            sizeof(vl.type_instance));
//
//   plugin_dispatch_values(&vl);
// } /* cipvs_submit_connections */
//
// static void cipvs_submit_if(const char *pi, const char *t, const char *ti,
//                             derive_t rx, derive_t tx) {
//   value_t values[] = {
//       {.derive = rx}, {.derive = tx},
//   };
//   value_list_t vl = VALUE_LIST_INIT;
//
//   vl.values = values;
//   vl.values_len = STATIC_ARRAY_SIZE(values);
//
//   sstrncpy(vl.plugin, "ipvs", sizeof(vl.plugin));
//   sstrncpy(vl.plugin_instance, pi, sizeof(vl.plugin_instance));
//   sstrncpy(vl.type, t, sizeof(vl.type));
//   sstrncpy(vl.type_instance, (ti != NULL) ? ti : "total",
//            sizeof(vl.type_instance));
//
//   plugin_dispatch_values(&vl);
// } /* cipvs_submit_if */
//
// static void cipvs_submit_dest(const char *pi, struct ip_vs_dest_entry *de) {
//   struct ip_vs_stats_user stats = de->stats;
//
//   char ti[DATA_MAX_NAME_LEN];
//
//   if (get_ti(de, ti, sizeof(ti)) != 0)
//     return;
//
//   cipvs_submit_connections(pi, ti, stats.conns);
//   cipvs_submit_if(pi, "if_packets", ti, stats.inpkts, stats.outpkts);
//   cipvs_submit_if(pi, "if_octets", ti, stats.inbytes, stats.outbytes);
// } /* cipvs_submit_dest */
//
// static void cipvs_submit_service(struct ip_vs_service_entry *se) {
//   struct ip_vs_stats_user stats = se->stats;
//   struct ip_vs_get_dests *dests = ipvs_get_dests(se);
//
//   char pi[DATA_MAX_NAME_LEN];
//
//   if (get_pi(se, pi, sizeof(pi)) != 0) {
//     free(dests);
//     return;
//   }
//
//   cipvs_submit_connections(pi, NULL, stats.conns);
//   cipvs_submit_if(pi, "if_packets", NULL, stats.inpkts, stats.outpkts);
//   cipvs_submit_if(pi, "if_octets", NULL, stats.inbytes, stats.outbytes);
//
//   for (size_t i = 0; i < dests->num_dests; ++i)
//     cipvs_submit_dest(pi, &dests->entrytable[i]);
//
//   free(dests);
//   return;
//} /* cipvs_submit_service */

static int cipvs_read(void) {
   struct ip_vs_get_services_nl *services = ipvs_get_services();
   if (services == NULL)
     return -1;
//
//   for (size_t i = 0; i < services->num_services; ++i)
//     cipvs_submit_service(&services->entrytable[i]);
//
//  free(services);
  return 0;
} /* cipvs_read */

static int cipvs_shutdown(void) {
 // if (sock != NULL)
   // nl_socket_free(sock);
//  sockfd = -1;
  return 0;
} /* cipvs_shutdown */

void module_register(void) {
  plugin_register_init("ipvs", cipvs_init);
  plugin_register_read("ipvs", cipvs_read);
  plugin_register_shutdown("ipvs", cipvs_shutdown);
  return;
} /* module_register */
