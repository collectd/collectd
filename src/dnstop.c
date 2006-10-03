/*
 * collectd - src/dnstop.c
 * Copyright (C) 2006  Florian octo Forster
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

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <netinet/in.h>

#include <pcap.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <curses.h>
#include <assert.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#ifdef __APPLE__
#include <arpa/nameser_compat.h>
#endif

#include <sys/socket.h>
#include <net/if_arp.h>
#include <net/if.h>
#include <netinet/if_ether.h>

#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/udp.h>

#define PCAP_SNAPLEN 1460
#define MAX_QNAME_SZ 512
#ifndef ETHER_HDR_LEN
#define ETHER_ADDR_LEN 6
#define ETHER_TYPE_LEN 2
#define ETHER_HDR_LEN (ETHER_ADDR_LEN * 2 + ETHER_TYPE_LEN)
#endif
#ifndef ETHERTYPE_8021Q
#define ETHERTYPE_8021Q 0x8100
#endif

#if USE_PPP
#include <net/if_ppp.h>
#define PPP_ADDRESS_VAL       0xff	/* The address byte value */
#define PPP_CONTROL_VAL       0x03	/* The control byte value */
#endif

#ifdef __linux__
#define uh_dport dest
#endif

#include "dnstop.h"

/*
 * Type definitions
 */
typedef struct _AgentAddr AgentAddr;
struct _AgentAddr {
    struct in_addr src;
    int count;
    AgentAddr *next;
};

typedef struct _StringCounter StringCounter;
struct _StringCounter {
    char *s;
    int count;
    StringCounter *next;
};

/* This struct cobbles together Source and Sld */
typedef struct _StringAddrCounter StringAddrCounter;
struct _StringAddrCounter {
    struct in_addr src;
    char *str;
    int count;
    StringAddrCounter *next;
};

typedef struct _foo foo;
struct _foo {
    int cnt;
    void *ptr;
};

typedef struct _rfc1035_header rfc1035_header;
struct _rfc1035_header {
    unsigned short id;
    unsigned int qr:1;
    unsigned int opcode:4;
    unsigned int aa:1;
    unsigned int tc:1;
    unsigned int rd:1;
    unsigned int ra:1;
    unsigned int rcode:4;
    unsigned short qdcount;
    unsigned short ancount;
    unsigned short nscount;
    unsigned short arcount;
};

typedef struct _AnonMap AnonMap;
struct _AnonMap {
    struct in_addr real;
    struct in_addr anon;
    AnonMap *next;
};

typedef int Filter_t(unsigned short,
	unsigned short,
	const char *,
	const struct in_addr,
	const struct in_addr);

typedef int (printer)(const char *, ...);

/*
 * flags/features for non-interactive mode
 */

#define T_MAX 65536
#ifndef T_A6
#define T_A6 38
#endif
#ifndef T_SRV
#define T_SRV 33
#endif
#define C_MAX 65536
#define OP_MAX 16

/*
 * Global variables
 */
static int interactive = 1;
static char *device = NULL;
static struct in_addr ignore_addr;
static pcap_t *pcap = NULL;
static char *bpf_program_str = "udp dst port 53 and udp[10:2] & 0x8000 = 0";
static WINDOW *w;
static unsigned short port53;
static void (*SubReport) (void) = NULL;
static int (*handle_datalink) (const u_char * pkt, int len) = NULL;
static int Quit = 0;
static char *progname = NULL;
static int anon_flag = 0;
static int sld_flag = 0;
static int nld_flag = 0;
static int promisc_flag = 1;
static AnonMap *Anons = NULL;

