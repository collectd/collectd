/*
 * collectd - src/utils_dns.c
 * Modifications Copyright (C) 2006  Florian octo Forster
 * Copyright (C) 2002  The Measurement Factory, Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of The Measurement Factory nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors:
 *   The Measurement Factory, Inc. <http://www.measurement-factory.com/>
 *   Florian octo Forster <octo at verplant.org>
 */

#define _BSD_SOURCE

#include "collectd.h"
#include "plugin.h"
#include "common.h"

#if HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#if HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#if HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif

#if HAVE_ARPA_NAMESER_H
# include <arpa/nameser.h>
#endif
#if HAVE_ARPA_NAMESER_COMPAT_H
# include <arpa/nameser_compat.h>
#endif

#if HAVE_NET_IF_ARP_H
# include <net/if_arp.h>
#endif
#if HAVE_NET_IF_H
# include <net/if.h>
#endif
#if HAVE_NETINET_IF_ETHER_H
# include <netinet/if_ether.h>
#endif
#if HAVE_NET_PPP_DEFS_H
# include <net/ppp_defs.h>
#endif
#if HAVE_NET_IF_PPP_H
# include <net/if_ppp.h>
#endif

#if HAVE_NETDB_H
# include <netdb.h>
#endif

#if HAVE_NETINET_IP_H
# include <netinet/ip.h>
#endif
#ifdef HAVE_NETINET_IP_VAR_H
# include <netinet/ip_var.h>
#endif
#if HAVE_NETINET_IP6_H
# include <netinet/ip6.h>
#endif
#if HAVE_NETINET_UDP_H
# include <netinet/udp.h>
#endif

#if HAVE_PCAP_H
# include <pcap.h>
#endif

#define PCAP_SNAPLEN 1460
#ifndef ETHER_HDR_LEN
#define ETHER_ADDR_LEN 6
#define ETHER_TYPE_LEN 2
#define ETHER_HDR_LEN (ETHER_ADDR_LEN * 2 + ETHER_TYPE_LEN)
#endif
#ifndef ETHERTYPE_8021Q
# define ETHERTYPE_8021Q 0x8100
#endif
#ifndef ETHERTYPE_IPV6
# define ETHERTYPE_IPV6 0x86DD
#endif

#ifndef PPP_ADDRESS_VAL
# define PPP_ADDRESS_VAL 0xff	/* The address byte value */
#endif
#ifndef PPP_CONTROL_VAL
# define PPP_CONTROL_VAL 0x03	/* The control byte value */
#endif

#if HAVE_STRUCT_UDPHDR_UH_DPORT && HAVE_STRUCT_UDPHDR_UH_SPORT
# define UDP_DEST uh_dport
# define UDP_SRC  uh_dport
#elif HAVE_STRUCT_UDPHDR_DEST && HAVE_STRUCT_UDPHDR_SOURCE
# define UDP_DEST dest
# define UDP_SRC  source
#else
# error "`struct udphdr' is unusable."
#endif

#include "utils_dns.h"

/*
 * Type definitions
 */
struct ip_list_s
{
    struct in6_addr addr;
    void *data;
    struct ip_list_s *next;
};
typedef struct ip_list_s ip_list_t;

typedef int (printer)(const char *, ...);

/*
 * flags/features for non-interactive mode
 */

#ifndef T_A6
#define T_A6 38
#endif
#ifndef T_SRV
#define T_SRV 33
#endif

/*
 * Global variables
 */
int qtype_counts[T_MAX];
int opcode_counts[OP_MAX];
int qclass_counts[C_MAX];

#if HAVE_PCAP_H
static pcap_t *pcap_obj = NULL;
#endif

static ip_list_t *IgnoreList = NULL;

#if HAVE_PCAP_H
static void (*Callback) (const rfc1035_header_t *) = NULL;

static int query_count_intvl = 0;
static int query_count_total = 0;
# ifdef __OpenBSD__
static struct bpf_timeval last_ts;
# else
static struct timeval last_ts;
# endif /* __OpenBSD__ */
#endif /* HAVE_PCAP_H */

static int cmp_in6_addr (const struct in6_addr *a,
	const struct in6_addr *b)
{
    int i;

    assert (sizeof (struct in6_addr) == 16);

    for (i = 0; i < 16; i++)
	if (a->s6_addr[i] != b->s6_addr[i])
	    break;

    if (i >= 16)
	return (0);

    return (a->s6_addr[i] > b->s6_addr[i] ? 1 : -1);
} /* int cmp_addrinfo */

