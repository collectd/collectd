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

/* Include header files for the mach system, if they exist.. */
#if HAVE_MACH_MACH_INIT_H
#  include <mach/mach_init.h>
#endif
#if HAVE_MACH_HOST_PRIV_H
#  include <mach/host_priv.h>
#endif
#if HAVE_MACH_MACH_ERROR_H
#  include <mach/mach_error.h>
#endif
#if HAVE_MACH_MACH_HOST_H
#  include <mach/mach_host.h>
#endif
#if HAVE_MACH_MACH_PORT_H
#  include <mach/mach_port.h>
#endif
#if HAVE_MACH_MACH_TYPES_H
#  include <mach/mach_types.h>
#endif
#if HAVE_MACH_MESSAGE_H
#  include <mach/message.h>
#endif
#if HAVE_MACH_PROCESSOR_SET_H
#  include <mach/processor_set.h>
#endif
#if HAVE_MACH_TASK_H
#  include <mach/task.h>
#endif
#if HAVE_MACH_THREAD_ACT_H
#  include <mach/thread_act.h>
#endif
#if HAVE_MACH_VM_REGION_H
#  include <mach/vm_region.h>
#endif
#if HAVE_MACH_VM_MAP_H
#  include <mach/vm_map.h>
#endif
#if HAVE_MACH_VM_PROT_H
#  include <mach/vm_prot.h>
#endif

#define MODULE_NAME "processes"

#if HAVE_THREAD_INFO || KERNEL_LINUX
# define PROCESSES_HAVE_READ 1
#else
# define PROCESSES_HAVE_READ 0
#endif

#define BUFSIZE 256

static char *ps_file = "processes.rrd";

static char *ds_def[] =
{
	"DS:running:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:sleeping:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:zombies:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:stopped:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:paging:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:blocked:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	NULL
};
static int ds_num = 6;

typedef struct procstat
{
#define PROCSTAT_NAME_LEN 256
	char         name[PROCSTAT_NAME_LEN];
	unsigned int num_proc;
	unsigned int num_lwp;
	unsigned int vmem_rss;
	unsigned int vmem_minflt;
	unsigned int vmem_majflt;
	unsigned int cpu_user;
	unsigned int cpu_system;
	struct procstat *next;
} procstat_t;

#if HAVE_THREAD_INFO
static mach_port_t port_host_self;
static mach_port_t port_task_self;

static processor_set_name_array_t pset_list;
static mach_msg_type_number_t     pset_list_len;
/* #endif HAVE_THREAD_INFO */

#elif KERNEL_LINUX
/* No global variables */
#endif /* KERNEL_LINUX */

static void ps_list_add (procstat_t *list, procstat_t *entry)
{
	procstat_t *ptr;

	ptr = list;
	while ((ptr != NULL) && (strcmp (ptr->name, entry->name) != 0))
		ptr = ptr->next;

	if (ptr == NULL)
		return;

	ptr->num_proc    += entry->num_proc;
	ptr->num_lwp     += entry->num_lwp;
	ptr->vmem_rss    += entry->vmem_rss;
	ptr->vmem_minflt += entry->vmem_minflt;
	ptr->vmem_maxflt += entry->vmem_maxflt;
	ptr->cpu_user    += entry->cpu_user;
	ptr->cpu_system  += entry->cpu_system;
}

static void ps_list_reset (procstat_t *ps)
{
	while (ps != NULL)
	{
		ps->num_proc    = 0;
		ps->num_lwp     = 0;
		ps->vmem_rss    = 0;
		ps->vmem_minflt = 0;
		ps->vmem_maxflt = 0;
		ps->cpu_user    = 0;
		ps->cpu_system  = 0;
		ps = ps->next;
	}
}

static int ps_config (char *key, char *value)
{
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
	/* No init */
#endif /* KERNEL_LINUX */

	return;
}

static void ps_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, ps_file, val, ds_def, ds_num);
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

static void ps_read (void)
{
#if HAVE_THREAD_INFO
	kern_return_t            status;

	int                      pset;
	processor_set_t          port_pset_priv;

	int                      task;
	task_array_t             task_list;
	mach_msg_type_number_t   task_list_len;

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
					syslog (LOG_ERR, "thread_info failed: %s\n",
							mach_error_string (status));
					if (task_list[task] != port_task_self)
						mach_port_deallocate (port_task_self,
								thread_list[thread]);
					continue; /* with next thread_list */
				}

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
/* #endif HAVE_THREAD_INFO */

#elif KERNEL_LINUX
	int running  = 0;
	int sleeping = 0;
	int zombies  = 0;
	int stopped  = 0;
	int paging   = 0;
	int blocked  = 0;

	char buf[BUFSIZE];
	char filename[20]; /* need 17 bytes */
	char *fields[BUFSIZE];

	struct dirent *ent;
	DIR *proc;
	FILE *fh;

	running = sleeping = zombies = stopped = paging = blocked = 0;

	if ((proc = opendir ("/proc")) == NULL)
	{
		syslog (LOG_ERR, "Cannot open `/proc': %s", strerror (errno));
		return;
	}

	while ((ent = readdir (proc)) != NULL)
	{
		if (!isdigit (ent->d_name[0]))
			continue;

		if (snprintf (filename, 20, "/proc/%s/stat", ent->d_name) >= 20)
			continue;

		if ((fh = fopen (filename, "r")) == NULL)
		{
			syslog (LOG_NOTICE, "Cannot open `%s': %s", filename,
					strerror (errno));
			continue;
		}

		if (fgets (buf, BUFSIZE, fh) == NULL)
		{
			syslog (LOG_NOTICE, "Unable to read from `%s': %s",
					filename, strerror (errno));
			fclose (fh);
			continue;
		}

		fclose (fh);

		if (strsplit (buf, fields, BUFSIZE) < 3)
		{
			DBG ("Line has less than three fields.");
			continue;
		}

		switch (fields[2][0])
		{
			case 'R': running++;  break;
			case 'S': sleeping++; break;
			case 'D': blocked++;  break;
			case 'Z': zombies++;  break;
			case 'T': stopped++;  break;
			case 'W': paging++;   break;
		}
	}

	closedir (proc);

	ps_submit (running, sleeping, zombies, stopped, paging, blocked);
#endif /* KERNEL_LINUX */
}
#else
# define ps_read NULL
#endif /* PROCESSES_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, ps_init, ps_read, ps_write);
}

#undef BUFSIZE
#undef MODULE_NAME
