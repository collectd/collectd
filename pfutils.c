#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <net/pfvar.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <netdb.h>
#include <stdarg.h>
#include <errno.h>
#include <err.h>
#include <ifaddrs.h>
#include <unistd.h>

#include "pfutils.h"

void		 print_fromto(struct pf_rule_addr *, pf_osfp_t,
		    struct pf_rule_addr *, u_int8_t, u_int8_t, int);
void		 print_ugid (u_int8_t, unsigned, unsigned, const char *, unsigned);
void		 print_flags (u_int8_t);
void		 print_addr(struct pf_addr_wrap *, sa_family_t, int);
void		 print_port (u_int8_t, u_int16_t, u_int16_t, const char *);
char		*pfctl_lookup_fingerprint(pf_osfp_t, char *, size_t);
void		 print_op (u_int8_t, const char *, const char *);
int		 unmask(struct pf_addr *, sa_family_t);

struct name_entry;
LIST_HEAD(name_list, name_entry);
struct name_entry {
	LIST_ENTRY(name_entry)	nm_entry;
	int			nm_num;
	char			nm_name[PF_OSFP_LEN];

	struct name_list	nm_sublist;
	int			nm_sublist_num;
};

struct name_list classes = LIST_HEAD_INITIALIZER(&classes);

struct icmptypeent {
	const char *name;
	u_int8_t type;
};

struct icmpcodeent {
	const char *name;
	u_int8_t type;
	u_int8_t code;
};

struct pf_timeout {
	const char	*name;
	int		 timeout;
};


const struct icmptypeent *geticmptypebynumber(u_int8_t, u_int8_t);
const struct icmptypeent *geticmptypebyname(char *, u_int8_t);
const struct icmpcodeent *geticmpcodebynumber(u_int8_t, u_int8_t, u_int8_t);
const struct icmpcodeent *geticmpcodebyname(u_long, char *, u_int8_t);

static const struct icmptypeent icmp_type[] = {
	{ "echoreq",	ICMP_ECHO },
	{ "echorep",	ICMP_ECHOREPLY },
	{ "unreach",	ICMP_UNREACH },
	{ "squench",	ICMP_SOURCEQUENCH },
	{ "redir",	ICMP_REDIRECT },
	{ "althost",	ICMP_ALTHOSTADDR },
	{ "routeradv",	ICMP_ROUTERADVERT },
	{ "routersol",	ICMP_ROUTERSOLICIT },
	{ "timex",	ICMP_TIMXCEED },
	{ "paramprob",	ICMP_PARAMPROB },
	{ "timereq",	ICMP_TSTAMP },
	{ "timerep",	ICMP_TSTAMPREPLY },
	{ "inforeq",	ICMP_IREQ },
	{ "inforep",	ICMP_IREQREPLY },
	{ "maskreq",	ICMP_MASKREQ },
	{ "maskrep",	ICMP_MASKREPLY },
	{ "trace",	ICMP_TRACEROUTE },
	{ "dataconv",	ICMP_DATACONVERR },
	{ "mobredir",	ICMP_MOBILE_REDIRECT },
	{ "ipv6-where",	ICMP_IPV6_WHEREAREYOU },
	{ "ipv6-here",	ICMP_IPV6_IAMHERE },
	{ "mobregreq",	ICMP_MOBILE_REGREQUEST },
	{ "mobregrep",	ICMP_MOBILE_REGREPLY },
	{ "skip",	ICMP_SKIP },
	{ "photuris",	ICMP_PHOTURIS }
};

static const struct icmptypeent icmp6_type[] = {
	{ "unreach",	ICMP6_DST_UNREACH },
	{ "toobig",	ICMP6_PACKET_TOO_BIG },
	{ "timex",	ICMP6_TIME_EXCEEDED },
	{ "paramprob",	ICMP6_PARAM_PROB },
	{ "echoreq",	ICMP6_ECHO_REQUEST },
	{ "echorep",	ICMP6_ECHO_REPLY },
	{ "groupqry",	ICMP6_MEMBERSHIP_QUERY },
	{ "listqry",	MLD_LISTENER_QUERY },
	{ "grouprep",	ICMP6_MEMBERSHIP_REPORT },
	{ "listenrep",	MLD_LISTENER_REPORT },
	{ "groupterm",	ICMP6_MEMBERSHIP_REDUCTION },
	{ "listendone", MLD_LISTENER_DONE },
	{ "routersol",	ND_ROUTER_SOLICIT },
	{ "routeradv",	ND_ROUTER_ADVERT },
	{ "neighbrsol", ND_NEIGHBOR_SOLICIT },
	{ "neighbradv", ND_NEIGHBOR_ADVERT },
	{ "redir",	ND_REDIRECT },
	{ "routrrenum", ICMP6_ROUTER_RENUMBERING },
	{ "wrureq",	ICMP6_WRUREQUEST },
	{ "wrurep",	ICMP6_WRUREPLY },
	{ "fqdnreq",	ICMP6_FQDN_QUERY },
	{ "fqdnrep",	ICMP6_FQDN_REPLY },
	{ "niqry",	ICMP6_NI_QUERY },
	{ "nirep",	ICMP6_NI_REPLY },
	{ "mtraceresp",	MLD_MTRACE_RESP },
	{ "mtrace",	MLD_MTRACE }
};


