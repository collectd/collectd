/**
 * collectd - src/tcpconns.c
 * Copyright (C) 2007  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

/**
 * Code within `__OpenBSD__' blocks is provided under the following license:
 *
 * $collectd: parts of tcpconns.c, 2008/08/08 03:48:30 Michael Stapelberg $
 * $OpenBSD: inet.c,v 1.100 2007/06/19 05:28:30 ray Exp $
 * $NetBSD: inet.c,v 1.14 1995/10/03 21:42:37 thorpej Exp $
 *
 * Copyright (c) 1983, 1988, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if !KERNEL_LINUX && !HAVE_SYSCTLBYNAME && !__OpenBSD__
# error "No applicable input method."
#endif

#if KERNEL_LINUX
/* #endif KERNEL_LINUX */

#elif HAVE_SYSCTLBYNAME
# include <sys/socketvar.h>
# include <sys/sysctl.h>

/* Some includes needed for compiling on FreeBSD */
#include <sys/time.h>
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#if HAVE_NET_IF_H
# include <net/if.h>
#endif

# include <net/route.h>
# include <netinet/in.h>
# include <netinet/in_systm.h>
# include <netinet/ip.h>
# include <netinet/ip6.h>
# include <netinet/in_pcb.h>
# include <netinet/ip_var.h>
# include <netinet/tcp.h>
# include <netinet/tcpip.h>
# include <netinet/tcp_seq.h>
# include <netinet/tcp_var.h>
/* #endif HAVE_SYSCTLBYNAME */

#elif __OpenBSD__
# include <sys/queue.h>
# include <sys/socket.h>
# include <net/route.h>
# include <netinet/in.h>
# include <netinet/in_systm.h>
# include <netinet/ip.h>
# include <netinet/in_pcb.h>
# include <netinet/tcp.h>
# include <netinet/tcp_timer.h>
# include <netinet/tcp_var.h>
# include <netdb.h>
# include <arpa/inet.h>
# include <nlist.h>
# include <kvm.h>
#endif /* __OpenBSD__ */

#if KERNEL_LINUX
static const char *tcp_state[] =
{
  "", /* 0 */
  "ESTABLISHED",
  "SYN_SENT",
  "SYN_RECV",
  "FIN_WAIT1",
  "FIN_WAIT2",
  "TIME_WAIT",
  "CLOSED",
  "CLOSE_WAIT",
  "LAST_ACK",
  "LISTEN", /* 10 */
  "CLOSING"
};

# define TCP_STATE_LISTEN 10
# define TCP_STATE_MIN 1
# define TCP_STATE_MAX 11
/* #endif KERNEL_LINUX */

#elif HAVE_SYSCTLBYNAME
static const char *tcp_state[] =
{
  "CLOSED",
  "LISTEN",
  "SYN_SENT",
  "SYN_RECV",
  "ESTABLISHED",
  "CLOSE_WAIT",
  "FIN_WAIT1",
  "CLOSING",
  "LAST_ACK",
  "FIN_WAIT2",
  "TIME_WAIT"
};

# define TCP_STATE_LISTEN 1
# define TCP_STATE_MIN 0
# define TCP_STATE_MAX 10
/* #endif HAVE_SYSCTLBYNAME */

#elif __OpenBSD__
static const char *tcp_state[] =
{
  "CLOSED",
  "LISTEN",
  "SYN_SENT",
  "SYN_RCVD",
  "ESTABLISHED",
  "CLOSE_WAIT",
  "FIN_WAIT_1",
  "CLOSING",
  "LAST_ACK",
  "FIN_WAIT_2",
  "TIME_WAIT"
};

static kvm_t *kvmd;

