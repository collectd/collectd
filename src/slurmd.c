/**
 * collectd - src/slurmd.c
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

#define MAX_JOBS 1024
#define MAX_JOB_NB_SIZE 16
#define MAX_JOB_CPUS 1024
#define MAX_CPUS_STR_SIZE 128
#define MAX_CPU_NAME_SIZE 128
#define MAX_PIDS 1024

static const char *config_keys[] =
{
  "CgroupMountPoint",
  "IgnoreAbsentCpuset",
  "SlurmdNodeName"
};

static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

/*
 * Contains a snapshot view of consumed CPU time.
 */

struct cpu_time_s
{
  long unsigned int user;
  long unsigned int nice;
  long unsigned int system;
  long unsigned int idle;
  long unsigned int iowait;
  long unsigned int irq;
  long unsigned int softirq;
  long unsigned int steal;
  long unsigned int guest;
  long unsigned int guest_nice;
};

typedef struct cpu_time_s cpu_time_t;

/*
 * Contains all informations about a job, with:
 *   - job number
 *   - global PSS
 *   - CPU usage counters
 */

typedef struct job_info_s job_info_t;

struct job_info_s {
  unsigned int job_number;
  cpu_time_t *cpu_time;
  long unsigned int pss;
  _Bool updated;
  job_info_t *next;
};

typedef struct job_info_s jobs_list_t;

/* configuration parameters */
static char *cgroup_mnt_pt = NULL;
static char *slurmd_node_name = NULL;
static _Bool ignore_absent_cpuset = 0;
/* global vars */
static jobs_list_t *jobs = NULL;

/*
 * jobs_cpu_time_list manipulation utilities
 */

/*
 * Adds a new job to the global jobs list.
 */
static job_info_t * slurmd_job_add (unsigned int job_number, cpu_time_t *initial_cpu_time)
{
  job_info_t *new_job = smalloc(sizeof(job_info_t));
  job_info_t *xjob = NULL;

  bzero(new_job, sizeof(job_info_t));

  new_job->job_number = job_number;
  new_job->cpu_time = initial_cpu_time;

  new_job->updated = 1;
  new_job->next = NULL;

  if (jobs == NULL) {
    jobs = new_job;
  } else {
    xjob = jobs;
    while (xjob->next)
      xjob = xjob->next;
    xjob->next = new_job;
  }

  return new_job;
} /* job_info_t * slurmd_job_add */

/*
 * Update the cpu time of the job with the new_cpu_time in parameter.
 */
static void slurmd_job_update_cpu_time (job_info_t *job, cpu_time_t *new_cpu_time)
{
  sfree(job->cpu_time);
  job->cpu_time = new_cpu_time;
  job->updated = 1;
} /* void slurmd_job_update_cpu_time */

/*
 * Finds the job whose number is in parameter out of the global jobs list
 * and returns a pointer to it. Return NULL if not found.
 */
static job_info_t * slurmd_job_find (unsigned int job_number)
{
  job_info_t *xjob = jobs;

  while(xjob) {
    if(xjob->job_number == job_number) {
      return xjob;
    }
    xjob = xjob->next;
  }
  return NULL;
} /* job_info_t * slurmd_job_find */

/*
 * Flags all jobs as outdated in the jobs global list.
 */
static void slurmd_jobs_flag_outdated (void)
{
  job_info_t *xjob = jobs;

  while(xjob) {
    xjob->updated = 0;
    xjob = xjob->next;
  }
} /* void slurmd_jobs_flag_outdated */

/*
 * Removes all jobs flagged as outdated in the jobs global list.
 */
static void slurmd_jobs_remove_outdated (void)
{
  job_info_t *cur_job, *next_job;

  if (jobs == NULL)
    return;

  cur_job = jobs;
  next_job = cur_job->next;

  /* if first elements of jobs list are outdated, remove them */
  while(cur_job && !cur_job->updated) {
    sfree(cur_job->cpu_time);
    sfree(cur_job);
    jobs = cur_job = next_job;
    if (next_job)
      next_job = next_job->next;
  }

  /* then iterate until the end of the list to remove outdated jobs */
  while(next_job) {
    if(!next_job->updated) {
      cur_job->next = next_job->next;
      sfree(next_job->cpu_time);
      sfree(next_job);
    } else {
      /* jump to next job */
      cur_job = next_job;
    }
    next_job = next_job->next;
  }
} /* void slurmd_jobs_remove_outdated */

