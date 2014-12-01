/*
 * collectd - src/apcups.c
 * Copyright (C) 2010 Julien Pichon
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General
 * Public License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 *
 * Authors:
 *   Julien Pichon <julienpichon7 at gmail.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <cups/cups.h>

/*
 * Private data types
 */
#define MAX_NAME_LEN	128
struct printer_entry_s
{
	char *name;
	char *description;
	int page_printed;
	struct printer_entry_s *next;
};
typedef struct printer_entry_s printer_entry_t;

static printer_entry_t *printer_list_head = NULL;

#if 0
static const char *config_key[] = {
	"",
};
#endif

void cups_destroy_printer (printer_entry_t *pe)
{
	if (pe == NULL)
		return;

	if (pe->next != NULL)
		 cups_destroy_printer (pe->next);

	sfree(pe->name);
	sfree(pe);
}

static int cups_add_printer (const char *name, const char *description)
{
	printer_entry_t *pe, *cur;

	if (name == NULL)
		return (-1);

	pe = calloc (1, sizeof (printer_entry_t));
	if (pe == NULL)
	{
		char errbuf[1024];
		ERROR ("cups plugin: calloc failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	pe->name = strdup (name);
	pe->description = strdup (description);
	pe->next = NULL;

	if (printer_list_head == NULL)
		printer_list_head = pe;
	else
	{
		for (cur = printer_list_head; cur->next != NULL; cur = cur->next);
		cur->next = pe;
	}

	return (0);
}

static int cups_init (void)
{
	int status, i, num;
	const char *description;

	cups_dest_t *dests;
	cups_dest_t *dest;

	num = cupsGetDests (&dests);	
	if (num < 1)
	{
		ERROR ("cups plugin: no printer was found, "
				"are you sure cups server is running ?");
		return (-1);
	}

	for (i = 0, dest = dests; i < num; i++, dest++)
	{
		description = cupsGetOption ("printer-info",
				dest->num_options, dest->options);

		status = cups_add_printer (dest->name, description);
		if (status != 0)
		{
			ERROR ("cups plugin: cups_init: cups_add_printer failed.");
			cups_destroy_printer (printer_list_head);
			return (-1);
		}
	}

	cupsFreeDests (num, dests);

	return (0);
} /* static int cups_init */

static void cups_submit (const char *plugin_instance,
		const char *type, counter_t cnt)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = cnt;

	vl.values     = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "cups", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance,
			sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));

	plugin_dispatch_values (&vl);
} /* void cups_submit */

static void cups_submit_all (void)
{
	printer_entry_t *pe;

	for (pe = printer_list_head; pe != NULL; pe = pe->next)
		cups_submit (pe->name, "cups_printed", pe->page_printed);
} /* void cups_submit_all */

static printer_entry_t *lookup_printer (const char *name)
{
	printer_entry_t *pe;

	if (name == NULL)
		return NULL;

	for (pe = printer_list_head; pe != NULL; pe = pe->next)
	{
		if (strncasecmp (name, pe->name, MAX_NAME_LEN) == 0)
			break;
	}

	return pe;
} /* printer_entry_t *lookup_printer */

static int update_stats (cups_job_t *jobs, int start, int length)
{
	int i;
	cups_job_t *job;
	printer_entry_t *pe;

	if (jobs == NULL || start < 0 || length < 0)
		return (-1);

	for (i = start, job = jobs + start; i < length; i++, job++)
	{
		/* quadratic complexity, use avl tree to get n log(n) ? */
		pe = lookup_printer (job->dest);
		if (pe == NULL)
		{
			WARNING ("cups: update_stats: trying to update statistics "
					"of an unexisting printer (`%s`)", job->dest);
			continue;
		}
		pe->page_printed++;
	}

	return (0);
} /* int update_stats */

static int last_num = 0;
static int cups_read (void)
{
	int num, status;
	cups_job_t *jobs;

	num = cupsGetJobs (&jobs, NULL /* from all printers */,
			0 /* from all users */, -1 /* all states (completed, active) */);

	/* no new jobs ? */
	if (num - last_num > 0)
	{
		status = update_stats (jobs, last_num, num);
		if (status != 0)
		{
			ERROR("cpus: cups_read: update_stats failed.");
			cupsFreeJobs (num, jobs);
			return (-1);
		}
	}

	last_num = num;
	cupsFreeJobs (num, jobs);

	cups_submit_all ();

	return (0);
} /* int cups_read */

static int cups_shutdown (void)
{
	cups_destroy_printer (printer_list_head);
	return (0);
} /* int cups_shutdown */

void module_register (void)
{
	plugin_register_read ("cups", cups_read);
	plugin_register_init ("cups", cups_init);
	plugin_register_shutdown ("cups", cups_shutdown);
	return;
} /* void module_register */
