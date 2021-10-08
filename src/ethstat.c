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
 *   Bartlomiej Kotlowski <bartlomiej.kotlowski@intel.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/avltree/avltree.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"
#include "utils_complain.h"
#include <dirent.h>
#include <stdbool.h>

#if HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#if HAVE_NET_IF_H
#include <net/if.h>
#endif
#if HAVE_LINUX_SOCKIOS_H
#include <linux/sockios.h>
#endif
#if HAVE_LINUX_ETHTOOL_H
#include <linux/ethtool.h>
#endif

#define SOURCE_ETH "ethtool"
#define SOURCE_SYSFS "sysfs"

#define PATH_SYSFS_INTERFACE "/sys/class/net/"
#define SIZE_PATH_SYSFS_INTERFACE 15
#define STAT "/statistics/"
#define SIZE_STAT 12
#define MAX_SIZE_METRIC_NAME 256
#define MAX_SIZE_INTERFACES_NAME DATA_MAX_NAME_LEN
#define MAX_SIZE_PATH_TO_STAT                                                  \
  (SIZE_PATH_SYSFS_INTERFACE + MAX_SIZE_INTERFACES_NAME + SIZE_STAT + 1)

#define INVALID_NAME false
#define VALID_NAME true

struct value_map_s {
  char type[DATA_MAX_NAME_LEN];
  char type_instance[DATA_MAX_NAME_LEN];
};
typedef struct value_map_s value_map_t;

static c_avl_tree_t *value_map;

static bool collect_mapped_only;

typedef struct node {
  int val;
  struct node *next;
} node_t;

typedef struct interface_metrics {
  char **interfaces;
  size_t interfaces_num;
  ignorelist_t *ignorelist_ethtool;
  node_t **ethtool_metrics;
  ignorelist_t *ignorelist_sysfs;
  bool use_sys_class_net;
  char **sysfs_metrics;
  size_t sysfs_metrics_num;
  size_t sysfs_metrics_size;
} interface_metrics_t;

interface_metrics_t *interface_metrics;
size_t interfaces_group_num = 0;

static struct node *getNewNode(size_t val) {
  struct node *newNode = malloc(sizeof(struct node));
  if (newNode == NULL) {
    ERROR("ethstat plugin: malloc failed.");
    return NULL;
  }
  newNode->val = val;
  newNode->next = NULL;
  return newNode;
}

static int push(node_t **head, size_t val) {

  node_t *current = *head;
  node_t *new_node = getNewNode(val);

  if (new_node == NULL)
    return ENOMEM;

  if (*head == NULL) {
    *head = new_node;
    return 0;
  }

  while (current->next != NULL) {
    current = current->next;
    if (val == current->val) {
      sfree(new_node);
      return 0;
    }
  }
  current->next = new_node;
  return 0;
}

static bool check_oconfig_type_string(const oconfig_item_t *ci) {
  for (int i = 0; i < ci->values_num; i++) {
    if (ci->values[i].type != OCONFIG_TYPE_STRING) {
      WARNING("ethstat plugin: The %s option requires string argument.",
              ci->key);
      return false;
    }
  }
  return true;
}

static bool check_name(const char *src, size_t size) {
  if (src == NULL || size == 0) {
    return INVALID_NAME;
  }

  while ((*src != '\0' && size > 0)) {
    if (!isalnum(*src) && !(*src == '-' || *src == '_')) {
      return INVALID_NAME;
    };
    src++;
    size--;
  }

  if (*src == '\0') {
    return VALID_NAME;
  } else {
    return INVALID_NAME;
  }
}