static struct nlist nl[] = {
#define N_MBSTAT        0
        { "_mbstat" },
#define N_IPSTAT        1
        { "_ipstat" },
#define N_TCBTABLE      2
        { "_tcbtable" },
#define N_TCPSTAT       3
        { "_tcpstat" },
#define N_UDBTABLE      4
        { "_udbtable" },
#define N_UDPSTAT       5
        { "_udpstat" },
#define N_IFNET         6
        { "_ifnet" },
#define N_ICMPSTAT      7
        { "_icmpstat" },
#define N_RTSTAT        8
        { "_rtstat" },
#define N_UNIXSW        9
        { "_unixsw" },
#define N_RTREE         10
        { "_rt_tables"},
#define N_FILE          11
        { "_file" },
#define N_IGMPSTAT      12
        { "_igmpstat" },
#define N_MRTPROTO      13
        { "_ip_mrtproto" },
#define N_MRTSTAT       14
        { "_mrtstat" },
#define N_MFCHASHTBL    15
        { "_mfchashtbl" },
#define N_MFCHASH       16
        { "_mfchash" },
        { "_viftable" },
#define N_AHSTAT        18
        { "_ahstat"},
#define N_ESPSTAT       19
        { "_espstat"},
#define N_IP4STAT       20
        { "_ipipstat"},
#define N_DDPSTAT       21
        { "_ddpstat"},
#define N_DDPCB         22
        { "_ddpcb"},
#define N_ETHERIPSTAT   23
        { "_etheripstat"},
#define N_IP6STAT       24
        { "_ip6stat" },
#define N_ICMP6STAT     25
        { "_icmp6stat" },
#define N_PIM6STAT      26
        { "_pim6stat" },
#define N_MRT6PROTO     27
        { "_ip6_mrtproto" },
#define N_MRT6STAT      28
        { "_mrt6stat" },
#define N_MF6CTABLE     29
        { "_mf6ctable" },
#define N_MIF6TABLE     30
        { "_mif6table" },
#define N_MBPOOL        31
        { "_mbpool" },
#define N_MCLPOOL       32
        { "_mclpool" },
#define N_IPCOMPSTAT    33
        { "_ipcompstat" },
#define N_RIP6STAT      34
        { "_rip6stat" },
#define N_CARPSTAT      35
        { "_carpstats" },
#define N_RAWIPTABLE    36
        { "_rawcbtable" },
#define N_RAWIP6TABLE   37
        { "_rawin6pcbtable" },
#define N_PFSYNCSTAT    38
        { "_pfsyncstats" },
#define N_PIMSTAT       39
        { "_pimstat" },
#define N_AF2RTAFIDX    40
        { "_af2rtafidx" },
#define N_RTBLIDMAX     41
        { "_rtbl_id_max" },
#define N_RTMASK        42
        { "_mask_rnhead" },

        { "" }
};

# define TCP_STATE_LISTEN 1
# define TCP_STATE_MIN 1
# define TCP_STATE_MAX 10
#endif /* __OpenBSD__ */

#define PORT_COLLECT_LOCAL  0x01
#define PORT_COLLECT_REMOTE 0x02
#define PORT_IS_LISTENING   0x04

typedef struct port_entry_s
{
  uint16_t port;
  uint16_t flags;
  uint32_t count_local[TCP_STATE_MAX + 1];
  uint32_t count_remote[TCP_STATE_MAX + 1];
  struct port_entry_s *next;
} port_entry_t;

static const char *config_keys[] =
{
  "ListeningPorts",
  "LocalPort",
  "RemotePort"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int port_collect_listening = 0;
static port_entry_t *port_list_head = NULL;

static void conn_submit_port_entry (port_entry_t *pe)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;
  int i;

  vl.values = values;
  vl.values_len = 1;
  vl.time = time (NULL);
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "tcpconns", sizeof (vl.plugin));
  sstrncpy (vl.type, "tcp_connections", sizeof (vl.type));

  if (((port_collect_listening != 0) && (pe->flags & PORT_IS_LISTENING))
      || (pe->flags & PORT_COLLECT_LOCAL))
  {
    ssnprintf (vl.plugin_instance, sizeof (vl.plugin_instance),
	"%hu-local", pe->port);

    for (i = 1; i <= TCP_STATE_MAX; i++)
    {
      vl.values[0].gauge = pe->count_local[i];

      sstrncpy (vl.type_instance, tcp_state[i], sizeof (vl.type_instance));

      plugin_dispatch_values (&vl);
    }
  }

  if (pe->flags & PORT_COLLECT_REMOTE)
  {
    ssnprintf (vl.plugin_instance, sizeof (vl.plugin_instance),
	"%hu-remote", pe->port);

    for (i = 1; i <= TCP_STATE_MAX; i++)
    {
      vl.values[0].gauge = pe->count_remote[i];

      sstrncpy (vl.type_instance, tcp_state[i], sizeof (vl.type_instance));

      plugin_dispatch_values (&vl);
    }
  }
} /* void conn_submit */