static int query_count_intvl = 0;
static int query_count_total = 0;
int qtype_counts[T_MAX];
static int opcode_counts[OP_MAX];
static int qclass_counts[C_MAX];
static AgentAddr *Sources = NULL;
static AgentAddr *Destinations = NULL;
static StringCounter *Tlds = NULL;
static StringCounter *Slds = NULL;
static StringCounter *Nlds = NULL;
static StringAddrCounter *SSC2 = NULL;
static StringAddrCounter *SSC3 = NULL;
#ifdef __OpenBSD__
static struct bpf_timeval last_ts;
#else
static struct timeval last_ts;
#endif

/* Prototypes */
static void SldBySource_report(void);
static void NldBySource_report(void);
static void Sources_report(void);
static void Destinatioreport(void);
static void Qtypes_report(void);
static void Opcodes_report(void);
static void Tld_report(void);
static void Sld_report(void);
static void Nld_report(void);
static void Help_report(void);
static void ResetCounters(void);

static Filter_t UnknownTldFilter;
static Filter_t AforAFilter;
static Filter_t RFC1918PtrFilter;
static Filter_t *Filter = NULL;

static printer *print_func = (printer *) printw;

static struct in_addr
AnonMap_lookup_or_add(AnonMap ** headP, struct in_addr real)
{
    AnonMap **T;
    for (T = headP; (*T); T = &(*T)->next)
	if ((*T)->real.s_addr == real.s_addr)
	    return (*T)->anon;
    (*T) = calloc(1, sizeof(**T));
    (*T)->real = real;
    (*T)->anon.s_addr = random();
    return (*T)->anon;
}

static char *
anon_inet_ntoa(struct in_addr a)
{
    if (anon_flag)
	a = AnonMap_lookup_or_add(&Anons, a);
    return inet_ntoa(a);
}

static AgentAddr *
AgentAddr_lookup_or_add(AgentAddr ** headP, struct in_addr a)
{
    AgentAddr **T;
    for (T = headP; (*T); T = &(*T)->next)
	if ((*T)->src.s_addr == a.s_addr)
	    return (*T);
    (*T) = calloc(1, sizeof(**T));
    (*T)->src = a;
    return (*T);
}

static StringCounter *
StringCounter_lookup_or_add(StringCounter ** headP, const char *s)
{
    StringCounter **T;
    for (T = headP; (*T); T = &(*T)->next)
	if (0 == strcmp((*T)->s, s))
	    return (*T);
    (*T) = calloc(1, sizeof(**T));
    (*T)->s = strdup(s);
    return (*T);
}

static StringAddrCounter *
StringAddrCounter_lookup_or_add(StringAddrCounter ** headP, struct in_addr a, const char *str)
{
    StringAddrCounter **T;
    for (T = headP; (*T); T = &(*T)->next)
	if (0 == strcmp((*T)->str, str))
	    if ((*T)->src.s_addr == a.s_addr)
		return (*T);
    (*T) = calloc(1, sizeof(**T));
    (*T)->str = strdup(str);
    (*T)->src = a;
    return (*T);
}

static int
foo_cmp(const void *A, const void *B)
{
    const foo *a = A;
    const foo *b = B;
    if (a->cnt < b->cnt)
	return 1;
    if (a->cnt > b->cnt)
	return -1;
    if (a->ptr < b->ptr)
	return 1;
    if (a->ptr > b->ptr)
	return -1;
    return 0;
}

static void
AgentAddr_sort(AgentAddr ** headP)
{
    foo *sortme;
    int n_agents = 0;
    int i;
    AgentAddr *a;
    for (a = *headP; a; a = a->next)
	n_agents++;
    sortme = calloc(n_agents, sizeof(foo));
    n_agents = 0;
    for (a = *headP; a; a = a->next) {
	sortme[n_agents].cnt = a->count;
	sortme[n_agents].ptr = a;
	n_agents++;
    }
    qsort(sortme, n_agents, sizeof(foo), foo_cmp);
    for (i = 0; i < n_agents; i++) {
	*headP = sortme[i].ptr;
	headP = &(*headP)->next;
    }
    free(sortme);
    *headP = NULL;
}

