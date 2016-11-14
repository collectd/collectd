/**
 * collectd - src/virt.c
 * Copyright (C) 2016 Francesco Romani <fromani at redhat.com>
 * Copyright (C) 2006-2008  Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the license is applicable.
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
 *   Francesco Romani <fromani at redhat.com>
 *   Richard W.M. Jones <rjones at redhat.com>
 **/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "common.h"
#include "configfile.h"
#include "plugin.h"
#include "utils_complain.h"
#include "utils_ignorelist.h"
#include "collectd.h"

#include "vminfo.h"

#define PLUGIN_NAME "virt2"

/*
 * Synopsis:
 * <Plugin "virt">
 *   Connection "qemu:///system"
 *   Instances 5
 * </Plugin>
 */

#define METADATA_VM_PARTITION_URI "http://ovirt.org/ovirtmap/tag/1.0"
#define METADATA_VM_PARTITION_ELEMENT "tag"
#define METADATA_VM_PARTITION_PREFIX "ovirtmap"

enum {
  INSTANCES_DEFAULT_NUM = 1,
  BUFFER_MAX_LEN = 256,
  PARTITION_TAG_MAX_LEN = 32,
  INTERFACE_NUMBER_MAX_LEN = 32,
  INSTANCES_MAX = 128,
  VM_VALUES_NUM = 256,
};

const char *virt2_config_keys[] = {"Connection",

                                   "RefreshInterval",

                                   "Domain",
                                   "BlockDevice",
                                   "InterfaceDevice",
                                   "IgnoreSelected",

                                   "HostnameFormat",
                                   "InterfaceFormat",

                                   "PluginInstanceFormat",

                                   "Instances",
                                   "DebugPartitioning",
                                   NULL};
#define NR_CONFIG_KEYS                                                         \
  ((sizeof virt2_config_keys / sizeof virt2_config_keys[0]) - 1)

/* HostnameFormat. */
#define HF_MAX_FIELDS 3

enum hf_field { hf_none = 0, hf_hostname, hf_name, hf_uuid };

/* PluginInstanceFormat */
#define PLGINST_MAX_FIELDS 2

enum plginst_field { plginst_none = 0, plginst_name, plginst_uuid };

/* InterfaceFormat. */
enum if_field { if_address, if_name, if_number };

typedef struct virt2_config_s virt2_config_t;
struct virt2_config_s {
  char *connection_uri;
  size_t instances;
  cdtime_t interval; /* could be 0, and it's OK */
  int debug_partitioning;
  enum hf_field hostname_format[HF_MAX_FIELDS];
  enum plginst_field plugin_instance_format[PLGINST_MAX_FIELDS];
  enum if_field interface_format;
  /* not user-facing */
  int stats;
  int flags;
};

typedef struct virt2_state_s virt2_state_t;

typedef struct virt2_instance_s virt2_instance_t;
struct virt2_instance_s {
  virt2_state_t *state;
  const virt2_config_t *conf;

  GArray *doms;
  virDomainPtr *domains_all;
  size_t domains_num;

  char tag[PARTITION_TAG_MAX_LEN];
  size_t id;
};

typedef struct virt2_user_data_s virt2_user_data_t;
struct virt2_user_data_s {
  virt2_instance_t inst;
  user_data_t ud;
};

struct virt2_state_s {
  virConnectPtr conn;
  GHashTable *known_tags;
  ignorelist_t *il_domains;
  ignorelist_t *il_block_devices;
  ignorelist_t *il_iface_devices;
  size_t instances;
};

typedef struct virt2_context_s virt2_context_t;
struct virt2_context_s {
  virt2_user_data_t user_data[INSTANCES_MAX];
  virt2_state_t state;
  virt2_config_t conf;
};

typedef struct virt2_domain_s virt2_domain_t;
struct virt2_domain_s {
  virDomainPtr dom;
  char tag[PARTITION_TAG_MAX_LEN];
  char uuid[VIR_UUID_STRING_BUFLEN + 1];
  char *name;
};

