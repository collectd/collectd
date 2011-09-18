/**
 * collectd - src/vmware.c
 * Copyright (C) 2010  Edward Muller
 * Copyright (C) 2011  Keith Chambers
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; only version 2.1 of the License is
 * applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *   Edward Muller <emuller at engineyard.com>
 *   Keith Chambers <chambers_keith at yahoo.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include <dlfcn.h>

#include <vmGuestLib.h>

/* functions to dynamically load from the GuestLib library */
static char const * (*GuestLib_GetErrorText)(VMGuestLibError);
static VMGuestLibError (*GuestLib_OpenHandle)(VMGuestLibHandle*);
static VMGuestLibError (*GuestLib_CloseHandle)(VMGuestLibHandle);
static VMGuestLibError (*GuestLib_UpdateInfo)(VMGuestLibHandle handle);
static VMGuestLibError (*GuestLib_GetSessionId)(VMGuestLibHandle handle, VMSessionId *id);
static VMGuestLibError (*GuestLib_GetElapsedMs)(VMGuestLibHandle handle, uint64_t *elapsedMs);
static VMGuestLibError (*GuestLib_GetCpuUsedMs)(VMGuestLibHandle handle, uint64_t *cpuUsedMs);
static VMGuestLibError (*GuestLib_GetCpuStolenMs)(VMGuestLibHandle handle, uint64_t *cpuStolenMs);
static VMGuestLibError (*GuestLib_GetCpuReservationMHz)(VMGuestLibHandle handle, uint32_t *cpuReservationMHz);
static VMGuestLibError (*GuestLib_GetCpuLimitMHz)(VMGuestLibHandle handle, uint32_t *cpuLimitMHz);
static VMGuestLibError (*GuestLib_GetCpuShares)(VMGuestLibHandle handle, uint32_t *cpuShares);
static VMGuestLibError (*GuestLib_GetHostProcessorSpeed)(VMGuestLibHandle handle, uint32_t *mhz);
static VMGuestLibError (*GuestLib_GetMemUsedMB)(VMGuestLibHandle handle, uint32_t *memUsedMB);
static VMGuestLibError (*GuestLib_GetMemMappedMB)(VMGuestLibHandle handle, uint32_t *memMappedMB);
static VMGuestLibError (*GuestLib_GetMemActiveMB)(VMGuestLibHandle handle, uint32_t *memActiveMB);
static VMGuestLibError (*GuestLib_GetMemTargetSizeMB)(VMGuestLibHandle handle, uint64_t *memTargetSizeMB);
static VMGuestLibError (*GuestLib_GetMemOverheadMB)(VMGuestLibHandle handle, uint32_t *memOverheadMB);
static VMGuestLibError (*GuestLib_GetMemSharedMB)(VMGuestLibHandle handle, uint32_t *memSharedMB);
static VMGuestLibError (*GuestLib_GetMemSharedSavedMB)(VMGuestLibHandle handle, uint32_t *memSharedSavedMB);
static VMGuestLibError (*GuestLib_GetMemBalloonedMB)(VMGuestLibHandle handle, uint32_t *memBalloonedMB);
static VMGuestLibError (*GuestLib_GetMemSwappedMB)(VMGuestLibHandle handle, uint32_t *memSwappedMB);
static VMGuestLibError (*GuestLib_GetMemReservationMB)(VMGuestLibHandle handle, uint32_t *memReservationMB);
static VMGuestLibError (*GuestLib_GetMemLimitMB)(VMGuestLibHandle handle, uint32_t *memLimitMB);
static VMGuestLibError (*GuestLib_GetMemShares)(VMGuestLibHandle handle, uint32_t *memShares);

/* handle for use with shared library */
static VMGuestLibHandle glHandle;

/* used when converting megabytes to bytes for memory counters */
#define BYTES_PER_MB 1024*1024

