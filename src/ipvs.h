#include <linux/types.h> 
#include <libnl3/netlink/genl/genl.h>


#include <arpa/inet.h>
#include <linux/types.h> /* For __beXX types in userland */
#include <netinet/in.h>
#include <sys/socket.h>

#define IPVS_INFO_ATTR_MAX (__IPVS_INFO_ATTR_MAX - 1)




union nf_inet_addr {
  __u32 all[4];
  __be32 ip;
  __be32 ip6[4];
  struct in_addr in;
  struct in6_addr in6;
};


/*
 *  *  *   IPVS statistics object (for user space), 64-bit
 *   *   */
struct ip_vs_stats64 {
  __u64 conns;    /* connections scheduled */
  __u64 inpkts;   /* incoming packets */
  __u64 outpkts;  /* outgoing packets */
  __u64 inbytes;  /* incoming bytes */
  __u64 outbytes; /* outgoing bytes */

  __u64 cps;    /* current connection rate */
  __u64 inpps;  /* current in packet rate */
  __u64 outpps; /* current out packet rate */
  __u64 inbps;  /* current in byte rate */
  __u64 outbps; /* current out byte rate */
};



/*
 * Strcut used for ipv6 addreses
 */
struct ip_vs_service_entry_nl {
  /* which service: user fills in these */
  u_int16_t protocol;
  __be32 __addr_v4; /* virtual address - internal use only*/
  __be16 port;
  u_int32_t fwmark; /* firwall mark of service */

  /* service options */
  char sched_name[IP_VS_SCHEDNAME_MAXLEN];
  unsigned flags;   /* virtual service flags */
  unsigned timeout; /* persistent timeout */
  __be32 netmask;   /* persistent netmask */

  /* number of real servers */
  unsigned int num_dests;

  /* statistics */
  struct ip_vs_stats_user stats;

  u_int16_t af;
  union nf_inet_addr addr;
  char pe_name[IP_VS_PENAME_MAXLEN];

  /* statistics, 64-bit */
  struct ip_vs_stats64 stats64;
};

struct ip_vs_dest_entry_nl {
  __be32 __addr_v4; /* destination address - internal use only */
  __be16 port;
  unsigned conn_flags; /* connection flags */
  int weight;          /* destination weight */

  u_int32_t u_threshold; /* upper threshold */
  u_int32_t l_threshold; /* lower threshold */

  u_int32_t activeconns;  /* active connections */
  u_int32_t inactconns;   /* inactive connections */
  u_int32_t persistconns; /* persistent connections */

  /* statistics */
  struct ip_vs_stats_user stats;
  u_int16_t af;
  union nf_inet_addr addr;

  /* statistics, 64-bit */
  struct ip_vs_stats64 stats64;
};

struct ip_vs_get_dests_nl {
  /* which service: user fills in these */
  u_int16_t protocol;
  __be32 __addr_v4; /* virtual address - internal use only */
  __be16 port;
  u_int32_t fwmark; /* firwall mark of service */

  /* number of real servers */
  unsigned int num_dests;
  u_int16_t af;
  union nf_inet_addr addr;

  /* the real servers */
  struct ip_vs_dest_entry_nl entrytable[0];
};

/* The argument to IP_VS_SO_GET_SERVICES */
struct ip_vs_get_services_nl {
  /* number of virtual services */
  unsigned int num_services;

  /* service table */
  struct ip_vs_service_entry_nl entrytable[0];
};





extern struct nla_policy ipvs_cmd_policy[IPVS_CMD_ATTR_MAX + 1];
extern struct nla_policy ipvs_service_policy[IPVS_SVC_ATTR_MAX + 1];
extern struct nla_policy ipvs_dest_policy[IPVS_DEST_ATTR_MAX + 1];
extern struct nla_policy ipvs_stats_policy[IPVS_STATS_ATTR_MAX + 1];
extern struct nla_policy ipvs_info_policy[IPVS_INFO_ATTR_MAX + 1];
//extern struct nla_policy ipvs_daemon_policy[IPVS_DAEMON_ATTR_MAX + 1];