static inline int ignore_list_match (const struct in6_addr *addr)
{
    ip_list_t *ptr;

    for (ptr = IgnoreList; ptr != NULL; ptr = ptr->next)
	if (cmp_in6_addr (addr, &ptr->addr) == 0)
	    return (1);
    return (0);
} /* int ignore_list_match */

static void ignore_list_add (const struct in6_addr *addr)
{
    ip_list_t *new;

    if (ignore_list_match (addr) != 0)
	return;

    new = malloc (sizeof (ip_list_t));
    if (new == NULL)
    {
	perror ("malloc");
	return;
    }

    memcpy (&new->addr, addr, sizeof (struct in6_addr));
    new->next = IgnoreList;

    IgnoreList = new;
} /* void ignore_list_add */

void ignore_list_add_name (const char *name)
{
    struct addrinfo *ai_list;
    struct addrinfo *ai_ptr;
    struct in6_addr  addr;
    int status;

    status = getaddrinfo (name, NULL, NULL, &ai_list);
    if (status != 0)
	return;

    for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
    {
	if (ai_ptr->ai_family == AF_INET)
	{
	    memset (&addr, '\0', sizeof (addr));
	    addr.s6_addr[10] = 0xFF;
	    addr.s6_addr[11] = 0xFF;
	    memcpy (addr.s6_addr + 12, &((struct sockaddr_in *) ai_ptr->ai_addr)->sin_addr, 4);

	    ignore_list_add (&addr);
	}
	else
	{
	    ignore_list_add (&((struct sockaddr_in6 *) ai_ptr->ai_addr)->sin6_addr);
	}
    } /* for */

    freeaddrinfo (ai_list);
}

#if HAVE_PCAP_H
static void in6_addr_from_buffer (struct in6_addr *ia,
	const void *buf, size_t buf_len,
	int family)
{
    memset (ia, 0, sizeof (struct in6_addr));
    if ((AF_INET == family) && (sizeof (uint32_t) == buf_len))
    {
	ia->s6_addr[10] = 0xFF;
	ia->s6_addr[11] = 0xFF;
	memcpy (ia->s6_addr + 12, buf, buf_len);
    }
    else if ((AF_INET6 == family) && (sizeof (struct in6_addr) == buf_len))
    {
	memcpy (ia, buf, buf_len);
    }
} /* void in6_addr_from_buffer */

void dnstop_set_pcap_obj (pcap_t *po)
{
	pcap_obj = po;
}

void dnstop_set_callback (void (*cb) (const rfc1035_header_t *))
{
	Callback = cb;
}

#define RFC1035_MAXLABELSZ 63
static int
rfc1035NameUnpack(const char *buf, size_t sz, off_t * off, char *name, size_t ns
)
{
    off_t no = 0;
    unsigned char c;
    size_t len;
    static int loop_detect = 0;
    if (loop_detect > 2)
	return 4;		/* compression loop */
    if (ns <= 0)
	return 4;		/* probably compression loop */
    do {
	if ((*off) >= sz)
	    break;
	c = *(buf + (*off));
	if (c > 191) {
	    /* blasted compression */
	    int rc;
	    unsigned short s;
	    off_t ptr;
	    memcpy(&s, buf + (*off), sizeof(s));
	    s = ntohs(s);
	    (*off) += sizeof(s);
	    /* Sanity check */
	    if ((*off) >= sz)
		return 1;	/* message too short */
	    ptr = s & 0x3FFF;
	    /* Make sure the pointer is inside this message */
	    if (ptr >= sz)
		return 2;	/* bad compression ptr */
	    if (ptr < DNS_MSG_HDR_SZ)
		return 2;	/* bad compression ptr */
	    loop_detect++;
	    rc = rfc1035NameUnpack(buf, sz, &ptr, name + no, ns - no);
	    loop_detect--;
	    return rc;
	} else if (c > RFC1035_MAXLABELSZ) {
	    /*
	     * "(The 10 and 01 combinations are reserved for future use.)"
	     */
	    return 3;		/* reserved label/compression flags */
	    break;
	} else {
	    (*off)++;
	    len = (size_t) c;
	    if (len == 0)
		break;
	    if (len > (ns - 1))
		len = ns - 1;
	    if ((*off) + len > sz)
		return 4;	/* message is too short */
	    if (no + len + 1 > ns)
		return 5;	/* qname would overflow name buffer */
	    memcpy(name + no, buf + (*off), len);
	    (*off) += len;
	    no += len;
	    *(name + (no++)) = '.';
	}
    } while (c > 0);
    if (no > 0)
	*(name + no - 1) = '\0';
    /* make sure we didn't allow someone to overflow the name buffer */
    assert(no <= ns);
    return 0;
}

