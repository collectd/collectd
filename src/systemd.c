#include "liboconfig/oconfig.h"
#include "metric.h"
#include "plugin.h"
#include "utils/common/common.h"

#include "systemd/sd-bus.h"

#include <libxml/parser.h>
#include <libxml/xpath.h>

typedef enum { SUCCESS = 0, FAIL = 1 } ret_code;

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
static size_t nunits = 0;

static sd_bus *bus = NULL;

static bool was_service = false;
static bool was_slice = false;

// `*prop` will be `NULL` if the systemd version doesn't provide given property.
static ret_code
introspect_prop(xmlXPathContextPtr const introspection_xpath_ctx,
                char const interface[const static 1], char **const prop) {
  char query[128];
  snprintf(query, sizeof(query),
           "//interface[@name=\"%s\"]/property[@name=\"%s\"]", interface,
           *prop);

  xmlXPathObjectPtr const xpath_obj =
      xmlXPathEvalExpression(BAD_CAST(query), introspection_xpath_ctx);
  if (!xpath_obj) {
    ERROR("Can't get xpath object");
    return FAIL;
  }
  if (xpath_obj->nodesetval->nodeNr == 0) {
    WARNING("This systemd version doesn't provide %s property in %s interface",
            *prop, interface);
    *prop = NULL;
  }

  return SUCCESS;
}

// Returns `NULL` on an error.
static xmlXPathContextPtr get_introspection_xpath_ctx(unit const *const unit) {
  xmlXPathContextPtr introspection_xpath_ctx = NULL;
  int r;
  sd_bus_error sd_bus_err = SD_BUS_ERROR_NULL;
  sd_bus_message *m = NULL;

  if ((r = sd_bus_call_method(bus, "org.freedesktop.systemd1", unit->path,
                              "org.freedesktop.DBus.Introspectable",
                              "Introspect", &sd_bus_err, &m, "")) < 0) {
    ERROR("Can't introspect %s: %s {%s}, %s", unit->path, sd_bus_err.name,
          sd_bus_err.message, strerror(-r));
    goto cleanup;
  }

  char const *xml;
  if ((r = sd_bus_message_read(m, "s", &xml) < 0)) {
    ERROR("Can't read %s introspection: %s {%s}, %s", unit->path,
          sd_bus_err.name, sd_bus_err.message, strerror(-r));
    goto cleanup;
  }

  xmlDocPtr doc = xmlReadMemory(xml, strlen(xml), "noname.xml", NULL, 0);
  if (!doc) {
    ERROR("Can't parse xml: %s", xml);
    goto cleanup;
  }

  introspection_xpath_ctx = xmlXPathNewContext(doc);
  if (!introspection_xpath_ctx) {
    ERROR("Can't get context of the xml");
    goto cleanup;
  }

cleanup:
  sd_bus_message_unref(m);
  sd_bus_error_free(&sd_bus_err);

  return introspection_xpath_ctx;
}

static ret_code introspect_unit(unit const *const unit) {
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

  xmlXPathContextPtr introspection_xpath_ctx =
      get_introspection_xpath_ctx(unit);
  for (systemd_metric_group *groups_it = groups; groups_it != groups + ngroups;
       ++groups_it) {
    if (groups_it->accounting_flag &&
        introspect_prop(introspection_xpath_ctx, interface,
                        &groups_it->accounting_flag) == FAIL)
      return FAIL;

    for (systemd_metric *metric_it = groups_it->metrics;
         metric_it->collectd_type != METRIC_TYPE_UNTYPED; ++metric_it)
      if (introspect_prop(introspection_xpath_ctx, interface,
                          &metric_it->name) == FAIL)
        return FAIL;
  }

  if (unit->is_slice)
    was_slice = true;
  else
    was_service = true;

  return SUCCESS;
}

static ret_code get_unit_path(oconfig_item_t *oconfig_item, char **unit_path) {
  int r;
  char *external_id = NULL;

  if (cf_util_get_string(oconfig_item, &external_id) != 0) {
    ERROR("Error during parsing the config");
    return FAIL;
  }

  if ((r = sd_bus_path_encode("/org/freedesktop/systemd1/unit", external_id,
                              unit_path)) < 0) {
    ERROR("Can't encode \"%s\" unit: %s", external_id, strerror(-r));
    return FAIL;
  }

  return SUCCESS;
}