static int add_sysfs_metric_to_readable(interface_metrics_t *interface_group,
                                        const char *metric) {

  if (interface_group->sysfs_metrics_num >=
      interface_group->sysfs_metrics_size) {
    char **tmp;
    tmp = realloc(interface_group->sysfs_metrics,
                  sizeof(*interface_group->sysfs_metrics) *
                      (interface_group->sysfs_metrics_num + 2));
    if (tmp == NULL)
      return -1;
    interface_group->sysfs_metrics_size += 2;
    interface_group->sysfs_metrics = tmp;
    interface_group->sysfs_metrics[interface_group->sysfs_metrics_num] = NULL;
  }
  char *metric_to_save;
  if (metric == NULL)
    return -1;
  metric_to_save = strdup(metric);
  if (metric_to_save == NULL)
    return -1;

  interface_group->sysfs_metrics[interface_group->sysfs_metrics_num] =
      metric_to_save;
  if (check_name(
          interface_group->sysfs_metrics[interface_group->sysfs_metrics_num],
          strlen(interface_group
                     ->sysfs_metrics[interface_group->sysfs_metrics_num])) !=
      VALID_NAME) {

    ERROR("ethstat plugin: Invalid metric name %s",
          interface_group->sysfs_metrics[interface_group->sysfs_metrics_num]);
    sfree(metric_to_save);
    return -1;
  }

  interface_group->sysfs_metrics_num++;
  INFO("ethstat plugin: Registered sysfs metric to read %s",
       interface_group->sysfs_metrics[interface_group->sysfs_metrics_num - 1]);

  return 0;
} /* }}} int ethstat_add_sysfs_metric */

static int
create_arrary_of_sysfs_readable_metrics(const oconfig_item_t *ci,
                                        interface_metrics_t *interface_group) {
  if (ci != NULL) {
    for (int i = 0; i < ci->values_num; i++) {
      ignorelist_add(interface_group->ignorelist_sysfs,
                     ci->values[i].value.string);
    }
  }

  int status;
  DIR *d;
  struct dirent *dir;
  char path[MAX_SIZE_PATH_TO_STAT + MAX_SIZE_METRIC_NAME];

  for (int i = 0; i < interface_group->interfaces_num; i++) {
    if (check_name(interface_group->interfaces[i],
                   strlen(interface_group->interfaces[i])) != VALID_NAME) {
      ERROR("ethstat plugin: Invalid interface name %s",
            interface_group->interfaces[i]);
      break;
    }

    status = ssnprintf(path, sizeof(path), "%s%s%s", PATH_SYSFS_INTERFACE,
                       interface_group->interfaces[i], STAT);
    if ((status < 0) || (status >= sizeof(path))) {
      ERROR("ethstat plugin: The interface name %s is illegal. Probably is too "
            "long",
            interface_group->interfaces[i]);
      break;
    }

    d = opendir(path);
    if (d != NULL) {
      while ((dir = readdir(d)) != NULL) {
        if (dir->d_type == DT_REG) {
          if (ignorelist_match(interface_group->ignorelist_sysfs,
                               dir->d_name) == 0) {
            status = add_sysfs_metric_to_readable(interface_group, dir->d_name);
            if (status != 0) {
              return -1;
            }
            ignorelist_add(interface_group->ignorelist_sysfs, dir->d_name);
          }
        }
      }
      closedir(d);
    } else {
      ERROR("ethstat plugin: Can't read sysfs metrics for interface %s",
            interface_group->interfaces[i]);
      return -1;
    }
  }
  return 0;
}
/* function that adds to the ignorelist_ethtool all metrics that are read from
   sysfs. As a result, two metrics with the same name will not be collected from
   two different sources simultaneously */
void add_readable_sysfs_metrics_to_ethtool_ignore_list(
    interface_metrics_t *interface_group) {
  if (interface_group->ignorelist_ethtool == NULL) {
    interface_group->ignorelist_ethtool = ignorelist_create(0);
  }
  for (int i = 0; i < interface_group->sysfs_metrics_num; i++) {
    if (interface_group->sysfs_metrics[i] != NULL) {
      ignorelist_add(interface_group->ignorelist_ethtool,
                     interface_group->sysfs_metrics[i]);
    }
  }
}