static void
StringCounter_sort(StringCounter ** headP)
{
    foo *sortme;
    int n_things = 0;
    int i;
    StringCounter *sc;
    for (sc = *headP; sc; sc = sc->next)
	n_things++;
    sortme = calloc(n_things, sizeof(foo));
    n_things = 0;
    for (sc = *headP; sc; sc = sc->next) {
	sortme[n_things].cnt = sc->count;
	sortme[n_things].ptr = sc;
	n_things++;
    }
    qsort(sortme, n_things, sizeof(foo), foo_cmp);
    for (i = 0; i < n_things; i++) {
	*headP = sortme[i].ptr;
	headP = &(*headP)->next;
    }
    free(sortme);
    *headP = NULL;
}

static void
StringAddrCounter_sort(StringAddrCounter ** headP)
{
    foo *sortme;
    int n_things = 0;
    int i;
    StringAddrCounter *ssc;
    for (ssc = *headP; ssc; ssc = ssc->next)
	n_things++;
    sortme = calloc(n_things, sizeof(foo));
    n_things = 0;
    for (ssc = *headP; ssc; ssc = ssc->next) {
	sortme[n_things].cnt = ssc->count;
	sortme[n_things].ptr = ssc;
	n_things++;
    }
    qsort(sortme, n_things, sizeof(foo), foo_cmp);
    for (i = 0; i < n_things; i++) {
	*headP = sortme[i].ptr;
	headP = &(*headP)->next;
    }
    free(sortme);
    *headP = NULL;
}

#define RFC1035_MAXLABELSZ 63
static int
rfc1035NameUnpack(const char *buf, size_t sz, off_t * off, char *name, size_t ns
)
{
    off_t no = 0;
    unsigned char c;
    size_t len;
    assert(ns > 0);
    do {
	if ((*off) >= sz)
	    break;
	c = *(buf + (*off));
	if (c > 191) {
	    /* blasted compression */
	    unsigned short s;
	    off_t ptr;
	    memcpy(&s, buf + (*off), sizeof(s));
	    s = ntohs(s);
	    (*off) += sizeof(s);
	    /* Sanity check */
	    if ((*off) >= sz)
		return 1;
	    ptr = s & 0x3FFF;
	    /* Make sure the pointer is inside this message */
	    if (ptr >= sz)
		return 2;
	    return rfc1035NameUnpack(buf, sz, &ptr, name + no, ns - no);
	} else if (c > RFC1035_MAXLABELSZ) {
	    /*
	     * "(The 10 and 01 combinations are reserved for future use.)"
	     */
	    break;
	    return 3;
	} else {
	    (*off)++;
	    len = (size_t) c;
	    if (len == 0)
		break;
	    if (len > (ns - 1))
		len = ns - 1;
	    if ((*off) + len > sz)	/* message is too short */
		return 4;
	    memcpy(name + no, buf + (*off), len);
	    (*off) += len;
	    no += len;
	    *(name + (no++)) = '.';
	}
    } while (c > 0);
    *(name + no - 1) = '\0';
    /* make sure we didn't allow someone to overflow the name buffer */
    assert(no <= ns);
    return 0;
}

static const char *
QnameToNld(const char *qname, int nld)
{
    const char *t = strrchr(qname, '.');
    int dotcount = 1;
    if (NULL == t)
	t = qname;
    if (0 == strcmp(t, ".arpa"))
	dotcount--;
    while (t > qname && dotcount < nld) {
	t--;
	if ('.' == *t)
	    dotcount++;
    }
    if (t > qname)
	t++;
    return t;
}

