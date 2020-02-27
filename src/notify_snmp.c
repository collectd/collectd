/**
 * collectd - src/notify_snmp.c
 * Copyright (C) 2012 Manuel Sanmartin
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
 * Authors: Manuel Sanmartin <manuel.luis at gmail.com>
 **/

#include "collectd.h"
#include "utils/common/common.h"
#include "plugin.h"

#include <pthread.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

static oid objid_snmptrap[] = { 1, 3, 6, 1, 6, 3, 1, 1, 4, 1, 0 };
static oid objid_sysuptime[] = { 1, 3, 6, 1, 2, 1, 1, 3, 0 };

enum notify_snmp_enum_oids_e {
    NOTIFY_SNMP_NULL_OID,
    NOTIFY_SNMP_ENTERPRISE_OID,
    NOTIFY_SNMP_TRAP_OID,
    NOTIFY_SNMP_SEVERITY_OID,
    NOTIFY_SNMP_TIMESTAMP_OID,
    NOTIFY_SNMP_HOST_OID,
    NOTIFY_SNMP_PLUGIN_OID,
    NOTIFY_SNMP_PLUGIN_INSTANCE_OID,
    NOTIFY_SNMP_TYPE_OID,
    NOTIFY_SNMP_TYPE_INSTANCE_OID,
    NOTIFY_SNMP_DATA_SOURCE_OID,
    NOTIFY_SNMP_VALUE_OID,
    NOTIFY_SNMP_MESSAGE_OID
};
typedef enum notify_snmp_enum_oids_e notify_snmp_enum_oids_t;

static struct {
    notify_snmp_enum_oids_t id;
    char *name;
} notify_snmp_oids_map[] = {
    { NOTIFY_SNMP_ENTERPRISE_OID,      "EnterpriseOID" },
    { NOTIFY_SNMP_TRAP_OID,            "TrapOID" },
    { NOTIFY_SNMP_SEVERITY_OID,        "SeverityOID" },
    { NOTIFY_SNMP_TIMESTAMP_OID,       "TimeStampOID" },
    { NOTIFY_SNMP_HOST_OID,            "HostOID" },
    { NOTIFY_SNMP_PLUGIN_OID,          "PluginOID" },
    { NOTIFY_SNMP_PLUGIN_INSTANCE_OID, "PluginInstanceOID" },
    { NOTIFY_SNMP_TYPE_OID,            "TypeOID" },
    { NOTIFY_SNMP_TYPE_INSTANCE_OID,   "TypeInstanceOID" },
    { NOTIFY_SNMP_DATA_SOURCE_OID,     "DataSourceOID" },
    { NOTIFY_SNMP_VALUE_OID,           "ValueOID" },
    { NOTIFY_SNMP_MESSAGE_OID,         "MessageOID" },
    { NOTIFY_SNMP_NULL_OID,            NULL },
};

struct notify_snmp_oid_s {
    notify_snmp_enum_oids_t id;
    char    *string;
    oid     objid[MAX_OID_LEN];
    size_t  len;
};
typedef struct notify_snmp_oid_s notify_snmp_oid_t;

struct notify_snmp_oids_s {
    char    *name;
    notify_snmp_oid_t *list;
    int len;

    struct notify_snmp_oids_s *next;
};
typedef struct notify_snmp_oids_s notify_snmp_oids_t;

struct notify_snmp_target_s {
    char    *name;
    char    *address;
    char    *community;
    int     version;
    notify_snmp_oids_t *oids;
    void    *sess_handle;
    bool    sess_reuse;
    pthread_mutex_t session_lock;

    struct notify_snmp_target_s *next;
};
typedef struct notify_snmp_target_s notify_snmp_target_t;

static notify_snmp_target_t *notify_snmp_targets;
static notify_snmp_oids_t *notify_snmp_oids;

