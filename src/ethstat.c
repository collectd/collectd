/**
 * collectd - src/ethstat.c
 * Copyright (C) 2011       Cyril Feraudet
 * Copyright (C) 2012       Florian "octo" Forster
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
 *   Cyril Feraudet <cyril at feraudet.com>
 *   Florian "octo" Forster <octo@collectd.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_avltree.h"
#include "utils_complain.h"

#if HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#if HAVE_NET_IF_H
# include <net/if.h>
#endif
#if HAVE_LINUX_SOCKIOS_H
# include <linux/sockios.h>
#endif
#if HAVE_LINUX_ETHTOOL_H
# include <linux/ethtool.h>
#endif

struct value_map_s
{
  char type[DATA_MAX_NAME_LEN];
  char type_instance[DATA_MAX_NAME_LEN];
};
typedef struct value_map_s value_map_t;

static char **interfaces = NULL;
static size_t interfaces_num = 0;

static c_avl_tree_t *value_map = NULL;

static _Bool collect_mapped_only = 0;

static int ethstat_add_interface (const oconfig_item_t *ci) /* {{{ */
{
  char **tmp;
  int status;

  tmp = realloc (interfaces,
      sizeof (*interfaces) * (interfaces_num + 1));
  if (tmp == NULL)
    return (-1);
  interfaces = tmp;

  status = cf_util_get_string (ci, interfaces + interfaces_num);
  if (status != 0)
    return (status);

  interfaces_num++;
  INFO("ethstat plugin: Registred interface %s",
      interfaces[interfaces_num - 1]);

  return (0);
} /* }}} int ethstat_add_interface */

static int ethstat_add_map (const oconfig_item_t *ci) /* {{{ */
{
  value_map_t *map;
  int status;

  if ((ci->values_num < 2)
      || (ci->values_num > 3)
      || (ci->values[0].type != OCONFIG_TYPE_STRING)
      || (ci->values[1].type != OCONFIG_TYPE_STRING)
      || ((ci->values_num == 3)
        && (ci->values[2].type != OCONFIG_TYPE_STRING)))
  {
    ERROR ("ethstat plugin: The %s option requires "
        "two or three string arguments.", ci->key);
    return (-1);
  }

  map = malloc (sizeof (*map));
  if (map == NULL)
  {
    ERROR ("ethstat plugin: malloc(3) failed.");
    return (ENOMEM);
  }
  memset (map, 0, sizeof (*map));

  sstrncpy (map->type, ci->values[1].value.string, sizeof (map->type));
  if (ci->values_num == 2)
    sstrncpy (map->type_instance, ci->values[2].value.string,
        sizeof (map->type_instance));

  if (value_map == NULL)
  {
    value_map = c_avl_create ((void *) strcmp);
    if (value_map == NULL)
    {
      sfree (map);
      ERROR ("ethstat plugin: c_avl_create() failed.");
      return (-1);
    }
  }

  status = c_avl_insert (value_map,
      /* key = */ ci->values[0].value.string,
      /* value = */ map);
  if (status != 0)
  {
    sfree (map);
    if (status > 0)
      ERROR ("ethstat plugin: Multiple mappings for \"%s\".",
          ci->values[0].value.string);
    else
      ERROR ("ethstat plugin: c_avl_insert(\"%s\") failed.",
          ci->values[0].value.string);
    return (-1);
  }

  return (0);
} /* }}} int ethstat_add_map */

static int ethstat_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Interface", child->key) == 0)
      ethstat_add_interface (child);
    else if (strcasecmp ("Map", child->key) == 0)
      ethstat_add_map (child);
    else if (strcasecmp ("MappedOnly", child->key) == 0)
      (void) cf_util_get_boolean (child, &collect_mapped_only);
    else
      WARNING ("ethstat plugin: The config option \"%s\" is unknown.",
          child->key);
  }

  return (0);
} /* }}} */

