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

#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(21, 8, 0)
#include "slurm_2180.h"
#else
#include "slurm_2020.h"
#endif

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

static void slurm_submit_gauge(const char *plugin_instance, const char *type,
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

static void slurm_submit_derive(const char *plugin_instance, const char *type,
                                const char *type_instance, derive_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.derive = value};
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
    slurm_submit_gauge(partition->name, "slurm_job_state", job_state_names[i],
                       partition->jobs_states_count[i]);
  }
  for (int i = 0; i < NUM_NODE_STATES; i++) {
    slurm_submit_gauge(partition->name, "slurm_node_state", node_state_names[i],
                       partition->nodes_states_count[i]);
  }
}

static void slurm_submit_stats(stats_info_response_msg_t *stats_resp) {
  // slurm load stats
  slurm_submit_gauge("slurm_load_stats", "threads", "server_thread_count",
                     stats_resp->server_thread_count);
  slurm_submit_gauge("slurm_load_stats", "threads", "agent_thread_count",
                     stats_resp->agent_count);
  slurm_submit_gauge("slurm_load_stats", "queue_length", "agent_queue_size",
                     stats_resp->agent_queue_size);
  slurm_submit_gauge("slurm_load_stats", "queue_length", "dbd_agent_queue_size",
                     stats_resp->dbd_agent_queue_size);

  // slurm scheduler stats
  slurm_submit_derive("slurm_sched_stats", "slurm_cycles", "schedule_cycles",
                      stats_resp->schedule_cycle_counter);
  slurm_submit_gauge("slurm_sched_stats", "slurm_cycle_last",
                     "schedule_cycle_last", stats_resp->schedule_cycle_last);
  slurm_submit_derive("slurm_sched_stats", "slurm_cycle_duration",
                      "schedule_cycle_duration",
                      stats_resp->schedule_cycle_sum);
  slurm_submit_derive("slurm_sched_stats", "slurm_cycle_depth",
                      "schedule_cycle_depth", stats_resp->schedule_cycle_depth);
  slurm_submit_gauge("slurm_sched_stats", "queue_length",
                     "schedule_queue_length", stats_resp->schedule_queue_len);

  // slurm job stats
  slurm_submit_derive("slurm_jobs_stats", "slurm_job_stats", "submitted",
                      stats_resp->jobs_submitted);
  slurm_submit_derive("slurm_jobs_stats", "slurm_job_stats", "started",
                      stats_resp->jobs_started);
  slurm_submit_derive("slurm_jobs_stats", "slurm_job_stats", "completed",
                      stats_resp->jobs_completed);
  slurm_submit_derive("slurm_jobs_stats", "slurm_job_stats", "canceled",
                      stats_resp->jobs_canceled);
  slurm_submit_derive("slurm_jobs_stats", "slurm_job_stats", "failed",
                      stats_resp->jobs_failed);

  // slurm backfill stats
  slurm_submit_derive("slurm_backfill_stats", "slurm_backfilled_jobs",
                      "backfilled_jobs", stats_resp->bf_backfilled_jobs);
  // The field bf_backfilled_pack_jobs was renamed in v20.02 to
  // bf_backfilled_het_jobs (commit #7ff37bfa)
#if SLURM_VERSION_NUMBER >= SLURM_VERSION_NUM(20, 2, 0)
  slurm_submit_derive("slurm_backfill_stats", "slurm_backfilled_jobs",
                      "backfilled_het_jobs",
                      stats_resp->bf_backfilled_het_jobs);
#else
  slurm_submit_derive("slurm_backfill_stats", "slurm_backfilled_jobs",
                      "backfilled_pack_jobs",
                      stats_resp->bf_backfilled_pack_jobs);
#endif
  slurm_submit_derive("slurm_backfill_stats", "slurm_cycles", "backfill_cycles",
                      stats_resp->bf_cycle_counter);
  slurm_submit_gauge("slurm_backfill_stats", "slurm_cycle_last",
                     "last_backfill_cycle", stats_resp->bf_cycle_last);
  slurm_submit_derive("slurm_backfill_stats", "slurm_cycle_duration",
                      "backfill_cycle_duration", stats_resp->bf_cycle_sum);
  slurm_submit_gauge("slurm_backfill_stats", "slurm_last_cycle_depth",
                     "backfill_last_cycle_depth", stats_resp->bf_last_depth);
  slurm_submit_gauge("slurm_backfill_stats", "slurm_last_cycle_depth",
                     "backfill_last_cycle_depth_try",
                     stats_resp->bf_last_depth_try);
  slurm_submit_derive("slurm_backfill_stats", "slurm_cycle_depth",
                      "backfill_cycle_depth", stats_resp->bf_depth_sum);
  slurm_submit_derive("slurm_backfill_stats", "slurm_cycle_depth",
                      "backfill_cycle_depth_try", stats_resp->bf_depth_try_sum);
  slurm_submit_gauge("slurm_backfill_stats", "queue_length",
                     "backfill_last_queue_length", stats_resp->bf_queue_len);
  slurm_submit_derive("slurm_backfill_stats", "slurm_queue_length",
                      "backfill_queue_length", stats_resp->bf_queue_len_sum);
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