static struct {
    notify_snmp_enum_oids_t id;
    char *string;
} notify_snmp_default_oids[] = {
    { NOTIFY_SNMP_ENTERPRISE_OID,     "SNMPv2-SMI::experimental.100" },
    { NOTIFY_SNMP_TRAP_OID,           "SNMPv2-SMI::experimental.100.1" },
    { NOTIFY_SNMP_SEVERITY_OID,       "SNMPv2-SMI::experimental.100.2.1" },
    { NOTIFY_SNMP_TIMESTAMP_OID,      "SNMPv2-SMI::experimental.100.2.2" },
    { NOTIFY_SNMP_HOST_OID,           "SNMPv2-SMI::experimental.100.2.3" },
    { NOTIFY_SNMP_PLUGIN_OID,         "SNMPv2-SMI::experimental.100.2.4" },
    { NOTIFY_SNMP_PLUGIN_INSTANCE_OID,"SNMPv2-SMI::experimental.100.2.5" },
    { NOTIFY_SNMP_TYPE_OID,           "SNMPv2-SMI::experimental.100.2.6" },
    { NOTIFY_SNMP_TYPE_INSTANCE_OID,  "SNMPv2-SMI::experimental.100.2.7" },
    { NOTIFY_SNMP_DATA_SOURCE_OID,    "SNMPv2-SMI::experimental.100.2.8" },
    { NOTIFY_SNMP_VALUE_OID,          "SNMPv2-SMI::experimental.100.2.9" },
    { NOTIFY_SNMP_MESSAGE_OID,        "SNMPv2-SMI::experimental.100.2.10" },
    { NOTIFY_SNMP_NULL_OID,           NULL }
};

static void call_snmp_init_once (void) /* {{{ */
{
    static int have_init = 0;

    if (have_init == 0)
        init_snmp (PACKAGE_NAME);
    have_init = 1;
} /* }}} void call_snmp_init_once */

static char *notify_snmp_oids_map_id2name (notify_snmp_enum_oids_t id) /* {{{ */
{
    int i;

    for (i=0; notify_snmp_oids_map[i].name != NULL; i++)
    {
        if (notify_snmp_oids_map[i].id == id)
            return notify_snmp_oids_map[i].name;
    }

    return NULL;
} /* }}} char *notify_snmp_oid_name */

static notify_snmp_oids_t *notify_snmp_get_oids (char *name) /* {{{ */
{
    notify_snmp_oids_t *oids;

    oids = notify_snmp_oids;
    while (oids != NULL)
    {
        if ((name == NULL && oids->name==NULL) ||
            strcasecmp(oids->name, name) == 0)
            return oids;
        oids = oids->next;
    }

    return NULL;
} /* }}} notify_snmp_oids_t *notify_snmp_get_oids */

static notify_snmp_oid_t *notify_snmp_oids_get_oid (notify_snmp_oids_t *oids, /* {{{ */
    notify_snmp_enum_oids_t id)
{
    int i;

    for (i=0; i < oids->len; i++)
    {
        if (oids->list[i].id == id)
            return &oids->list[i];
    }
    return NULL;

} /* }}} notify_snmp_oids_t *notify_snmp_oids_get_oid */

static notify_snmp_target_t *notify_snmp_malloc_target(void) /* {{{ */
{
    notify_snmp_target_t *target;

    target = (notify_snmp_target_t *) calloc(sizeof(notify_snmp_target_t ), 1);
    if (target == NULL)
    {
        ERROR ("notify_snmp plugin: notify_snmp_malloc_target: malloc failed.");
        return (NULL);
    }

    pthread_mutex_init(&target->session_lock, /* attr = */ NULL);

    return target;
} /* }}} notify_snmp_target_t *notify_snmp_malloc_target */

static void notify_snmp_free_target(notify_snmp_target_t *target) /* {{{ */
{
    sfree(target->name);
    sfree(target->address);
    sfree(target->community);

    if(target->sess_handle != NULL)
        snmp_sess_close(target->sess_handle);

    sfree(target);
} /* }}} void notify_snmp_free_target */

