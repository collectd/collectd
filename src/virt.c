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

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"
#include "utils_complain.h"

#include <libgen.h> /* for basename(3) */
#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <stdbool.h>

/* Plugin name */
#define PLUGIN_NAME "virt"

/* Secure strcat macro assuring null termination. Parameter (n) is the size of
   buffer (d), allowing this macro to be safe for static and dynamic buffers */
#define SSTRNCAT(d, s, n)                                                      \
  do {                                                                         \
    size_t _l = strlen(d);                                                     \
    sstrncpy((d) + _l, (s), (n)-_l);                                           \
  } while (0)

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

/*
  virConnectListAllDomains() appeared in 0.10.2
  Note that LIBVIR_CHECK_VERSION appeared a year later, so
  in some systems which actually have virConnectListAllDomains()
  we can't detect this.
 */
#if LIBVIR_CHECK_VERSION(0, 10, 2)
#define HAVE_LIST_ALL_DOMAINS 1
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

/* structure used for aggregating notification-thread data*/
typedef struct virt_notif_thread_s {
  pthread_t event_loop_tid;
  int domain_event_cb_id;
  pthread_mutex_t active_mutex; /* protects 'is_active' member access*/
  bool is_active;
} virt_notif_thread_t;

/* PersistentNotification is false by default */
static bool persistent_notification = false;

static bool report_block_devices = true;
static bool report_network_interfaces = true;

/* Thread used for handling libvirt notifications events */
static virt_notif_thread_t notif_thread;

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

static int map_domain_event_to_state(int event) {
  int ret;
  switch (event) {
  case VIR_DOMAIN_EVENT_STARTED:
    ret = VIR_DOMAIN_RUNNING;
    break;
  case VIR_DOMAIN_EVENT_SUSPENDED:
    ret = VIR_DOMAIN_PAUSED;
    break;
  case VIR_DOMAIN_EVENT_RESUMED:
    ret = VIR_DOMAIN_RUNNING;
    break;
  case VIR_DOMAIN_EVENT_STOPPED:
    ret = VIR_DOMAIN_SHUTOFF;
    break;
  case VIR_DOMAIN_EVENT_SHUTDOWN:
    ret = VIR_DOMAIN_SHUTDOWN;
    break;
#ifdef HAVE_DOM_STATE_PMSUSPENDED
  case VIR_DOMAIN_EVENT_PMSUSPENDED:
    ret = VIR_DOMAIN_PMSUSPENDED;
    break;
#endif
#ifdef HAVE_DOM_REASON_CRASHED
  case VIR_DOMAIN_EVENT_CRASHED:
    ret = VIR_DOMAIN_CRASHED;
    break;
#endif
  default:
    ret = VIR_DOMAIN_NOSTATE;
  }
  return ret;
}

#ifdef HAVE_DOM_REASON
static int map_domain_event_detail_to_reason(int event, int detail) {
  int ret;
  switch (event) {
  case VIR_DOMAIN_EVENT_STARTED:
    switch (detail) {
    case VIR_DOMAIN_EVENT_STARTED_BOOTED: /* Normal startup from boot */
      ret = VIR_DOMAIN_RUNNING_BOOTED;
      break;
    case VIR_DOMAIN_EVENT_STARTED_MIGRATED: /* Incoming migration from another
                                               host */
      ret = VIR_DOMAIN_RUNNING_MIGRATED;
      break;
    case VIR_DOMAIN_EVENT_STARTED_RESTORED: /* Restored from a state file */
      ret = VIR_DOMAIN_RUNNING_RESTORED;
      break;
    case VIR_DOMAIN_EVENT_STARTED_FROM_SNAPSHOT: /* Restored from snapshot */
      ret = VIR_DOMAIN_RUNNING_FROM_SNAPSHOT;
      break;
#ifdef HAVE_DOM_REASON_RUNNING_WAKEUP
    case VIR_DOMAIN_EVENT_STARTED_WAKEUP: /* Started due to wakeup event */
      ret = VIR_DOMAIN_RUNNING_WAKEUP;
      break;
#endif
    default:
      ret = VIR_DOMAIN_RUNNING_UNKNOWN;
    }
    break;
  case VIR_DOMAIN_EVENT_SUSPENDED:
    switch (detail) {
    case VIR_DOMAIN_EVENT_SUSPENDED_PAUSED: /* Normal suspend due to admin
                                               pause */
      ret = VIR_DOMAIN_PAUSED_USER;
      break;
    case VIR_DOMAIN_EVENT_SUSPENDED_MIGRATED: /* Suspended for offline
                                                 migration */
      ret = VIR_DOMAIN_PAUSED_MIGRATION;
      break;
    case VIR_DOMAIN_EVENT_SUSPENDED_IOERROR: /* Suspended due to a disk I/O
                                                error */
      ret = VIR_DOMAIN_PAUSED_IOERROR;
      break;
    case VIR_DOMAIN_EVENT_SUSPENDED_WATCHDOG: /* Suspended due to a watchdog
                                                 firing */
      ret = VIR_DOMAIN_PAUSED_WATCHDOG;
      break;
    case VIR_DOMAIN_EVENT_SUSPENDED_RESTORED: /* Restored from paused state
                                                 file */
      ret = VIR_DOMAIN_PAUSED_UNKNOWN;
      break;
    case VIR_DOMAIN_EVENT_SUSPENDED_FROM_SNAPSHOT: /* Restored from paused
                                                      snapshot */
      ret = VIR_DOMAIN_PAUSED_FROM_SNAPSHOT;
      break;
    case VIR_DOMAIN_EVENT_SUSPENDED_API_ERROR: /* Suspended after failure during
                                                  libvirt API call */
      ret = VIR_DOMAIN_PAUSED_UNKNOWN;
      break;
#ifdef HAVE_DOM_REASON_POSTCOPY
    case VIR_DOMAIN_EVENT_SUSPENDED_POSTCOPY: /* Suspended for post-copy
                                                 migration */
      ret = VIR_DOMAIN_PAUSED_POSTCOPY;
      break;
    case VIR_DOMAIN_EVENT_SUSPENDED_POSTCOPY_FAILED: /* Suspended after failed
                                                        post-copy */
      ret = VIR_DOMAIN_PAUSED_POSTCOPY_FAILED;
      break;
#endif
    default:
      ret = VIR_DOMAIN_PAUSED_UNKNOWN;
    }
    break;
  case VIR_DOMAIN_EVENT_RESUMED:
    switch (detail) {
    case VIR_DOMAIN_EVENT_RESUMED_UNPAUSED: /* Normal resume due to admin
                                               unpause */
      ret = VIR_DOMAIN_RUNNING_UNPAUSED;
      break;
    case VIR_DOMAIN_EVENT_RESUMED_MIGRATED: /* Resumed for completion of
                                               migration */
      ret = VIR_DOMAIN_RUNNING_MIGRATED;
      break;
    case VIR_DOMAIN_EVENT_RESUMED_FROM_SNAPSHOT: /* Resumed from snapshot */
      ret = VIR_DOMAIN_RUNNING_FROM_SNAPSHOT;
      break;
#ifdef HAVE_DOM_REASON_POSTCOPY
    case VIR_DOMAIN_EVENT_RESUMED_POSTCOPY: /* Resumed, but migration is still
                                               running in post-copy mode */
      ret = VIR_DOMAIN_RUNNING_POSTCOPY;
      break;
#endif
    default:
      ret = VIR_DOMAIN_RUNNING_UNKNOWN;
    }
    break;
  case VIR_DOMAIN_EVENT_STOPPED:
    switch (detail) {
    case VIR_DOMAIN_EVENT_STOPPED_SHUTDOWN: /* Normal shutdown */
      ret = VIR_DOMAIN_SHUTOFF_SHUTDOWN;
      break;
    case VIR_DOMAIN_EVENT_STOPPED_DESTROYED: /* Forced poweroff from host */
      ret = VIR_DOMAIN_SHUTOFF_DESTROYED;
      break;
    case VIR_DOMAIN_EVENT_STOPPED_CRASHED: /* Guest crashed */
      ret = VIR_DOMAIN_SHUTOFF_CRASHED;
      break;
    case VIR_DOMAIN_EVENT_STOPPED_MIGRATED: /* Migrated off to another host */
      ret = VIR_DOMAIN_SHUTOFF_MIGRATED;
      break;
    case VIR_DOMAIN_EVENT_STOPPED_SAVED: /* Saved to a state file */
      ret = VIR_DOMAIN_SHUTOFF_SAVED;
      break;
    case VIR_DOMAIN_EVENT_STOPPED_FAILED: /* Host emulator/mgmt failed */
      ret = VIR_DOMAIN_SHUTOFF_FAILED;
      break;
    case VIR_DOMAIN_EVENT_STOPPED_FROM_SNAPSHOT: /* Offline snapshot loaded */
      ret = VIR_DOMAIN_SHUTOFF_FROM_SNAPSHOT;
      break;
    default:
      ret = VIR_DOMAIN_SHUTOFF_UNKNOWN;
    }
    break;
  case VIR_DOMAIN_EVENT_SHUTDOWN:
    switch (detail) {
    case VIR_DOMAIN_EVENT_SHUTDOWN_FINISHED: /* Guest finished shutdown
                                                sequence */
      ret = VIR_DOMAIN_SHUTDOWN_USER;
      break;
    default:
      ret = VIR_DOMAIN_SHUTDOWN_UNKNOWN;
    }
    break;
#ifdef HAVE_DOM_STATE_PMSUSPENDED
  case VIR_DOMAIN_EVENT_PMSUSPENDED:
    switch (detail) {
    case VIR_DOMAIN_EVENT_PMSUSPENDED_MEMORY: /* Guest was PM suspended to
                                                 memory */
      ret = VIR_DOMAIN_PMSUSPENDED_UNKNOWN;
      break;
    case VIR_DOMAIN_EVENT_PMSUSPENDED_DISK: /* Guest was PM suspended to disk */
      ret = VIR_DOMAIN_PMSUSPENDED_DISK_UNKNOWN;
      break;
    default:
      ret = VIR_DOMAIN_PMSUSPENDED_UNKNOWN;
    }
    break;
#endif
  case VIR_DOMAIN_EVENT_CRASHED:
    switch (detail) {
    case VIR_DOMAIN_EVENT_CRASHED_PANICKED: /* Guest was panicked */
      ret = VIR_DOMAIN_CRASHED_PANICKED;
      break;
    default:
      ret = VIR_DOMAIN_CRASHED_UNKNOWN;
    }
    break;
  default:
    ret = VIR_DOMAIN_NOSTATE_UNKNOWN;
  }
  return ret;
}

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