const struct pf_timeout pf_timeouts[] = {
	{ "tcp.first",		PFTM_TCP_FIRST_PACKET },
	{ "tcp.opening",	PFTM_TCP_OPENING },
	{ "tcp.established",	PFTM_TCP_ESTABLISHED },
	{ "tcp.closing",	PFTM_TCP_CLOSING },
	{ "tcp.finwait",	PFTM_TCP_FIN_WAIT },
	{ "tcp.closed",		PFTM_TCP_CLOSED },
	{ "tcp.tsdiff",		PFTM_TS_DIFF },
	{ "udp.first",		PFTM_UDP_FIRST_PACKET },
	{ "udp.single",		PFTM_UDP_SINGLE },
	{ "udp.multiple",	PFTM_UDP_MULTIPLE },
	{ "icmp.first",		PFTM_ICMP_FIRST_PACKET },
	{ "icmp.error",		PFTM_ICMP_ERROR_REPLY },
	{ "other.first",	PFTM_OTHER_FIRST_PACKET },
	{ "other.single",	PFTM_OTHER_SINGLE },
	{ "other.multiple",	PFTM_OTHER_MULTIPLE },
	{ "frag",		PFTM_FRAG },
	{ "interval",		PFTM_INTERVAL },
	{ "adaptive.start",	PFTM_ADAPTIVE_START },
	{ "adaptive.end",	PFTM_ADAPTIVE_END },
	{ "src.track",		PFTM_SRC_NODE },
	{ NULL,			0 }
};

static const struct icmpcodeent icmp_code[] = {
	{ "net-unr",		ICMP_UNREACH,	ICMP_UNREACH_NET },
	{ "host-unr",		ICMP_UNREACH,	ICMP_UNREACH_HOST },
	{ "proto-unr",		ICMP_UNREACH,	ICMP_UNREACH_PROTOCOL },
	{ "port-unr",		ICMP_UNREACH,	ICMP_UNREACH_PORT },
	{ "needfrag",		ICMP_UNREACH,	ICMP_UNREACH_NEEDFRAG },
	{ "srcfail",		ICMP_UNREACH,	ICMP_UNREACH_SRCFAIL },
	{ "net-unk",		ICMP_UNREACH,	ICMP_UNREACH_NET_UNKNOWN },
	{ "host-unk",		ICMP_UNREACH,	ICMP_UNREACH_HOST_UNKNOWN },
	{ "isolate",		ICMP_UNREACH,	ICMP_UNREACH_ISOLATED },
	{ "net-prohib",		ICMP_UNREACH,	ICMP_UNREACH_NET_PROHIB },
	{ "host-prohib",	ICMP_UNREACH,	ICMP_UNREACH_HOST_PROHIB },
	{ "net-tos",		ICMP_UNREACH,	ICMP_UNREACH_TOSNET },
	{ "host-tos",		ICMP_UNREACH,	ICMP_UNREACH_TOSHOST },
	{ "filter-prohib",	ICMP_UNREACH,	ICMP_UNREACH_FILTER_PROHIB },
	{ "host-preced",	ICMP_UNREACH,	ICMP_UNREACH_HOST_PRECEDENCE },
	{ "cutoff-preced",	ICMP_UNREACH,	ICMP_UNREACH_PRECEDENCE_CUTOFF },
	{ "redir-net",		ICMP_REDIRECT,	ICMP_REDIRECT_NET },
	{ "redir-host",		ICMP_REDIRECT,	ICMP_REDIRECT_HOST },
	{ "redir-tos-net",	ICMP_REDIRECT,	ICMP_REDIRECT_TOSNET },
	{ "redir-tos-host",	ICMP_REDIRECT,	ICMP_REDIRECT_TOSHOST },
	{ "normal-adv",		ICMP_ROUTERADVERT, ICMP_ROUTERADVERT_NORMAL },
	{ "common-adv",		ICMP_ROUTERADVERT, ICMP_ROUTERADVERT_NOROUTE_COMMON },
	{ "transit",		ICMP_TIMXCEED,	ICMP_TIMXCEED_INTRANS },
	{ "reassemb",		ICMP_TIMXCEED,	ICMP_TIMXCEED_REASS },
	{ "badhead",		ICMP_PARAMPROB,	ICMP_PARAMPROB_ERRATPTR },
	{ "optmiss",		ICMP_PARAMPROB,	ICMP_PARAMPROB_OPTABSENT },
	{ "badlen",		ICMP_PARAMPROB,	ICMP_PARAMPROB_LENGTH },
	{ "unknown-ind",	ICMP_PHOTURIS,	ICMP_PHOTURIS_UNKNOWN_INDEX },
	{ "auth-fail",		ICMP_PHOTURIS,	ICMP_PHOTURIS_AUTH_FAILED },
	{ "decrypt-fail",	ICMP_PHOTURIS,	ICMP_PHOTURIS_DECRYPT_FAILED }
};

