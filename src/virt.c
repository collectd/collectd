/**
 * collectd - src/virt.c
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
 *   Richard W.M. Jones <rjones@redhat.com>
 *   Przemyslaw Szczerbik <przemyslawx.szczerbik@intel.com>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_complain.h"
#include "utils_ignorelist.h"

#include <libgen.h> /* for basename(3) */
#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

/* Plugin name */
#define PLUGIN_NAME "virt"

#ifdef LIBVIR_CHECK_VERSION

#if LIBVIR_CHECK_VERSION(0, 9, 2)
#define HAVE_DOM_REASON 1
#endif

#if LIBVIR_CHECK_VERSION(0, 9, 5)
#define HAVE_BLOCK_STATS_FLAGS 1
#define HAVE_DOM_REASON_PAUSED_SHUTTING_DOWN 1
#endif

#if LIBVIR_CHECK_VERSION(0, 9, 10)
#define HAVE_DISK_ERR 1
#endif

#if LIBVIR_CHECK_VERSION(0, 9, 11)
#define HAVE_CPU_STATS 1
#define HAVE_DOM_STATE_PMSUSPENDED 1
#define HAVE_DOM_REASON_RUNNING_WAKEUP 1
#endif

#if LIBVIR_CHECK_VERSION(1, 0, 1)
#define HAVE_DOM_REASON_PAUSED_SNAPSHOT 1
#endif

#if LIBVIR_CHECK_VERSION(1, 1, 1)
#define HAVE_DOM_REASON_PAUSED_CRASHED 1
#endif

#if LIBVIR_CHECK_VERSION(1, 2, 9)
#define HAVE_JOB_STATS 1
#endif

#if LIBVIR_CHECK_VERSION(1, 2, 10)
#define HAVE_DOM_REASON_CRASHED 1
#endif

#if LIBVIR_CHECK_VERSION(1, 2, 11)
#define HAVE_FS_INFO 1
#endif

#if LIBVIR_CHECK_VERSION(1, 2, 15)
#define HAVE_DOM_REASON_PAUSED_STARTING_UP 1
#endif

#if LIBVIR_CHECK_VERSION(1, 3, 3)
#define HAVE_PERF_STATS 1
#define HAVE_DOM_REASON_POSTCOPY 1
#endif

#endif /* LIBVIR_CHECK_VERSION */

static const char *config_keys[] = {"Connection",

                                    "RefreshInterval",

                                    "Domain",
                                    "BlockDevice",
                                    "BlockDeviceFormat",
                                    "BlockDeviceFormatBasename",
                                    "InterfaceDevice",
                                    "IgnoreSelected",

                                    "HostnameFormat",
                                    "InterfaceFormat",

                                    "PluginInstanceFormat",

                                    "Instances",
                                    "ExtraStats",
                                    NULL};

const char *domain_states[] = {
        [VIR_DOMAIN_NOSTATE] = "no state",
        [VIR_DOMAIN_RUNNING] = "the domain is running",
        [VIR_DOMAIN_BLOCKED] = "the domain is blocked on resource",
        [VIR_DOMAIN_PAUSED] = "the domain is paused by user",
        [VIR_DOMAIN_SHUTDOWN] = "the domain is being shut down",
        [VIR_DOMAIN_SHUTOFF] = "the domain is shut off",
        [VIR_DOMAIN_CRASHED] = "the domain is crashed",
#ifdef HAVE_DOM_STATE_PMSUSPENDED
        [VIR_DOMAIN_PMSUSPENDED] =
            "the domain is suspended by guest power management",
#endif
};

#ifdef HAVE_DOM_REASON
#define DOMAIN_STATE_REASON_MAX_SIZE 20
const char *domain_reasons[][DOMAIN_STATE_REASON_MAX_SIZE] = {
        [VIR_DOMAIN_NOSTATE][VIR_DOMAIN_NOSTATE_UNKNOWN] =
            "the reason is unknown",

        [VIR_DOMAIN_RUNNING][VIR_DOMAIN_RUNNING_UNKNOWN] =
            "the reason is unknown",
        [VIR_DOMAIN_RUNNING][VIR_DOMAIN_RUNNING_BOOTED] =
            "normal startup from boot",
        [VIR_DOMAIN_RUNNING][VIR_DOMAIN_RUNNING_MIGRATED] =
            "migrated from another host",
        [VIR_DOMAIN_RUNNING][VIR_DOMAIN_RUNNING_RESTORED] =
            "restored from a state file",
        [VIR_DOMAIN_RUNNING][VIR_DOMAIN_RUNNING_FROM_SNAPSHOT] =
            "restored from snapshot",
        [VIR_DOMAIN_RUNNING][VIR_DOMAIN_RUNNING_UNPAUSED] =
            "returned from paused state",
        [VIR_DOMAIN_RUNNING][VIR_DOMAIN_RUNNING_MIGRATION_CANCELED] =
            "returned from migration",
        [VIR_DOMAIN_RUNNING][VIR_DOMAIN_RUNNING_SAVE_CANCELED] =
            "returned from failed save process",
#ifdef HAVE_DOM_REASON_RUNNING_WAKEUP
        [VIR_DOMAIN_RUNNING][VIR_DOMAIN_RUNNING_WAKEUP] =
            "returned from pmsuspended due to wakeup event",
#endif
#ifdef HAVE_DOM_REASON_CRASHED
        [VIR_DOMAIN_RUNNING][VIR_DOMAIN_RUNNING_CRASHED] =
            "resumed from crashed",
#endif
#ifdef HAVE_DOM_REASON_POSTCOPY
        [VIR_DOMAIN_RUNNING][VIR_DOMAIN_RUNNING_POSTCOPY] =
            "running in post-copy migration mode",
#endif

        [VIR_DOMAIN_BLOCKED][VIR_DOMAIN_BLOCKED_UNKNOWN] =
            "the reason is unknown",

        [VIR_DOMAIN_PAUSED][VIR_DOMAIN_PAUSED_UNKNOWN] =
            "the reason is unknown",
        [VIR_DOMAIN_PAUSED][VIR_DOMAIN_PAUSED_USER] = "paused on user request",
        [VIR_DOMAIN_PAUSED][VIR_DOMAIN_PAUSED_MIGRATION] =
            "paused for offline migration",
        [VIR_DOMAIN_PAUSED][VIR_DOMAIN_PAUSED_SAVE] = "paused for save",
        [VIR_DOMAIN_PAUSED][VIR_DOMAIN_PAUSED_DUMP] =
            "paused for offline core dump",
        [VIR_DOMAIN_PAUSED][VIR_DOMAIN_PAUSED_IOERROR] =
            "paused due to a disk I/O error",
        [VIR_DOMAIN_PAUSED][VIR_DOMAIN_PAUSED_WATCHDOG] =
            "paused due to a watchdog event",
        [VIR_DOMAIN_PAUSED][VIR_DOMAIN_PAUSED_FROM_SNAPSHOT] =
            "paused after restoring from snapshot",
#ifdef HAVE_DOM_REASON_PAUSED_SHUTTING_DOWN
        [VIR_DOMAIN_PAUSED][VIR_DOMAIN_PAUSED_SHUTTING_DOWN] =
            "paused during shutdown process",
#endif
#ifdef HAVE_DOM_REASON_PAUSED_SNAPSHOT
        [VIR_DOMAIN_PAUSED][VIR_DOMAIN_PAUSED_SNAPSHOT] =
            "paused while creating a snapshot",
#endif
#ifdef HAVE_DOM_REASON_PAUSED_CRASHED
        [VIR_DOMAIN_PAUSED][VIR_DOMAIN_PAUSED_CRASHED] =
            "paused due to a guest crash",
#endif
#ifdef HAVE_DOM_REASON_PAUSED_STARTING_UP
        [VIR_DOMAIN_PAUSED][VIR_DOMAIN_PAUSED_STARTING_UP] =
            "the domain is being started",
#endif
#ifdef HAVE_DOM_REASON_POSTCOPY
        [VIR_DOMAIN_PAUSED][VIR_DOMAIN_PAUSED_POSTCOPY] =
            "paused for post-copy migration",
        [VIR_DOMAIN_PAUSED][VIR_DOMAIN_PAUSED_POSTCOPY_FAILED] =
            "paused after failed post-copy",
#endif

        [VIR_DOMAIN_SHUTDOWN][VIR_DOMAIN_SHUTDOWN_UNKNOWN] =
            "the reason is unknown",
        [VIR_DOMAIN_SHUTDOWN][VIR_DOMAIN_SHUTDOWN_USER] =
            "shutting down on user request",

        [VIR_DOMAIN_SHUTOFF][VIR_DOMAIN_SHUTOFF_UNKNOWN] =
            "the reason is unknown",
        [VIR_DOMAIN_SHUTOFF][VIR_DOMAIN_SHUTOFF_SHUTDOWN] = "normal shutdown",
        [VIR_DOMAIN_SHUTOFF][VIR_DOMAIN_SHUTOFF_DESTROYED] = "forced poweroff",
        [VIR_DOMAIN_SHUTOFF][VIR_DOMAIN_SHUTOFF_CRASHED] = "domain crashed",
        [VIR_DOMAIN_SHUTOFF][VIR_DOMAIN_SHUTOFF_MIGRATED] =
            "migrated to another host",
        [VIR_DOMAIN_SHUTOFF][VIR_DOMAIN_SHUTOFF_SAVED] = "saved to a file",
        [VIR_DOMAIN_SHUTOFF][VIR_DOMAIN_SHUTOFF_FAILED] =
            "domain failed to start",
        [VIR_DOMAIN_SHUTOFF][VIR_DOMAIN_SHUTOFF_FROM_SNAPSHOT] =
            "restored from a snapshot which was taken while domain was shutoff",

        [VIR_DOMAIN_CRASHED][VIR_DOMAIN_CRASHED_UNKNOWN] =
            "the reason is unknown",
#ifdef VIR_DOMAIN_CRASHED_PANICKED
        [VIR_DOMAIN_CRASHED][VIR_DOMAIN_CRASHED_PANICKED] = "domain panicked",
#endif

#ifdef HAVE_DOM_STATE_PMSUSPENDED
        [VIR_DOMAIN_PMSUSPENDED][VIR_DOMAIN_PMSUSPENDED_UNKNOWN] =
            "the reason is unknown",
#endif
};
#endif /* HAVE_DOM_REASON */

