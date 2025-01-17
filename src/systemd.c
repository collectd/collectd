#include "liboconfig/oconfig.h"
#include "metric.h"
#include "plugin.h"

#include "systemd/sd-bus.h"
#include "utils/common/common.h"

#include "libxml/parser.h"
#include <libxml/xpath.h>

typedef struct {
  char *name;
  char const dbus_type[2];
  metric_type_t collectd_type;
} systemd_metric;

typedef struct {
  char *accounting_flag;
  systemd_metric *metrics;
} systemd_metric_group;

static systemd_metric_group service_groups[] = {
    {
        .accounting_flag = "MemoryAccounting",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "MemoryAvailable",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {
                    .name = "MemoryCurrent",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {
                    .name = "MemoryPeak",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {
                    .name = "MemorySwapCurrent",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {
                    .name = "MemoryZSwapCurrent",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {
                    .name = "MemorySwapPeak",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {.collectd_type = METRIC_TYPE_UNTYPED},
            },
    },
    {
        .accounting_flag = "IOAccounting",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "IOReadBytes",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IOReadOperations",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IOWriteBytes",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IOWriteOperations",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {.collectd_type = METRIC_TYPE_UNTYPED},
            },
    },
    {
        .accounting_flag = "CPUAccounting",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "CPUUsageNSec",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {.collectd_type = METRIC_TYPE_UNTYPED},
            },
    },
    {
        .accounting_flag = "IPAccounting",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "IPEgressBytes",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IPEgressPackets",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IPIngressBytes",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IPIngressPackets",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {.collectd_type = METRIC_TYPE_UNTYPED},
            },
    },
    {
        .accounting_flag = "TasksAccounting",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "TasksCurrent",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {.collectd_type = METRIC_TYPE_UNTYPED},
            },
    },
    {
        .accounting_flag = NULL,
        .metrics =
            (systemd_metric[]){
                {
                    .name = "NRestarts",
                    .dbus_type = "u",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {.collectd_type = METRIC_TYPE_UNTYPED},
            },
    },
};

static systemd_metric_group slice_groups[] = {
    {
        .accounting_flag = "MemoryAccounting",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "MemoryAvailable",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {
                    .name = "MemoryCurrent",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {
                    .name = "MemoryPeak",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {
                    .name = "MemorySwapCurrent",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {
                    .name = "MemoryZSwapCurrent",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {
                    .name = "MemorySwapPeak",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {.collectd_type = METRIC_TYPE_UNTYPED},
            },
    },
    {
        .accounting_flag = "IOAccounting",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "IOReadBytes",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IOReadOperations",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IOWriteBytes",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IOWriteOperations",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {.collectd_type = METRIC_TYPE_UNTYPED},
            },
    },
    {
        .accounting_flag = "CPUAccounting",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "CPUUsageNSec",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {.collectd_type = METRIC_TYPE_UNTYPED},
            },
    },
    {
        .accounting_flag = "IPAccounting",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "IPEgressBytes",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IPEgressPackets",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IPIngressBytes",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IPIngressPackets",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {.collectd_type = METRIC_TYPE_UNTYPED},
            },
    },
    {
        .accounting_flag = "TasksAccounting",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "TasksCurrent",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {.collectd_type = METRIC_TYPE_UNTYPED},
            },
    },
};

typedef struct {
  char *path;
  bool is_slice;
} unit;

static unit *units = NULL;

static size_t units_num = 0;

static sd_bus *bus = NULL;

static bool was_service = false;
static bool was_slice = false;

static int introspect_prop(xmlXPathContextPtr xpath_ctx,
                           char const interface[static 1], char **prop) {
  char query[128];
  sprintf(query,
          "//interface[@name=\"%s\"]/"
          "property[@name=\"%s\"]",
          interface, *prop);
  xmlXPathObjectPtr xpath_obj =
      xmlXPathEvalExpression(BAD_CAST(query), xpath_ctx);
  if (xpath_obj == NULL) {
    ERROR("Can't get xpath object");
    return EXIT_FAILURE;
  }
  if (xpath_obj->nodesetval->nodeNr == 0) {
    WARNING("This systemd version doesn't provide %s property in %s interface",
            *prop, interface);
    *prop = NULL;
  }
  return EXIT_SUCCESS;
}

static int introspect_unit(unit *unit) {
  sd_bus_error sd_bus_err = SD_BUS_ERROR_NULL;
  sd_bus_message *m = NULL;
  int r = sd_bus_call_method(bus, "org.freedesktop.systemd1", unit->path,
                             "org.freedesktop.DBus.Introspectable",
                             "Introspect", &sd_bus_err, &m, "");
  if (r < 0) {
    ERROR("Can't introspect %s: %s {%s}, %s", unit->path, sd_bus_err.name,
          sd_bus_err.message, strerror(-r));
    return EXIT_FAILURE;
  }
  char const *xml;
  r = sd_bus_message_read(m, "s", &xml);
  xmlDocPtr doc = xmlReadMemory(xml, strlen(xml), "noname.xml", NULL, 0);
  if (doc == NULL) {
    ERROR("Can't parse xml: %s", xml);
    return EXIT_FAILURE;
  }
  xmlXPathContextPtr xpath_ctx = xmlXPathNewContext(doc);
  if (xpath_ctx == NULL) {
    ERROR("Can't get context of the xml");
    return EXIT_FAILURE;
  }
  systemd_metric_group *groups;
  size_t ngroups;
  char const *interface;
  if (unit->is_slice) {
    groups = slice_groups;
    ngroups = STATIC_ARRAY_SIZE(slice_groups);
    interface = "org.freedesktop.systemd1.Slice";
  } else {
    groups = service_groups;
    ngroups = STATIC_ARRAY_SIZE(service_groups);
    interface = "org.freedesktop.systemd1.Service";
  }
  for (systemd_metric_group *groups_it = groups; groups_it != groups + ngroups;
       ++groups_it) {
    if (groups_it->accounting_flag != NULL) {
      if (introspect_prop(xpath_ctx, interface, &groups_it->accounting_flag) <
          0) {
        return EXIT_FAILURE;
      }
    }
    for (systemd_metric *metric_it = groups_it->metrics;
         metric_it->collectd_type != METRIC_TYPE_UNTYPED; ++metric_it) {
      if (introspect_prop(xpath_ctx, interface, &metric_it->name) < 0) {
        return EXIT_FAILURE;
      }
    }
  }
  if (unit->is_slice) {
    was_slice = true;
  } else {
    was_service = true;
  }
  return EXIT_SUCCESS;
}

static int systemd_config(oconfig_item_t *ci) {
  if (bus == NULL) {
    int r = sd_bus_open_system(&bus);
    if (r < 0) {
      ERROR("Failed to connect to system bus: %s", strerror(-r));
      sd_bus_unref(bus);
      return r;
    }
  }
  units_num += ci->children_num;
  units = realloc(units, sizeof(unit) * units_num);
  if (units == NULL) {
    ERROR("Can't allocate memory for units");
    return EXIT_FAILURE;
  }
  for (size_t i = 0; i < ci->children_num; ++i) {
    oconfig_item_t *child = ci->children + i;
    char *external_id = NULL;
    unit unit;
    unit.is_slice = !strcmp(child->key, "Slice");
    if (cf_util_get_string(child, &external_id) < 0) {
      ERROR("Error during parsing config");
      return EXIT_FAILURE;
    }
    int r = sd_bus_path_encode("/org/freedesktop/systemd1/unit", external_id,
                               &unit.path);
    if (r < 0) {
      ERROR("Can't encode \"%s\" unit: %s", external_id, strerror(-r));
      return EXIT_FAILURE;
    }
    units[units_num - ci->children_num + i] = unit;
    if ((!was_slice && unit.is_slice) || (!was_service && !unit.is_slice)) {
      if (introspect_unit(&unit) == EXIT_FAILURE) {
        return EXIT_FAILURE;
      }
    }
  }
  return EXIT_SUCCESS;
}

static int get_prop(sd_bus *bus, char const *interface, char const *unit,
                    char const type[static 1], char const prop[static 1],
                    void *var, sd_bus_error *err) {
  sd_bus_message *m = NULL;
  int r = sd_bus_get_property(bus, "org.freedesktop.systemd1", unit, interface,
                              prop, err, &m, type);
  if (r < 0) {
    return r;
  }
  r = sd_bus_message_read(m, type, var);
  sd_bus_message_unref(m);
  return r;
}

static int systemd_read(void) {
  int r;
  sd_bus_error sd_bus_err = SD_BUS_ERROR_NULL;
  for (unit *unit_it = units; unit_it != units + units_num; ++unit_it) {
    systemd_metric_group const *groups;
    size_t ngroups;
    if (unit_it->is_slice) {
      groups = slice_groups;
      ngroups = STATIC_ARRAY_SIZE(slice_groups);
    } else {
      groups = service_groups;
      ngroups = STATIC_ARRAY_SIZE(service_groups);
    }
    for (systemd_metric_group const *groups_it = groups;
         groups_it != groups + ngroups; ++groups_it) {
      bool accounting_flag_var = true;
      char *interface;
      if (unit_it->is_slice) {
        interface = "org.freedesktop.systemd1.Slice";
      } else {
        interface = "org.freedesktop.systemd1.Service";
      }
      if (groups_it->accounting_flag) {
        r = get_prop(bus, interface, unit_it->path, "b",
                     groups_it->accounting_flag, &accounting_flag_var,
                     &sd_bus_err);
        if (r < 0) {
          ERROR("Failed to get %s accounting flag: %s {%s}, %s",
                groups_it->accounting_flag, sd_bus_err.name, sd_bus_err.message,
                strerror(-r));
          goto fail;
        }
      }
      if (accounting_flag_var) {
        for (systemd_metric *metrics_it = groups_it->metrics;
             metrics_it->collectd_type != METRIC_TYPE_UNTYPED; ++metrics_it) {
          if (!metrics_it->name) {
            continue;
          }
          uint64_t val;
          r = get_prop(bus, interface, unit_it->path, metrics_it->dbus_type,
                       metrics_it->name, &val, &sd_bus_err);
          if (r < 0) {
            ERROR("Failed to get %s property: %s {%s}, %s", metrics_it->name,
                  sd_bus_err.name, sd_bus_err.message, strerror(-r));
            goto fail;
          }
          metric_family_t fam = {
              .name = metrics_it->name,
              .type = metrics_it->collectd_type,
          };
          metric_t m;
          switch (metrics_it->collectd_type) {
          case METRIC_TYPE_COUNTER:
            m = (metric_t){.value.counter = val};
            break;
          case METRIC_TYPE_GAUGE:
            m = (metric_t){.value.gauge = val};
            break;
          default:
            ERROR("Unimplemented collectd type");
            goto fail;
          }
          metric_label_set(&m, "path", unit_it->path);
          metric_family_metric_append(&fam, m);
          r = plugin_dispatch_metric_family(&fam);
          metric_family_metric_reset(&fam);
          if (r != 0) {
            ERROR("Failed to dispatch: %s", STRERROR(r));
            goto fail;
          }
        }
      }
    }
  }
  return EXIT_SUCCESS;

fail:
  sd_bus_error_free(&sd_bus_err);
  return r;
}

static int systemd_shutdown(void) {
  sd_bus_unref(bus);
  for (unit *unit_it = units; unit_it != units + units_num; ++unit_it) {
    free(unit_it->path);
  }
  free(units);
  return EXIT_SUCCESS;
}

void module_register(void) {
  plugin_register_complex_config("systemd", systemd_config);
  plugin_register_read("systemd", systemd_read);
  plugin_register_shutdown("systemd", systemd_shutdown);
}