static const struct icmpcodeent icmp6_code[] = {
	{ "admin-unr", ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_ADMIN },
	{ "noroute-unr", ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_NOROUTE },
	{ "notnbr-unr",	ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_NOTNEIGHBOR },
	{ "beyond-unr", ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_BEYONDSCOPE },
	{ "addr-unr", ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_ADDR },
	{ "port-unr", ICMP6_DST_UNREACH, ICMP6_DST_UNREACH_NOPORT },
	{ "transit", ICMP6_TIME_EXCEEDED, ICMP6_TIME_EXCEED_TRANSIT },
	{ "reassemb", ICMP6_TIME_EXCEEDED, ICMP6_TIME_EXCEED_REASSEMBLY },
	{ "badhead", ICMP6_PARAM_PROB, ICMP6_PARAMPROB_HEADER },
	{ "nxthdr", ICMP6_PARAM_PROB, ICMP6_PARAMPROB_NEXTHEADER },
	{ "redironlink", ND_REDIRECT, ND_REDIRECT_ONLINK },
	{ "redirrouter", ND_REDIRECT, ND_REDIRECT_ROUTER }
};

const char *tcpflags = "FSRPAUEW";

void
print_rule(struct pf_rule *r, const char *anchor_call, int verbose)
{
	static const char *actiontypes[] = { "pass", "block", "scrub",
	    "no scrub", "nat", "no nat", "binat", "no binat", "rdr", "no rdr" };
	static const char *anchortypes[] = { "anchor", "anchor", "anchor",
	    "anchor", "nat-anchor", "nat-anchor", "binat-anchor",
	    "binat-anchor", "rdr-anchor", "rdr-anchor" };
	int	i, opts;

	if (verbose)
		printf("@%d ", r->nr);
	if (r->action > PF_NORDR)
		printf("action(%d)", r->action);
	else if (anchor_call[0]) {
		if (anchor_call[0] == '_') {
			printf("%s", anchortypes[r->action]);
		} else
			printf("%s \"%s\"", anchortypes[r->action],
			    anchor_call);
	} else {
		printf("%s", actiontypes[r->action]);
		if (r->natpass)
			printf(" pass");
	}
	if (r->action == PF_DROP) {
		if (r->rule_flag & PFRULE_RETURN)
			printf(" return");
		else if (r->rule_flag & PFRULE_RETURNRST) {
			if (!r->return_ttl)
				printf(" return-rst");
			else
				printf(" return-rst(ttl %d)", r->return_ttl);
		} else if (r->rule_flag & PFRULE_RETURNICMP) {
			const struct icmpcodeent	*ic, *ic6;

			ic = geticmpcodebynumber(r->return_icmp >> 8,
			    r->return_icmp & 255, AF_INET);
			ic6 = geticmpcodebynumber(r->return_icmp6 >> 8,
			    r->return_icmp6 & 255, AF_INET6);

			switch (r->af) {
			case AF_INET:
				printf(" return-icmp");
				if (ic == NULL)
					printf("(%u)", r->return_icmp & 255);
				else
					printf("(%s)", ic->name);
				break;
			case AF_INET6:
				printf(" return-icmp6");
				if (ic6 == NULL)
					printf("(%u)", r->return_icmp6 & 255);
				else
					printf("(%s)", ic6->name);
				break;
			default:
				printf(" return-icmp");
				if (ic == NULL)
					printf("(%u, ", r->return_icmp & 255);
				else
					printf("(%s, ", ic->name);
				if (ic6 == NULL)
					printf("%u)", r->return_icmp6 & 255);
				else
					printf("%s)", ic6->name);
				break;
			}
		} else
			printf(" drop");
	}
	if (r->direction == PF_IN)
		printf(" in");
	else if (r->direction == PF_OUT)
		printf(" out");
	if (r->log) {
		printf(" log");
		if (r->log & ~PF_LOG || r->logif) {
			int count = 0;

			printf(" (");
			if (r->log & PF_LOG_ALL)
				printf("%sall", count++ ? ", " : "");
			if (r->log & PF_LOG_SOCKET_LOOKUP)
				printf("%suser", count++ ? ", " : "");
			if (r->logif)
				printf("%sto pflog%u", count++ ? ", " : "",
				    r->logif);
			printf(")");
		}
	}
	if (r->quick)
		printf(" quick");
	if (r->ifname[0]) {
		if (r->ifnot)
			printf(" on ! %s", r->ifname);
		else
			printf(" on %s", r->ifname);
	}
	if (r->rt) {
		if (r->rt == PF_ROUTETO)
			printf(" route-to");
		else if (r->rt == PF_REPLYTO)
			printf(" reply-to");
		else if (r->rt == PF_DUPTO)
			printf(" dup-to");
		else if (r->rt == PF_FASTROUTE)
			printf(" fastroute");
		if (r->rt != PF_FASTROUTE) {
			printf(" ");
			print_pool(&r->rpool, 0, 0, r->af, PF_PASS);
		}
	}
	if (r->af) {
		if (r->af == AF_INET)
			printf(" inet");
		else
			printf(" inet6");
	}
	if (r->proto) {
		struct protoent	*p;

		if ((p = getprotobynumber(r->proto)) != NULL)
			printf(" proto %s", p->p_name);
		else
			printf(" proto %u", r->proto);
	}
	print_fromto(&r->src, r->os_fingerprint, &r->dst, r->af, r->proto,
	    verbose);
	if (r->uid.op)
		print_ugid(r->uid.op, r->uid.uid[0], r->uid.uid[1], "user",
		    UID_MAX);
	if (r->gid.op)
		print_ugid(r->gid.op, r->gid.gid[0], r->gid.gid[1], "group",
		    GID_MAX);
	if (r->flags || r->flagset) {
		printf(" flags ");
		print_flags(r->flags);
		printf("/");
		print_flags(r->flagset);
	} else if (r->action == PF_PASS &&
	    (!r->proto || r->proto == IPPROTO_TCP) &&
	    !(r->rule_flag & PFRULE_FRAGMENT) &&
	    !anchor_call[0] && r->keep_state)
		printf(" flags any");
	if (r->type) {
		const struct icmptypeent	*it;

		it = geticmptypebynumber(r->type-1, r->af);
		if (r->af != AF_INET6)
			printf(" icmp-type");
		else
			printf(" icmp6-type");
		if (it != NULL)
			printf(" %s", it->name);
		else
			printf(" %u", r->type-1);
		if (r->code) {
			const struct icmpcodeent	*ic;

			ic = geticmpcodebynumber(r->type-1, r->code-1, r->af);
			if (ic != NULL)
				printf(" code %s", ic->name);
			else
				printf(" code %u", r->code-1);
		}
	}
	if (r->tos)
		printf(" tos 0x%2.2x", r->tos);
	if (!r->keep_state && r->action == PF_PASS && !anchor_call[0])
		printf(" no state");
	else if (r->keep_state == PF_STATE_NORMAL)
		printf(" keep state");
	else if (r->keep_state == PF_STATE_MODULATE)
		printf(" modulate state");
	else if (r->keep_state == PF_STATE_SYNPROXY)
		printf(" synproxy state");
	if (r->prob) {
		char	buf[20];

		snprintf(buf, sizeof(buf), "%f", r->prob*100.0/(UINT_MAX+1.0));
		for (i = strlen(buf)-1; i > 0; i--) {
			if (buf[i] == '0')
				buf[i] = '\0';
			else {
				if (buf[i] == '.')
					buf[i] = '\0';
				break;
			}
		}
		printf(" probability %s%%", buf);
	}
	opts = 0;
	if (r->max_states || r->max_src_nodes || r->max_src_states)
		opts = 1;
	if (r->rule_flag & PFRULE_NOSYNC)
		opts = 1;
	if (r->rule_flag & PFRULE_SRCTRACK)
		opts = 1;
	if (r->rule_flag & PFRULE_IFBOUND)
		opts = 1;
	if (r->rule_flag & PFRULE_STATESLOPPY)
		opts = 1;
	for (i = 0; !opts && i < PFTM_MAX; ++i)
		if (r->timeout[i])
			opts = 1;
	if (opts) {
		printf(" (");
		if (r->max_states) {
			printf("max %u", r->max_states);
			opts = 0;
		}
		if (r->rule_flag & PFRULE_NOSYNC) {
			if (!opts)
				printf(", ");
			printf("no-sync");
			opts = 0;
		}
		if (r->rule_flag & PFRULE_SRCTRACK) {
			if (!opts)
				printf(", ");
			printf("source-track");
			if (r->rule_flag & PFRULE_RULESRCTRACK)
				printf(" rule");
			else
				printf(" global");
			opts = 0;
		}
		if (r->max_src_states) {
			if (!opts)
				printf(", ");
			printf("max-src-states %u", r->max_src_states);
			opts = 0;
		}
		if (r->max_src_conn) {
			if (!opts)
				printf(", ");
			printf("max-src-conn %u", r->max_src_conn);
			opts = 0;
		}
		if (r->max_src_conn_rate.limit) {
			if (!opts)
				printf(", ");
			printf("max-src-conn-rate %u/%u",
			    r->max_src_conn_rate.limit,
			    r->max_src_conn_rate.seconds);
			opts = 0;
		}
		if (r->max_src_nodes) {
			if (!opts)
				printf(", ");
			printf("max-src-nodes %u", r->max_src_nodes);
			opts = 0;
		}
		if (r->overload_tblname[0]) {
			if (!opts)
				printf(", ");
			printf("overload <%s>", r->overload_tblname);
			if (r->flush)
				printf(" flush");
			if (r->flush & PF_FLUSH_GLOBAL)
				printf(" global");
		}
		if (r->rule_flag & PFRULE_IFBOUND) {
			if (!opts)
				printf(", ");
			printf("if-bound");
			opts = 0;
		}
		if (r->rule_flag & PFRULE_STATESLOPPY) {
			if (!opts)
				printf(", ");
			printf("sloppy");
			opts = 0;
		}
		if (r->rule_flag & PFRULE_PFLOW) {
			if (!opts)
				printf(", ");
			printf("pflow");
			opts = 0;
		}
		for (i = 0; i < PFTM_MAX; ++i)
			if (r->timeout[i]) {
				int j;

				if (!opts)
					printf(", ");
				opts = 0;
				for (j = 0; pf_timeouts[j].name != NULL;
				    ++j)
					if (pf_timeouts[j].timeout == i)
						break;
				printf("%s %u", pf_timeouts[j].name == NULL ?
				    "inv.timeout" : pf_timeouts[j].name,
				    r->timeout[i]);
			}
		printf(")");
	}
	if (r->rule_flag & PFRULE_FRAGMENT)
		printf(" fragment");
	if (r->rule_flag & PFRULE_NODF)
		printf(" no-df");
	if (r->rule_flag & PFRULE_RANDOMID)
		printf(" random-id");
	if (r->min_ttl)
		printf(" min-ttl %d", r->min_ttl);
	if (r->max_mss)
		printf(" max-mss %d", r->max_mss);
	if (r->rule_flag & PFRULE_SET_TOS)
		printf(" set-tos 0x%2.2x", r->set_tos);
	if (r->allow_opts)
		printf(" allow-opts");
	if (r->action == PF_SCRUB) {
		if (r->rule_flag & PFRULE_REASSEMBLE_TCP)
			printf(" reassemble tcp");

		if (r->rule_flag & PFRULE_FRAGDROP)
			printf(" fragment drop-ovl");
		else if (r->rule_flag & PFRULE_FRAGCROP)
			printf(" fragment crop");
		else
			printf(" fragment reassemble");
	}
	if (r->label[0])
		printf(" label \"%s\"", r->label);
	if (r->qname[0] && r->pqname[0])
		printf(" queue(%s, %s)", r->qname, r->pqname);
	else if (r->qname[0])
		printf(" queue %s", r->qname);
	if (r->tagname[0])
		printf(" tag %s", r->tagname);
	if (r->match_tagname[0]) {
		if (r->match_tag_not)
			printf(" !");
		printf(" tagged %s", r->match_tagname);
	}
	if (r->rtableid != -1)
		printf(" rtable %u", r->rtableid);
	if (r->divert.port) {
		if (PF_AZERO(&r->divert.addr, r->af)) {
			printf(" divert-reply");
		} else {
			/* XXX cut&paste from print_addr */
			char buf[48];

			printf(" divert-to ");
			if (inet_ntop(r->af, &r->divert.addr, buf,
			    sizeof(buf)) == NULL)
				printf("?");
			else
				printf("%s", buf);
			printf(" port %u", ntohs(r->divert.port));
		}
	}
	if (!anchor_call[0] && (r->action == PF_NAT ||
	    r->action == PF_BINAT || r->action == PF_RDR)) {
		printf(" -> ");
		print_pool(&r->rpool, r->rpool.proxy_port[0],
		    r->rpool.proxy_port[1], r->af, r->action);
	}
}