/* Policy definitions */
struct nla_policy ipvs_cmd_policy[IPVS_CMD_ATTR_MAX + 1] = {
        [IPVS_CMD_ATTR_SERVICE] = {.type = NLA_NESTED},
        [IPVS_CMD_ATTR_DEST] = {.type = NLA_NESTED},
        [IPVS_CMD_ATTR_DAEMON] = {.type = NLA_NESTED},
        [IPVS_CMD_ATTR_TIMEOUT_TCP] = {.type = NLA_U32},
        [IPVS_CMD_ATTR_TIMEOUT_TCP_FIN] = {.type = NLA_U32},
        [IPVS_CMD_ATTR_TIMEOUT_UDP] = {.type = NLA_U32},
};


struct nla_policy ipvs_info_policy[IPVS_INFO_ATTR_MAX + 1] = {
        [IPVS_INFO_ATTR_VERSION] = {.type = NLA_U32},
        [IPVS_INFO_ATTR_CONN_TAB_SIZE] = {.type = NLA_U32},
};

struct nla_policy ipvs_service_policy[IPVS_SVC_ATTR_MAX + 1] = {
        [IPVS_SVC_ATTR_AF] = {.type = NLA_U16},
        [IPVS_SVC_ATTR_PROTOCOL] = {.type = NLA_U16},
        [IPVS_SVC_ATTR_ADDR] = {.type = NLA_UNSPEC,
                                .maxlen = sizeof(struct in6_addr)},
        [IPVS_SVC_ATTR_PORT] = {.type = NLA_U16},
        [IPVS_SVC_ATTR_FWMARK] = {.type = NLA_U32},
        [IPVS_SVC_ATTR_SCHED_NAME] = {.type = NLA_STRING,
                                      .maxlen = IP_VS_SCHEDNAME_MAXLEN},
        [IPVS_SVC_ATTR_FLAGS] = {.type = NLA_UNSPEC,
                                 .minlen = sizeof(struct ip_vs_flags),
                                 .maxlen = sizeof(struct ip_vs_flags)},
        [IPVS_SVC_ATTR_TIMEOUT] = {.type = NLA_U32},
        [IPVS_SVC_ATTR_NETMASK] = {.type = NLA_U32},
        [IPVS_SVC_ATTR_STATS] = {.type = NLA_NESTED},
};

struct nla_policy ipvs_stats_policy[IPVS_STATS_ATTR_MAX + 1] = {
        [IPVS_STATS_ATTR_CONNS] = {.type = NLA_U32},
        [IPVS_STATS_ATTR_INPKTS] = {.type = NLA_U32},
        [IPVS_STATS_ATTR_OUTPKTS] = {.type = NLA_U32},
        [IPVS_STATS_ATTR_INBYTES] = {.type = NLA_U64},
        [IPVS_STATS_ATTR_OUTBYTES] = {.type = NLA_U64},
        [IPVS_STATS_ATTR_CPS] = {.type = NLA_U32},
        [IPVS_STATS_ATTR_INPPS] = {.type = NLA_U32},
        [IPVS_STATS_ATTR_OUTPPS] = {.type = NLA_U32},
        [IPVS_STATS_ATTR_INBPS] = {.type = NLA_U32},
        [IPVS_STATS_ATTR_OUTBPS] = {.type = NLA_U32},
};

struct nla_policy ipvs_dest_policy[IPVS_DEST_ATTR_MAX + 1] = {
        [IPVS_DEST_ATTR_ADDR] = {.type = NLA_UNSPEC,
                                 .maxlen = sizeof(struct in6_addr)},
        [IPVS_DEST_ATTR_PORT] = {.type = NLA_U16},
        [IPVS_DEST_ATTR_FWD_METHOD] = {.type = NLA_U32},
        [IPVS_DEST_ATTR_WEIGHT] = {.type = NLA_U32},
        [IPVS_DEST_ATTR_U_THRESH] = {.type = NLA_U32},
        [IPVS_DEST_ATTR_L_THRESH] = {.type = NLA_U32},
        [IPVS_DEST_ATTR_ACTIVE_CONNS] = {.type = NLA_U32},
        [IPVS_DEST_ATTR_INACT_CONNS] = {.type = NLA_U32},
        [IPVS_DEST_ATTR_PERSIST_CONNS] = {.type = NLA_U32},
        [IPVS_DEST_ATTR_STATS] = {.type = NLA_NESTED},
};