static void conn_submit_all (void)
{
  port_entry_t *pe;

  for (pe = port_list_head; pe != NULL; pe = pe->next)
    conn_submit_port_entry (pe);
} /* void conn_submit_all */

static port_entry_t *conn_get_port_entry (uint16_t port, int create)
{
  port_entry_t *ret;

  ret = port_list_head;
  while (ret != NULL)
  {
    if (ret->port == port)
      break;
    ret = ret->next;
  }

  if ((ret == NULL) && (create != 0))
  {
    ret = (port_entry_t *) malloc (sizeof (port_entry_t));
    if (ret == NULL)
      return (NULL);
    memset (ret, '\0', sizeof (port_entry_t));

    ret->port = port;
    ret->next = port_list_head;
    port_list_head = ret;
  }

  return (ret);
} /* port_entry_t *conn_get_port_entry */

/* Removes ports that were added automatically due to the `ListeningPorts'
 * setting but which are no longer listening. */
static void conn_reset_port_entry (void)
{
  port_entry_t *prev = NULL;
  port_entry_t *pe = port_list_head;

  while (pe != NULL)
  {
    /* If this entry was created while reading the files (ant not when handling
     * the configuration) remove it now. */
    if ((pe->flags & (PORT_COLLECT_LOCAL
	    | PORT_COLLECT_REMOTE
	    | PORT_IS_LISTENING)) == 0)
    {
      port_entry_t *next = pe->next;

      DEBUG ("tcpconns plugin: Removing temporary entry "
	  "for listening port %hu", pe->port);

      if (prev == NULL)
	port_list_head = next;
      else
	prev->next = next;

      sfree (pe);
      pe = next;

      continue;
    }

    memset (pe->count_local, '\0', sizeof (pe->count_local));
    memset (pe->count_remote, '\0', sizeof (pe->count_remote));
    pe->flags &= ~PORT_IS_LISTENING;

    pe = pe->next;
  }
} /* void conn_reset_port_entry */

static int conn_handle_ports (uint16_t port_local, uint16_t port_remote, uint8_t state)
{
  port_entry_t *pe = NULL;

  if ((state > TCP_STATE_MAX)
#if TCP_STATE_MIN > 0
      || (state < TCP_STATE_MIN)
#endif
     )
  {
    NOTICE ("tcpconns plugin: Ignoring connection with unknown state 0x%02x.",
	state);
    return (-1);
  }

  /* Listening sockets */
  if ((state == TCP_STATE_LISTEN) && (port_collect_listening != 0))
  {
    pe = conn_get_port_entry (port_local, 1 /* create */);
    if (pe != NULL)
      pe->flags |= PORT_IS_LISTENING;
  }

  DEBUG ("tcpconns plugin: Connection %hu <-> %hu (%s)",
      port_local, port_remote, tcp_state[state]);

  pe = conn_get_port_entry (port_local, 0 /* no create */);
  if (pe != NULL)
    pe->count_local[state]++;

  pe = conn_get_port_entry (port_remote, 0 /* no create */);
  if (pe != NULL)
    pe->count_remote[state]++;

  return (0);
} /* int conn_handle_ports */