typedef struct virt2_array_s virt2_array_t;
struct virt2_array_s {
  gchar *data;
  guint len;
};


/* *** */

static int ignore_device_match(ignorelist_t *il, const char *domname,
                               const char *devpath) {
  char *name;
  int n, r;

  if ((domname == NULL) || (devpath == NULL))
    return 0;

  n = strlen(domname) + strlen(devpath) + 2;
  name = malloc(n);
  if (name == NULL) {
    ERROR(PLUGIN_NAME " plugin: malloc failed.");
    return 0;
  }
  ssnprintf(name, n, "%s:%s", domname, devpath);
  r = ignorelist_match(il, name);
  sfree(name);
  return r;
}

/* *** */

virt2_context_t default_context = {
    .conf =
        {
            .hostname_format = {hf_name},
            .plugin_instance_format = {plginst_none},
            .interface_format = if_name,
            /*
             * Using 0 for @stats returns all stats groups supported by the
             * given hypervisor.
             * http://libvirt.org/html/libvirt-libvirt-domain.html#virConnectGetAllDomainStats
             */
            .stats = 0,
            .flags = 0,
        },
};

static virt2_context_t *virt2_get_default_context() { return &default_context; }

/* *** */

static void virt2_value_list_set_plugin_instance(value_list_t *vl,
                                                 const VMInfo *info,
                                                 const virt2_config_t *cfg) {
  int i, n;
  for (i = 0; i < PLGINST_MAX_FIELDS; ++i) {
    if (cfg->plugin_instance_format[i] == plginst_none)
      continue;

    n = sizeof(vl->plugin_instance) - strlen(vl->plugin_instance) - 2;

    if (i > 0 && n >= 1) {
      strncat(vl->plugin_instance, ":", 1);
      n--;
    }

    switch (cfg->plugin_instance_format[i]) {
    case plginst_none:
      break;
    case plginst_name:
      strncat(vl->plugin_instance, info->name, n);
      break;
    case plginst_uuid:
      strncat(vl->plugin_instance, info->uuid, n);
      break;
    }
  }

  vl->plugin_instance[sizeof(vl->plugin_instance) - 1] = '\0';
}

static void virt2_value_list_set_host(value_list_t *vl, const VMInfo *info,
                                      const virt2_config_t *cfg) {
  int i, n;
  vl->host[0] = '\0';
  for (i = 0; i < HF_MAX_FIELDS; i++) {
    if (cfg->hostname_format[i] == hf_none)
      continue;

    n = DATA_MAX_NAME_LEN - strlen(vl->host) - 2;

    if (i > 0 && n >= 1) {
      strncat(vl->host, ":", 1);
      n--;
    }

    switch (cfg->hostname_format[i]) {
    case hf_none:
      break;
    case hf_hostname:
      strncat(vl->host, hostname_g, n);
      break;
    case hf_name:
      strncat(vl->host, info->name, n);
      break;
    case hf_uuid:
      strncat(vl->host, info->uuid, n);
      break;
    }
  }
  vl->host[sizeof(vl->host) - 1] = '\0';
}

static int virt2_submit(const virt2_config_t *cfg, const VMInfo *info,
                        const char *type, const char *type_instance,
                        value_t *values, size_t values_len) {
  value_list_t vl = VALUE_LIST_INIT;

  sstrncpy(vl.plugin, PLUGIN_NAME, sizeof(vl.plugin));
  virt2_value_list_set_plugin_instance(&vl, info, cfg);
  virt2_value_list_set_host(&vl, info, cfg);
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  vl.values = values;
  vl.values_len = values_len;

  plugin_dispatch_values(&vl);
  return 0;
}