static notify_snmp_oids_t *notify_snmp_malloc_oids(void) /* {{{ */
{
    notify_snmp_oids_t *oids;

    oids = (notify_snmp_oids_t *) calloc(sizeof(notify_snmp_oids_t), 1);
    if (oids == NULL)
    {
        ERROR ("notify_snmp plugin: notify_snmp_malloc_oids: malloc failed.");
        return (NULL);
    }

    return oids;
} /* }}} notify_snmp_oids_t *notify_snmp_malloc_oids */

static void notify_snmp_free_oids(notify_snmp_oids_t *oids) /* {{{ */
{
    int i;

    for (i=0 ; i < oids->len ; i++)
        sfree(oids->list[i].string);

    sfree(oids->name);

    sfree(oids);
} /* }}} void notify_snmp_free_oids */

static int notify_snmp_config_set_string (char **ret_string, oconfig_item_t *ci) /* {{{ */
{
    char *string;

    if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
    {
        WARNING ("notify_snmp plugin: The `%s' config option "
            "needs exactly one string argument.", ci->key);
        return (-1);
    }

    string = strdup (ci->values[0].value.string);
    if (string == NULL)
    {
        ERROR ("notify_snmp plugin: notify_snmp_config_set_string:"
            " strdup failed.");
        return (-1);
    }

    if (*ret_string != NULL)
        free (*ret_string);

    *ret_string = string;
    return (0);
} /* }}} int notify_snmp_config_set_string */

static int notify_snmp_config_set_integer (int *ret_number, oconfig_item_t *ci) /* {{{ */
{
    if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
    {
        WARNING ("notify_snmp plugin: The `%s' config option "
            "needs exactly one number argument.", ci->key);
        return (-1);
    }

    *ret_number = (int)ci->values[0].value.number;

    return (0);
} /* }}} int notify_snmp_config_set_int */

static int notify_snmp_config_set_target_oids (notify_snmp_oids_t **oids, /* {{{ */
    oconfig_item_t *ci)
{
    char *string;
    notify_snmp_oids_t *ret_oids;

    if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
    {
        WARNING ("notify_snmp plugin: The `%s' config option "
            "needs exactly one string argument.", ci->key);
        return (-1);
    }

    string = ci->values[0].value.string;

    ret_oids = notify_snmp_get_oids(string);
    if (ret_oids == NULL)
    {
        WARNING ("notify_snmp plugin: OIDs '%s' not found.", string);

        return (-1);
    }

    *oids = ret_oids;

    return (0);
} /* }}} int notify_snmp_config_set_target_oids */

static int notify_snmp_oids_append_oid (notify_snmp_oids_t **oids, /* {{{ */
    notify_snmp_enum_oids_t id, char *string_oid)
{
    notify_snmp_oid_t *oids_list;
    int n;

    oids_list = (notify_snmp_oid_t *) realloc ((*oids)->list,
        sizeof(notify_snmp_oid_t)*((*oids)->len+1));
    if (oids == NULL)
    {
        ERROR ("notify_snmp plugin: notify_snmp_oids_append_oid: realloc failed.");
        return (-1);
    }

    (*oids)->list = oids_list;
    n = (*oids)->len;
    (*oids)->len++;

    oids_list[n].string = strdup (string_oid);
    if (oids_list[n].string == NULL)
    {
        ERROR ("notify_snmp plugin: notify_snmp_oids_append_oid: strdup failed.");
        return (-1);
    }

    oids_list[n].len = MAX_OID_LEN;
    if (snmp_parse_oid(oids_list[n].string, oids_list[n].objid,
            &oids_list[n].len) == NULL)
    {
        ERROR ("notify_snmp plugin: OIDs %s: snmp_parse_oid %s (%s) failed.",
            ((*oids)->name == NULL) ? "default" : (*oids)->name,
            notify_snmp_oids_map_id2name(oids_list[n].id), oids_list[n].string);
        return (-1);
    }

    oids_list[n].id = id;

    return (0);
} /* }}} int notify_snmp_append_oid */