const struct icmptypeent *
geticmptypebynumber(u_int8_t type, sa_family_t af)
{
	unsigned int	i;

	if (af != AF_INET6) {
		for (i=0; i < (sizeof (icmp_type) / sizeof(icmp_type[0]));
		    i++) {
			if (type == icmp_type[i].type)
				return (&icmp_type[i]);
		}
	} else {
		for (i=0; i < (sizeof (icmp6_type) /
		    sizeof(icmp6_type[0])); i++) {
			if (type == icmp6_type[i].type)
				 return (&icmp6_type[i]);
		}
	}
	return (NULL);
}

const struct icmpcodeent *
geticmpcodebynumber(u_int8_t type, u_int8_t code, sa_family_t af)
{
	unsigned int	i;

	if (af != AF_INET6) {
		for (i=0; i < (sizeof (icmp_code) / sizeof(icmp_code[0]));
		    i++) {
			if (type == icmp_code[i].type &&
			    code == icmp_code[i].code)
				return (&icmp_code[i]);
		}
	} else {
		for (i=0; i < (sizeof (icmp6_code) /
		    sizeof(icmp6_code[0])); i++) {
			if (type == icmp6_code[i].type &&
			    code == icmp6_code[i].code)
				return (&icmp6_code[i]);
		}
	}
	return (NULL);
}

