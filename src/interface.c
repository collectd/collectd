/**
 * collectd - src/interface.c
 * Copyright (C) 2005-2010  Florian octo Forster
 * Copyright (C) 2009       Manuel Sanmartin
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
 *   Sune Marcher <sm at flork.dk>
 *   Manuel Sanmartin
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

/* One cannot include both. This sucks. */
#if HAVE_LINUX_IF_H
#include <linux/if.h>
#elif HAVE_NET_IF_H
#include <net/if.h>
#endif

#if HAVE_LINUX_NETDEVICE_H
#include <linux/netdevice.h>
#endif
#if HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif

#if HAVE_STATGRAB_H
#include <statgrab.h>
#endif

#if HAVE_PERFSTAT
#include <libperfstat.h>
#include <sys/protosw.h>
#endif

/*
 * Various people have reported problems with `getifaddrs' and varying versions
 * of `glibc'. That's why it's disabled by default. Since more statistics are
 * available this way one may enable it using the `--enable-getifaddrs' option
 * of the configure script. -octo
 */
#if KERNEL_LINUX
#if !COLLECT_GETIFADDRS
#undef HAVE_GETIFADDRS
#endif /* !COLLECT_GETIFADDRS */
#endif /* KERNEL_LINUX */

#if HAVE_PERFSTAT
static perfstat_netinterface_t *ifstat;
static int nif;
static int pnif;
#endif /* HAVE_PERFSTAT */

#if !HAVE_GETIFADDRS && !KERNEL_LINUX && !HAVE_LIBKSTAT &&                     \
    !HAVE_LIBSTATGRAB && !HAVE_PERFSTAT
#error "No applicable input method."
#endif

/*
 * (Module-)Global variables
 */
static const char *config_keys[] = {
    "Interface", "IgnoreSelected", "ReportInactive",
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *ignorelist = NULL;

static _Bool report_inactive = 1;

#ifdef HAVE_LIBKSTAT
#define MAX_NUMIF 256
extern kstat_ctl_t *kc;
static kstat_t *ksp[MAX_NUMIF];
static int numif = 0;
static _Bool unique_name = 0;
#endif /* HAVE_LIBKSTAT */

static int interface_config(const char *key, const char *value) {
  if (ignorelist == NULL)
    ignorelist = ignorelist_create(/* invert = */ 1);

  if (strcasecmp(key, "Interface") == 0) {
    ignorelist_add(ignorelist, value);
  } else if (strcasecmp(key, "IgnoreSelected") == 0) {
    int invert = 1;
    if (IS_TRUE(value))
      invert = 0;
    ignorelist_set_invert(ignorelist, invert);
  } else if (strcasecmp(key, "ReportInactive") == 0)
    report_inactive = IS_TRUE(value);
  else if (strcasecmp(key, "UniqueName") == 0) {
#ifdef HAVE_LIBKSTAT
    if (IS_TRUE(value))
      unique_name = 1;
#else
    WARNING("interface plugin: the \"UniqueName\" option is only valid on "
            "Solaris.");
#endif /* HAVE_LIBKSTAT */
  } else {
    return -1;
  }

  return 0;
}

#if HAVE_LIBKSTAT
static int interface_init(void) {
  kstat_t *ksp_chain;

  numif = 0;

  if (kc == NULL)
    return -1;

  for (numif = 0, ksp_chain = kc->kc_chain;
       (numif < MAX_NUMIF) && (ksp_chain != NULL);
       ksp_chain = ksp_chain->ks_next) {
    if (strncmp(ksp_chain->ks_class, "net", 3))
      continue;
    if (ksp_chain->ks_type != KSTAT_TYPE_NAMED)
      continue;
    if (kstat_read(kc, ksp_chain, NULL) == -1)
      continue;
    if (get_kstat_value(ksp_chain, "obytes") == -1LL)
      continue;
    ksp[numif++] = ksp_chain;
  }

  return 0;
} /* int interface_init */
#endif /* HAVE_LIBKSTAT */

static void if_submit(const char *dev, const char *type, derive_t rx,
                      derive_t tx) {
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[] = {
      {.derive = rx}, {.derive = tx},
  };

  if (ignorelist_match(ignorelist, dev) != 0)
    return;

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);
  sstrncpy(vl.plugin, "interface", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, dev, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));

  plugin_dispatch_values(&vl);
} /* void if_submit */

