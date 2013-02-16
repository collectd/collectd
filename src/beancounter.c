/**
 * Collectd - src/beancounter.c
 * Copyright (C) 2011, 2012  Stefan Möding
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
 *   Stefan Möding  <stm at kill-9.net>
 **/


#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#if !KERNEL_LINUX
# error "No applicable input method."
#endif


/**
 * Plugin:beancounter
 *
 * The beancounter plugin collects resource usage and their limits for
 * an OpenVZ or Virtuozzo container. In contrast to the OpenVZ plugin
 * it runs inside the container to measure the resources of its own
 * Virtual Environment (VE). The plugin will collect all beancounter
 * values if there is no specific configuration. Values for entries
 * called dummy are never collected.
 *
 * The beancounters are available in /proc/bc/resources or
 * /proc/user_beancounters (legacy interface). The files are only
 * readable by root and look similar to this example:
 *
 * Version: 2.5
 *        uid  resource          held    maxheld    barrier      limit  failcnt
 *     123456: kmemsize        788790     886020   14237585   15661344        0
 *             lockedpages          0          0        764        764        0
 *             privvmpages       3920       8566     268538     295392        0
 *             shmpages          1280       1296       2953       2953        0
 *             dummy                0          0          0          0        0
 *             numproc             13         16        382        382        0
 *             physpages         2294       5224          0 2147483647        0
 *             vmguarpages          0          0      39232 2147483647        0
 *             oomguarpages      2294       5224      49232 2147483647        0
 *             numtcpsock          11         11        382        382        0
 *             numflock             1          2        611        672        0
 *             numpty               2          2         38         38        0
 *             numsiginfo           0          2       1024       1024        0
 *             tcpsndbuf         6660      11100    3655776    5220448        0
 *             tcprcvbuf            0       4268    3655776    5220448        0
 *             othersockbuf      4440       7860    1827888    3392560        0
 *             dgramrcvbuf          0       4268    1827888    1827888        0
 *             numothersock         5          8        382        382        0
 *             dcachesize       85685      96052    3520512    3626127        0
 *             numfile            217        242       6112       6112        0
 *             dummy                0          0          0          0        0
 *             dummy                0          0          0          0        0
 *             dummy                0          0          0          0        0
 *             numiptent           10         10         74         74        0
 *
 * There are some memory related resources that are measured in pages
 * (privvmpages, shmpages, physpages, vmguarpages, oomguarpages) while
 * others are measured in bytes (kmemsize, tcpsndbuf, tcprcvbuf,
 * othersockbuf, dgramrcvbuf, dcachesize). Even though OpenVZ and
 * Virtuozzo currently use a fixed pagesize (4096 bytes) the plugin
 * scales any value measured in pages to the appropriate value in
 * bytes. The resource name is changed from "...pages" to "...size" to
 * reflect this transformation (e.g. privvmpages becomes privvmsize).
 *
 * OpenVZ/Virtuozzo uses LONG_MAX (2^31-1 on 32-bit architecures,
 * 2^63-1 on 64-bit architectures) to indicate an "unlimited" resource
 * (http://wiki.openvz.org/LONG_MAX). These values are stored as
 * "unknown" in collectd.
 *
 * Synopsis
 *
 * LoadPlugin "beancounter"
 *
 * <Plugin "beancounter">
 *   Beancounter kmemsize
 *   Beancounter privvmsize
 *   IgnoreSelected false
 * </Plugin>
 *
 * Dependencies
 *
 * * Linux
 *   * /proc-file system
 **/

/*
 * Configuration
 */
static const char *config_keys[] = {
  "Beancounter",
  "IgnoreSelected"
};

static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static ignorelist_t *ignorelist = NULL;


/*
 * Types of counters we monitor in the same order that they appear in
 * the beancounters file. BEANCOUNTER_TYPE_SIZE is a dummy and must be
 * the last entry so we can use it for array size calculations.
 */
enum BEANCOUNTER_TYPE {
  HELD = 0, BARRIER, LIMIT, FAILCNT, BEANCOUNTER_TYPE_SIZE
};


/*
 * Structure to store the values and submit them to collectd.
 */
struct beancounter_s {
  value_t values[BEANCOUNTER_TYPE_SIZE];
};

typedef struct beancounter_s beancounter_t;


/*
 * Plugin configuration
 */
static int beancounter_config (const char *key, const char *value) {
  if (ignorelist == NULL) {
    ignorelist = ignorelist_create (/* invert = */ 1);
  }

  if (strcasecmp (key, "Beancounter") == 0) {
    ignorelist_add (ignorelist, value);
  }
  else if (strcasecmp (key, "IgnoreSelected") == 0) {
    int invert = 1;
    if (IS_TRUE (value)) invert = 0;
    ignorelist_set_invert (ignorelist, invert);
  }
  else {
    return (-1);
  }

  return (0);
} /* beancounter_config */


/*
 * Submit beancounter to the collectd daemon.
 */