#define NANOSEC_IN_SEC 1e9

#define GET_STATS(_f, _name, ...)                                              \
  do {                                                                         \
    status = _f(__VA_ARGS__);                                                  \
    if (status != 0)                                                           \
      ERROR(PLUGIN_NAME " plugin: Failed to get " _name);                      \
  } while (0)

/* Connection. */
static virConnectPtr conn;
static char *conn_string;
static c_complain_t conn_complain = C_COMPLAIN_INIT_STATIC;

/* Node information required for %CPU */
static virNodeInfo nodeinfo;

/* Seconds between list refreshes, 0 disables completely. */
static int interval = 60;

/* List of domains, if specified. */
static ignorelist_t *il_domains;
/* List of block devices, if specified. */
static ignorelist_t *il_block_devices;
/* List of network interface devices, if specified. */
static ignorelist_t *il_interface_devices;

static int ignore_device_match(ignorelist_t *, const char *domname,
                               const char *devpath);

/* Actual list of block devices found on last refresh. */
struct block_device {
  virDomainPtr dom; /* domain */
  char *path;       /* name of block device */
  bool has_source;  /* information whether source is defined or not */
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
  bool active;
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
static int add_domain(struct lv_read_state *state, virDomainPtr dom,
                      bool active);

static void free_block_devices(struct lv_read_state *state);
static int add_block_device(struct lv_read_state *state, virDomainPtr dom,
                            const char *path, bool has_source);

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
#define HF_MAX_FIELDS 4

enum hf_field { hf_none = 0, hf_hostname, hf_name, hf_uuid, hf_metadata };

static enum hf_field hostname_format[HF_MAX_FIELDS] = {hf_name};

/* PluginInstanceFormat */
#define PLGINST_MAX_FIELDS 3

enum plginst_field {
  plginst_none = 0,
  plginst_name,
  plginst_uuid,
  plginst_metadata
};

static enum plginst_field plugin_instance_format[PLGINST_MAX_FIELDS] = {
    plginst_none};

/* HostnameMetadataNS && HostnameMetadataXPath */
static char *hm_xpath;
static char *hm_ns;

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
  ex_stats_disk_allocation = 1 << 10,
  ex_stats_disk_capacity = 1 << 11,
  ex_stats_disk_physical = 1 << 12
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
    {"disk_allocation", ex_stats_disk_allocation},
    {"disk_capacity", ex_stats_disk_capacity},
    {"disk_physical", ex_stats_disk_physical},
    {NULL, ex_stats_none},
};

/* BlockDeviceFormatBasename */
static bool blockdevice_format_basename;
static enum bd_field blockdevice_format = target;
static enum if_field interface_format = if_name;

/* Time that we last refreshed. */
static time_t last_refresh = (time_t)0;

static int refresh_lists(struct lv_read_instance *inst);

struct lv_block_stats {
  virDomainBlockStatsStruct bi;

  long long rd_total_times;
  long long wr_total_times;

  long long fl_req;
  long long fl_total_times;
};

static void init_block_stats(struct lv_block_stats *bstats) {
  if (bstats == NULL)
    return;

  bstats->bi.rd_req = -1;
  bstats->bi.wr_req = -1;
  bstats->bi.rd_bytes = -1;
  bstats->bi.wr_bytes = -1;

  bstats->rd_total_times = -1;
  bstats->wr_total_times = -1;
  bstats->fl_req = -1;
  bstats->fl_total_times = -1;
}

static void init_block_info(virDomainBlockInfoPtr binfo) {
  binfo->allocation = -1;
  binfo->capacity = -1;
  binfo->physical = -1;
}

#ifdef HAVE_BLOCK_STATS_FLAGS

#define GET_BLOCK_STATS_VALUE(NAME, FIELD)                                     \
  if (!strcmp(param[i].field, NAME)) {                                         \
    bstats->FIELD = param[i].value.l;                                          \
    continue;                                                                  \
  }

static int get_block_stats(struct lv_block_stats *bstats,
                           virTypedParameterPtr param, int nparams) {
  if (bstats == NULL || param == NULL)
    return -1;

  for (int i = 0; i < nparams; ++i) {
    /* ignore type. Everything must be LLONG anyway. */
    GET_BLOCK_STATS_VALUE("rd_operations", bi.rd_req);
    GET_BLOCK_STATS_VALUE("wr_operations", bi.wr_req);
    GET_BLOCK_STATS_VALUE("rd_bytes", bi.rd_bytes);
    GET_BLOCK_STATS_VALUE("wr_bytes", bi.wr_bytes);
    GET_BLOCK_STATS_VALUE("rd_total_times", rd_total_times);
    GET_BLOCK_STATS_VALUE("wr_total_times", wr_total_times);
    GET_BLOCK_STATS_VALUE("flush_operations", fl_req);
    GET_BLOCK_STATS_VALUE("flush_total_times", fl_total_times);
  }

  return 0;
}

#undef GET_BLOCK_STATS_VALUE

#endif /* HAVE_BLOCK_STATS_FLAGS */

/* ERROR(...) macro for virterrors. */
#define VIRT_ERROR(conn, s)                                                    \
  do {                                                                         \
    virErrorPtr err;                                                           \
    err = (conn) ? virConnGetLastError((conn)) : virGetLastError();            \
    if (err)                                                                   \
      ERROR(PLUGIN_NAME " plugin: %s failed: %s", (s), err->message);          \
  } while (0)