const struct icmpcodeent *
geticmpcodebyname(u_long type, char *w, sa_family_t af)
{
	unsigned int	i;

	if (af != AF_INET6) {
		for (i=0; i < (sizeof (icmp_code) / sizeof(icmp_code[0]));
		    i++) {
			if (type == icmp_code[i].type &&
			    !strcmp(w, icmp_code[i].name))
				return (&icmp_code[i]);
		}
	} else {
		for (i=0; i < (sizeof (icmp6_code) /
		    sizeof(icmp6_code[0])); i++) {
			if (type == icmp6_code[i].type &&
			    !strcmp(w, icmp6_code[i].name))
				return (&icmp6_code[i]);
		}
	}
	return (NULL);
}

void
print_pool(struct pf_pool *pool, u_int16_t p1, u_int16_t p2,
    sa_family_t af, int id)
{
	struct pf_pooladdr	*pooladdr;

	if ((TAILQ_FIRST(&pool->list) != NULL) &&
	    TAILQ_NEXT(TAILQ_FIRST(&pool->list), entries) != NULL)
		printf("{ ");
	TAILQ_FOREACH(pooladdr, &pool->list, entries){
		switch (id) {
		case PF_NAT:
		case PF_RDR:
		case PF_BINAT:
			print_addr(&pooladdr->addr, af, 0);
			break;
		case PF_PASS:
			if (PF_AZERO(&pooladdr->addr.v.a.addr, af))
				printf("%s", pooladdr->ifname);
			else {
				printf("(%s ", pooladdr->ifname);
				print_addr(&pooladdr->addr, af, 0);
				printf(")");
			}
			break;
		default:
			break;
		}
		if (TAILQ_NEXT(pooladdr, entries) != NULL)
			printf(", ");
		else if (TAILQ_NEXT(TAILQ_FIRST(&pool->list), entries) != NULL)
			printf(" }");
	}
	switch (id) {
	case PF_NAT:
		if ((p1 != PF_NAT_PROXY_PORT_LOW ||
		    p2 != PF_NAT_PROXY_PORT_HIGH) && (p1 != 0 || p2 != 0)) {
			if (p1 == p2)
				printf(" port %u", p1);
			else
				printf(" port %u:%u", p1, p2);
		}
		break;
	case PF_RDR:
		if (p1) {
			printf(" port %u", p1);
			if (p2 && (p2 != p1))
				printf(":%u", p2);
		}
		break;
	default:
		break;
	}
	switch (pool->opts & PF_POOL_TYPEMASK) {
	case PF_POOL_NONE:
		break;
	case PF_POOL_BITMASK:
		printf(" bitmask");
		break;
	case PF_POOL_RANDOM:
		printf(" random");
		break;
	case PF_POOL_SRCHASH:
		printf(" source-hash 0x%08x%08x%08x%08x",
		    pool->key.key32[0], pool->key.key32[1],
		    pool->key.key32[2], pool->key.key32[3]);
		break;
	case PF_POOL_ROUNDROBIN:
		printf(" round-robin");
		break;
	}
	if (pool->opts & PF_POOL_STICKYADDR)
		printf(" sticky-address");
	if (id == PF_NAT && p1 == 0 && p2 == 0)
		printf(" static-port");
}