static int notify_snmp_config_oids_append_oid (notify_snmp_oids_t **oids, /* {{{ */
    notify_snmp_enum_oids_t id, oconfig_item_t *ci)
{
    if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
    {
        WARNING ("notify_snmp plugin: The `%s' config option "
            "needs exactly one string argument.", ci->key);
        return (-1);
    }

    return notify_snmp_oids_append_oid(oids, id, ci->values[0].value.string);
} /* }}} int notify_snmp_config_append_oid */

static notify_snmp_oids_t *notify_snmp_get_default_oids (void) /* {{{ */
{
    notify_snmp_oids_t *oids;
    int i;
    int status;

    oids = notify_snmp_get_oids(NULL);
    if (oids != NULL)
        return oids;

    oids = notify_snmp_malloc_oids();
    if (oids == NULL)
        return NULL;

    oids->name = NULL;

    for (i=0; notify_snmp_default_oids[i].string != NULL ; i++)
    {
        status = notify_snmp_oids_append_oid(&oids, notify_snmp_default_oids[i].id,
            notify_snmp_default_oids[i].string);
        if (status != 0)
        {
            notify_snmp_free_oids(oids);
            return NULL;
        }
    }

    if (notify_snmp_oids != NULL)
        oids->next = notify_snmp_oids->next;
    notify_snmp_oids = oids;

    return oids;
} /* }}} int notify_snmp_config_default_oids */

static int notify_snmp_config_add_oids (oconfig_item_t *ci) /* {{{ */
{
    notify_snmp_oids_t *oids;
    int i,n;
    int status;

    if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
    {
        WARNING ("notify_snmp plugin: The `Target' block "
        "needs exactly one string argument.");
        return (-1);
    }

    oids = notify_snmp_malloc_oids();
    if (oids == NULL)
        return (-1);

    status = notify_snmp_config_set_string (&oids->name, ci);
    if (status != 0)
    {
        notify_snmp_free_oids(oids);
        return (-1);
    }

    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;

        int found = 0;

        for (n=0; notify_snmp_oids_map[n].name != NULL; n++)
        {
            if (strcasecmp (notify_snmp_oids_map[n].name, child->key) == 0)
            {
                status = notify_snmp_config_oids_append_oid(&oids,
                    notify_snmp_oids_map[n].id, child);
                found = 1;
                break;
            }

        }

        if (found == 0)
        {
            WARNING ("notify_snmp plugin: Option `%s' not allowed here."
                , child->key);
            status = -1;
        }

        if (status != 0)
        {
            notify_snmp_free_oids(oids);
            return (-1);
        }
    }

    if (notify_snmp_oids != NULL)
        oids->next = notify_snmp_oids->next;
    notify_snmp_oids = oids;

    return (0);
} /* }}} int notify_snmp_config_add_oids */