static int virt2_dispatch_cpu(virt2_instance_t *inst, const VMInfo *vm) {
  value_t val;

  val.derive = vm->info.cpuTime;
  virt2_submit(inst->conf, vm, "virt_cpu_total", "", &val, 1);
  // TODO: cpu.user, cpu.sys, cpu.total

  for (size_t j = 0; j < vm->vcpu.nstats; j++) {
    char type_instance[DATA_MAX_NAME_LEN];
    ssnprintf(type_instance, sizeof(type_instance), "%zu", j);
    const VCpuStats *stats =
        (vm->vcpu.xstats) ? vm->vcpu.xstats : vm->vcpu.stats;
    val.derive = stats[j].time;
    virt2_submit(inst->conf, vm, "virt_vcpu", type_instance, &val, 1);
  }

  return 0;
}

static int virt2_dispatch_memory(virt2_instance_t *inst, const VMInfo *vm) {
  value_t val;
  val.gauge = vm->info.memory * 1024;
  virt2_submit(inst->conf, vm, "memory", "total", &val, 1);
  for (int j = 0; j < vm->memstats_count; j++) {
    static const char *tags[] = {"swap_in",        "swap_out", "major_fault",
                                 "minor_fault",    "unused",   "available",
                                 "actual_balloon", "rss"};
    if ((vm->memstats[j].tag < 0) ||
        (vm->memstats[j].tag >= STATIC_ARRAY_SIZE(tags))) {
      ERROR(PLUGIN_NAME " plugin#%s: unknown tag %i", inst->tag,
            vm->memstats[j].tag);
      continue;
    }
    val.gauge = vm->memstats[j].val * 1024;
    virt2_submit(inst->conf, vm, "memory", tags[vm->memstats[j].tag], &val, 1);
  }

  return 0;
}

static int virt2_dispatch_block(virt2_instance_t *inst, const VMInfo *vm) {
  value_t vals[2];

  for (size_t j = 0; j < vm->block.nstats; j++) {
    const BlockStats *stats =
        (vm->block.xstats) ? vm->block.xstats : vm->block.stats;
    const char *name = stats[j].xname ? stats[j].xname : stats[j].name;

    if (inst->state->il_block_devices != NULL &&
        ignore_device_match(inst->state->il_block_devices, vm->name, name) != 0)
      continue;

    vals[0].derive = stats[j].rd_reqs;
    vals[1].derive = stats[j].wr_reqs;
    virt2_submit(inst->conf, vm, "disk_ops", name, vals,
                 STATIC_ARRAY_SIZE(vals));

    vals[0].derive = stats[j].rd_bytes;
    vals[1].derive = stats[j].wr_bytes;
    virt2_submit(inst->conf, vm, "disk_octets", name, vals,
                 STATIC_ARRAY_SIZE(vals));
  }
  return 0;
}

static int virt2_dispatch_iface(virt2_instance_t *inst, const VMInfo *vm) {
  value_t vals[2]; // TODO: magic number
  size_t j;

  for (j = 0; j < vm->iface.nstats; j++) {
    char iface_num[INTERFACE_NUMBER_MAX_LEN] = {'\0'};
    const IFaceStats *stats =
        (vm->iface.xstats) ? vm->iface.xstats : vm->iface.stats;
    const char *iface_name = stats[j].xname ? stats[j].xname : stats[j].name;
    const char *display_name = NULL;

    if (inst->state->il_iface_devices != NULL &&
        ignore_device_match(inst->state->il_iface_devices, vm->name,
                            iface_name) != 0)
      // TODO: check match by address
      continue;

    switch (inst->conf->interface_format) {
    case if_address:
      ERROR(PLUGIN_NAME " plugin: not yet supported, fallback to 'name'");
      display_name = iface_name;
      break;
    case if_number:
      ssnprintf(iface_num, sizeof(iface_num), "interface-%zu", j + 1);
      display_name = iface_num;
      break;
    case if_name: // fallthrough
    default:
      display_name = iface_name;
      break;
    }

    vals[0].derive = stats[j].rx_bytes;
    vals[1].derive = stats[j].tx_bytes;
    virt2_submit(inst->conf, vm, "if_octets", display_name, vals,
                 STATIC_ARRAY_SIZE(vals));

    vals[0].derive = stats[j].rx_pkts;
    vals[1].derive = stats[j].tx_pkts;
    virt2_submit(inst->conf, vm, "if_packets", display_name, vals,
                 STATIC_ARRAY_SIZE(vals));

    vals[0].derive = stats[j].rx_errs;
    vals[1].derive = stats[j].tx_errs;
    virt2_submit(inst->conf, vm, "if_errors", display_name, vals,
                 STATIC_ARRAY_SIZE(vals));

    vals[0].derive = stats[j].rx_drop;
    vals[1].derive = stats[j].tx_drop;
    virt2_submit(inst->conf, vm, "if_dropped", display_name, vals,
                 STATIC_ARRAY_SIZE(vals));
  }

  return 0;
}