static void beancounter_submit (const char *type_instance,
                                beancounter_t beancounter) {
  value_list_t vl = VALUE_LIST_INIT;

  if (ignorelist_match (ignorelist, type_instance) != 0)
    return;

  /* Define common fields */
  sstrncpy (vl.host,          hostname_g,    sizeof (vl.host));
  sstrncpy (vl.plugin,        "beancounter", sizeof (vl.plugin));
  sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  vl.values_len = 1;

  vl.values = &beancounter.values[HELD];
  sstrncpy (vl.type, "beancounter_held", sizeof (vl.type));
  plugin_dispatch_values (&vl);

  vl.values = &beancounter.values[BARRIER];
  sstrncpy (vl.type, "beancounter_barrier", sizeof (vl.type));
  plugin_dispatch_values (&vl);

  vl.values = &beancounter.values[LIMIT];
  sstrncpy (vl.type, "beancounter_limit", sizeof (vl.type));
  plugin_dispatch_values (&vl);

  vl.values = &beancounter.values[FAILCNT];
  sstrncpy (vl.type, "beancounter_failcnt", sizeof (vl.type));
  plugin_dispatch_values (&vl);
} /* beancounter_submit */


/*
 * Initialize plugin
 */
static int beancounter_init (void) {

  /* This plugin must run as root as the beancounter files are only
   * readable for root. Therefore we fail early.
   */
  if (getuid() != 0) {
    ERROR("beancounter plugin: must be root to use this plugin");
    return (-1);
  }

  return (0);
} /* beancounter_init */


/*
 * Read beancounters
 */
static int beancounter_read (void) {
  int  pagesize = getpagesize();
  char buffer[1024];
  FILE *fd;

  /* Try to open one of the beancounter files in /proc */
  if ((fd = fopen ("/proc/bc/resources", "r")) == NULL) {
    if ((fd = fopen ("/proc/user_beancounters", "r")) == NULL) {
      char errbuf[1024];
      ERROR ("beancounter plugin: fopen beancounters failed: %s",
             sstrerror (errno, errbuf, sizeof (errbuf)));
      return (-1);
    }
  }

  /* read line by line from the file */
  while (fgets (buffer, sizeof (buffer), fd) != NULL) {
    beancounter_t beancounter;
    char *resource;
    size_t pos;
    char *token[7];
    int token_idx, i;

    /* Start with a clean beancounter structure for every new line */
    memset (&beancounter, 0, sizeof (beancounter));

    /* Tokenize the line on word boundaries and return no more than
     * the allocated number of tokens. Continue with the next line
     * if buffer doesn't look like beancounters.
     * With 7 tokens, token[0] holds the uid which is ignored. Therefore
     * the first token to look at has the index 1.
     */
    switch(strsplit(buffer, token, STATIC_ARRAY_SIZE(token))) {
    case 6:
      token_idx = 0;
      break;
    case 7:
      token_idx = 1;
      break;
    default:
      continue; /* next line from file */
      break;
    }

    /* Ignore dummy beancounter and the heading */
    if ((strcmp (token[token_idx], "dummy") == 0) ||
        (strcmp (token[token_idx], "resource") == 0)) {
      continue;
    }

    /* Store the resource name */
    resource = token[token_idx];

    /* Next field: held */
    token_idx++;
    parse_value(token[token_idx], &beancounter.values[HELD], DS_TYPE_GAUGE);

    /* Next field: maxheld (ignored) */
    token_idx++;

    /* Next field: barrier */
    token_idx++;
    parse_value(token[token_idx], &beancounter.values[BARRIER], DS_TYPE_GAUGE);

    /* Next field: limit */
    token_idx++;
    parse_value(token[token_idx], &beancounter.values[LIMIT], DS_TYPE_GAUGE);

    /* Next field: failcnt */
    token_idx++;
    parse_value(token[token_idx], &beancounter.values[FAILCNT], DS_TYPE_DERIVE);

    /* Only values smaller than LONG_MAX are valid; everything is unlimited.
     * Use NaN (Not a Number) to indicate we have no sensible value.
     */
    for (i=HELD; i<=LIMIT; i++) {
      if (beancounter.values[i].gauge >= (gauge_t)LONG_MAX) {
        beancounter.values[i].gauge = (gauge_t)NAN;
      }
    }

    /* Check if the resource name ends with "pages".
     * In this case it is scaled to bytes.
     */
    pos = strlen (resource) - strlen ("pages");

    if ((pos >= 0) && (strcmp (&resource[pos], "pages") == 0)) {

      /* Overwrite "pages" with "size" */
      memcpy(&resource[pos], "size\000", 5);

      /* Scale value according to the pagesize */
      for (i=HELD; i<=LIMIT; i++) {
        beancounter.values[i].gauge *= (gauge_t)pagesize;
      }
    }

    beancounter_submit(resource, beancounter);
  }
  fclose (fd);

  return (0);
} /* beancounter_read */


void module_register (void) {
  plugin_register_config ("beancounter", beancounter_config,
                          config_keys, config_keys_num);
  plugin_register_init ("beancounter", beancounter_init);
  plugin_register_read ("beancounter", beancounter_read);
} /* module_register */