static int notify_snmp_config_add_target (oconfig_item_t *ci) /* {{{ */
{
    notify_snmp_target_t *target;
    int status;
    int i;

    if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
    {
        WARNING ("notify_snmp plugin: The `Target' block "
        "needs exactly one string argument.");
        return (-1);
    }

    target = notify_snmp_malloc_target();
    if (target == NULL)
        return (-1);

    status = notify_snmp_config_set_string (&target->name, ci);
    if (status != 0)
    {
        notify_snmp_free_target(target);
        return (-1);
    }

    target->version = 1;
    target->sess_reuse = false;

    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;
        status = 0;

        if (strcasecmp ("Address", child->key) == 0)
            status = notify_snmp_config_set_string(&target->address, child);
        else if (strcasecmp ("Version", child->key) == 0)
        {
            status = notify_snmp_config_set_integer(&target->version, child);
            if (status == 0 && (target->version < 0 || target->version > 2))
                target->version = 1;
        }
        else if (strcasecmp ("Community", child->key) == 0)
            status = notify_snmp_config_set_string(&target->community, child);
        else if (strcasecmp ("OIDs", child->key) == 0)
            status = notify_snmp_config_set_target_oids (&target->oids, child);
        else if (strcasecmp ("SessionReuse", child->key) == 0)
            status = cf_util_get_boolean (child, &target->sess_reuse);
        else
        {
            WARNING ("notify_snmp plugin: Option `%s' not allowed here."
                , child->key);
            status = -1;
        }

        if (status != 0)
        {
            notify_snmp_free_target(target);
            return (-1);
        }
    }

    if (target->oids == NULL)
    {
        target->oids = notify_snmp_get_default_oids();
        if (target->oids == NULL)
        {
            WARNING ("notify_snmp plugin: cannot get default OIDs.");
            notify_snmp_free_target(target);
            return (-1);
        }
    }

    if (target->version == 1 &&
        notify_snmp_oids_get_oid(target->oids, NOTIFY_SNMP_ENTERPRISE_OID) == NULL)
    {
        WARNING ("notify_snmp plugin: With SNMP Version 1 need an Enterprise OID.");
        notify_snmp_free_target(target);
        return (-1);
    }

    if (target->version == 2 &&
        notify_snmp_oids_get_oid(target->oids, NOTIFY_SNMP_TRAP_OID) == NULL)
    {
        WARNING ("notify_snmp plugin: With SNMP Version 2 need a Trap OID.");
        notify_snmp_free_target(target);
        return (-1);
    }

    if (target->address == NULL)
        target->address = strdup ("localhost");

    if (target->community == NULL)
        target->community = strdup("public");

    if (notify_snmp_targets != NULL)
    {
        notify_snmp_target_t *targets = notify_snmp_targets;
        while(targets->next != NULL)
            targets = target->next;
        targets->next = target;
    }
    else
        notify_snmp_targets = target;

    return (0);
} /* }}} int notify_snmp_config_add_target */

/*
<Plugin notify_snmp>
  <OIDs collectd>
    EnterpriseOID     "SNMPv2-SMI::experimental.100"
    TrapOID           "SNMPv2-SMI::experimental.100.1"
    TimeStampOID      "SNMPv2-SMI::experimental.100.2.1"
    SeverityOID       "SNMPv2-SMI::experimental.100.2.2"
    HostOID           "SNMPv2-SMI::experimental.100.2.3"
    PluginOID         "SNMPv2-SMI::experimental.100.2.4"
    PluginInstanceOID "SNMPv2-SMI::experimental.100.2.5"
    TypeOID           "SNMPv2-SMI::experimental.100.2.6"
    TypeInstanceOID   "SNMPv2-SMI::experimental.100.2.7"
    DataSourceOID     "SNMPv2-SMI::experimental.100.2.8"
    ValueOID          "SNMPv2-SMI::experimental.100.2.9"
    MessageOID        "SNMPv2-SMI::experimental.100.2.10"
  </OIDs>
  <Target localhost>
    Address "localhost:162"
    Version 2
    Community "public"
    SessionReuse true
    OIDs collectd
  </Target>
</Plugin>
*/

static int notify_snmp_config (oconfig_item_t *ci) /* {{{ */
{
    int i;
    int status;

    call_snmp_init_once();

    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;
        if (strcasecmp ("Target", child->key) == 0)
        {
            status = notify_snmp_config_add_target (child);
            if (status < 0)
                return (-1);
        }
        else if (strcasecmp ("OIDs", child->key) == 0)
        {
            status = notify_snmp_config_add_oids (child);
            if (status < 0)
                return (-1);
        }
        else
            WARNING ("notify_snmp plugin: Ignoring unknown config option `%s'."
                , child->key);
    } /* for (ci->children) */
    return (0);
} /* }}} int notify_snmp_config */

