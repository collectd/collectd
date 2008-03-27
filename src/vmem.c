/**
 * collectd - src/vmem.c
 * Copyright (C) 2008  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if KERNEL_LINUX
/* No global variables */
/* #endif KERNEL_LINUX */

#else
# error "No applicable input method."
#endif /* HAVE_LIBSTATGRAB */

static void submit (const char *plugin_instance, const char *type,
    const char *type_instance, value_t *values, int values_len)
{
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = values;
  vl.values_len = values_len;

  vl.time = time (NULL);
  strcpy (vl.host, hostname_g);
  strcpy (vl.plugin, "vmem");
  if (plugin_instance != NULL)
    sstrncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));
  if (type_instance != NULL)
    sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  plugin_dispatch_values (type, &vl);
} /* void vmem_submit */

static void submit_two (const char *plugin_instance, const char *type,
    const char *type_instance, counter_t c0, counter_t c1)
{
  value_t values[2];

  values[0].counter = c0;
  values[1].counter = c1;

  submit (plugin_instance, type, type_instance, values, 2);
} /* void submit_one */

static void submit_one (const char *plugin_instance, const char *type,
    const char *type_instance, value_t value)
{
  submit (plugin_instance, type, type_instance, &value, 1);
} /* void submit_one */

static int vmem_read (void)
{
#if KERNEL_LINUX
  counter_t pgpgin = 0;
  counter_t pgpgout = 0;
  int pgpgvalid = 0;

  counter_t pswpin = 0;
  counter_t pswpout = 0;
  int pswpvalid = 0;

  counter_t pgfault = 0;
  counter_t pgmajfault = 0;
  int pgfaultvalid = 0;

  FILE *fh;
  char buffer[1024];

  fh = fopen ("/proc/vmstat", "r");
  if (fh == NULL)
  {
    char errbuf[1024];
    ERROR ("vmem plugin: fopen (/proc/vmstat) failed: %s",
	sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  while (fgets (buffer, sizeof (buffer), fh) != NULL)
  {
    char *fields[4];
    int fields_num;
    char *key;
    char *endptr;
    counter_t counter;
    gauge_t gauge;

    fields_num = strsplit (buffer, fields, STATIC_ARRAY_SIZE (fields));
    if (fields_num != 2)
      continue;

    key = fields[0];

    endptr = NULL;
    counter = strtoll (fields[1], &endptr, 10);
    if (fields[1] == endptr)
      continue;

    endptr = NULL;
    gauge = strtod (fields[1], &endptr);
    if (fields[1] == endptr)
      continue;

    /* 
     * Number of pages
     *
     * The total number of {inst} pages, e. g dirty pages.
     */
    if (strncmp ("nr_", key, strlen ("nr_")) == 0)
    {
      char *inst = key + strlen ("nr_");
      value_t value = { .gauge = gauge };
      submit_one (NULL, "vmpage_number", inst, value);
    }

    /*
     * Number of page allocations, refills, steals and scans. This is collected
     * ``per zone'', i. e. for DMA, DMA32, normal and possibly highmem.
     */
    else if (strncmp ("pgalloc_", key, strlen ("pgalloc_")) == 0)
    {
      char *inst = key + strlen ("pgalloc_");
      value_t value  = { .counter = counter };
      submit_one (inst, "vmpage_action", "alloc", value);
    }
    else if (strncmp ("pgrefill_", key, strlen ("pgrefill_")) == 0)
    {
      char *inst = key + strlen ("pgrefill_");
      value_t value  = { .counter = counter };
      submit_one (inst, "vmpage_action", "refill", value);
    }
    else if (strncmp ("pgsteal_", key, strlen ("pgsteal_")) == 0)
    {
      char *inst = key + strlen ("pgsteal_");
      value_t value  = { .counter = counter };
      submit_one (inst, "vmpage_action", "steal", value);
    }
    else if (strncmp ("pgscan_kswapd_", key, strlen ("pgscan_kswapd_")) == 0)
    {
      char *inst = key + strlen ("pgscan_kswapd_");
      value_t value  = { .counter = counter };
      submit_one (inst, "vmpage_action", "scan_kswapd", value);
    }
    else if (strncmp ("pgscan_direct_", key, strlen ("pgscan_direct_")) == 0)
    {
      char *inst = key + strlen ("pgscan_direct_");
      value_t value  = { .counter = counter };
      submit_one (inst, "vmpage_action", "scan_direct", value);
    }

    /* 
     * Page in and page outs. For memory and swap.
     */
    else if (strcmp ("pgpgin", key) == 0)
    {
      pgpgin = counter;
      pgpgvalid |= 0x01;
    }
    else if (strcmp ("pgpgout", key) == 0)
    {
      pgpgout = counter;
      pgpgvalid |= 0x02;
    }
    else if (strcmp ("pswpin", key) == 0)
    {
      pswpin = counter;
      pswpvalid |= 0x01;
    }
    else if (strcmp ("pswpout", key) == 0)
    {
      pswpout = counter;
      pswpvalid |= 0x02;
    }

    /*
     * Pagefaults
     */
    else if (strcmp ("pgfault", key) == 0)
    {
      pgfault = counter;
      pgfaultvalid |= 0x01;
    }
    else if (strcmp ("pgmajfault", key) == 0)
    {
      pgmajfault = counter;
      pgfaultvalid |= 0x02;
    }

    /*
     * Page action
     *
     * number of pages moved to the active or inactive lists and freed, i. e.
     * removed from either list.
     */
    else if (strcmp ("pgfree", key) == 0)
    {
      value_t value  = { .counter = counter };
      submit_one (NULL, "vmpage_action", "free", value);
    }
    else if (strcmp ("pgactivate", key) == 0)
    {
      value_t value  = { .counter = counter };
      submit_one (NULL, "vmpage_action", "activate", value);
    }
    else if (strcmp ("pgdeactivate", key) == 0)
    {
      value_t value  = { .counter = counter };
      submit_one (NULL, "vmpage_action", "deactivate", value);
    }
  } /* while (fgets) */

  fclose (fh);
  fh = NULL;

  if (pgfaultvalid == 0x03)
    submit_two (NULL, "vmpage_faults", NULL, pgfault, pgmajfault);

  if (pgpgvalid == 0x03)
    submit_two (NULL, "vmpage_io", "memory", pgpgin, pgpgout);

  if (pswpvalid == 0x03)
    submit_two (NULL, "vmpage_io", "swap", pswpin, pswpout);
#endif /* KERNEL_LINUX */

  return (0);
} /* int vmem_read */

void module_register (void)
{
	plugin_register_read ("vmem", vmem_read);
} /* void module_register */

/* vim: set sw=2 sts=2 ts=8 : */