void
print_fromto(struct pf_rule_addr *src, pf_osfp_t osfp, struct pf_rule_addr *dst,
    sa_family_t af, u_int8_t proto, int verbose)
{
	char buf[PF_OSFP_LEN*3];
	if (src->addr.type == PF_ADDR_ADDRMASK &&
	    dst->addr.type == PF_ADDR_ADDRMASK &&
	    PF_AZERO(&src->addr.v.a.addr, AF_INET6) &&
	    PF_AZERO(&src->addr.v.a.mask, AF_INET6) &&
	    PF_AZERO(&dst->addr.v.a.addr, AF_INET6) &&
	    PF_AZERO(&dst->addr.v.a.mask, AF_INET6) &&
	    !src->neg && !dst->neg &&
	    !src->port_op && !dst->port_op &&
	    osfp == PF_OSFP_ANY)
		printf(" all");
	else {
		printf(" from ");
		if (src->neg)
			printf("! ");
		print_addr(&src->addr, af, verbose);
		if (src->port_op)
			print_port(src->port_op, src->port[0],
			    src->port[1],
			    proto == IPPROTO_TCP ? "tcp" : "udp");
		if (osfp != PF_OSFP_ANY)
			printf(" os \"%s\"", pfctl_lookup_fingerprint(osfp, buf,
			    sizeof(buf)));

		printf(" to ");
		if (dst->neg)
			printf("! ");
		print_addr(&dst->addr, af, verbose);
		if (dst->port_op)
			print_port(dst->port_op, dst->port[0],
			    dst->port[1],
			    proto == IPPROTO_TCP ? "tcp" : "udp");
	}
}

