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

#include "plugin.h"
#include "common.h"

#if HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif /* HAVE_ARPA_INET_H */
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif /* HAVE_NETINET_IN_H */


#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <netlink/msg.h>

//Include our own version of ip_vs.h
#include "ip_vs.h"


#ifdef LIBIPVS_USE_NL
#ifdef FALLBACK_LIBNL1
#define nl_sock         nl_handle
#define nl_socket_alloc nl_handle_alloc
#define nl_socket_free  nl_handle_destroy
#endif
static  struct nl_sock *sock = NULL;
static  int family, try_nl = 1;
typedef struct ip_vs_service_entry      ipvs_service_entry_t;
typedef struct ip_vs_dest_entry         ipvs_dest_entry_t;
#endif


#define log_err(...) ERROR ("ipvs: " __VA_ARGS__)
#define log_info(...) INFO ("ipvs: " __VA_ARGS__)

/*
 * private variables
 */
static int sockfd = -1;
static void* ipvs_func = NULL;
struct ip_vs_getinfo ipvs_info;

#ifdef LIBIPVS_USE_NL
/* Policy definitions */
struct nla_policy ipvs_cmd_policy[IPVS_CMD_ATTR_MAX + 1] = {
	[IPVS_CMD_ATTR_SERVICE]		= { .type = NLA_NESTED },
	[IPVS_CMD_ATTR_DEST]		= { .type = NLA_NESTED },
	[IPVS_CMD_ATTR_DAEMON]		= { .type = NLA_NESTED },
	[IPVS_CMD_ATTR_TIMEOUT_TCP]	= { .type = NLA_U32 },
	[IPVS_CMD_ATTR_TIMEOUT_TCP_FIN]	= { .type = NLA_U32 },
	[IPVS_CMD_ATTR_TIMEOUT_UDP]	= { .type = NLA_U32 },
};

struct nla_policy ipvs_service_policy[IPVS_SVC_ATTR_MAX + 1] = {
	[IPVS_SVC_ATTR_AF]		= { .type = NLA_U16 },
	[IPVS_SVC_ATTR_PROTOCOL]	= { .type = NLA_U16 },
	[IPVS_SVC_ATTR_ADDR]		= { .type = NLA_UNSPEC,
					    .maxlen = sizeof(struct in6_addr) },
	[IPVS_SVC_ATTR_PORT]		= { .type = NLA_U16 },
	[IPVS_SVC_ATTR_FWMARK]		= { .type = NLA_U32 },
	[IPVS_SVC_ATTR_SCHED_NAME]	= { .type = NLA_STRING,
					    .maxlen = IP_VS_SCHEDNAME_MAXLEN },
	[IPVS_SVC_ATTR_FLAGS]		= { .type = NLA_UNSPEC,
					    .minlen = sizeof(struct ip_vs_flags),
					    .maxlen = sizeof(struct ip_vs_flags) },
	[IPVS_SVC_ATTR_TIMEOUT]		= { .type = NLA_U32 },
	[IPVS_SVC_ATTR_NETMASK]		= { .type = NLA_U32 },
	[IPVS_SVC_ATTR_STATS]		= { .type = NLA_NESTED },
};

struct nla_policy ipvs_dest_policy[IPVS_DEST_ATTR_MAX + 1] = {
	[IPVS_DEST_ATTR_ADDR]		= { .type = NLA_UNSPEC,
					    .maxlen = sizeof(struct in6_addr) },
	[IPVS_DEST_ATTR_PORT]		= { .type = NLA_U16 },
	[IPVS_DEST_ATTR_FWD_METHOD]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_WEIGHT]		= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_U_THRESH]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_L_THRESH]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_ACTIVE_CONNS]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_INACT_CONNS]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_PERSIST_CONNS]	= { .type = NLA_U32 },
	[IPVS_DEST_ATTR_STATS]		= { .type = NLA_NESTED },
};

