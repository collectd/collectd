/**
 * collectd - src/uuid.c
 * Copyright (C) 2007  Red Hat Inc.
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
 *   Dan Berrange <berrange@redhat.com>
 *   Richard W.M. Jones <rjones@redhat.com>
 *
 * Derived from UUID detection code by Dan Berrange <berrange@redhat.com>
 * http://hg.et.redhat.com/virt/daemons/spectre--devel?f=f6e3a1b06433;file=lib/uuid.c
 **/

#include "collectd.h"
#include "common.h"
#include "configfile.h"
#include "plugin.h"

#if HAVE_LIBHAL
#include <libhal.h>
#endif

#define UUID_RAW_LENGTH 16
#define UUID_PRINTABLE_COMPACT_LENGTH  (UUID_RAW_LENGTH * 2)
#define UUID_PRINTABLE_NORMAL_LENGTH  (UUID_PRINTABLE_COMPACT_LENGTH + 4)

static char *uuidfile = NULL;

static const char *config_keys[] = {
    "UUIDFile"
};

static int
looks_like_a_uuid (const char *uuid)
{
    int len;

    if (!uuid) return 0;

    len = strlen (uuid);

    if (len < UUID_PRINTABLE_COMPACT_LENGTH)
        return 0;

    while (*uuid) {
        if (!isxdigit ((int)*uuid) && *uuid != '-') return 0;
        uuid++;
    }
    return 1;
}

static char *
uuid_parse_dmidecode(FILE *file)
{
    char line[1024];

    while (fgets (line, sizeof (line), file) != NULL)
    {
        char *fields[4];
        int fields_num;

        strstripnewline (line);

        /* Look for a line reading:
         *   UUID: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
         */
        fields_num = strsplit (line, fields, STATIC_ARRAY_SIZE (fields));
        if (fields_num != 2)
            continue;

        if (strcmp("UUID:", fields[0]) != 0)
            continue;

        if (!looks_like_a_uuid (fields[1]))
            continue;

        return strdup (fields[1]);
    }
    return NULL;
}

static char *
uuid_get_from_dmidecode(void)
{
    FILE *dmidecode = popen("dmidecode 2>/dev/null", "r");
    char *uuid;

    if (!dmidecode) {
        return NULL;
    }
    
    uuid = uuid_parse_dmidecode(dmidecode);

    pclose(dmidecode);
    return uuid;
}

#if HAVE_LIBHAL

#define UUID_PATH "/org/freedesktop/Hal/devices/computer"
#define UUID_PROPERTY "smbios.system.uuid"

static char *
uuid_get_from_hal(void)
{
    LibHalContext *ctx;

    DBusError error;
    DBusConnection *con;

    dbus_error_init(&error);

    if (!(con = dbus_bus_get(DBUS_BUS_SYSTEM, &error)) ) {
        goto bailout_nobus;
    }

    ctx = libhal_ctx_new();
    libhal_ctx_set_dbus_connection(ctx, con);

    if (!libhal_ctx_init(ctx, &error)) {
        goto bailout;
    }

    if (!libhal_device_property_exists(ctx,
                                       UUID_PATH,
                                       UUID_PROPERTY,
                                       &error)) {
        goto bailout;
    }

    char *uuid  = libhal_device_get_property_string(ctx,
                                                    UUID_PATH,
                                                    UUID_PROPERTY,
                                                    &error);
    if (looks_like_a_uuid (uuid)) {
        return uuid;
    }

 bailout:
    {
        DBusError ctxerror;
        dbus_error_init(&ctxerror);
        if (!(libhal_ctx_shutdown(ctx, &ctxerror))) {
            dbus_error_free(&ctxerror);
        }
    }

    libhal_ctx_free(ctx);
    //dbus_connection_unref(con);

 bailout_nobus:
    if (dbus_error_is_set(&error)) {
        /*printf("Error %s\n", error.name);*/
        dbus_error_free(&error);
    }
    return NULL;
}
#endif

static char *
uuid_get_from_file(const char *path)
{
    FILE *file;
    char uuid[UUID_PRINTABLE_NORMAL_LENGTH + 1] = "";

    file = fopen (path, "r");
    if (file == NULL)
        return NULL;

    if (!fgets(uuid, sizeof(uuid), file)) {
        fclose(file);
        return NULL;
    }
    fclose(file);
    strstripnewline (uuid);

    return strdup (uuid);
}

static char *
uuid_get_local(void)
{
    char *uuid;

    /* Check /etc/uuid / UUIDFile before any other method. */
    if ((uuid = uuid_get_from_file(uuidfile ? uuidfile : "/etc/uuid")) != NULL){
        return uuid;
    }

#if HAVE_LIBHAL
    if ((uuid = uuid_get_from_hal()) != NULL) {
        return uuid;
    }
#endif

    if ((uuid = uuid_get_from_dmidecode()) != NULL) {
        return uuid;
    }

    if ((uuid = uuid_get_from_file("/sys/hypervisor/uuid")) != NULL) {
        return uuid;
    }

    return NULL;
}

static int
uuid_config (const char *key, const char *value)
{
    if (strcasecmp (key, "UUIDFile") == 0) {
        char *tmp = strdup (value);
        if (tmp == NULL)
            return -1;
        sfree (uuidfile);
        uuidfile = tmp;
    } else {
        return 1;
    }

    return 0;
}

static int
uuid_init (void)
{
    char *uuid = uuid_get_local ();

    if (uuid) {
        sstrncpy (hostname_g, uuid, DATA_MAX_NAME_LEN);
        sfree (uuid);
        return 0;
    }

    WARNING ("uuid: could not read UUID using any known method");
    return 0;
}

void module_register (void)
{
    plugin_register_config ("uuid", uuid_config,
            config_keys, STATIC_ARRAY_SIZE (config_keys));
    plugin_register_init ("uuid", uuid_init);
}

/*
 * vim: set tabstop=4:
 * vim: set shiftwidth=4:
 * vim: set expandtab:
 */
/*
 * Local variables:
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
