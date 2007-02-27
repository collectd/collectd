/**
 * collectd - src/irq.c
 * Copyright (C) 2007  Peter Holik
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
 *   Peter Holik <peter at holik.at>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#define MODULE_NAME "irq"

#if KERNEL_LINUX
# define IRQ_HAVE_READ 1
#else
# define IRQ_HAVE_READ 0
#endif

#define BUFSIZE 128

/*
 * (Module-)Global variables
 */
static char *irq_file   = "irq-%s.rrd";

static char *config_keys[] =
{
	"Irq",
	"IgnoreSelected",
	NULL
};
static int config_keys_num = 2;

static char *ds_def[] =
{
	"DS:irq:COUNTER:"COLLECTD_HEARTBEAT":0:U",
	NULL
};
static int ds_num = 1;

static unsigned int *irq_list;
static unsigned int irq_list_num;

static int base = 10;

/* 
 * irq_list_action:
 * 0 => default is to collect selected irqs
 * 1 => ignore selcted irqs
 */
static int irq_list_action;

static int irq_config (char *key, char *value)
{
	unsigned int *temp;
	unsigned int irq;
        char *endptr;

	if (strcasecmp (key, "Irq") == 0)
	{
		temp = (unsigned int *) realloc (irq_list, (irq_list_num + 1) * sizeof (unsigned int *));
		if (temp == NULL)
		{
			syslog (LOG_EMERG, "Cannot allocate more memory.");
			return (1);
		}
		irq_list = temp;

		irq = strtol(value, &endptr, base);

		if (endptr == value ||
		    (errno == ERANGE && (irq == LONG_MAX || irq == LONG_MIN)) ||
		    (errno != 0 && irq == 0))
		{
			syslog (LOG_EMERG, "Irq value is not a number.");
			return (1);
		}
		irq_list[irq_list_num] = irq;
		irq_list_num++;
	}
	else if (strcasecmp (key, "IgnoreSelected") == 0)
	{
		if ((strcasecmp (value, "True") == 0)
				|| (strcasecmp (value, "Yes") == 0)
				|| (strcasecmp (value, "On") == 0))
			irq_list_action = 1;
		else
			irq_list_action = 0;
	}
	else
	{
		return (-1);
	}
	return (0);
}

/*
 * Check if this interface/instance should be ignored. This is called from
 * both, `submit' and `write' to give client and server the ability to
 * ignore certain stuff..
 */
static int check_ignore_irq (const unsigned int irq)
{
	int i;

	if (irq_list_num < 1)
		return (0);

	for (i = 0; i < irq_list_num; i++)
		if (irq == irq_list[i])
			return (irq_list_action);

	return (1 - irq_list_action);
}

static void irq_write (char *host, char *inst, char *value)
{
	char file[BUFSIZE];
	int status;

	if (check_ignore_irq (atoi(inst)))
		return;

	status = snprintf (file, BUFSIZE, irq_file, inst);
	if (status < 1)
		return;
	else if (status >= BUFSIZE)
		return;

	rrd_update_file (host, file, value, ds_def, ds_num);
}

#if IRQ_HAVE_READ
static void irq_submit (unsigned int irq, unsigned int value, char *devices)
{
	char buf[BUFSIZE];
	char desc[BUFSIZE];
	int  status;

	if (check_ignore_irq (irq))
		return;

	status = snprintf (buf, BUFSIZE, "%u:%u",
				(unsigned int) curtime, value);

	if ((status >= BUFSIZE) || (status < 1))
		return;

	status = snprintf (desc, BUFSIZE, "%d-%s", irq, devices);

	if ((status >= BUFSIZE) || (status < 1))
		return;

	plugin_submit (MODULE_NAME, desc, buf);
}

static void irq_read (void)
{
#if KERNEL_LINUX

#undef BUFSIZE
#define BUFSIZE 256

	FILE *fh;
	char buffer[BUFSIZE];
	unsigned int irq;
	unsigned int irq_value;
	long value;
	char *ptr, *endptr;

	if ((fh = fopen ("/proc/interrupts", "r")) == NULL)
	{
		syslog (LOG_WARNING, "irq: fopen: %s", strerror (errno));
		return;
	}
	while (fgets (buffer, BUFSIZE, fh) != NULL)
	{
		errno = 0;    /* To distinguish success/failure after call */
		irq = strtol(buffer, &endptr, base);

		if (endptr == buffer ||
		    (errno == ERANGE && (irq == LONG_MAX || irq == LONG_MIN)) ||
		    (errno != 0 && irq == 0)) continue;

		if (*endptr != ':') continue;

                ptr = ++endptr;

		irq_value = 0;
		/* sum irq's for all CPUs */
		while (1)
		{
			errno = 0;
			value = strtol(ptr, &endptr, base);

			if (endptr == ptr ||
			    (errno == ERANGE &&
				(value == LONG_MAX || value == LONG_MIN)) ||
			    (errno != 0 && value == 0)) break;

			irq_value += value;
			ptr = endptr;
		}
		while (*ptr == ' ') ptr++;
		while (*ptr && *ptr != ' ') ptr++;
		while (*ptr == ' ') ptr++;

		if (!*ptr) continue;

		endptr = ptr;

		while (*(++endptr))
			if (!isalnum(*endptr)) *endptr='_';

		ptr[strlen(ptr)-1] = '\0';

		irq_submit (irq, irq_value, ptr);
	}
	fclose (fh);
#endif /* KERNEL_LINUX */
}
#else
#define irq_read NULL
#endif /* IRQ_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, NULL, irq_read, irq_write);
	cf_register (MODULE_NAME, irq_config, config_keys, config_keys_num);
}

#undef BUFSIZE
#undef MODULE_NAME