static int create_new_interfaces_group(const oconfig_item_t *ci,
                                       interface_metrics_t *interface_group) {

  interface_group->interfaces_num = ci->values_num;
  interface_group->use_sys_class_net = false;
  interface_group->ignorelist_ethtool = ignorelist_create(0);
  interface_group->ignorelist_sysfs = ignorelist_create(0);
  if (interface_group->ignorelist_ethtool == NULL ||
      interface_group->ignorelist_sysfs == NULL) {
    ERROR("ethstat plugin: can't create ignorelist");
    return 1;
  }

  // standard interface statistics based on struct rtnl_link_stats64 which have
  // 24 element. If this number increases, the size will increase later in the
  // program
  interface_group->sysfs_metrics_size = 24;

  interface_group->sysfs_metrics =
      malloc(sizeof(*interface_group->sysfs_metrics) *
             interface_group->sysfs_metrics_size);
  if (interface_group->sysfs_metrics == NULL) {
    ERROR("ethstat plugin: malloc failed.");
    return -1;
  }
  interface_group->sysfs_metrics[0] = NULL;
  node_t **array_node =
      malloc(sizeof(node_t *) * interface_group->interfaces_num);
  if (array_node == NULL) {
    ERROR("ethstat plugin: calloc failed.");
    return 1;
  }
  interface_group->ethtool_metrics = array_node;
  for (int i = 0; i < interface_group->interfaces_num; i++) {
    interface_group->ethtool_metrics[i] = NULL;
  }

  interface_group->interfaces = malloc(sizeof(*interface_group->interfaces) *
                                       (interface_group->interfaces_num));

  if (interface_group->interfaces == NULL) {
    ERROR("ethstat plugin: malloc failed.");
    return ENOMEM;
  }

  for (int i = 0; i < ci->values_num; i++) {
    interface_group->interfaces[i] = NULL;
    char *interface_name;
    interface_name = strdup(ci->values[i].value.string);

    if (interface_name == NULL) {
      ERROR("ethstat plugin: Failed to allocate interface name.");
      sfree(interface_name);
      return ENOMEM;
    }
    interface_group->interfaces[i] = interface_name;

    INFO("ethstat plugin: Registered interface %s",
         interface_group->interfaces[i]);
  }
  interfaces_group_num++;

  return 0;
} /* }}} int ethstat_add_interface */

static int ethstat_add_map(const oconfig_item_t *ci) /* {{{ */
{
  value_map_t *map;
  int status;
  char *key;

  if ((ci->values_num < 2) || (ci->values_num > 3) ||
      (ci->values[0].type != OCONFIG_TYPE_STRING) ||
      (ci->values[1].type != OCONFIG_TYPE_STRING) ||
      ((ci->values_num == 3) && (ci->values[2].type != OCONFIG_TYPE_STRING))) {
    ERROR("ethstat plugin: The %s option requires "
          "two or three string arguments.",
          ci->key);
    return -1;
  }

  key = strdup(ci->values[0].value.string);
  if (key == NULL) {
    ERROR("ethstat plugin: strdup(3) failed.");
    return ENOMEM;
  }

  map = calloc(1, sizeof(*map));
  if (map == NULL) {
    sfree(key);
    ERROR("ethstat plugin: calloc failed.");
    return ENOMEM;
  }

  sstrncpy(map->type, ci->values[1].value.string, sizeof(map->type));
  if (ci->values_num == 3)
    sstrncpy(map->type_instance, ci->values[2].value.string,
             sizeof(map->type_instance));

  if (value_map == NULL) {
    value_map = c_avl_create((int (*)(const void *, const void *))strcmp);
    if (value_map == NULL) {
      sfree(map);
      sfree(key);
      ERROR("ethstat plugin: c_avl_create() failed.");
      return -1;
    }
  }

  status = c_avl_insert(value_map,
                        /* key = */ key,
                        /* value = */ map);
  if (status != 0) {
    if (status > 0)
      ERROR("ethstat plugin: Multiple mappings for \"%s\".", key);
    else
      ERROR("ethstat plugin: c_avl_insert(\"%s\") failed.", key);

    sfree(map);
    sfree(key);
    return -1;
  }

  return 0;
} /* }}} int ethstat_add_map */

