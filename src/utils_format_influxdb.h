#ifndef UTILS_FORMAT_INFLUXDB_H
#define UTILS_FORMAT_INFLUXDB_H

#include "daemon/common.h"
#include "utils_buffer.h"


/*
 * Format strings have some '%' escapes replaced with information
 * from a value list:
 *
 * %h - hostname
 * %p - plugin name
 * %i - plugin instance
 * %t - type
 * %j - type instance
 * %f - field name
 */


enum {
    INFLUXDB_FORMAT_HAS_HOSTNAME    = 1 << 0,
    INFLUXDB_FORMAT_HAS_PLUGIN      = 1 << 1,
    INFLUXDB_FORMAT_HAS_PLUGINST    = 1 << 2,
    INFLUXDB_FORMAT_HAS_TYPE        = 1 << 3,
    INFLUXDB_FORMAT_HAS_TYPEINST    = 1 << 4,
    INFLUXDB_FORMAT_HAS_FIELDNAME   = 1 << 5,
};


/*
 * Append quoted version of the src string to the buffer.
 * All instances of ',' and '\' have a leading backslash added.
 * Returns number of characters written, negative on failure.
 */
int influxdb_quote (buffer_t *buf, const char *src);

/*
 * Check for valid format string. Return a bitmask of flags as defined
 * above, or a negative value if invalid.
 */
int influxdb_check_format (const char *fmt);

/*
 * Replace '%' escapes in fmt with values in vl and field, append
 * result to buffer. Returns number of characters written or a negative
 * value on failure. In case of failure, nothing is added to the buffer.
 */
int influxdb_format (buffer_t *buf, const char *fmt, const value_list_t *vl, const char *field);

/*
 * This structure holds format strings for main measurement name and
 * zero or more attribute/value pairs ("tags" in InfluxDB language) to
 * be added.
 */
typedef struct influxdb_attrs_s influxdb_attrs_t;

/*
 * Create structure with format string for measurement name.
 */
influxdb_attrs_t *influxdb_attrs_create (const char *main_fmt);

/*
 * Add an attribute/value pair, where value is a format string.
 */
int influxdb_attrs_add (influxdb_attrs_t *attrs, const char *name, const char *fmt);

/*
 * Get the combined (ORed) flags for all format strings.
 */
int influxdb_attrs_flags (const influxdb_attrs_t *attrs);

/*
 * Append a string of the form "MEASUREMENT,TAG1=VALUE1,..." to the
 * buffer. Tags with empty values are omitted.
 * Returns number of characters written, or a negative value on failure,
 * in which case the buffer is unmodified.
 */
int influxdb_attrs_format (buffer_t *buf, const influxdb_attrs_t *attrs, const value_list_t *vl, const char *field);

/*
 * Free all memory used by influxdb_attrs_t.
 */
void influxdb_attrs_free (influxdb_attrs_t *attrs);


/*
 * Generate influxdb_attrs_t from configuration file section.
 */
influxdb_attrs_t *influxdb_config_format (oconfig_item_t *ci);


#endif /* UTILS_FORMAT_INFLUXDB_H */