#if KERNEL_LINUX
static int conn_handle_line (char *buffer)
{
  char *fields[32];
  int   fields_len;

  char *endptr;

  char *port_local_str;
  char *port_remote_str;
  uint16_t port_local;
  uint16_t port_remote;

  uint8_t state;

  int buffer_len = strlen (buffer);

  while ((buffer_len > 0) && (buffer[buffer_len - 1] < 32))
    buffer[--buffer_len] = '\0';
  if (buffer_len <= 0)
    return (-1);

  fields_len = strsplit (buffer, fields, STATIC_ARRAY_SIZE (fields));
  if (fields_len < 12)
  {
    DEBUG ("tcpconns plugin: Got %i fields, expected at least 12.", fields_len);
    return (-1);
  }

  port_local_str  = strchr (fields[1], ':');
  port_remote_str = strchr (fields[2], ':');

  if ((port_local_str == NULL) || (port_remote_str == NULL))
    return (-1);
  port_local_str++;
  port_remote_str++;
  if ((*port_local_str == '\0') || (*port_remote_str == '\0'))
    return (-1);

  endptr = NULL;
  port_local = (uint16_t) strtol (port_local_str, &endptr, 16);
  if ((endptr == NULL) || (*endptr != '\0'))
    return (-1);

  endptr = NULL;
  port_remote = (uint16_t) strtol (port_remote_str, &endptr, 16);
  if ((endptr == NULL) || (*endptr != '\0'))
    return (-1);

  endptr = NULL;
  state = (uint8_t) strtol (fields[3], &endptr, 16);
  if ((endptr == NULL) || (*endptr != '\0'))
    return (-1);

  return (conn_handle_ports (port_local, port_remote, state));
} /* int conn_handle_line */

static int conn_read_file (const char *file)
{
  FILE *fh;
  char buffer[1024];

  fh = fopen (file, "r");
  if (fh == NULL)
    return (-1);

  while (fgets (buffer, sizeof (buffer), fh) != NULL)
  {
    conn_handle_line (buffer);
  } /* while (fgets) */

  fclose (fh);

  return (0);
} /* int conn_read_file */
/* #endif KERNEL_LINUX */

#elif HAVE_SYSCTLBYNAME
#endif /* HAVE_SYSCTLBYNAME */

static int conn_config (const char *key, const char *value)
{
  if (strcasecmp (key, "ListeningPorts") == 0)
  {
    if ((strcasecmp (value, "Yes") == 0)
	|| (strcasecmp (value, "True") == 0)
	|| (strcasecmp (value, "On") == 0))
      port_collect_listening = 1;
    else
      port_collect_listening = 0;
  }
  else if ((strcasecmp (key, "LocalPort") == 0)
      || (strcasecmp (key, "RemotePort") == 0))
  {
      port_entry_t *pe;
      int port = atoi (value);

      if ((port < 1) || (port > 65535))
      {
	ERROR ("tcpconns plugin: Invalid port: %i", port);
	return (1);
      }

      pe = conn_get_port_entry ((uint16_t) port, 1 /* create */);
      if (pe == NULL)
      {
	ERROR ("tcpconns plugin: conn_get_port_entry failed.");
	return (1);
      }

      if (strcasecmp (key, "LocalPort") == 0)
	pe->flags |= PORT_COLLECT_LOCAL;
      else
	pe->flags |= PORT_COLLECT_REMOTE;
  }
  else
  {
    return (-1);
  }

  return (0);
} /* int conn_config */

#if KERNEL_LINUX
static int conn_init (void)
{
  if (port_list_head == NULL)
    port_collect_listening = 1;

  return (0);
} /* int conn_init */

static int conn_read (void)
{
  int errors_num = 0;

  conn_reset_port_entry ();

  if (conn_read_file ("/proc/net/tcp") != 0)
    errors_num++;
  if (conn_read_file ("/proc/net/tcp6") != 0)
    errors_num++;

  if (errors_num < 2)
  {
    conn_submit_all ();
  }
  else
  {
    ERROR ("tcpconns plugin: Neither /proc/net/tcp nor /proc/net/tcp6 "
	"coult be read.");
    return (-1);
  }

  return (0);
} /* int conn_read */
/* #endif KERNEL_LINUX */