/* macro to load a single GuestLib function from the shared library */
#define LOAD_ONE_FUNC(funcname)                               \
  do {                                                       \
    funcname = dlsym(dlHandle, "VM" #funcname);             \
    if ((dlErrStr = dlerror()) != NULL) {                   \
      ERROR ("vmware plugin: Failed to load \"%s\": %s",   \
#funcname, dlErrStr);                         \
      return (-1);                                         \
    }                                                       \
  } while (0)

  _Bool
static LoadFunctions(void)
{
  void *dlHandle = NULL;

  /* first try to load the shared library */
  char const *dlErrStr;

  dlHandle = dlopen("libvmGuestLib.so", RTLD_NOW);
  if (!dlHandle) {
    dlErrStr = dlerror();
    ERROR("vmware plugin: dlopen (\"libvmGuestLib.so\") failed: %s",
        dlErrStr);
    return (-1);
  }

  /* Load all the individual library functions */
  LOAD_ONE_FUNC(GuestLib_GetErrorText);
  LOAD_ONE_FUNC(GuestLib_OpenHandle);
  LOAD_ONE_FUNC(GuestLib_CloseHandle);
  LOAD_ONE_FUNC(GuestLib_UpdateInfo);
  LOAD_ONE_FUNC(GuestLib_GetSessionId);
  LOAD_ONE_FUNC(GuestLib_GetElapsedMs);
  LOAD_ONE_FUNC(GuestLib_GetCpuStolenMs);
  LOAD_ONE_FUNC(GuestLib_GetCpuUsedMs);
  LOAD_ONE_FUNC(GuestLib_GetCpuReservationMHz);
  LOAD_ONE_FUNC(GuestLib_GetCpuLimitMHz);
  LOAD_ONE_FUNC(GuestLib_GetCpuShares);
  LOAD_ONE_FUNC(GuestLib_GetHostProcessorSpeed);
  LOAD_ONE_FUNC(GuestLib_GetMemReservationMB);
  LOAD_ONE_FUNC(GuestLib_GetMemLimitMB);
  LOAD_ONE_FUNC(GuestLib_GetMemShares);
  LOAD_ONE_FUNC(GuestLib_GetMemMappedMB);
  LOAD_ONE_FUNC(GuestLib_GetMemActiveMB);
  LOAD_ONE_FUNC(GuestLib_GetMemOverheadMB);
  LOAD_ONE_FUNC(GuestLib_GetMemBalloonedMB);
  LOAD_ONE_FUNC(GuestLib_GetMemSwappedMB);
  LOAD_ONE_FUNC(GuestLib_GetMemSharedMB);
  LOAD_ONE_FUNC(GuestLib_GetMemSharedSavedMB);
  LOAD_ONE_FUNC(GuestLib_GetMemUsedMB);
  LOAD_ONE_FUNC(GuestLib_GetMemTargetSizeMB);

  return (0);
}

static int vmware_init (void)
{
  VMGuestLibError glError;

  if (!LoadFunctions()) {
    ERROR ("vmware plugin: Unable to load GuestLib functions");
    return (-1);
  }

  /* try to load the library */
  glError = GuestLib_OpenHandle(&glHandle);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    ERROR ("vmware plugin: OpenHandle failed: %s", GuestLib_GetErrorText(glError));
    return (-1);
  }

  return (0);
}

static void submit_vmw_counter (const char *type, const char *type_inst,
    derive_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].derive = value;

  vl.values = values;
  vl.values_len = 1;

  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "vmware", sizeof (vl.plugin));
  sstrncpy (vl.type, type, sizeof (vl.type));
  sstrncpy (vl.type_instance, type_inst, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
}

static void submit_vmw_gauge (const char *type, const char *type_inst,
    gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;

  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "vmware", sizeof (vl.plugin));
  sstrncpy (vl.type, type, sizeof (vl.type));
  sstrncpy (vl.type_instance, type_inst, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
}

static int vmw_query_memory (VMGuestLibHandle handle, const char *function_name,
    VMGuestLibError (*function) (VMGuestLibHandle handle, uint32_t *ret_data),
    const char *type_instance)
{
  uint32_t value;
  VMGuestLibError status;

  status = (*function) (handle, &value);
  if (status != VMGUESTLIB_ERROR_SUCCESS) {
    WARNING ("vmware plugin: %s failed: %s",
        function_name,
        GuestLib_GetErrorText(glError));
    return (-1);
  }

  /* The returned value is in megabytes, so multiply it by 2^20. It's not
   * 10^6, because we're talking about memory. */
  submit_vmw_gauge ("memory", type_instance,
      (gauge_t) (1024 * 1024 * value));
  return (0);
} /* }}} int vmw_query_megabyte */