struct nla_policy ipvs_stats_policy[IPVS_STATS_ATTR_MAX + 1] = {
	[IPVS_STATS_ATTR_CONNS]		= { .type = NLA_U32 },
	[IPVS_STATS_ATTR_INPKTS]	= { .type = NLA_U32 },
	[IPVS_STATS_ATTR_OUTPKTS]	= { .type = NLA_U32 },
	[IPVS_STATS_ATTR_INBYTES]	= { .type = NLA_U64 },
	[IPVS_STATS_ATTR_OUTBYTES]	= { .type = NLA_U64 },
	[IPVS_STATS_ATTR_CPS]		= { .type = NLA_U32 },
	[IPVS_STATS_ATTR_INPPS]		= { .type = NLA_U32 },
	[IPVS_STATS_ATTR_OUTPPS]	= { .type = NLA_U32 },
	[IPVS_STATS_ATTR_INBPS]		= { .type = NLA_U32 },
	[IPVS_STATS_ATTR_OUTBPS]	= { .type = NLA_U32 },
};

struct nla_policy ipvs_info_policy[IPVS_INFO_ATTR_MAX + 1] = {
	[IPVS_INFO_ATTR_VERSION]	= { .type = NLA_U32 },
	[IPVS_INFO_ATTR_CONN_TAB_SIZE]	= { .type = NLA_U32 },
};
#endif
/*
 * libipvs API
 */
#ifdef LIBIPVS_USE_NL
struct nl_msg *ipvs_nl_message(int cmd, int flags)
{
	struct nl_msg *msg;

	msg = nlmsg_alloc();
	if (!msg)
		return NULL;

	genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family, 0, flags,
		cmd, IPVS_GENL_VERSION);

	return msg;
}

