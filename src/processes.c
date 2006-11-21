/**
 * collectd - src/processes.c
 * Copyright (C) 2005  Lyonel Vincent
 * Copyright (C) 2006  Florian Forster (Mach code)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *   Lyonel Vincent <lyonel at ezix.org>
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_debug.h"
#include "configfile.h"

/* Include header files for the mach system, if they exist.. */
#if HAVE_THREAD_INFO
#  if HAVE_MACH_MACH_INIT_H
#    include <mach/mach_init.h>
#  endif
#  if HAVE_MACH_HOST_PRIV_H
#    include <mach/host_priv.h>
#  endif
#  if HAVE_MACH_MACH_ERROR_H
#    include <mach/mach_error.h>
#  endif
#  if HAVE_MACH_MACH_HOST_H
#    include <mach/mach_host.h>
#  endif
#  if HAVE_MACH_MACH_PORT_H
#    include <mach/mach_port.h>
#  endif
#  if HAVE_MACH_MACH_TYPES_H
#    include <mach/mach_types.h>
#  endif
#  if HAVE_MACH_MESSAGE_H
#    include <mach/message.h>
#  endif
#  if HAVE_MACH_PROCESSOR_SET_H
#    include <mach/processor_set.h>
#  endif
#  if HAVE_MACH_TASK_H
#    include <mach/task.h>
#  endif
#  if HAVE_MACH_THREAD_ACT_H
#    include <mach/thread_act.h>
#  endif
#  if HAVE_MACH_VM_REGION_H
#    include <mach/vm_region.h>
#  endif
#  if HAVE_MACH_VM_MAP_H
#    include <mach/vm_map.h>
#  endif
#  if HAVE_MACH_VM_PROT_H
#    include <mach/vm_prot.h>
#  endif
#  if HAVE_SYS_SYSCTL_H
#    include <sys/sysctl.h>
#  endif
/* #endif HAVE_THREAD_INFO */

#elif KERNEL_LINUX
#  if HAVE_LINUX_CONFIG_H
#    include <linux/config.h>
#  endif
#  ifndef CONFIG_HZ
#    define CONFIG_HZ 100
#  endif
#endif /* KERNEL_LINUX */

#define MODULE_NAME "processes"

#if HAVE_THREAD_INFO || KERNEL_LINUX
# define PROCESSES_HAVE_READ 1
#else
# define PROCESSES_HAVE_READ 0
#endif

#define BUFSIZE 256

static char *processes_file = "processes.rrd";
static char *processes_ds_def[] =
{
	"DS:running:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:sleeping:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:zombies:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:stopped:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:paging:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:blocked:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	NULL
};
static int processes_ds_num = 6;

static char *ps_rss_file = "processes/ps_rss-%s.rrd";
static char *ps_rss_ds_def[] =
{
	/* max = 2^63 - 1 */
	"DS:byte:GAUGE:"COLLECTD_HEARTBEAT":0:9223372036854775807",
	NULL
};
static int ps_rss_ds_num = 1;

static char *ps_cputime_file = "processes/ps_cputime-%s.rrd";
static char *ps_cputime_ds_def[] =
{
	/* 1 second in user-mode per second ought to be enough.. */
	"DS:user:COUNTER:"COLLECTD_HEARTBEAT":0:1000000",
	"DS:syst:COUNTER:"COLLECTD_HEARTBEAT":0:1000000",
	NULL
};
static int ps_cputime_ds_num = 2;

static char *ps_count_file = "processes/ps_count-%s.rrd";
static char *ps_count_ds_def[] =
{
	"DS:processes:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:threads:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	NULL
};
static int ps_count_ds_num = 2;

static char *ps_pagefaults_file = "processes/ps_pagefaults-%s.rrd";
static char *ps_pagefaults_ds_def[] =
{
	/* max = 2^63 - 1 */
	"DS:minflt:COUNTER:"COLLECTD_HEARTBEAT":0:9223372036854775807",
	"DS:majflt:COUNTER:"COLLECTD_HEARTBEAT":0:9223372036854775807",
	NULL
};
static int ps_pagefaults_ds_num = 2;

static char *config_keys[] =
{
	"Process",
	NULL
};
static int config_keys_num = 1;

typedef struct procstat_entry_s
{
	unsigned long id;
	unsigned long age;

	unsigned long num_proc;
	unsigned long num_lwp;
	unsigned long vmem_rss;

	unsigned long vmem_minflt;
	unsigned long vmem_majflt;
	unsigned long vmem_minflt_counter;
	unsigned long vmem_majflt_counter;

	unsigned long cpu_user;
	unsigned long cpu_system;
	unsigned long cpu_user_counter;
	unsigned long cpu_system_counter;

	struct procstat_entry_s *next;
} procstat_entry_t;

#define PROCSTAT_NAME_LEN 256
typedef struct procstat
{
	char          name[PROCSTAT_NAME_LEN];

	unsigned long num_proc;
	unsigned long num_lwp;
	unsigned long vmem_rss;

	unsigned long vmem_minflt_counter;
	unsigned long vmem_majflt_counter;

	unsigned long cpu_user_counter;
	unsigned long cpu_system_counter;

	struct procstat   *next;
	struct procstat_entry_s *instances;
} procstat_t;

static procstat_t *list_head_g = NULL;

#if HAVE_THREAD_INFO
static mach_port_t port_host_self;
static mach_port_t port_task_self;

static processor_set_name_array_t pset_list;
static mach_msg_type_number_t     pset_list_len;
/* #endif HAVE_THREAD_INFO */

#elif KERNEL_LINUX
static long pagesize_g;
#endif /* KERNEL_LINUX */

#if HAVE_THREAD_INFO | KERNEL_LINUX
static void ps_list_register (const char *name)
{
	procstat_t *new;
	procstat_t *ptr;

	if ((new = (procstat_t *) malloc (sizeof (procstat_t))) == NULL)
		return;
	memset (new, 0, sizeof (procstat_t));
	strncpy (new->name, name, PROCSTAT_NAME_LEN);

	for (ptr = list_head_g; ptr != NULL; ptr = ptr->next)
	{
		if (strcmp (ptr->name, name) == 0)
			return;
		if (ptr->next == NULL)
			break;
	}

	if (ptr == NULL)
		list_head_g = new;
	else
		ptr->next = new;
}

static procstat_t *ps_list_search (const char *name)
{
	procstat_t *ptr;

	for (ptr = list_head_g; ptr != NULL; ptr = ptr->next)
		if (strcmp (ptr->name, name) == 0)
			break;

	return (ptr);
}

static void ps_list_add (const char *name, procstat_entry_t *entry)
{
	procstat_t *ps;
	procstat_entry_t *pse;

	if (entry->id == 0)
		return;

	if ((ps = ps_list_search (name)) == NULL)
		return;

	for (pse = ps->instances; pse != NULL; pse = pse->next)
		if ((pse->id == entry->id) || (pse->next == NULL))
			break;

	if ((pse == NULL) || (pse->id != entry->id))
	{
		procstat_entry_t *new;

		new = (procstat_entry_t *) malloc (sizeof (procstat_entry_t));
		if (new == NULL)
			return;
		memset (new, 0, sizeof (procstat_entry_t));
		new->id = entry->id;

		if (pse == NULL)
			ps->instances = new;
		else
			pse->next = new;

		pse = new;
	}

	pse->age = 0;
	pse->num_proc = entry->num_proc;
	pse->num_lwp  = entry->num_lwp;
	pse->vmem_rss = entry->vmem_rss;

	ps->num_proc += pse->num_proc;
	ps->num_lwp  += pse->num_lwp;
	ps->vmem_rss += pse->vmem_rss;

	if ((entry->vmem_minflt_counter == 0)
			&& (entry->vmem_majflt_counter == 0))
	{
		pse->vmem_minflt_counter += entry->vmem_minflt;
		pse->vmem_minflt = entry->vmem_minflt;

		pse->vmem_majflt_counter += entry->vmem_majflt;
		pse->vmem_majflt = entry->vmem_majflt;
	}
	else
	{
		if (entry->vmem_minflt_counter < pse->vmem_minflt_counter)
		{
			pse->vmem_minflt = entry->vmem_minflt_counter
				+ (ULONG_MAX - pse->vmem_minflt_counter);
		}
		else
		{
			pse->vmem_minflt = entry->vmem_minflt_counter - pse->vmem_minflt_counter;
		}
		pse->vmem_minflt_counter = entry->vmem_minflt_counter;

		if (entry->vmem_majflt_counter < pse->vmem_majflt_counter)
		{
			pse->vmem_majflt = entry->vmem_majflt_counter
				+ (ULONG_MAX - pse->vmem_majflt_counter);
		}
		else
		{
			pse->vmem_majflt = entry->vmem_majflt_counter - pse->vmem_majflt_counter;
		}
		pse->vmem_majflt_counter = entry->vmem_majflt_counter;
	}

	ps->vmem_minflt_counter += pse->vmem_minflt;
	ps->vmem_majflt_counter += pse->vmem_majflt;

	if ((entry->cpu_user_counter == 0)
			&& (entry->cpu_system_counter == 0))
	{
		pse->cpu_user_counter += entry->cpu_user;
		pse->cpu_user = entry->cpu_user;

		pse->cpu_system_counter += entry->cpu_system;
		pse->cpu_system = entry->cpu_system;
	}
	else
	{
		if (entry->cpu_user_counter < pse->cpu_user_counter)
		{
			pse->cpu_user = entry->cpu_user_counter
				+ (ULONG_MAX - pse->cpu_user_counter);
		}
		else
		{
			pse->cpu_user = entry->cpu_user_counter - pse->cpu_user_counter;
		}
		pse->cpu_user_counter = entry->cpu_user_counter;

		if (entry->cpu_system_counter < pse->cpu_system_counter)
		{
			pse->cpu_system = entry->cpu_system_counter
				+ (ULONG_MAX - pse->cpu_system_counter);
		}
		else
		{
			pse->cpu_system = entry->cpu_system_counter - pse->cpu_system_counter;
		}
		pse->cpu_system_counter = entry->cpu_system_counter;
	}

	ps->cpu_user_counter   += pse->cpu_user;
	ps->cpu_system_counter += pse->cpu_system;
}

static void ps_list_reset (void)
{
	procstat_t *ps;
	procstat_entry_t *pse;
	procstat_entry_t *pse_prev;

	for (ps = list_head_g; ps != NULL; ps = ps->next)
	{
		ps->num_proc    = 0;
		ps->num_lwp     = 0;
		ps->vmem_rss    = 0;

		pse_prev = NULL;
		pse = ps->instances;
		while (pse != NULL)
		{
			if (pse->age > 10)
			{
				DBG ("Removing this procstat entry cause it's too old: "
						"id = %lu; name = %s;",
						pse->id, ps->name);

				if (pse_prev == NULL)
				{
					ps->instances = pse->next;
					free (pse);
					pse = ps->instances;
				}
				else
				{
					pse_prev->next = pse->next;
					free (pse);
					pse = pse_prev->next;
				}
			}
			else
			{
				pse->age++;
				pse_prev = pse;
				pse = pse->next;
			}
		} /* while (pse != NULL) */
	} /* for (ps = list_head_g; ps != NULL; ps = ps->next) */
}
#endif /* HAVE_THREAD_INFO | KERNEL_LINUX */

static int ps_config (char *key, char *value)
{
	if (strcasecmp (key, "Process") == 0)
	{
		ps_list_register (value);
	}
	else
	{
		return (-1);
	}

	return (0);
}

static void ps_init (void)
{
#if HAVE_THREAD_INFO
	kern_return_t status;

	port_host_self = mach_host_self ();
	port_task_self = mach_task_self ();

	if (pset_list != NULL)
	{
		vm_deallocate (port_task_self,
				(vm_address_t) pset_list,
				pset_list_len * sizeof (processor_set_t));
		pset_list = NULL;
		pset_list_len = 0;
	}

	if ((status = host_processor_sets (port_host_self,
					&pset_list,
				       	&pset_list_len)) != KERN_SUCCESS)
	{
		syslog (LOG_ERR, "host_processor_sets failed: %s\n",
			       	mach_error_string (status));
		pset_list = NULL;
		pset_list_len = 0;
		return;
	}
/* #endif HAVE_THREAD_INFO */

#elif KERNEL_LINUX
	pagesize_g = sysconf(_SC_PAGESIZE);
	DBG ("pagesize_g = %li; CONFIG_HZ = %i;",
			pagesize_g, CONFIG_HZ);
#endif /* KERNEL_LINUX */

	return;
}

static void ps_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, processes_file, val,
			processes_ds_def, processes_ds_num);
}

static void ps_rss_write (char *host, char *inst, char *val)
{
	char filename[256];
	int status;

	status = snprintf (filename, 256, ps_rss_file, inst);
	if ((status < 1) || (status >= 256))
		return;

	rrd_update_file (host, filename, val, ps_rss_ds_def, ps_rss_ds_num);
}

static void ps_cputime_write (char *host, char *inst, char *val)
{
	char filename[256];
	int status;

	status = snprintf (filename, 256, ps_cputime_file, inst);
	if ((status < 1) || (status >= 256))
		return;

	DBG ("host = %s; filename = %s; val = %s;",
			host, filename, val);
	rrd_update_file (host, filename, val,
			ps_cputime_ds_def, ps_cputime_ds_num);
}

static void ps_count_write (char *host, char *inst, char *val)
{
	char filename[256];
	int status;

	status = snprintf (filename, 256, ps_count_file, inst);
	if ((status < 1) || (status >= 256))
		return;

	DBG ("host = %s; filename = %s; val = %s;",
			host, filename, val);
	rrd_update_file (host, filename, val,
			ps_count_ds_def, ps_count_ds_num);
}

static void ps_pagefaults_write (char *host, char *inst, char *val)
{
	char filename[256];
	int status;

	status = snprintf (filename, 256, ps_pagefaults_file, inst);
	if ((status < 1) || (status >= 256))
		return;

	DBG ("host = %s; filename = %s; val = %s;",
			host, filename, val);
	rrd_update_file (host, filename, val,
			ps_pagefaults_ds_def, ps_pagefaults_ds_num);
}

#if PROCESSES_HAVE_READ
static void ps_submit (int running,
		int sleeping,
		int zombies,
		int stopped,
		int paging,
		int blocked)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%i:%i:%i:%i:%i:%i",
				(unsigned int) curtime,
				running, sleeping, zombies, stopped, paging,
				blocked) >= BUFSIZE)
		return;

	DBG ("running = %i; sleeping = %i; zombies = %i; stopped = %i; paging = %i; blocked = %i;",
			running, sleeping, zombies, stopped, paging, blocked);

	plugin_submit (MODULE_NAME, "-", buf);
}

static void ps_submit_proc_list (procstat_t *ps)
{
	char buffer[64];

	if (ps == NULL)
		return;

	snprintf (buffer, 64, "%u:%lu",
			(unsigned int) curtime,
			ps->vmem_rss);
	buffer[63] = '\0';
	plugin_submit ("ps_rss", ps->name, buffer);

	snprintf (buffer, 64, "%u:%u:%u",
			(unsigned int) curtime,
			/* Make the counter overflow */
			(unsigned int) (ps->cpu_user_counter   & 0xFFFFFFFF),
			(unsigned int) (ps->cpu_system_counter & 0xFFFFFFFF));
	buffer[63] = '\0';
	plugin_submit ("ps_cputime", ps->name, buffer);

	snprintf (buffer, 64, "%u:%lu:%lu",
			(unsigned int) curtime,
			ps->num_proc, ps->num_lwp);
	buffer[63] = '\0';
	plugin_submit ("ps_count", ps->name, buffer);

	snprintf (buffer, 64, "%u:%lu:%lu",
			(unsigned int) curtime,
			ps->vmem_minflt_counter, ps->vmem_majflt_counter);
	buffer[63] = '\0';
	plugin_submit ("ps_pagefaults", ps->name, buffer);

	DBG ("name = %s; num_proc = %lu; num_lwp = %lu; vmem_rss = %lu; "
			"vmem_minflt_counter = %lu; vmem_majflt_counter = %lu; "
			"cpu_user_counter = %lu; cpu_system_counter = %lu;",
			ps->name, ps->num_proc, ps->num_lwp, ps->vmem_rss,
			ps->vmem_minflt_counter, ps->vmem_majflt_counter, ps->cpu_user_counter,
			ps->cpu_system_counter);

}

#if KERNEL_LINUX
static int *ps_read_tasks (int pid)
{
	int *list = NULL;
	int  list_size = 1; /* size of allocated space, in elements */
	int  list_len = 0;  /* number of currently used elements */

	char           dirname[64];
	DIR           *dh;
	struct dirent *ent;

	snprintf (dirname, 64, "/proc/%i/task", pid);
	dirname[63] = '\0';

	if ((dh = opendir (dirname)) == NULL)
	{
		syslog (LOG_NOTICE, "processes plugin: Failed to open directory `%s'",
				dirname);
		return (NULL);
	}

	while ((ent = readdir (dh)) != NULL)
	{
		if (!isdigit (ent->d_name[0]))
			continue;

		if ((list_len + 1) >= list_size)
		{
			int *new_ptr;
			int  new_size = 2 * list_size;
			/* Comes in sizes: 2, 4, 8, 16, ... */

			new_ptr = (int *) realloc (list, (size_t) (sizeof (int) * new_size));
			if (new_ptr == NULL)
			{
				if (list != NULL)
					free (list);
				syslog (LOG_ERR, "processes plugin: "
						"Failed to allocate more memory.");
				return (NULL);
			}

			list = new_ptr;
			list_size = new_size;

			memset (list + list_len, 0, sizeof (int) * (list_size - list_len));
		}

		list[list_len] = atoi (ent->d_name);
		if (list[list_len] != 0)
			list_len++;
	}

	closedir (dh);

	assert (list_len < list_size);
	assert (list[list_len] == 0);

	return (list);
}

int ps_read_process (int pid, procstat_t *ps, char *state)
{
	char  filename[64];
	char  buffer[1024];
	FILE *fh;

	char *fields[64];
	char  fields_len;

	int  *tasks;
	int   i;

	int   ppid;
	int   name_len;

	long long unsigned cpu_user_counter;
	long long unsigned cpu_system_counter;
	long long unsigned vmem_rss;

	memset (ps, 0, sizeof (procstat_t));

	snprintf (filename, 64, "/proc/%i/stat", pid);
	filename[63] = '\0';

	if ((fh = fopen (filename, "r")) == NULL)
		return (-1);

	if (fgets (buffer, 1024, fh) == NULL)
	{
		fclose (fh);
		return (-1);
	}

	fclose (fh);

	fields_len = strsplit (buffer, fields, 64);
	if (fields_len < 24)
	{
		DBG ("`%s' has only %i fields..",
				filename, fields_len);
		return (-1);
	}
	else if (fields_len != 41)
	{
		DBG ("WARNING: (fields_len = %i) != 41", fields_len);
	}

	/* copy the name, strip brackets in the process */
	name_len = strlen (fields[1]) - 2;
	if ((fields[1][0] != '(') || (fields[1][name_len + 1] != ')'))
	{
		DBG ("No brackets found in process name: `%s'", fields[1]);
		return (-1);
	}
	fields[1] = fields[1] + 1;
	fields[1][name_len] = '\0';
	strncpy (ps->name, fields[1], PROCSTAT_NAME_LEN);

	ppid = atoi (fields[3]);

	if ((tasks = ps_read_tasks (pid)) == NULL)
	{
		/* This happends for zombied, e.g. */
		DBG ("ps_read_tasks (%i) failed.", pid);
		*state = 'Z';
		ps->num_lwp  = 0;
		ps->num_proc = 0;
	}
	else
	{
		*state = '\0';
		ps->num_lwp  = 0;
		ps->num_proc = 1;
		for (i = 0; tasks[i] != 0; i++)
			ps->num_lwp++;

		free (tasks);
		tasks = NULL;
	}

	/* Leave the rest at zero if this is only an LWP */
	if (ps->num_proc == 0)
	{
		DBG ("This is only an LWP: pid = %i; name = %s;",
				pid, ps->name);
		return (0);
	}

	cpu_user_counter   = atoll (fields[13]);
	cpu_system_counter = atoll (fields[14]);
	vmem_rss = atoll (fields[23]);
	ps->vmem_minflt_counter = atol (fields[9]);
	ps->vmem_majflt_counter = atol (fields[11]);
	
	/* Convert jiffies to useconds */
	cpu_user_counter   = cpu_user_counter   * 1000000 / CONFIG_HZ;
	cpu_system_counter = cpu_system_counter * 1000000 / CONFIG_HZ;
	vmem_rss = vmem_rss * pagesize_g;

	ps->cpu_user_counter = (unsigned long) cpu_user_counter;
	ps->cpu_system_counter = (unsigned long) cpu_system_counter;
	ps->vmem_rss = (unsigned long) vmem_rss;

	*state = fields[2][0];

	/* success */
	return (0);
} /* int ps_read_process (...) */
#endif /* KERNEL_LINUX */

#if HAVE_THREAD_INFO
static int mach_get_task_name (task_t t, int *pid, char *name, size_t name_max_len)
{
	int mib[4];

	struct kinfo_proc kp;
	size_t            kp_size;

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_PID;

	if (pid_for_task (t, pid) != KERN_SUCCESS)
		return (-1);
	mib[3] = *pid;

	kp_size = sizeof (kp);
	if (sysctl (mib, 4, &kp, &kp_size, NULL, 0) != 0)
		return (-1);

	if (name_max_len > (MAXCOMLEN + 1))
		name_max_len = MAXCOMLEN + 1;

	strncpy (name, kp.kp_proc.p_comm, name_max_len - 1);
	name[name_max_len - 1] = '\0';

	DBG ("pid = %i; name = %s;", *pid, name);

	/* We don't do the special handling for `p_comm == "LaunchCFMApp"' as
	 * `top' does it, because it is a lot of work and only used when
	 * debugging. -octo */

	return (0);
}
#endif /* HAVE_THREAD_INFO */

static void ps_read (void)
{
#if HAVE_THREAD_INFO
	kern_return_t            status;

	int                      pset;
	processor_set_t          port_pset_priv;

	int                      task;
	task_array_t             task_list;
	mach_msg_type_number_t   task_list_len;

	int                      task_pid;
	char                     task_name[MAXCOMLEN + 1];

	int                      thread;
	thread_act_array_t       thread_list;
	mach_msg_type_number_t   thread_list_len;
	thread_basic_info_data_t thread_data;
	mach_msg_type_number_t   thread_data_len;

	int running  = 0;
	int sleeping = 0;
	int zombies  = 0;
	int stopped  = 0;
	int blocked  = 0;

	procstat_t *ps;
	procstat_entry_t pse;

	ps_list_reset ();

	/*
	 * The Mach-concept is a little different from the traditional UNIX
	 * concept: All the work is done in threads. Threads are contained in
	 * `tasks'. Therefore, `task status' doesn't make much sense, since
	 * it's actually a `thread status'.
	 * Tasks are assigned to sets of processors, so that's where you go to
	 * get a list.
	 */
	for (pset = 0; pset < pset_list_len; pset++)
	{
		if ((status = host_processor_set_priv (port_host_self,
						pset_list[pset],
						&port_pset_priv)) != KERN_SUCCESS)
		{
			syslog (LOG_ERR, "host_processor_set_priv failed: %s\n",
					mach_error_string (status));
			continue;
		}

		if ((status = processor_set_tasks (port_pset_priv,
						&task_list,
						&task_list_len)) != KERN_SUCCESS)
		{
			syslog (LOG_ERR, "processor_set_tasks failed: %s\n",
					mach_error_string (status));
			mach_port_deallocate (port_task_self, port_pset_priv);
			continue;
		}

		for (task = 0; task < task_list_len; task++)
		{
			ps = NULL;
			if (mach_get_task_name (task_list[task],
						&task_pid,
						task_name, PROCSTAT_NAME_LEN) == 0)
				ps = ps_list_search (task_name);

			/* Collect more detailed statistics for this process */
			if (ps != NULL)
			{
				task_basic_info_data_t        task_basic_info;
				mach_msg_type_number_t        task_basic_info_len;
				task_events_info_data_t       task_events_info;
				mach_msg_type_number_t        task_events_info_len;
				task_absolutetime_info_data_t task_absolutetime_info;
				mach_msg_type_number_t        task_absolutetime_info_len;

				memset (&pse, '\0', sizeof (pse));
				pse.id = task_pid;

				task_basic_info_len = TASK_BASIC_INFO_COUNT;
				status = task_info (task_list[task],
						TASK_BASIC_INFO,
						(task_info_t) &task_basic_info,
						&task_basic_info_len);
				if (status != KERN_SUCCESS)
				{
					syslog (LOG_ERR, "task_info failed: %s",
							mach_error_string (status));
					continue; /* with next thread_list */
				}

				task_events_info_len = TASK_EVENTS_INFO_COUNT;
				status = task_info (task_list[task],
						TASK_EVENTS_INFO,
						(task_info_t) &task_events_info,
						&task_events_info_len);
				if (status != KERN_SUCCESS)
				{
					syslog (LOG_ERR, "task_info failed: %s",
							mach_error_string (status));
					continue; /* with next thread_list */
				}

				task_absolutetime_info_len = TASK_ABSOLUTETIME_INFO_COUNT;
				status = task_info (task_list[task],
						TASK_ABSOLUTETIME_INFO,
						(task_info_t) &task_absolutetime_info,
						&task_absolutetime_info_len);
				if (status != KERN_SUCCESS)
				{
					syslog (LOG_ERR, "task_info failed: %s",
							mach_error_string (status));
					continue; /* with next thread_list */
				}

				pse.num_proc++;
				pse.vmem_rss = task_basic_info.resident_size;

				pse.vmem_minflt_counter = task_events_info.cow_faults;
				pse.vmem_majflt_counter = task_events_info.faults;

				pse.cpu_user_counter = task_absolutetime_info.total_user;
				pse.cpu_system_counter = task_absolutetime_info.total_system;
			}

			status = task_threads (task_list[task], &thread_list,
					&thread_list_len);
			if (status != KERN_SUCCESS)
			{
				/* Apple's `top' treats this case a zombie. It
				 * makes sense to some extend: A `zombie'
				 * thread is nonsense, since the task/process
				 * is dead. */
				zombies++;
				DBG ("task_threads failed: %s",
						mach_error_string (status));
				if (task_list[task] != port_task_self)
					mach_port_deallocate (port_task_self,
							task_list[task]);
				continue; /* with next task_list */
			}

			for (thread = 0; thread < thread_list_len; thread++)
			{
				thread_data_len = THREAD_BASIC_INFO_COUNT;
				status = thread_info (thread_list[thread],
						THREAD_BASIC_INFO,
						(thread_info_t) &thread_data,
						&thread_data_len);
				if (status != KERN_SUCCESS)
				{
					syslog (LOG_ERR, "thread_info failed: %s",
							mach_error_string (status));
					if (task_list[task] != port_task_self)
						mach_port_deallocate (port_task_self,
								thread_list[thread]);
					continue; /* with next thread_list */
				}

				if (ps != NULL)
					pse.num_lwp++;

				switch (thread_data.run_state)
				{
					case TH_STATE_RUNNING:
						running++;
						break;
					case TH_STATE_STOPPED:
					/* What exactly is `halted'? */
					case TH_STATE_HALTED:
						stopped++;
						break;
					case TH_STATE_WAITING:
						sleeping++;
						break;
					case TH_STATE_UNINTERRUPTIBLE:
						blocked++;
						break;
					/* There is no `zombie' case here,
					 * since there are no zombie-threads.
					 * There's only zombie tasks, which are
					 * handled above. */
					default:
						syslog (LOG_WARNING,
								"Unknown thread status: %s",
								thread_data.run_state);
						break;
				} /* switch (thread_data.run_state) */

				if (task_list[task] != port_task_self)
				{
					status = mach_port_deallocate (port_task_self,
							thread_list[thread]);
					if (status != KERN_SUCCESS)
						syslog (LOG_ERR, "mach_port_deallocate failed: %s",
								mach_error_string (status));
				}
			} /* for (thread_list) */

			if ((status = vm_deallocate (port_task_self,
							(vm_address_t) thread_list,
							thread_list_len * sizeof (thread_act_t)))
					!= KERN_SUCCESS)
			{
				syslog (LOG_ERR, "vm_deallocate failed: %s",
						mach_error_string (status));
			}
			thread_list = NULL;
			thread_list_len = 0;

			/* Only deallocate the task port, if it isn't our own.
			 * Don't know what would happen in that case, but this
			 * is what Apple's top does.. ;) */
			if (task_list[task] != port_task_self)
			{
				status = mach_port_deallocate (port_task_self,
						task_list[task]);
				if (status != KERN_SUCCESS)
					syslog (LOG_ERR, "mach_port_deallocate failed: %s",
							mach_error_string (status));
			}

			if (ps != NULL)
				ps_list_add (task_name, &pse);
		} /* for (task_list) */

		if ((status = vm_deallocate (port_task_self,
				(vm_address_t) task_list,
				task_list_len * sizeof (task_t))) != KERN_SUCCESS)
		{
			syslog (LOG_ERR, "vm_deallocate failed: %s",
					mach_error_string (status));
		}
		task_list = NULL;
		task_list_len = 0;

		if ((status = mach_port_deallocate (port_task_self, port_pset_priv))
				!= KERN_SUCCESS)
		{
			syslog (LOG_ERR, "mach_port_deallocate failed: %s",
					mach_error_string (status));
		}
	} /* for (pset_list) */

	ps_submit (running, sleeping, zombies, stopped, -1, blocked);

	for (ps = list_head_g; ps != NULL; ps = ps->next)
		ps_submit_proc_list (ps);
/* #endif HAVE_THREAD_INFO */

#elif KERNEL_LINUX
	int running  = 0;
	int sleeping = 0;
	int zombies  = 0;
	int stopped  = 0;
	int paging   = 0;
	int blocked  = 0;

	struct dirent *ent;
	DIR           *proc;
	int            pid;

	int        status;
	procstat_t ps;
	procstat_entry_t pse;
	char       state;

	procstat_t *ps_ptr;

	running = sleeping = zombies = stopped = paging = blocked = 0;
	ps_list_reset ();

	if ((proc = opendir ("/proc")) == NULL)
	{
		syslog (LOG_ERR, "Cannot open `/proc': %s", strerror (errno));
		return;
	}

	while ((ent = readdir (proc)) != NULL)
	{
		if (!isdigit (ent->d_name[0]))
			continue;

		if ((pid = atoi (ent->d_name)) < 1)
			continue;

		status = ps_read_process (pid, &ps, &state);
		if (status != 0)
		{
			DBG ("ps_read_process failed: %i", status);
			continue;
		}

		pse.id       = pid;
		pse.age      = 0;

		pse.num_proc = ps.num_proc;
		pse.num_lwp  = ps.num_lwp;
		pse.vmem_rss = ps.vmem_rss;

		pse.vmem_minflt = 0;
		pse.vmem_minflt_counter = ps.vmem_minflt_counter;
		pse.vmem_majflt = 0;
		pse.vmem_majflt_counter = ps.vmem_majflt_counter;

		pse.cpu_user = 0;
		pse.cpu_user_counter = ps.cpu_user_counter;
		pse.cpu_system = 0;
		pse.cpu_system_counter = ps.cpu_system_counter;

		switch (state)
		{
			case 'R': running++;  break;
			case 'S': sleeping++; break;
			case 'D': blocked++;  break;
			case 'Z': zombies++;  break;
			case 'T': stopped++;  break;
			case 'W': paging++;   break;
		}

		ps_list_add (ps.name, &pse);
	}

	closedir (proc);

	ps_submit (running, sleeping, zombies, stopped, paging, blocked);

	for (ps_ptr = list_head_g; ps_ptr != NULL; ps_ptr = ps_ptr->next)
		ps_submit_proc_list (ps_ptr);
#endif /* KERNEL_LINUX */
}
#else
# define ps_read NULL
#endif /* PROCESSES_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, ps_init, ps_read, ps_write);
	plugin_register ("ps_rss", NULL, NULL, ps_rss_write);
	plugin_register ("ps_cputime", NULL, NULL, ps_cputime_write);
	plugin_register ("ps_count", NULL, NULL, ps_count_write);
	plugin_register ("ps_pagefaults", NULL, NULL, ps_pagefaults_write);
	cf_register (MODULE_NAME, ps_config, config_keys, config_keys_num);
}

#undef BUFSIZE
#undef MODULE_NAME