/* *** */

static size_t virt2_get_optimal_instance_count(virt2_context_t *ctx) {
  /*
   * TODO: if ctx->conf.instances == -1, query libvirt using
   * the ADMIN API for the worker thread pool size, and return
   * that value.
   */
  size_t num = ctx->conf.instances;
  if (num == 0) {
    num = INSTANCES_DEFAULT_NUM;
  }
  INFO(PLUGIN_NAME " plugin: using %zu instances (configured=%zu)", num,
       ctx->conf.instances);
  return num;
}

static int virt2_init_instance(virt2_context_t *ctx, size_t i,
                               int (*func_body)(user_data_t *ud)) {
  virt2_user_data_t *user_data = &(ctx->user_data[i]);

  virt2_instance_t *inst = &user_data->inst;
  ssnprintf(inst->tag, sizeof(inst->tag), "virt-%zu", i);
  inst->state = &ctx->state;
  inst->conf = &ctx->conf;
  inst->id = i;

  user_data_t *ud = &user_data->ud;
  ud->data = inst;
  ud->free_func = NULL;

  g_hash_table_add(ctx->state.known_tags, inst->tag);
  return plugin_register_complex_read(NULL, inst->tag, func_body,
                                      ctx->conf.interval, ud);
}

static int virt2_domain_get_tag(virt2_domain_t *vdom, const char *xml) {
  char xpath_str[BUFFER_MAX_LEN] = {'\0'};
  xmlDocPtr xml_doc = NULL;
  xmlXPathContextPtr xpath_ctx = NULL;
  xmlXPathObjectPtr xpath_obj = NULL;
  xmlNodePtr xml_node = NULL;
  int err = -1;

  if (xml == NULL) {
    ERROR(PLUGIN_NAME " plugin: xmlReadDoc() NULL XML on domain %s",
          vdom->uuid);
    goto done;
  }

  xml_doc = xmlReadDoc((const xmlChar *)xml, NULL, NULL,
                       XML_PARSE_NONET | XML_PARSE_NSCLEAN);
  if (xml_doc == NULL) {
    ERROR(PLUGIN_NAME " plugin: xmlReadDoc() failed on domain %s", vdom->uuid);
    goto done;
  }

  xpath_ctx = xmlXPathNewContext(xml_doc);
  err = xmlXPathRegisterNs(xpath_ctx,
                           (const xmlChar *)METADATA_VM_PARTITION_PREFIX,
                           (const xmlChar *)METADATA_VM_PARTITION_URI);
  if (err) {
    ERROR(PLUGIN_NAME " plugin: xmlXpathRegisterNs(%s, %s) failed on domain %s",
          METADATA_VM_PARTITION_PREFIX, METADATA_VM_PARTITION_URI, vdom->uuid);
    goto done;
  }

  ssnprintf(xpath_str, sizeof(xpath_str), "/domain/metadata/%s:%s/text()",
            METADATA_VM_PARTITION_PREFIX, METADATA_VM_PARTITION_ELEMENT);
  xpath_obj = xmlXPathEvalExpression((xmlChar *)xpath_str, xpath_ctx);
  if (xpath_obj == NULL) {
    ERROR(PLUGIN_NAME " plugin: xmlXPathEval(%s) failed on domain %s",
          xpath_str, vdom->uuid);
    goto done;
  }

  if (xpath_obj->type != XPATH_NODESET) {
    ERROR(PLUGIN_NAME " plugin: xmlXPathEval(%s) unexpected return type %d "
                      "(wanted %d) on domain %s",
          xpath_str, xpath_obj->type, XPATH_NODESET, vdom->uuid);
    goto done;
  }

  /*
   * from now on there is no real error, it's ok if a domain
   * doesn't have the metadata partition tag.
   */
  err = 0;

  if (xpath_obj->nodesetval == NULL || xpath_obj->nodesetval->nodeNr != 1) {
    DEBUG(PLUGIN_NAME " plugin: xmlXPathEval(%s) return nodeset size=%i "
                      "expected=1 on domain %s",
          xpath_str,
          (xpath_obj->nodesetval == NULL) ? 0 : xpath_obj->nodesetval->nodeNr,
          vdom->uuid);
  } else {
    xml_node = xpath_obj->nodesetval->nodeTab[0];
    sstrncpy(vdom->tag, (const char *)xml_node->content, sizeof(vdom->tag));
  }

done:
  if (xpath_obj)
    xmlXPathFreeObject(xpath_obj);
  if (xpath_ctx)
    xmlXPathFreeContext(xpath_ctx);
  if (xml_doc)
    xmlFreeDoc(xml_doc);

  return err;
}

