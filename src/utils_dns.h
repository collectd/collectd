#ifndef COLLECTD_UTILS_DNS_H
#define COLLECTD_UTILS_DNS_H 1
/*
 * collectd - src/utils_dns.h
 * Copyright (C) 2006  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 */

#include "config.h"

#include <arpa/nameser.h>
#include <stdint.h>

#if HAVE_PCAP_H
# include <pcap.h>
#endif

#define DNS_MSG_HDR_SZ 12

#define T_MAX 65536
#define OP_MAX 16
#define C_MAX 65536
#define MAX_QNAME_SZ 512

struct rfc1035_header_s {
    uint16_t id;
    unsigned int qr:1;
    unsigned int opcode:4;
    unsigned int aa:1;
    unsigned int tc:1;
    unsigned int rd:1;
    unsigned int ra:1;
    unsigned int z:1;
    unsigned int ad:1;
    unsigned int cd:1;
    unsigned int rcode:4;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
    uint16_t qtype;
    uint16_t qclass;
    char     qname[MAX_QNAME_SZ];
    uint16_t length;
};
typedef struct rfc1035_header_s rfc1035_header_t;

extern int qtype_counts[T_MAX];
extern int opcode_counts[OP_MAX];
extern int qclass_counts[C_MAX];

#if HAVE_PCAP_H
void dnstop_set_pcap_obj (pcap_t *po);
#endif
void dnstop_set_callback (void (*cb) (const rfc1035_header_t *));

void ignore_list_add_name (const char *name);
#if HAVE_PCAP_H
void handle_pcap (u_char * udata, const struct pcap_pkthdr *hdr, const u_char * pkt);
#endif

const char *qtype_str(int t);
const char *opcode_str(int o);
const char *rcode_str (int r);

#endif /* !COLLECTD_UTILS_DNS_H */