static void slurmd_submit_value (long unsigned int job_number, char *type, char *type_instance, value_t value[])
{
  value_list_t vl = VALUE_LIST_INIT;

  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "slurmd", sizeof (vl.plugin));
  ssnprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "job_%lu", job_number);
  sstrncpy (vl.type, type, sizeof(vl.type));
  sstrncpy (vl.type_instance, type_instance, sizeof(vl.type_instance));

  vl.values = value;
  vl.values_len = 1;

  plugin_dispatch_values (&vl);
} /* void slurmd_submit_value */

static void slurmd_submit_gauge (long unsigned int job_number, char *type, char *type_instance, gauge_t value)
{
  value_t values[1];
  values[0].gauge = value;
  DEBUG("slurmd plugin: submitting gauge for job %lu %s: %f", job_number, type_instance, value);
  slurmd_submit_value(job_number, type, type_instance, values);
} /* void slurmd_submit_gauge */

static void slurmd_submit_derive (long unsigned int job_number, char *type, char *type_instance, derive_t value)
{
  value_t values[1];
  values[0].derive = value;
  DEBUG("slurmd plugin: submitting derive for job %lu %s: %"PRIi64, job_number, type_instance, value);
  slurmd_submit_value(job_number, type, type_instance, values);
} /* void slurmd_submit_derive */

/*
 * Submit all jobs' metrics
 */
static void slurmd_jobs_report_metrics (void)
{
  job_info_t *xjob = jobs;

  while(xjob) {

    /* PSS is in kBytes, submit in Bytes */
    slurmd_submit_gauge(xjob->job_number, "memory", "pss",
                        (gauge_t) (xjob->pss * 1024));
    slurmd_submit_derive(xjob->job_number, "cpu", "user",
                         (derive_t) xjob->cpu_time->user);
    slurmd_submit_derive(xjob->job_number, "cpu", "nice",
                         (derive_t) xjob->cpu_time->nice);
    slurmd_submit_derive(xjob->job_number, "cpu", "system",
                         (derive_t) xjob->cpu_time->system);
    slurmd_submit_derive(xjob->job_number, "cpu", "idle",
                         (derive_t) xjob->cpu_time->idle);
    slurmd_submit_derive(xjob->job_number, "cpu", "iowait",
                         (derive_t) xjob->cpu_time->iowait);
    slurmd_submit_derive(xjob->job_number, "cpu", "irq",
                         (derive_t) xjob->cpu_time->irq);
    slurmd_submit_derive(xjob->job_number, "cpu", "softirq",
                         (derive_t) xjob->cpu_time->softirq);
    slurmd_submit_derive(xjob->job_number, "cpu", "steal",
                         (derive_t) xjob->cpu_time->steal);
    slurmd_submit_derive(xjob->job_number, "cpu", "guest",
                         (derive_t) xjob->cpu_time->guest);
    slurmd_submit_derive(xjob->job_number, "cpu", "guest_nice",
                         (derive_t) xjob->cpu_time->guest_nice);

    xjob = xjob->next;

  }
} /* void slurmd_jobs_report_metrics */

/*
 * Does total += to_add for all cpu_time_t members.
 */
static void slurmd_add_cpu_time (cpu_time_t *total, cpu_time_t *to_add)
{
  total->user += to_add->user;
  total->nice += to_add->nice;
  total->system += to_add->system;
  total->idle += to_add->idle;
  total->iowait += to_add->iowait;
  total->irq += to_add->irq;
  total->softirq += to_add->softirq;
  total->steal += to_add->steal;
  total->guest += to_add->guest;
  total->guest_nice += to_add->guest_nice;
} /* void slurmd_add_cpu_time */

#if COLLECT_DEBUG
/*
 * Fill the string str in parameter with a string representation of all values
 * in the cpu_time_t in parameter.
 */