static int systemd_config(oconfig_item_t *ci) {
  int r;

  if (!bus && (r = sd_bus_open_system(&bus)) < 0) {
    ERROR("Failed to connect to system bus: %s", strerror(-r));
    sd_bus_unref(bus);
    return FAIL;
  }

  nunits += ci->children_num;
  units = realloc(units, sizeof(unit) * nunits);
  if (!units) {
    ERROR("Can't allocate memory for units");
    return FAIL;
  }

  for (size_t i = 0; i < ci->children_num; ++i) {
    oconfig_item_t *child = ci->children + i;
    unit unit;

    unit.is_slice = !strcmp(child->key, "Slice");
    if (!unit.is_slice && strcmp(child->key, "Service")) {
      ERROR("Invalid config item: %s", child->key);
      return FAIL;
    }

    if (get_unit_path(child, &unit.path) == FAIL)
      return FAIL;

    units[nunits - ci->children_num + i] = unit;

    if (((!was_slice && unit.is_slice) || (!was_service && !unit.is_slice)) &&
        introspect_unit(&unit) == FAIL)
      return FAIL;
  }

  return SUCCESS;
}

static ret_code get_prop(sd_bus *bus, char const *interface, char const *unit,
                         char const type[static 1], char const prop[static 1],
                         void *var, sd_bus_error *err) {
  int r;
  sd_bus_message *m = NULL;

  if ((r = sd_bus_get_property(bus, "org.freedesktop.systemd1", unit, interface,
                               prop, err, &m, type) < 0)) {
    ERROR("Failed to get %s property: %s {%s}, %s", prop, err->name,
          err->message, strerror(-r));
    return FAIL;
  }

  if ((r = sd_bus_message_read(m, type, var) < 0)) {
    ERROR("Failed to read %s property: %s {%s}, %s", prop, err->name,
          err->message, strerror(-r));
    return FAIL;
  }

  sd_bus_message_unref(m);
  return SUCCESS;
}

static ret_code submit(systemd_metric *metric, uint64_t val, char *unit_path) {
  metric_family_t fam = {
      .name = metric->name,
      .type = metric->collectd_type,
  };

  metric_t m;
  switch (metric->collectd_type) {
  case METRIC_TYPE_COUNTER:
    m = (metric_t){.value.counter = val};
    break;
  case METRIC_TYPE_GAUGE:
    m = (metric_t){.value.gauge = val};
    break;
  default:
    ERROR("Unimplemented collectd type: %d", metric->collectd_type);
    return FAIL;
  }

  metric_label_set(&m, "path", unit_path);
  metric_family_metric_append(&fam, m);
  int r = plugin_dispatch_metric_family(&fam);
  metric_family_metric_reset(&fam);

  if (r != 0) {
    ERROR("Failed to dispatch: %s", STRERROR(r));
    return FAIL;
  }
  return SUCCESS;
}

static ret_code submit_unit(systemd_metric_group const *groups, size_t ngroups,
                            char const *interface, char *unit_path,
                            sd_bus_error *sd_bus_err) {
  for (systemd_metric_group const *groups_it = groups;
       groups_it != groups + ngroups; ++groups_it) {
    bool accounting_flag = true;

    if (groups_it->accounting_flag &&
        get_prop(bus, interface, unit_path, "b", groups_it->accounting_flag,
                 &accounting_flag, sd_bus_err) == FAIL)
      return FAIL;

    if (accounting_flag)
      for (systemd_metric *metrics_it = groups_it->metrics;
           metrics_it->collectd_type != METRIC_TYPE_UNTYPED; ++metrics_it) {
        if (!metrics_it->name)
          continue;

        uint64_t val;
        if (get_prop(bus, interface, unit_path, metrics_it->dbus_type,
                     metrics_it->name, &val, sd_bus_err) == FAIL)
          return FAIL;

        if (submit(metrics_it, val, unit_path) == FAIL)
          return FAIL;
      }
  }

  return SUCCESS;
}

static int systemd_read(void) {
  sd_bus_error sd_bus_err = SD_BUS_ERROR_NULL;

  for (unit *unit_it = units; unit_it != units + nunits; ++unit_it) {
    systemd_metric_group const *groups;
    size_t ngroups;
    char const *interface;
    if (unit_it->is_slice) {
      groups = slice_groups;
      ngroups = STATIC_ARRAY_SIZE(slice_groups);
      interface = "org.freedesktop.systemd1.Slice";
    } else {
      groups = service_groups;
      ngroups = STATIC_ARRAY_SIZE(service_groups);
      interface = "org.freedesktop.systemd1.Service";
    }

    if (submit_unit(groups, ngroups, interface, unit_it->path, &sd_bus_err)) {
      sd_bus_error_free(&sd_bus_err);
      return FAIL;
    }
  }

  return SUCCESS;
}

static int systemd_shutdown(void) {
  sd_bus_unref(bus);

  for (unit *unit_it = units; unit_it != units + nunits; ++unit_it)
    free(unit_it->path);
  free(units);

  return SUCCESS;
}

void module_register(void) {
  plugin_register_complex_config("systemd", systemd_config);
  plugin_register_read("systemd", systemd_read);
  plugin_register_shutdown("systemd", systemd_shutdown);
}
