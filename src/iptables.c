/**
 * collectd - src/iptables.c
 * Copyright (C) 2007       Sjoerd van der Berg
 * Copyright (C) 2007-2010  Florian octo Forster
 * Copyright (C) 2009       Marco Chiappero
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *  Sjoerd van der Berg <harekiet at users.sourceforge.net>
 *  Florian Forster <octo at collectd.org>
 *  Marco Chiappero <marco at absence.it>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include <libiptc/libip6tc.h>
#include <libiptc/libiptc.h>

#ifdef HAVE_SYS_CAPABILITY_H
#include <sys/capability.h>
#endif

/*
 * iptc_handle_t was available before libiptc was officially available as a
 * shared library. Note, that when the shared lib was introduced, the API and
 * ABI have changed slightly:
 * 'iptc_handle_t' used to be 'struct iptc_handle *' and most functions used
 * 'iptc_handle_t *' as an argument. Now, most functions use 'struct
 * iptc_handle *' (thus removing one level of pointer indirection).
 *
 * HAVE_IPTC_HANDLE_T is used to determine which API ought to be used. While
 * this is somewhat hacky, I didn't find better way to solve that :-/
 * -tokkee
 */
#ifndef HAVE_IPTC_HANDLE_T
typedef struct iptc_handle iptc_handle_t;
#endif
#ifndef HAVE_IP6TC_HANDLE_T
typedef struct ip6tc_handle ip6tc_handle_t;
#endif

/*
 * (Module-)Global variables
 */

/*
 * Config format should be `Chain table chainname',
 * e. g. `Chain mangle incoming'
 */
static const char *config_keys[] = {"Chain", "Chain6"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);
enum protocol_version_e { IPV4, IPV6 };
typedef enum protocol_version_e protocol_version_t;

/*
 * Each table/chain combo that will be queried goes into this list
 */
#ifndef XT_TABLE_MAXNAMELEN
#define XT_TABLE_MAXNAMELEN 32
#endif
typedef struct {
  protocol_version_t ip_version;
  char table[XT_TABLE_MAXNAMELEN];
  char chain[XT_TABLE_MAXNAMELEN];
  union {
    int num;
    char *comment;
  } rule;
  enum { RTYPE_NUM, RTYPE_COMMENT, RTYPE_COMMENT_ALL } rule_type;
  char name[64];
} ip_chain_t;

static ip_chain_t **chain_list;
static int chain_num;

static int iptables_config(const char *key, const char *value) {
  /* int ip_value; */
  protocol_version_t ip_version = 0;

  if (strcasecmp(key, "Chain") == 0)
    ip_version = IPV4;
  else if (strcasecmp(key, "Chain6") == 0)
    ip_version = IPV6;
  else
    return 1;

  ip_chain_t temp = {0};
  ip_chain_t *final, **list;
  char *table;
  int table_len;
  char *chain;
  int chain_len;

  char *value_copy;
  char *fields[4];
  int fields_num;

  value_copy = strdup(value);
  if (value_copy == NULL) {
    ERROR("strdup failed: %s", STRERRNO);
    return 1;
  }

  /*
   *  Time to fill the temp element
   *  Examine value string, it should look like:
   *  Chain[6] <table> <chain> [<comment|num> [name]]
   */

  /* set IPv4 or IPv6 */
  temp.ip_version = ip_version;

  /* Chain <table> <chain> [<comment|num> [name]] */
  fields_num = strsplit(value_copy, fields, 4);
  if (fields_num < 2) {
    free(value_copy);
    return 1;
  }

  table = fields[0];
  chain = fields[1];

  table_len = strlen(table) + 1;
  if ((unsigned int)table_len > sizeof(temp.table)) {
    ERROR("Table `%s' too long.", table);
    free(value_copy);
    return 1;
  }
  sstrncpy(temp.table, table, table_len);

  chain_len = strlen(chain) + 1;
  if ((unsigned int)chain_len > sizeof(temp.chain)) {
    ERROR("Chain `%s' too long.", chain);
    free(value_copy);
    return 1;
  }
  sstrncpy(temp.chain, chain, chain_len);

  if (fields_num >= 3) {
    char *comment = fields[2];
    int rule = atoi(comment);

    if (rule) {
      temp.rule.num = rule;
      temp.rule_type = RTYPE_NUM;
    } else {
      temp.rule.comment = strdup(comment);
      if (temp.rule.comment == NULL) {
        free(value_copy);
        return 1;
      }
      temp.rule_type = RTYPE_COMMENT;
    }
  } else {
    temp.rule_type = RTYPE_COMMENT_ALL;
  }

  if (fields_num >= 4)
    sstrncpy(temp.name, fields[3], sizeof(temp.name));

  free(value_copy);
  value_copy = NULL;
  table = NULL;
  chain = NULL;

  list = realloc(chain_list, (chain_num + 1) * sizeof(ip_chain_t *));
  if (list == NULL) {
    ERROR("realloc failed: %s", STRERRNO);
    sfree(temp.rule.comment);
    return 1;
  }

  chain_list = list;
  final = malloc(sizeof(*final));
  if (final == NULL) {
    ERROR("malloc failed: %s", STRERRNO);
    sfree(temp.rule.comment);
    return 1;
  }
  memcpy(final, &temp, sizeof(temp));
  chain_list[chain_num] = final;
  chain_num++;

  DEBUG("Chain #%i: table = %s; chain = %s;", chain_num, final->table,
        final->chain);

  return 0;
} /* int iptables_config */