#define NR_CONFIG_KEYS ((sizeof config_keys / sizeof config_keys[0]) - 1)
#define NANOSEC_IN_SEC 1e9

#define GET_STATS(_f, _name, ...)                                              \
  do {                                                                         \
    status = _f(__VA_ARGS__);                                                  \
    if (status != 0)                                                           \
      ERROR(PLUGIN_NAME ": Failed to get " _name);                             \
  } while (0)

/* Connection. */
static virConnectPtr conn = 0;
static char *conn_string = NULL;
static c_complain_t conn_complain = C_COMPLAIN_INIT_STATIC;

/* Node information required for %CPU */
static virNodeInfo nodeinfo;

/* Seconds between list refreshes, 0 disables completely. */
static int interval = 60;

/* List of domains, if specified. */
static ignorelist_t *il_domains = NULL;
/* List of block devices, if specified. */
static ignorelist_t *il_block_devices = NULL;
/* List of network interface devices, if specified. */
static ignorelist_t *il_interface_devices = NULL;

static int ignore_device_match(ignorelist_t *, const char *domname,
                               const char *devpath);

/* Actual list of block devices found on last refresh. */
struct block_device {
  virDomainPtr dom; /* domain */
  char *path;       /* name of block device */
};

/* Actual list of network interfaces found on last refresh. */
struct interface_device {
  virDomainPtr dom; /* domain */
  char *path;       /* name of interface device */
  char *address;    /* mac address of interface device */
  char *number;     /* interface device number */
};

typedef struct domain_s {
  virDomainPtr ptr;
  virDomainInfo info;
} domain_t;

struct lv_read_state {
  /* Actual list of domains found on last refresh. */
  domain_t *domains;
  int nr_domains;

  struct block_device *block_devices;
  int nr_block_devices;

  struct interface_device *interface_devices;
  int nr_interface_devices;
};

static void free_domains(struct lv_read_state *state);
static int add_domain(struct lv_read_state *state, virDomainPtr dom);

static void free_block_devices(struct lv_read_state *state);
static int add_block_device(struct lv_read_state *state, virDomainPtr dom,
                            const char *path);

static void free_interface_devices(struct lv_read_state *state);
static int add_interface_device(struct lv_read_state *state, virDomainPtr dom,
                                const char *path, const char *address,
                                unsigned int number);

#define METADATA_VM_PARTITION_URI "http://ovirt.org/ovirtmap/tag/1.0"
#define METADATA_VM_PARTITION_ELEMENT "tag"
#define METADATA_VM_PARTITION_PREFIX "ovirtmap"

#define BUFFER_MAX_LEN 256
#define PARTITION_TAG_MAX_LEN 32

struct lv_read_instance {
  struct lv_read_state read_state;
  char tag[PARTITION_TAG_MAX_LEN];
  size_t id;
};

struct lv_user_data {
  struct lv_read_instance inst;
  user_data_t ud;
};

#define NR_INSTANCES_DEFAULT 1
#define NR_INSTANCES_MAX 128
static int nr_instances = NR_INSTANCES_DEFAULT;
static struct lv_user_data lv_read_user_data[NR_INSTANCES_MAX];

/* HostnameFormat. */
#define HF_MAX_FIELDS 3

enum hf_field { hf_none = 0, hf_hostname, hf_name, hf_uuid };

static enum hf_field hostname_format[HF_MAX_FIELDS] = {hf_name};

/* PluginInstanceFormat */
#define PLGINST_MAX_FIELDS 2

enum plginst_field { plginst_none = 0, plginst_name, plginst_uuid };

static enum plginst_field plugin_instance_format[PLGINST_MAX_FIELDS] = {
    plginst_none};

/* BlockDeviceFormat */
enum bd_field { target, source };

/* InterfaceFormat. */
enum if_field { if_address, if_name, if_number };

/* ExtraStats */
#define EX_STATS_MAX_FIELDS 15
enum ex_stats {
  ex_stats_none = 0,
  ex_stats_disk = 1 << 0,
  ex_stats_pcpu = 1 << 1,
  ex_stats_cpu_util = 1 << 2,
  ex_stats_domain_state = 1 << 3,
#ifdef HAVE_PERF_STATS
  ex_stats_perf = 1 << 4,
#endif
  ex_stats_vcpupin = 1 << 5,
#ifdef HAVE_DISK_ERR
  ex_stats_disk_err = 1 << 6,
#endif
#ifdef HAVE_FS_INFO
  ex_stats_fs_info = 1 << 7,
#endif
#ifdef HAVE_JOB_STATS
  ex_stats_job_stats_completed = 1 << 8,
  ex_stats_job_stats_background = 1 << 9,
#endif
};

static unsigned int extra_stats = ex_stats_none;

struct ex_stats_item {
  const char *name;
  enum ex_stats flag;
};
static const struct ex_stats_item ex_stats_table[] = {
    {"disk", ex_stats_disk},
    {"pcpu", ex_stats_pcpu},
    {"cpu_util", ex_stats_cpu_util},
    {"domain_state", ex_stats_domain_state},
#ifdef HAVE_PERF_STATS
    {"perf", ex_stats_perf},
#endif
    {"vcpupin", ex_stats_vcpupin},
#ifdef HAVE_DISK_ERR
    {"disk_err", ex_stats_disk_err},
#endif
#ifdef HAVE_FS_INFO
    {"fs_info", ex_stats_fs_info},
#endif
#ifdef HAVE_JOB_STATS
    {"job_stats_completed", ex_stats_job_stats_completed},
    {"job_stats_background", ex_stats_job_stats_background},
#endif
    {NULL, ex_stats_none},
};

/* BlockDeviceFormatBasename */
_Bool blockdevice_format_basename = 0;
static enum bd_field blockdevice_format = target;
static enum if_field interface_format = if_name;

/* Time that we last refreshed. */
static time_t last_refresh = (time_t)0;

static int refresh_lists(struct lv_read_instance *inst);

struct lv_info {
  virDomainInfo di;
  unsigned long long total_user_cpu_time;
  unsigned long long total_syst_cpu_time;
};

struct lv_block_info {
  virDomainBlockStatsStruct bi;

  long long rd_total_times;
  long long wr_total_times;

  long long fl_req;
  long long fl_total_times;
};

static void init_block_info(struct lv_block_info *binfo) {
  if (binfo == NULL)
    return;

  binfo->bi.rd_req = -1;
  binfo->bi.wr_req = -1;
  binfo->bi.rd_bytes = -1;
  binfo->bi.wr_bytes = -1;

  binfo->rd_total_times = -1;
  binfo->wr_total_times = -1;
  binfo->fl_req = -1;
  binfo->fl_total_times = -1;
}

#ifdef HAVE_BLOCK_STATS_FLAGS

#define GET_BLOCK_INFO_VALUE(NAME, FIELD)                                      \
  if (!strcmp(param[i].field, NAME)) {                                         \
    binfo->FIELD = param[i].value.l;                                           \
    continue;                                                                  \
  }

static int get_block_info(struct lv_block_info *binfo,
                          virTypedParameterPtr param, int nparams) {
  if (binfo == NULL || param == NULL)
    return -1;

  for (int i = 0; i < nparams; ++i) {
    /* ignore type. Everything must be LLONG anyway. */
    GET_BLOCK_INFO_VALUE("rd_operations", bi.rd_req);
    GET_BLOCK_INFO_VALUE("wr_operations", bi.wr_req);
    GET_BLOCK_INFO_VALUE("rd_bytes", bi.rd_bytes);
    GET_BLOCK_INFO_VALUE("wr_bytes", bi.wr_bytes);
    GET_BLOCK_INFO_VALUE("rd_total_times", rd_total_times);
    GET_BLOCK_INFO_VALUE("wr_total_times", wr_total_times);
    GET_BLOCK_INFO_VALUE("flush_operations", fl_req);
    GET_BLOCK_INFO_VALUE("flush_total_times", fl_total_times);
  }

  return 0;
}

#undef GET_BLOCK_INFO_VALUE

#endif /* HAVE_BLOCK_STATS_FLAGS */

/* ERROR(...) macro for virterrors. */
#define VIRT_ERROR(conn, s)                                                    \
  do {                                                                         \
    virErrorPtr err;                                                           \
    err = (conn) ? virConnGetLastError((conn)) : virGetLastError();            \
    if (err)                                                                   \
      ERROR("%s: %s", (s), err->message);                                      \
  } while (0)

static void init_lv_info(struct lv_info *info) {
  if (info != NULL)
    memset(info, 0, sizeof(*info));
}