static int vmware_read (void)
{
  VMGuestLibError glError;

  /* total_time_in_ms */
  uint64_t elapsedMs = 0;

  /* virt_vcpu */
  uint64_t cpuUsedMs = 0;
  uint64_t cpuStolenMs = 0;

  /* vcpu (quality of service) */
  uint32_t cpuReservationMHz = 0;
  uint32_t cpuLimitMHz = 0;
  uint32_t cpuShares = 0;

  /* cpufreq */
  uint32_t hostMHz = 0;

  /* memory */
  uint64_t memTargetSizeMB = 0;
  uint32_t memUsedMB = 0;
  uint32_t memMappedMB = 0;
  uint32_t memActiveMB = 0;
  uint32_t memOverheadMB = 0;
  uint32_t memSharedMB = 0;
  uint32_t memSharedSavedMB = 0;
  uint32_t memBalloonedMB = 0;
  uint32_t memSwappedMB = 0;

  /* memory (quality of service) */
  uint32_t memReservationMB = 0;
  uint32_t memLimitMB = 0;
  uint32_t memShares = 0;

  VMSessionId sessionId = 0;

  /* attempt to retrieve info from the host */
  VMSessionId tmpSession;

  glError = GuestLib_UpdateInfo(glHandle);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    ERROR ("vmware plugin: UpdateInfo failed: %s", GuestLib_GetErrorText(glError));
    return (-1);
  }

  /* retrieve and check the session ID */
  glError = GuestLib_GetSessionId(glHandle, &tmpSession);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    ERROR ("vmware plugin: Failed to get session ID: %s", GuestLib_GetErrorText(glError));
    return (-1);
  }

  if (tmpSession == 0) {
    ERROR ("vmware plugin: Error: Got zero sessionId from GuestLib");
    return (-1);
  }

  if (sessionId == 0) {
    sessionId = tmpSession;
    DEBUG ("vmware plugin: Initial session ID is %#"PRIx64, (uint64_t) sessionId);
  } else if (tmpSession != sessionId) {
    sessionId = tmpSession;
    DEBUG ("vmware plugin: Session ID changed to %#"PRIx64, (uint64_t) sessionId);
  }

  /* GetElapsedMs */
  glError = GuestLib_GetElapsedMs(glHandle, &elapsedMs);
  if (glError != VMGUESTLIB_ERROR_SUCCESS)
    WARNING ("vmware plugin: Failed to get elapsed ms: %s", GuestLib_GetErrorText(glError));
  else
    submit_vmw_counter ("total_time_in_ms", "elapsed", (derive_t) elapsedMs);

  /* GetCpuUsedMs */
  glError = GuestLib_GetCpuUsedMs(glHandle, &cpuUsedMs);
  if (glError != VMGUESTLIB_ERROR_SUCCESS)
    WARNING ("vmware plugin: Failed to get used ms: %s",
        GuestLib_GetErrorText(glError));
  else
    submit_vmw_counter ("virt_vcpu", "used", (derive_t) cpuUsedMs);

  /* GetCpuStolenMs */
  glError = GuestLib_GetCpuStolenMs(glHandle, &cpuStolenMs);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get CPU stolen: %s\n", GuestLib_GetErrorText(glError));
    if (glError == VMGUESTLIB_ERROR_UNSUPPORTED_VERSION) {
      cpuStolenMs = 0;
    }
  }
  submit_vmw_counter ("virt_vcpu", "stolen", (derive_t) cpuStolenMs);

  /* GetCpuReservationMHz */
  glError = GuestLib_GetCpuReservationMHz(glHandle, &cpuReservationMHz);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get CPU reservation: %s\n", GuestLib_GetErrorText(glError));
  }
  submit_vmw_gauge ("vcpu", "reservation", (gauge_t) cpuReservationMHz);

  /* GetCpuLimitMHz */
  glError = GuestLib_GetCpuLimitMHz(glHandle, &cpuLimitMHz);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get CPU limit: %s\n", GuestLib_GetErrorText(glError));
  }
  submit_vmw_gauge ("vcpu", "limit", (gauge_t) cpuLimitMHz);

  /* GetCpuShares */
  glError = GuestLib_GetCpuShares(glHandle, &cpuShares);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get cpu shares: %s\n", GuestLib_GetErrorText(glError));
  }
  submit_vmw_gauge ("vcpu", "shares", (gauge_t) cpuShares);

  /* GetHostProcessorSpeed */
  glError = GuestLib_GetHostProcessorSpeed(glHandle, &hostMHz);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get host proc speed: %s\n", GuestLib_GetErrorText(glError));
  }
  submit_vmw_gauge ("cpufreq", "", 1.0e6 * (gauge_t) hostMHz);