static int virt2_acquire_domains(virt2_instance_t *inst) {
  unsigned int flags = VIR_CONNECT_LIST_DOMAINS_RUNNING;
  int ret =
      virConnectListAllDomains(inst->state->conn, &inst->domains_all, flags);
  if (ret < 0) {
    ERROR(PLUGIN_NAME " plugin#%s: virConnectListAllDomains failed: %s",
          inst->tag, virGetLastErrorMessage());
    return -1;
  }
  inst->domains_num = (size_t)ret;
  DEBUG(PLUGIN_NAME " plugin#%s: found %i domains", inst->tag,
        inst->domains_num);

  if (inst->domains_num == 0)
    return 1;
  return 0;
}

static void virt2_release_domains(virt2_instance_t *inst) {
  for (size_t i = 0; i < inst->domains_num; i++)
    virDomainFree(inst->domains_all[i]);
  sfree(inst->domains_all);
  inst->domains_num = 0;
}

static int virt2_dispatch_samples(virt2_instance_t *inst,
                                  virDomainStatsRecordPtr *records,
                                  int records_num) {
  for (int i = 0; i < records_num; i++) {
    VMInfo vm;
    vminfo_init(&vm);
    vminfo_parse(&vm, records[i], TRUE);

    virt2_dispatch_cpu(inst, &vm);
    virt2_dispatch_memory(inst, &vm);
    virt2_dispatch_block(inst, &vm);
    virt2_dispatch_iface(inst, &vm);

    vminfo_free(&vm);
  }
  return 0;
}

static int virt2_sample_domains(virt2_instance_t *inst, GArray *doms) {
  if (doms->len == 0) // nothing to do, and it's OK
    return 0;

  virDomainStatsRecordPtr *records = NULL;
  int ret =
      virDomainListGetStats(((virDomainPtr *)doms->data), inst->conf->stats,
                            &records, inst->conf->flags);
  if (ret == -1)
    return ret;

  int records_num = ret;
  ret = virt2_dispatch_samples(inst, records, records_num);
  virDomainStatsRecordListFree(records);

  return ret;
}