static int
handle_dns(const char *buf, int len)
{
    rfc1035_header_t qh;
    uint16_t us;
    off_t offset;
    char *t;
    int status;

    /* The DNS header is 12 bytes long */
    if (len < DNS_MSG_HDR_SZ)
	return 0;

    memcpy(&us, buf + 0, 2);
    qh.id = ntohs(us);

    memcpy(&us, buf + 2, 2);
    us = ntohs(us);
    qh.qr = (us >> 15) & 0x01;
    qh.opcode = (us >> 11) & 0x0F;
    qh.aa = (us >> 10) & 0x01;
    qh.tc = (us >> 9) & 0x01;
    qh.rd = (us >> 8) & 0x01;
    qh.ra = (us >> 7) & 0x01;
    qh.z  = (us >> 6) & 0x01;
    qh.ad = (us >> 5) & 0x01;
    qh.cd = (us >> 4) & 0x01;
    qh.rcode = us & 0x0F;

    memcpy(&us, buf + 4, 2);
    qh.qdcount = ntohs(us);

    memcpy(&us, buf + 6, 2);
    qh.ancount = ntohs(us);

    memcpy(&us, buf + 8, 2);
    qh.nscount = ntohs(us);

    memcpy(&us, buf + 10, 2);
    qh.arcount = ntohs(us);

    offset = DNS_MSG_HDR_SZ;
    memset(qh.qname, '\0', MAX_QNAME_SZ);
    status = rfc1035NameUnpack(buf, len, &offset, qh.qname, MAX_QNAME_SZ);
    if (status != 0)
    {
	INFO ("utils_dns: handle_dns: rfc1035NameUnpack failed "
		"with status %i.", status);
	return 0;
    }
    if ('\0' == qh.qname[0])
	sstrncpy (qh.qname, ".", sizeof (qh.qname));
    while ((t = strchr(qh.qname, '\n')))
	*t = ' ';
    while ((t = strchr(qh.qname, '\r')))
	*t = ' ';
    for (t = qh.qname; *t; t++)
	*t = tolower((int) *t);

    memcpy(&us, buf + offset, 2);
    qh.qtype = ntohs(us);
    memcpy(&us, buf + offset + 2, 2);
    qh.qclass = ntohs(us);

    qh.length = (uint16_t) len;

    /* gather stats */
    qtype_counts[qh.qtype]++;
    qclass_counts[qh.qclass]++;
    opcode_counts[qh.opcode]++;

    if (Callback != NULL)
	    Callback (&qh);

    return 1;
}

static int
handle_udp(const struct udphdr *udp, int len)
{
    char buf[PCAP_SNAPLEN];
    if ((ntohs (udp->UDP_DEST) != 53)
		    && (ntohs (udp->UDP_SRC) != 53))
	return 0;
    memcpy(buf, udp + 1, len - sizeof(*udp));
    if (0 == handle_dns(buf, len - sizeof(*udp)))
	return 0;
    return 1;
}

