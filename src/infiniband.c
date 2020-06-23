/**
 * collectd - src/infiniband.c
 *
 * Copyright 2002 NVIDIA Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors:
 *   Luke Yeager <lyeager at nvidia.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"

#if !KERNEL_LINUX
#error "No applicable input method."
#endif

#include <ctype.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Configuration settings ****************************************************/

static const char *config_keys[] = {
    "Port",
    "IgnoreSelected",
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);
static ignorelist_t *ignorelist;

/* Listing ports *************************************************************/

static int ib_glob_ports(glob_t *g) {
  return glob("/sys/class/infiniband/*/ports/*/state", GLOB_NOSORT, NULL, g);
}

static int ib_parse_glob_port(char *path, char **device, char **port) {
  char *tok, *saveptr = NULL;
  int j = 0;
  *device = NULL;
  *port = NULL;
  tok = strtok_r(path, "/", &saveptr);
  while (tok != NULL) {
    if (j == 3)
      *device = tok;
    else if (j == 5) {
      *port = tok;
      break;
    }
    j++;
    tok = strtok_r(NULL, "/", &saveptr);
  }
  return (*device != NULL && *port != NULL) ? 0 : 1;
}

/* Core functions ************************************************************/

static int ib_read_value_file(const char *device, const char *port,
                              const char *filename, int ds_type, value_t *dst) {
  char path[PATH_MAX];
  if (snprintf(path, PATH_MAX, "/sys/class/infiniband/%s/ports/%s/%s", device,
               port, filename) < 0)
    return 1;
  if (parse_value_file(path, dst, ds_type) != 0)
    return 1;
  return 0;
}

/*
 * Used to parse files like this:
 * rate:       "100 Gb/sec"
 * state:      "4: ACTIVE"
 * phys_state: "5: LinkUp"
 */
static int ib_read_value_file_num_only(const char *device, const char *port,
                                       const char *filename, int ds_type,
                                       value_t *dst) {
  char path[PATH_MAX];
  FILE *fh;
  char buffer[256];

  if (snprintf(path, PATH_MAX, "/sys/class/infiniband/%s/ports/%s/%s", device,
               port, filename) < 0)
    return 1;

  // copied from parse_value_file()
  fh = fopen(path, "r");
  if (fh == NULL)
    return 1;
  if (fgets(buffer, sizeof(buffer), fh) == NULL) {
    fclose(fh);
    return 1;
  }
  fclose(fh);
  strstripnewline(buffer);

  // zero-out the first non-digit character
  for (int i = 0; i < sizeof(buffer); i++) {
    if (!isdigit(buffer[i])) {
      buffer[i] = '\0';
      break;
    }
  }

  return parse_value(buffer, dst, ds_type);
}

static void ib_submit(const char *device, const char *port, value_t *vs, int vc,
                      const char *type, const char *type_instance) {
  value_list_t vl = VALUE_LIST_INIT;
  vl.values = vs;
  vl.values_len = vc;
  sstrncpy(vl.plugin, "infiniband", sizeof(vl.plugin));
  snprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%s:%s", device,
           port);
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));
  plugin_dispatch_values(&vl);
}

/**
 * For further reading on the available sysfs files, see:
 * - Linux: ./Documentation/infiniband/sysfs.txt
 *
 * For further reading on the meaning of each counter, see the InfiniBand
 *   Architecture Specification, sections 14.2.5.6 and 16.1.3.5.
 **/