static int
handle_dns(const char *buf, int len, const struct in_addr sip, const struct in_addr dip)
{
    rfc1035_header qh;
    unsigned short us;
    char qname[MAX_QNAME_SZ];
    unsigned short qtype;
    unsigned short qclass;
    off_t offset;
    char *t;
    const char *s;
    int x;
    StringCounter *sc;
    StringAddrCounter *ssc;

    if (len < sizeof(qh))
	return 0;

    memcpy(&us, buf + 00, 2);
    qh.id = ntohs(us);

    memcpy(&us, buf + 2, 2);
    us = ntohs(us);
    qh.qr = (us >> 15) & 0x01;
    qh.opcode = (us >> 11) & 0x0F;
    qh.aa = (us >> 10) & 0x01;
    qh.tc = (us >> 9) & 0x01;
    qh.rd = (us >> 8) & 0x01;
    qh.ra = (us >> 7) & 0x01;
    qh.rcode = us & 0x0F;

    memcpy(&us, buf + 4, 2);
    qh.qdcount = ntohs(us);

    memcpy(&us, buf + 6, 2);
    qh.ancount = ntohs(us);

    memcpy(&us, buf + 8, 2);
    qh.nscount = ntohs(us);

    memcpy(&us, buf + 10, 2);
    qh.arcount = ntohs(us);

    offset = sizeof(qh);
    memset(qname, '\0', MAX_QNAME_SZ);
    x = rfc1035NameUnpack(buf, len, &offset, qname, MAX_QNAME_SZ);
    if (0 != x)
	return 0;
    if ('\0' == qname[0])
	strcpy(qname, ".");
    while ((t = strchr(qname, '\n')))
	*t = ' ';
    while ((t = strchr(qname, '\r')))
	*t = ' ';
    for (t = qname; *t; t++)
	*t = tolower(*t);

    memcpy(&us, buf + offset, 2);
    qtype = ntohs(us);
    memcpy(&us, buf + offset + 2, 2);
    qclass = ntohs(us);

    if (Filter && 0 == Filter(qtype, qclass, qname, sip, dip))
	return 0;

    /* gather stats */
    qtype_counts[qtype]++;
    qclass_counts[qclass]++;
    opcode_counts[qh.opcode]++;

    s = QnameToNld(qname, 1);
    sc = StringCounter_lookup_or_add(&Tlds, s);
    sc->count++;

    if (sld_flag) {
	s = QnameToNld(qname, 2);
	sc = StringCounter_lookup_or_add(&Slds, s);
	sc->count++;

	/* increment StringAddrCounter */
	ssc = StringAddrCounter_lookup_or_add(&SSC2, sip, s);
	ssc->count++;

    }
    if (nld_flag) {
	s = QnameToNld(qname, 3);
	sc = StringCounter_lookup_or_add(&Nlds, s);
	sc->count++;

	/* increment StringAddrCounter */
	ssc = StringAddrCounter_lookup_or_add(&SSC3, sip, s);
	ssc->count++;

    }
    return 1;
}

static int
handle_udp(const struct udphdr *udp, int len, struct in_addr sip, struct in_addr dip)
{
    char buf[PCAP_SNAPLEN];
    if (port53 != udp->uh_dport)
	return 0;
    memcpy(buf, udp + 1, len - sizeof(*udp));
    if (0 == handle_dns(buf, len - sizeof(*udp), sip, dip))
	return 0;
    return 1;
}

static int
handle_ip(const struct ip *ip, int len)
{
    char buf[PCAP_SNAPLEN];
    int offset = ip->ip_hl << 2;
    AgentAddr *clt;
    AgentAddr *srv;
    if (ignore_addr.s_addr)
	if (ip->ip_src.s_addr == ignore_addr.s_addr)
	    return 0;
    if (IPPROTO_UDP != ip->ip_p)
	return 0;
    memcpy(buf, (void *) ip + offset, len - offset);
    if (0 == handle_udp((struct udphdr *) buf, len - offset, ip->ip_src, ip->ip_dst))
	return 0;
    clt = AgentAddr_lookup_or_add(&Sources, ip->ip_src);
    clt->count++;
    srv = AgentAddr_lookup_or_add(&Destinations, ip->ip_dst);
    srv->count++;
    return 1;
}