#if HAVE_NETINET_IP6_H
static int
handle_ipv6 (struct ip6_hdr *ipv6, int len)
{
    char buf[PCAP_SNAPLEN];
    unsigned int offset;
    int nexthdr;

    struct in6_addr s_addr;
    uint16_t payload_len;

    if (0 > len)
	return (0);

    offset = sizeof (struct ip6_hdr);
    nexthdr = ipv6->ip6_nxt;
    s_addr = ipv6->ip6_src;
    payload_len = ntohs (ipv6->ip6_plen);

    if (ignore_list_match (&s_addr))
	    return (0);

    /* Parse extension headers. This only handles the standard headers, as
     * defined in RFC 2460, correctly. Fragments are discarded. */
    while ((IPPROTO_ROUTING == nexthdr) /* routing header */
	    || (IPPROTO_HOPOPTS == nexthdr) /* Hop-by-Hop options. */
	    || (IPPROTO_FRAGMENT == nexthdr) /* fragmentation header. */
	    || (IPPROTO_DSTOPTS == nexthdr) /* destination options. */
	    || (IPPROTO_DSTOPTS == nexthdr) /* destination options. */
	    || (IPPROTO_AH == nexthdr) /* destination options. */
	    || (IPPROTO_ESP == nexthdr)) /* encapsulating security payload. */
    {
	struct ip6_ext ext_hdr;
	uint16_t ext_hdr_len;

	/* Catch broken packets */
	if ((offset + sizeof (struct ip6_ext)) > (unsigned int)len)
	    return (0);

	/* Cannot handle fragments. */
	if (IPPROTO_FRAGMENT == nexthdr)
	    return (0);

	memcpy (&ext_hdr, (char *) ipv6 + offset, sizeof (struct ip6_ext));
	nexthdr = ext_hdr.ip6e_nxt;
	ext_hdr_len = (8 * (ntohs (ext_hdr.ip6e_len) + 1));

	/* This header is longer than the packets payload.. WTF? */
	if (ext_hdr_len > payload_len)
	    return (0);

	offset += ext_hdr_len;
	payload_len -= ext_hdr_len;
    } /* while */

    /* Catch broken and empty packets */
    if (((offset + payload_len) > (unsigned int)len)
	    || (payload_len == 0)
	    || (payload_len > PCAP_SNAPLEN))
	return (0);

    if (IPPROTO_UDP != nexthdr)
	return (0);

    memcpy (buf, (char *) ipv6 + offset, payload_len);
    if (handle_udp ((struct udphdr *) buf, payload_len) == 0)
	return (0);

    return (1); /* Success */
} /* int handle_ipv6 */
/* #endif HAVE_NETINET_IP6_H */

#else /* if !HAVE_NETINET_IP6_H */
static int
handle_ipv6 (__attribute__((unused)) void *pkg,
	__attribute__((unused)) int len)
{
    return (0);
}
#endif /* !HAVE_NETINET_IP6_H */

static int
handle_ip(const struct ip *ip, int len)
{
    char buf[PCAP_SNAPLEN];
    int offset = ip->ip_hl << 2;
    struct in6_addr s_addr;
    struct in6_addr d_addr;

    if (ip->ip_v == 6)
	return (handle_ipv6 ((void *) ip, len));

    in6_addr_from_buffer (&s_addr, &ip->ip_src.s_addr, sizeof (ip->ip_src.s_addr), AF_INET);
    in6_addr_from_buffer (&d_addr, &ip->ip_dst.s_addr, sizeof (ip->ip_dst.s_addr), AF_INET);
    if (ignore_list_match (&s_addr))
	    return (0);
    if (IPPROTO_UDP != ip->ip_p)
	return 0;
    memcpy(buf, (void *) ip + offset, len - offset);
    if (0 == handle_udp((struct udphdr *) buf, len - offset))
	return 0;
    return 1;
}

#if HAVE_NET_IF_PPP_H
static int
handle_ppp(const u_char * pkt, int len)
{
    char buf[PCAP_SNAPLEN];
    unsigned short us;
    unsigned short proto;
    if (len < 2)
	return 0;
    if (*pkt == PPP_ADDRESS_VAL && *(pkt + 1) == PPP_CONTROL_VAL) {
	pkt += 2;		/* ACFC not used */
	len -= 2;
    }
    if (len < 2)
	return 0;
    if (*pkt % 2) {
	proto = *pkt;		/* PFC is used */
	pkt++;
	len--;
    } else {
	memcpy(&us, pkt, sizeof(us));
	proto = ntohs(us);
	pkt += 2;
	len -= 2;
    }
    if (ETHERTYPE_IP != proto && PPP_IP != proto)
	return 0;
    memcpy(buf, pkt, len);
    return handle_ip((struct ip *) buf, len);
}
#endif /* HAVE_NET_IF_PPP_H */

static int
handle_null(const u_char * pkt, int len)
{
    unsigned int family;
    memcpy(&family, pkt, sizeof(family));
    if (AF_INET != family)
	return 0;
    return handle_ip((struct ip *) (pkt + 4), len - 4);
}