static void ethstat_submit_value (const char *device,
    const char *type_instance, derive_t value)
{
  static c_complain_t complain_no_map = C_COMPLAIN_INIT_STATIC;

  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;
  value_map_t *map = NULL;

  if (value_map != NULL)
    c_avl_get (value_map, type_instance, (void *) &map);

  /* If the "MappedOnly" option is specified, ignore unmapped values. */
  if (collect_mapped_only && (map == NULL))
  {
    if (value_map == NULL)
      c_complain (LOG_WARNING, &complain_no_map,
          "ethstat plugin: The \"MappedOnly\" option has been set to true, "
          "but no mapping has been configured. All values will be ignored!");
    return;
  }

  values[0].derive = value;
  vl.values = values;
  vl.values_len = 1;

  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "ethstat", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, device, sizeof (vl.plugin_instance));
  if (map != NULL)
  {
    sstrncpy (vl.type, map->type, sizeof (vl.type));
    sstrncpy (vl.type_instance, map->type_instance,
        sizeof (vl.type_instance));
  }
  else
  {
    sstrncpy (vl.type, "derive", sizeof (vl.type));
    sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));
  }

  plugin_dispatch_values (&vl);
}

static int ethstat_read_interface (char *device)
{
  int fd;
  struct ifreq req;
  struct ethtool_drvinfo drvinfo;
  struct ethtool_gstrings *strings;
  struct ethtool_stats *stats;
  size_t n_stats;
  size_t strings_size;
  size_t stats_size;
  size_t i;
  int status;

  memset (&req, 0, sizeof (req));
  sstrncpy(req.ifr_name, device, sizeof (req.ifr_name));

  fd = socket(AF_INET, SOCK_DGRAM, /* protocol = */ 0);
  if (fd < 0)
  {
    char errbuf[1024];
    ERROR("ethstat plugin: Failed to open control socket: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return 1;
  }

  memset (&drvinfo, 0, sizeof (drvinfo));
  drvinfo.cmd = ETHTOOL_GDRVINFO;
  req.ifr_data = (void *) &drvinfo;
  status = ioctl (fd, SIOCETHTOOL, &req);
  if (status < 0)
  {
    char errbuf[1024];
    close (fd);
    ERROR ("ethstat plugin: Failed to get driver information "
        "from %s: %s", device,
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  n_stats = (size_t) drvinfo.n_stats;
  if (n_stats < 1)
  {
    close (fd);
    ERROR("ethstat plugin: No stats available for %s", device);
    return (-1);
  }

  strings_size = sizeof (struct ethtool_gstrings)
    + (n_stats * ETH_GSTRING_LEN);
  stats_size = sizeof (struct ethtool_stats)
    + (n_stats * sizeof (uint64_t));

  strings = malloc (strings_size);
  stats = malloc (stats_size);
  if ((strings == NULL) || (stats == NULL))
  {
    close (fd);
    sfree (strings);
    sfree (stats);
    ERROR("ethstat plugin: malloc(3) failed.");
    return (-1);
  }

  strings->cmd = ETHTOOL_GSTRINGS;
  strings->string_set = ETH_SS_STATS;
  strings->len = n_stats;
  req.ifr_data = (void *) strings;
  status = ioctl (fd, SIOCETHTOOL, &req);
  if (status < 0)
  {
    char errbuf[1024];
    close (fd);
    free (strings);
    free (stats);
    ERROR ("ethstat plugin: Cannot get strings from %s: %s",
        device,
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  stats->cmd = ETHTOOL_GSTATS;
  stats->n_stats = n_stats;
  req.ifr_data = (void *) stats;
  status = ioctl (fd, SIOCETHTOOL, &req);
  if (status < 0)
  {
    char errbuf[1024];
    close (fd);
    free(strings);
    free(stats);
    ERROR("ethstat plugin: Reading statistics from %s failed: %s",
        device,
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  for (i = 0; i < n_stats; i++)
  {
    const char *stat_name;

    stat_name = (void *) &strings->data[i * ETH_GSTRING_LEN];
    DEBUG("ethstat plugin: device = \"%s\": %s = %"PRIu64,
        device, stat_name, (uint64_t) stats->data[i]);
    ethstat_submit_value (device,
        stat_name, (derive_t) stats->data[i]);
  }

  close (fd);
  sfree (strings);
  sfree (stats);

  return (0);
} /* }}} ethstat_read_interface */

static int ethstat_read(void)
{
  size_t i;

  for (i = 0; i < interfaces_num; i++)
    ethstat_read_interface (interfaces[i]);

  return 0;
}

void module_register (void)
{
  plugin_register_complex_config ("ethstat", ethstat_config);
  plugin_register_read ("ethstat", ethstat_read);
}

/* vim: set sw=2 sts=2 et fdm=marker : */