#if USE_PPP
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

#endif

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
    if (ETHERTYPE_IP != etype)
	return 0;
    memcpy(buf, pkt, len);
    return handle_ip((struct ip *) buf, len);
}

/* public function */
void handle_pcap(u_char *udata, const struct pcap_pkthdr *hdr, const u_char *pkt)
{
    int status;

    if (hdr->caplen < ETHER_HDR_LEN)
	return;

    switch (pcap_datalink (pcap))
    {
	case DLT_EN10MB:
	    status = handle_ether (pkt, hdr->caplen);
	    break;
#if USE_PPP
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
	case DLT_NULL:
	    status = handle_null (pkt, hdr->caplen);
	    break;

	default:
	    fprintf (stderr, "unsupported data link type %d\n",
		    pcap_datalink(pcap));
	    status = 0;
	    break;
    } /* switch (pcap_datalink(pcap)) */

    if (0 == status)
	return;

    query_count_intvl++;
    query_count_total++;
    last_ts = hdr->ts;
}

static void
cron_pre(void)
{
    AgentAddr_sort(&Sources);
    AgentAddr_sort(&Destinations);
    StringCounter_sort(&Tlds);
    StringCounter_sort(&Slds);
    StringCounter_sort(&Nlds);
    StringAddrCounter_sort(&SSC2);
    StringAddrCounter_sort(&SSC3);
}

static void
cron_post(void)
{
    query_count_intvl = 0;
}

static void
keyboard(void)
{
    int ch;
    ch = getch() & 0xff;
    if (ch >= 'A' && ch <= 'Z')
	ch += 'a' - 'A';
    switch (ch) {
    case 's':
	SubReport = Sources_report;
	break;
    case 'd':
	SubReport = Destinatioreport;
	break;
    case '1':
	SubReport = Tld_report;
	break;
    case '2':
	SubReport = Sld_report;
	break;
    case '3':
	SubReport = Nld_report;
	break;
    case 'c':
    case '@':
	SubReport = SldBySource_report;
	break;
    case '#':
	SubReport = NldBySource_report;
	break;
    case 't':
	SubReport = Qtypes_report;
	break;
    case 'o':
	SubReport = Opcodes_report;
	break;
    case 030:
	Quit = 1;
	break;
    case 022:
	ResetCounters();
	break;
    case '?':
	SubReport = Help_report;
	break;
    default:
	break;
    }
}

static void
Help_report(void)
{
    print_func(" s - Sources list\n");
    print_func(" d - Destinations list\n");
    print_func(" t - Query types\n");
    print_func(" o - Opcodes\n");
    print_func(" 1 - TLD list\n");
    print_func(" 2 - SLD list\n");
    print_func(" 3 - 3LD list\n");
    print_func(" @ - SLD+Sources list\n");
    print_func(" # - 3LD+Sources list\n");
    print_func("^R - Reset counters\n");
    print_func("^X - Exit\n");
    print_func("\n");
    print_func("? - this\n");
}

static char *
qtype_str(int t)
{
    static char buf[30];
    switch (t) {
    case T_A:
	return "A?";
	break;
    case T_NS:
	return "NS?";
	break;
    case T_CNAME:
	return "CNAME?";
	break;
    case T_SOA:
	return "SOA?";
	break;
    case T_PTR:
	return "PTR?";
	break;
    case T_MX:
	return "MX?";
	break;
    case T_TXT:
	return "TXT?";
	break;
    case T_SIG:
	return "SIG?";
	break;
    case T_KEY:
	return "KEY?";
	break;
    case T_AAAA:
	return "AAAA?";
	break;
    case T_LOC:
	return "LOC?";
	break;
    case T_SRV:
	return "SRV?";
	break;
    case T_A6:
	return "A6?";
	break;
    case T_ANY:
	return "ANY?";
	break;
    default:
	snprintf(buf, 30, "#%d?", t);
	return buf;
    }
    /* NOTREACHED */
}