static void notify_snmp_exit_session(notify_snmp_target_t *target) /* {{{ */
{
    if(target->sess_handle != NULL && !target->sess_reuse)
    {
        int status;

        status = snmp_sess_close(target->sess_handle);
        if (status == 0)
        {
            char *errstr = NULL;
            snmp_sess_error (target->sess_handle, NULL, NULL, &errstr);

            WARNING ("notify_snmp plugin: snmp_sess_close error: '%s'.",
                errstr);
        }
        target->sess_handle = NULL;
    }
} /* }}} void notify_snmp_exit_session */

static int notify_snmp_init_session(notify_snmp_target_t *target) /* {{{ */
{
    netsnmp_session session;

    if (target->sess_handle != NULL)
        notify_snmp_exit_session(target);

    snmp_sess_init(&session);
    session.version = target->version == 1 ? SNMP_VERSION_1 : SNMP_VERSION_2c;
    session.callback = NULL;
    session.callback_magic = NULL;
    session.peername = target->address;

    session.community = (u_char *)target->community;
    session.community_len = strlen(target->community);

    target->sess_handle = snmp_sess_open(&session);
    if (target->sess_handle == NULL)
    {
        char *errstr = NULL;

        snmp_error (&session, NULL, NULL, &errstr);

        ERROR("notify_snmp plugin: target %s: snmp_sess_open failed: %s",
            target->name, (errstr == NULL) ? "Unknown problem" : errstr);
        return (-1);
    }

    return (0);
} /* }}} int notify_snmp_init_session */

static netsnmp_pdu *notify_snmp_create_pdu (notify_snmp_target_t *target) /* {{{ */
{
    netsnmp_session *session;
    netsnmp_pdu *pdu = NULL;
    int status;

    session = snmp_sess_session(target->sess_handle);

    if (session->version == SNMP_VERSION_1)
    {
        notify_snmp_oid_t  *oid_enterprise = notify_snmp_oids_get_oid (
            target->oids, NOTIFY_SNMP_ENTERPRISE_OID);

        if (oid_enterprise == NULL)
        {
            ERROR ("notify_snmp plugin: notify_snmp_create_pdu "
                "cannot find Enterprise OID for Target %s.", target->name);
            return (NULL);
        }

        pdu = snmp_pdu_create(SNMP_MSG_TRAP);
        if (pdu == NULL)
        {
            ERROR ("notify_snmp plugin: Target %s: snmp_pdu_create failed.",
                target->name);
            return (NULL);
        }

        pdu->enterprise = (oid *) malloc(oid_enterprise->len * sizeof(oid));
        if (pdu->enterprise == NULL)
        {
            ERROR ("notify_snmp plugin: notify_snmp_create_pdu: "
                "malloc failed.");
            snmp_free_pdu(pdu);
            return (NULL);
        }

        memcpy(pdu->enterprise, oid_enterprise->objid,
            oid_enterprise->len * sizeof(oid));
        pdu->enterprise_length = oid_enterprise->len;

        pdu->trap_type = SNMP_TRAP_ENTERPRISESPECIFIC;
        pdu->specific_type = 0;
        pdu->time = get_uptime();
    }
    else if (session->version == SNMP_VERSION_2c)
    {
        char sysuptime[20];

        notify_snmp_oid_t *oid_trap = notify_snmp_oids_get_oid(target->oids,
            NOTIFY_SNMP_TRAP_OID);

        if (oid_trap == NULL)
        {
            ERROR ("notify_snmp plugin: notify_snmp_create_pdu "
                "cannot find Trap OID for Target %s.", target->name);
            return (NULL);
        }

        pdu = snmp_pdu_create(SNMP_MSG_TRAP2);
        if (pdu == NULL)
        {
            ERROR ("notify_snmp plugin: Target %s: snmp_pdu_create failed.",
                target->name);
            return (NULL);
        }

        status = ssnprintf( sysuptime, sizeof(sysuptime) , "%ld", get_uptime());
        if ((status < 0) || ((size_t) status >= sizeof (sysuptime)))
        {
            WARNING ("notify_snmp plugin: notify_snmp_create_pdu snprintf"
                " truncated sysuptime");
        }

        status = snmp_add_var(pdu, objid_sysuptime,
            sizeof(objid_sysuptime) / sizeof(oid), 't', sysuptime);
        if (status)
        {
            ERROR ("notify_snmp plugin: Target %s: "
                "snmp_add_var oid sysuptime failed", target->name);
            snmp_free_pdu(pdu);
            return (NULL);
        }

        status = snmp_add_var(pdu, objid_snmptrap,
            sizeof(objid_snmptrap) / sizeof(oid), 'o', oid_trap->string);
        if (status)
        {
            ERROR ("notify_snmp plugin: Target %s: "
                "snmp_add_var oid trap (%s) failed.",
                target->name, oid_trap->string);
            snmp_free_pdu(pdu);
            return (NULL);
        }
    }

    return pdu;
}
/* }}} netsnmp_pdu *notify_snmp_create_pdu */