static int lv_domain_info(virDomainPtr dom, struct lv_info *info) {
#ifdef HAVE_CPU_STATS
  virTypedParameterPtr param = NULL;
  int nparams = 0;
#endif /* HAVE_CPU_STATS */
  int ret = virDomainGetInfo(dom, &(info->di));
  if (ret != 0) {
    return ret;
  }

#ifdef HAVE_CPU_STATS
  nparams = virDomainGetCPUStats(dom, NULL, 0, -1, 1, 0);
  if (nparams < 0) {
    VIRT_ERROR(conn, "getting the CPU params count");
    return -1;
  }

  param = calloc(nparams, sizeof(virTypedParameter));
  if (param == NULL) {
    ERROR("virt plugin: alloc(%i) for cpu parameters failed.", nparams);
    return -1;
  }

  ret = virDomainGetCPUStats(dom, param, nparams, -1, 1, 0); // total stats.
  if (ret < 0) {
    virTypedParamsClear(param, nparams);
    sfree(param);
    VIRT_ERROR(conn, "getting the disk params values");
    return -1;
  }

  for (int i = 0; i < nparams; ++i) {
    if (!strcmp(param[i].field, "user_time"))
      info->total_user_cpu_time = param[i].value.ul;
    else if (!strcmp(param[i].field, "system_time"))
      info->total_syst_cpu_time = param[i].value.ul;
  }

  virTypedParamsClear(param, nparams);
  sfree(param);
#endif /* HAVE_CPU_STATS */

  return 0;
}

static void init_value_list(value_list_t *vl, virDomainPtr dom) {
  int n;
  const char *name;
  char uuid[VIR_UUID_STRING_BUFLEN];

  sstrncpy(vl->plugin, PLUGIN_NAME, sizeof(vl->plugin));

  vl->host[0] = '\0';

  /* Construct the hostname field according to HostnameFormat. */
  for (int i = 0; i < HF_MAX_FIELDS; ++i) {
    if (hostname_format[i] == hf_none)
      continue;

    n = DATA_MAX_NAME_LEN - strlen(vl->host) - 2;

    if (i > 0 && n >= 1) {
      strncat(vl->host, ":", 1);
      n--;
    }

    switch (hostname_format[i]) {
    case hf_none:
      break;
    case hf_hostname:
      strncat(vl->host, hostname_g, n);
      break;
    case hf_name:
      name = virDomainGetName(dom);
      if (name)
        strncat(vl->host, name, n);
      break;
    case hf_uuid:
      if (virDomainGetUUIDString(dom, uuid) == 0)
        strncat(vl->host, uuid, n);
      break;
    }
  }

  vl->host[sizeof(vl->host) - 1] = '\0';

  /* Construct the plugin instance field according to PluginInstanceFormat. */
  for (int i = 0; i < PLGINST_MAX_FIELDS; ++i) {
    if (plugin_instance_format[i] == plginst_none)
      continue;

    n = sizeof(vl->plugin_instance) - strlen(vl->plugin_instance) - 2;

    if (i > 0 && n >= 1) {
      strncat(vl->plugin_instance, ":", 1);
      n--;
    }

    switch (plugin_instance_format[i]) {
    case plginst_none:
      break;
    case plginst_name:
      name = virDomainGetName(dom);
      if (name)
        strncat(vl->plugin_instance, name, n);
      break;
    case plginst_uuid:
      if (virDomainGetUUIDString(dom, uuid) == 0)
        strncat(vl->plugin_instance, uuid, n);
      break;
    }
  }

  vl->plugin_instance[sizeof(vl->plugin_instance) - 1] = '\0';

} /* void init_value_list */

static int init_notif(notification_t *notif, const virDomainPtr domain,
                      int severity, const char *msg, const char *type,
                      const char *type_instance) {
  value_list_t vl = VALUE_LIST_INIT;

  if (!notif) {
    ERROR(PLUGIN_NAME ": init_notif: NULL pointer");
    return -1;
  }

  init_value_list(&vl, domain);
  notification_init(notif, severity, msg, vl.host, vl.plugin,
                    vl.plugin_instance, type, type_instance);
  notif->time = cdtime();
  return 0;
}

static void submit_notif(const virDomainPtr domain, int severity,
                         const char *msg, const char *type,
                         const char *type_instance) {
  notification_t notif;

  init_notif(&notif, domain, severity, msg, type, type_instance);
  plugin_dispatch_notification(&notif);
  if (notif.meta)
    plugin_notification_meta_free(notif.meta);
}

static void submit(virDomainPtr dom, char const *type,
                   char const *type_instance, value_t *values,
                   size_t values_len) {
  value_list_t vl = VALUE_LIST_INIT;
  init_value_list(&vl, dom);

  vl.values = values;
  vl.values_len = values_len;

  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_instance != NULL)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static void memory_submit(virDomainPtr dom, gauge_t value) {
  submit(dom, "memory", "total", &(value_t){.gauge = value}, 1);
}

static void memory_stats_submit(gauge_t value, virDomainPtr dom,
                                int tag_index) {
  static const char *tags[] = {"swap_in",        "swap_out", "major_fault",
                               "minor_fault",    "unused",   "available",
                               "actual_balloon", "rss",      "usable",
                               "last_update"};

  if ((tag_index < 0) || (tag_index >= (int)STATIC_ARRAY_SIZE(tags))) {
    ERROR("virt plugin: Array index out of bounds: tag_index = %d", tag_index);
    return;
  }

  submit(dom, "memory", tags[tag_index], &(value_t){.gauge = value}, 1);
}

static void submit_derive2(const char *type, derive_t v0, derive_t v1,
                           virDomainPtr dom, const char *devname) {
  value_t values[] = {
      {.derive = v0}, {.derive = v1},
  };

  submit(dom, type, devname, values, STATIC_ARRAY_SIZE(values));
} /* void submit_derive2 */

static void pcpu_submit(virDomainPtr dom, struct lv_info *info) {
#ifdef HAVE_CPU_STATS
  if (extra_stats & ex_stats_pcpu)
    submit_derive2("ps_cputime", info->total_user_cpu_time,
                   info->total_syst_cpu_time, dom, NULL);
#endif /* HAVE_CPU_STATS */
}

static double cpu_ns_to_percent(unsigned int node_cpus,
                                unsigned long long cpu_time_old,
                                unsigned long long cpu_time_new) {
  double percent = 0.0;
  unsigned long long cpu_time_diff = 0;
  double time_diff_sec = CDTIME_T_TO_DOUBLE(plugin_get_interval());

  if (node_cpus != 0 && time_diff_sec != 0 && cpu_time_old != 0) {
    cpu_time_diff = cpu_time_new - cpu_time_old;
    percent = ((double)(100 * cpu_time_diff)) /
              (time_diff_sec * node_cpus * NANOSEC_IN_SEC);
  }

  DEBUG(PLUGIN_NAME ": node_cpus=%u cpu_time_old=%" PRIu64
                    " cpu_time_new=%" PRIu64 "cpu_time_diff=%" PRIu64
                    " time_diff_sec=%f percent=%f",
        node_cpus, (uint64_t)cpu_time_old, (uint64_t)cpu_time_new,
        (uint64_t)cpu_time_diff, time_diff_sec, percent);

  return percent;
}

static void cpu_submit(const domain_t *dom, unsigned long long cpuTime_new) {

  if (!dom)
    return;

  if (extra_stats & ex_stats_cpu_util) {
    /* Computing %CPU requires 2 samples of cpuTime */
    if (dom->info.cpuTime != 0 && cpuTime_new != 0) {

      submit(dom->ptr, "percent", "virt_cpu_total",
             &(value_t){.gauge = cpu_ns_to_percent(
                            nodeinfo.cpus, dom->info.cpuTime, cpuTime_new)},
             1);
    }
  }

  submit(dom->ptr, "virt_cpu_total", NULL, &(value_t){.derive = cpuTime_new},
         1);
}

static void vcpu_submit(derive_t value, virDomainPtr dom, int vcpu_nr,
                        const char *type) {
  char type_instance[DATA_MAX_NAME_LEN];

  snprintf(type_instance, sizeof(type_instance), "%d", vcpu_nr);
  submit(dom, type, type_instance, &(value_t){.derive = value}, 1);
}

static void disk_submit(struct lv_block_info *binfo, virDomainPtr dom,
                        const char *dev) {
  char *dev_copy = strdup(dev);
  const char *type_instance = dev_copy;

  if (!dev_copy)
    return;

  if (blockdevice_format_basename && blockdevice_format == source)
    type_instance = basename(dev_copy);

  if (!type_instance) {
    sfree(dev_copy);
    return;
  }

  char flush_type_instance[DATA_MAX_NAME_LEN];
  snprintf(flush_type_instance, sizeof(flush_type_instance), "flush-%s",
           type_instance);

  if ((binfo->bi.rd_req != -1) && (binfo->bi.wr_req != -1))
    submit_derive2("disk_ops", (derive_t)binfo->bi.rd_req,
                   (derive_t)binfo->bi.wr_req, dom, type_instance);

  if ((binfo->bi.rd_bytes != -1) && (binfo->bi.wr_bytes != -1))
    submit_derive2("disk_octets", (derive_t)binfo->bi.rd_bytes,
                   (derive_t)binfo->bi.wr_bytes, dom, type_instance);

  if (extra_stats & ex_stats_disk) {
    if ((binfo->rd_total_times != -1) && (binfo->wr_total_times != -1))
      submit_derive2("disk_time", (derive_t)binfo->rd_total_times,
                     (derive_t)binfo->wr_total_times, dom, type_instance);

    if (binfo->fl_req != -1)
      submit(dom, "total_requests", flush_type_instance,
             &(value_t){.derive = (derive_t)binfo->fl_req}, 1);
    if (binfo->fl_total_times != -1) {
      derive_t value = binfo->fl_total_times / 1000; // ns -> ms
      submit(dom, "total_time_in_ms", flush_type_instance,
             &(value_t){.derive = value}, 1);
    }
  }

  sfree(dev_copy);
}