static int complete_list_of_metrics_read_by_ethtool(
    char *device, ignorelist_t *ignorelist_ethtool, node_t **ethtool_metrics) {

  int fd;
  struct ethtool_gstrings *strings;
  struct ethtool_stats *stats;
  size_t n_stats;
  size_t strings_size;
  size_t stats_size;
  int status;

  fd = socket(AF_INET, SOCK_DGRAM, /* protocol = */ 0);
  if (fd < 0) {
    ERROR("ethstat plugin: Failed to open control socket: %s", STRERRNO);
    return 1;
  }

  struct ethtool_drvinfo drvinfo = {.cmd = ETHTOOL_GDRVINFO};

  struct ifreq req = {.ifr_data = (void *)&drvinfo};

  sstrncpy(req.ifr_name, device, sizeof(req.ifr_name));

  status = ioctl(fd, SIOCETHTOOL, &req);
  if (status < 0) {
    close(fd);
    ERROR("ethstat plugin: Failed to get driver information "
          "from %s: %s",
          device, STRERRNO);
    return -1;
  }

  n_stats = (size_t)drvinfo.n_stats;
  if (n_stats < 1) {
    close(fd);
    ERROR("ethstat plugin: No stats available for %s", device);
    return -1;
  }

  strings_size = sizeof(struct ethtool_gstrings) + (n_stats * ETH_GSTRING_LEN);
  stats_size = sizeof(struct ethtool_stats) + (n_stats * sizeof(uint64_t));

  strings = malloc(strings_size);
  stats = malloc(stats_size);
  if ((strings == NULL) || (stats == NULL)) {
    close(fd);
    sfree(strings);
    sfree(stats);
    ERROR("ethstat plugin: malloc failed.");
    return -1;
  }

  strings->cmd = ETHTOOL_GSTRINGS;
  strings->string_set = ETH_SS_STATS;
  strings->len = n_stats;
  req.ifr_data = (void *)strings;
  status = ioctl(fd, SIOCETHTOOL, &req);
  if (status < 0) {
    close(fd);
    free(strings);
    free(stats);
    ERROR("ethstat plugin: Cannot get strings from %s: %s", device, STRERRNO);
    return -1;
  }

  stats->cmd = ETHTOOL_GSTATS;
  stats->n_stats = n_stats;
  req.ifr_data = (void *)stats;
  status = ioctl(fd, SIOCETHTOOL, &req);
  if (status < 0) {
    close(fd);
    free(strings);
    free(stats);
    ERROR("ethstat plugin: Reading statistics from %s failed: %s", device,
          STRERRNO);
    return -1;
  }

  for (size_t i = 0; i < n_stats; i++) {
    char *stat_name;

    stat_name = (char *)&strings->data[i * ETH_GSTRING_LEN];

    /* Remove leading spaces in key name */
    while (isspace((int)*stat_name))
      stat_name++;
    // if the metric is not on the ignorelist_ethtool, add its number to the
    // list of read metrics
    if (ignorelist_match(ignorelist_ethtool, stat_name) == 0) {
      status = push(ethtool_metrics, i);
      if (status != 0) {
        ERROR("ethstat plugin: Unable to add item %s to list", stat_name);
        return -1;
      }
    }
  }

  close(fd);
  sfree(strings);
  sfree(stats);

  return 0;
};