#ifdef DLT_LOOP
static int
handle_loop(const u_char * pkt, int len)
{
    unsigned int family;
    memcpy(&family, pkt, sizeof(family));
    if (AF_INET != ntohl(family))
	return 0;
    return handle_ip((struct ip *) (pkt + 4), len - 4);
}

#endif

#ifdef DLT_RAW
static int
handle_raw(const u_char * pkt, int len)
{
    return handle_ip((struct ip *) pkt, len);
}

#endif

static int
handle_ether(const u_char * pkt, int len)
{
    char buf[PCAP_SNAPLEN];
    struct ether_header *e = (void *) pkt;
    unsigned short etype = ntohs(e->ether_type);
    if (len < ETHER_HDR_LEN)
	return 0;
    pkt += ETHER_HDR_LEN;
    len -= ETHER_HDR_LEN;
    if (ETHERTYPE_8021Q == etype) {
	etype = ntohs(*(unsigned short *) (pkt + 2));
	pkt += 4;
	len -= 4;
    }
    if ((ETHERTYPE_IP != etype)
	    && (ETHERTYPE_IPV6 != etype))
	return 0;
    memcpy(buf, pkt, len);
    if (ETHERTYPE_IPV6 == etype)
	return (handle_ipv6 ((void *) buf, len));
    else
	return handle_ip((struct ip *) buf, len);
}

#ifdef DLT_LINUX_SLL
static int
handle_linux_sll (const u_char *pkt, int len)
{
    struct sll_header
    {
	uint16_t pkt_type;
	uint16_t dev_type;
	uint16_t addr_len;
	uint8_t  addr[8];
	uint16_t proto_type;
    } *hdr;
    uint16_t etype;

    if ((0 > len) || ((unsigned int)len < sizeof (struct sll_header)))
	return (0);

    hdr  = (struct sll_header *) pkt;
    pkt  = (u_char *) (hdr + 1);
    len -= sizeof (struct sll_header);

    etype = ntohs (hdr->proto_type);

    if ((ETHERTYPE_IP != etype)
	    && (ETHERTYPE_IPV6 != etype))
	return 0;

    if (ETHERTYPE_IPV6 == etype)
	return (handle_ipv6 ((void *) pkt, len));
    else
	return handle_ip((struct ip *) pkt, len);
}
#endif /* DLT_LINUX_SLL */

/* public function */
void handle_pcap(u_char *udata, const struct pcap_pkthdr *hdr, const u_char *pkt)
{
    int status;

    if (hdr->caplen < ETHER_HDR_LEN)
	return;

    switch (pcap_datalink (pcap_obj))
    {
	case DLT_EN10MB:
	    status = handle_ether (pkt, hdr->caplen);
	    break;
#if HAVE_NET_IF_PPP_H
	case DLT_PPP:
	    status = handle_ppp (pkt, hdr->caplen);
	    break;
#endif
#ifdef DLT_LOOP
	case DLT_LOOP:
	    status = handle_loop (pkt, hdr->caplen);
	    break;
#endif
#ifdef DLT_RAW
	case DLT_RAW:
	    status = handle_raw (pkt, hdr->caplen);
	    break;
#endif
#ifdef DLT_LINUX_SLL
	case DLT_LINUX_SLL:
	    status = handle_linux_sll (pkt, hdr->caplen);
	    break;
#endif
	case DLT_NULL:
	    status = handle_null (pkt, hdr->caplen);
	    break;

	default:
	    ERROR ("handle_pcap: unsupported data link type %d",
		    pcap_datalink(pcap_obj));
	    status = 0;
	    break;
    } /* switch (pcap_datalink(pcap_obj)) */

    if (0 == status)
	return;

    query_count_intvl++;
    query_count_total++;
    last_ts = hdr->ts;
}
#endif /* HAVE_PCAP_H */