static unsigned int parse_ex_stats_flags(char **exstats, int numexstats) {
  unsigned int ex_stats_flags = ex_stats_none;
  for (int i = 0; i < numexstats; i++) {
    for (int j = 0; ex_stats_table[j].name != NULL; j++) {
      if (strcasecmp(exstats[i], ex_stats_table[j].name) == 0) {
        DEBUG(PLUGIN_NAME " plugin: enabling extra stats for '%s'",
              ex_stats_table[j].name);
        ex_stats_flags |= ex_stats_table[j].flag;
        break;
      }

      if (ex_stats_table[j + 1].name == NULL) {
        ERROR(PLUGIN_NAME ": Unmatched ExtraStats option: %s", exstats[i]);
      }
    }
  }
  return ex_stats_flags;
}

static void domain_state_submit(virDomainPtr dom, int state, int reason) {

  if ((state < 0) || (state >= STATIC_ARRAY_SIZE(domain_states))) {
    ERROR(PLUGIN_NAME ": Array index out of bounds: state=%d", state);
    return;
  }

  char msg[DATA_MAX_NAME_LEN];
  const char *state_str = domain_states[state];
#ifdef HAVE_DOM_REASON
  if ((reason < 0) || (reason >= STATIC_ARRAY_SIZE(domain_reasons[0]))) {
    ERROR(PLUGIN_NAME ": Array index out of bounds: reason=%d", reason);
    return;
  }

  const char *reason_str = domain_reasons[state][reason];
  /* Array size for domain reasons is fixed, but different domain states can
   * have different number of reasons. We need to check if reason was
   * successfully parsed */
  if (!reason_str) {
    ERROR(PLUGIN_NAME ": Invalid reason (%d) for domain state: %s", reason,
          state_str);
    return;
  }
#else
  const char *reason_str = "N/A";
#endif

  snprintf(msg, sizeof(msg), "Domain state: %s. Reason: %s", state_str,
           reason_str);

  int severity;
  switch (state) {
  case VIR_DOMAIN_NOSTATE:
  case VIR_DOMAIN_RUNNING:
  case VIR_DOMAIN_SHUTDOWN:
  case VIR_DOMAIN_SHUTOFF:
    severity = NOTIF_OKAY;
    break;
  case VIR_DOMAIN_BLOCKED:
  case VIR_DOMAIN_PAUSED:
#ifdef DOM_STATE_PMSUSPENDED
  case VIR_DOMAIN_PMSUSPENDED:
#endif
    severity = NOTIF_WARNING;
    break;
  case VIR_DOMAIN_CRASHED:
    severity = NOTIF_FAILURE;
    break;
  default:
    ERROR(PLUGIN_NAME ": Unrecognized domain state (%d)", state);
    return;
  }
  submit_notif(dom, severity, msg, "domain_state", NULL);
}

static int lv_config(const char *key, const char *value) {
  if (virInitialize() != 0)
    return 1;

  if (il_domains == NULL)
    il_domains = ignorelist_create(1);
  if (il_block_devices == NULL)
    il_block_devices = ignorelist_create(1);
  if (il_interface_devices == NULL)
    il_interface_devices = ignorelist_create(1);

  if (strcasecmp(key, "Connection") == 0) {
    char *tmp = strdup(value);
    if (tmp == NULL) {
      ERROR(PLUGIN_NAME " plugin: Connection strdup failed.");
      return 1;
    }
    sfree(conn_string);
    conn_string = tmp;
    return 0;
  }

  if (strcasecmp(key, "RefreshInterval") == 0) {
    char *eptr = NULL;
    interval = strtol(value, &eptr, 10);
    if (eptr == NULL || *eptr != '\0')
      return 1;
    return 0;
  }

  if (strcasecmp(key, "Domain") == 0) {
    if (ignorelist_add(il_domains, value))
      return 1;
    return 0;
  }
  if (strcasecmp(key, "BlockDevice") == 0) {
    if (ignorelist_add(il_block_devices, value))
      return 1;
    return 0;
  }

  if (strcasecmp(key, "BlockDeviceFormat") == 0) {
    if (strcasecmp(value, "target") == 0)
      blockdevice_format = target;
    else if (strcasecmp(value, "source") == 0)
      blockdevice_format = source;
    else {
      ERROR(PLUGIN_NAME " plugin: unknown BlockDeviceFormat: %s", value);
      return -1;
    }
    return 0;
  }
  if (strcasecmp(key, "BlockDeviceFormatBasename") == 0) {
    blockdevice_format_basename = IS_TRUE(value);
    return 0;
  }
  if (strcasecmp(key, "InterfaceDevice") == 0) {
    if (ignorelist_add(il_interface_devices, value))
      return 1;
    return 0;
  }

  if (strcasecmp(key, "IgnoreSelected") == 0) {
    if (IS_TRUE(value)) {
      ignorelist_set_invert(il_domains, 0);
      ignorelist_set_invert(il_block_devices, 0);
      ignorelist_set_invert(il_interface_devices, 0);
    } else {
      ignorelist_set_invert(il_domains, 1);
      ignorelist_set_invert(il_block_devices, 1);
      ignorelist_set_invert(il_interface_devices, 1);
    }
    return 0;
  }

  if (strcasecmp(key, "HostnameFormat") == 0) {
    char *value_copy;
    char *fields[HF_MAX_FIELDS];
    int n;

    value_copy = strdup(value);
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

    for (int i = 0; i < n; ++i) {
      if (strcasecmp(fields[i], "hostname") == 0)
        hostname_format[i] = hf_hostname;
      else if (strcasecmp(fields[i], "name") == 0)
        hostname_format[i] = hf_name;
      else if (strcasecmp(fields[i], "uuid") == 0)
        hostname_format[i] = hf_uuid;
      else {
        ERROR(PLUGIN_NAME " plugin: unknown HostnameFormat field: %s",
              fields[i]);
        sfree(value_copy);
        return -1;
      }
    }
    sfree(value_copy);

    for (int i = n; i < HF_MAX_FIELDS; ++i)
      hostname_format[i] = hf_none;

    return 0;
  }

  if (strcasecmp(key, "PluginInstanceFormat") == 0) {
    char *value_copy;
    char *fields[PLGINST_MAX_FIELDS];
    int n;

    value_copy = strdup(value);
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

    for (int i = 0; i < n; ++i) {
      if (strcasecmp(fields[i], "none") == 0) {
        plugin_instance_format[i] = plginst_none;
        break;
      } else if (strcasecmp(fields[i], "name") == 0)
        plugin_instance_format[i] = plginst_name;
      else if (strcasecmp(fields[i], "uuid") == 0)
        plugin_instance_format[i] = plginst_uuid;
      else {
        ERROR(PLUGIN_NAME " plugin: unknown PluginInstanceFormat field: %s",
              fields[i]);
        sfree(value_copy);
        return -1;
      }
    }
    sfree(value_copy);

    for (int i = n; i < PLGINST_MAX_FIELDS; ++i)
      plugin_instance_format[i] = plginst_none;

    return 0;
  }

  if (strcasecmp(key, "InterfaceFormat") == 0) {
    if (strcasecmp(value, "name") == 0)
      interface_format = if_name;
    else if (strcasecmp(value, "address") == 0)
      interface_format = if_address;
    else if (strcasecmp(value, "number") == 0)
      interface_format = if_number;
    else {
      ERROR(PLUGIN_NAME " plugin: unknown InterfaceFormat: %s", value);
      return -1;
    }
    return 0;
  }

  if (strcasecmp(key, "Instances") == 0) {
    char *eptr = NULL;
    double val = strtod(value, &eptr);

    if (*eptr != '\0') {
      ERROR(PLUGIN_NAME " plugin: Invalid value for Instances = '%s'", value);
      return 1;
    }
    if (val <= 0) {
      ERROR(PLUGIN_NAME " plugin: Instances <= 0 makes no sense.");
      return 1;
    }
    if (val > NR_INSTANCES_MAX) {
      ERROR(PLUGIN_NAME " plugin: Instances=%f > NR_INSTANCES_MAX=%i"
                        " use a lower setting or recompile the plugin.",
            val, NR_INSTANCES_MAX);
      return 1;
    }

    nr_instances = (int)val;
    DEBUG(PLUGIN_NAME " plugin: configured %i instances", nr_instances);
    return 0;
  }

  if (strcasecmp(key, "ExtraStats") == 0) {
    char *localvalue = strdup(value);
    if (localvalue != NULL) {
      char *exstats[EX_STATS_MAX_FIELDS];
      int numexstats =
          strsplit(localvalue, exstats, STATIC_ARRAY_SIZE(exstats));
      extra_stats = parse_ex_stats_flags(exstats, numexstats);
      sfree(localvalue);

#ifdef HAVE_JOB_STATS
      if ((extra_stats & ex_stats_job_stats_completed) &&
          (extra_stats & ex_stats_job_stats_background)) {
        ERROR(PLUGIN_NAME " plugin: Invalid job stats configuration. Only one "
                          "type of job statistics can be collected at the same "
                          "time");
        return 1;
      }
#endif
    }
  }

  /* Unrecognised option. */
  return -1;
}

