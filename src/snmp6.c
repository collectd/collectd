/*
  This Plugin is based opn the interface.c Plugin.
*/

#if HAVE_LINUX_IF_H
#include <linux/if.h>
#elif HAVE_NET_IF_H
#include <net/if.h>
#endif

#include <ifaddrs.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

#include "plugin.h"
#include "utils/cmds/putval.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"

static const char *config_keys[] = {
    "Interface",
    "IgnoreSelected",
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *ignorelist;
struct ifaddrs *if_list;

static int snmp6_config(const char *key, const char *value) {
  if (ignorelist == NULL)
    ignorelist = ignorelist_create(/* invert = */ 1);

  if (strcasecmp(key, "Interface") == 0) {
    ignorelist_add(ignorelist, value);
  } else if (strcasecmp(key, "IgnoreSelected") == 0) {
    int invert = 1;
    if (IS_TRUE(value))
      invert = 0;
    ignorelist_set_invert(ignorelist, invert);
  }

  return 0;
}

/* Copied from interface.c */
static void snmp6_submit(const char *dev, const char *type, derive_t rx,
                         derive_t tx) {
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[] = {
      {.derive = rx},
      {.derive = tx},
  };

  if (ignorelist_match(ignorelist, dev) != 0)
    return;

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);
  sstrncpy(vl.plugin, "snmp6", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, dev, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));

  plugin_dispatch_values(&vl);
} /* void if_submit */

int snmp_read(char *ifname) {
  FILE *fh;
  char buffer[1024];
  char *fields[16];
  int numfields;
  int currline = 0;
  derive_t data[76];
  char procpath[1024];
  int offset = 0;

  if (strncmp("all", ifname, strlen("all")) == 0) {
    snprintf(procpath, 1024, "/proc/net/snmp6");
    offset = 1;
  } else {
    snprintf(procpath, 1024, "/proc/net/dev_snmp6/%s", ifname);
  }

  if ((fh = fopen(procpath, "r")) == NULL) {
    WARNING("interface plugin: fopen: %s", STRERRNO);
    return -1;
  }

  while (fgets(buffer, 1024, fh) != NULL) {
    numfields = strsplit(buffer, fields, 16);

    if (numfields < 2)
      continue;

    data[currline++] = atoll(fields[1]);
  }

  fclose(fh);

  if (currline < 25) {
    return -1;
  }

  snmp6_submit(ifname, "if_octets", data[23 - offset], data[24 - offset]);
  snmp6_submit(ifname, "if_octets_mcast", data[25 - offset], data[26 - offset]);
  snmp6_submit(ifname, "if_octets_bcast", data[27 - offset], data[28 - offset]);
  return 0;
}

int read_all_interfaces(void) {
#ifndef HAVE_IFADDRS_H
  return -1;
#else
  if (getifaddrs(&if_list) != 0)
    return -1;

  for (struct ifaddrs *if_ptr = if_list; if_ptr != NULL;
       if_ptr = if_ptr->ifa_next) {
    snmp_read(if_ptr->ifa_name);
  }
  snmp_read("all");
  return 0;
#endif
}

void module_register(void) {
  plugin_register_config("snmp6", snmp6_config, config_keys, config_keys_num);
  plugin_register_read("snmp6", read_all_interfaces);
} /* void module_register */
