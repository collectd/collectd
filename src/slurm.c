/**
 * collectd - src/slurm.c
 * Copyright (C) 2018       Pablo Llopis
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
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
 *   Pablo Llopis <pablo.llopis at gmail.com>
 **/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define PLUGIN_NAME "slurm"
#define PART_NAME_SIZE 128

/* this function declaration is missing in slurm.h */
extern void slurm_free_stats_response_msg(stats_info_response_msg_t *msg);

enum slurm_node_states {
  MAINT_NONRESP,
  MAINT,
  REBOOT_NONRESP,
  REBOOT,
  DRAINING_MAINT,
  DRAINING_REBOOT,
  DRAINING_POWERUP,
  DRAINING_POWERDOWN,
  DRAINING_NONRESP,
  DRAINING,
  DRAINED_MAINT,
  DRAINED_REBOOT,
  DRAINED_POWERUP,
  DRAINED_POWERDOWN,
  DRAINED_NONRESP,
  DRAINED,
  FAILING_NONRESP,
  FAILING,
  FAIL_NONRESP,
  FAIL,
  CANCEL_REBOOT,
  POWER_DOWN,
  POWER_UP,
  DOWN_MAINT,
  DOWN_REBOOT,
  DOWN_POWERUP,
  DOWN_POWERDOWN,
  DOWN_NONRESP,
  DOWN,
  ALLOCATED_MAINT,
  ALLOCATED_REBOOT,
  ALLOCATED_POWERUP,
  ALLOCATED_POWERDOWN,
  ALLOCATED_NONRESP,
  ALLOCATED_COMP,
  ALLOCATED,
  COMPLETING_MAINT,
  COMPLETING_REBOOT,
  COMPLETING_POWERUP,
  COMPLETING_POWERDOWN,
  COMPLETING_NONRESP,
  COMPLETING,
  IDLE_MAINT,
  IDLE_REBOOT,
  IDLE_POWERUP,
  IDLE_POWERDOWN,
  IDLE_NONRESP,
  PERFCTRS,
  RESERVED,
  IDLE,
  MIXED_MAINT,
  MIXED_REBOOT,
  MIXED_POWERUP,
  MIXED_POWERDOWN,
  MIXED_NONRESP,
  MIXED,
  FUTURE_MAINT,
  FUTURE_REBOOT,
  FUTURE_POWERUP,
  FUTURE_POWERDOWN,
  FUTURE_NONRESP,
  FUTURE,
  RESUME,
  UNKNOWN_NONRESP,
  UNKNOWN,
  UNKNOWN2
};

char *node_state_names[] = {"MAINT_NONRESP",
                            "MAINT",
                            "REBOOT_NONRESP",
                            "REBOOT",
                            "DRAINING_MAINT",
                            "DRAINING_REBOOT",
                            "DRAINING_POWERUP",
                            "DRAINING_POWERDOWN",
                            "DRAINING_NONRESP",
                            "DRAINING",
                            "DRAINED_MAINT",
                            "DRAINED_REBOOT",
                            "DRAINED_POWERUP",
                            "DRAINED_POWERDOWN",
                            "DRAINED_NONRESP",
                            "DRAINED",
                            "FAILING_NONRESP",
                            "FAILING",
                            "FAIL_NONRESP",
                            "FAIL",
                            "CANCEL_REBOOT",
                            "POWER_DOWN",
                            "POWER_UP",
                            "DOWN_MAINT",
                            "DOWN_REBOOT",
                            "DOWN_POWERUP",
                            "DOWN_POWERDOWN",
                            "DOWN_NONRESP",
                            "DOWN",
                            "ALLOCATED_MAINT",
                            "ALLOCATED_REBOOT",
                            "ALLOCATED_POWERUP",
                            "ALLOCATED_POWERDOWN",
                            "ALLOCATED_NONRESP",
                            "ALLOCATED_COMP",
                            "ALLOCATED",
                            "COMPLETING_MAINT",
                            "COMPLETING_REBOOT",
                            "COMPLETING_POWERUP",
                            "COMPLETING_POWERDOWN",
                            "COMPLETING_NONRESP",
                            "COMPLETING",
                            "IDLE_MAINT",
                            "IDLE_REBOOT",
                            "IDLE_POWERUP",
                            "IDLE_POWERDOWN",
                            "IDLE_NONRESP",
                            "PERFCTRS",
                            "RESERVED",
                            "IDLE",
                            "MIXED_MAINT",
                            "MIXED_REBOOT",
                            "MIXED_POWERUP",
                            "MIXED_POWERDOWN",
                            "MIXED_NONRESP",
                            "MIXED",
                            "FUTURE_MAINT",
                            "FUTURE_REBOOT",
                            "FUTURE_POWERUP",
                            "FUTURE_POWERDOWN",
                            "FUTURE_NONRESP",
                            "FUTURE",
                            "RESUME",
                            "UNKNOWN_NONRESP",
                            "UNKNOWN",
                            "?"};

/* based on src/common/slurm_protocol_defs.c node_state_string function */
uint8_t slurm_node_state(uint32_t inx) {
  int base = (inx & NODE_STATE_BASE);
  bool comp_flag = (inx & NODE_STATE_COMPLETING);
  bool drain_flag = (inx & NODE_STATE_DRAIN);
  bool fail_flag = (inx & NODE_STATE_FAIL);
  bool maint_flag = (inx & NODE_STATE_MAINT);
  bool net_flag = (inx & NODE_STATE_NET);
  bool reboot_flag = (inx & NODE_STATE_REBOOT);
  bool res_flag = (inx & NODE_STATE_RES);
  bool resume_flag = (inx & NODE_RESUME);
  bool no_resp_flag = (inx & NODE_STATE_NO_RESPOND);
  bool power_down_flag = (inx & NODE_STATE_POWER_SAVE);
  bool power_up_flag = (inx & NODE_STATE_POWER_UP);

  if (maint_flag) {
    if (drain_flag || (base == NODE_STATE_ALLOCATED) ||
        (base == NODE_STATE_DOWN) || (base == NODE_STATE_MIXED))
      ;
    else if (no_resp_flag)
      return MAINT_NONRESP;
    else
      return MAINT;
  }
  if (reboot_flag) {
    if ((base == NODE_STATE_ALLOCATED) || (base == NODE_STATE_MIXED))
      ;
    else if (no_resp_flag)
      return REBOOT_NONRESP;
    else
      return REBOOT;
  }
  if (drain_flag) {
    if (comp_flag || (base == NODE_STATE_ALLOCATED) ||
        (base == NODE_STATE_MIXED)) {
      if (maint_flag)
        return DRAINING_MAINT;
      if (reboot_flag)
        return DRAINING_REBOOT;
      if (power_up_flag)
        return DRAINING_POWERUP;
      if (power_down_flag)
        return DRAINING_POWERDOWN;
      if (no_resp_flag)
        return DRAINING_NONRESP;
      return DRAINING;
    } else {
      if (maint_flag)
        return DRAINED_MAINT;
      if (reboot_flag)
        return DRAINED_REBOOT;
      if (power_up_flag)
        return DRAINED_POWERUP;
      if (power_down_flag)
        return DRAINED_POWERDOWN;
      if (no_resp_flag)
        return DRAINED_NONRESP;
      return DRAINED;
    }
  }
  if (fail_flag) {
    if (comp_flag || (base == NODE_STATE_ALLOCATED)) {
      if (no_resp_flag)
        return FAILING_NONRESP;
      return FAILING;
    } else {
      if (no_resp_flag)
        return FAIL_NONRESP;
      return FAIL;
    }
  }

  if (inx == NODE_STATE_CANCEL_REBOOT)
    return CANCEL_REBOOT;
  if (inx == NODE_STATE_POWER_SAVE)
    return POWER_DOWN;
  if (inx == NODE_STATE_POWER_UP)
    return POWER_UP;
  if (base == NODE_STATE_DOWN) {
    if (maint_flag)
      return DOWN_MAINT;
    if (reboot_flag)
      return DOWN_REBOOT;
    if (power_up_flag)
      return DOWN_POWERUP;
    if (power_down_flag)
      return DOWN_POWERDOWN;
    if (no_resp_flag)
      return DOWN_NONRESP;
    return DOWN;
  }

  if (base == NODE_STATE_ALLOCATED) {
    if (maint_flag)
      return ALLOCATED_MAINT;
    if (reboot_flag)
      return ALLOCATED_REBOOT;
    if (power_up_flag)
      return ALLOCATED_POWERUP;
    if (power_down_flag)
      return ALLOCATED_POWERDOWN;
    if (no_resp_flag)
      return ALLOCATED_NONRESP;
    if (comp_flag)
      return ALLOCATED_COMP;
    return ALLOCATED;
  }
  if (comp_flag) {
    if (maint_flag)
      return COMPLETING_MAINT;
    if (reboot_flag)
      return COMPLETING_REBOOT;
    if (power_up_flag)
      return COMPLETING_POWERUP;
    if (power_down_flag)
      return COMPLETING_POWERDOWN;
    if (no_resp_flag)
      return COMPLETING_NONRESP;
    return COMPLETING;
  }
  if (base == NODE_STATE_IDLE) {
    if (maint_flag)
      return IDLE_MAINT;
    if (reboot_flag)
      return IDLE_REBOOT;
    if (power_up_flag)
      return IDLE_POWERUP;
    if (power_down_flag)
      return IDLE_POWERDOWN;
    if (no_resp_flag)
      return IDLE_NONRESP;
    if (net_flag)
      return PERFCTRS;
    if (res_flag)
      return RESERVED;
    return IDLE;
  }
  if (base == NODE_STATE_MIXED) {
    if (maint_flag)
      return MIXED_MAINT;
    if (reboot_flag)
      return MIXED_REBOOT;
    if (power_up_flag)
      return MIXED_POWERUP;
    if (power_down_flag)
      return MIXED_POWERDOWN;
    if (no_resp_flag)
      return MIXED_NONRESP;
    return MIXED;
  }
  if (base == NODE_STATE_FUTURE) {
    if (maint_flag)
      return FUTURE_MAINT;
    if (reboot_flag)
      return FUTURE_REBOOT;
    if (power_up_flag)
      return FUTURE_POWERUP;
    if (power_down_flag)
      return FUTURE_POWERDOWN;
    if (no_resp_flag)
      return FUTURE_NONRESP;
    return FUTURE;
  }
  if (resume_flag)
    return RESUME;
  if (base == NODE_STATE_UNKNOWN) {
    if (no_resp_flag)
      return UNKNOWN_NONRESP;
    return UNKNOWN;
  }
  return UNKNOWN2;
}

#define NUM_NODE_STATES (sizeof(node_state_names) / sizeof(node_state_names[0]))

typedef struct partition_state_st {
  char name[PART_NAME_SIZE];
  uint32_t nodes_states_count[NUM_NODE_STATES];
  /* counts jobs states indexed by enum job_states in slurm.h */
  uint32_t jobs_states_count[JOB_END];
} partition_state_t;

/* based on enum job_states from slurm.h */
static const char *job_state_names[] = {
    "pending", "running",   "suspended", "complete",  "cancelled", "failed",
    "timeout", "node_fail", "preempted", "boot_fail", "deadline",  "oom",
};

static partition_state_t *alloc_partition_states(uint32_t num_partitions,
                                                 partition_info_t *partitions) {
  partition_state_t *partition_states;

  partition_states =
      (partition_state_t *)calloc(num_partitions, sizeof(partition_state_t));
  if (!partition_states) {
    return NULL;
  }

  for (int i = 0; i < num_partitions; i++)
    sstrncpy(partition_states[i].name, partitions[i].name, PART_NAME_SIZE);

  return partition_states;
}

static partition_state_t *find_partition(partition_state_t *partitions,
                                         uint32_t num_partitions, char *name) {
  partition_state_t *part = NULL;

  for (int i = 0; i < num_partitions; i++) {
    if (strncmp(name, partitions[i].name, PART_NAME_SIZE) == 0)
      part = &partitions[i];
  }

  return part;
}

/*
 * Submit one gauge value
 */
static void slurm_submit(const char *plugin_instance, const char *type,
                         const char *type_instance, value_t *values,
                         size_t values_len) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = values;
  vl.values_len = values_len;
  sstrncpy(vl.plugin, PLUGIN_NAME, sizeof(vl.plugin));
  if (plugin_instance != NULL)
    sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_instance != NULL)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static void slurm_submit_partition(partition_state_t *partition) {
  for (int i = 0; i < JOB_END; i++) {
    slurm_submit(partition->name, "slurm_job_state", job_state_names[i],
                 &(value_t){.gauge = partition->jobs_states_count[i]}, 1);
  }
  for (int i = 0; i < NUM_NODE_STATES; i++) {
    slurm_submit(partition->name, "slurm_node_state", node_state_names[i],
                 &(value_t){.gauge = partition->nodes_states_count[i]}, 1);
  }
}

static void slurm_submit_stats(stats_info_response_msg_t *stats_resp) {
  value_t load_values[] = {
      {.gauge = stats_resp->server_thread_count},
      {.gauge = stats_resp->agent_count},
      {.gauge = stats_resp->agent_queue_size},
      {.gauge = stats_resp->dbd_agent_queue_size},
  };
  slurm_submit("slurm_stats", "slurm_stats_load", NULL, load_values,
               STATIC_ARRAY_SIZE(load_values));

  value_t schedule_values[] = {
      {.counter = stats_resp->schedule_cycle_max},
      {.gauge = stats_resp->schedule_cycle_last},
      {.counter = stats_resp->schedule_cycle_sum},
      {.counter = stats_resp->schedule_cycle_counter},
      {.derive = stats_resp->schedule_cycle_depth},
      {.gauge = stats_resp->schedule_queue_len},
  };
  slurm_submit("slurm_stats", "slurm_stats_cycles", NULL, schedule_values,
               STATIC_ARRAY_SIZE(schedule_values));

  value_t jobs_values[] = {
      {.derive = stats_resp->jobs_submitted},
      {.derive = stats_resp->jobs_started},
      {.derive = stats_resp->jobs_completed},
      {.derive = stats_resp->jobs_canceled},
      {.derive = stats_resp->jobs_failed},
  };
  slurm_submit("slurm_stats", "slurm_stats_jobs", NULL, jobs_values,
               STATIC_ARRAY_SIZE(jobs_values));

  value_t bf_values[] = {
      {.derive = stats_resp->bf_backfilled_jobs},
      {.derive = stats_resp->bf_backfilled_pack_jobs},
      {.counter = stats_resp->bf_cycle_counter},
      {.gauge = stats_resp->bf_cycle_last},
      {.counter = stats_resp->bf_cycle_max},
      {.counter = stats_resp->bf_cycle_sum},
      {.gauge = stats_resp->bf_last_depth},
      {.gauge = stats_resp->bf_last_depth_try},
      {.counter = stats_resp->bf_depth_sum},
      {.counter = stats_resp->bf_depth_try_sum},
      {.gauge = stats_resp->bf_queue_len},
      {.counter = stats_resp->bf_queue_len_sum},
  };
  slurm_submit("slurm_stats", "slurm_stats_backfill", NULL, bf_values,
               STATIC_ARRAY_SIZE(bf_values));
}