static int submit6_match(const struct ip6t_entry_match *match,
                         const struct ip6t_entry *entry,
                         const ip_chain_t *chain, int rule_num) {
  int status;
  value_list_t vl = VALUE_LIST_INIT;

  /* Select the rules to collect */
  if (chain->rule_type == RTYPE_NUM) {
    if (chain->rule.num != rule_num)
      return 0;
  } else {
    if (strcmp(match->u.user.name, "comment") != 0)
      return 0;
    if ((chain->rule_type == RTYPE_COMMENT) &&
        (strcmp(chain->rule.comment, (char *)match->data) != 0))
      return 0;
  }

  sstrncpy(vl.plugin, "ip6tables", sizeof(vl.plugin));

  status = snprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%s-%s",
                    chain->table, chain->chain);
  if ((status < 1) || ((unsigned int)status >= sizeof(vl.plugin_instance)))
    return 0;

  if (chain->name[0] != '\0') {
    sstrncpy(vl.type_instance, chain->name, sizeof(vl.type_instance));
  } else {
    if (chain->rule_type == RTYPE_NUM)
      snprintf(vl.type_instance, sizeof(vl.type_instance), "%i",
               chain->rule.num);
    else
      sstrncpy(vl.type_instance, (char *)match->data, sizeof(vl.type_instance));
  }

  sstrncpy(vl.type, "ipt_bytes", sizeof(vl.type));
  vl.values = &(value_t){.derive = (derive_t)entry->counters.bcnt};
  vl.values_len = 1;
  plugin_dispatch_values(&vl);

  sstrncpy(vl.type, "ipt_packets", sizeof(vl.type));
  vl.values = &(value_t){.derive = (derive_t)entry->counters.pcnt};
  plugin_dispatch_values(&vl);

  return 0;
} /* int submit6_match */

/* This needs to return `int' for IPT_MATCH_ITERATE to work. */
static int submit_match(const struct ipt_entry_match *match,
                        const struct ipt_entry *entry, const ip_chain_t *chain,
                        int rule_num) {
  int status;
  value_list_t vl = VALUE_LIST_INIT;

  /* Select the rules to collect */
  if (chain->rule_type == RTYPE_NUM) {
    if (chain->rule.num != rule_num)
      return 0;
  } else {
    if (strcmp(match->u.user.name, "comment") != 0)
      return 0;
    if ((chain->rule_type == RTYPE_COMMENT) &&
        (strcmp(chain->rule.comment, (char *)match->data) != 0))
      return 0;
  }

  sstrncpy(vl.plugin, "iptables", sizeof(vl.plugin));

  status = snprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%s-%s",
                    chain->table, chain->chain);
  if ((status < 1) || ((unsigned int)status >= sizeof(vl.plugin_instance)))
    return 0;

  if (chain->name[0] != '\0') {
    sstrncpy(vl.type_instance, chain->name, sizeof(vl.type_instance));
  } else {
    if (chain->rule_type == RTYPE_NUM)
      snprintf(vl.type_instance, sizeof(vl.type_instance), "%i",
               chain->rule.num);
    else
      sstrncpy(vl.type_instance, (char *)match->data, sizeof(vl.type_instance));
  }

  sstrncpy(vl.type, "ipt_bytes", sizeof(vl.type));
  vl.values = &(value_t){.derive = (derive_t)entry->counters.bcnt};
  vl.values_len = 1;
  plugin_dispatch_values(&vl);

  sstrncpy(vl.type, "ipt_packets", sizeof(vl.type));
  vl.values = &(value_t){.derive = (derive_t)entry->counters.pcnt};
  plugin_dispatch_values(&vl);

  return 0;
} /* int submit_match */