static int lv_connect(void) {
  if (conn == NULL) {
/* `conn_string == NULL' is acceptable */
#ifdef HAVE_FS_INFO
    /* virDomainGetFSInfo requires full read-write access connection */
    if (extra_stats & ex_stats_fs_info)
      conn = virConnectOpen(conn_string);
    else
#endif
      conn = virConnectOpenReadOnly(conn_string);
    if (conn == NULL) {
      c_complain(LOG_ERR, &conn_complain,
                 PLUGIN_NAME " plugin: Unable to connect: "
                             "virConnectOpen failed.");
      return -1;
    }
    int status = virNodeGetInfo(conn, &nodeinfo);
    if (status != 0) {
      ERROR(PLUGIN_NAME ": virNodeGetInfo failed");
      return -1;
    }
  }
  c_release(LOG_NOTICE, &conn_complain,
            PLUGIN_NAME " plugin: Connection established.");
  return 0;
}

static void lv_disconnect(void) {
  if (conn != NULL)
    virConnectClose(conn);
  conn = NULL;
  WARNING(PLUGIN_NAME " plugin: closed connection to libvirt");
}

static int lv_domain_block_info(virDomainPtr dom, const char *path,
                                struct lv_block_info *binfo) {
#ifdef HAVE_BLOCK_STATS_FLAGS
  int nparams = 0;
  if (virDomainBlockStatsFlags(dom, path, NULL, &nparams, 0) < 0 ||
      nparams <= 0) {
    VIRT_ERROR(conn, "getting the disk params count");
    return -1;
  }

  virTypedParameterPtr params = calloc((size_t)nparams, sizeof(*params));
  if (params == NULL) {
    ERROR("virt plugin: alloc(%i) for block=%s parameters failed.", nparams,
          path);
    return -1;
  }

  int rc = -1;
  if (virDomainBlockStatsFlags(dom, path, params, &nparams, 0) < 0) {
    VIRT_ERROR(conn, "getting the disk params values");
  } else {
    rc = get_block_info(binfo, params, nparams);
  }

  virTypedParamsClear(params, nparams);
  sfree(params);
  return rc;
#else
  return virDomainBlockStats(dom, path, &(binfo->bi), sizeof(binfo->bi));
#endif /* HAVE_BLOCK_STATS_FLAGS */
}

#ifdef HAVE_PERF_STATS
static void perf_submit(virDomainStatsRecordPtr stats) {
  for (int i = 0; i < stats->nparams; ++i) {
    /* Replace '.' with '_' in event field to match other metrics' naming
     * convention */
    char *c = strchr(stats->params[i].field, '.');
    if (c)
      *c = '_';
    submit(stats->dom, "perf", stats->params[i].field,
           &(value_t){.derive = stats->params[i].value.ul}, 1);
  }
}

static int get_perf_events(virDomainPtr domain) {
  virDomainStatsRecordPtr *stats = NULL;
  /* virDomainListGetStats requires a NULL terminated list of domains */
  virDomainPtr domain_array[] = {domain, NULL};

  int status =
      virDomainListGetStats(domain_array, VIR_DOMAIN_STATS_PERF, &stats, 0);
  if (status == -1) {
    ERROR("virt plugin: virDomainListGetStats failed with status %i.", status);
    return status;
  }

  for (int i = 0; i < status; ++i)
    perf_submit(stats[i]);

  virDomainStatsRecordListFree(stats);
  return 0;
}
#endif /* HAVE_PERF_STATS */

static void vcpu_pin_submit(virDomainPtr dom, int max_cpus, int vcpu,
                            unsigned char *cpu_maps, int cpu_map_len) {
  for (int cpu = 0; cpu < max_cpus; ++cpu) {
    char type_instance[DATA_MAX_NAME_LEN];
    _Bool is_set = VIR_CPU_USABLE(cpu_maps, cpu_map_len, vcpu, cpu) ? 1 : 0;

    snprintf(type_instance, sizeof(type_instance), "vcpu_%d-cpu_%d", vcpu, cpu);
    submit(dom, "cpu_affinity", type_instance, &(value_t){.gauge = is_set}, 1);
  }
}

static int get_vcpu_stats(virDomainPtr domain, unsigned short nr_virt_cpu) {
  int max_cpus = VIR_NODEINFO_MAXCPUS(nodeinfo);
  int cpu_map_len = VIR_CPU_MAPLEN(max_cpus);

  virVcpuInfoPtr vinfo = calloc(nr_virt_cpu, sizeof(vinfo[0]));
  if (vinfo == NULL) {
    ERROR(PLUGIN_NAME " plugin: malloc failed.");
    return -1;
  }

  unsigned char *cpumaps = calloc(nr_virt_cpu, cpu_map_len);
  if (cpumaps == NULL) {
    ERROR(PLUGIN_NAME " plugin: malloc failed.");
    sfree(vinfo);
    return -1;
  }

  int status =
      virDomainGetVcpus(domain, vinfo, nr_virt_cpu, cpumaps, cpu_map_len);
  if (status < 0) {
    ERROR(PLUGIN_NAME " plugin: virDomainGetVcpus failed with status %i.",
          status);
    sfree(cpumaps);
    sfree(vinfo);
    return status;
  }

  for (int i = 0; i < nr_virt_cpu; ++i) {
    vcpu_submit(vinfo[i].cpuTime, domain, vinfo[i].number, "virt_vcpu");
    if (extra_stats & ex_stats_vcpupin)
      vcpu_pin_submit(domain, max_cpus, i, cpumaps, cpu_map_len);
  }

  sfree(cpumaps);
  sfree(vinfo);
  return 0;
}

#ifdef HAVE_DOM_REASON
static int get_domain_state(virDomainPtr domain) {
  int domain_state = 0;
  int domain_reason = 0;

  int status = virDomainGetState(domain, &domain_state, &domain_reason, 0);
  if (status != 0) {
    ERROR(PLUGIN_NAME " plugin: virDomainGetState failed with status %i.",
          status);
    return status;
  }

  domain_state_submit(domain, domain_state, domain_reason);
  return status;
}
#endif /* HAVE_DOM_REASON */

static int get_memory_stats(virDomainPtr domain) {
  virDomainMemoryStatPtr minfo =
      calloc(VIR_DOMAIN_MEMORY_STAT_NR, sizeof(virDomainMemoryStatStruct));
  if (minfo == NULL) {
    ERROR("virt plugin: malloc failed.");
    return -1;
  }

  int mem_stats =
      virDomainMemoryStats(domain, minfo, VIR_DOMAIN_MEMORY_STAT_NR, 0);
  if (mem_stats < 0) {
    ERROR("virt plugin: virDomainMemoryStats failed with mem_stats %i.",
          mem_stats);
    sfree(minfo);
    return mem_stats;
  }

  for (int i = 0; i < mem_stats; i++)
    memory_stats_submit((gauge_t)minfo[i].val * 1024, domain, minfo[i].tag);

  sfree(minfo);
  return 0;
}

#ifdef HAVE_DISK_ERR
static void disk_err_submit(virDomainPtr domain,
                            virDomainDiskErrorPtr disk_err) {
  submit(domain, "disk_error", disk_err->disk,
         &(value_t){.gauge = disk_err->error}, 1);
}

static int get_disk_err(virDomainPtr domain) {
  /* Get preferred size of disk errors array */
  int disk_err_count = virDomainGetDiskErrors(domain, NULL, 0, 0);
  if (disk_err_count == -1) {
    ERROR(PLUGIN_NAME
          " plugin: failed to get preferred size of disk errors array");
    return -1;
  }

  DEBUG(PLUGIN_NAME
        " plugin: preferred size of disk errors array: %d for domain %s",
        disk_err_count, virDomainGetName(domain));
  virDomainDiskError disk_err[disk_err_count];

  disk_err_count = virDomainGetDiskErrors(domain, disk_err, disk_err_count, 0);
  if (disk_err_count == -1) {
    ERROR(PLUGIN_NAME " plugin: virDomainGetDiskErrors failed with status %d",
          disk_err_count);
    return -1;
  }

  DEBUG(PLUGIN_NAME " plugin: detected %d disk errors in domain %s",
        disk_err_count, virDomainGetName(domain));

  for (int i = 0; i < disk_err_count; ++i) {
    disk_err_submit(domain, &disk_err[i]);
    sfree(disk_err[i].disk);
  }

  return 0;
}
#endif /* HAVE_DISK_ERR */

static int get_block_stats(struct block_device *block_dev) {

  if (!block_dev) {
    ERROR(PLUGIN_NAME " plugin: get_block_stats NULL pointer");
    return -1;
  }

  struct lv_block_info binfo;
  init_block_info(&binfo);

  if (lv_domain_block_info(block_dev->dom, block_dev->path, &binfo) < 0) {
    ERROR(PLUGIN_NAME " plugin: lv_domain_block_info failed");
    return -1;
  }

  disk_submit(&binfo, block_dev->dom, block_dev->path);
  return 0;
}

#ifdef HAVE_FS_INFO

#define NM_ADD_ITEM(_fun, _name, _val)                                         \
  do {                                                                         \
    ret = _fun(&notif, _name, _val);                                           \
    if (ret != 0) {                                                            \
      ERROR(PLUGIN_NAME " plugin: failed to add notification metadata");       \
      goto cleanup;                                                            \
    }                                                                          \
  } while (0)