const char *qtype_str(int t)
{
    static char buf[32];
    switch (t) {
#if (defined (__NAMESER)) && (__NAMESER >= 19991001)
	    case ns_t_a:        return ("A");
	    case ns_t_ns:       return ("NS");
	    case ns_t_md:       return ("MD");
	    case ns_t_mf:       return ("MF");
	    case ns_t_cname:    return ("CNAME");
	    case ns_t_soa:      return ("SOA");
	    case ns_t_mb:       return ("MB");
	    case ns_t_mg:       return ("MG");
	    case ns_t_mr:       return ("MR");
	    case ns_t_null:     return ("NULL");
	    case ns_t_wks:      return ("WKS");
	    case ns_t_ptr:      return ("PTR");
	    case ns_t_hinfo:    return ("HINFO");
	    case ns_t_minfo:    return ("MINFO");
	    case ns_t_mx:       return ("MX");
	    case ns_t_txt:      return ("TXT");
	    case ns_t_rp:       return ("RP");
	    case ns_t_afsdb:    return ("AFSDB");
	    case ns_t_x25:      return ("X25");
	    case ns_t_isdn:     return ("ISDN");
	    case ns_t_rt:       return ("RT");
	    case ns_t_nsap:     return ("NSAP");
	    case ns_t_nsap_ptr: return ("NSAP-PTR");
	    case ns_t_sig:      return ("SIG");
	    case ns_t_key:      return ("KEY");
	    case ns_t_px:       return ("PX");
	    case ns_t_gpos:     return ("GPOS");
	    case ns_t_aaaa:     return ("AAAA");
	    case ns_t_loc:      return ("LOC");
	    case ns_t_nxt:      return ("NXT");
	    case ns_t_eid:      return ("EID");
	    case ns_t_nimloc:   return ("NIMLOC");
	    case ns_t_srv:      return ("SRV");
	    case ns_t_atma:     return ("ATMA");
	    case ns_t_naptr:    return ("NAPTR");
	    case ns_t_kx:       return ("KX");
	    case ns_t_cert:     return ("CERT");
	    case ns_t_a6:       return ("A6");
	    case ns_t_dname:    return ("DNAME");
	    case ns_t_sink:     return ("SINK");
	    case ns_t_opt:      return ("OPT");
# if __NAMESER >= 19991006
	    case ns_t_tsig:     return ("TSIG");
# endif
	    case ns_t_ixfr:     return ("IXFR");
	    case ns_t_axfr:     return ("AXFR");
	    case ns_t_mailb:    return ("MAILB");
	    case ns_t_maila:    return ("MAILA");
	    case ns_t_any:      return ("ANY");
	    case ns_t_zxfr:     return ("ZXFR");
/* #endif __NAMESER >= 19991006 */
#elif (defined (__BIND)) && (__BIND >= 19950621)
	    case T_A:		return ("A"); /* 1 ... */
	    case T_NS:		return ("NS");
	    case T_MD:		return ("MD");
	    case T_MF:		return ("MF");
	    case T_CNAME:	return ("CNAME");
	    case T_SOA:		return ("SOA");
	    case T_MB:		return ("MB");
	    case T_MG:		return ("MG");
	    case T_MR:		return ("MR");
	    case T_NULL:	return ("NULL");
	    case T_WKS:		return ("WKS");
	    case T_PTR:		return ("PTR");
	    case T_HINFO:	return ("HINFO");
	    case T_MINFO:	return ("MINFO");
	    case T_MX:		return ("MX");
	    case T_TXT:		return ("TXT");
	    case T_RP:		return ("RP");
	    case T_AFSDB:	return ("AFSDB");
	    case T_X25:		return ("X25");
	    case T_ISDN:	return ("ISDN");
	    case T_RT:		return ("RT");
	    case T_NSAP:	return ("NSAP");
	    case T_NSAP_PTR:	return ("NSAP_PTR");
	    case T_SIG:		return ("SIG");
	    case T_KEY:		return ("KEY");
	    case T_PX:		return ("PX");
	    case T_GPOS:	return ("GPOS");
	    case T_AAAA:	return ("AAAA");
	    case T_LOC:		return ("LOC");
	    case T_NXT:		return ("NXT");
	    case T_EID:		return ("EID");
	    case T_NIMLOC:	return ("NIMLOC");
	    case T_SRV:		return ("SRV");
	    case T_ATMA:	return ("ATMA");
	    case T_NAPTR:	return ("NAPTR"); /* ... 35 */
#if (__BIND >= 19960801)
	    case T_KX:		return ("KX"); /* 36 ... */
	    case T_CERT:	return ("CERT");
	    case T_A6:		return ("A6");
	    case T_DNAME:	return ("DNAME");
	    case T_SINK:	return ("SINK");
	    case T_OPT:		return ("OPT");
	    case T_APL:		return ("APL");
	    case T_DS:		return ("DS");
	    case T_SSHFP:	return ("SSHFP");
	    case T_RRSIG:	return ("RRSIG");
	    case T_NSEC:	return ("NSEC");
	    case T_DNSKEY:	return ("DNSKEY"); /* ... 48 */
	    case T_TKEY:	return ("TKEY"); /* 249 */
#endif /* __BIND >= 19960801 */
	    case T_TSIG:	return ("TSIG"); /* 250 ... */
	    case T_IXFR:	return ("IXFR");
	    case T_AXFR:	return ("AXFR");
	    case T_MAILB:	return ("MAILB");
	    case T_MAILA:	return ("MAILA");
	    case T_ANY:		return ("ANY"); /* ... 255 */
#endif /* __BIND >= 19950621 */
	    default:
		    ssnprintf (buf, sizeof (buf), "#%i", t);
		    return (buf);
    }; /* switch (t) */
    /* NOTREACHED */
    return (NULL);
}