static char *metadata_get_hostname(virDomainPtr dom) {
  const char *xpath_str = NULL;
  if (hm_xpath == NULL)
    xpath_str = "/instance/name/text()";
  else
    xpath_str = hm_xpath;

  const char *namespace = NULL;
  if (hm_ns == NULL) {
    namespace = "http://openstack.org/xmlns/libvirt/nova/1.0";
  } else {
    namespace = hm_ns;
  }

  char *metadata_str = virDomainGetMetadata(
      dom, VIR_DOMAIN_METADATA_ELEMENT, namespace, VIR_DOMAIN_AFFECT_CURRENT);
  if (metadata_str == NULL) {
    return NULL;
  }

  char *hostname = NULL;
  xmlXPathContextPtr xpath_ctx = NULL;
  xmlXPathObjectPtr xpath_obj = NULL;
  xmlNodePtr xml_node = NULL;

  xmlDocPtr xml_doc =
      xmlReadDoc((xmlChar *)metadata_str, NULL, NULL, XML_PARSE_NONET);
  if (xml_doc == NULL) {
    ERROR(PLUGIN_NAME " plugin: xmlReadDoc failed to read metadata");
    goto metadata_end;
  }

  xpath_ctx = xmlXPathNewContext(xml_doc);
  if (xpath_ctx == NULL) {
    ERROR(PLUGIN_NAME " plugin: xmlXPathNewContext(%s) failed for metadata",
          metadata_str);
    goto metadata_end;
  }
  xpath_obj = xmlXPathEval((xmlChar *)xpath_str, xpath_ctx);
  if (xpath_obj == NULL) {
    ERROR(PLUGIN_NAME " plugin: xmlXPathEval(%s) failed for metadata",
          xpath_str);
    goto metadata_end;
  }

  if (xpath_obj->type != XPATH_NODESET) {
    ERROR(PLUGIN_NAME " plugin: xmlXPathEval(%s) unexpected return type %d "
                      "(wanted %d) for metadata",
          xpath_str, xpath_obj->type, XPATH_NODESET);
    goto metadata_end;
  }

  // TODO(sileht): We can support || operator by looping on nodes here
  if (xpath_obj->nodesetval == NULL || xpath_obj->nodesetval->nodeNr != 1) {
    WARNING(PLUGIN_NAME " plugin: xmlXPathEval(%s) return nodeset size=%i "
                        "expected=1 for metadata",
            xpath_str,
            (xpath_obj->nodesetval == NULL) ? 0
                                            : xpath_obj->nodesetval->nodeNr);
    goto metadata_end;
  }

  xml_node = xpath_obj->nodesetval->nodeTab[0];
  if (xml_node->type == XML_TEXT_NODE) {
    hostname = strdup((const char *)xml_node->content);
  } else if (xml_node->type == XML_ATTRIBUTE_NODE) {
    hostname = strdup((const char *)xml_node->children->content);
  } else {
    ERROR(PLUGIN_NAME " plugin: xmlXPathEval(%s) unsupported node type %d",
          xpath_str, xml_node->type);
    goto metadata_end;
  }

  if (hostname == NULL) {
    ERROR(PLUGIN_NAME " plugin: strdup(%s) hostname failed", xpath_str);
    goto metadata_end;
  }

metadata_end:
  if (xpath_obj)
    xmlXPathFreeObject(xpath_obj);
  if (xpath_ctx)
    xmlXPathFreeContext(xpath_ctx);
  if (xml_doc)
    xmlFreeDoc(xml_doc);
  sfree(metadata_str);
  return hostname;
}

static void init_value_list(value_list_t *vl, virDomainPtr dom) {
  const char *name;
  char uuid[VIR_UUID_STRING_BUFLEN];

  sstrncpy(vl->plugin, PLUGIN_NAME, sizeof(vl->plugin));

  vl->host[0] = '\0';

  /* Construct the hostname field according to HostnameFormat. */
  for (int i = 0; i < HF_MAX_FIELDS; ++i) {
    if (hostname_format[i] == hf_none)
      continue;

    if (i > 0)
      SSTRNCAT(vl->host, ":", sizeof(vl->host));

    switch (hostname_format[i]) {
    case hf_none:
      break;
    case hf_hostname:
      SSTRNCAT(vl->host, hostname_g, sizeof(vl->host));
      break;
    case hf_name:
      name = virDomainGetName(dom);
      if (name)
        SSTRNCAT(vl->host, name, sizeof(vl->host));
      break;
    case hf_uuid:
      if (virDomainGetUUIDString(dom, uuid) == 0)
        SSTRNCAT(vl->host, uuid, sizeof(vl->host));
      break;
    case hf_metadata:
      name = metadata_get_hostname(dom);
      if (name)
        SSTRNCAT(vl->host, name, sizeof(vl->host));
      break;
    }
  }

  /* Construct the plugin instance field according to PluginInstanceFormat. */
  for (int i = 0; i < PLGINST_MAX_FIELDS; ++i) {
    if (plugin_instance_format[i] == plginst_none)
      continue;

    if (i > 0)
      SSTRNCAT(vl->plugin_instance, ":", sizeof(vl->plugin_instance));

    switch (plugin_instance_format[i]) {
    case plginst_none:
      break;
    case plginst_name:
      name = virDomainGetName(dom);
      if (name)
        SSTRNCAT(vl->plugin_instance, name, sizeof(vl->plugin_instance));
      break;
    case plginst_uuid:
      if (virDomainGetUUIDString(dom, uuid) == 0)
        SSTRNCAT(vl->plugin_instance, uuid, sizeof(vl->plugin_instance));
      break;
    case plginst_metadata:
      name = metadata_get_hostname(dom);
      if (name)
        SSTRNCAT(vl->plugin_instance, name, sizeof(vl->plugin_instance));
      break;
    }
  }

} /* void init_value_list */