#define NM_ADD_STR_ITEMS(_items, _size)                                        \
  do {                                                                         \
    for (int _i = 0; _i < _size; ++_i) {                                       \
      DEBUG(PLUGIN_NAME                                                        \
            " plugin: Adding notification metadata name=%s value=%s",          \
            _items[_i].name, _items[_i].value);                                \
      NM_ADD_ITEM(plugin_notification_meta_add_string, _items[_i].name,        \
                  _items[_i].value);                                           \
    }                                                                          \
  } while (0)

static int fs_info_notify(virDomainPtr domain, virDomainFSInfoPtr fs_info) {
  notification_t notif;
  int ret = 0;

  /* Local struct, just for the purpose of this function. */
  typedef struct nm_str_item_s {
    const char *name;
    const char *value;
  } nm_str_item_t;

  nm_str_item_t fs_dev_alias[fs_info->ndevAlias];
  nm_str_item_t fs_str_items[] = {
      {.name = "mountpoint", .value = fs_info->mountpoint},
      {.name = "name", .value = fs_info->name},
      {.name = "fstype", .value = fs_info->fstype}};

  for (int i = 0; i < fs_info->ndevAlias; ++i) {
    fs_dev_alias[i].name = "devAlias";
    fs_dev_alias[i].value = fs_info->devAlias[i];
  }

  init_notif(&notif, domain, NOTIF_OKAY, "File system information",
             "file_system", NULL);
  NM_ADD_STR_ITEMS(fs_str_items, STATIC_ARRAY_SIZE(fs_str_items));
  NM_ADD_ITEM(plugin_notification_meta_add_unsigned_int, "ndevAlias",
              fs_info->ndevAlias);
  NM_ADD_STR_ITEMS(fs_dev_alias, fs_info->ndevAlias);

  plugin_dispatch_notification(&notif);

cleanup:
  if (notif.meta)
    plugin_notification_meta_free(notif.meta);
  return ret;
}

#undef RETURN_ON_ERR
#undef NM_ADD_STR_ITEMS

static int get_fs_info(virDomainPtr domain) {
  virDomainFSInfoPtr *fs_info = NULL;
  int ret = 0;

  int mount_points_cnt = virDomainGetFSInfo(domain, &fs_info, 0);
  if (mount_points_cnt == -1) {
    ERROR(PLUGIN_NAME " plugin: virDomainGetFSInfo failed: %d",
          mount_points_cnt);
    return mount_points_cnt;
  }

  for (int i = 0; i < mount_points_cnt; ++i) {
    if (fs_info_notify(domain, fs_info[i]) != 0) {
      ERROR(PLUGIN_NAME " plugin: failed to send file system notification "
                        "for mount point %s",
            fs_info[i]->mountpoint);
      ret = -1;
    }
    virDomainFSInfoFree(fs_info[i]);
  }

  sfree(fs_info);
  return ret;
}

#endif /* HAVE_FS_INFO */

#ifdef HAVE_JOB_STATS
static void job_stats_submit(virDomainPtr domain, virTypedParameterPtr param) {
  value_t vl = {0};

  if (param->type == VIR_TYPED_PARAM_INT)
    vl.derive = param->value.i;
  else if (param->type == VIR_TYPED_PARAM_UINT)
    vl.derive = param->value.ui;
  else if (param->type == VIR_TYPED_PARAM_LLONG)
    vl.derive = param->value.l;
  else if (param->type == VIR_TYPED_PARAM_ULLONG)
    vl.derive = param->value.ul;
  else if (param->type == VIR_TYPED_PARAM_DOUBLE)
    vl.derive = param->value.d;
  else if (param->type == VIR_TYPED_PARAM_BOOLEAN)
    vl.derive = param->value.b;
  else if (param->type == VIR_TYPED_PARAM_STRING) {
    submit_notif(domain, NOTIF_OKAY, param->value.s, "job_stats", param->field);
    return;
  } else {
    ERROR(PLUGIN_NAME " plugin: unrecognized virTypedParameterType");
    return;
  }

  submit(domain, "job_stats", param->field, &vl, 1);
}

static int get_job_stats(virDomainPtr domain) {
  int ret = 0;
  int job_type = 0;
  int nparams = 0;
  virTypedParameterPtr params = NULL;
  int flags = (extra_stats & ex_stats_job_stats_completed)
                  ? VIR_DOMAIN_JOB_STATS_COMPLETED
                  : 0;

  ret = virDomainGetJobStats(domain, &job_type, &params, &nparams, flags);
  if (ret != 0) {
    ERROR(PLUGIN_NAME " plugin: virDomainGetJobStats failed: %d", ret);
    return ret;
  }

  DEBUG(PLUGIN_NAME " plugin: job_type=%d nparams=%d", job_type, nparams);

  for (int i = 0; i < nparams; ++i) {
    DEBUG(PLUGIN_NAME " plugin: param[%d] field=%s type=%d", i, params[i].field,
          params[i].type);
    job_stats_submit(domain, &params[i]);
  }

  virTypedParamsFree(params, nparams);
  return ret;
}
#endif /* HAVE_JOB_STATS */

static int get_domain_metrics(domain_t *domain) {
  struct lv_info info;

  if (!domain || !domain->ptr) {
    ERROR(PLUGIN_NAME ": get_domain_metrics: NULL pointer");
    return -1;
  }

  init_lv_info(&info);
  int status = lv_domain_info(domain->ptr, &info);
  if (status != 0) {
    ERROR(PLUGIN_NAME " plugin: virDomainGetInfo failed with status %i.",
          status);
    return -1;
  }

  if (extra_stats & ex_stats_domain_state) {
#ifdef HAVE_DOM_REASON
    /* At this point we already know domain's state from virDomainGetInfo call,
     * however it doesn't provide a reason for entering particular state.
     * We need to get it from virDomainGetState.
     */
    GET_STATS(get_domain_state, "domain reason", domain->ptr);
#else
    /* virDomainGetState is not available. Submit 0, which corresponds to
     * unknown reason. */
    domain_state_submit(domain->ptr, info.di.state, 0);
#endif
  }

  /* Gather remaining stats only for running domains */
  if (info.di.state != VIR_DOMAIN_RUNNING)
    return 0;

  pcpu_submit(domain->ptr, &info);
  cpu_submit(domain, info.di.cpuTime);

  memory_submit(domain->ptr, (gauge_t)info.di.memory * 1024);

  GET_STATS(get_vcpu_stats, "vcpu stats", domain->ptr, info.di.nrVirtCpu);
  GET_STATS(get_memory_stats, "memory stats", domain->ptr);

#ifdef HAVE_PERF_STATS
  if (extra_stats & ex_stats_perf)
    GET_STATS(get_perf_events, "performance monitoring events", domain->ptr);
#endif

#ifdef HAVE_FS_INFO
  if (extra_stats & ex_stats_fs_info)
    GET_STATS(get_fs_info, "file system info", domain->ptr);
#endif

#ifdef HAVE_DISK_ERR
  if (extra_stats & ex_stats_disk_err)
    GET_STATS(get_disk_err, "disk errors", domain->ptr);
#endif

#ifdef HAVE_JOB_STATS
  if (extra_stats &
      (ex_stats_job_stats_completed | ex_stats_job_stats_background))
    GET_STATS(get_job_stats, "job stats", domain->ptr);
#endif

  /* Update cached virDomainInfo. It has to be done after cpu_submit */
  memcpy(&domain->info, &info.di, sizeof(domain->info));
  return 0;
}

static int get_if_dev_stats(struct interface_device *if_dev) {
  virDomainInterfaceStatsStruct stats = {0};
  char *display_name = NULL;

  if (!if_dev) {
    ERROR(PLUGIN_NAME " plugin: get_if_dev_stats: NULL pointer");
    return -1;
  }

  switch (interface_format) {
  case if_address:
    display_name = if_dev->address;
    break;
  case if_number:
    display_name = if_dev->number;
    break;
  case if_name:
  default:
    display_name = if_dev->path;
  }

  if (virDomainInterfaceStats(if_dev->dom, if_dev->path, &stats,
                              sizeof(stats)) != 0) {
    ERROR(PLUGIN_NAME " plugin: virDomainInterfaceStats failed");
    return -1;
  }

  if ((stats.rx_bytes != -1) && (stats.tx_bytes != -1))
    submit_derive2("if_octets", (derive_t)stats.rx_bytes,
                   (derive_t)stats.tx_bytes, if_dev->dom, display_name);

  if ((stats.rx_packets != -1) && (stats.tx_packets != -1))
    submit_derive2("if_packets", (derive_t)stats.rx_packets,
                   (derive_t)stats.tx_packets, if_dev->dom, display_name);

  if ((stats.rx_errs != -1) && (stats.tx_errs != -1))
    submit_derive2("if_errors", (derive_t)stats.rx_errs,
                   (derive_t)stats.tx_errs, if_dev->dom, display_name);

  if ((stats.rx_drop != -1) && (stats.tx_drop != -1))
    submit_derive2("if_dropped", (derive_t)stats.rx_drop,
                   (derive_t)stats.tx_drop, if_dev->dom, display_name);
  return 0;
}