static notification_meta_t *notify_snmp_notification_meta_get ( /* {{{ */
    notification_meta_t *meta, char *name)
{
    while (meta != NULL)
    {
        if (strcmp(meta->name, name) == 0)
            return meta;
        meta = meta->next;
    }
    return NULL;
} /* }}} notification_meta_t *notify_snmp_notification_meta_get */

static int notify_snmp_sendsnmp(notify_snmp_target_t *target, notification_t *n) /* {{{ */
{
    netsnmp_pdu *pdu = NULL;
    notify_snmp_oid_t *oids_list;
    int oids_len;
    int status;
    int i;

    if (target->sess_handle == NULL)
    {
        if (notify_snmp_init_session(target) < 0)
            return (-1);
    }

    pdu = notify_snmp_create_pdu (target);
    if (pdu == NULL)
        return (-1);

    oids_list = target->oids->list;
    oids_len = target->oids->len;

    for (i=0; i < oids_len ; i++)
    {
        char *value = NULL;
        char buffer[32];
        int add_value = 1;

        switch (oids_list[i].id)
        {
            case NOTIFY_SNMP_SEVERITY_OID:
                if (n->severity == NOTIF_FAILURE)
                    value = "FAILURE";
                else if (n->severity == NOTIF_WARNING)
                    value = "WARNING";
                else if (n->severity == NOTIF_OKAY)
                    value = "OKAY";
                else
                    value = "UNKNOWN";
                break;
            case NOTIFY_SNMP_TIMESTAMP_OID:
                status = ssnprintf(buffer, sizeof(buffer), "%u",
                                    (unsigned int)CDTIME_T_TO_TIME_T(n->time));
                if ((status < 0) || ((size_t) status >= sizeof (buffer)))
                    WARNING ("notify_snmp plugin: notify_snmp_sendsnmp:"
                        " truncate notification time.");
                value = buffer;
                break;
            case NOTIFY_SNMP_HOST_OID:
                value = n->host;
                break;
            case NOTIFY_SNMP_PLUGIN_OID:
                value = n->plugin;
                break;
            case NOTIFY_SNMP_PLUGIN_INSTANCE_OID:
                value = n->plugin_instance;
                break;
            case NOTIFY_SNMP_TYPE_OID:
                value = n->type;
                break;
            case NOTIFY_SNMP_TYPE_INSTANCE_OID:
                value = n->type_instance;
                break;
            case NOTIFY_SNMP_DATA_SOURCE_OID:
                {
                    notification_meta_t *meta =
                        notify_snmp_notification_meta_get(n->meta, "DataSource");
                    if (meta != NULL && meta->type == NM_TYPE_STRING)
                        value = (char *)meta->nm_value.nm_string;
                }
                break;
            case NOTIFY_SNMP_VALUE_OID:
                {
                    notification_meta_t *meta =
                        notify_snmp_notification_meta_get(n->meta, "CurrentValue");
                    if (meta != NULL && meta->type == NM_TYPE_DOUBLE)
                    {
                        status = ssnprintf(buffer, sizeof(buffer), "%f",
                            meta->nm_value.nm_double);
                        if ((status < 0) || ((size_t) status >= sizeof (buffer)))
                            WARNING ("notify_snmp plugin: notify_snmp_sendsnmp:"
                                " truncate notification CurrentValue.");
                        value = buffer;
                    }
                }
                break;
            case NOTIFY_SNMP_MESSAGE_OID:
                value = n->message;
                break;
            default:
                add_value = 0;
                break;
        }

        if (add_value)
        {

            status = snmp_add_var(pdu, oids_list[i].objid,
                oids_list[i].len, 's', value == NULL ? "" : value);

            if (status)
            {
                char *errstr = NULL;

                snmp_sess_error (target->sess_handle, NULL, NULL, &errstr);

                ERROR ("notify_snmp plugin: target %s: snmp_add_var"
                    " for %s (%s) failed: %s", target->name,
                    notify_snmp_oids_map_id2name(oids_list[i].id),
                    oids_list[i].string,
                    (errstr == NULL) ? "Unknown problem" : errstr );
                snmp_free_pdu(pdu);
                notify_snmp_exit_session(target);
                return (-1);
            }
        }
    }

    status = snmp_sess_send(target->sess_handle, pdu);
    if (status == 0)
    {
        char *errstr = NULL;

        snmp_sess_error (target->sess_handle, NULL, NULL, &errstr);

        ERROR ("notify_snmp plugin: target %s: snmp_sess_send failed: %s.",
            target->name, (errstr == NULL) ? "Unknown problem" : errstr );

        snmp_free_pdu(pdu);
        notify_snmp_exit_session(target);
        return (-1);
    }

    notify_snmp_exit_session(target);

    return(0);
} /* }}} int notify_snmp_sendsnmp */

