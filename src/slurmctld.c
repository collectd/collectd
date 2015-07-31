/**
 * collectd - src/slurmctld.c
 * Copyright (C) 2015       Rémi Palancher
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
 *   Rémi Palancher <remi at rezib.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include <slurm/slurm.h>

/*
 * Submit a gauge related to a job number.
 */
static void slurmctld_submit_gauge (long unsigned int job_number,
                          char *type_instance,
                          gauge_t value)
{
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[1];
  values[0].gauge = value;

  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "slurmctld", sizeof (vl.plugin));
  ssnprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "job_%lu", job_number);
  sstrncpy (vl.type, "count", sizeof(vl.type));
  sstrncpy (vl.type_instance, type_instance, sizeof(vl.type_instance));

  vl.values = values;
  vl.values_len = 1;

  plugin_dispatch_values (&vl);
} /* void slurmctld_submit_gauge */

/*
 * Submit all gauges for a slurm job.
 */
static void slurmctld_report_job_info (job_info_t *job)
{
  slurmctld_submit_gauge(job->job_id, "nodes", (gauge_t) job->num_nodes);
  slurmctld_submit_gauge(job->job_id, "cpus", (gauge_t) job->num_cpus);
} /* void slurmctld_report_job_info */

/*
 * Send RPC to slurmctld in order to load the list of jobs. Then, it iterates
 * over the list of currently running jobs to submit their metrics.
 * Returns 0 on success, -1 on error.
 */
static int slurmctld_read (void)
{
  job_info_msg_t *job_info_msg = NULL;
  job_info_t *job = NULL;
  int i;
  int errcode = 0;

  errcode = slurm_load_jobs((time_t) NULL, &job_info_msg, 0);

  if (errcode) {
    ERROR("slurmctld plugin: error during slurm_load_jobs(): %d\n", errcode);
    slurm_free_job_info_msg(job_info_msg);
    return errcode;
  }

  for (i=0; i<job_info_msg->record_count; i++) {
    job = &(job_info_msg->job_array[i]);

    if ((job->job_state & JOB_STATE_BASE) == JOB_RUNNING)
      slurmctld_report_job_info(job);
    else
      DEBUG("slurmctld plugin: excluding job id: %" PRIu32 " because not "
            "running: %" PRIu16 "",
            job->job_id,
            job->job_state);
  }
  slurm_free_job_info_msg(job_info_msg);
  return (0);
} /* int slurmctld_read */

void module_register (void)
{
  plugin_register_read("slurmctld", slurmctld_read);
} /* void module_register */