static int lv_read(user_data_t *ud) {
  time_t t;
  struct lv_read_instance *inst = NULL;
  struct lv_read_state *state = NULL;

  if (ud->data == NULL) {
    ERROR(PLUGIN_NAME " plugin: NULL userdata");
    return -1;
  }

  inst = ud->data;
  state = &inst->read_state;

  if (inst->id == 0) {
    if (lv_connect() < 0)
      return -1;
  }

  time(&t);

  /* Need to refresh domain or device lists? */
  if ((last_refresh == (time_t)0) ||
      ((interval > 0) && ((last_refresh + interval) <= t))) {
    if (refresh_lists(inst) != 0) {
      if (inst->id == 0)
        lv_disconnect();
      return -1;
    }
    last_refresh = t;
  }

#if 0
    for (int i = 0; i < nr_domains; ++i)
        fprintf (stderr, "domain %s\n", virDomainGetName (state->domains[i].ptr));
    for (int i = 0; i < nr_block_devices; ++i)
        fprintf  (stderr, "block device %d %s:%s\n",
                  i, virDomainGetName (block_devices[i].dom),
                  block_devices[i].path);
    for (int i = 0; i < nr_interface_devices; ++i)
        fprintf (stderr, "interface device %d %s:%s\n",
                 i, virDomainGetName (interface_devices[i].dom),
                 interface_devices[i].path);
#endif

  /* Get domains' metrics */
  for (int i = 0; i < state->nr_domains; ++i) {
    int status = get_domain_metrics(&state->domains[i]);
    if (status != 0)
      ERROR(PLUGIN_NAME " failed to get metrics for domain=%s",
            virDomainGetName(state->domains[i].ptr));
  }

  /* Get block device stats for each domain. */
  for (int i = 0; i < state->nr_block_devices; ++i) {
    int status = get_block_stats(&state->block_devices[i]);
    if (status != 0)
      ERROR(PLUGIN_NAME
            " failed to get stats for block device (%s) in domain %s",
            state->block_devices[i].path,
            virDomainGetName(state->domains[i].ptr));
  }

  /* Get interface stats for each domain. */
  for (int i = 0; i < state->nr_interface_devices; ++i) {
    int status = get_if_dev_stats(&state->interface_devices[i]);
    if (status != 0)
      ERROR(PLUGIN_NAME
            " failed to get interface stats for device (%s) in domain %s",
            state->interface_devices[i].path,
            virDomainGetName(state->interface_devices[i].dom));
  }

  return 0;
}

static int lv_init_instance(size_t i, plugin_read_cb callback) {
  struct lv_user_data *lv_ud = &(lv_read_user_data[i]);
  struct lv_read_instance *inst = &(lv_ud->inst);

  memset(lv_ud, 0, sizeof(*lv_ud));

  snprintf(inst->tag, sizeof(inst->tag), "%s-%" PRIsz, PLUGIN_NAME, i);
  inst->id = i;

  user_data_t *ud = &(lv_ud->ud);
  ud->data = inst;
  ud->free_func = NULL;

  INFO(PLUGIN_NAME " plugin: reader %s initialized", inst->tag);
  return plugin_register_complex_read(NULL, inst->tag, callback, 0, ud);
}

static void lv_clean_read_state(struct lv_read_state *state) {
  free_block_devices(state);
  free_interface_devices(state);
  free_domains(state);
}

static void lv_fini_instance(size_t i) {
  struct lv_read_instance *inst = &(lv_read_user_data[i].inst);
  struct lv_read_state *state = &(inst->read_state);

  lv_clean_read_state(state);
  INFO(PLUGIN_NAME " plugin: reader %s finalized", inst->tag);
}

static int lv_init(void) {
  if (virInitialize() != 0)
    return -1;

  if (lv_connect() != 0)
    return -1;

  DEBUG(PLUGIN_NAME " plugin: starting %i instances", nr_instances);

  for (int i = 0; i < nr_instances; ++i)
    lv_init_instance(i, lv_read);

  return 0;
}

/*
 * returns 0 on success and <0 on error
 */
static int lv_domain_get_tag(xmlXPathContextPtr xpath_ctx, const char *dom_name,
                             char *dom_tag) {
  char xpath_str[BUFFER_MAX_LEN] = {'\0'};
  xmlXPathObjectPtr xpath_obj = NULL;
  xmlNodePtr xml_node = NULL;
  int ret = -1;
  int err;

  err = xmlXPathRegisterNs(xpath_ctx,
                           (const xmlChar *)METADATA_VM_PARTITION_PREFIX,
                           (const xmlChar *)METADATA_VM_PARTITION_URI);
  if (err) {
    ERROR(PLUGIN_NAME " plugin: xmlXpathRegisterNs(%s, %s) failed on domain %s",
          METADATA_VM_PARTITION_PREFIX, METADATA_VM_PARTITION_URI, dom_name);
    goto done;
  }

  snprintf(xpath_str, sizeof(xpath_str), "/domain/metadata/%s:%s/text()",
           METADATA_VM_PARTITION_PREFIX, METADATA_VM_PARTITION_ELEMENT);
  xpath_obj = xmlXPathEvalExpression((xmlChar *)xpath_str, xpath_ctx);
  if (xpath_obj == NULL) {
    ERROR(PLUGIN_NAME " plugin: xmlXPathEval(%s) failed on domain %s",
          xpath_str, dom_name);
    goto done;
  }

  if (xpath_obj->type != XPATH_NODESET) {
    ERROR(PLUGIN_NAME " plugin: xmlXPathEval(%s) unexpected return type %d "
                      "(wanted %d) on domain %s",
          xpath_str, xpath_obj->type, XPATH_NODESET, dom_name);
    goto done;
  }

  /*
   * from now on there is no real error, it's ok if a domain
   * doesn't have the metadata partition tag.
   */
  ret = 0;
  if (xpath_obj->nodesetval == NULL || xpath_obj->nodesetval->nodeNr != 1) {
    DEBUG(PLUGIN_NAME " plugin: xmlXPathEval(%s) return nodeset size=%i "
                      "expected=1 on domain %s",
          xpath_str,
          (xpath_obj->nodesetval == NULL) ? 0 : xpath_obj->nodesetval->nodeNr,
          dom_name);
  } else {
    xml_node = xpath_obj->nodesetval->nodeTab[0];
    sstrncpy(dom_tag, (const char *)xml_node->content, PARTITION_TAG_MAX_LEN);
  }

done:
  /* deregister to clean up */
  err = xmlXPathRegisterNs(xpath_ctx,
                           (const xmlChar *)METADATA_VM_PARTITION_PREFIX, NULL);
  if (err) {
    /* we can't really recover here */
    ERROR(PLUGIN_NAME
          " plugin: deregistration of namespace %s failed for domain %s",
          METADATA_VM_PARTITION_PREFIX, dom_name);
  }
  if (xpath_obj)
    xmlXPathFreeObject(xpath_obj);

  return ret;
}

static int is_known_tag(const char *dom_tag) {
  for (int i = 0; i < nr_instances; ++i)
    if (!strcmp(dom_tag, lv_read_user_data[i].inst.tag))
      return 1;
  return 0;
}

static int lv_instance_include_domain(struct lv_read_instance *inst,
                                      const char *dom_name,
                                      const char *dom_tag) {
  if ((dom_tag[0] != '\0') && (strcmp(dom_tag, inst->tag) == 0))
    return 1;

  /* instance#0 will always be there, so it is in charge of extra duties */
  if (inst->id == 0) {
    if (dom_tag[0] == '\0' || !is_known_tag(dom_tag)) {
      DEBUG(PLUGIN_NAME " plugin#%s: refreshing domain %s "
                        "with unknown tag '%s'",
            inst->tag, dom_name, dom_tag);
      return 1;
    }
  }

  return 0;
}

/*
  virConnectListAllDomains() appeared in 0.10.2
  Note that LIBVIR_CHECK_VERSION appeared a year later, so
  in some systems which actually have virConnectListAllDomains()
  we can't detect this.
 */
#ifdef LIBVIR_CHECK_VERSION
#if LIBVIR_CHECK_VERSION(0, 10, 2)
#define HAVE_LIST_ALL_DOMAINS 1
#endif
#endif