int ipvs_nl_send_message(struct nl_msg *msg, nl_recvmsg_msg_cb_t func, void *arg)
{
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

/* To test connections and set the family */
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

static int ipvs_getinfo_parse_cb(struct nl_msg *msg, void *arg)
{
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct nlattr *attrs[IPVS_INFO_ATTR_MAX + 1];

	if (genlmsg_parse(nlh, 0, attrs, IPVS_INFO_ATTR_MAX, ipvs_info_policy) != 0)
		return -1;

	if (!(attrs[IPVS_INFO_ATTR_VERSION] &&
			attrs[IPVS_INFO_ATTR_CONN_TAB_SIZE]))
		return -1;

	ipvs_info.version = nla_get_u32(attrs[IPVS_INFO_ATTR_VERSION]);
	ipvs_info.size = nla_get_u32(attrs[IPVS_INFO_ATTR_CONN_TAB_SIZE]);

	return NL_OK;
}
#endif

int ipvs_getinfo(void)
{
	socklen_t len;

#ifdef LIBIPVS_USE_NL
	if (try_nl) {
		struct nl_msg *msg;
		msg = ipvs_nl_message(IPVS_CMD_GET_INFO, 0);
		if (msg)
			return ipvs_nl_send_message(msg, ipvs_getinfo_parse_cb,NULL);
		return -1;
	}
#endif

	ipvs_func = ipvs_getinfo;
	len = sizeof(ipvs_info);
	return getsockopt(sockfd, IPPROTO_IP, IP_VS_SO_GET_INFO,
			(char *)&ipvs_info, &len);
}

int ipvs_init(void)
{
	socklen_t len;

	ipvs_func = ipvs_init;

#ifdef LIBIPVS_USE_NL
	try_nl = 1;

	if (ipvs_nl_send_message(NULL, NULL, NULL) == 0) {
		try_nl = 1;
		return ipvs_getinfo();
	}

	try_nl = 0;
#endif

	len = sizeof(ipvs_info);
	if ((sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) == -1)
		return -1;

	if (getsockopt(sockfd, IPPROTO_IP, IP_VS_SO_GET_INFO,
			(char *)&ipvs_info, &len))
		return -1;

	return 0;
}

static int ipvs_parse_stats(struct ip_vs_stats64 *stats, struct nlattr *nla)
{
	struct nlattr *attrs[IPVS_STATS_ATTR_MAX + 1];

	if (nla_parse_nested(attrs, IPVS_STATS_ATTR_MAX, nla, ipvs_stats_policy))
		return -1;

	if (!(attrs[IPVS_STATS_ATTR_CONNS] &&
			attrs[IPVS_STATS_ATTR_INPKTS] &&
			attrs[IPVS_STATS_ATTR_OUTPKTS] &&
			attrs[IPVS_STATS_ATTR_INBYTES] &&
			attrs[IPVS_STATS_ATTR_OUTBYTES] &&
			attrs[IPVS_STATS_ATTR_CPS] &&
			attrs[IPVS_STATS_ATTR_INPPS] &&
			attrs[IPVS_STATS_ATTR_OUTPPS] &&
			attrs[IPVS_STATS_ATTR_INBPS] &&
			attrs[IPVS_STATS_ATTR_OUTBPS]))
		return -1;

	stats->conns = nla_get_u32(attrs[IPVS_STATS_ATTR_CONNS]);
	stats->inpkts = nla_get_u32(attrs[IPVS_STATS_ATTR_INPKTS]);
	stats->outpkts = nla_get_u32(attrs[IPVS_STATS_ATTR_OUTPKTS]);
	stats->inbytes = nla_get_u64(attrs[IPVS_STATS_ATTR_INBYTES]);
	stats->outbytes = nla_get_u64(attrs[IPVS_STATS_ATTR_OUTBYTES]);
	stats->cps = nla_get_u32(attrs[IPVS_STATS_ATTR_CPS]);
	stats->inpps = nla_get_u32(attrs[IPVS_STATS_ATTR_INPPS]);
	stats->outpps = nla_get_u32(attrs[IPVS_STATS_ATTR_OUTPPS]);
	stats->inbps = nla_get_u32(attrs[IPVS_STATS_ATTR_INBPS]);
	stats->outbps = nla_get_u32(attrs[IPVS_STATS_ATTR_OUTBPS]);

	return 0;

}

static int ipvs_parse_stats64(struct ip_vs_stats64 *stats, struct nlattr *nla)
{
	struct nlattr *attrs[IPVS_STATS_ATTR_MAX + 1];

	if (nla_parse_nested(attrs, IPVS_STATS_ATTR_MAX, nla,
			ipvs_stats_policy))
		return -1;

	if (!(attrs[IPVS_STATS_ATTR_CONNS] &&
		attrs[IPVS_STATS_ATTR_INPKTS] &&
		attrs[IPVS_STATS_ATTR_OUTPKTS] &&
		attrs[IPVS_STATS_ATTR_INBYTES] &&
		attrs[IPVS_STATS_ATTR_OUTBYTES] &&
		attrs[IPVS_STATS_ATTR_CPS] &&
		attrs[IPVS_STATS_ATTR_INPPS] &&
		attrs[IPVS_STATS_ATTR_OUTPPS] &&
		attrs[IPVS_STATS_ATTR_INBPS] &&
		attrs[IPVS_STATS_ATTR_OUTBPS]))
	return -1;

	stats->conns = nla_get_u64(attrs[IPVS_STATS_ATTR_CONNS]);
	stats->inpkts = nla_get_u64(attrs[IPVS_STATS_ATTR_INPKTS]);
	stats->outpkts = nla_get_u64(attrs[IPVS_STATS_ATTR_OUTPKTS]);
	stats->inbytes = nla_get_u64(attrs[IPVS_STATS_ATTR_INBYTES]);
	stats->outbytes = nla_get_u64(attrs[IPVS_STATS_ATTR_OUTBYTES]);
	stats->cps = nla_get_u64(attrs[IPVS_STATS_ATTR_CPS]);
	stats->inpps = nla_get_u64(attrs[IPVS_STATS_ATTR_INPPS]);
	stats->outpps = nla_get_u64(attrs[IPVS_STATS_ATTR_OUTPPS]);
	stats->inbps = nla_get_u64(attrs[IPVS_STATS_ATTR_INBPS]);
	stats->outbps = nla_get_u64(attrs[IPVS_STATS_ATTR_OUTBPS]);

	return 0;
}

static int ipvs_services_parse_cb(struct nl_msg *msg, void *arg)
{
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct nlattr *attrs[IPVS_CMD_ATTR_MAX + 1];
	struct nlattr *svc_attrs[IPVS_SVC_ATTR_MAX + 1];
	struct ip_vs_get_services **getp = (struct ip_vs_get_services **)arg;
	struct ip_vs_get_services *get = (struct ip_vs_get_services *)*getp;
	struct ip_vs_flags flags;
	int i = get->num_services;

	if (genlmsg_parse(nlh, 0, attrs, IPVS_CMD_ATTR_MAX, ipvs_cmd_policy) != 0)
		return -1;

	if (!attrs[IPVS_CMD_ATTR_SERVICE])
		return -1;

	if (nla_parse_nested(svc_attrs, IPVS_SVC_ATTR_MAX, attrs[IPVS_CMD_ATTR_SERVICE], ipvs_service_policy))
		return -1;

		memset(&(get->entrytable[i]), 0, sizeof(get->entrytable[i]));

		if (!(svc_attrs[IPVS_SVC_ATTR_AF] &&
			(svc_attrs[IPVS_SVC_ATTR_FWMARK] ||
			(svc_attrs[IPVS_SVC_ATTR_PROTOCOL] &&
			svc_attrs[IPVS_SVC_ATTR_ADDR] &&
			svc_attrs[IPVS_SVC_ATTR_PORT])) &&
			svc_attrs[IPVS_SVC_ATTR_SCHED_NAME] &&
			svc_attrs[IPVS_SVC_ATTR_NETMASK] &&
			svc_attrs[IPVS_SVC_ATTR_TIMEOUT] &&
			svc_attrs[IPVS_SVC_ATTR_FLAGS]))
			return -1;

		get->entrytable[i].af = nla_get_u16(svc_attrs[IPVS_SVC_ATTR_AF]);

		if (svc_attrs[IPVS_SVC_ATTR_FWMARK])
			get->entrytable[i].fwmark = nla_get_u32(svc_attrs[IPVS_SVC_ATTR_FWMARK]);
		else {
			get->entrytable[i].protocol = nla_get_u16(svc_attrs[IPVS_SVC_ATTR_PROTOCOL]);
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
				IP_VS_PENAME_MAXLEN);

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
		get = realloc(get, sizeof(*get)
				+ sizeof(ipvs_service_entry_t) * (get->num_services + 1));
		*getp = get;
		return 0;
}

struct ip_vs_get_services *ipvs_get_services(void)
{
		struct ip_vs_get_services *get;
		struct ip_vs_get_services_kern *getk;
		socklen_t len;
		int i;
	//	char straddr[INET6_ADDRSTRLEN];

#ifdef LIBIPVS_USE_NL
		if (try_nl) {
			struct nl_msg *msg;
			len = sizeof(*get) +
					sizeof(ipvs_service_entry_t);
			if (!(get = malloc(len)))
				return NULL;

			get->num_services = 0;

			msg = ipvs_nl_message(IPVS_CMD_GET_SERVICE, NLM_F_DUMP);

			if (msg && (ipvs_nl_send_message(msg, ipvs_services_parse_cb, &get) == 0)) {
				return get;
			}
			free(get);
			return NULL;
		}
#endif

		len = sizeof(*get) +
			sizeof(ipvs_service_entry_t) * ipvs_info.num_services;
		if (!(get = malloc(len)))
			return NULL;
		len = sizeof(*getk) +
			sizeof(struct ip_vs_service_entry_kern) * ipvs_info.num_services;
		if (!(getk = malloc(len))) {
			free(get);
			return NULL;
		}

		ipvs_func = ipvs_get_services;
		getk->num_services = ipvs_info.num_services;

		if (getsockopt(sockfd, IPPROTO_IP,
			IP_VS_SO_GET_SERVICES, getk, &len) < 0) {
			free(get);
			free(getk);
			return NULL;
		}
		memcpy(get, getk, sizeof(struct ip_vs_get_services));
		for (i = 0; i < getk->num_services; i++) {
				memcpy(&get->entrytable[i], &getk->entrytable[i],
					sizeof(struct ip_vs_service_entry_kern));
				get->entrytable[i].af = AF_INET;
				get->entrytable[i].addr.ip = get->entrytable[i].__addr_v4;
		}
		free(getk);
		return get;
}

static int ipvs_dests_parse_cb(struct nl_msg *msg, void *arg)
{
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct nlattr *attrs[IPVS_CMD_ATTR_MAX + 1];
	struct nlattr *dest_attrs[IPVS_DEST_ATTR_MAX + 1];
	struct nlattr *attr_addr_family = NULL;
	struct ip_vs_get_dests **dp = (struct ip_vs_get_dests **)arg;
	struct ip_vs_get_dests *d = (struct ip_vs_get_dests *)*dp;
	int i = d->num_dests;

	if (genlmsg_parse(nlh, 0, attrs, IPVS_CMD_ATTR_MAX, ipvs_cmd_policy) != 0)
		return -1;

	if (!attrs[IPVS_CMD_ATTR_DEST])
		return -1;

	if (nla_parse_nested(dest_attrs, IPVS_DEST_ATTR_MAX, attrs[IPVS_CMD_ATTR_DEST], ipvs_dest_policy))
		return -1;

	memset(&(d->entrytable[i]), 0, sizeof(d->entrytable[i]));

	if (!(dest_attrs[IPVS_DEST_ATTR_ADDR] &&
			dest_attrs[IPVS_DEST_ATTR_PORT] &&
			dest_attrs[IPVS_DEST_ATTR_FWD_METHOD] &&
			dest_attrs[IPVS_DEST_ATTR_WEIGHT] &&
			dest_attrs[IPVS_DEST_ATTR_U_THRESH] &&
			dest_attrs[IPVS_DEST_ATTR_L_THRESH] &&
			dest_attrs[IPVS_DEST_ATTR_ACTIVE_CONNS] &&
			dest_attrs[IPVS_DEST_ATTR_INACT_CONNS] &&
			dest_attrs[IPVS_DEST_ATTR_PERSIST_CONNS]))
		return -1;

	memcpy(&(d->entrytable[i].addr),
		nla_data(dest_attrs[IPVS_DEST_ATTR_ADDR]),
		sizeof(d->entrytable[i].addr));
	d->entrytable[i].port = nla_get_u16(dest_attrs[IPVS_DEST_ATTR_PORT]);
	d->entrytable[i].conn_flags = nla_get_u32(dest_attrs[IPVS_DEST_ATTR_FWD_METHOD]);
	d->entrytable[i].weight = nla_get_u32(dest_attrs[IPVS_DEST_ATTR_WEIGHT]);
	d->entrytable[i].u_threshold = nla_get_u32(dest_attrs[IPVS_DEST_ATTR_U_THRESH]);
	d->entrytable[i].l_threshold = nla_get_u32(dest_attrs[IPVS_DEST_ATTR_L_THRESH]);
	d->entrytable[i].activeconns = nla_get_u32(dest_attrs[IPVS_DEST_ATTR_ACTIVE_CONNS]);
	d->entrytable[i].inactconns = nla_get_u32(dest_attrs[IPVS_DEST_ATTR_INACT_CONNS]);
	d->entrytable[i].persistconns = nla_get_u32(dest_attrs[IPVS_DEST_ATTR_PERSIST_CONNS]);
	attr_addr_family = dest_attrs[IPVS_DEST_ATTR_ADDR_FAMILY];
	if (attr_addr_family)
		d->entrytable[i].af = nla_get_u16(attr_addr_family);
	else
		d->entrytable[i].af = d->af;

	if (dest_attrs[IPVS_DEST_ATTR_STATS64]) {
		if (ipvs_parse_stats(&d->entrytable[i].stats64,
				dest_attrs[IPVS_DEST_ATTR_STATS64]) != 0)
			return -1;
	} else if (dest_attrs[IPVS_DEST_ATTR_STATS]) {
		if (ipvs_parse_stats(&d->entrytable[i].stats64,
				dest_attrs[IPVS_DEST_ATTR_STATS]) != 0)
			return -1;
	}

	i++;

	d->num_dests = i;
	d = realloc(d, sizeof(*d) + sizeof(ipvs_dest_entry_t) * (d->num_dests + 1));
	*dp = d;
	return 0;
}

static struct ip_vs_get_dests *ipvs_get_dests (struct ip_vs_service_entry *se)
{
		struct ip_vs_get_dests *ret;
		socklen_t len;

		len = sizeof (*ret) + sizeof (struct ip_vs_dest_entry) * se->num_dests;

		if (NULL == (ret = malloc (len))) {
				log_err ("ipvs_get_dests: Out of memory.");
				exit (3);
		}

#ifdef LIBIPVS_USE_NL
		if (try_nl) {
			struct nl_msg *msg;
			struct nlattr *nl_service;
				if (se->num_dests == 0)
					ret  = realloc(ret,sizeof(*ret) + sizeof(struct ip_vs_dest_entry));

				ret->fwmark = se->fwmark;
				ret->protocol = se->protocol;
				ret->addr = se->addr;
				ret->port = se->port;
				ret->num_dests = se->num_dests;
				ret->af = se->af;

				msg = ipvs_nl_message(IPVS_CMD_GET_DEST, NLM_F_DUMP);
				if (!msg)
					goto ipvs_nl_dest_failure;

				nl_service = nla_nest_start(msg, IPVS_CMD_ATTR_SERVICE);
				if (!nl_service)
					goto nla_put_failure;

				NLA_PUT_U16(msg, IPVS_SVC_ATTR_AF, se->af);

				if (se->fwmark) {
					NLA_PUT_U32(msg, IPVS_SVC_ATTR_FWMARK, se->fwmark);
				} else {
					NLA_PUT_U16(msg, IPVS_SVC_ATTR_PROTOCOL, se->protocol);
					NLA_PUT(msg, IPVS_SVC_ATTR_ADDR, sizeof(se->addr),
						&se->addr);
					NLA_PUT_U16(msg, IPVS_SVC_ATTR_PORT, se->port);
				}

				nla_nest_end(msg, nl_service);
				if (ipvs_nl_send_message(msg, ipvs_dests_parse_cb, &ret))
					goto ipvs_nl_dest_failure;

				return ret;

nla_put_failure:
				nlmsg_free(msg);
ipvs_nl_dest_failure:
				free(ret);
				return NULL;
		}
#endif

		ret->fwmark    = se->fwmark;
		ret->protocol  = se->protocol;
		ret->addr      = se->addr;
		ret->port      = se->port;
		ret->num_dests = se->num_dests;

		if (0 != getsockopt (sockfd, IPPROTO_IP, IP_VS_SO_GET_DESTS,
					(void *)ret, &len)) {
				char errbuf[1024];
				log_err ("ipvs_get_dests: getsockopt() failed: %s",
                                sstrerror (errno, errbuf, sizeof (errbuf)));
				free (ret);
				return NULL;
		}
		return ret;
} /* ip_vs_get_dests */



/*
 * collectd plugin API and helper functions
 */
 //TODO ben pretty sure we should update this to use netlink?
static int cipvs_init (void)
{
	struct ip_vs_getinfo ipvs_info;

	socklen_t len;

	if (-1 == (sockfd = socket (AF_INET, SOCK_RAW, IPPROTO_RAW))) {
		char errbuf[1024];
		log_err ("cipvs_init: socket() failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return -1;
	}

	len = sizeof (ipvs_info);

	if (0 != getsockopt (sockfd, IPPROTO_IP, IP_VS_SO_GET_INFO,
				(void *)&ipvs_info, &len)) {
		char errbuf[1024];
		log_err ("cipvs_init: getsockopt() failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		close (sockfd);
		sockfd = -1;
		return -1;
	}

	/* we need IPVS >= 1.1.4 */
	if (ipvs_info.version < ((1 << 16) + (1 << 8) + 4)) {
		log_err ("cipvs_init: IPVS version too old (%d.%d.%d < %d.%d.%d)",
				NVERSION (ipvs_info.version), 1, 1, 4);
		close (sockfd);
		sockfd = -1;
		return -1;
	}
	else {
		log_info ("Successfully connected to IPVS %d.%d.%d",
				NVERSION (ipvs_info.version));
	}
	return 0;
} /* cipvs_init */

/*
 * ipvs-<virtual IP>_{UDP,TCP}<port>/<type>-total
 * ipvs-<virtual IP>_{UDP,TCP}<port>/<type>-<real IP>_<port>
 */

/* plugin instance */
static int get_pi (struct ip_vs_service_entry *se, char *pi, size_t size)
{
	union nf_inet_addr addr;
	char straddr[INET6_ADDRSTRLEN];
	int len = 0;


	if ((NULL == se) || (NULL == pi))
		return 0;

	addr = se->addr;

	/* inet_ntoa() returns a pointer to a statically allocated buffer
	 * I hope non-glibc systems behave the same */
	if ( se->af == AF_INET6) {
		len = ssnprintf (pi, size, "%s_%s%u", inet_ntop(AF_INET6, &addr,
			 	straddr, sizeof(straddr)), (se->protocol == IPPROTO_TCP) ? "TCP" : "UDP",
				ntohs (se->port));
	}else {
		len = ssnprintf (pi, size, "%s_%s%u", inet_ntoa (addr.in),
				(se->protocol == IPPROTO_TCP) ? "TCP" : "UDP",
				ntohs (se->port));
	}

	if ((0 > len) || (size <= ((size_t) len))) {
		log_err ("plugin instance truncated: %s", pi);
		return -1;
	}
	return 0;
} /* get_pi */

/* type instance */
static int get_ti (struct ip_vs_dest_entry *de, char *ti, size_t size)
{
	union nf_inet_addr addr;
	char straddr[INET6_ADDRSTRLEN];
	int len = 0;

	if ((NULL == de) || (NULL == ti))
		return 0;

	addr = de->addr;

	/* inet_ntoa() returns a pointer to a statically allocated buffer
	* I hope non-glibc systems behave the same */
	if ( de->af == AF_INET6) {
		len = ssnprintf (ti, size, "%s_%u", inet_ntop(AF_INET6, &addr,
				straddr, sizeof(straddr)),
				ntohs (de->port));
	}else {
		len = ssnprintf (ti, size, "%s_%u", inet_ntoa (addr.in),
				ntohs (de->port));
	}

	if ((0 > len) || (size <= ((size_t) len))) {
		log_err ("type instance truncated: %s", ti);
		return -1;
	}
	return 0;
} /* get_ti */

static void cipvs_submit_connections (const char *pi, const char *ti,
		derive_t value)
{
	value_list_t vl = VALUE_LIST_INIT;

	vl.values     = &(value_t) { .derive = value };
	vl.values_len = 1;

	sstrncpy (vl.plugin, "ipvs", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, pi, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, "connections", sizeof (vl.type));
	sstrncpy (vl.type_instance, (NULL != ti) ? ti : "total",
		sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
	return;
} /* cipvs_submit_connections */

static void cipvs_submit_if (const char *pi, const char *t, const char *ti,
		derive_t rx, derive_t tx)
{
	value_t values[] = {
		{ .derive = rx },
		{ .derive = tx },
	};
	value_list_t vl = VALUE_LIST_INIT;

	vl.values     = values;
	vl.values_len = STATIC_ARRAY_SIZE (values);

	sstrncpy (vl.plugin, "ipvs", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, pi, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, t, sizeof (vl.type));
	sstrncpy (vl.type_instance, (NULL != ti) ? ti : "total",
		sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
	return;
} /* cipvs_submit_if */

static void cipvs_submit_dest (const char *pi, struct ip_vs_dest_entry *de)
{
	struct ip_vs_stats64  stats = de->stats64;

	char ti[DATA_MAX_NAME_LEN];

	if (0 != get_ti (de, ti, sizeof (ti)))
		return;

	cipvs_submit_connections (pi, ti, stats.conns);
	cipvs_submit_if (pi, "if_packets", ti, stats.inpkts, stats.outpkts);
	cipvs_submit_if (pi, "if_octets", ti, stats.inbytes, stats.outbytes);
	return;
} /* cipvs_submit_dest */

static void cipvs_submit_service (struct ip_vs_service_entry *se)
{
	struct ip_vs_stats_user  stats = se->stats;
	struct ip_vs_get_dests  *dests = ipvs_get_dests (se);

	char pi[DATA_MAX_NAME_LEN];

	if (0 != get_pi (se, pi, sizeof (pi)))
	{
		free (dests);
		return;
	}

	cipvs_submit_connections (pi, NULL, stats.conns);
	cipvs_submit_if (pi, "if_packets", NULL, stats.inpkts, stats.outpkts);
	cipvs_submit_if (pi, "if_octets", NULL, stats.inbytes, stats.outbytes);

	for (size_t i = 0; i < dests->num_dests; ++i)
		cipvs_submit_dest (pi, &dests->entrytable[i]);

	free (dests);
	return;
} /* cipvs_submit_service */

static int cipvs_read (void)
{
	struct ip_vs_get_services *services = NULL;

	//TODO ben check if this needs to be updated for netlink
	if (sockfd < 0)
		return (-1);

	if (NULL == (services = ipvs_get_services ()))
		return -1;

	for (size_t i = 0; i < services->num_services; ++i)
		cipvs_submit_service (&services->entrytable[i]);

	free (services);
	return 0;
} /* cipvs_read */

static int cipvs_shutdown (void)
{
	if (sockfd >= 0)
		close (sockfd);
	sockfd = -1;

	return 0;
} /* cipvs_shutdown */

void module_register (void)
{
	plugin_register_init ("ipvs", cipvs_init);
	plugin_register_read ("ipvs", cipvs_read);
	plugin_register_shutdown ("ipvs", cipvs_shutdown);
	return;
} /* module_register */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */
