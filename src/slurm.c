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

#include "common.h"
#include "plugin.h"

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

typedef struct partition_state_st {
  char name[PART_NAME_SIZE];
  /* counts nodes states indexed by enum node_states in slurm.h */
  uint32_t nodes_states[NODE_STATE_END];
  /* counts jobs states indexed by enum job_states in slurm.h */
  uint32_t jobs_states[JOB_END];
  /* other node flags */
  uint32_t drain;
  uint32_t completing;
  uint32_t no_respond;
  uint32_t power_save;
  uint32_t fail;
} partition_state_t;

/* based on enum node_states from slurm.h */
static const char *node_state_names[] = {
    "unknown", "down", "idle", "allocated", "error", "mixed", "future",
};

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
                         const char *type_instance, gauge_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
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
                 partition->jobs_states[i]);
  }
  for (int i = 0; i < NODE_STATE_END; i++) {
    slurm_submit(partition->name, "slurm_node_state", node_state_names[i],
                 partition->nodes_states[i]);
  }
  slurm_submit(partition->name, "slurm_node_flag", "drain", partition->drain);
  slurm_submit(partition->name, "slurm_node_flag", "completing",
               partition->completing);
  slurm_submit(partition->name, "slurm_node_flag", "no_respond",
               partition->no_respond);
  slurm_submit(partition->name, "slurm_node_flag", "power_save",
               partition->power_save);
  slurm_submit(partition->name, "slurm_node_flag", "fail", partition->fail);
}

static void slurm_submit_stats(stats_info_response_msg_t *stats_resp) {
  slurm_submit("stats", "slurm_stats", "parts_packed",
               stats_resp->parts_packed);
  slurm_submit("stats", "slurm_stats", "server_thread_count",
               stats_resp->server_thread_count);
  slurm_submit("stats", "slurm_stats", "agent_queue_size",
               stats_resp->agent_queue_size);
  slurm_submit("stats", "slurm_stats", "schedule_cycle_max",
               stats_resp->schedule_cycle_max);
  slurm_submit("stats", "slurm_stats", "schedule_cycle_last",
               stats_resp->schedule_cycle_last);
  slurm_submit("stats", "slurm_stats", "schedule_cycle_sum",
               stats_resp->schedule_cycle_sum);
  slurm_submit("stats", "slurm_stats", "schedule_cycle_counter",
               stats_resp->schedule_cycle_counter);
  slurm_submit("stats", "slurm_stats", "schedule_cycle_depth",
               stats_resp->schedule_cycle_depth);
  slurm_submit("stats", "slurm_stats", "schedule_queue_len",
               stats_resp->schedule_queue_len);
  slurm_submit("stats", "slurm_stats", "jobs_submitted",
               stats_resp->jobs_submitted);
  slurm_submit("stats", "slurm_stats", "jobs_started",
               stats_resp->jobs_started);
  slurm_submit("stats", "slurm_stats", "jobs_completed",
               stats_resp->jobs_completed);
  slurm_submit("stats", "slurm_stats", "jobs_canceled",
               stats_resp->jobs_canceled);
  slurm_submit("stats", "slurm_stats", "jobs_failed", stats_resp->jobs_failed);
  slurm_submit("stats", "slurm_stats", "bf_backfilled_jobs",
               stats_resp->bf_backfilled_jobs);
  slurm_submit("stats", "slurm_stats", "bf_last_backfilled_jobs",
               stats_resp->bf_last_backfilled_jobs);
  slurm_submit("stats", "slurm_stats", "bf_cycle_counter",
               stats_resp->bf_cycle_counter);
  slurm_submit("stats", "slurm_stats", "bf_cycle_sum",
               stats_resp->bf_cycle_sum);
  slurm_submit("stats", "slurm_stats", "bf_cycle_last",
               stats_resp->bf_cycle_last);
  slurm_submit("stats", "slurm_stats", "bf_cycle_max",
               stats_resp->bf_cycle_max);
  slurm_submit("stats", "slurm_stats", "bf_last_depth",
               stats_resp->bf_last_depth);
  slurm_submit("stats", "slurm_stats", "bf_last_depth_try",
               stats_resp->bf_last_depth_try);
  slurm_submit("stats", "slurm_stats", "bf_depth_sum",
               stats_resp->bf_depth_sum);
  slurm_submit("stats", "slurm_stats", "bf_depth_try_sum",
               stats_resp->bf_depth_try_sum);
  slurm_submit("stats", "slurm_stats", "bf_queue_len",
               stats_resp->bf_queue_len);
  slurm_submit("stats", "slurm_stats", "bf_queue_len_sum",
               stats_resp->bf_queue_len_sum);
  slurm_submit("stats", "slurm_stats", "bf_active", stats_resp->bf_active);
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
    ERROR("slurm_load_jobs error");
    return -1;
  }

  if (slurm_load_node((time_t)NULL, &node_buffer_ptr, SHOW_ALL)) {
    slurm_free_job_info_msg(job_buffer_ptr);
    ERROR("slurm_load_node error");
    return -1;
  }

  if (slurm_load_partitions((time_t)NULL, &part_buffer_ptr, 0)) {
    slurm_free_job_info_msg(job_buffer_ptr);
    slurm_free_node_info_msg(node_buffer_ptr);
    ERROR("slurm_load_partitions error");
    return -1;
  }

  stats_req.command_id = STAT_COMMAND_GET;
  if (slurm_get_statistics(&stats_resp, &stats_req)) {
    slurm_free_job_info_msg(job_buffer_ptr);
    slurm_free_node_info_msg(node_buffer_ptr);
    slurm_free_partition_info_msg(part_buffer_ptr);
    ERROR("slurm_get_statistics error");
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
    ERROR("alloc_partition_states");
    return -1;
  }

  /* fill partition_states array with per-partition job state information */
  for (int i = 0; i < job_buffer_ptr->record_count; i++) {
    job_ptr = &job_buffer_ptr->job_array[i];
    partition_state =
        find_partition(partition_states, num_partitions, job_ptr->partition);
    if (!partition_state) {
      ERROR("slurm_read: cannot find partition %s from jobid %d"
            " in partition list returned by slurm_load_partitions",
            job_ptr->partition, job_ptr->job_id);
      continue;
    }

    uint8_t job_state = job_ptr->job_state & JOB_STATE_BASE;
    partition_state->jobs_states[job_state]++;
  }

  /* fill partition_states array with per-partition node state information */
  for (int i = 0; i < part_buffer_ptr->record_count; i++) {
    part_ptr = &part_buffer_ptr->partition_array[i];

    partition_state =
        find_partition(partition_states, num_partitions, part_ptr->name);
    if (!partition_state) {
      ERROR("slurm_read: cannot find partition %s"
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
        uint8_t node_state = node_ptr->node_state & NODE_STATE_BASE;
        partition_state->nodes_states[node_state]++;
        if (node_ptr->node_state & NODE_STATE_DRAIN)
          partition_state->drain++;
        if (node_ptr->node_state & NODE_STATE_COMPLETING)
          partition_state->completing++;
        if (node_ptr->node_state & NODE_STATE_NO_RESPOND)
          partition_state->no_respond++;
        if (node_ptr->node_state & NODE_STATE_POWER_SAVE)
          partition_state->power_save++;
        if (node_ptr->node_state & NODE_STATE_FAIL)
          partition_state->fail++;
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