static char *
opcode_str(int o)
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
	snprintf(buf, 30, "Opcode%d", o);
	return buf;
    }
    /* NOTREACHED */
}

static int
get_nlines(void)
{
	if (interactive)
		return getmaxy(w) - 6;
	else
		return 50;
}

static void
StringCounter_report(StringCounter * list, char *what)
{
    StringCounter *sc;
    int nlines = get_nlines();
    print_func("%-30s %9s %6s\n", what, "count", "%");
    print_func("%-30s %9s %6s\n",
	"------------------------------", "---------", "------");
    for (sc = list; sc; sc = sc->next) {
	print_func("%-30.30s %9d %6.1f\n",
	    sc->s,
	    sc->count,
	    100.0 * sc->count / query_count_total);
	if (0 == --nlines)
	    break;
    }
}

static void
StringCounter_free(StringCounter ** headP)
{
    StringCounter *sc;
    void *next;
    for (sc = *headP; sc; sc = next) {
	next = sc->next;
	free(sc->s);
	free(sc);
    }
    *headP = NULL;
}

static void
StringAddrCounter_free(StringAddrCounter ** headP)
{
    StringAddrCounter *ssc;
    void *next;
    for (ssc = *headP; ssc; ssc = next) {
	next = ssc->next;
	free(ssc->str);
	free(ssc);
    }
    *headP = NULL;
}

static void
Tld_report(void)
{
    StringCounter_report(Tlds, "TLD");
}

static void
Sld_report(void)
{
    if (0 == sld_flag) {
	print_func("\tYou must start %s with the -s option\n", progname);
	print_func("\tto collect 2nd level domain stats.\n", progname);
    } else {
	StringCounter_report(Slds, "SLD");
    }
}

static void
Nld_report(void)
{
    if (0 == nld_flag) {
	print_func("\tYou must start %s with the -t option\n", progname);
	print_func("\tto collect 3nd level domain stats.\n", progname);
    } else {
	StringCounter_report(Nlds, "3LD");
    }
}

static void
Qtypes_report(void)
{
    int type;
    int nlines = get_nlines();
    print_func("%-10s %9s %6s\n", "Query Type", "count", "%");
    print_func("%-10s %9s %6s\n", "----------", "---------", "------");
    for (type = 0; type < T_MAX; type++) {
	if (0 == qtype_counts[type])
	    continue;
	print_func("%-10s %9d %6.1f\n",
	    qtype_str(type),
	    qtype_counts[type],
	    100.0 * qtype_counts[type] / query_count_total);
	if (0 == --nlines)
	    break;
    }
}

static void
Opcodes_report(void)
{
    int op;
    int nlines = get_nlines();
    print_func("%-10s %9s %6s\n", "Opcode    ", "count", "%");
    print_func("%-10s %9s %6s\n", "----------", "---------", "------");
    for (op = 0; op < OP_MAX; op++) {
	if (0 == opcode_counts[op])
	    continue;
	print_func("%-10s %9d %6.1f\n",
	    opcode_str(op),
	    opcode_counts[op],
	    100.0 * opcode_counts[op] / query_count_total);
	if (0 == --nlines)
	    break;
    }
}

static void
AgentAddr_report(AgentAddr * list, const char *what)
{
    AgentAddr *agent;
    int nlines = get_nlines();
    print_func("%-16s %9s %6s\n", what, "count", "%");
    print_func("%-16s %9s %6s\n", "----------------", "---------", "------");
    for (agent = list; agent; agent = agent->next) {
	print_func("%-16s %9d %6.1f\n",
	    anon_inet_ntoa(agent->src),
	    agent->count,
	    100.0 * agent->count / query_count_total);
	if (0 == --nlines)
	    break;
    }
}