void
print_ugid(u_int8_t op, unsigned u1, unsigned u2, const char *t, unsigned umax)
{
	char	a1[11], a2[11];

	snprintf(a1, sizeof(a1), "%u", u1);
	snprintf(a2, sizeof(a2), "%u", u2);
	printf(" %s", t);
	if (u1 == umax && (op == PF_OP_EQ || op == PF_OP_NE))
		print_op(op, "unknown", a2);
	else
		print_op(op, a1, a2);
}

void
print_flags(u_int8_t f)
{
	int	i;

	for (i = 0; tcpflags[i]; ++i)
		if (f & (1 << i))
			printf("%c", tcpflags[i]);
}

void
print_addr(struct pf_addr_wrap *addr, sa_family_t af, int verbose)
{
	switch (addr->type) {
	case PF_ADDR_DYNIFTL:
		printf("(%s", addr->v.ifname);
		if (addr->iflags & PFI_AFLAG_NETWORK)
			printf(":network");
		if (addr->iflags & PFI_AFLAG_BROADCAST)
			printf(":broadcast");
		if (addr->iflags & PFI_AFLAG_PEER)
			printf(":peer");
		if (addr->iflags & PFI_AFLAG_NOALIAS)
			printf(":0");
		if (verbose) {
			if (addr->p.dyncnt <= 0)
				printf(":*");
			else
				printf(":%d", addr->p.dyncnt);
		}
		printf(")");
		break;
	case PF_ADDR_TABLE:
		if (verbose)
			if (addr->p.tblcnt == -1)
				printf("<%s:*>", addr->v.tblname);
			else
				printf("<%s:%d>", addr->v.tblname,
				    addr->p.tblcnt);
		else
			printf("<%s>", addr->v.tblname);
		return;
	case PF_ADDR_RANGE: {
		char buf[48];

		if (inet_ntop(af, &addr->v.a.addr, buf, sizeof(buf)) == NULL)
			printf("?");
		else
			printf("%s", buf);
		if (inet_ntop(af, &addr->v.a.mask, buf, sizeof(buf)) == NULL)
			printf(" - ?");
		else
			printf(" - %s", buf);
		break;
	}
	case PF_ADDR_ADDRMASK:
		if (PF_AZERO(&addr->v.a.addr, AF_INET6) &&
		    PF_AZERO(&addr->v.a.mask, AF_INET6))
			printf("any");
		else {
			char buf[48];

			if (inet_ntop(af, &addr->v.a.addr, buf,
			    sizeof(buf)) == NULL)
				printf("?");
			else
				printf("%s", buf);
		}
		break;
	case PF_ADDR_NOROUTE:
		printf("no-route");
		return;
	case PF_ADDR_URPFFAILED:
		printf("urpf-failed");
		return;
	case PF_ADDR_RTLABEL:
		printf("route \"%s\"", addr->v.rtlabelname);
		return;
	default:
		printf("?");
		return;
	}

	/* mask if not _both_ address and mask are zero */
	if (addr->type != PF_ADDR_RANGE &&
	    !(PF_AZERO(&addr->v.a.addr, AF_INET6) &&
	    PF_AZERO(&addr->v.a.mask, AF_INET6))) {
		int bits = unmask(&addr->v.a.mask, af);

		if (bits != (af == AF_INET ? 32 : 128))
			printf("/%d", bits);
	}
}

