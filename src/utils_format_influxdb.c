#include "utils_format_influxdb.h"


struct influxdb_attr_s {
    char *name;
    char *fmt;
    struct influxdb_attr_s *next;
};

typedef struct influxdb_attr_s influxdb_attr_t;


struct influxdb_attrs_s {
    char *fmt;
    influxdb_attr_t *first, *last;
    int flags;
};


influxdb_attrs_t *
influxdb_attrs_create (const char *main_fmt)
{
    int flags = influxdb_check_format (main_fmt);
    if (flags < 0)
        return NULL;

    influxdb_attrs_t *attrs = malloc (sizeof (*attrs));
    if (attrs == NULL)
        return NULL;

    attrs->fmt = strdup (main_fmt);
    if (attrs->fmt == NULL) {
        free (attrs);
        return NULL;
    }

    attrs->first = NULL;
    attrs->last = NULL;
    attrs->flags = flags;

    return attrs;
}


int influxdb_attrs_add (influxdb_attrs_t *attrs, const char *name, const char *fmt)
{
    int flags = influxdb_check_format (fmt);
    if (flags < 0)
        return -1;

    influxdb_attr_t *attr = malloc (sizeof (*attr));
    if (attr == NULL)
        return -1;

    attr->name = strdup (name);
    if (attr->name == NULL) {
        free (attr);
        return -1;
    }

    attr->fmt = strdup (fmt);
    if (attr->fmt == NULL) {
        free (attr->name);
        free (attr);
        return -1;
    }

    attrs->flags |= flags;

    attr->next = NULL;

    if (attrs->first == NULL)
        attrs->first = attr;
    else 
        attrs->last->next = attr;
    attrs->last = attr;

    return 0;
}


int
influxdb_attrs_flags (const influxdb_attrs_t *attrs)
{
    return attrs->flags;
}


int
influxdb_attrs_format (buffer_t *buf, const influxdb_attrs_t *attrs, const value_list_t *vl, const char *field)
{
    const size_t orig_pos = buffer_getpos (buf);

    if (influxdb_format (buf, attrs->fmt, vl, field) < 0)
        goto fail;

    influxdb_attr_t *attr;
    for (attr = attrs->first; attr != NULL; attr = attr->next) {
        const size_t old_pos = buffer_getpos (buf);
        if (buffer_printf (buf, ",%s=", attr->name) < 0)
            goto fail;
        int r = influxdb_format (buf, attr->fmt, vl, field);
        if (r < 0)
            goto fail;
        if (r == 0)
            buffer_setpos (buf, old_pos);
    }

    return buffer_getpos (buf) - orig_pos;

fail:
    buffer_setpos (buf, orig_pos);
    return -1;
}


void influxdb_attrs_free (influxdb_attrs_t *attrs)
{
    influxdb_attr_t *attr = attrs->first;
    while (attr != NULL) {
        influxdb_attr_t *next = attr->next;
        free (attr);
        attr = next;
    }

    free (attrs);
}


int
influxdb_quote (buffer_t *buf, const char *src)
{
    const size_t orig_pos = buffer_getpos (buf);

    while (*src != 0) {
        if ((*src == ' ' || *src == ',') && buffer_putc (buf, '\\') < 0)
            goto fail;
        if (buffer_putc (buf, *(src++)) < 0)
            goto fail;
    }

    return buffer_getpos (buf) - orig_pos;

fail:
    buffer_setpos (buf, orig_pos);
    return -1;
}


int
influxdb_check_format (const char *fmt)
{
    int r = 0;

    while (*fmt != 0) {
        if (*(fmt++) != '%')
            continue;
        switch (*(fmt++)) {
            case '%': break;
            case 'h': r |= INFLUXDB_FORMAT_HAS_HOSTNAME;    break;
            case 'p': r |= INFLUXDB_FORMAT_HAS_PLUGIN;      break;
            case 'i': r |= INFLUXDB_FORMAT_HAS_PLUGINST;    break;
            case 't': r |= INFLUXDB_FORMAT_HAS_TYPE;        break;
            case 'j': r |= INFLUXDB_FORMAT_HAS_TYPEINST;    break;
            case 'f': r |= INFLUXDB_FORMAT_HAS_FIELDNAME;   break;

            default: return -1;
        }
    }

    return r;
}


int
influxdb_format (buffer_t *buf, const char *fmt, const value_list_t *vl, const char *field)
{
    const size_t orig_pos = buffer_getpos (buf);

    while (*fmt != 0) {
        if (*fmt != '%') {
            buffer_putc (buf, *(fmt++));
        } else {
            const char *s = NULL;

            switch (fmt[1]) {
                case '%': s = "%";                  break;
                case 'h': s = vl->host;             break;
                case 'p': s = vl->plugin;           break;
                case 'i': s = vl->plugin_instance;  break;
                case 't': s = vl->type;             break;
                case 'j': s = vl->type_instance;    break;
                case 'f': s = field;                break;
                default:  goto fail;
            }

            if (influxdb_quote (buf, s) < 0)
                goto fail;

            fmt += 2;
        }
    }

    return buffer_getpos (buf) - orig_pos;

fail:
    buffer_setpos (buf, orig_pos);
    return -1;
}


influxdb_attrs_t *
influxdb_config_format (oconfig_item_t *ci)
{
    char *fmt = NULL;
    cf_util_get_string (ci, &fmt);
    if (fmt == NULL) {
        ERROR ("write_influxdb: Need format string");
        return NULL;
    }

    influxdb_attrs_t *attrs = influxdb_attrs_create (fmt);
    if (attrs == NULL) {
        ERROR ("write_influxdb: invalid format string: %s", fmt);
        return NULL;
    }

    int i;
    for (i = 0; i < ci->children_num; i++) {
        oconfig_item_t *child = ci->children + i;

        if (strcasecmp (child->key, "tag") == 0) {
            if (child->values_num != 2
                || child->values[0].type != OCONFIG_TYPE_STRING
                || child->values[1].type != OCONFIG_TYPE_STRING)
            {
                ERROR ("write_influxdb: invalid parameters for Tag");
                influxdb_attrs_free (attrs);
                return NULL;
            }

            if (influxdb_attrs_add (attrs, child->values[0].value.string, child->values[1].value.string) < 0) {
                ERROR ("write_influxdb: invalid format string: %s", child->values[1].value.string);
                influxdb_attrs_free (attrs);
                return NULL;
            }
        } else {
            ERROR ("write_influxdb: invalid config item: %s", child->key);
            influxdb_attrs_free (attrs);
            return NULL;
        }
    }

    return attrs;
}