#define VMW_QUERY_MEMORY(func, type) \
  vmw_query_memory (glHandle, #func, GuestLib_ ## func, type)

  VMW_QUERY_MEMORY (GetMemTargetSizeMB,  "target");
  VMW_QUERY_MEMORY (GetMemUsedMB,        "used");
  VMW_QUERY_MEMORY (GetMemMappedMB,      "mapped");
  VMW_QUERY_MEMORY (GetMemActiveMB,      "active");
  VMW_QUERY_MEMORY (GetMemOverheadMB,    "overhead");
  VMW_QUERY_MEMORY (GetMemSharedMB,      "shared");
  VMW_QUERY_MEMORY (GetMemSharedSavedMB, "shared_saved");
  VMW_QUERY_MEMORY (GetMemBalloonedMB,   "ballooned");
  VMW_QUERY_MEMORY (GetMemSwappedMB,     "swapped");
  VMW_QUERY_MEMORY (GetMemReservationMB, "reservation");
  VMW_QUERY_MEMORY (GetMemLimitMB,       "limit");

#undef VMW_QUERY_MEMORY

  /* GetMemTargetSizeMB */
  glError = GuestLib_GetMemTargetSizeMB(glHandle, &memTargetSizeMB);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get target mem size: %s\n", GuestLib_GetErrorText(glError));
    if (glError == VMGUESTLIB_ERROR_UNSUPPORTED_VERSION) {
      memTargetSizeMB = 0;
    }
  }
  submit_vmw_gauge ("memory", "target", BYTES_PER_MB * (gauge_t) memTargetSizeMB);

  /* GetMemUsedMB */
  glError = GuestLib_GetMemUsedMB(glHandle, &memUsedMB);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get used mem: %s\n", GuestLib_GetErrorText(glError));
  }
  submit_vmw_gauge ("memory", "used", BYTES_PER_MB * (gauge_t) memUsedMB);

  /* GetMemMappedMB */
  glError = GuestLib_GetMemMappedMB(glHandle, &memMappedMB);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get mapped mem: %s\n", GuestLib_GetErrorText(glError));
  }
  submit_vmw_gauge ("memory", "mapped", BYTES_PER_MB * (gauge_t) memMappedMB);

  /* GetMemActiveMB */
  glError = GuestLib_GetMemActiveMB(glHandle, &memActiveMB);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get active mem: %s\n", GuestLib_GetErrorText(glError));
  }
  submit_vmw_gauge ("memory", "active", BYTES_PER_MB * (gauge_t) memActiveMB);

  /* GetMemOverheadMB */
  glError = GuestLib_GetMemOverheadMB(glHandle, &memOverheadMB);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get overhead mem: %s\n", GuestLib_GetErrorText(glError));
  }
  submit_vmw_gauge ("memory", "overhead", BYTES_PER_MB * (gauge_t) memOverheadMB);

  /* GetMemSharedMB */
  glError = GuestLib_GetMemSharedMB(glHandle, &memSharedMB);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get shared mem: %s\n", GuestLib_GetErrorText(glError));
  }
  submit_vmw_gauge ("memory", "shared", BYTES_PER_MB * (gauge_t) memSharedMB);

  /* GetMemSharedSavedMB */
  glError = GuestLib_GetMemSharedSavedMB(glHandle, &memSharedSavedMB);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get shared saved mem: %s\n", GuestLib_GetErrorText(glError));
  }
  submit_vmw_gauge ("memory", "shared_saved", BYTES_PER_MB * (gauge_t) memSharedSavedMB);

  /* GetMemBalloonedMB */
  glError = GuestLib_GetMemBalloonedMB(glHandle, &memBalloonedMB);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get ballooned mem: %s\n", GuestLib_GetErrorText(glError));
  }
  submit_vmw_gauge ("memory", "ballooned", BYTES_PER_MB * (gauge_t) memBalloonedMB);

  /* GetMemSwappedMB */
  glError = GuestLib_GetMemSwappedMB(glHandle, &memSwappedMB);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get swapped mem: %s\n", GuestLib_GetErrorText(glError));
  }
  submit_vmw_gauge ("memory", "swapped", BYTES_PER_MB * (gauge_t) memSwappedMB);

  /* GetMemReservationMB */
  glError = GuestLib_GetMemReservationMB(glHandle, &memReservationMB);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get mem reservation: %s\n", GuestLib_GetErrorText(glError));
  }
  submit_vmw_gauge ("memory", "reservation", BYTES_PER_MB * (gauge_t) memReservationMB);

  /* GetMemLimitMB */
  glError = GuestLib_GetMemLimitMB(glHandle, &memLimitMB);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get mem limit: %s\n", GuestLib_GetErrorText(glError));
  }
  submit_vmw_gauge ("memory", "limit", BYTES_PER_MB * (gauge_t) memLimitMB);

  /* GetMemShares */
  glError = GuestLib_GetMemShares(glHandle, &memShares);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    DEBUG ("vmware plugin: Failed to get mem shares: %s\n", GuestLib_GetErrorText(glError));
    memShares = 0;
  }
  submit_vmw_gauge ("memory", "shares", (gauge_t) memShares);

  return (0);
}

void module_register (void)
{
  plugin_register_init ("vmware", vmware_init);
  plugin_register_read ("vmware", vmware_read);
}