static int ethstat_config(oconfig_item_t *ci) /* {{{ */
{
  int ret;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Interface", child->key) == 0) {
      if (check_oconfig_type_string(child) == true) {
        if (interface_metrics == NULL) {
          interface_metrics =
              (interface_metrics_t *)calloc(1, sizeof(*interface_metrics));
          if (interface_metrics == NULL) {
            ERROR("ethstat plugin: calloc failed.");
            return -1;
          }
        } else {
          interface_metrics_t *tmp;
          tmp = realloc(interface_metrics,
                        sizeof(*tmp) * (interfaces_group_num + 1));
          if (tmp == NULL) {
            ERROR("ethstat plugin: realloc failed.");
            return -1;
          }
          interface_metrics = tmp;
        }

        ret = create_new_interfaces_group(
            child, &interface_metrics[interfaces_group_num]);
        if (ret != 0) {
          ERROR("ethstat plugin: can't read interface config");
          return ret;
        }
      }

    } else if (strcasecmp("Map", child->key) == 0)
      ethstat_add_map(child);
    else if (strcasecmp("MappedOnly", child->key) == 0) {
      ret = cf_util_get_boolean(child, &collect_mapped_only);
      if (ret != 0) {
        ERROR("ethstat plugin: Unable to set MappedOnly");
        return 1;
      }
    } else if (strcasecmp("EthtoolExcludeMetrics", child->key) == 0) {
      if (interfaces_group_num > 0) {
        if (check_oconfig_type_string(child) == true) {
          for (int j = 0; j < child->values_num; j++) {
            ignorelist_add(
                interface_metrics[interfaces_group_num - 1].ignorelist_ethtool,
                child->values[j].value.string);
          }
        }
      } else {
        ERROR("ethstat plugin: Interface names must appear before adding "
              "EthtoolExcludeMetrics");
        return 1;
      }
    } else if (strcasecmp("UseSysClassNet", child->key) == 0) {
      if (interfaces_group_num > 0) {
        ret = cf_util_get_boolean(
            child,
            &interface_metrics[interfaces_group_num - 1].use_sys_class_net);
        if (ret != 0) {
          ERROR("ethstat plugin: Unable to set UseSysClassNet");
          return 1;
        }
      } else {
        ERROR(
            "Interface names must appear before adding EthtoolExcludeMetrics");
        return 1;
      }
    } else if (strcasecmp("SysClassNetExcludeMetrics", child->key) == 0) {

      if (interfaces_group_num > 0) {
        if (check_oconfig_type_string(child) == true) {
          ret = create_arrary_of_sysfs_readable_metrics(
              child, &interface_metrics[interfaces_group_num - 1]);
          if (ret != 0) {
            ERROR("ethstat plugin: Unable to create metric reading list from "
                  "sysfs");
            return 1;
          }
        }
      } else {
        ERROR("Interface names must appear before adding "
              "SysClassNetExcludeMetrics");
        return 1;
      }
    } else
      WARNING("ethstat plugin: The config option \"%s\" is unknown.",
              child->key);
  }
  for (int i = 0; i < interfaces_group_num; i++) {
    for (int j = 0; j < interface_metrics[i].interfaces_num; j++) {
      if (interface_metrics[i].use_sys_class_net == true) {
        if (interface_metrics[i].sysfs_metrics[0] == NULL) {
          // This mean use_sys_class_net is set to true, but
          // SysClassNetExcludeMetrics is not used in config
          ret = create_arrary_of_sysfs_readable_metrics(NULL,
                                                        &interface_metrics[i]);
          if (ret != 0) {
            ERROR("ethstat plugin: Unable to create metric reading list "
                  "from sysfs");
            break;
          }
        }
        add_readable_sysfs_metrics_to_ethtool_ignore_list(
            &interface_metrics[i]);
      }
      complete_list_of_metrics_read_by_ethtool(
          interface_metrics[i].interfaces[j],
          interface_metrics[i].ignorelist_ethtool,
          &interface_metrics[i].ethtool_metrics[j]);
    }
  }
  return 0;
} /* }}} */

static void ethstat_submit_value(const char *device, const char *name,
                                 counter_t value, char *source) {
  static c_complain_t complain_no_map = C_COMPLAIN_INIT_STATIC;
  value_map_t *map = NULL;
  char fam_name[256];
  if (value_map != NULL)
    c_avl_get(value_map, name, (void *)&map);

  /* If the "MappedOnly" option is specified, ignore unmapped values. */
  if (collect_mapped_only && (map == NULL)) {
    if (value_map == NULL)
      c_complain(
          LOG_WARNING, &complain_no_map,
          "ethstat plugin: The \"MappedOnly\" option has been set to true, "
          "but no mapping has been configured. All values will be ignored!");
    return;
  }

  if (map != NULL) {
    ssnprintf(fam_name, sizeof(fam_name), "%s", map->type);
  } else {
    ssnprintf(fam_name, sizeof(fam_name), "%s", name);
  }

  metric_family_t fam = {
      .name = fam_name,
      .type = METRIC_TYPE_COUNTER,
  };

  metric_t m = {
      .family = &fam,
      .value = (value_t){.counter = value},
  };

  metric_label_set(&m, "interface", device);
  metric_label_set(&m, "plugin", "ethstat");
  metric_label_set(&m, "source", source);

  if (map != NULL) {
    metric_label_set(&m, "tag", map->type_instance);
  }

  metric_family_metric_append(&fam, m);
  metric_reset(&m);

  int status = plugin_dispatch_metric_family(&fam);
  if (status != 0) {
    ERROR("ethstat plugin: plugin_dispatch_metric_family failed: %s",
          STRERROR(status));
  }

  metric_family_metric_reset(&fam);
}