/* ipv6 submit_chain */
static void submit6_chain(ip6tc_handle_t *handle, ip_chain_t *chain) {
  const struct ip6t_entry *entry;
  int rule_num;

  /* Find first rule for chain and use the iterate macro */
  entry = ip6tc_first_rule(chain->chain, handle);
  if (entry == NULL) {
    DEBUG("ip6tc_first_rule failed: %s", ip6tc_strerror(errno));
    return;
  }

  rule_num = 1;
  while (entry) {
    if (chain->rule_type == RTYPE_NUM) {
      submit6_match(NULL, entry, chain, rule_num);
    } else {
      IP6T_MATCH_ITERATE(entry, submit6_match, entry, chain, rule_num);
    }

    entry = ip6tc_next_rule(entry, handle);
    rule_num++;
  } /* while (entry) */
}

/* ipv4 submit_chain */
static void submit_chain(iptc_handle_t *handle, ip_chain_t *chain) {
  const struct ipt_entry *entry;
  int rule_num;

  /* Find first rule for chain and use the iterate macro */
  entry = iptc_first_rule(chain->chain, handle);
  if (entry == NULL) {
    DEBUG("iptc_first_rule failed: %s", iptc_strerror(errno));
    return;
  }

  rule_num = 1;
  while (entry) {
    if (chain->rule_type == RTYPE_NUM) {
      submit_match(NULL, entry, chain, rule_num);
    } else {
      IPT_MATCH_ITERATE(entry, submit_match, entry, chain, rule_num);
    }

    entry = iptc_next_rule(entry, handle);
    rule_num++;
  } /* while (entry) */
}

static int iptables_read(void) {
  int num_failures = 0;
  ip_chain_t *chain;

  /* Init the iptc handle structure and query the correct table */
  for (int i = 0; i < chain_num; i++) {
    chain = chain_list[i];

    if (!chain) {
      DEBUG("iptables plugin: chain == NULL");
      continue;
    }

    if (chain->ip_version == IPV4) {
#ifdef HAVE_IPTC_HANDLE_T
      iptc_handle_t _handle;
      iptc_handle_t *handle = &_handle;

      *handle = iptc_init(chain->table);
#else
      iptc_handle_t *handle;
      handle = iptc_init(chain->table);
#endif

      if (!handle) {
        ERROR("iptables plugin: iptc_init (%s) failed: %s", chain->table,
              iptc_strerror(errno));
        num_failures++;
        continue;
      }

      submit_chain(handle, chain);
      iptc_free(handle);
    } else if (chain->ip_version == IPV6) {
#ifdef HAVE_IP6TC_HANDLE_T
      ip6tc_handle_t _handle;
      ip6tc_handle_t *handle = &_handle;

      *handle = ip6tc_init(chain->table);
#else
      ip6tc_handle_t *handle;
      handle = ip6tc_init(chain->table);
#endif
      if (!handle) {
        ERROR("iptables plugin: ip6tc_init (%s) failed: %s", chain->table,
              ip6tc_strerror(errno));
        num_failures++;
        continue;
      }

      submit6_chain(handle, chain);
      ip6tc_free(handle);
    } else
      num_failures++;
  } /* for (i = 0 .. chain_num) */

  return (num_failures < chain_num) ? 0 : -1;
} /* int iptables_read */

static int iptables_shutdown(void) {
  for (int i = 0; i < chain_num; i++) {
    if ((chain_list[i] != NULL) && (chain_list[i]->rule_type == RTYPE_COMMENT))
      sfree(chain_list[i]->rule.comment);
    sfree(chain_list[i]);
  }
  sfree(chain_list);

  return 0;
} /* int iptables_shutdown */

static int iptables_init(void) {
#if defined(HAVE_SYS_CAPABILITY_H) && defined(CAP_NET_ADMIN)
  if (check_capability(CAP_NET_ADMIN) != 0) {
    if (getuid() == 0)
      WARNING("iptables plugin: Running collectd as root, but the "
              "CAP_NET_ADMIN capability is missing. The plugin's read "
              "function will probably fail. Is your init system dropping "
              "capabilities?");
    else
      WARNING("iptables plugin: collectd doesn't have the CAP_NET_ADMIN "
              "capability. If you don't want to run collectd as root, try "
              "running \"setcap cap_net_admin=ep\" on the collectd binary.");
  }
#endif
  return 0;
} /* int iptables_init */

void module_register(void) {
  plugin_register_config("iptables", iptables_config, config_keys,
                         config_keys_num);
  plugin_register_init("iptables", iptables_init);
  plugin_register_read("iptables", iptables_read);
  plugin_register_shutdown("iptables", iptables_shutdown);
} /* void module_register */
