#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <dlfcn.h>
#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include "vmGuestLib.h"

/* Functions to dynamically load from the GuestLib library. */
char const * (*GuestLib_GetErrorText)(VMGuestLibError);
VMGuestLibError (*GuestLib_OpenHandle)(VMGuestLibHandle*);
VMGuestLibError (*GuestLib_CloseHandle)(VMGuestLibHandle);
VMGuestLibError (*GuestLib_UpdateInfo)(VMGuestLibHandle handle);
VMGuestLibError (*GuestLib_GetSessionId)(VMGuestLibHandle handle,
                                         VMSessionId *id);
VMGuestLibError (*GuestLib_GetCpuReservationMHz)(VMGuestLibHandle handle,
                                                 uint32 *cpuReservationMHz);
VMGuestLibError (*GuestLib_GetCpuLimitMHz)(VMGuestLibHandle handle, uint32 *cpuLimitMHz);
VMGuestLibError (*GuestLib_GetCpuShares)(VMGuestLibHandle handle, uint32 *cpuShares);
VMGuestLibError (*GuestLib_GetCpuUsedMs)(VMGuestLibHandle handle, uint64 *cpuUsedMs);
VMGuestLibError (*GuestLib_GetHostProcessorSpeed)(VMGuestLibHandle handle, uint32 *mhz);
VMGuestLibError (*GuestLib_GetMemReservationMB)(VMGuestLibHandle handle,
                                                uint32 *memReservationMB);
VMGuestLibError (*GuestLib_GetMemLimitMB)(VMGuestLibHandle handle, uint32 *memLimitMB);
VMGuestLibError (*GuestLib_GetMemShares)(VMGuestLibHandle handle, uint32 *memShares);
VMGuestLibError (*GuestLib_GetMemMappedMB)(VMGuestLibHandle handle,
                                           uint32 *memMappedMB);
VMGuestLibError (*GuestLib_GetMemActiveMB)(VMGuestLibHandle handle, uint32 *memActiveMB);
VMGuestLibError (*GuestLib_GetMemOverheadMB)(VMGuestLibHandle handle,
                                             uint32 *memOverheadMB);
VMGuestLibError (*GuestLib_GetMemBalloonedMB)(VMGuestLibHandle handle,
                                              uint32 *memBalloonedMB);
VMGuestLibError (*GuestLib_GetMemSwappedMB)(VMGuestLibHandle handle,
                                            uint32 *memSwappedMB);
VMGuestLibError (*GuestLib_GetMemSharedMB)(VMGuestLibHandle handle,
                                           uint32 *memSharedMB);
VMGuestLibError (*GuestLib_GetMemSharedSavedMB)(VMGuestLibHandle handle,
                                                uint32 *memSharedSavedMB);
VMGuestLibError (*GuestLib_GetMemUsedMB)(VMGuestLibHandle handle,
                                         uint32 *memUsedMB);
VMGuestLibError (*GuestLib_GetElapsedMs)(VMGuestLibHandle handle, uint64 *elapsedMs);
VMGuestLibError (*GuestLib_GetResourcePoolPath)(VMGuestLibHandle handle,
                                                size_t *bufferSize,
                                                char *pathBuffer);
VMGuestLibError (*GuestLib_GetCpuStolenMs)(VMGuestLibHandle handle,
                                           uint64 *cpuStolenMs);
VMGuestLibError (*GuestLib_GetMemTargetSizeMB)(VMGuestLibHandle handle,
                                               uint64 *memTargetSizeMB);
VMGuestLibError (*GuestLib_GetHostNumCpuCores)(VMGuestLibHandle handle,
                                               uint32 *hostNumCpuCores);
VMGuestLibError (*GuestLib_GetHostCpuUsedMs)(VMGuestLibHandle handle,
                                             uint64 *hostCpuUsedMs);
VMGuestLibError (*GuestLib_GetHostMemSwappedMB)(VMGuestLibHandle handle,
                                                uint64 *hostMemSwappedMB);
VMGuestLibError (*GuestLib_GetHostMemSharedMB)(VMGuestLibHandle handle,
                                               uint64 *hostMemSharedMB);
VMGuestLibError (*GuestLib_GetHostMemUsedMB)(VMGuestLibHandle handle,
                                             uint64 *hostMemUsedMB);
VMGuestLibError (*GuestLib_GetHostMemPhysMB)(VMGuestLibHandle handle,
                                             uint64 *hostMemPhysMB);