static int virt2_domain_init(virt2_domain_t *vdom, virDomainPtr dom) {
  memset(vdom, 0, sizeof(*vdom));
  vdom->dom = dom;
  virDomainGetUUIDString(dom, vdom->uuid);

  unsigned int flags = 0;
  char *dom_xml = virDomainGetXMLDesc(dom, flags);
  if (dom_xml == NULL) {
    ERROR(PLUGIN_NAME " plugin: domain %s don't provide XML: %s", vdom->uuid,
          virGetLastErrorMessage());
    return -1;
  }

  int err = virt2_domain_get_tag(vdom, dom_xml);
  sfree(dom_xml);
  return err;
}

static int virt2_instance_include_domain(virt2_domain_t *vdom,
                                         virt2_instance_t *inst) {
  /* instance#0 will always be there, so it is in charge of extra duties */
  if (inst->id == 0) {
    if (vdom->tag[0] == '\0' ||
        !g_hash_table_contains(inst->state->known_tags, vdom->tag)) {
      if (inst->conf->debug_partitioning)
        WARNING(PLUGIN_NAME " plugin#%s: adopted domain %s "
                            "with unknown tag '%s'",
                inst->tag, vdom->uuid, vdom->tag);
      return 1;
    }
  }
  return (strcmp(vdom->tag, inst->tag) == 0);
}

static int virt2_domain_ignored(virt2_instance_t *inst, virDomainPtr dom) {
  const char *name = virDomainGetName(dom);
  return (inst->state->il_domains &&
          ignorelist_match(inst->state->il_domains, name) != 0);
}

static GArray *virt2_partition_domains(virt2_instance_t *inst) {
  GArray *doms =
      g_array_sized_new(TRUE, FALSE, sizeof(virDomainPtr), inst->domains_num);
  if (!doms) {
    ERROR(PLUGIN_NAME " plugin#%s: cannot allocate the libvirt domain "
                      "partition (%zu domains)",
          inst->tag, inst->domains_num);
    return NULL;
  }

  for (size_t i = 0; i < inst->domains_num; i++) {
    virt2_domain_t vdom = {NULL};
    int err;
    if (virt2_domain_ignored(inst, inst->domains_all[i]))
      continue;
    err = virt2_domain_init(&vdom, inst->domains_all[i]);
    if (err)
      continue;
    if (!virt2_instance_include_domain(&vdom, inst))
      continue;

    g_array_append_val(doms, (inst->domains_all[i]));
  }

  return doms;
}

int virt2_read_domains(user_data_t *ud) {
  int err;
  virt2_instance_t *inst = ud->data;
  if (!inst) {
    ERROR(PLUGIN_NAME " plugin: NULL userdata");
    return -1;
  }

  err = virt2_acquire_domains(inst);
  if (err)
    return -1;

  if (inst->domains_num == 0)
    return 0; /* nothing to do here, but it's OK */

  GArray *doms = virt2_partition_domains(inst);
  if (doms != NULL) {
    err = virt2_sample_domains(inst, doms);
    g_array_free(doms, TRUE);
  } else
    err = -1;

  virt2_release_domains(inst);
  return err;
}

static int virt2_setup(virt2_context_t *ctx) {
  ctx->state.known_tags = g_hash_table_new(g_str_hash, g_str_equal);
  ctx->state.il_domains = NULL;       /* will be created lazily */
  ctx->state.il_block_devices = NULL; /* ditto */
  ctx->state.il_iface_devices = NULL; /* ditto */

  for (size_t i = 0; i < ctx->state.instances; i++)
    virt2_init_instance(ctx, i, virt2_read_domains);

  return 0;
}

static int virt2_teardown(virt2_context_t *ctx) {
  if (ctx->state.il_domains != NULL)
    ignorelist_free(ctx->state.il_domains);
  if (ctx->state.il_block_devices != NULL)
    ignorelist_free(ctx->state.il_block_devices);
  if (ctx->state.il_iface_devices != NULL)
    ignorelist_free(ctx->state.il_iface_devices);

  g_hash_table_destroy(ctx->state.known_tags);
  return 0;
}