static int slurm_read(void) {
  job_info_msg_t *job_buffer_ptr = NULL;
  job_info_t *job_ptr;
  partition_info_msg_t *part_buffer_ptr = NULL;
  partition_info_t *part_ptr;
  partition_state_t *partition_states;
  partition_state_t *partition_state;
  node_info_msg_t *node_buffer_ptr = NULL;
  node_info_t *node_ptr;
  stats_info_response_msg_t *stats_resp;
  stats_info_request_msg_t stats_req;

  if (slurm_load_jobs((time_t)NULL, &job_buffer_ptr, SHOW_ALL)) {
    ERROR(PLUGIN_NAME ": slurm_load_jobs error");
    return -1;
  }

  if (slurm_load_node((time_t)NULL, &node_buffer_ptr, SHOW_ALL)) {
    slurm_free_job_info_msg(job_buffer_ptr);
    ERROR(PLUGIN_NAME ": slurm_load_node error");
    return -1;
  }

  if (slurm_load_partitions((time_t)NULL, &part_buffer_ptr, 0)) {
    slurm_free_job_info_msg(job_buffer_ptr);
    slurm_free_node_info_msg(node_buffer_ptr);
    ERROR(PLUGIN_NAME ": slurm_load_partitions error");
    return -1;
  }

  stats_req.command_id = STAT_COMMAND_GET;
  if (slurm_get_statistics(&stats_resp, &stats_req)) {
    slurm_free_job_info_msg(job_buffer_ptr);
    slurm_free_node_info_msg(node_buffer_ptr);
    slurm_free_partition_info_msg(part_buffer_ptr);
    ERROR(PLUGIN_NAME ": slurm_get_statistics error");
  }

  /* SLURM APIs provide *non-relational* data about nodes, partitions and jobs.
   * We allocate a data structure that relates all three together, and the
   * following
   * two for loops fill this data structure. The data structure is an array
   * of partition_state_t that holds job and node states. */
  uint32_t num_partitions = part_buffer_ptr->record_count;
  partition_states =
      alloc_partition_states(num_partitions, part_buffer_ptr->partition_array);
  if (!partition_states) {
    slurm_free_job_info_msg(job_buffer_ptr);
    slurm_free_node_info_msg(node_buffer_ptr);
    slurm_free_partition_info_msg(part_buffer_ptr);
    ERROR(PLUGIN_NAME ": alloc_partition_states");
    return -1;
  }

  /* fill partition_states array with per-partition job state information */
  for (int i = 0; i < job_buffer_ptr->record_count; i++) {
    job_ptr = &job_buffer_ptr->job_array[i];
    partition_state =
        find_partition(partition_states, num_partitions, job_ptr->partition);
    if (!partition_state) {
      ERROR(PLUGIN_NAME ": slurm_read: cannot find partition %s from jobid %d"
                        " in partition list returned by slurm_load_partitions",
            job_ptr->partition, job_ptr->job_id);
      continue;
    }

    uint8_t job_state = job_ptr->job_state & JOB_STATE_BASE;
    partition_state->jobs_states_count[job_state]++;
  }

  /* fill partition_states array with per-partition node state information */
  for (int i = 0; i < part_buffer_ptr->record_count; i++) {
    part_ptr = &part_buffer_ptr->partition_array[i];

    partition_state =
        find_partition(partition_states, num_partitions, part_ptr->name);
    if (!partition_state) {
      ERROR(PLUGIN_NAME ": slurm_read: cannot find partition %s"
                        " in partition list returned by slurm_load_partitions",
            part_ptr->name);
      continue;
    }

    for (int j = 0; part_ptr->node_inx; j += 2) {
      if (part_ptr->node_inx[j] == -1)
        break;
      for (int k = part_ptr->node_inx[j]; k <= part_ptr->node_inx[j + 1]; k++) {
        node_ptr = &node_buffer_ptr->node_array[k];
        /* some non-existant nodes (name is NULL) may show up as node_state
         * FUTURE */
        uint8_t node_state = slurm_node_state(node_ptr->node_state);
        partition_state->nodes_states_count[node_state]++;
      }
    }
  }

  for (int i = 0; i < num_partitions; i++)
    slurm_submit_partition(&partition_states[i]);

  slurm_submit_stats(stats_resp);

  slurm_free_job_info_msg(job_buffer_ptr);
  slurm_free_node_info_msg(node_buffer_ptr);
  slurm_free_partition_info_msg(part_buffer_ptr);
  slurm_free_stats_response_msg(stats_resp);
  free(partition_states);
  return 0;
}

void module_register(void) { plugin_register_read("slurm", slurm_read); }