static void
Combo_report(StringAddrCounter * list, char *what1, char *what2)
{
    StringAddrCounter *ssc;
    int nlines = get_nlines();
    print_func("%-16s %-32s %9s %6s\n", what1, what2, "count", "%");
    print_func("%-16s %-32s %9s %6s\n",
	"----------------", "--------------------", "---------", "------");
    for (ssc = list; ssc; ssc = ssc->next) {
	print_func("%-16s %-32s %9d %6.1f\n",
	    anon_inet_ntoa(ssc->src),
	    ssc->str,
	    ssc->count,
	    100.0 * ssc->count / query_count_total);
	if (0 == --nlines)
	    break;
    }
}

static void
SldBySource_report(void)
{
    if (0 == sld_flag) {
	print_func("\tYou must start %s with the -s option\n", progname);
	print_func("\tto collect 2nd level domain stats.\n", progname);
    } else {
	Combo_report(SSC2, "Source", "SLD");
    }
}

static void
NldBySource_report(void)
{
    if (0 == nld_flag) {
	print_func("\tYou must start %s with the -t option\n", progname);
	print_func("\tto collect 3nd level domain stats.\n", progname);
    } else {
	Combo_report(SSC3, "Source", "3LD");
    }
}


static void
AgentAddr_free(AgentAddr ** headP)
{
    AgentAddr *aa;
    void *next;
    for (aa = *headP; aa; aa = next) {
	next = aa->next;
	free(aa);
    }
    *headP = NULL;
}

static void
Sources_report(void)
{
    AgentAddr_report(Sources, "Sources");
}

static void
Destinatioreport(void)
{
    AgentAddr_report(Destinations, "Destinations");
}

static void
report(void)
{
    move(0, 0);
    print_func("%d new queries, %d total queries",
	query_count_intvl, query_count_total);
    clrtoeol();
    if (last_ts.tv_sec) {
	time_t t = (time_t) last_ts.tv_sec;
	move(0, 50);
	print_func("%s", ctime(&t));
    }
    move(2, 0);
    clrtobot();
    if (SubReport)
	SubReport();
    refresh();
}

/*
 * === BEGIN FILTERS ==========================================================
 */

#include "known_tlds.h"

static int
UnknownTldFilter(unsigned short qt, unsigned short qc, const char *qn, const struct in_addr sip, const struct in_addr dip)
{
    const char *tld = QnameToNld(qn, 1);
    unsigned int i;
    if (NULL == tld)
	return 1;		/* tld is unknown */
    for (i = 0; KnownTLDS[i]; i++)
	if (0 == strcmp(KnownTLDS[i], tld))
	    return 0;		/* tld is known */
    return 1;			/* tld is unknown */
}

static int
AforAFilter(unsigned short qt, unsigned short qc, const char *qn, const struct in_addr sip, const struct in_addr dip)
{
    struct in_addr a;
    if (qt != T_A)
	return 0;  
    return inet_aton(qn, &a);
}

static int
RFC1918PtrFilter(unsigned short qt, unsigned short qc, const char *qn, const struct in_addr sip, const struct in_addr dip)
{
    char *t;
    char q[128];   
    unsigned int i = 0;
    if (qt != T_PTR)
	return 0;  
    strncpy(q, qn, sizeof(q)-1);
    q[sizeof(q)-1] = '\0';
    t = strstr(q, ".in-addr.arpa");
    if (NULL == t)
	return 0;
    *t = '\0';
    for (t = strtok(q, "."); t; t = strtok(NULL, ".")) {
	i >>= 8;
	i |= ((atoi(t) & 0xff) << 24);
    }
    if ((i & 0xff000000) == 0x0a000000)
	return 1;
    if ((i & 0xfff00000) == 0xac100000)
	return 1;
    if ((i & 0xffff0000) == 0xc0a80000)
	return 1;
    return 0;
}

