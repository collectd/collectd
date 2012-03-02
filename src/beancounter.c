/**
 * Collectd - src/beancounter.c
 * Copyright (C) 2011  Stefan Möding
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
 * The name of the plugin. This must be the same name that is used
 * in the type.db or we won't be able to submit values.
 */
static const char plugin_name[] = "beancounter";

/*
 * Where the beancounters are kept
 */
static const char *bc_files[] = {
  "/proc/bc/resources",
  "/proc/user_beancounters"
};

static int bc_files_num = STATIC_ARRAY_SIZE (bc_files);

/* The name of the beancounter file we are using */
static char *bc_filename = NULL;


/*
 * Types of counters we monitor in the same order that they appear in
 * the beancounters file. BEANCOUNTER_TYPE_SIZE is a dummy and must be
 * the last entry so we can use it for array size calculations.
 */
enum BEANCOUNTER_TYPE {
  HELD = 0, MAXHELD, BARRIER, LIMIT, FAILCNT, BEANCOUNTER_TYPE_SIZE
};


/*
 * Global structure to store the values and submit them to collectd.
 */
static struct {
  char    resource[DATA_MAX_NAME_LEN];
  value_t values[BEANCOUNTER_TYPE_SIZE];
} beancounter;


/*
 * Global variable to keep the page size of the local architecture
 */
static int pagesize;


/*
 * Plugin configuration
 */
static int beancounter_config (const char *key, const char *value) {
  if (ignorelist == NULL)
    ignorelist = ignorelist_create (/* invert = */ 1);

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
static void beancounter_submit (void) {
  value_list_t vl = VALUE_LIST_INIT;

  if (ignorelist_match (ignorelist, beancounter.resource) != 0) return;

  vl.values = beancounter.values;
  vl.values_len = STATIC_ARRAY_SIZE (beancounter.values);
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, plugin_name, sizeof (vl.plugin));
  sstrncpy (vl.type, plugin_name, sizeof (vl.type));
  sstrncpy (vl.type_instance, beancounter.resource, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* beancounter_submit */


/*
 *
 */
static void beancounter_scale (void) {
  const gauge_t nan = (gauge_t)strtod("NAN", NULL);
  const char pages[] = "pages";
  const char size0[] = "size\000";
  size_t idx;
  int i;

  /* Values larger than LONG_MAX represent unlimited.
   * Use NaN (Not a Number) to indicate we have no sensible value.
   */
  for (i=HELD; i<=LIMIT; i++) {
    if (beancounter.values[i].gauge >= (gauge_t)LONG_MAX) {
      beancounter.values[i].gauge = nan;
    }
  }

  /* Check if the resource name ends with "pages" */
  idx = strlen (beancounter.resource) - strlen (pages);

  if ((idx >= 0) && (strcmp (&beancounter.resource[idx], pages) == 0)) {

    /* Overwrite "pages" with "size" */
    memcpy(&beancounter.resource[idx], size0, sizeof(size0));

    /* scale value according to the pagesize */
    for (i=HELD; i<=LIMIT; i++) {
      beancounter.values[i].gauge *= (gauge_t)pagesize;
    }
  }
}


/*
 * Initialize plugin
 */
static int beancounter_init (void) {
  struct stat sb;
  int i;

  /* This plugin must run as root as the beancounter files are only
   * readable for root. Therefore we fail early.
   */
  if (getuid() != 0) {
    ERROR("beancounter plugin: must be root to use this plugin");
    return (-1);
  }

  /* Set filename to use for reading beancounters */
  if (!bc_filename) {
    for (i=0; i<bc_files_num; i++) {
      if (stat(bc_files[i], &sb) == 0) {
        /* remeber the name of the file and terminate loop */
        bc_filename = (char*)bc_files[i];
        break;
      }
    }
  }

  /* flag error if no beancounter file has been found */
  if (!bc_filename) {
    ERROR("beancounter plugin: no beancounter file found");
    return (-1);
  }

  pagesize = getpagesize();

  return (0);
}


/*
 * Read beancounters
 */
static int beancounter_read (void) {
  char buffer[1024];
  FILE *fd;

  if ((fd = fopen (bc_filename, "r")) == NULL) {
    char errbuf[1024];
    ERROR ("beancounter plugin: fopen (%s) failed: %s",
           bc_filename, sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  /* read line by line from the file */
  while (fgets (buffer, sizeof (buffer), fd) != NULL) {
    int valid_entry = 0;
    int token_count = 0;
    char *saveptr = NULL;
    char *strptr = buffer;
    char *token;

    /* Start with a clean beancounter structure for every new line */
    memset (&beancounter, 0, sizeof (beancounter));

    /* Tokenize the line on word boundaries and loop over every word */
    while ((token = strtok_r (strptr, " \t\n", &saveptr)) != NULL) {
      token_count++;
      strptr = NULL;

      switch (token_count) {
      case 1:
        /* Ignore first token if it starts with a digit, as that is the uid.
         * In this case continue to read the rest of the line.
         */
        if (isdigit (token[0])) {
          token_count = 0;
          continue;
        }

        /* Store the resource name unless it is a dummy entry or a line not
         * containing any beancounters. Remember that we have a valid entry.
         */
        if ((strcmp (token, "dummy") != 0) &&
            (strcmp (token, "Version:") != 0) &&
            (strcmp (token, "uid") != 0)) {
          sstrncpy (beancounter.resource, token, sizeof (beancounter.resource));
          valid_entry = 1;
        }

        break;

      case 2:
        beancounter.values[HELD].gauge = strtod (token, NULL);
        break;

      case 3:
        beancounter.values[MAXHELD].gauge = strtod (token, NULL);
        break;

      case 4:
        beancounter.values[BARRIER].gauge = strtod (token, NULL);
        break;

      case 5:
        beancounter.values[LIMIT].gauge = strtod (token, NULL);
        break;

      case 6:
        beancounter.values[FAILCNT].derive = strtoll (token, NULL, 10);
        break;
      }
    }

    if (valid_entry) {
      beancounter_scale();
      beancounter_submit();
    }
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