static int notify_snmp_notification (const notification_t *n, /* {{{ */
    user_data_t __attribute__((unused)) *user_data)
{
    notify_snmp_target_t *target = notify_snmp_targets;
    int status;
    int ok = 0;
    int fail = 0;

    while (target != NULL)
    {
        pthread_mutex_lock (&target->session_lock);
        status = notify_snmp_sendsnmp (target, (notification_t *)n);
        pthread_mutex_unlock (&target->session_lock);

        if (status < 0)
            fail++;
        else
            ok++;
        target = target->next;
    }

    if (ok == 0 && fail > 0)
        return (-1);

    return (0);
} /* }}} int notify_snmp_notification */

static int notify_snmp_init (void) /* {{{ */
{
    call_snmp_init_once();

    return (0);
} /* }}} int notify_snmp_init */

static int notify_snmp_shutdown (void) /* {{{ */
{
    notify_snmp_target_t *target = notify_snmp_targets;
    notify_snmp_oids_t *oid = notify_snmp_oids;

    notify_snmp_targets = NULL;
    while (target != NULL)
    {
        notify_snmp_target_t *next;

        next = target->next;
        pthread_mutex_lock (&target->session_lock);
        pthread_mutex_unlock (&target->session_lock);
        pthread_mutex_destroy (&target->session_lock);
        notify_snmp_free_target(target);
        target = next;
    }

    notify_snmp_oids = NULL;
    while (oid != NULL)
    {
        notify_snmp_oids_t *next;

        next = oid->next;
        notify_snmp_free_oids(oid);
        oid = next;
    }

    return (0);
} /* }}} int notify_snmp_shutdown */

void module_register (void)
{
    plugin_register_init ("notify_snmp", notify_snmp_init);
    plugin_register_shutdown ("notify_snmp", notify_snmp_shutdown);
    plugin_register_complex_config ("notify_snmp", notify_snmp_config);
    plugin_register_notification ("notify_snmp", notify_snmp_notification,
          /* user_data = */ NULL);
} /* void module_register (void) */

/* vim: set sw=4 ts=4 tw=78 et : */