static int interface_read(void) {
#if HAVE_GETIFADDRS
  struct ifaddrs *if_list;

/* Darwin/Mac OS X and possible other *BSDs */
#if HAVE_STRUCT_IF_DATA
#define IFA_DATA if_data
#define IFA_RX_BYTES ifi_ibytes
#define IFA_TX_BYTES ifi_obytes
#define IFA_RX_PACKT ifi_ipackets
#define IFA_TX_PACKT ifi_opackets
#define IFA_RX_ERROR ifi_ierrors
#define IFA_TX_ERROR ifi_oerrors
/* #endif HAVE_STRUCT_IF_DATA */

#elif HAVE_STRUCT_NET_DEVICE_STATS
#define IFA_DATA net_device_stats
#define IFA_RX_BYTES rx_bytes
#define IFA_TX_BYTES tx_bytes
#define IFA_RX_PACKT rx_packets
#define IFA_TX_PACKT tx_packets
#define IFA_RX_ERROR rx_errors
#define IFA_TX_ERROR tx_errors
#else
#error "No suitable type for `struct ifaddrs->ifa_data' found."
#endif

  struct IFA_DATA *if_data;

  if (getifaddrs(&if_list) != 0)
    return -1;

  for (struct ifaddrs *if_ptr = if_list; if_ptr != NULL;
       if_ptr = if_ptr->ifa_next) {
    if (if_ptr->ifa_addr != NULL && if_ptr->ifa_addr->sa_family == AF_LINK) {
      if_data = (struct IFA_DATA *)if_ptr->ifa_data;

      if (!report_inactive && if_data->IFA_RX_PACKT == 0 &&
          if_data->IFA_TX_PACKT == 0)
        continue;

      if_submit(if_ptr->ifa_name, "if_octets", if_data->IFA_RX_BYTES,
                if_data->IFA_TX_BYTES);
      if_submit(if_ptr->ifa_name, "if_packets", if_data->IFA_RX_PACKT,
                if_data->IFA_TX_PACKT);
      if_submit(if_ptr->ifa_name, "if_errors", if_data->IFA_RX_ERROR,
                if_data->IFA_TX_ERROR);
    }
  }

  freeifaddrs(if_list);
/* #endif HAVE_GETIFADDRS */

#elif KERNEL_LINUX
  FILE *fh;
  char buffer[1024];
  derive_t incoming, outgoing;
  char *device;

  char *dummy;
  char *fields[16];
  int numfields;

  if ((fh = fopen("/proc/net/dev", "r")) == NULL) {
    WARNING("interface plugin: fopen: %s", STRERRNO);
    return -1;
  }

  while (fgets(buffer, 1024, fh) != NULL) {
    if (!(dummy = strchr(buffer, ':')))
      continue;
    dummy[0] = '\0';
    dummy++;

    device = buffer;
    while (device[0] == ' ')
      device++;

    if (device[0] == '\0')
      continue;

    numfields = strsplit(dummy, fields, 16);

    if (numfields < 11)
      continue;

    incoming = atoll(fields[1]);
    outgoing = atoll(fields[9]);
    if (!report_inactive && incoming == 0 && outgoing == 0)
      continue;

    if_submit(device, "if_packets", incoming, outgoing);

    incoming = atoll(fields[0]);
    outgoing = atoll(fields[8]);
    if_submit(device, "if_octets", incoming, outgoing);

    incoming = atoll(fields[2]);
    outgoing = atoll(fields[10]);
    if_submit(device, "if_errors", incoming, outgoing);

    incoming = atoll(fields[3]);
    outgoing = atoll(fields[11]);
    if_submit(device, "if_dropped", incoming, outgoing);
  }

  fclose(fh);
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
  derive_t rx;
  derive_t tx;
  char iname[DATA_MAX_NAME_LEN];

  if (kc == NULL)
    return -1;

  for (int i = 0; i < numif; i++) {
    if (kstat_read(kc, ksp[i], NULL) == -1)
      continue;

    if (unique_name)
      snprintf(iname, sizeof(iname), "%s_%d_%s", ksp[i]->ks_module,
               ksp[i]->ks_instance, ksp[i]->ks_name);
    else
      sstrncpy(iname, ksp[i]->ks_name, sizeof(iname));

    /* try to get 64bit counters */
    rx = get_kstat_value(ksp[i], "ipackets64");
    tx = get_kstat_value(ksp[i], "opackets64");
    /* or fallback to 32bit */
    if (rx == -1LL)
      rx = get_kstat_value(ksp[i], "ipackets");
    if (tx == -1LL)
      tx = get_kstat_value(ksp[i], "opackets");
    if (!report_inactive && rx == 0 && tx == 0)
      continue;
    if ((rx != -1LL) || (tx != -1LL))
      if_submit(iname, "if_packets", rx, tx);

    /* try to get 64bit counters */
    rx = get_kstat_value(ksp[i], "rbytes64");
    tx = get_kstat_value(ksp[i], "obytes64");
    /* or fallback to 32bit */
    if (rx == -1LL)
      rx = get_kstat_value(ksp[i], "rbytes");
    if (tx == -1LL)
      tx = get_kstat_value(ksp[i], "obytes");
    if ((rx != -1LL) || (tx != -1LL))
      if_submit(iname, "if_octets", rx, tx);

    /* no 64bit error counters yet */
    rx = get_kstat_value(ksp[i], "ierrors");
    tx = get_kstat_value(ksp[i], "oerrors");
    if ((rx != -1LL) || (tx != -1LL))
      if_submit(iname, "if_errors", rx, tx);
  }
/* #endif HAVE_LIBKSTAT */

#elif defined(HAVE_LIBSTATGRAB)
  sg_network_io_stats *ios;
  int num;

  ios = sg_get_network_io_stats(&num);

  for (int i = 0; i < num; i++) {
    if (!report_inactive && ios[i].rx == 0 && ios[i].tx == 0)
      continue;
    if_submit(ios[i].interface_name, "if_octets", ios[i].rx, ios[i].tx);
  }
/* #endif HAVE_LIBSTATGRAB */

#elif defined(HAVE_PERFSTAT)
  perfstat_id_t id;
  int ifs;

  if ((nif = perfstat_netinterface(NULL, NULL, sizeof(perfstat_netinterface_t),
                                   0)) < 0) {
    WARNING("interface plugin: perfstat_netinterface: %s", STRERRNO);
    return -1;
  }

  if (pnif != nif || ifstat == NULL) {
    free(ifstat);
    ifstat = malloc(nif * sizeof(*ifstat));
  }
  pnif = nif;

  id.name[0] = '\0';
  if ((ifs = perfstat_netinterface(&id, ifstat, sizeof(perfstat_netinterface_t),
                                   nif)) < 0) {
    WARNING("interface plugin: perfstat_netinterface (interfaces=%d): %s", nif,
            STRERRNO);
    return -1;
  }

  for (int i = 0; i < ifs; i++) {
    if (!report_inactive && ifstat[i].ipackets == 0 && ifstat[i].opackets == 0)
      continue;

    if_submit(ifstat[i].name, "if_octets", ifstat[i].ibytes, ifstat[i].obytes);
    if_submit(ifstat[i].name, "if_packets", ifstat[i].ipackets,
              ifstat[i].opackets);
    if_submit(ifstat[i].name, "if_errors", ifstat[i].ierrors,
              ifstat[i].oerrors);
  }
#endif /* HAVE_PERFSTAT */

  return 0;
} /* int interface_read */

void module_register(void) {
  plugin_register_config("interface", interface_config, config_keys,
                         config_keys_num);
#if HAVE_LIBKSTAT
  plugin_register_init("interface", interface_init);
#endif
  plugin_register_read("interface", interface_read);
} /* void module_register */
