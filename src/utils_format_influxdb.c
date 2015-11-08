#include "utils_format_influxdb.h"


#define META_TAG_PREFIX "prefix"
#define META_TAG_PREFIX_LEN (sizeof (META_TAG_PREFIX) - 1)

#define META_TAG_MEASUREMENT "measurement"
#define META_TAG_MEASUREMENT_LEN (sizeof (META_TAG_MEASUREMENT) - 1)

#define META_TAG_TAG "tag"
#define META_TAG_TAG_LEN (sizeof (META_TAG_TAG) - 1)


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
    char *meta_prefix;
    unsigned meta_prefix_len;
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
    attrs->meta_prefix = NULL;
    attrs->meta_prefix_len = 0;

    return attrs;
}


int
influxdb_attrs_set_meta_prefix (influxdb_attrs_t *attrs, const char *meta_prefix)
{
    sfree (attrs->meta_prefix);

    if (meta_prefix != NULL) {
        attrs->meta_prefix = strdup (meta_prefix);
        if (attrs->meta_prefix == NULL) {
            attrs->meta_prefix_len = 0;
            return -1;
        }
        attrs->meta_prefix_len = strlen (attrs->meta_prefix);
    } else {
        attrs->meta_prefix = NULL;
        attrs->meta_prefix_len = 0;
    }

    return 0;
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


static int
format_tag (buffer_t *buf, const char *name, const char *value_fmt, const value_list_t *vl, const char *field)
{
    const size_t old_pos = buffer_getpos (buf);
    if (buffer_printf (buf, ",%s=", name) < 0)
        return -1;

    int rc = influxdb_format (buf, value_fmt, vl, field);

    if (rc <= 0)
        buffer_setpos (buf, old_pos);
    else
        rc = buffer_getpos (buf) - old_pos;

    return rc;
}


int
influxdb_attrs_format (buffer_t *buf, const influxdb_attrs_t *attrs, const value_list_t *vl, const char *field)
{
    const size_t orig_pos = buffer_getpos (buf);
    int meta_count = 0;
    char **keys = NULL;

    const bool use_meta = vl->meta != NULL && attrs->meta_prefix != NULL;
    char *prefix = NULL, *measurement = NULL;
    if (use_meta) {
        unsigned len = attrs->meta_prefix_len + 2;
        if (META_TAG_PREFIX_LEN > META_TAG_MEASUREMENT_LEN)
            len += META_TAG_PREFIX_LEN;
        else
            len += META_TAG_MEASUREMENT_LEN;

        char sbuf[len];
        ssnprintf (sbuf, sizeof (sbuf), "%s:%s", attrs->meta_prefix, META_TAG_PREFIX);
        meta_data_get_string (vl->meta, sbuf, &prefix);
        ssnprintf (sbuf, sizeof (sbuf), "%s:%s", attrs->meta_prefix, META_TAG_MEASUREMENT);
        meta_data_get_string (vl->meta, sbuf, &measurement);
    }

    if (prefix != NULL && buffer_putstr (buf, prefix) < 0)
        goto fail;

    const char *fmt = measurement != NULL ? measurement : attrs->fmt;
    int rc = influxdb_format (buf, fmt, vl, field);
    if (rc < 0)
        goto fail;

    influxdb_attr_t *attr;
    for (attr = attrs->first; attr != NULL; attr = attr->next) {
        /* Skip this tag if it's overridden by metadata. */
        if (use_meta) {
            char sbuf[attrs->meta_prefix_len + 1 + META_TAG_TAG_LEN + 1 + strlen (attr->name) + 1];
            ssnprintf (sbuf, sizeof (sbuf), "%s:%s:%s", attrs->meta_prefix, META_TAG_TAG, attr->name);
            if (meta_data_exists (vl->meta, sbuf))
                continue;
        }

        if (format_tag (buf, attr->name, attr->fmt, vl, field) < 0)
            goto fail;
    }

    if (use_meta) {
        char tag_prefix[attrs->meta_prefix_len + 1 + META_TAG_TAG_LEN + 2];
        const int tag_prefix_len = ssnprintf (tag_prefix, sizeof (tag_prefix), "%s:%s:", attrs->meta_prefix, META_TAG_TAG);
        if (tag_prefix_len < 0)
            goto fail;

        meta_count = meta_data_toc (vl->meta, &keys);
        if (meta_count < 0)
            goto fail;

        int i;
        for (i = 0; i < meta_count; i++) {
            if (strncmp (keys[i], tag_prefix, tag_prefix_len) != 0)
                continue;

            char *value = NULL;
            meta_data_get_string (vl->meta, keys[i], &value);
            if (value == NULL || value[0] == 0) {
                sfree (value);
                continue;
            }

            rc = format_tag (buf, keys[i] + tag_prefix_len, value, vl, field);
            sfree (value);

            if (rc < 0)
                goto fail;
        }
    }

    rc = buffer_getpos (buf) - orig_pos;
    goto cleanup;

fail:
    buffer_setpos (buf, orig_pos);
    rc = -1;

cleanup:
    sfree (prefix);
    sfree (measurement);
    if (keys != NULL) {
        int i;
        for (i = 0; i < meta_count; i++)
            sfree (keys[i]);
        sfree (keys);
    }

    return rc;
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
    sfree (fmt);
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