static void
set_filter(const char *fn)
{
	if (0 == strcmp(fn, "unknown-tlds"))
		Filter = UnknownTldFilter;
	else if (0 == strcmp(fn, "A-for-A"))
		Filter = AforAFilter;
	else if (0 == strcmp(fn, "rfc1918-ptr"))
		Filter = RFC1918PtrFilter;
	else
		Filter = NULL;
}

/*
 * === END FILTERS ==========================================================
 */

static void
init_curses(void)
{
    w = initscr();
    cbreak();
    noecho();
    nodelay(w, 1);
}

static void
ResetCounters(void)
{
    query_count_intvl = 0;
    query_count_total = 0;
    memset(qtype_counts, '\0', sizeof(qtype_counts));
    memset(qclass_counts, '\0', sizeof(qclass_counts));
    memset(opcode_counts, '\0', sizeof(opcode_counts));
    AgentAddr_free(&Sources);
    AgentAddr_free(&Destinations);
    StringCounter_free(&Tlds);
    StringCounter_free(&Slds);
    StringCounter_free(&Nlds);
    StringAddrCounter_free(&SSC2);
    StringAddrCounter_free(&SSC3);
    memset(&last_ts, '\0', sizeof(last_ts));
}

static void
usage(void)
{
    fprintf(stderr, "usage: %s [opts] netdevice|savefile\n",
	progname);
    fprintf(stderr, "\t-a\tAnonymize IP Addrs\n");
    fprintf(stderr, "\t-b expr\tBPF program code\n");
    fprintf(stderr, "\t-i addr\tIgnore this source IP address\n");
    fprintf(stderr, "\t-p\tDon't put interface in promiscuous mode\n");
    fprintf(stderr, "\t-s\tEnable 2nd level domain stats collection\n");
    fprintf(stderr, "\t-t\tEnable 3nd level domain stats collection\n");
    fprintf(stderr, "\t-f\tfilter-name\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Available filters:\n");
    fprintf(stderr, "\tunknown-tlds\n");
    fprintf(stderr, "\tA-for-A\n");
    fprintf(stderr, "\trfc1918-ptr\n");
    exit(1);
}

static int
pcap_select(pcap_t * p, int sec, int usec)
{
    fd_set R;
    struct timeval to;
    FD_ZERO(&R);
    FD_SET(pcap_fileno(p), &R);
    to.tv_sec = sec;
    to.tv_usec = usec;
    return select(pcap_fileno(p) + 1, &R, NULL, NULL, &to);
}

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
	pcap = pcap_open_offline(device, errbuf);
    } else {
	pcap = pcap_open_live(device, PCAP_SNAPLEN, promisc_flag, 1000, errbuf);
    }
    if (NULL == pcap) {
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
    x = pcap_compile(pcap, &fp, bpf_program_str, 1, 0);
    if (x < 0) {
	fprintf(stderr, "pcap_compile failed\n");
	exit(1);
    }
    x = pcap_setfilter(pcap, &fp);
    if (x < 0) {
	fprintf(stderr, "pcap_setfilter failed\n");
	exit(1);
    }

    /*
     * non-blocking call added for Mac OS X bugfix.  Sent by Max Horn.
     * ref http://www.tcpdump.org/lists/workers/2002/09/msg00033.html
     */
    x = pcap_setnonblock(pcap, 1, errbuf);
    if (x < 0) {
	fprintf(stderr, "pcap_setnonblock failed: %s\n", errbuf);
	exit(1);
    }

    switch (pcap_datalink(pcap)) {
    case DLT_EN10MB:
	handle_datalink = handle_ether;
	break;
#if USE_PPP
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
	    pcap_datalink(pcap));
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
		    pcap_select(pcap, 1, 0);
		x = pcap_dispatch(pcap, 50, handle_pcap, NULL);
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
	while (pcap_dispatch(pcap, 50, handle_pcap, NULL))
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

    pcap_close(pcap);
    return 0;
} /* static int main(int argc, char *argv[]) */
#endif