static int refresh_lists(struct lv_read_instance *inst) {
  struct lv_read_state *state = &inst->read_state;
  int n;

  n = virConnectNumOfDomains(conn);
  if (n < 0) {
    VIRT_ERROR(conn, "reading number of domains");
    return -1;
  }

  lv_clean_read_state(state);

  if (n > 0) {
#ifdef HAVE_LIST_ALL_DOMAINS
    virDomainPtr *domains;
    n = virConnectListAllDomains(conn, &domains,
                                 VIR_CONNECT_LIST_DOMAINS_ACTIVE);
#else
    int *domids;

    /* Get list of domains. */
    domids = malloc(sizeof(*domids) * n);
    if (domids == NULL) {
      ERROR(PLUGIN_NAME " plugin: malloc failed.");
      return -1;
    }

    n = virConnectListDomains(conn, domids, n);
#endif

    if (n < 0) {
      VIRT_ERROR(conn, "reading list of domains");
#ifndef HAVE_LIST_ALL_DOMAINS
      sfree(domids);
#endif
      return -1;
    }

    /* Fetch each domain and add it to the list, unless ignore. */
    for (int i = 0; i < n; ++i) {
      const char *name;
      char *xml = NULL;
      xmlDocPtr xml_doc = NULL;
      xmlXPathContextPtr xpath_ctx = NULL;
      xmlXPathObjectPtr xpath_obj = NULL;
      char tag[PARTITION_TAG_MAX_LEN] = {'\0'};
      virDomainInfo info;
      int status;

#ifdef HAVE_LIST_ALL_DOMAINS
      virDomainPtr dom = domains[i];
#else
      virDomainPtr dom = NULL;
      dom = virDomainLookupByID(conn, domids[i]);
      if (dom == NULL) {
        VIRT_ERROR(conn, "virDomainLookupByID");
        /* Could be that the domain went away -- ignore it anyway. */
        continue;
      }
#endif

      name = virDomainGetName(dom);
      if (name == NULL) {
        VIRT_ERROR(conn, "virDomainGetName");
        goto cont;
      }

      status = virDomainGetInfo(dom, &info);
      if (status != 0) {
        ERROR(PLUGIN_NAME " plugin: virDomainGetInfo failed with status %i.",
              status);
        continue;
      }

      if (info.state != VIR_DOMAIN_RUNNING) {
        DEBUG(PLUGIN_NAME " plugin: skipping inactive domain %s", name);
        continue;
      }

      if (il_domains && ignorelist_match(il_domains, name) != 0)
        goto cont;

      /* Get a list of devices for this domain. */
      xml = virDomainGetXMLDesc(dom, 0);
      if (!xml) {
        VIRT_ERROR(conn, "virDomainGetXMLDesc");
        goto cont;
      }

      /* Yuck, XML.  Parse out the devices. */
      xml_doc = xmlReadDoc((xmlChar *)xml, NULL, NULL, XML_PARSE_NONET);
      if (xml_doc == NULL) {
        VIRT_ERROR(conn, "xmlReadDoc");
        goto cont;
      }

      xpath_ctx = xmlXPathNewContext(xml_doc);

      if (lv_domain_get_tag(xpath_ctx, name, tag) < 0) {
        ERROR(PLUGIN_NAME " plugin: lv_domain_get_tag failed.");
        goto cont;
      }

      if (!lv_instance_include_domain(inst, name, tag))
        goto cont;

      if (add_domain(state, dom) < 0) {
        ERROR(PLUGIN_NAME " plugin: malloc failed.");
        goto cont;
      }

      /* Block devices. */
      const char *bd_xmlpath = "/domain/devices/disk/target[@dev]";
      if (blockdevice_format == source)
        bd_xmlpath = "/domain/devices/disk/source[@dev]";
      xpath_obj = xmlXPathEval((const xmlChar *)bd_xmlpath, xpath_ctx);

      if (xpath_obj == NULL || xpath_obj->type != XPATH_NODESET ||
          xpath_obj->nodesetval == NULL)
        goto cont;

      for (int j = 0; j < xpath_obj->nodesetval->nodeNr; ++j) {
        xmlNodePtr node;
        char *path = NULL;

        node = xpath_obj->nodesetval->nodeTab[j];
        if (!node)
          continue;
        path = (char *)xmlGetProp(node, (xmlChar *)"dev");
        if (!path)
          continue;

        if (il_block_devices &&
            ignore_device_match(il_block_devices, name, path) != 0)
          goto cont2;

        add_block_device(state, dom, path);
      cont2:
        if (path)
          xmlFree(path);
      }
      xmlXPathFreeObject(xpath_obj);

      /* Network interfaces. */
      xpath_obj = xmlXPathEval(
          (xmlChar *)"/domain/devices/interface[target[@dev]]", xpath_ctx);
      if (xpath_obj == NULL || xpath_obj->type != XPATH_NODESET ||
          xpath_obj->nodesetval == NULL)
        goto cont;

      xmlNodeSetPtr xml_interfaces = xpath_obj->nodesetval;

      for (int j = 0; j < xml_interfaces->nodeNr; ++j) {
        char *path = NULL;
        char *address = NULL;
        xmlNodePtr xml_interface;

        xml_interface = xml_interfaces->nodeTab[j];
        if (!xml_interface)
          continue;

        for (xmlNodePtr child = xml_interface->children; child;
             child = child->next) {
          if (child->type != XML_ELEMENT_NODE)
            continue;

          if (xmlStrEqual(child->name, (const xmlChar *)"target")) {
            path = (char *)xmlGetProp(child, (const xmlChar *)"dev");
            if (!path)
              continue;
          } else if (xmlStrEqual(child->name, (const xmlChar *)"mac")) {
            address = (char *)xmlGetProp(child, (const xmlChar *)"address");
            if (!address)
              continue;
          }
        }

        if (il_interface_devices &&
            (ignore_device_match(il_interface_devices, name, path) != 0 ||
             ignore_device_match(il_interface_devices, name, address) != 0))
          goto cont3;

        add_interface_device(state, dom, path, address, j + 1);
      cont3:
        if (path)
          xmlFree(path);
        if (address)
          xmlFree(address);
      }

    cont:
      if (xpath_obj)
        xmlXPathFreeObject(xpath_obj);
      if (xpath_ctx)
        xmlXPathFreeContext(xpath_ctx);
      if (xml_doc)
        xmlFreeDoc(xml_doc);
      sfree(xml);
    }

#ifdef HAVE_LIST_ALL_DOMAINS
    sfree(domains);
#else
    sfree(domids);
#endif
  }

  DEBUG(PLUGIN_NAME " plugin#%s: refreshing"
                    " domains=%i block_devices=%i iface_devices=%i",
        inst->tag, state->nr_domains, state->nr_block_devices,
        state->nr_interface_devices);

  return 0;
}

static void free_domains(struct lv_read_state *state) {
  if (state->domains) {
    for (int i = 0; i < state->nr_domains; ++i)
      virDomainFree(state->domains[i].ptr);
    sfree(state->domains);
  }
  state->domains = NULL;
  state->nr_domains = 0;
}

static int add_domain(struct lv_read_state *state, virDomainPtr dom) {
  domain_t *new_ptr;
  int new_size = sizeof(state->domains[0]) * (state->nr_domains + 1);

  if (state->domains)
    new_ptr = realloc(state->domains, new_size);
  else
    new_ptr = malloc(new_size);

  if (new_ptr == NULL)
    return -1;

  state->domains = new_ptr;
  state->domains[state->nr_domains].ptr = dom;
  memset(&state->domains[state->nr_domains].info, 0,
         sizeof(state->domains[state->nr_domains].info));

  return state->nr_domains++;
}

static void free_block_devices(struct lv_read_state *state) {
  if (state->block_devices) {
    for (int i = 0; i < state->nr_block_devices; ++i)
      sfree(state->block_devices[i].path);
    sfree(state->block_devices);
  }
  state->block_devices = NULL;
  state->nr_block_devices = 0;
}

static int add_block_device(struct lv_read_state *state, virDomainPtr dom,
                            const char *path) {
  struct block_device *new_ptr;
  int new_size =
      sizeof(state->block_devices[0]) * (state->nr_block_devices + 1);
  char *path_copy;

  path_copy = strdup(path);
  if (!path_copy)
    return -1;

  if (state->block_devices)
    new_ptr = realloc(state->block_devices, new_size);
  else
    new_ptr = malloc(new_size);

  if (new_ptr == NULL) {
    sfree(path_copy);
    return -1;
  }
  state->block_devices = new_ptr;
  state->block_devices[state->nr_block_devices].dom = dom;
  state->block_devices[state->nr_block_devices].path = path_copy;
  return state->nr_block_devices++;
}

static void free_interface_devices(struct lv_read_state *state) {
  if (state->interface_devices) {
    for (int i = 0; i < state->nr_interface_devices; ++i) {
      sfree(state->interface_devices[i].path);
      sfree(state->interface_devices[i].address);
      sfree(state->interface_devices[i].number);
    }
    sfree(state->interface_devices);
  }
  state->interface_devices = NULL;
  state->nr_interface_devices = 0;
}

static int add_interface_device(struct lv_read_state *state, virDomainPtr dom,
                                const char *path, const char *address,
                                unsigned int number) {
  struct interface_device *new_ptr;
  int new_size =
      sizeof(state->interface_devices[0]) * (state->nr_interface_devices + 1);
  char *path_copy, *address_copy, number_string[15];

  if ((path == NULL) || (address == NULL))
    return EINVAL;

  path_copy = strdup(path);
  if (!path_copy)
    return -1;

  address_copy = strdup(address);
  if (!address_copy) {
    sfree(path_copy);
    return -1;
  }

  snprintf(number_string, sizeof(number_string), "interface-%u", number);

  if (state->interface_devices)
    new_ptr = realloc(state->interface_devices, new_size);
  else
    new_ptr = malloc(new_size);

  if (new_ptr == NULL) {
    sfree(path_copy);
    sfree(address_copy);
    return -1;
  }
  state->interface_devices = new_ptr;
  state->interface_devices[state->nr_interface_devices].dom = dom;
  state->interface_devices[state->nr_interface_devices].path = path_copy;
  state->interface_devices[state->nr_interface_devices].address = address_copy;
  state->interface_devices[state->nr_interface_devices].number =
      strdup(number_string);
  return state->nr_interface_devices++;
}

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
  snprintf(name, n, "%s:%s", domname, devpath);
  r = ignorelist_match(il, name);
  sfree(name);
  return r;
}

static int lv_shutdown(void) {
  for (int i = 0; i < nr_instances; ++i) {
    lv_fini_instance(i);
  }

  lv_disconnect();

  ignorelist_free(il_domains);
  il_domains = NULL;
  ignorelist_free(il_block_devices);
  il_block_devices = NULL;
  ignorelist_free(il_interface_devices);
  il_interface_devices = NULL;

  return 0;
}

void module_register(void) {
  plugin_register_config(PLUGIN_NAME, lv_config, config_keys, NR_CONFIG_KEYS);
  plugin_register_init(PLUGIN_NAME, lv_init);
  plugin_register_shutdown(PLUGIN_NAME, lv_shutdown);
}