VMGuestLibError (*GuestLib_GetHostMemPhysFreeMB)(VMGuestLibHandle handle,
                                                 uint64 *hostMemPhysFreeMB);
VMGuestLibError (*GuestLib_GetHostMemKernOvhdMB)(VMGuestLibHandle handle,
                                                 uint64 *hostMemKernOvhdMB);
VMGuestLibError (*GuestLib_GetHostMemMappedMB)(VMGuestLibHandle handle,
                                               uint64 *hostMemMappedMB);
VMGuestLibError (*GuestLib_GetHostMemUnmappedMB)(VMGuestLibHandle handle,
                                                 uint64 *hostMemUnmappedMB);
/*
 * Handle for use with shared library.
 */
void *dlHandle = NULL;

/*
 * GuestLib handle.
 */
VMGuestLibHandle glHandle;

VMGuestLibError glError;

/*
 * Macro to load a single GuestLib function from the shared library.
 */
#define LOAD_ONE_FUNC(funcname)                           \
   do {                                                   \
      funcname = dlsym(dlHandle, "VM" #funcname);         \
      if ((dlErrStr = dlerror()) != NULL) {               \
         printf("Failed to load \'%s\': \'%s\'\n",        \
                #funcname, dlErrStr);                     \
         return FALSE;                                    \
      }                                                   \
   } while (0)

Bool
LoadFunctions(void)
{
   /*
    * First, try to load the shared library.
    */
   char const *dlErrStr;

   dlHandle = dlopen("libvmGuestLib.so", RTLD_NOW);
   if (!dlHandle) {
      dlErrStr = dlerror();
      printf("dlopen failed: \'%s\'\n", dlErrStr);
      return FALSE;
   }

   /* Load all the individual library functions. */
   LOAD_ONE_FUNC(GuestLib_GetErrorText);
   LOAD_ONE_FUNC(GuestLib_OpenHandle);
   LOAD_ONE_FUNC(GuestLib_CloseHandle);
   LOAD_ONE_FUNC(GuestLib_UpdateInfo);
   LOAD_ONE_FUNC(GuestLib_GetSessionId);
   LOAD_ONE_FUNC(GuestLib_GetCpuReservationMHz);
   LOAD_ONE_FUNC(GuestLib_GetCpuLimitMHz);
   LOAD_ONE_FUNC(GuestLib_GetCpuShares);
   LOAD_ONE_FUNC(GuestLib_GetCpuUsedMs);
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
   LOAD_ONE_FUNC(GuestLib_GetElapsedMs);
   LOAD_ONE_FUNC(GuestLib_GetResourcePoolPath);
   LOAD_ONE_FUNC(GuestLib_GetCpuStolenMs);
   LOAD_ONE_FUNC(GuestLib_GetMemTargetSizeMB);
   LOAD_ONE_FUNC(GuestLib_GetHostNumCpuCores);
   LOAD_ONE_FUNC(GuestLib_GetHostCpuUsedMs);
   LOAD_ONE_FUNC(GuestLib_GetHostMemSwappedMB);
   LOAD_ONE_FUNC(GuestLib_GetHostMemSharedMB);
   LOAD_ONE_FUNC(GuestLib_GetHostMemUsedMB);
   LOAD_ONE_FUNC(GuestLib_GetHostMemPhysMB);
   LOAD_ONE_FUNC(GuestLib_GetHostMemPhysFreeMB);
   LOAD_ONE_FUNC(GuestLib_GetHostMemKernOvhdMB);
   LOAD_ONE_FUNC(GuestLib_GetHostMemMappedMB);
   LOAD_ONE_FUNC(GuestLib_GetHostMemUnmappedMB);

   return TRUE;
}

static int vmware_init (void)
{
  if (!LoadFunctions()) {
    ERROR ("vmware guest plugin: Unable to load GuistLib functions");
    return (-1);
  }

  /* Try to load the library. */
  glError = GuestLib_OpenHandle(&glHandle);
  if (glError != VMGUESTLIB_ERROR_SUCCESS) {
     ERROR ("OpenHandle failed: %s", GuestLib_GetErrorText(glError));
     return (-1);
  }

  return (0);
}

static void vmware_submit_counter (const char *reading, counter_t value)
{
    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;

    values[0].counter = value;

    vl.values = values;
    vl.values_len = 1;

    sstrncpy (vl.host, hostname_g, sizeof (vl.host));
    sstrncpy (vl.plugin, "vmware", sizeof (vl.plugin));
    sstrncpy (vl.type, reading, sizeof (vl.type));

    plugin_dispatch_values (&vl);
}

static void vmware_submit_gauge (const char *reading, gauge_t value)
{
    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;

    values[0].gauge = value;

    vl.values = values;
    vl.values_len = 1;

    sstrncpy (vl.host, hostname_g, sizeof (vl.host));
    sstrncpy (vl.plugin, "vmware", sizeof (vl.plugin));
    sstrncpy (vl.type, reading, sizeof (vl.type));

    plugin_dispatch_values (&vl);
}

static int vmware_read (void)
{
   counter_t value;
   uint32 cpuReservationMHz = 0;
   uint32 cpuLimitMHz = 0;
   uint32 cpuShares = 0;
   uint64 cpuUsedMs = 0;
   uint32 hostMHz = 0;
   uint32 memReservationMB = 0;
   uint32 memLimitMB = 0;
   uint32 memShares = 0;
   uint32 memMappedMB = 0;
   uint32 memActiveMB = 0;
   uint32 memOverheadMB = 0;
   uint32 memBalloonedMB = 0;
   uint32 memSwappedMB = 0;
   uint32 memSharedMB = 0;
   uint32 memSharedSavedMB = 0;
   uint32 memUsedMB = 0;
   uint64 elapsedMs = 0;
   uint64 cpuStolenMs = 0;
   uint64 memTargetSizeMB = 0;
   uint32 hostNumCpuCores = 0;
   uint64 hostCpuUsedMs = 0;
   uint64 hostMemSwappedMB = 0;
   uint64 hostMemSharedMB = 0;
   uint64 hostMemUsedMB = 0;
   uint64 hostMemPhysMB = 0;
   uint64 hostMemPhysFreeMB = 0;
   uint64 hostMemKernOvhdMB = 0;
   uint64 hostMemMappedMB = 0;
   uint64 hostMemUnmappedMB = 0;
   VMSessionId sessionId = 0;

   /* Attempt to retrieve info from the host. */
   VMSessionId tmpSession;

   glError = GuestLib_UpdateInfo(glHandle);
   if (glError != VMGUESTLIB_ERROR_SUCCESS) {
     ERROR ("UpdateInfo failed: %s", GuestLib_GetErrorText(glError));
     return (-1);
   }

   /* Retrieve and check the session ID */
   glError = GuestLib_GetSessionId(glHandle, &tmpSession);
   if (glError != VMGUESTLIB_ERROR_SUCCESS) {
    ERROR ("Failed to get session ID: %s", GuestLib_GetErrorText(glError));
    return (-1);
   }

   if (tmpSession == 0) {
     ERROR ("Error: Got zero sessionId from GuestLib");
     return (-1);
   }

   if (sessionId == 0) {
    sessionId = tmpSession;
    DEBUG ("Initial session ID is 0x%"FMT64"x", sessionId);
   } else if (tmpSession != sessionId) {
    sessionId = tmpSession;
    DEBUG ("SESSION CHANGED: New session ID is 0x%"FMT64"x\n", sessionId);
   }

   /* Retrieve all the stats. */
   /* FIXME: GENERALIZE */
    glError = GuestLib_GetCpuReservationMHz(glHandle, &cpuReservationMHz);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get CPU reservation: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (gauge_t) cpuReservationMHz;
    vmware_submit_gauge ("cpu_reservation_mhz", value);

    glError = GuestLib_GetCpuLimitMHz(glHandle, &cpuLimitMHz);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get CPU limit: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (gauge_t) cpuLimitMHz;
    vmware_submit_gauge ("cpu_limit_mhz", value);

    glError = GuestLib_GetCpuShares(glHandle, &cpuShares);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get cpu shares: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (gauge_t) cpuShares;
    vmware_submit_gauge ("cpu_shares", value);

    glError = GuestLib_GetCpuUsedMs(glHandle, &cpuUsedMs);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get used ms: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (counter_t) cpuUsedMs;
    vmware_submit_counter ("cpu_used_ms", value);

    glError = GuestLib_GetHostProcessorSpeed(glHandle, &hostMHz);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get host proc speed: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (gauge_t) hostMHz;
    vmware_submit_gauge ("host_processor_speed", value);

    glError = GuestLib_GetMemReservationMB(glHandle, &memReservationMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get mem reservation: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (gauge_t) memReservationMB;
    vmware_submit_gauge ("memory_reservation_mb", value);

    glError = GuestLib_GetMemLimitMB(glHandle, &memLimitMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get mem limit: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (gauge_t) memLimitMB;
    vmware_submit_gauge ("memory_limit_mb", value);

    glError = GuestLib_GetMemShares(glHandle, &memShares);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get mem shares: %s\n", GuestLib_GetErrorText(glError));
      memShares = 0;
    }
    value = (gauge_t) memShares;
    vmware_submit_gauge ("memory_shares", value);

    glError = GuestLib_GetMemMappedMB(glHandle, &memMappedMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get mapped mem: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (gauge_t) memMappedMB;
    vmware_submit_gauge ("memory_mapped_mb", value);

    glError = GuestLib_GetMemActiveMB(glHandle, &memActiveMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
     DEBUG ("Failed to get active mem: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (gauge_t) memActiveMB;
    vmware_submit_gauge ("memory_active_mb", value);

    glError = GuestLib_GetMemOverheadMB(glHandle, &memOverheadMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get overhead mem: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (gauge_t) memOverheadMB;
    vmware_submit_gauge ("memory_overhead_mb", value);

    glError = GuestLib_GetMemBalloonedMB(glHandle, &memBalloonedMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get ballooned mem: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (gauge_t) memBalloonedMB;
    vmware_submit_gauge ("memory_ballooned_mb", value);

    glError = GuestLib_GetMemSwappedMB(glHandle, &memSwappedMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get swapped mem: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (gauge_t) memSwappedMB;
    vmware_submit_gauge ("memory_swapped_mb", value);

    glError = GuestLib_GetMemSharedMB(glHandle, &memSharedMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get swapped mem: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (gauge_t) memSharedMB;
    vmware_submit_gauge ("memory_shared_mb", value);

    glError = GuestLib_GetMemSharedSavedMB(glHandle, &memSharedSavedMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
     DEBUG ("Failed to get swapped mem: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (gauge_t) memSharedSavedMB;
    vmware_submit_gauge ("memory_shared_saved_mb", value);

    glError = GuestLib_GetMemUsedMB(glHandle, &memUsedMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get swapped mem: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (gauge_t) memUsedMB;
    vmware_submit_gauge ("memory_used_mb", value);

    glError = GuestLib_GetElapsedMs(glHandle, &elapsedMs);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get elapsed ms: %s\n", GuestLib_GetErrorText(glError));
    }
    value = (counter_t) elapsedMs;
    vmware_submit_counter ("elapsed_ms", value);

    glError = GuestLib_GetCpuStolenMs(glHandle, &cpuStolenMs);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get CPU stolen: %s\n", GuestLib_GetErrorText(glError));
      if (glError == VMGUESTLIB_ERROR_UNSUPPORTED_VERSION) {
        cpuStolenMs = 0;
      }
    }
    value = (counter_t) cpuStolenMs;
    vmware_submit_counter ("cpu_stolen_ms", value);

    glError = GuestLib_GetMemTargetSizeMB(glHandle, &memTargetSizeMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get target mem size: %s\n", GuestLib_GetErrorText(glError));
      if (glError == VMGUESTLIB_ERROR_UNSUPPORTED_VERSION) {
        memTargetSizeMB = 0;
      }
    }
    value = (gauge_t) memTargetSizeMB;
    vmware_submit_gauge ("memory_target_size", value);

    glError = GuestLib_GetHostNumCpuCores(glHandle, &hostNumCpuCores);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get host CPU cores: %s\n", GuestLib_GetErrorText(glError));
      if (glError == VMGUESTLIB_ERROR_UNSUPPORTED_VERSION ||
          glError == VMGUESTLIB_ERROR_NOT_AVAILABLE) {
        hostNumCpuCores = 0;
      }
    }
    value = (gauge_t) hostNumCpuCores;
    vmware_submit_gauge ("host_cpu_cores", value);

    glError = GuestLib_GetHostCpuUsedMs(glHandle, &hostCpuUsedMs);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get host CPU used: %s\n", GuestLib_GetErrorText(glError));
      if (glError == VMGUESTLIB_ERROR_UNSUPPORTED_VERSION ||
          glError == VMGUESTLIB_ERROR_NOT_AVAILABLE) {
        hostCpuUsedMs = 0;
      }
    }
    value = (counter_t) hostCpuUsedMs;
    vmware_submit_counter ("host_cpu_used_ms", value);

    glError = GuestLib_GetHostMemSwappedMB(glHandle, &hostMemSwappedMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get host mem swapped: %s\n", GuestLib_GetErrorText(glError));
      if (glError == VMGUESTLIB_ERROR_UNSUPPORTED_VERSION ||
          glError == VMGUESTLIB_ERROR_NOT_AVAILABLE) {
        hostMemSwappedMB = 0;
      }
    }
    value = (gauge_t) hostMemSwappedMB;
    vmware_submit_gauge ("host_mem_swapped_mb", value);

    glError = GuestLib_GetHostMemSharedMB(glHandle, &hostMemSharedMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get host mem shared: %s\n", GuestLib_GetErrorText(glError));
      if (glError == VMGUESTLIB_ERROR_UNSUPPORTED_VERSION ||
          glError == VMGUESTLIB_ERROR_NOT_AVAILABLE) {
        hostMemSharedMB = 0;
      }
    }
    value = (gauge_t) hostMemSharedMB;
    vmware_submit_gauge ("host_mem_shared_mb", value);

    glError = GuestLib_GetHostMemUsedMB(glHandle, &hostMemUsedMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get host mem used: %s\n", GuestLib_GetErrorText(glError));
      if (glError == VMGUESTLIB_ERROR_UNSUPPORTED_VERSION ||
          glError == VMGUESTLIB_ERROR_NOT_AVAILABLE) {
        hostMemUsedMB = 0;
      }
    }
    value = (gauge_t) hostMemSharedMB;
    vmware_submit_gauge ("host_mem_used_mb", value);

    glError = GuestLib_GetHostMemPhysMB(glHandle, &hostMemPhysMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get host phys mem: %s\n", GuestLib_GetErrorText(glError));
      if (glError == VMGUESTLIB_ERROR_UNSUPPORTED_VERSION ||
          glError == VMGUESTLIB_ERROR_NOT_AVAILABLE) {
        hostMemPhysMB = 0;
      }
    }
    value = (gauge_t) hostMemPhysMB;
    vmware_submit_gauge ("host_mem_physical_mb", value);

    glError = GuestLib_GetHostMemPhysFreeMB(glHandle, &hostMemPhysFreeMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get host phys mem free: %s\n", GuestLib_GetErrorText(glError));
      if (glError == VMGUESTLIB_ERROR_UNSUPPORTED_VERSION ||
          glError == VMGUESTLIB_ERROR_NOT_AVAILABLE) {
        hostMemPhysFreeMB = 0;
      }
    }
    value = (gauge_t) hostMemPhysFreeMB;
    vmware_submit_gauge ("host_mem_physical_free_mb", value);

    glError = GuestLib_GetHostMemKernOvhdMB(glHandle, &hostMemKernOvhdMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get host kernel overhead mem: %s\n", GuestLib_GetErrorText(glError));
      if (glError == VMGUESTLIB_ERROR_UNSUPPORTED_VERSION ||
          glError == VMGUESTLIB_ERROR_NOT_AVAILABLE) {
        hostMemKernOvhdMB = 0;
      }
    }
    value = (gauge_t) hostMemKernOvhdMB;
    vmware_submit_gauge ("host_mem_kernel_overhead_mb", value);

    glError = GuestLib_GetHostMemMappedMB(glHandle, &hostMemMappedMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get host mem mapped: %s\n", GuestLib_GetErrorText(glError));
      if (glError == VMGUESTLIB_ERROR_UNSUPPORTED_VERSION ||
          glError == VMGUESTLIB_ERROR_NOT_AVAILABLE) {
        hostMemMappedMB = 0;
      }
    }
    value = (gauge_t) hostMemMappedMB;
    vmware_submit_gauge ("host_mem_mapped_mb", value);

    glError = GuestLib_GetHostMemUnmappedMB(glHandle, &hostMemUnmappedMB);
    if (glError != VMGUESTLIB_ERROR_SUCCESS) {
      DEBUG ("Failed to get host mem unmapped: %s\n", GuestLib_GetErrorText(glError));
      if (glError == VMGUESTLIB_ERROR_UNSUPPORTED_VERSION ||
          glError == VMGUESTLIB_ERROR_NOT_AVAILABLE) {
        hostMemUnmappedMB = 0;
      }
    }
    value = (gauge_t) hostMemUnmappedMB;
    vmware_submit_gauge ("host_mem_unmapped_mb", value);

   return (0);
}

void module_register (void)
{
  plugin_register_init ("vmware", vmware_init);
  plugin_register_read ("vmware", vmware_read);
}
