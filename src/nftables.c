/**
 * collectd - src/nftables.c
 * Copyright (C) 2020       Jose M. Guisado
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
 *  Jose M. Guisado <guigom at riseup.net>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>

#include <libmnl/libmnl.h>
#include <libnftnl/expr.h>
#include <libnftnl/rule.h>
#include <libnftnl/udata.h>

static const char *config_keys[] = {"ip",     "ip6",  "arp",
                                    "bridge", "inet", "netdev"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

struct nftables_rule {
  int family;
  char *table, *chain, *comment;
  uint64_t pkts, bytes;
};
static struct nftables_rule **rule_list;
static int rule_count = 0;

static struct mnl_socket *nl;
static uint32_t portid;

static struct nftnl_rule *setup_rule(uint8_t family, const char *table,
                                     const char *chain) {
  struct nftnl_rule *r;

  r = nftnl_rule_alloc();
  if (r == NULL) {
    ERROR("nftables plugin: error allocating nftnl_rule");
    return NULL;
  }

  if (table != NULL)
    nftnl_rule_set_str(r, NFTNL_RULE_TABLE, table);
  if (chain != NULL)
    nftnl_rule_set_str(r, NFTNL_RULE_CHAIN, chain);

  nftnl_rule_set_u32(r, NFTNL_RULE_FAMILY, family);

  return r;
} /* nftnl_rule setup_rule */

static int parse_rule_udata_cb(const struct nftnl_udata *attr, void *data) {
  unsigned char *value = nftnl_udata_get(attr);
  uint8_t type = nftnl_udata_type(attr);
  uint8_t len = nftnl_udata_len(attr);
  const struct nftnl_udata **tb = data;

  switch (type) {
  case NFTNL_UDATA_RULE_COMMENT:
    if (value[len - 1] != '\0')
      return -1;
    break;
  default:
    return 0;
  }
  tb[type] = attr;
  return 0;
} /* int parse_rule_udata_cb */

static char *nftnl_rule_get_comment(const struct nftnl_rule *nlr) {
  const struct nftnl_udata *tb[NFTNL_UDATA_RULE_MAX + 1] = {};
  const void *data;
  uint32_t len;

  if (!nftnl_rule_is_set(nlr, NFTNL_RULE_USERDATA))
    return NULL;

  data = nftnl_rule_get_data(nlr, NFTNL_RULE_USERDATA, &len);

  if (nftnl_udata_parse(data, len, parse_rule_udata_cb, tb) < 0)
    return NULL;

  if (!tb[NFTNL_UDATA_RULE_COMMENT])
    return NULL;

  return strdup(nftnl_udata_get(tb[NFTNL_UDATA_RULE_COMMENT]));
} /* char *nftnl_rule_get_comment */

static void submit(const char *table, const char *chain, char *comment,
                   uint64_t bts, uint64_t cnt) {
  value_list_t vl = VALUE_LIST_INIT;
  int status;

  sstrncpy(vl.plugin, "nftables", sizeof(vl.plugin));

  status = ssnprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%s-%s",
                     table, chain);
  if ((status < 1) || ((unsigned int)status >= sizeof(vl.plugin_instance)))
    return;

  sstrncpy(vl.type_instance, comment, sizeof(vl.type_instance));

  sstrncpy(vl.type, "ipt_bytes", sizeof(vl.type));
  vl.values = &(value_t){.derive = (derive_t)bts};
  vl.values_len = 1;
  plugin_dispatch_values(&vl);

  sstrncpy(vl.type, "ipt_packets", sizeof(vl.type));
  vl.values = &(value_t){.derive = (derive_t)cnt};
  plugin_dispatch_values(&vl);
} /* void submit */

static int submit_cb(struct nftnl_expr *e, void *data) {
  const char *name = nftnl_expr_get_str(e, NFTNL_EXPR_NAME);
  uint64_t bytes, count;

  struct nftnl_rule *r = (struct nftnl_rule *)data;
  const char *table = nftnl_rule_get_str(r, NFTNL_RULE_TABLE);
  const char *chain = nftnl_rule_get_str(r, NFTNL_RULE_CHAIN);
  char *comment = nftnl_rule_get_comment(r);

  if (strcmp(name, "counter") == 0) {
    count = nftnl_expr_get_u64(e, NFTNL_EXPR_CTR_PACKETS);
    bytes = nftnl_expr_get_u64(e, NFTNL_EXPR_CTR_BYTES);
    plugin_log(LOG_NOTICE, "Table: %s | Chain: %s | Comment: %s", table, chain,
               comment);
    plugin_log(LOG_NOTICE, "Bytes: %lu | Packets: %lu", bytes, count);
    submit(table, chain, comment, bytes, count);
  }
  free(comment);

  return MNL_CB_OK;
} /* int submit_cb */

static int table_cb(const struct nlmsghdr *nlh, void *data) {
  struct nftnl_rule *t;
  char *comment = (char *)data;
  char *t_comment = NULL;

  t = nftnl_rule_alloc();
  if (t == NULL) {
    ERROR("nftables plugin: Error allocating nftnl_rule");
    goto err;
  }

  if (nftnl_rule_nlmsg_parse(nlh, t) < 0) {
    ERROR("nftables plugin: Error parsing nlmsghdr");
    goto err_free;
  }

  t_comment = nftnl_rule_get_comment(t);
  plugin_log(LOG_NOTICE, "table_cb | filter_comment: %s rule_comment: %s",
             comment, t_comment);
  if (strlen(comment) > 0) {
    if (t_comment && strcmp(t_comment, comment) == 0) {
      nftnl_expr_foreach(t, submit_cb, t);
    }
  } else if (t_comment) {
    nftnl_expr_foreach(t, submit_cb, t);
  }

err_free:
  if (t_comment)
    free(t_comment);
  nftnl_rule_free(t);
err:
  return MNL_CB_OK;
} /* int table_cb */