#elif HAVE_SYSCTLBYNAME
static int conn_read (void)
{
  int status;
  char *buffer;
  size_t buffer_len;;

  struct xinpgen *in_orig;
  struct xinpgen *in_ptr;

  conn_reset_port_entry ();

  buffer_len = 0;
  status = sysctlbyname ("net.inet.tcp.pcblist", NULL, &buffer_len, 0, 0);
  if (status < 0)
  {
    ERROR ("tcpconns plugin: sysctlbyname failed.");
    return (-1);
  }

  buffer = (char *) malloc (buffer_len);
  if (buffer == NULL)
  {
    ERROR ("tcpconns plugin: malloc failed.");
    return (-1);
  }

  status = sysctlbyname ("net.inet.tcp.pcblist", buffer, &buffer_len, 0, 0);
  if (status < 0)
  {
    ERROR ("tcpconns plugin: sysctlbyname failed.");
    sfree (buffer);
    return (-1);
  }

  if (buffer_len <= sizeof (struct xinpgen))
  {
    ERROR ("tcpconns plugin: (buffer_len <= sizeof (struct xinpgen))");
    sfree (buffer);
    return (-1);
  }

  in_orig = (struct xinpgen *) buffer;
  for (in_ptr = (struct xinpgen *) (((char *) in_orig) + in_orig->xig_len);
      in_ptr->xig_len > sizeof (struct xinpgen);
      in_ptr = (struct xinpgen *) (((char *) in_ptr) + in_ptr->xig_len))
  {
    struct tcpcb *tp = &((struct xtcpcb *) in_ptr)->xt_tp;
    struct inpcb *inp = &((struct xtcpcb *) in_ptr)->xt_inp;
    struct xsocket *so = &((struct xtcpcb *) in_ptr)->xt_socket;

    /* Ignore non-TCP sockets */
    if (so->xso_protocol != IPPROTO_TCP)
      continue;

    /* Ignore PCBs which were freed during copyout. */
    if (inp->inp_gencnt > in_orig->xig_gen)
      continue;

    if (((inp->inp_vflag & INP_IPV4) == 0)
	&& ((inp->inp_vflag & INP_IPV6) == 0))
      continue;

    conn_handle_ports (inp->inp_lport, inp->inp_fport, tp->t_state);
  } /* for (in_ptr) */

  in_orig = NULL;
  in_ptr = NULL;
  sfree (buffer);

  conn_submit_all ();

  return (0);
} /* int conn_read */
/* #endif HAVE_SYSCTLBYNAME */

#elif __OpenBSD__
static int kread(u_long addr, void *buf, int size)
{
  if (kvm_read(kvmd, addr, buf, size) != size)
  {
    ERROR ("tcpconns plugin: %s\n", kvm_geterr(kvmd));
    return (-1);
  }
  return (0);
}

static int conn_init (void)
{
  char buf[_POSIX2_LINE_MAX];
  if ((kvmd = kvm_openfiles(NULL, NULL, NULL, O_RDONLY, buf)) == NULL)
  {
    ERROR("tcpconns plugin: %s", buf);
    return (-1);
  }
  if (kvm_nlist(kvmd, nl) < 0 || nl[0].n_type == 0)
  {
    ERROR("tcpconns plugin: No namelist.");
    return (-1);
  }
  return (0);
}

static int conn_read (void)
{
  u_long off = nl[2].n_value;
  struct inpcbtable table;
  struct inpcb *head, *next, *prev;
  struct inpcb inpcb;
  struct tcpcb tcpcb;

  conn_reset_port_entry ();

  kread(off, &table, sizeof(table));
  prev = head = (struct inpcb *)&CIRCLEQ_FIRST(&((struct inpcbtable *)off)->inpt_queue);
  next = CIRCLEQ_FIRST(&table.inpt_queue);

  while (next != head) {
    kread((u_long)next, &inpcb, sizeof(inpcb));
    prev = next;
    next = CIRCLEQ_NEXT(&inpcb, inp_queue);
    if (inet_lnaof(inpcb.inp_laddr) == INADDR_ANY)
      continue;
    kread((u_long)inpcb.inp_ppcb, &tcpcb, sizeof(tcpcb));
    conn_handle_ports (ntohs(inpcb.inp_lport), ntohs(inpcb.inp_fport), tcpcb.t_state);
  }

  conn_submit_all ();

  return (0);
}
#endif /* __OpenBSD__ */

void module_register (void)
{
	plugin_register_config ("tcpconns", conn_config,
			config_keys, config_keys_num);
#if KERNEL_LINUX
	plugin_register_init ("tcpconns", conn_init);
#elif HAVE_SYSCTLBYNAME
	/* no initialization */
#elif __OpenBSD__
	plugin_register_init ("tcpconns", conn_init);
#endif
	plugin_register_read ("tcpconns", conn_read);
} /* void module_register */

/*
 * vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
 */