static int ib_read_port(const char *device, const char *port) {
  value_t value, values[2];

  /* PortInfo attributes */

  if (ib_read_value_file_num_only(device, port, "state", DS_TYPE_GAUGE,
                                  &value) == 0)
    ib_submit(device, port, &value, 1, "ib_state", "");
  if (ib_read_value_file_num_only(device, port, "phys_state", DS_TYPE_GAUGE,
                                  &value) == 0)
    ib_submit(device, port, &value, 1, "ib_phys_state", "");
  if (ib_read_value_file_num_only(device, port, "rate", DS_TYPE_GAUGE,
                                  &value) == 0)
    ib_submit(device, port, &value, 1, "ib_rate", ""); // units are Gb/s
  if (ib_read_value_file(device, port, "cap_mask", DS_TYPE_GAUGE, &value) == 0)
    ib_submit(device, port, &value, 1, "ib_cap_mask", "");
  if (ib_read_value_file(device, port, "lid", DS_TYPE_GAUGE, &value) == 0)
    ib_submit(device, port, &value, 1, "ib_lid", "");
  if (ib_read_value_file(device, port, "lid_mask_count", DS_TYPE_GAUGE,
                         &value) == 0)
    ib_submit(device, port, &value, 1, "ib_lid_mask_count", "");
  if (ib_read_value_file(device, port, "sm_lid", DS_TYPE_GAUGE, &value) == 0)
    ib_submit(device, port, &value, 1, "ib_sm_lid", "");
  if (ib_read_value_file(device, port, "sm_sl", DS_TYPE_GAUGE, &value) == 0)
    ib_submit(device, port, &value, 1, "ib_sm_sl", "");

  /* PortCounters */

  // Total number of data octets, divided by 4, received on all VLs at the port
  if ((ib_read_value_file(device, port, "counters/port_rcv_data",
                          DS_TYPE_DERIVE, &values[0]) == 0) &&
      (ib_read_value_file(device, port, "counters/port_xmit_data",
                          DS_TYPE_DERIVE, &values[1]) == 0)) {
    values[0].derive *= 4;
    values[1].derive *= 4;
    ib_submit(device, port, values, 2, "ib_octets", "");
  }
  // Total number of packets, including packets containing errors, and excluding
  //    link packets, received from all VLs on the port
  if ((ib_read_value_file(device, port, "counters/port_rcv_packets",
                          DS_TYPE_DERIVE, &values[0]) == 0) &&
      (ib_read_value_file(device, port, "counters/port_xmit_packets",
                          DS_TYPE_DERIVE, &values[1]) == 0))
    ib_submit(device, port, values, 2, "ib_packets", "total");
  // Total number of packets containing an error that were received on the port
  if (ib_read_value_file(device, port, "counters/port_rcv_errors",
                         DS_TYPE_DERIVE, &values[0]) == 0) {
    values[1].derive = 0;
    ib_submit(device, port, values, 2, "ib_packets", "errors");
  }
  // Total number of packets marked with the EBP delimiter received on the port.
  if (ib_read_value_file(device, port,
                         "counters/port_rcv_remote_physical_errors",
                         DS_TYPE_DERIVE, &values[0]) == 0) {
    values[1].derive = 0;
    ib_submit(device, port, values, 2, "ib_packets", "remote_physical_errors");
  }
  // Total number of packets received on the port that were discarded because
  //    they could not be forwarded by the switch relay
  if (ib_read_value_file(device, port, "counters/port_rcv_switch_relay_errors",
                         DS_TYPE_DERIVE, &values[0]) == 0) {
    values[1].derive = 0;
    ib_submit(device, port, values, 2, "ib_packets", "switch_relay_errors");
  }
  // Total number of outbound packets discarded by the port because the port is
  //    down or congested.
  if (ib_read_value_file(device, port, "counters/port_xmit_discards",
                         DS_TYPE_DERIVE, &values[1]) == 0) {
    values[0].derive = 0;
    ib_submit(device, port, values, 2, "ib_packets", "discards");
  }
  // Total number of packets not transmitted from the switch physical port
  // Total number of packets received on the switch physical port that are
  //    discarded
  if ((ib_read_value_file(device, port, "counters/port_rcv_constraint_errors",
                          DS_TYPE_DERIVE, &values[0]) == 0) &&
      (ib_read_value_file(device, port, "counters/port_xmit_constraint_errors",
                          DS_TYPE_DERIVE, &values[1]) == 0))
    ib_submit(device, port, values, 2, "ib_packets", "constraint_errors");
  // Number of incoming VL15 packets dropped due to resource limitations (e.g.,
  //    lack of buffers) in the port
  if (ib_read_value_file(device, port, "counters/VL15_dropped", DS_TYPE_DERIVE,
                         &values[0]) == 0) {
    values[1].derive = 0;
    ib_submit(device, port, values, 2, "ib_packets", "vl15_dropped");
  }
  // Total number of times the Port Training state machine has successfully
  //    completed the link error recovery process.
  if (ib_read_value_file(device, port, "counters/link_error_recovery",
                         DS_TYPE_DERIVE, &value) == 0)
    ib_submit(device, port, &value, 1, "ib_link_error_recovery", "recovered");
  // Total number of times the Port Training state machine has failed the link
  //    error recovery process and downed the link.
  if (ib_read_value_file(device, port, "counters/link_downed", DS_TYPE_DERIVE,
                         &value) == 0)
    ib_submit(device, port, &value, 1, "ib_link_error_recovery", "downed");
  // Total number of minor link errors detected on one or more physical lanes.
  if (ib_read_value_file(device, port, "counters/symbol_error", DS_TYPE_DERIVE,
                         &value) == 0)
    ib_submit(device, port, &value, 1, "ib_errors", "symbol_errors");
  // The number of times that the count of local physical errors exceeded the
  //    threshold specified by LocalPhyErrors
  if (ib_read_value_file(device, port, "counters/local_link_integrity_errors",
                         DS_TYPE_DERIVE, &value) == 0)
    ib_submit(device, port, &value, 1, "ib_errors",
              "local_link_integrity_errors");
  // The number of times that OverrunErrors consecutive flow control update
  //    periods occurred, each having at least one overrun error
  if (ib_read_value_file(device, port,
                         "counters/excessive_buffer_overrun_errors",
                         DS_TYPE_DERIVE, &value) == 0)
    ib_submit(device, port, &value, 1, "ib_errors",
              "excessive_buffer_overrun_errors");
  // The number of ticks during which the port selected by PortSelect had data
  //    to transmit but no data was sent during the entire tick
  if (ib_read_value_file(device, port, "counters/port_xmit_wait",
                         DS_TYPE_DERIVE, &value) == 0)
    ib_submit(device, port, &value, 1, "ib_xmit_wait", "");

  /* PortCountersExtended */

  if ((ib_read_value_file(device, port, "counters/unicast_rcv_packets",
                          DS_TYPE_DERIVE, &values[0]) == 0) &&
      (ib_read_value_file(device, port, "counters/unicast_xmit_packets",
                          DS_TYPE_DERIVE, &values[1]) == 0))
    ib_submit(device, port, values, 2, "ib_packets", "unicast");
  if ((ib_read_value_file(device, port, "counters/multicast_rcv_packets",
                          DS_TYPE_DERIVE, &values[0]) == 0) &&
      (ib_read_value_file(device, port, "counters/multicast_xmit_packets",
                          DS_TYPE_DERIVE, &values[1]) == 0))
    ib_submit(device, port, values, 2, "ib_packets", "multicast");

  return 0;
}

