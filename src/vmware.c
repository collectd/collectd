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
static VMGuestLibHandle gl_handle;
static VMSessionId gl_session;

/* used when converting megabytes to bytes for memory counters */
#define BYTES_PER_MB 1024*1024

/* macro to load a single GuestLib function from the shared library */
#define LOAD_ONE_FUNC(funcname) do {                                         \
    char const *errmsg;                                                      \
    funcname = dlsym(dl_handle, "VM" #funcname);                             \
    if ((errmsg = dlerror()) != NULL) {                                      \
      ERROR ("vmware plugin: Failed to load \"%s\": %s",                     \
          #funcname, errmsg);                                                \
      return (-1);                                                           \
    }                                                                        \
} while (0)

_Bool static vmware_load_functions (void)
{
  void *dl_handle;

  dl_handle = dlopen("libvmGuestLib.so", RTLD_NOW);
  if (!dl_handle)
  {
    char const *errmsg;

    errmsg = dlerror();
    ERROR("vmware plugin: dlopen (\"libvmGuestLib.so\") failed: %s",
        errmsg);
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
  VMGuestLibError status;

  if (!vmware_load_functions()) {
    ERROR ("vmware plugin: Unable to load GuestLib functions");
    return (-1);
  }

  /* try to load the library */
  status = GuestLib_OpenHandle(&gl_handle);
  if (status != VMGUESTLIB_ERROR_SUCCESS)
  {
    ERROR ("vmware plugin: OpenHandle failed: %s",
        GuestLib_GetErrorText (status));
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

static int vmw_query_memory (VMGuestLibHandle handle,
    char const *function_name,
    VMGuestLibError (*function) (VMGuestLibHandle, uint32_t *),
    char const *type_instance)
{
  uint32_t value;
  VMGuestLibError status;

  status = (*function) (handle, &value);
  if (status != VMGUESTLIB_ERROR_SUCCESS)
  {
    WARNING ("vmware plugin: %s failed: %s",
        function_name, GuestLib_GetErrorText (status));
    return (-1);
  }

  /* The returned value is in megabytes, so multiply it by 2^20. It's not
   * 10^6, because we're talking about memory. */
  submit_vmw_gauge ("memory", type_instance,
      (gauge_t) (BYTES_PER_MB * value));
  return (0);
} /* }}} int vmw_query_megabyte */

static int vmware_read (void)
{

  VMGuestLibError status;

  uint32_t tmp32;
  uint64_t tmp64;

  status = GuestLib_UpdateInfo(gl_handle);
  if (status != VMGUESTLIB_ERROR_SUCCESS) {
    ERROR ("vmware plugin: UpdateInfo failed: %s",
        GuestLib_GetErrorText(status));
    return (-1);
  }

  /* retrieve and check the session ID */
  status = GuestLib_GetSessionId(gl_handle, &gl_session);
  if (status != VMGUESTLIB_ERROR_SUCCESS) {
    ERROR ("vmware plugin: Failed to get session ID: %s",
        GuestLib_GetErrorText(status));
    return (-1);
  }

  if (gl_session == 0) {
    ERROR ("vmware plugin: Error: Got zero sessionId from GuestLib");
    return (-1);
  }

  /* GetElapsedMs */
  status = GuestLib_GetElapsedMs(gl_handle, &tmp64);
  if (status != VMGUESTLIB_ERROR_SUCCESS)
    WARNING ("vmware plugin: Failed to get elapsed ms: %s",
        GuestLib_GetErrorText(status));
  else
    submit_vmw_counter ("total_time_in_ms", "elapsed", (derive_t) tmp64);

  /* GetCpuUsedMs */
  status = GuestLib_GetCpuUsedMs(gl_handle, &tmp64);
  if (status != VMGUESTLIB_ERROR_SUCCESS)
    WARNING ("vmware plugin: Failed to get used ms: %s",
        GuestLib_GetErrorText(status));
  else
    submit_vmw_counter ("virt_vcpu", "used", (derive_t) tmp64);

  /* GetCpuStolenMs */
  status = GuestLib_GetCpuStolenMs(gl_handle, &tmp64);
  if (status == VMGUESTLIB_ERROR_UNSUPPORTED_VERSION)
    /* ignore */;
  else if (status != VMGUESTLIB_ERROR_SUCCESS)
    WARNING ("vmware plugin: Failed to get CPU stolen: %s",
        GuestLib_GetErrorText(status));
  else
    submit_vmw_counter ("virt_vcpu", "stolen", (derive_t) tmp64);

  /* GetCpuReservationMHz */
  status = GuestLib_GetCpuReservationMHz(gl_handle, &tmp32);
  if (status != VMGUESTLIB_ERROR_SUCCESS)
    WARNING ("vmware plugin: Failed to get CPU reservation: %s",
        GuestLib_GetErrorText(status));
  else
    submit_vmw_gauge ("vcpu", "reservation", (gauge_t) tmp32);

  /* GetCpuLimitMHz */
  status = GuestLib_GetCpuLimitMHz(gl_handle, &tmp32);
  if (status != VMGUESTLIB_ERROR_SUCCESS)
    WARNING ("vmware plugin: Failed to get CPU limit: %s",
        GuestLib_GetErrorText(status));
  else
    submit_vmw_gauge ("vcpu", "limit", (gauge_t) tmp32);

  /* GetCpuShares */
  status = GuestLib_GetCpuShares(gl_handle, &tmp32);
  if (status != VMGUESTLIB_ERROR_SUCCESS)
    WARNING ("vmware plugin: Failed to get cpu shares: %s",
        GuestLib_GetErrorText(status));
  else
    submit_vmw_gauge ("vcpu", "shares", (gauge_t) tmp32);

  /* GetHostProcessorSpeed */
  status = GuestLib_GetHostProcessorSpeed(gl_handle, &tmp32);
  if (status != VMGUESTLIB_ERROR_SUCCESS)
    WARNING ("vmware plugin: Failed to get host proc speed: %s",
        GuestLib_GetErrorText(status));
  else
    submit_vmw_gauge ("cpufreq", "", 1.0e6 * (gauge_t) tmp32);

  /* GetMemTargetSizeMB */
  status = GuestLib_GetMemTargetSizeMB(gl_handle, &tmp64);
  if (status != VMGUESTLIB_ERROR_SUCCESS)
    WARNING ("vmware plugin: GuestLib_GetMemTargetSizeMB failed: %s",
        GuestLib_GetErrorText (status));
  else
    submit_vmw_gauge ("memory", "target", (gauge_t) (BYTES_PER_MB * tmp64));

#define VMW_QUERY_MEMORY(func, type) \
  vmw_query_memory (gl_handle, #func, GuestLib_ ## func, type)

  VMW_QUERY_MEMORY (GetMemUsedMB,        "used"); /* pysical; used = mapped - shared_saved */
  VMW_QUERY_MEMORY (GetMemMappedMB,      "mapped"); /* mapped = used + shared_saved */
  VMW_QUERY_MEMORY (GetMemActiveMB,      "active");
  VMW_QUERY_MEMORY (GetMemOverheadMB,    "overhead");
  VMW_QUERY_MEMORY (GetMemSharedMB,      "shared"); /* physical */
  VMW_QUERY_MEMORY (GetMemSharedSavedMB, "shared_saved");
  VMW_QUERY_MEMORY (GetMemBalloonedMB,   "ballooned");
  VMW_QUERY_MEMORY (GetMemSwappedMB,     "swapped"); /* physical? */
  /* min memory available to the guest */
  VMW_QUERY_MEMORY (GetMemReservationMB, "reservation");
  /* max memory available to the guest */
  VMW_QUERY_MEMORY (GetMemLimitMB,       "limit");
  /* ??? */
  VMW_QUERY_MEMORY (GetMemShares,        "shares");

#undef VMW_QUERY_MEMORY

  return (0);
}

void module_register (void)
{
  plugin_register_init ("vmware", vmware_init);
  plugin_register_read ("vmware", vmware_read);
}