int nl_match_rules() {
  int num_failures = 0;
  uint32_t seq, ret = NFTNL_OUTPUT_DEFAULT;
  struct nlmsghdr *nlh;
  char buf[MNL_SOCKET_BUFFER_SIZE];

  for (int i = 0; i < rule_count; i++) {
    struct nftnl_rule *r;
    struct nftables_rule *r_info = rule_list[i];
    int family = r_info->family;
    char *table = r_info->table;
    char *chain = r_info->chain;
    char *comment = r_info->comment;

    seq = time(NULL);
    nlh = nftnl_rule_nlmsg_build_hdr(buf, NFT_MSG_GETRULE, family, NLM_F_DUMP,
                                     seq);

    r = setup_rule(family, table, chain, NULL);

    if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
      ERROR("nftables plugin: Error sending to mnl socket");
      num_failures++;
      continue;
    }

    ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
    plugin_log(LOG_NOTICE, "Rule counters from table: %s chain: %s | ret: %d",
               table, chain, ret);
    while (ret > 0) {
      ret = mnl_cb_run(buf, ret, seq, portid, table_cb, comment);
      if (ret <= 0)
        break;
      ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
    }
    if (ret == -1) {
      ERROR("nftables plugin: Error when reading from nl socket");
      num_failures++;
    }

    nftnl_rule_free(r);
  } /* for (i = 0 .. rule_count) */

  return num_failures;
} /* int nl_match_rules */

static int nftables_config(const char *key, const char *value) {

  struct nftables_rule *rule_info;
  struct nftables_rule **list;
  int family;
  char *fields[3] = {0};

  if (strcasecmp(key, "ip") == 0) {
    family = NFPROTO_IPV4;
  } else if (strcasecmp(key, "ip6") == 0) {
    family = NFPROTO_IPV6;
  } else if (strcasecmp(key, "arp") == 0) {
    family = NFPROTO_ARP;
  } else if (strcasecmp(key, "bridge") == 0) {
    family = NFPROTO_BRIDGE;
  } else if (strcasecmp(key, "netdev") == 0) {
    family = NFPROTO_NETDEV;
  } else if (strcasecmp(key, "inet") == 0) {
    family = NFPROTO_INET;
  } else {
    ERROR("Unknown family: %s", key);
    return -1;
  }

  char *value_copy = strdup(value);
  if (value_copy == NULL) {
    ERROR("strdup failed: %s", STRERRNO);
    return 1;
  }

  int fields_num = strsplit(value_copy, fields, 3);
  if (fields_num < 2) {
    free(value_copy);
    return 1;
  }

  char *table = fields[0];
  char *chain = fields[1];
  char *comment = "";

  if (fields_num == 3) {
    comment = fields[2];
  } else if (fields_num > 3) {
    plugin_log(LOG_NOTICE, "Ignoring excess arguments");
  }

  rule_info = (struct nftables_rule *)malloc(sizeof(struct nftables_rule));
  if (!rule_info) {
    ERROR("nftables plugin: nftables rule_info malloc failed: %s", STRERRNO);
    return 1;
  } else {
    rule_info->family = family;
    rule_info->table = strdup(table);
    rule_info->chain = strdup(chain);
    rule_info->comment = strdup(comment);
  }
  free(value_copy);

  list = realloc(rule_list, (rule_count + 1) * sizeof(struct nftables_rule *));
  if (list == NULL) {
    ERROR("nftables plugin: rule_info list realloc failed: %s", STRERRNO);
    return 1;
  }

  rule_list = list;
  rule_list[rule_count] = rule_info;
  rule_count++;

  plugin_log(LOG_INFO,
             "Stored %s rule info -> table: %s, chain: %s, comment: %s", key,
             rule_info->table, rule_info->chain, rule_info->comment);

  return 0;
} /* int nftables_config */

static int nftables_init(void) {
  nl = mnl_socket_open(NETLINK_NETFILTER);

  plugin_log(LOG_NOTICE, "Initializing nftables plugin...");
  for (int i = 0; i < rule_count; i++) {
    struct nftables_rule *r = rule_list[i];
    plugin_log(LOG_INFO,
               "rule_list[%d] => family: %d table: %s chain: %s comment: %s", i,
               r->family, r->table, r->chain, r->comment);
  }

  if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
    plugin_log(LOG_ERR, "error calling mnl_socket_bind");
    return -1;
  }

  portid = mnl_socket_get_portid(nl);
  plugin_log(LOG_INFO, "mnl socket bind, portid: %d", portid);

  return 0;
} /* int nftables_init */

static int nftables_read(void) {
  return nl_match_rules();
} /* int nftables_read */

static int nftables_shutdown(void) {
  mnl_socket_close(nl);
  struct nftables_rule *r;

  for (int i = 0; i < rule_count; i++) {
    r = rule_list[i];
    free(r->table);
    free(r->chain);
    free(r->comment);
    free(r);
  }
  free(rule_list);

  return 0;
} /* int nftables_shutdown */

void module_register(void) {
  plugin_register_config("nftables", nftables_config, config_keys,
                         config_keys_num);
  plugin_register_init("nftables", nftables_init);
  plugin_register_read("nftables", nftables_read);
  plugin_register_shutdown("nftables", nftables_shutdown);
} /* void module_register */