void
print_op(u_int8_t op, const char *a1, const char *a2)
{
	if (op == PF_OP_IRG)
		printf(" %s >< %s", a1, a2);
	else if (op == PF_OP_XRG)
		printf(" %s <> %s", a1, a2);
	else if (op == PF_OP_EQ)
		printf(" = %s", a1);
	else if (op == PF_OP_NE)
		printf(" != %s", a1);
	else if (op == PF_OP_LT)
		printf(" < %s", a1);
	else if (op == PF_OP_LE)
		printf(" <= %s", a1);
	else if (op == PF_OP_GT)
		printf(" > %s", a1);
	else if (op == PF_OP_GE)
		printf(" >= %s", a1);
	else if (op == PF_OP_RRG)
		printf(" %s:%s", a1, a2);
}

int
unmask(struct pf_addr *m, sa_family_t af)
{
	int i = 31, j = 0, b = 0;
	u_int32_t tmp;

	while (j < 4 && m->addr32[j] == 0xffffffff) {
		b += 32;
		j++;
	}
	if (j < 4) {
		tmp = ntohl(m->addr32[j]);
		for (i = 31; tmp & (1 << i); --i)
			b++;
	}
	return (b);
}
void
print_port(u_int8_t op, u_int16_t p1, u_int16_t p2, const char *proto)
{
	char		 a1[6], a2[6];
	struct servent	*s;

	s = getservbyport(p1, proto);
	p1 = ntohs(p1);
	p2 = ntohs(p2);
	snprintf(a1, sizeof(a1), "%u", p1);
	snprintf(a2, sizeof(a2), "%u", p2);
	printf(" port");
	if (s != NULL && (op == PF_OP_EQ || op == PF_OP_NE))
		print_op(op, s->s_name, a2);
	else
		print_op(op, a1, a2);
}

/* Lookup a fingerprint name by ID */
char *
pfctl_lookup_fingerprint(pf_osfp_t fp, char *buf, size_t len)
{
	int class, version, subtype;
	struct name_list *list;
	struct name_entry *nm;

	char *class_name, *version_name, *subtype_name;
	class_name = version_name = subtype_name = NULL;

	if (fp == PF_OSFP_UNKNOWN) {
		strlcpy(buf, "unknown", len);
		return (buf);
	}
	if (fp == PF_OSFP_ANY) {
		strlcpy(buf, "any", len);
		return (buf);
	}

	PF_OSFP_UNPACK(fp, class, version, subtype);
	if (class >= (1 << _FP_CLASS_BITS) ||
	    version >= (1 << _FP_VERSION_BITS) ||
	    subtype >= (1 << _FP_SUBTYPE_BITS)) {
		warnx("PF_OSFP_UNPACK(0x%x) failed!!", fp);
		strlcpy(buf, "nomatch", len);
		return (buf);
	}

	LIST_FOREACH(nm, &classes, nm_entry) {
		if (nm->nm_num == class) {
			class_name = nm->nm_name;
			if (version == PF_OSFP_ANY)
				goto found;
			list = &nm->nm_sublist;
			LIST_FOREACH(nm, list, nm_entry) {
				if (nm->nm_num == version) {
					version_name = nm->nm_name;
					if (subtype == PF_OSFP_ANY)
						goto found;
					list = &nm->nm_sublist;
					LIST_FOREACH(nm, list, nm_entry) {
						if (nm->nm_num == subtype) {
							subtype_name =
							    nm->nm_name;
							goto found;
						}
					} /* foreach subtype */
					strlcpy(buf, "nomatch", len);
					return (buf);
				}
			} /* foreach version */
			strlcpy(buf, "nomatch", len);
			return (buf);
		}
	} /* foreach class */

	strlcpy(buf, "nomatch", len);
	return (buf);

found:
	snprintf(buf, len, "%s", class_name);
	if (version_name) {
		strlcat(buf, " ", len);
		strlcat(buf, version_name, len);
		if (subtype_name) {
			if (strchr(version_name, ' '))
				strlcat(buf, " ", len);
			else if (strchr(version_name, '.') &&
			    isdigit(*subtype_name))
				strlcat(buf, ".", len);
			else
				strlcat(buf, " ", len);
			strlcat(buf, subtype_name, len);
		}
	}
	return (buf);
}