const char *opcode_str (int o)
{
    static char buf[30];
    switch (o) {
    case 0:
	return "Query";
	break;
    case 1:
	return "Iquery";
	break;
    case 2:
	return "Status";
	break;
    case 4:
	return "Notify";
	break;
    case 5:
	return "Update";
	break;
    default:
	ssnprintf(buf, sizeof (buf), "Opcode%d", o);
	return buf;
    }
    /* NOTREACHED */
}

const char *rcode_str (int rcode)
{
	static char buf[32];
	switch (rcode)
	{
#if (defined (__NAMESER)) && (__NAMESER >= 19991006)
		case ns_r_noerror:  return ("NOERROR");
		case ns_r_formerr:  return ("FORMERR");
		case ns_r_servfail: return ("SERVFAIL");
		case ns_r_nxdomain: return ("NXDOMAIN");
		case ns_r_notimpl:  return ("NOTIMPL");
		case ns_r_refused:  return ("REFUSED");
		case ns_r_yxdomain: return ("YXDOMAIN");
		case ns_r_yxrrset:  return ("YXRRSET");
		case ns_r_nxrrset:  return ("NXRRSET");
		case ns_r_notauth:  return ("NOTAUTH");
		case ns_r_notzone:  return ("NOTZONE");
		case ns_r_max:      return ("MAX");
		case ns_r_badsig:   return ("BADSIG");
		case ns_r_badkey:   return ("BADKEY");
		case ns_r_badtime:  return ("BADTIME");
/* #endif __NAMESER >= 19991006 */
#elif (defined (__BIND)) && (__BIND >= 19950621)
		case NOERROR:	    return ("NOERROR");
		case FORMERR:	    return ("FORMERR");
		case SERVFAIL:	    return ("SERVFAIL");
		case NXDOMAIN:	    return ("NXDOMAIN");
		case NOTIMP:	    return ("NOTIMP");
		case REFUSED:	    return ("REFUSED");
#if defined (YXDOMAIN) && defined (NXRRSET)
		case YXDOMAIN:	    return ("YXDOMAIN");
		case YXRRSET:	    return ("YXRRSET");
		case NXRRSET:	    return ("NXRRSET");
		case NOTAUTH:	    return ("NOTAUTH");
		case NOTZONE:	    return ("NOTZONE");
#endif  /* RFC2136 rcodes */
#endif /* __BIND >= 19950621 */
		default:
			ssnprintf (buf, sizeof (buf), "RCode%i", rcode);
			return (buf);
	}
	/* Never reached */
	return (NULL);
} /* const char *rcode_str (int rcode) */