static int read_sysfs_metrics(char *device, char **sysfs_metrics,
                              size_t sysfs_metrics_num) {

  if (sysfs_metrics[0] == NULL) {
    return 1;
  }

  FILE *fp;
  int status;
  char path_base[MAX_SIZE_PATH_TO_STAT];
  char path_metric[MAX_SIZE_PATH_TO_STAT + MAX_SIZE_METRIC_NAME];
  if (check_name(device, strlen(device)) != VALID_NAME) {
    ERROR("ethstat plugin: Invalid interface name %s", device);
    return 1;
  };
  status = ssnprintf(path_base, sizeof(path_base), "%s%s%s",
                     PATH_SYSFS_INTERFACE, device, STAT);
  if ((status < 0) || (status >= sizeof(path_base))) {
    ERROR("ethstat plugin: The interface name %s is illegal. Probably is too "
          "long",
          device);
    return ENOMEM;
  }

  uint64_t buff;

  for (int i = 0; i < sysfs_metrics_num; i++) {
    status = ssnprintf(path_metric, sizeof(path_metric), "%s%s", path_base,
                       sysfs_metrics[i]);
    if ((status < 0) || (status >= sizeof(path_metric))) {
      ERROR(
          "ethstat plugin: The metric name %s is illegal. Probably is too long",
          sysfs_metrics[i]);
      return ENOMEM;
    }

    fp = fopen(path_metric, "r");
    if (fp == NULL) {
      ERROR("ethstat plugin: Can't open file %s", path_metric);
    } else {
      if (fscanf(fp, "%" PRIu64, &buff) != 0) {
        ethstat_submit_value(device, sysfs_metrics[i], (counter_t)buff,
                             SOURCE_SYSFS);
      } else {
        ERROR("ethstat plugin: Can't read metric from %s", path_metric);
      };
      fclose(fp);
    }
  }
  return 0;
}