void slurmd_snprintf_cpu_times (char *str, int strlen, cpu_time_t *times)
{
  ssnprintf(str, strlen,
            "user: %lu nice: %lu system: %lu idle: %lu iowait: %lu irq: %lu "
            "softirq: %lu steal: %lu guest: %lu guest_nice: %lu",
            times->user,
            times->nice,
            times->system,
            times->idle,
            times->iowait,
            times->irq,
            times->softirq,
            times->steal,
            times->guest,
            times->guest_nice);
} /* void slurmd_snprintf_cpu_times */
#endif

/*
 * Returns an array of char* with all tokens extracted for a_str splitted
 * with a_delim.
 */
static char ** slurmd_str_split (char* a_str, const char a_delim)
{
  char **result = 0;
  size_t count = 0;
  char *tmp = a_str;
  char *last_delim = 0;
  char delim[2];
  char *save_ptr;
  delim[0] = a_delim;
  delim[1] = 0;

  /* Count how many elements will be extracted. */
  while (*tmp) {
    if (a_delim == *tmp) {
      count++;
      last_delim = tmp;
    }
    tmp++;
  }

  /* Add space for trailing token. */
  count += last_delim < (a_str + strlen(a_str) - 1);

  /* Add space for terminating null string so caller
     knows where the list of returned strings ends. */
  count++;

  result = smalloc(sizeof(char*) * count);

  if (result) {
    size_t idx  = 0;
    char* token = strtok_r(a_str, delim, &save_ptr);

    while (token) {
      assert(idx < count);
      *(result + idx++) = sstrdup(token);
      token = strtok_r(NULL, delim, &save_ptr);
    }
    assert(idx == count - 1);
    *(result + idx) = 0;
  }

  return result;
} /* char ** slurmd_str_split */

/*
 * Convert a string to a long and returns it, or -1 on error.
 */
static inline long slurmd_str_to_long (const char *cpu_str)
{
  char *endptr;
  long val = 0;
  errno = 0;
  val = strtol(cpu_str, &endptr, 10);

  if ((errno == ERANGE && (val == LONG_MAX || val == LONG_MIN))
       || (errno != 0 && val == 0)) {
    char errbuf[1024];
    ERROR("slurmd plugin: error with strtol(): %s", sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  if (endptr == cpu_str) {
    ERROR("slurmd plugin: no digits were found during strtol() conversion");
    return (-1);
  }

  return val;
} /* long slurmd_str_to_long */

/*
 * Sums all the PSS in /proc/<pid>/smaps for the PID given in parameter.
 *
 * Returns the sum of PSS of the PID or -1 on error.
 */
static unsigned int slurmd_get_pid_pss (const pid_t pid)
{
  char smaps_fpath[PATH_MAX];
  char buf[1024];
  FILE *smaps_fh;
  int nb_matches;
  long unsigned int pss = 0, xpss = 0;

  /* build smaps fpath */
  bzero(smaps_fpath, sizeof(smaps_fpath));
  ssnprintf(smaps_fpath, sizeof(smaps_fpath), "/proc/%d/smaps", pid);

  smaps_fh = fopen(smaps_fpath, "r");

  if (smaps_fh == NULL) {
    char errbuf[1024];
    WARNING("slurmd plugin: fopen() error on smaps file %s: %s", smaps_fpath, sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  /* Read all lines to make the sum of all Pss */
  while (fgets(buf, sizeof(buf), smaps_fh)) {
    nb_matches = sscanf(buf, "Pss: %lu kB\n", &xpss);
    if (nb_matches == 1)
      pss += xpss;
  }

  fclose(smaps_fh);
  DEBUG("slurmd plugin: total %lu kB for PID %i", pss, pid);
  return pss;
} /* int slurmd_get_pid_pss */

/*
 * Parses /proc/stat file calculature the sum of cpu time of all cpus
 * allocated to a job and given in parameters.
 *
 * Returns 0 on success, -1 on error.
 */
static int slurmd_get_job_cpus_time (int job_cpus[], int job_nb_cpus, cpu_time_t *result)
{
  char cur_cpu_name[MAX_CPU_NAME_SIZE];
  char searched_cpu_name[MAX_CPU_NAME_SIZE];
  int cpu_idx = 0;
  FILE *fstat = NULL;
  _Bool all_cpus_found = 0;
  cpu_time_t *xcpu_time = smalloc(sizeof(cpu_time_t));

  bzero(searched_cpu_name, sizeof(searched_cpu_name));
  bzero(cur_cpu_name, sizeof(cur_cpu_name));

  /* initialize result to 0 before doing sums */
  bzero(result, sizeof(cpu_time_t));
  bzero(xcpu_time, sizeof(cpu_time_t));

  fstat = fopen("/proc/stat", "r");

  if (fstat == NULL) {
    char errbuf[1024];
    WARNING("slurmd plugin: fopen() error on file /proc/stat: %s", sstrerror (errno, errbuf, sizeof (errbuf)));
    fclose(fstat);
    return (-1);
  }

  ssnprintf(searched_cpu_name, MAX_CPU_NAME_SIZE, "cpu%d", job_cpus[cpu_idx]);
  while (!all_cpus_found &&
         (fscanf(fstat, "%s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
                 cur_cpu_name, &xcpu_time->user, &xcpu_time->nice,
                 &xcpu_time->system, &xcpu_time->idle, &xcpu_time->iowait,
                 &xcpu_time->irq, &xcpu_time->softirq, &xcpu_time->steal,
                 &xcpu_time->guest, &xcpu_time->guest_nice)) != EOF) {
    /*
     * Check if CPU name matches.
     */
    if (strncmp(cur_cpu_name, searched_cpu_name, strlen(searched_cpu_name)) == 0) {
#if COLLECT_DEBUG
      char cpu_times_s[256];
      slurmd_snprintf_cpu_times(cpu_times_s, sizeof(cpu_times_s), xcpu_time);
      DEBUG("slurmd plugin: read %s jiffies: %s", cur_cpu_name, cpu_times_s);
#endif
      slurmd_add_cpu_time(result, xcpu_time);
      cpu_idx++;
      if (cpu_idx >= job_nb_cpus)
        all_cpus_found = 1;
      ssnprintf(searched_cpu_name, MAX_CPU_NAME_SIZE, "cpu%d", job_cpus[cpu_idx]);
    }
  }
  fclose(fstat);
  sfree(xcpu_time);

  return (0);
} /* int slurmd_get_job_cpus_time */

/*
 * Read the cpuset tasks file whose path is given in parameter and fill the
 * pids array with all PIDs found in this file.
 *
 * Returns the number of found PIDs, -1 on error.
 */
static int slurmd_read_tasks_pids (const char *tasks_fpath, pid_t pids[])
{
  int xpid = 0;
  int idx = 0;
  FILE *ftasks = fopen(tasks_fpath, "r");

  if (ftasks == NULL) {
    char errbuf[1024];
    WARNING("slurmd plugin: fopen() error on tasks file %s: %s", tasks_fpath, sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  while (fscanf(ftasks, "%d\n", &xpid) != EOF)
    pids[idx++] = xpid;

  fclose(ftasks);
  return idx;
} /* int slurmd_read_tasks_pids */

/*
 * Returns 1 if pid is in threads array, or 0 if not.
 */
static inline _Bool slurmd_pid_is_thread (pid_t pid, pid_t threads[], int nb_threads)
{
  int i;

  for (i=0; i<nb_threads; i++)
    if (pid == threads[i])
      return (1);

  return (0);
} /* _Bool slurmd_pid_is_thread */

/*
 * Spots threads among the PIDs in parameter by checking the content of the
 * /proc/<pid>/task/ directory. Fill the threads array in parameter with the
 * PIDs identified as threads.
 *
 * Returns the number of threads found.
 */
static int slurmd_track_threads (pid_t pids[], int nb_pids, pid_t threads[])
{
  int nb_threads = 0;
  char task_dir_fpath[PATH_MAX];
  DIR *dir;
  struct dirent dir_ent_buf, *dir_ent;
  int i;
  pid_t pid, xtid;

  /* initialize null-string */
  bzero(task_dir_fpath, sizeof(task_dir_fpath));

  for(i=0; i<nb_pids; i++) {
    pid = pids[i];
    if(!slurmd_pid_is_thread (pid, threads, nb_threads)) {
      ssnprintf(task_dir_fpath, PATH_MAX, "/proc/%d/task", pid);

      dir = opendir(task_dir_fpath);

      /*
       * If dir is NULL, it means the /proc/<pid>/task directory does not exist
       * and that the PID itself is a thread so simply ignore this case.
       */
      if (dir != NULL) {
        while(1) {
          if (readdir_r(dir, &dir_ent_buf, &dir_ent) != 0) {
            WARNING("slurmd plugin: problem during readdir_r() %s", task_dir_fpath);
            continue;
          }
          if (dir_ent == NULL)
            break;

          /* exclude . and .. dir entries */
          if ( strncmp(dir_ent->d_name, ".", 1) &&
               strncmp(dir_ent->d_name, "..", 2) ) {
            xtid = (pid_t)slurmd_str_to_long(dir_ent->d_name);
            if (xtid == -1)
               WARNING("slurmd plugin: weird task TID found %s for PID %d", dir_ent->d_name, pid);
            else if (xtid != pid)
               threads[nb_threads++] = xtid;
          }
        }
        closedir(dir);
      }
    }
  }

  return nb_threads;
} /* int slurmd_track_threads */

/*
 * Parses the PIDs found in the cpuset cgroup tasks file whose path is given
 * in parameter and compute the sum of PSS of all these PIDs (excepting
 * threads).
 *
 * Returns the sum of PSS of the PIDs found in the tasks file or -1 on error.
 */
static int slurmd_get_tasks_pss (const char *tasks_fpath)
{
  int idx = 0;
  int nb_pids = -1;
  int nb_threads = -1;
  unsigned int pss = 0, xpss = 0;

  pid_t pids[MAX_PIDS];
  /* used to track threads and exclude them from memory accounting */
  pid_t threads[MAX_PIDS];

  bzero(pids, sizeof(pids));

  nb_pids = slurmd_read_tasks_pids(tasks_fpath, pids);
  if(nb_pids < 0)
    return(-1);

  nb_threads = slurmd_track_threads(pids, nb_pids, threads);
  if(nb_pids < 0)
    return (-1);

  for(idx = 0 ; idx<nb_pids ; idx++) {
    /* Exclude threads */
    if (!slurmd_pid_is_thread(pids[idx], threads, nb_threads)) {
      xpss = slurmd_get_pid_pss(pids[idx]);
      if (xpss < 0)
        return (-1);
      else
        pss += xpss;
    }
  }

  return pss;
} /* int slurmd_get_tasks_pss */

/*
 * Browse a Slurm job step specific cgroup whose path is given in parameter
 * to find the tasks file and get the sum of all theses tasks PSS.
 *
 * Returns the total PSS consumed by the job step tasks or -1 on error
 */
static int slurmd_get_jobstep_pss (const char *step_cpuset_mnt_pt)
{
  char tasks_fpath[PATH_MAX];
  DIR *dir;
  struct dirent dir_ent_buf, *dir_ent;
  unsigned int pss = 0, xpss = 0;

  dir = opendir(step_cpuset_mnt_pt);
  if (dir == NULL) {
    WARNING("slurmd plugin: directory %s could not be open", step_cpuset_mnt_pt);
    return (-1);
  }

  while(1) {
    if (readdir_r(dir, &dir_ent_buf, &dir_ent) != 0) {
      WARNING("slurmd plugin: problem during readdir_r() %s", step_cpuset_mnt_pt);
      closedir(dir);
      return (-1);
    }
    if (dir_ent == NULL)
      break;

    /* check if ent name starts with 'job_' */
    if (strncmp(dir_ent->d_name, "tasks", 6) == 0) {
      ssnprintf(tasks_fpath, sizeof(tasks_fpath), "%s/%s",
                step_cpuset_mnt_pt, dir_ent->d_name);
      xpss = slurmd_get_tasks_pss(tasks_fpath);
      if (xpss < 0) {
        closedir(dir);
        return -1;
      }
      else
        pss += xpss;
    }
  }
  closedir(dir);

  return pss;
} /* int slurmd_get_jobstep_pss */

/*
 * Parses cpuset cpuset.cpus file of the cpuset cgroup whose path is given in
 * parameter, then it fills cpus array with the list of CPUs numbers found in
 * this file.
 *
 * Returns the number of CPUs or -1 on error.
 */
static int slurmd_parse_cpuset_cpus_list (const char* cpuset_fpath, int cpus[])
{
  char cpus_fpath[PATH_MAX];
  char **tokens;
  char *token, *inter_end, *save_ptr;
  char cpus_str[MAX_CPUS_STR_SIZE];
  FILE *cpus_fh;
  int i;
  int cpu_idx = 0;
  int xcpu_id, xcpu_id_start, xcpu_id_end;

  bzero(cpus_fpath, sizeof(cpus_fpath));
  bzero(cpus_str, sizeof(cpus_str));

  ssnprintf(cpus_fpath, sizeof(cpus_fpath), "%s/%s", cpuset_fpath, "cpuset.cpus");
  cpus_fh = fopen(cpus_fpath, "r");

  if (cpus_fh == NULL) {
    char errbuf[1024];
    WARNING("slurmd plugin: cpuset.cpus fopen() error on file %s: %s", cpus_fpath, sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  if(fscanf(cpus_fh, "%s\n", cpus_str) < 1) {
    char errbuf[1024];
    WARNING("slurmd plugin: fscanf() error on file %s: %s", cpus_fpath, sstrerror (errno, errbuf, sizeof (errbuf)));
    fclose(cpus_fh);
    return (-1);
  }
  fclose(cpus_fh);

  /*
   * Ex. file content: 1-4,6
   * 1st split on commas, then for each element check if interval and compute
   * it if so.
   */
  tokens = slurmd_str_split(cpus_str, ',');
  for (i = 0; (token = *(tokens + i)) != NULL; i++) {

    inter_end = strtok_r(token, "-", &save_ptr);
    inter_end = strtok_r(NULL, "-", &save_ptr);

    if (inter_end == NULL) {
      /* It is just a CPU number, not an interval of CPUs.
       * It just has to be converted into integer.
       */
      xcpu_id = slurmd_str_to_long(token);
      if (xcpu_id != -1) {
        DEBUG("slurmd plugin: adding CPU number [%d] %d", cpu_idx, xcpu_id);
        cpus[cpu_idx++] = xcpu_id;
      }
      else
        WARNING("slurmd plugin: weird CPU number %s found in cpuset.cpus file %s", token, cpus_fpath);
    } else {

      /* This a CPUs interval */
      xcpu_id_start = slurmd_str_to_long(token);
      xcpu_id_end = slurmd_str_to_long(inter_end);
      if(xcpu_id_start != -1 && xcpu_id_end != -1)
        for (xcpu_id = xcpu_id_start; xcpu_id <= xcpu_id_end; xcpu_id++) {
          DEBUG("slurmd plugin: adding CPU number [%d] %d", cpu_idx, xcpu_id);
          cpus[cpu_idx++] = xcpu_id;
        }
      else
        WARNING("slurmd plugin: weird CPU number %s/%s found in cpuset.cpus file %s", token, inter_end, cpus_fpath);
    }
    sfree(token);

  }
  sfree(tokens);

  return cpu_idx;
} /* int slurmd_parse_cpuset_cpus_list */

/*
 * Browse Slurm Job cgroup cpuset hierarchy.
 *
 * First, it looks at the CPUs allocated to the job and get a new snapshot of
 * those CPUs jiffies.
 *
 * Then, it browses all Slurm job's steps specific sub-cpusets to get all job's
 * PIDs and measure their memory consumption.
 *
 * Returns 0 on success, -1 on error.
 */
static int slurmd_browse_job_cpuset (char* job_cpuset_mnt_pt, unsigned long int job_number)
{
  char step_cpuset_mnt_pt[PATH_MAX];
  DIR *dir;
  struct dirent dir_ent_buf, *dir_ent;
  int job_cpus[MAX_JOB_CPUS];
  int job_nb_cpus = 0;
  int ret = 0;
  cpu_time_t *cpu_time = NULL;
  job_info_t *my_job;
  unsigned int job_pss = 0;
  int xpss = 0;

  /*
   * Get job's allocated CPUs
   */
  job_nb_cpus = slurmd_parse_cpuset_cpus_list(job_cpuset_mnt_pt, job_cpus);

  if(job_nb_cpus < 0)
    return (-1);

  /*
   * Get job's allocated CPUs usage
   */
  cpu_time = smalloc(sizeof(cpu_time_t));
  ret = slurmd_get_job_cpus_time(job_cpus, job_nb_cpus, cpu_time);

  if (ret < 0)
    return (-1);

  my_job = slurmd_job_find(job_number);
  if (my_job == NULL)
    my_job = slurmd_job_add(job_number, cpu_time);
  else
    slurmd_job_update_cpu_time(my_job, cpu_time);

  /*
   * Get all steps tasks usage
   */
  dir = opendir(job_cpuset_mnt_pt);
  if (dir == NULL) {
    WARNING("slurmd plugin: directory %s could not be open", job_cpuset_mnt_pt);
    return (-1);
  }

  ret = 0;

  while(1) {
    if (readdir_r(dir, &dir_ent_buf, &dir_ent) != 0) {
      WARNING("slurmd plugin: problem during readdir_r() %s", job_cpuset_mnt_pt);
      closedir(dir);
      return (-1);
    }
    if (dir_ent == NULL)
      break;

    if (strncmp(dir_ent->d_name, "step_", 5) == 0) {
        ssnprintf(step_cpuset_mnt_pt, sizeof(step_cpuset_mnt_pt), "%s/%s",
                  job_cpuset_mnt_pt, dir_ent->d_name);
        xpss = slurmd_get_jobstep_pss(step_cpuset_mnt_pt);
        if(xpss < 0)
          ret = -1;
        else
          job_pss += (unsigned int) xpss;
    }
  }
  closedir(dir);
  my_job->pss = job_pss;

  return ret;
} /* int slurmd_browse_job_cpuset */

/*
 * Browse Slurm UID cgroup cpuset hierarchy to search for Slurm job specific
 * sub-cpusets, and browse all of them.
 *
 * Returns 0 on success, -1 on error.
 */
static int slurmd_browse_uid_cpuset (const char *uid_cpuset_mnt_pt)
{
  char job_cpuset_mnt_pt[PATH_MAX];
  DIR *dir;
  struct dirent dir_ent_buf, *dir_ent;
  char job_number_str[MAX_JOB_NB_SIZE];
  char *job_number_addr = NULL;
  unsigned long int job_number = 0;
  int ret = 0; // return code

  bzero(job_number_str, MAX_JOB_NB_SIZE);

  dir = opendir(uid_cpuset_mnt_pt);
  if (dir == NULL) {
    ERROR("slurmd plugin: directory %s could not be open", uid_cpuset_mnt_pt);
    return (-1);
  }

  /* At this point, return error code if ALL jobs' cpuset fail. If at least one
     succeeds, return 0.
  */
  ret = -1;

  while(1) {
    if (readdir_r(dir, &dir_ent_buf, &dir_ent) != 0) {
      WARNING("slurmd plugin: problem during readdir_r() %s", uid_cpuset_mnt_pt);
      closedir(dir);
      return (-1);
    }
    if (dir_ent == NULL)
      break;

    /* check if dir_ent name starts with 'job_' */
    if (strncmp(dir_ent->d_name, "job_", 4) == 0) {

      /*
       * Extract job number out of dir entry name
       */
      job_number_addr = dir_ent->d_name + 4*sizeof(char);
      strncpy(job_number_str, job_number_addr, strlen(job_number_addr));
      job_number = slurmd_str_to_long(job_number_str);
      if (job_number != -1) {
        ssnprintf(job_cpuset_mnt_pt, sizeof(job_cpuset_mnt_pt), "%s/%s",
                  uid_cpuset_mnt_pt, dir_ent->d_name);
        if(!slurmd_browse_job_cpuset(job_cpuset_mnt_pt, job_number))
          ret = 0;
      } else
        WARNING("slurmd plugin: weird job number %s found", job_number_str);
    }
  }
  closedir(dir);
  return ret;
} /* int slurmd_browse_uid_cpuset */


/*
 * Browse Slurm cgroup cpuset hierarchy to search for Slurm UID specific
 * sub-cpusets, and browse all of them.
 *
 * Returns 0 on success, -1 on error.
 */
static int slurmd_update_jobs_usage (void)
{
  char slurm_cpuset_mnt_pt[PATH_MAX];
  char uid_cpuset_mnt_pt[PATH_MAX];
  char node_name[HOST_NAME_MAX];
  DIR *dir;
  struct dirent dir_ent_buf, *dir_ent;
  int ret = 0;

  /* Try to open cpuset/slurm in cgroups */
  bzero(slurm_cpuset_mnt_pt, PATH_MAX);
  ssnprintf(slurm_cpuset_mnt_pt, sizeof(slurm_cpuset_mnt_pt), "%s/%s",
            cgroup_mnt_pt, "cpuset/slurm");

  dir = opendir(slurm_cpuset_mnt_pt);

  /* There is no cpuset/slurm directory, check cpuset/slurm_<nodename> */
  if (dir == NULL) {
    bzero(node_name, HOST_NAME_MAX);
    if (slurmd_node_name != NULL)
    {
      sstrncpy(node_name, slurmd_node_name, sizeof(slurmd_node_name));
    } else {
      if ( gethostname(node_name, HOST_NAME_MAX) != 0) {
        ERROR("slurmd plugin: can't gethostname and none provided in config: %d", errno);
        return(-1);
      }
    }

    bzero(slurm_cpuset_mnt_pt, PATH_MAX);
    ssnprintf(slurm_cpuset_mnt_pt, sizeof(slurm_cpuset_mnt_pt), "%s/%s_%s",
              cgroup_mnt_pt, "cpuset/slurm", node_name);
    dir = opendir(slurm_cpuset_mnt_pt);
  }

  if (dir == NULL) {
    ERROR("slurmd plugin: directory %s could not be open", slurm_cpuset_mnt_pt);
    /* if IgnoreAbsentCpuset conf parameter is true, ignore the error */
    if (ignore_absent_cpuset)
      return (0);
    else
      return (-1);
  }

  while(1) {
    if (readdir_r(dir, &dir_ent_buf, &dir_ent) != 0) {
      WARNING("slurmd plugin: problem during readdir_r() %s", slurm_cpuset_mnt_pt);
      closedir(dir);
      return (-1);
    }
    if (dir_ent == NULL)
      break;

    /* check if dir_ent name starts with 'uid_' */
    if (strncmp(dir_ent->d_name, "uid_", 4) == 0) {
      ssnprintf(uid_cpuset_mnt_pt, sizeof(uid_cpuset_mnt_pt), "%s/%s",
                slurm_cpuset_mnt_pt, dir_ent->d_name);
      if(slurmd_browse_uid_cpuset(uid_cpuset_mnt_pt))
        ret = -1;
    }
  }
  closedir(dir);
  return ret;
} /* int slurmd_update_jobs_usage */

/* slurmd plugin read callback */
static int slurmd_read (void)
{
  int update_status = 0;

  /*
   * Flag all job metrics as outdated, read new job metrics then remove all
   * jobs still flagged as outdated.
   */
  slurmd_jobs_flag_outdated();
  update_status = slurmd_update_jobs_usage();
  slurmd_jobs_remove_outdated();

  /* report metrics only if update_status is OK */
  if(!update_status)
      slurmd_jobs_report_metrics();

  return update_status;
} /* int slurmd_read */

/* slurmd plugin config callback */
static int slurmd_config (const char *key, const char *value)
{
  if (strcasecmp(key, "CgroupMountPoint") == 0) {
    if (cgroup_mnt_pt != NULL)
      sfree(cgroup_mnt_pt);
    cgroup_mnt_pt = strdup(value);
  } else if (0 == strcasecmp (key, "IgnoreAbsentCpuset")) {
    if (IS_FALSE (value))
      ignore_absent_cpuset = 0;
    else
      ignore_absent_cpuset = 1;
  } else if (0 == strcasecmp (key, "SlurmdNodeName")) {
    if (slurmd_node_name != NULL)
      sfree(slurmd_node_name);
    slurmd_node_name = strdup(value);
  } else
    return (-1);
  return (0);
} /* int slurmd_config */

void module_register (void)
{
  plugin_register_config("slurmd", slurmd_config, config_keys, config_keys_num);
  plugin_register_read("slurmd", slurmd_read);
} /* void module_register */