static int init_notif(notification_t *notif, const virDomainPtr domain,
                      int severity, const char *msg, const char *type,
                      const char *type_instance) {
  value_list_t vl = VALUE_LIST_INIT;

  if (!notif) {
    ERROR(PLUGIN_NAME " plugin: init_notif: NULL pointer");
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

  DEBUG(PLUGIN_NAME " plugin: node_cpus=%u cpu_time_old=%" PRIu64
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

static void disk_block_stats_submit(struct lv_block_stats *bstats,
                                    virDomainPtr dom, const char *dev,
                                    virDomainBlockInfoPtr binfo) {
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

  if ((bstats->bi.rd_req != -1) && (bstats->bi.wr_req != -1))
    submit_derive2("disk_ops", (derive_t)bstats->bi.rd_req,
                   (derive_t)bstats->bi.wr_req, dom, type_instance);

  if ((bstats->bi.rd_bytes != -1) && (bstats->bi.wr_bytes != -1))
    submit_derive2("disk_octets", (derive_t)bstats->bi.rd_bytes,
                   (derive_t)bstats->bi.wr_bytes, dom, type_instance);

  if (extra_stats & ex_stats_disk) {
    if ((bstats->rd_total_times != -1) && (bstats->wr_total_times != -1))
      submit_derive2("disk_time", (derive_t)bstats->rd_total_times,
                     (derive_t)bstats->wr_total_times, dom, type_instance);

    if (bstats->fl_req != -1)
      submit(dom, "total_requests", flush_type_instance,
             &(value_t){.derive = (derive_t)bstats->fl_req}, 1);
    if (bstats->fl_total_times != -1) {
      derive_t value = bstats->fl_total_times / 1000; // ns -> ms
      submit(dom, "total_time_in_ms", flush_type_instance,
             &(value_t){.derive = value}, 1);
    }
  }

  /* disk_allocation, disk_capacity and disk_physical are stored only
   * if corresponding extrastats are set in collectd configuration file */
  if ((extra_stats & ex_stats_disk_allocation) && binfo->allocation != -1)
    submit(dom, "disk_allocation", type_instance,
           &(value_t){.gauge = (gauge_t)binfo->allocation}, 1);

  if ((extra_stats & ex_stats_disk_capacity) && binfo->capacity != -1)
    submit(dom, "disk_capacity", type_instance,
           &(value_t){.gauge = (gauge_t)binfo->capacity}, 1);

  if ((extra_stats & ex_stats_disk_physical) && binfo->physical != -1)
    submit(dom, "disk_physical", type_instance,
           &(value_t){.gauge = (gauge_t)binfo->physical}, 1);

  sfree(dev_copy);
}

/**
 * Function for parsing ExtraStats configuration options.
 * Result of parsing is stored under 'out_parsed_flags' pointer.
 *
 * Returns 0 in case of success and 1 in case of parsing error
 */
static int parse_ex_stats_flags(unsigned int *out_parsed_flags, char **exstats,
                                int numexstats) {
  unsigned int ex_stats_flags = ex_stats_none;

  assert(out_parsed_flags != NULL);

  for (int i = 0; i < numexstats; i++) {
    for (int j = 0; ex_stats_table[j].name != NULL; j++) {
      if (strcasecmp(exstats[i], ex_stats_table[j].name) == 0) {
        DEBUG(PLUGIN_NAME " plugin: enabling extra stats for '%s'",
              ex_stats_table[j].name);
        ex_stats_flags |= ex_stats_table[j].flag;
        break;
      }

      if (ex_stats_table[j + 1].name == NULL) {
        ERROR(PLUGIN_NAME " plugin: Unmatched ExtraStats option: %s",
              exstats[i]);
        return 1;
      }
    }
  }

  *out_parsed_flags = ex_stats_flags;
  return 0;
}

static void domain_state_submit_notif(virDomainPtr dom, int state, int reason) {
  if ((state < 0) || ((size_t)state >= STATIC_ARRAY_SIZE(domain_states))) {
    ERROR(PLUGIN_NAME " plugin: Array index out of bounds: state=%d", state);
    return;
  }

  char msg[DATA_MAX_NAME_LEN];
  const char *state_str = domain_states[state];
#ifdef HAVE_DOM_REASON
  if ((reason < 0) ||
      ((size_t)reason >= STATIC_ARRAY_SIZE(domain_reasons[0]))) {
    ERROR(PLUGIN_NAME " plugin: Array index out of bounds: reason=%d", reason);
    return;
  }

  const char *reason_str = domain_reasons[state][reason];
  /* Array size for domain reasons is fixed, but different domain states can
   * have different number of reasons. We need to check if reason was
   * successfully parsed */
  if (!reason_str) {
    ERROR(PLUGIN_NAME " plugin: Invalid reason (%d) for domain state: %s",
          reason, state_str);
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
    ERROR(PLUGIN_NAME " plugin: Unrecognized domain state (%d)", state);
    return;
  }
  submit_notif(dom, severity, msg, "domain_state", NULL);
}

static int lv_init_ignorelists() {
  if (il_domains == NULL)
    il_domains = ignorelist_create(1);
  if (il_block_devices == NULL)
    il_block_devices = ignorelist_create(1);
  if (il_interface_devices == NULL)
    il_interface_devices = ignorelist_create(1);

  if (!il_domains || !il_block_devices || !il_interface_devices)
    return 1;

  return 0;
}

/* Validates config option that may take multiple strings arguments.
 * Returns 0 on success, -1 otherwise */
static int check_config_multiple_string_entry(const oconfig_item_t *ci) {
  if (ci == NULL) {
    ERROR(PLUGIN_NAME " plugin: ci oconfig_item can't be NULL");
    return -1;
  }

  if (ci->values_num < 1) {
    ERROR(PLUGIN_NAME
          " plugin: the '%s' option requires at least one string argument",
          ci->key);
    return -1;
  }

  for (int i = 0; i < ci->values_num; ++i) {
    if (ci->values[i].type != OCONFIG_TYPE_STRING) {
      ERROR(PLUGIN_NAME
            " plugin: one of the '%s' options is not a valid string",
            ci->key);
      return -1;
    }
  }

  return 0;
}

static int lv_config(oconfig_item_t *ci) {
  if (lv_init_ignorelists() != 0) {
    ERROR(PLUGIN_NAME " plugin: lv_init_ignorelist failed.");
    return -1;
  }

  for (int i = 0; i < ci->children_num; ++i) {
    oconfig_item_t *c = ci->children + i;

    if (strcasecmp(c->key, "Connection") == 0) {
      if (cf_util_get_string(c, &conn_string) != 0 || conn_string == NULL)
        return -1;

      continue;
    } else if (strcasecmp(c->key, "RefreshInterval") == 0) {
      if (cf_util_get_int(c, &interval) != 0)
        return -1;

      continue;
    } else if (strcasecmp(c->key, "Domain") == 0) {
      char *domain_name = NULL;
      if (cf_util_get_string(c, &domain_name) != 0)
        return -1;

      if (ignorelist_add(il_domains, domain_name)) {
        ERROR(PLUGIN_NAME " plugin: Adding '%s' to domain-ignorelist failed",
              domain_name);
        sfree(domain_name);
        return -1;
      }

      sfree(domain_name);
      continue;
    } else if (strcasecmp(c->key, "BlockDevice") == 0) {
      char *device_name = NULL;
      if (cf_util_get_string(c, &device_name) != 0)
        return -1;

      if (ignorelist_add(il_block_devices, device_name) != 0) {
        ERROR(PLUGIN_NAME
              " plugin: Adding '%s' to block-device-ignorelist failed",
              device_name);
        sfree(device_name);
        return -1;
      }

      sfree(device_name);
      continue;
    } else if (strcasecmp(c->key, "BlockDeviceFormat") == 0) {
      char *device_format = NULL;
      if (cf_util_get_string(c, &device_format) != 0)
        return -1;

      if (strcasecmp(device_format, "target") == 0)
        blockdevice_format = target;
      else if (strcasecmp(device_format, "source") == 0)
        blockdevice_format = source;
      else {
        ERROR(PLUGIN_NAME " plugin: unknown BlockDeviceFormat: %s",
              device_format);
        sfree(device_format);
        return -1;
      }

      sfree(device_format);
      continue;
    } else if (strcasecmp(c->key, "BlockDeviceFormatBasename") == 0) {
      if (cf_util_get_boolean(c, &blockdevice_format_basename) != 0)
        return -1;

      continue;
    } else if (strcasecmp(c->key, "InterfaceDevice") == 0) {
      char *interface_name = NULL;
      if (cf_util_get_string(c, &interface_name) != 0)
        return -1;

      if (ignorelist_add(il_interface_devices, interface_name)) {
        ERROR(PLUGIN_NAME " plugin: Adding '%s' to interface-ignorelist failed",
              interface_name);
        sfree(interface_name);
        return -1;
      }

      sfree(interface_name);
      continue;
    } else if (strcasecmp(c->key, "IgnoreSelected") == 0) {
      bool ignore_selected = false;
      if (cf_util_get_boolean(c, &ignore_selected) != 0)
        return -1;

      if (ignore_selected) {
        ignorelist_set_invert(il_domains, 0);
        ignorelist_set_invert(il_block_devices, 0);
        ignorelist_set_invert(il_interface_devices, 0);
      } else {
        ignorelist_set_invert(il_domains, 1);
        ignorelist_set_invert(il_block_devices, 1);
        ignorelist_set_invert(il_interface_devices, 1);
      }

      continue;
    } else if (strcasecmp(c->key, "HostnameMetadataNS") == 0) {
      if (cf_util_get_string(c, &hm_ns) != 0)
        return -1;

      continue;
    } else if (strcasecmp(c->key, "HostnameMetadataXPath") == 0) {
      if (cf_util_get_string(c, &hm_xpath) != 0)
        return -1;

      continue;
    } else if (strcasecmp(c->key, "HostnameFormat") == 0) {
      /* this option can take multiple strings arguments in one config line*/
      if (check_config_multiple_string_entry(c) != 0) {
        ERROR(PLUGIN_NAME " plugin: Could not get 'HostnameFormat' parameter");
        return -1;
      }

      const int params_num = c->values_num;
      for (int i = 0; i < params_num; ++i) {
        const char *param_name = c->values[i].value.string;
        if (strcasecmp(param_name, "hostname") == 0)
          hostname_format[i] = hf_hostname;
        else if (strcasecmp(param_name, "name") == 0)
          hostname_format[i] = hf_name;
        else if (strcasecmp(param_name, "uuid") == 0)
          hostname_format[i] = hf_uuid;
        else if (strcasecmp(param_name, "metadata") == 0)
          hostname_format[i] = hf_metadata;
        else {
          ERROR(PLUGIN_NAME " plugin: unknown HostnameFormat field: %s",
                param_name);
          return -1;
        }
      }

      for (int i = params_num; i < HF_MAX_FIELDS; ++i)
        hostname_format[i] = hf_none;

      continue;
    } else if (strcasecmp(c->key, "PluginInstanceFormat") == 0) {
      /* this option can handle list of string parameters in one line*/
      if (check_config_multiple_string_entry(c) != 0) {
        ERROR(PLUGIN_NAME
              " plugin: Could not get 'PluginInstanceFormat' parameter");
        return -1;
      }

      const int params_num = c->values_num;
      for (int i = 0; i < params_num; ++i) {
        const char *param_name = c->values[i].value.string;
        if (strcasecmp(param_name, "none") == 0) {
          plugin_instance_format[i] = plginst_none;
          break;
        } else if (strcasecmp(param_name, "name") == 0)
          plugin_instance_format[i] = plginst_name;
        else if (strcasecmp(param_name, "uuid") == 0)
          plugin_instance_format[i] = plginst_uuid;
        else if (strcasecmp(param_name, "metadata") == 0)
          plugin_instance_format[i] = plginst_metadata;
        else {
          ERROR(PLUGIN_NAME " plugin: unknown PluginInstanceFormat field: %s",
                param_name);

          return -1;
        }
      }

      for (int i = params_num; i < PLGINST_MAX_FIELDS; ++i)
        plugin_instance_format[i] = plginst_none;

      continue;
    } else if (strcasecmp(c->key, "InterfaceFormat") == 0) {
      char *format = NULL;
      if (cf_util_get_string(c, &format) != 0)
        return -1;

      if (strcasecmp(format, "name") == 0)
        interface_format = if_name;
      else if (strcasecmp(format, "address") == 0)
        interface_format = if_address;
      else if (strcasecmp(format, "number") == 0)
        interface_format = if_number;
      else {
        ERROR(PLUGIN_NAME " plugin: unknown InterfaceFormat: %s", format);
        sfree(format);
        return -1;
      }

      sfree(format);
      continue;
    } else if (strcasecmp(c->key, "Instances") == 0) {
      if (cf_util_get_int(c, &nr_instances) != 0)
        return -1;

      if (nr_instances <= 0) {
        ERROR(PLUGIN_NAME " plugin: Instances <= 0 makes no sense.");
        return -1;
      }
      if (nr_instances > NR_INSTANCES_MAX) {
        ERROR(PLUGIN_NAME " plugin: Instances=%i > NR_INSTANCES_MAX=%i"
                          " use a lower setting or recompile the plugin.",
              nr_instances, NR_INSTANCES_MAX);
        return -1;
      }

      DEBUG(PLUGIN_NAME " plugin: configured %i instances", nr_instances);
      continue;
    } else if (strcasecmp(c->key, "ExtraStats") == 0) {
      char *ex_str = NULL;

      if (cf_util_get_string(c, &ex_str) != 0)
        return -1;

      char *exstats[EX_STATS_MAX_FIELDS];
      int numexstats = strsplit(ex_str, exstats, STATIC_ARRAY_SIZE(exstats));
      int status = parse_ex_stats_flags(&extra_stats, exstats, numexstats);
      sfree(ex_str);
      if (status != 0) {
        ERROR(PLUGIN_NAME " plugin: parsing 'ExtraStats' option failed");
        return status;
      }

#ifdef HAVE_JOB_STATS
      if ((extra_stats & ex_stats_job_stats_completed) &&
          (extra_stats & ex_stats_job_stats_background)) {
        ERROR(PLUGIN_NAME " plugin: Invalid job stats configuration. Only one "
                          "type of job statistics can be collected at the same "
                          "time");
        return -1;
      }
#endif

      /* ExtraStats parsed successfully */
      continue;
    } else if (strcasecmp(c->key, "PersistentNotification") == 0) {
      if (cf_util_get_boolean(c, &persistent_notification) != 0)
        return -1;

      continue;
    } else if (strcasecmp(c->key, "ReportBlockDevices") == 0) {
      if (cf_util_get_boolean(c, &report_block_devices) != 0)
        return -1;

      continue;
    } else if (strcasecmp(c->key, "ReportNetworkInterfaces") == 0) {
      if (cf_util_get_boolean(c, &report_network_interfaces) != 0)
        return -1;

      continue;
    } else {
      /* Unrecognised option. */
      ERROR(PLUGIN_NAME " plugin: Unrecognized option: '%s'", c->key);
      return -1;
    }
  }

  return 0;
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
      ERROR(PLUGIN_NAME " plugin: virNodeGetInfo failed");
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

static int lv_domain_block_stats(virDomainPtr dom, const char *path,
                                 struct lv_block_stats *bstats) {
#ifdef HAVE_BLOCK_STATS_FLAGS
  int nparams = 0;
  if (virDomainBlockStatsFlags(dom, path, NULL, &nparams, 0) < 0 ||
      nparams <= 0) {
    VIRT_ERROR(conn, "getting the disk params count");
    return -1;
  }

  virTypedParameterPtr params = calloc(nparams, sizeof(*params));
  if (params == NULL) {
    ERROR("virt plugin: alloc(%i) for block=%s parameters failed.", nparams,
          path);
    return -1;
  }

  int rc = -1;
  if (virDomainBlockStatsFlags(dom, path, params, &nparams, 0) < 0) {
    VIRT_ERROR(conn, "getting the disk params values");
  } else {
    rc = get_block_stats(bstats, params, nparams);
  }

  virTypedParamsClear(params, nparams);
  sfree(params);
  return rc;
#else
  return virDomainBlockStats(dom, path, &(bstats->bi), sizeof(bstats->bi));
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
    bool is_set = VIR_CPU_USABLE(cpu_maps, cpu_map_len, vcpu, cpu);

    snprintf(type_instance, sizeof(type_instance), "vcpu_%d-cpu_%d", vcpu, cpu);
    submit(dom, "cpu_affinity", type_instance, &(value_t){.gauge = is_set}, 1);
  }
}

static int get_vcpu_stats(virDomainPtr domain, unsigned short nr_virt_cpu) {
  int max_cpus = VIR_NODEINFO_MAXCPUS(nodeinfo);
  int cpu_map_len = VIR_CPU_MAPLEN(max_cpus);

  virVcpuInfoPtr vinfo = calloc(nr_virt_cpu, sizeof(*vinfo));
  if (vinfo == NULL) {
    ERROR(PLUGIN_NAME " plugin: calloc failed.");
    return -1;
  }

  unsigned char *cpumaps = calloc(nr_virt_cpu, cpu_map_len);
  if (cpumaps == NULL) {
    ERROR(PLUGIN_NAME " plugin: calloc failed.");
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

#ifdef HAVE_CPU_STATS
static int get_pcpu_stats(virDomainPtr dom) {
  int nparams = virDomainGetCPUStats(dom, NULL, 0, -1, 1, 0);
  if (nparams < 0) {
    VIRT_ERROR(conn, "getting the CPU params count");
    return -1;
  }

  virTypedParameterPtr param = calloc(nparams, sizeof(*param));
  if (param == NULL) {
    ERROR(PLUGIN_NAME " plugin: alloc(%i) for cpu parameters failed.", nparams);
    return -1;
  }

  int ret = virDomainGetCPUStats(dom, param, nparams, -1, 1, 0); // total stats.
  if (ret < 0) {
    virTypedParamsClear(param, nparams);
    sfree(param);
    VIRT_ERROR(conn, "getting the CPU params values");
    return -1;
  }

  unsigned long long total_user_cpu_time = 0;
  unsigned long long total_syst_cpu_time = 0;

  for (int i = 0; i < nparams; ++i) {
    if (!strcmp(param[i].field, "user_time"))
      total_user_cpu_time = param[i].value.ul;
    else if (!strcmp(param[i].field, "system_time"))
      total_syst_cpu_time = param[i].value.ul;
  }

  if (total_user_cpu_time > 0 || total_syst_cpu_time > 0)
    submit_derive2("ps_cputime", total_user_cpu_time, total_syst_cpu_time, dom,
                   NULL);

  virTypedParamsClear(param, nparams);
  sfree(param);

  return 0;
}
#endif /* HAVE_CPU_STATS */

#ifdef HAVE_DOM_REASON

static void domain_state_submit(virDomainPtr dom, int state, int reason) {
  value_t values[] = {
      {.gauge = (gauge_t)state}, {.gauge = (gauge_t)reason},
  };

  submit(dom, "domain_state", NULL, values, STATIC_ARRAY_SIZE(values));
}

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

#ifdef HAVE_LIST_ALL_DOMAINS
static int get_domain_state_notify(virDomainPtr domain) {
  int domain_state = 0;
  int domain_reason = 0;

  int status = virDomainGetState(domain, &domain_state, &domain_reason, 0);
  if (status != 0) {
    ERROR(PLUGIN_NAME " plugin: virDomainGetState failed with status %i.",
          status);
    return status;
  }

  if (persistent_notification)
    domain_state_submit_notif(domain, domain_state, domain_reason);

  return status;
}
#endif /* HAVE_LIST_ALL_DOMAINS */
#endif /* HAVE_DOM_REASON */

static int get_memory_stats(virDomainPtr domain) {
  virDomainMemoryStatPtr minfo =
      calloc(VIR_DOMAIN_MEMORY_STAT_NR, sizeof(*minfo));
  if (minfo == NULL) {
    ERROR("virt plugin: calloc failed.");
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

static int get_block_device_stats(struct block_device *block_dev) {
  if (!block_dev) {
    ERROR(PLUGIN_NAME " plugin: get_block_stats NULL pointer");
    return -1;
  }

  virDomainBlockInfo binfo;
  init_block_info(&binfo);

  /* Fetching block info stats only if needed*/
  if (extra_stats & (ex_stats_disk_allocation | ex_stats_disk_capacity |
                     ex_stats_disk_physical)) {
    /* Block info statistics can be only fetched from devices with 'source'
     * defined */
    if (block_dev->has_source) {
      if (virDomainGetBlockInfo(block_dev->dom, block_dev->path, &binfo, 0) <
          0) {
        ERROR(PLUGIN_NAME " plugin: virDomainGetBlockInfo failed for path: %s",
              block_dev->path);
        return -1;
      }
    }
  }

  struct lv_block_stats bstats;
  init_block_stats(&bstats);

  if (lv_domain_block_stats(block_dev->dom, block_dev->path, &bstats) < 0) {
    ERROR(PLUGIN_NAME " plugin: lv_domain_block_stats failed");
    return -1;
  }

  disk_block_stats_submit(&bstats, block_dev->dom, block_dev->path, &binfo);
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
    for (size_t _i = 0; _i < _size; ++_i) {                                    \
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

  for (size_t i = 0; i < fs_info->ndevAlias; ++i) {
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
  if (!domain || !domain->ptr) {
    ERROR(PLUGIN_NAME " plugin: get_domain_metrics: NULL pointer");
    return -1;
  }

  virDomainInfo info;
  int status = virDomainGetInfo(domain->ptr, &info);
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
#endif
  }

  /* Gather remaining stats only for running domains */
  if (info.state != VIR_DOMAIN_RUNNING)
    return 0;

#ifdef HAVE_CPU_STATS
  if (extra_stats & ex_stats_pcpu)
    get_pcpu_stats(domain->ptr);
#endif

  cpu_submit(domain, info.cpuTime);

  memory_submit(domain->ptr, (gauge_t)info.memory * 1024);

  GET_STATS(get_vcpu_stats, "vcpu stats", domain->ptr, info.nrVirtCpu);
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
  memcpy(&domain->info, &info, sizeof(domain->info));

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

static int domain_lifecycle_event_cb(__attribute__((unused)) virConnectPtr con_,
                                     virDomainPtr dom, int event, int detail,
                                     __attribute__((unused)) void *opaque) {
  int domain_state = map_domain_event_to_state(event);
  int domain_reason = 0; /* 0 means UNKNOWN reason for any state */
#ifdef HAVE_DOM_REASON
  domain_reason = map_domain_event_detail_to_reason(event, detail);
#endif
  domain_state_submit_notif(dom, domain_state, domain_reason);

  return 0;
}

static int register_event_impl(void) {
  if (virEventRegisterDefaultImpl() < 0) {
    virErrorPtr err = virGetLastError();
    ERROR(PLUGIN_NAME
          " plugin: error while event implementation registering: %s",
          err && err->message ? err->message : "Unknown error");
    return -1;
  }

  return 0;
}

static void virt_notif_thread_set_active(virt_notif_thread_t *thread_data,
                                         const bool active) {
  assert(thread_data != NULL);
  pthread_mutex_lock(&thread_data->active_mutex);
  thread_data->is_active = active;
  pthread_mutex_unlock(&thread_data->active_mutex);
}

static bool virt_notif_thread_is_active(virt_notif_thread_t *thread_data) {
  bool active = false;

  assert(thread_data != NULL);
  pthread_mutex_lock(&thread_data->active_mutex);
  active = thread_data->is_active;
  pthread_mutex_unlock(&thread_data->active_mutex);

  return active;
}

/* worker function running default event implementation */
static void *event_loop_worker(void *arg) {
  virt_notif_thread_t *thread_data = (virt_notif_thread_t *)arg;

  while (virt_notif_thread_is_active(thread_data)) {
    if (virEventRunDefaultImpl() < 0) {
      virErrorPtr err = virGetLastError();
      ERROR(PLUGIN_NAME " plugin: failed to run event loop: %s\n",
            err && err->message ? err->message : "Unknown error");
    }
  }

  return NULL;
}

static int virt_notif_thread_init(virt_notif_thread_t *thread_data) {
  int ret;

  assert(thread_data != NULL);
  ret = pthread_mutex_init(&thread_data->active_mutex, NULL);
  if (ret != 0) {
    ERROR(PLUGIN_NAME " plugin: Failed to initialize mutex, err %u", ret);
    return ret;
  }

  /**
   * '0' and positive integers are meaningful ID's, therefore setting
   * domain_event_cb_id to '-1'
   */
  thread_data->domain_event_cb_id = -1;
  pthread_mutex_lock(&thread_data->active_mutex);
  thread_data->is_active = false;
  pthread_mutex_unlock(&thread_data->active_mutex);

  return 0;
}

/* register domain event callback and start event loop thread */
static int start_event_loop(virt_notif_thread_t *thread_data) {
  assert(thread_data != NULL);
  thread_data->domain_event_cb_id = virConnectDomainEventRegisterAny(
      conn, NULL, VIR_DOMAIN_EVENT_ID_LIFECYCLE,
      VIR_DOMAIN_EVENT_CALLBACK(domain_lifecycle_event_cb), NULL, NULL);
  if (thread_data->domain_event_cb_id == -1) {
    ERROR(PLUGIN_NAME " plugin: error while callback registering");
    return -1;
  }

  DEBUG(PLUGIN_NAME " plugin: starting event loop");

  virt_notif_thread_set_active(thread_data, 1);
  if (pthread_create(&thread_data->event_loop_tid, NULL, event_loop_worker,
                     thread_data)) {
    ERROR(PLUGIN_NAME " plugin: failed event loop thread creation");
    virt_notif_thread_set_active(thread_data, 0);
    virConnectDomainEventDeregisterAny(conn, thread_data->domain_event_cb_id);
    thread_data->domain_event_cb_id = -1;
    return -1;
  }

  return 0;
}

/* stop event loop thread and deregister callback */
static void stop_event_loop(virt_notif_thread_t *thread_data) {

  DEBUG(PLUGIN_NAME " plugin: stopping event loop");

  /* Stopping loop */
  if (virt_notif_thread_is_active(thread_data)) {
    virt_notif_thread_set_active(thread_data, 0);
    if (pthread_join(notif_thread.event_loop_tid, NULL) != 0)
      ERROR(PLUGIN_NAME " plugin: stopping notification thread failed");
  }

  /* ... and de-registering event handler */
  if (conn != NULL && thread_data->domain_event_cb_id != -1) {
    virConnectDomainEventDeregisterAny(conn, thread_data->domain_event_cb_id);
    thread_data->domain_event_cb_id = -1;
  }
}

static int persistent_domains_state_notification(void) {
  int status = 0;
  int n;
#ifdef HAVE_LIST_ALL_DOMAINS
  virDomainPtr *domains = NULL;
  n = virConnectListAllDomains(conn, &domains,
                               VIR_CONNECT_LIST_DOMAINS_PERSISTENT);
  if (n < 0) {
    VIRT_ERROR(conn, "reading list of persistent domains");
    status = -1;
  } else {
    DEBUG(PLUGIN_NAME " plugin: getting state of %i persistent domains", n);
    /* Fetch each persistent domain's state and notify it */
    int n_notified = n;
    for (int i = 0; i < n; ++i) {
      status = get_domain_state_notify(domains[i]);
      if (status != 0) {
        n_notified--;
        ERROR(PLUGIN_NAME " plugin: could not notify state of domain %s",
              virDomainGetName(domains[i]));
      }
      virDomainFree(domains[i]);
    }

    sfree(domains);
    DEBUG(PLUGIN_NAME " plugin: notified state of %i persistent domains",
          n_notified);
  }
#else
  n = virConnectNumOfDomains(conn);
  if (n > 0) {
    int *domids;
    /* Get list of domains. */
    domids = calloc(n, sizeof(*domids));
    if (domids == NULL) {
      ERROR(PLUGIN_NAME " plugin: calloc failed.");
      return -1;
    }
    n = virConnectListDomains(conn, domids, n);
    if (n < 0) {
      VIRT_ERROR(conn, "reading list of domains");
      sfree(domids);
      return -1;
    }
    /* Fetch info of each active domain and notify it */
    for (int i = 0; i < n; ++i) {
      virDomainInfo info;
      virDomainPtr dom = NULL;
      dom = virDomainLookupByID(conn, domids[i]);
      if (dom == NULL) {
        VIRT_ERROR(conn, "virDomainLookupByID");
        /* Could be that the domain went away -- ignore it anyway. */
        continue;
      }
      status = virDomainGetInfo(dom, &info);
      if (status == 0)
        /* virDomainGetState is not available. Submit 0, which corresponds to
         * unknown reason. */
        domain_state_submit_notif(dom, info.state, 0);
      else
        ERROR(PLUGIN_NAME " plugin: virDomainGetInfo failed with status %i.",
              status);

      virDomainFree(dom);
    }
    sfree(domids);
  }
#endif

  return status;
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

  bool reconnect = conn == NULL ? true : false;
  /* event implementation must be registered before connection is opened */
  if (inst->id == 0) {
    if (!persistent_notification && reconnect)
      if (register_event_impl() != 0)
        return -1;

    if (lv_connect() < 0)
      return -1;

    if (!persistent_notification && reconnect && conn != NULL)
      if (start_event_loop(&notif_thread) != 0)
        return -1;
  }

  time(&t);

  /* Need to refresh domain or device lists? */
  if ((last_refresh == (time_t)0) ||
      ((interval > 0) && ((last_refresh + interval) <= t))) {
    if (refresh_lists(inst) != 0) {
      if (inst->id == 0) {
        if (!persistent_notification)
          stop_event_loop(&notif_thread);
        lv_disconnect();
      }
      return -1;
    }
    last_refresh = t;
  }

  /* persistent domains state notifications are handled by instance 0 */
  if (inst->id == 0 && persistent_notification) {
    int status = persistent_domains_state_notification();
    if (status != 0)
      DEBUG(PLUGIN_NAME " plugin: persistent_domains_state_notifications "
                        "returned with status %i",
            status);
  }

#if COLLECT_DEBUG
  for (int i = 0; i < state->nr_domains; ++i)
    DEBUG(PLUGIN_NAME " plugin: domain %s",
          virDomainGetName(state->domains[i].ptr));
  for (int i = 0; i < state->nr_block_devices; ++i)
    DEBUG(PLUGIN_NAME " plugin: block device %d %s:%s", i,
          virDomainGetName(state->block_devices[i].dom),
          state->block_devices[i].path);
  for (int i = 0; i < state->nr_interface_devices; ++i)
    DEBUG(PLUGIN_NAME " plugin: interface device %d %s:%s", i,
          virDomainGetName(state->interface_devices[i].dom),
          state->interface_devices[i].path);
#endif

  /* Get domains' metrics */
  for (int i = 0; i < state->nr_domains; ++i) {
    domain_t *dom = &state->domains[i];
    int status = 0;
    if (dom->active)
      status = get_domain_metrics(dom);
#ifdef HAVE_DOM_REASON
    else
      status = get_domain_state(dom->ptr);
#endif

    if (status != 0)
      ERROR(PLUGIN_NAME " plugin: failed to get metrics for domain=%s",
            virDomainGetName(dom->ptr));
  }

  /* Get block device stats for each domain. */
  for (int i = 0; i < state->nr_block_devices; ++i) {
    int status = get_block_device_stats(&state->block_devices[i]);
    if (status != 0)
      ERROR(PLUGIN_NAME
            " plugin: failed to get stats for block device (%s) in domain %s",
            state->block_devices[i].path,
            virDomainGetName(state->block_devices[i].dom));
  }

  /* Get interface stats for each domain. */
  for (int i = 0; i < state->nr_interface_devices; ++i) {
    int status = get_if_dev_stats(&state->interface_devices[i]);
    if (status != 0)
      ERROR(
          PLUGIN_NAME
          " plugin: failed to get interface stats for device (%s) in domain %s",
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

  /* Init ignorelists if there was no explicit configuration */
  if (lv_init_ignorelists() != 0)
    return -1;

  /* event implementation must be registered before connection is opened */
  if (!persistent_notification)
    if (register_event_impl() != 0)
      return -1;

  if (lv_connect() != 0)
    return -1;

  if (!persistent_notification) {
    virt_notif_thread_init(&notif_thread);
    if (start_event_loop(&notif_thread) != 0)
      return -1;
  }

  DEBUG(PLUGIN_NAME " plugin: starting %i instances", nr_instances);

  for (int i = 0; i < nr_instances; ++i)
    if (lv_init_instance(i, lv_read) != 0)
      return -1;

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

static void lv_add_block_devices(struct lv_read_state *state, virDomainPtr dom,
                                 const char *domname,
                                 xmlXPathContextPtr xpath_ctx) {
  xmlXPathObjectPtr xpath_obj =
      xmlXPathEval((const xmlChar *)"/domain/devices/disk", xpath_ctx);

  if (xpath_obj == NULL) {
    DEBUG(PLUGIN_NAME " plugin: no disk xpath-object found for domain %s",
          domname);
    return;
  }

  if (xpath_obj->type != XPATH_NODESET || xpath_obj->nodesetval == NULL) {
    DEBUG(PLUGIN_NAME " plugin: no disk node found for domain %s", domname);
    goto cleanup;
  }

  xmlNodeSetPtr xml_block_devices = xpath_obj->nodesetval;
  for (int i = 0; i < xml_block_devices->nodeNr; ++i) {
    xmlNodePtr xml_device = xpath_obj->nodesetval->nodeTab[i];
    char *path_str = NULL;
    char *source_str = NULL;

    if (!xml_device)
      continue;

    /* Fetching path and source for block device */
    for (xmlNodePtr child = xml_device->children; child; child = child->next) {
      if (child->type != XML_ELEMENT_NODE)
        continue;

      /* we are interested only in either "target" or "source" elements */
      if (xmlStrEqual(child->name, (const xmlChar *)"target"))
        path_str = (char *)xmlGetProp(child, (const xmlChar *)"dev");
      else if (xmlStrEqual(child->name, (const xmlChar *)"source")) {
        /* name of the source is located in "dev" or "file" element (it depends
         * on type of source). Trying "dev" at first*/
        source_str = (char *)xmlGetProp(child, (const xmlChar *)"dev");
        if (!source_str)
          source_str = (char *)xmlGetProp(child, (const xmlChar *)"file");
      }
      /* ignoring any other element*/
    }

    /* source_str will be interpreted as a device path if blockdevice_format
     *  param is set to 'source'. */
    const char *device_path =
        (blockdevice_format == source) ? source_str : path_str;

    if (!device_path) {
      /* no path found and we can't add block_device without it */
      WARNING(PLUGIN_NAME " plugin: could not generate device path for disk in "
                          "domain %s - disk device will be ignored in reports",
              domname);
      goto cont;
    }

    if (ignore_device_match(il_block_devices, domname, device_path) == 0) {
      /* we only have to store information whether 'source' exists or not */
      bool has_source = (source_str != NULL) ? true : false;

      add_block_device(state, dom, device_path, has_source);
    }

  cont:
    if (path_str)
      xmlFree(path_str);

    if (source_str)
      xmlFree(source_str);
  }

cleanup:
  xmlXPathFreeObject(xpath_obj);
}

static void lv_add_network_interfaces(struct lv_read_state *state,
                                      virDomainPtr dom, const char *domname,
                                      xmlXPathContextPtr xpath_ctx) {
  xmlXPathObjectPtr xpath_obj = xmlXPathEval(
      (xmlChar *)"/domain/devices/interface[target[@dev]]", xpath_ctx);

  if (xpath_obj == NULL)
    return;

  if (xpath_obj->type != XPATH_NODESET || xpath_obj->nodesetval == NULL) {
    xmlXPathFreeObject(xpath_obj);
    return;
  }

  xmlNodeSetPtr xml_interfaces = xpath_obj->nodesetval;

  for (int j = 0; j < xml_interfaces->nodeNr; ++j) {
    char *path = NULL;
    char *address = NULL;
    const int itf_number = j + 1;

    xmlNodePtr xml_interface = xml_interfaces->nodeTab[j];
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

    bool device_ignored = false;
    switch (interface_format) {
    case if_name:
      if (ignore_device_match(il_interface_devices, domname, path) != 0)
        device_ignored = true;
      break;
    case if_address:
      if (ignore_device_match(il_interface_devices, domname, address) != 0)
        device_ignored = true;
      break;
    case if_number: {
      char number_string[4];
      snprintf(number_string, sizeof(number_string), "%d", itf_number);
      if (ignore_device_match(il_interface_devices, domname, number_string) !=
          0)
        device_ignored = true;
    } break;
    default:
      ERROR(PLUGIN_NAME " plugin: Unknown interface_format option: %d",
            interface_format);
    }

    if (!device_ignored)
      add_interface_device(state, dom, path, address, itf_number);

    if (path)
      xmlFree(path);
    if (address)
      xmlFree(address);
  }
  xmlXPathFreeObject(xpath_obj);
}

static bool is_domain_ignored(virDomainPtr dom) {
  const char *domname = virDomainGetName(dom);

  if (domname == NULL) {
    VIRT_ERROR(conn, "virDomainGetName failed, ignoring domain");
    return true;
  }

  if (ignorelist_match(il_domains, domname) != 0) {
    DEBUG(PLUGIN_NAME
          " plugin: ignoring domain '%s' because of ignorelist option",
          domname);
    return true;
  }

  return false;
}

static int refresh_lists(struct lv_read_instance *inst) {
  struct lv_read_state *state = &inst->read_state;
  int n;

#ifndef HAVE_LIST_ALL_DOMAINS
  n = virConnectNumOfDomains(conn);
  if (n < 0) {
    VIRT_ERROR(conn, "reading number of domains");
    return -1;
  }
#endif

  lv_clean_read_state(state);

#ifndef HAVE_LIST_ALL_DOMAINS
  if (n == 0)
    goto end;
#endif

#ifdef HAVE_LIST_ALL_DOMAINS
  virDomainPtr *domains, *domains_inactive;
  int m = virConnectListAllDomains(conn, &domains_inactive,
                                   VIR_CONNECT_LIST_DOMAINS_INACTIVE);
  n = virConnectListAllDomains(conn, &domains, VIR_CONNECT_LIST_DOMAINS_ACTIVE);
#else
  /* Get list of domains. */
  int *domids = calloc(n, sizeof(*domids));
  if (domids == NULL) {
    ERROR(PLUGIN_NAME " plugin: calloc failed.");
    return -1;
  }

  n = virConnectListDomains(conn, domids, n);
#endif

  if (n < 0) {
    VIRT_ERROR(conn, "reading list of domains");
#ifndef HAVE_LIST_ALL_DOMAINS
    sfree(domids);
#else
    for (int i = 0; i < m; ++i)
      virDomainFree(domains_inactive[i]);
    sfree(domains_inactive);
#endif
    return -1;
  }

#ifdef HAVE_LIST_ALL_DOMAINS
  for (int i = 0; i < m; ++i)
    if (is_domain_ignored(domains_inactive[i]) ||
        add_domain(state, domains_inactive[i], 0) < 0) {
      /* domain ignored or failed during adding to domains list*/
      virDomainFree(domains_inactive[i]);
      domains_inactive[i] = NULL;
      continue;
    }
#endif

  /* Fetch each domain and add it to the list, unless ignore. */
  for (int i = 0; i < n; ++i) {

#ifdef HAVE_LIST_ALL_DOMAINS
    virDomainPtr dom = domains[i];
#else
    virDomainPtr dom = virDomainLookupByID(conn, domids[i]);
    if (dom == NULL) {
      VIRT_ERROR(conn, "virDomainLookupByID");
      /* Could be that the domain went away -- ignore it anyway. */
      continue;
    }
#endif

    if (is_domain_ignored(dom) || add_domain(state, dom, 1) < 0) {
      /*
       * domain ignored or failed during adding to domains list
       *
       * When domain is already tracked, then there is
       * no problem with memory handling (will be freed
       * with the rest of domains cached data)
       * But in case of error like this (error occurred
       * before adding domain to track) we have to take
       * care it ourselves and call virDomainFree
       */
      virDomainFree(dom);
      continue;
    }

    const char *domname = virDomainGetName(dom);
    if (domname == NULL) {
      VIRT_ERROR(conn, "virDomainGetName");
      continue;
    }

    virDomainInfo info;
    int status = virDomainGetInfo(dom, &info);
    if (status != 0) {
      ERROR(PLUGIN_NAME " plugin: virDomainGetInfo failed with status %i.",
            status);
      continue;
    }

    if (info.state != VIR_DOMAIN_RUNNING) {
      DEBUG(PLUGIN_NAME " plugin: skipping inactive domain %s", domname);
      continue;
    }

    /* Get a list of devices for this domain. */
    xmlDocPtr xml_doc = NULL;
    xmlXPathContextPtr xpath_ctx = NULL;

    char *xml = virDomainGetXMLDesc(dom, 0);
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

    char tag[PARTITION_TAG_MAX_LEN] = {'\0'};
    if (lv_domain_get_tag(xpath_ctx, domname, tag) < 0) {
      ERROR(PLUGIN_NAME " plugin: lv_domain_get_tag failed.");
      goto cont;
    }

    if (!lv_instance_include_domain(inst, domname, tag))
      goto cont;

    /* Block devices. */
    if (report_block_devices)
      lv_add_block_devices(state, dom, domname, xpath_ctx);

    /* Network interfaces. */
    if (report_network_interfaces)
      lv_add_network_interfaces(state, dom, domname, xpath_ctx);

  cont:
    if (xpath_ctx)
      xmlXPathFreeContext(xpath_ctx);
    if (xml_doc)
      xmlFreeDoc(xml_doc);
    sfree(xml);
  }

#ifdef HAVE_LIST_ALL_DOMAINS
  /* NOTE: domains_active and domains_inactive data will be cleared during
     refresh of all domains (inside lv_clean_read_state function) so we need
     to free here only allocated arrays */
  sfree(domains);
  sfree(domains_inactive);
#else
  sfree(domids);

end:
#endif

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

static int add_domain(struct lv_read_state *state, virDomainPtr dom,
                      bool active) {
  int new_size = sizeof(state->domains[0]) * (state->nr_domains + 1);

  domain_t *new_ptr = realloc(state->domains, new_size);
  if (new_ptr == NULL) {
    ERROR(PLUGIN_NAME " plugin: realloc failed in add_domain()");
    return -1;
  }

  state->domains = new_ptr;
  state->domains[state->nr_domains].ptr = dom;
  state->domains[state->nr_domains].active = active;
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
                            const char *path, bool has_source) {

  char *path_copy = strdup(path);
  if (!path_copy)
    return -1;

  int new_size =
      sizeof(state->block_devices[0]) * (state->nr_block_devices + 1);

  struct block_device *new_ptr = realloc(state->block_devices, new_size);
  if (new_ptr == NULL) {
    sfree(path_copy);
    return -1;
  }
  state->block_devices = new_ptr;
  state->block_devices[state->nr_block_devices].dom = dom;
  state->block_devices[state->nr_block_devices].path = path_copy;
  state->block_devices[state->nr_block_devices].has_source = has_source;
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

  if ((path == NULL) || (address == NULL))
    return EINVAL;

  char *path_copy = strdup(path);
  if (!path_copy)
    return -1;

  char *address_copy = strdup(address);
  if (!address_copy) {
    sfree(path_copy);
    return -1;
  }

  char number_string[21];
  snprintf(number_string, sizeof(number_string), "interface-%u", number);
  char *number_copy = strdup(number_string);
  if (!number_copy) {
    sfree(path_copy);
    sfree(address_copy);
    return -1;
  }

  int new_size =
      sizeof(state->interface_devices[0]) * (state->nr_interface_devices + 1);

  struct interface_device *new_ptr =
      realloc(state->interface_devices, new_size);
  if (new_ptr == NULL) {
    sfree(path_copy);
    sfree(address_copy);
    sfree(number_copy);
    return -1;
  }

  state->interface_devices = new_ptr;
  state->interface_devices[state->nr_interface_devices].dom = dom;
  state->interface_devices[state->nr_interface_devices].path = path_copy;
  state->interface_devices[state->nr_interface_devices].address = address_copy;
  state->interface_devices[state->nr_interface_devices].number = number_copy;
  return state->nr_interface_devices++;
}

static int ignore_device_match(ignorelist_t *il, const char *domname,
                               const char *devpath) {
  if ((domname == NULL) || (devpath == NULL))
    return 0;

  size_t n = strlen(domname) + strlen(devpath) + 2;
  char *name = malloc(n);
  if (name == NULL) {
    ERROR(PLUGIN_NAME " plugin: malloc failed.");
    return 0;
  }
  snprintf(name, n, "%s:%s", domname, devpath);
  int r = ignorelist_match(il, name);
  sfree(name);
  return r;
}

static int lv_shutdown(void) {
  for (int i = 0; i < nr_instances; ++i) {
    lv_fini_instance(i);
  }

  if (!persistent_notification)
    stop_event_loop(&notif_thread);

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
  plugin_register_complex_config("virt", lv_config);
  plugin_register_init(PLUGIN_NAME, lv_init);
  plugin_register_shutdown(PLUGIN_NAME, lv_shutdown);
}