#if 0
static int
main(int argc, char *argv[])
{
    char errbuf[PCAP_ERRBUF_SIZE];
    int x;
    struct stat sb;
    int readfile_state = 0;
    struct bpf_program fp;

    port53 = htons(53);
    SubReport = Sources_report;
    ignore_addr.s_addr = 0;
    progname = strdup(strrchr(argv[0], '/') ? strchr(argv[0], '/') + 1 : argv[0]);
    srandom(time(NULL));
    ResetCounters();

    while ((x = getopt(argc, argv, "ab:f:i:pst")) != -1) {
	switch (x) {
	case 'a':
	    anon_flag = 1;
	    break;
	case 's':
	    sld_flag = 1;
	    break;
	case 't':
	    nld_flag = 1;
	    break;
	case 'p':
	    promisc_flag = 0;
	    break;
	case 'b':
	    bpf_program_str = strdup(optarg);
	    break;
	case 'i':
	    ignore_addr.s_addr = inet_addr(optarg);
	    break;
	case 'f':
	    set_filter(optarg);
	    break;
	default:
	    usage();
	    break;
	}
    }
    argc -= optind;
    argv += optind;

    if (argc < 1)
	usage();
    device = strdup(argv[0]);

    if (0 == stat(device, &sb))
	readfile_state = 1;
    if (readfile_state) {
	pcap_obj = pcap_open_offline(device, errbuf);
    } else {
	pcap_obj = pcap_open_live(device, PCAP_SNAPLEN, promisc_flag, 1000, errbuf);
    }
    if (NULL == pcap_obj) {
	fprintf(stderr, "pcap_open_*: %s\n", errbuf);
	exit(1);
    }

    if (0 == isatty(1)) {
	if (0 == readfile_state) {
	    fprintf(stderr, "Non-interactive mode requires savefile argument\n");
	    exit(1);
	}
	interactive = 0;
	print_func = printf;
    }

    memset(&fp, '\0', sizeof(fp));
    x = pcap_compile(pcap_obj, &fp, bpf_program_str, 1, 0);
    if (x < 0) {
	fprintf(stderr, "pcap_compile failed\n");
	exit(1);
    }
    x = pcap_setfilter(pcap_obj, &fp);
    if (x < 0) {
	fprintf(stderr, "pcap_setfilter failed\n");
	exit(1);
    }

    /*
     * non-blocking call added for Mac OS X bugfix.  Sent by Max Horn.
     * ref http://www.tcpdump.org/lists/workers/2002/09/msg00033.html
     */
    x = pcap_setnonblock(pcap_obj, 1, errbuf);
    if (x < 0) {
	fprintf(stderr, "pcap_setnonblock failed: %s\n", errbuf);
	exit(1);
    }

    switch (pcap_datalink(pcap_obj)) {
    case DLT_EN10MB:
	handle_datalink = handle_ether;
	break;
#if HAVE_NET_IF_PPP_H
    case DLT_PPP:
	handle_datalink = handle_ppp;
	break;
#endif
#ifdef DLT_LOOP
    case DLT_LOOP:
	handle_datalink = handle_loop;
	break;
#endif
#ifdef DLT_RAW
    case DLT_RAW:
	handle_datalink = handle_raw;
	break;
#endif
    case DLT_NULL:
	handle_datalink = handle_null;
	break;
    default:
	fprintf(stderr, "unsupported data link type %d\n",
	    pcap_datalink(pcap_obj));
	return 1;
	break;
    }
    if (interactive) {
	init_curses();
	while (0 == Quit) {
	    if (readfile_state < 2) {
		/*
		 * On some OSes select() might return 0 even when
		 * there are packets to process.  Thus, we always
		 * ignore its return value and just call pcap_dispatch()
		 * anyway.
		 */
		if (0 == readfile_state) 	/* interactive */
		    pcap_select(pcap_obj, 1, 0);
		x = pcap_dispatch(pcap_obj, 50, handle_pcap, NULL);
	    }
	    if (0 == x && 1 == readfile_state) {
		/* block on keyboard until user quits */
		readfile_state++;
		nodelay(w, 0);
	    }
	    keyboard();
	    cron_pre();
	    report();
	    cron_post();
	}
	endwin();		/* klin, Thu Nov 28 08:56:51 2002 */
    } else {
	while (pcap_dispatch(pcap_obj, 50, handle_pcap, NULL))
		(void) 0;
	cron_pre();
	Sources_report(); print_func("\n");
	Destinatioreport(); print_func("\n");
	Qtypes_report(); print_func("\n");
	Opcodes_report(); print_func("\n");
	Tld_report(); print_func("\n");
	Sld_report(); print_func("\n");
	Nld_report(); print_func("\n");
	SldBySource_report();
    }

    pcap_close(pcap_obj);
    return 0;
} /* static int main(int argc, char *argv[]) */
#endif
/*
 * vim:shiftwidth=4:tabstop=8:softtabstop=4
 */