/* Plugin entrypoints ********************************************************/

static int infiniband_config(const char *key, const char *value) {
  if (ignorelist == NULL)
    ignorelist = ignorelist_create(1);

  if (strcasecmp(key, "Port") == 0) {
    ignorelist_add(ignorelist, value);
  } else if (strcasecmp(key, "IgnoreSelected") == 0) {
    int invert = 1;
    if (IS_TRUE(value))
      invert = 0;
    ignorelist_set_invert(ignorelist, invert);
  } else {
    return -1;
  }
  return 0;
}

static int infiniband_init(void) {
  glob_t g;

  if (ib_glob_ports(&g) != 0)
    plugin_unregister_read("infiniband"); // no ports found

  globfree(&g);
  return 0;
}

static int infiniband_read(void) {
  int rc = 0;
  glob_t g;
  char port_name[255];

  if (ib_glob_ports(&g) == 0) {
    for (int i = 0; i < g.gl_pathc; ++i) {
      char *device = NULL, *port = NULL;
      if (ib_parse_glob_port(g.gl_pathv[i], &device, &port) == 0) {
        snprintf(port_name, sizeof(port_name), "%s:%s", device, port);
        if (ignorelist_match(ignorelist, port_name) == 0)
          rc &= ib_read_port(device, port);
      }
    }
  }

  globfree(&g);
  return rc;
}

void module_register(void) {
  plugin_register_config("infiniband", infiniband_config, config_keys,
                         config_keys_num);
  plugin_register_init("infiniband", infiniband_init);
  plugin_register_read("infiniband", infiniband_read);
}