/* *** */

int virt2_config(const char *key, const char *value) {
  virt2_context_t *ctx = virt2_get_default_context();
  virt2_config_t *cfg = &ctx->conf;

  if (strcasecmp(key, "Connection") == 0) {
    char *tmp = sstrdup(value);
    if (tmp == NULL) {
      ERROR(PLUGIN_NAME " plugin: Connection strdup failed.");
      return 1;
    }
    sfree(cfg->connection_uri);
    cfg->connection_uri = tmp;
    return 0;
  }
  if (strcasecmp(key, "Instances") == 0) {
    char *eptr = NULL;
    long val = strtol(value, &eptr, 10);
    if (eptr == NULL || *eptr != '\0')
      return 1;
    if (val <= 0) {
      // TODO: remove once we have autotune using libvirt admin API
      ERROR(PLUGIN_NAME " plugin: Instances <= 0 makes no sense.");
      return 1;
    }
    if (val > INSTANCES_MAX) {
      ERROR(PLUGIN_NAME " plugin: Instances=%li > INSTANCES_MAX=%i"
                        " use a lower setting or recompile the plugin.",
            val, INSTANCES_MAX);
      return 1;
    }
    cfg->instances = val;
    return 0;
  }
  if (strcasecmp(key, "RefreshInterval") == 0) {
    char *eptr = NULL;
    double val = strtod(value, &eptr);
    if (eptr == NULL || *eptr != '\0')
      return 1;
    if (val <= 0) {
      ERROR(PLUGIN_NAME " plugin: RefreshInterval <= 0 makes no sense.");
      return 1;
    }
    cfg->interval = DOUBLE_TO_CDTIME_T(val);
    return 0;
  }
  if (strcasecmp(key, "DebugPartitioning") == 0) {
    cfg->debug_partitioning = IS_TRUE(value);
    return 0;
  }

  if (strcasecmp(key, "Domain") == 0) {
    if (ctx->state.il_domains == NULL)
      ctx->state.il_domains = ignorelist_create(1);
    if (ignorelist_add(ctx->state.il_domains, value))
      return 1;
    return 0;
  }
  if (strcasecmp(key, "BlockDevice") == 0) {
    if (ctx->state.il_block_devices == NULL)
      ctx->state.il_block_devices = ignorelist_create(1);
    if (ignorelist_add(ctx->state.il_block_devices, value))
      return 1;
    return 0;
  }
  if (strcasecmp(key, "InterfaceDevice") == 0) {
    if (ctx->state.il_iface_devices == NULL)
      ctx->state.il_iface_devices = ignorelist_create(1);
    if (ignorelist_add(ctx->state.il_iface_devices, value))
      return 1;
    return 0;
  }

  if (strcasecmp(key, "IgnoreSelected") == 0) {
    if (IS_TRUE(value)) {
      ignorelist_set_invert(ctx->state.il_domains, 0);
      ignorelist_set_invert(ctx->state.il_block_devices, 0);
      ignorelist_set_invert(ctx->state.il_iface_devices, 0);
    } else {
      ignorelist_set_invert(ctx->state.il_domains, 1);
      ignorelist_set_invert(ctx->state.il_block_devices, 1);
      ignorelist_set_invert(ctx->state.il_iface_devices, 1);
    }
    return 0;
  }
  if (strcasecmp(key, "HostnameFormat") == 0) {
    int i, n;
    char *fields[HF_MAX_FIELDS];
    char *value_copy = strdup(value);
    if (value_copy == NULL) {
      ERROR(PLUGIN_NAME " plugin: strdup failed.");
      return -1;
    }

    n = strsplit(value_copy, fields, HF_MAX_FIELDS);
    if (n < 1) {
      sfree(value_copy);
      ERROR(PLUGIN_NAME " plugin: HostnameFormat: no fields");
      return -1;
    }

    for (i = 0; i < n; ++i) {
      if (strcasecmp(fields[i], "hostname") == 0)
        cfg->hostname_format[i] = hf_hostname;
      else if (strcasecmp(fields[i], "name") == 0)
        cfg->hostname_format[i] = hf_name;
      else if (strcasecmp(fields[i], "uuid") == 0)
        cfg->hostname_format[i] = hf_uuid;
      else {
        ERROR(PLUGIN_NAME " plugin: unknown HostnameFormat field: %s",
              fields[i]);
        sfree(value_copy);
        return -1;
      }
    }
    sfree(value_copy);

    for (i = n; i < HF_MAX_FIELDS; ++i)
      cfg->hostname_format[i] = hf_none;

    return 0;
  }
  if (strcasecmp(key, "PluginInstanceFormat") == 0) {
    int i, n;
    char *fields[PLGINST_MAX_FIELDS];
    char *value_copy = strdup(value);
    if (value_copy == NULL) {
      ERROR(PLUGIN_NAME " plugin: strdup failed.");
      return -1;
    }

    n = strsplit(value_copy, fields, PLGINST_MAX_FIELDS);
    if (n < 1) {
      sfree(value_copy);
      ERROR(PLUGIN_NAME " plugin: PluginInstanceFormat: no fields");
      return -1;
    }

    for (i = 0; i < n; ++i) {
      if (strcasecmp(fields[i], "none") == 0) {
        cfg->plugin_instance_format[i] = plginst_none;
        break;
      } else if (strcasecmp(fields[i], "name") == 0)
        cfg->plugin_instance_format[i] = plginst_name;
      else if (strcasecmp(fields[i], "uuid") == 0)
        cfg->plugin_instance_format[i] = plginst_uuid;
      else {
        ERROR(PLUGIN_NAME " plugin: unknown PluginInstanceFormat field: %s",
              fields[i]);
        sfree(value_copy);
        return -1;
      }
    }
    sfree(value_copy);

    for (i = n; i < PLGINST_MAX_FIELDS; ++i)
      cfg->plugin_instance_format[i] = plginst_none;

    return 0;
  }

  if (strcasecmp(key, "InterfaceFormat") == 0) {
    if (strcasecmp(value, "name") == 0)
      cfg->interface_format = if_name;
    else if (strcasecmp(value, "address") == 0)
      cfg->interface_format = if_address;
    else if (strcasecmp(value, "number") == 0)
      cfg->interface_format = if_number;
    else {
      ERROR(PLUGIN_NAME " plugin: unknown InterfaceFormat: %s", value);
      return -1;
    }
    return 0;
  }

  /* Unrecognised option. */
  return -1;
}

static int virt2_init(void) {
  virt2_context_t *ctx = virt2_get_default_context();
  ctx->state.instances = virt2_get_optimal_instance_count(ctx);

  ctx->state.conn = virConnectOpenReadOnly(ctx->conf.connection_uri);
  if (ctx->state.conn == NULL) {
    ERROR(PLUGIN_NAME " plugin: Unable to connect: "
                      "virConnectOpenReadOnly (%s) failed.",
          ctx->conf.connection_uri);
    return -1;
  }

  return virt2_setup(ctx);
}

static int virt2_shutdown(void) {
  virt2_context_t *ctx = virt2_get_default_context();

  if (ctx->state.conn != NULL)
    virConnectClose(ctx->state.conn);
  ctx->state.conn = NULL;

  return virt2_teardown(ctx);
}

void module_register(void) {
  plugin_register_config(PLUGIN_NAME, virt2_config, virt2_config_keys,
                         NR_CONFIG_KEYS);
  plugin_register_init(PLUGIN_NAME, virt2_init);
  plugin_register_shutdown(PLUGIN_NAME, virt2_shutdown);
}

/* vim: set sw=2 sts=2 et : */