static int ethstat_read_interface(char *device, node_t *ethtool_metrics) {
  int fd;
  struct ethtool_gstrings *strings;
  struct ethtool_stats *stats;
  size_t n_stats;
  size_t strings_size;
  size_t stats_size;
  int status;

  fd = socket(AF_INET, SOCK_DGRAM, /* protocol = */ 0);
  if (fd < 0) {
    ERROR("ethstat plugin: Failed to open control socket: %s", STRERRNO);
    return 1;
  }

  struct ethtool_drvinfo drvinfo = {.cmd = ETHTOOL_GDRVINFO};

  struct ifreq req = {.ifr_data = (void *)&drvinfo};

  sstrncpy(req.ifr_name, device, sizeof(req.ifr_name));

  status = ioctl(fd, SIOCETHTOOL, &req);
  if (status < 0) {
    close(fd);
    ERROR("ethstat plugin: Failed to get driver information "
          "from %s: %s",
          device, STRERRNO);
    return -1;
  }

  n_stats = (size_t)drvinfo.n_stats;
  if (n_stats < 1) {
    close(fd);
    ERROR("ethstat plugin: No stats available for %s", device);
    return -1;
  }

  strings_size = sizeof(struct ethtool_gstrings) + (n_stats * ETH_GSTRING_LEN);
  stats_size = sizeof(struct ethtool_stats) + (n_stats * sizeof(uint64_t));

  strings = malloc(strings_size);
  stats = malloc(stats_size);
  if ((strings == NULL) || (stats == NULL)) {
    close(fd);
    sfree(strings);
    sfree(stats);
    ERROR("ethstat plugin: malloc failed.");
    return -1;
  }

  strings->cmd = ETHTOOL_GSTRINGS;
  strings->string_set = ETH_SS_STATS;
  strings->len = n_stats;
  req.ifr_data = (void *)strings;
  status = ioctl(fd, SIOCETHTOOL, &req);
  if (status < 0) {
    close(fd);
    free(strings);
    free(stats);
    ERROR("ethstat plugin: Cannot get strings from %s: %s", device, STRERRNO);
    return -1;
  }

  stats->cmd = ETHTOOL_GSTATS;
  stats->n_stats = n_stats;
  req.ifr_data = (void *)stats;
  status = ioctl(fd, SIOCETHTOOL, &req);
  if (status < 0) {
    close(fd);
    free(strings);
    free(stats);
    ERROR("ethstat plugin: Reading statistics from %s failed: %s", device,
          STRERRNO);
    return -1;
  }
  if (ethtool_metrics->val == -1) {
    for (size_t i = 0; i < n_stats; i++) {
      char *stat_name;

      stat_name = (char *)&strings->data[i * ETH_GSTRING_LEN];
      /* Remove leading spaces in key name */
      while (isspace((int)*stat_name)) {
        stat_name++;
      }

      ethstat_submit_value(device, stat_name, (counter_t)stats->data[i],
                           SOURCE_ETH);
    }
  } else {
    node_t *current = ethtool_metrics;

    while (current != NULL) {
      if (current->val < n_stats) {
        char *stat_name;

        stat_name = (void *)&strings->data[current->val * ETH_GSTRING_LEN];
        /* Remove leading spaces in key name */
        while (isspace((int)*stat_name)) {
          stat_name++;
        }
        ethstat_submit_value(device, stat_name,
                             (counter_t)stats->data[current->val], SOURCE_ETH);
      }
      current = current->next;
    }
  }

  close(fd);
  sfree(strings);
  sfree(stats);
  return 0;
} /* }}} ethstat_read_interface */

static int ethstat_read(void) {
  if (interfaces_group_num == 0) {
    WARNING("No interface added to read");
  } else {
    for (size_t i = 0; i < interfaces_group_num; i++) {
      for (size_t j = 0; j < interface_metrics[i].interfaces_num; j++) {
        ethstat_read_interface(interface_metrics[i].interfaces[j],
                               interface_metrics[i].ethtool_metrics[j]);
        if (interface_metrics[i].use_sys_class_net) {
          read_sysfs_metrics(interface_metrics[i].interfaces[j],
                             interface_metrics[i].sysfs_metrics,
                             interface_metrics[i].sysfs_metrics_num);
        }
      }
    }
  }
  return 0;
}

static int ethstat_shutdown(void) {
  void *key = NULL;
  void *value = NULL;

  if (value_map == NULL)
    return 0;

  while (c_avl_pick(value_map, &key, &value) == 0) {
    sfree(key);
    sfree(value);
  }

  c_avl_destroy(value_map);
  value_map = NULL;

  for (int i = 0; i < interfaces_group_num; i++) {

    for (int j = 0; j < interface_metrics[i].interfaces_num; j++) {

      sfree(interface_metrics[i].interfaces[j]);

      node_t *current = interface_metrics[i].ethtool_metrics[0];
      while (current != NULL) {
        node_t *to_remove = current;
        current = current->next;
        sfree(to_remove);
      }
      sfree(interface_metrics[i].ethtool_metrics);
    }
    sfree(interface_metrics[i].interfaces);

    if (interface_metrics[i].ignorelist_sysfs != NULL) {
      ignorelist_free(interface_metrics[i].ignorelist_sysfs);
    }
    if (interface_metrics[i].ignorelist_ethtool != NULL) {
      ignorelist_free(interface_metrics[i].ignorelist_ethtool);
    }

    for (size_t j = 0; j < interface_metrics[i].sysfs_metrics_size; j++) {
      sfree(interface_metrics[i].sysfs_metrics[j]);
    }
    sfree(interface_metrics[i].sysfs_metrics);
  }
  return 0;
}

void module_register(void) {
  plugin_register_complex_config("ethstat", ethstat_config);
  plugin_register_read("ethstat", ethstat_read);
  plugin_register_shutdown("ethstat", ethstat_shutdown);
}
