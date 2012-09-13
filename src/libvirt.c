/**
 * collectd - src/libvirt.c
 * Copyright (C) 2006-2008  Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the license is applicable.
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
 *   Richard W.M. Jones <rjones@redhat.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_ignorelist.h"
#include "utils_complain.h"

#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

static const char *config_keys[] = {
    "Connection",

    "RefreshInterval",

    "Domain",
    "BlockDevice",
    "InterfaceDevice",
    "IgnoreSelected",

    "HostnameFormat",
    "InterfaceFormat",

    NULL
};
#define NR_CONFIG_KEYS ((sizeof config_keys / sizeof config_keys[0]) - 1)

/* Connection. */
static virConnectPtr conn = 0;
static char *conn_string = NULL;
static c_complain_t conn_complain = C_COMPLAIN_INIT_STATIC;

/* Seconds between list refreshes, 0 disables completely. */
static int interval = 60;

/* List of domains, if specified. */
static ignorelist_t *il_domains = NULL;
/* List of block devices, if specified. */
static ignorelist_t *il_block_devices = NULL;
/* List of network interface devices, if specified. */
static ignorelist_t *il_interface_devices = NULL;

static int ignore_device_match (ignorelist_t *,
                                const char *domname, const char *devpath);

/* Actual list of domains found on last refresh. */
static virDomainPtr *domains = NULL;
static int nr_domains = 0;

static void free_domains (void);
static int add_domain (virDomainPtr dom);

/* Actual list of block devices found on last refresh. */
struct block_device {
    virDomainPtr dom;           /* domain */
    char *path;                 /* name of block device */
};

static struct block_device *block_devices = NULL;
static int nr_block_devices = 0;

static void free_block_devices (void);
static int add_block_device (virDomainPtr dom, const char *path);

/* Actual list of network interfaces found on last refresh. */
struct interface_device {
    virDomainPtr dom;           /* domain */
    char *path;                 /* name of interface device */
    char *address;              /* mac address of interface device */
};

static struct interface_device *interface_devices = NULL;
static int nr_interface_devices = 0;

static void free_interface_devices (void);
static int add_interface_device (virDomainPtr dom, const char *path, const char *address);

/* HostnameFormat. */
#define HF_MAX_FIELDS 3

enum hf_field {
    hf_none = 0,
    hf_hostname,
    hf_name,
    hf_uuid
};

static enum hf_field hostname_format[HF_MAX_FIELDS] =
    { hf_name };

/* InterfaceFormat. */
enum if_field {
    if_address,
    if_name
};

static enum if_field interface_format = if_name;

/* Time that we last refreshed. */
static time_t last_refresh = (time_t) 0;

static int refresh_lists (void);

/* ERROR(...) macro for virterrors. */
#define VIRT_ERROR(conn,s) do {                 \
        virErrorPtr err;                        \
        err = (conn) ? virConnGetLastError ((conn)) : virGetLastError (); \
        if (err) ERROR ("%s: %s", (s), err->message);                   \
    } while(0)

static void
init_value_list (value_list_t *vl, virDomainPtr dom)
{
    int i, n;
    const char *name;
    char uuid[VIR_UUID_STRING_BUFLEN];

    vl->interval = interval_g;

    sstrncpy (vl->plugin, "libvirt", sizeof (vl->plugin));

    vl->host[0] = '\0';

    /* Construct the hostname field according to HostnameFormat. */
    for (i = 0; i < HF_MAX_FIELDS; ++i) {
        if (hostname_format[i] == hf_none)
            continue;

        n = DATA_MAX_NAME_LEN - strlen (vl->host) - 2;

        if (i > 0 && n >= 1) {
            strncat (vl->host, ":", 1);
            n--;
        }

        switch (hostname_format[i]) {
        case hf_none: break;
        case hf_hostname:
            strncat (vl->host, hostname_g, n);
            break;
        case hf_name:
            name = virDomainGetName (dom);
            if (name)
                strncat (vl->host, name, n);
            break;
        case hf_uuid:
            if (virDomainGetUUIDString (dom, uuid) == 0)
                strncat (vl->host, uuid, n);
            break;
        }
    }

    vl->host[sizeof (vl->host) - 1] = '\0';
} /* void init_value_list */

static void
cpu_submit (unsigned long long cpu_time,
            virDomainPtr dom, const char *type)
{
    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;

    init_value_list (&vl, dom);

    values[0].derive = cpu_time;

    vl.values = values;
    vl.values_len = 1;

    sstrncpy (vl.type, type, sizeof (vl.type));

    plugin_dispatch_values (&vl);
}

static void
vcpu_submit (derive_t cpu_time,
             virDomainPtr dom, int vcpu_nr, const char *type)
{
    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;

    init_value_list (&vl, dom);

    values[0].derive = cpu_time;
    vl.values = values;
    vl.values_len = 1;

    sstrncpy (vl.type, type, sizeof (vl.type));
    ssnprintf (vl.type_instance, sizeof (vl.type_instance), "%d", vcpu_nr);

    plugin_dispatch_values (&vl);
}

static void
submit_derive2 (const char *type, derive_t v0, derive_t v1,
             virDomainPtr dom, const char *devname)
{
    value_t values[2];
    value_list_t vl = VALUE_LIST_INIT;

    init_value_list (&vl, dom);

    values[0].derive = v0;
    values[1].derive = v1;
    vl.values = values;
    vl.values_len = 2;

    sstrncpy (vl.type, type, sizeof (vl.type));
    sstrncpy (vl.type_instance, devname, sizeof (vl.type_instance));

    plugin_dispatch_values (&vl);
} /* void submit_derive2 */

static int
lv_init (void)
{
    if (virInitialize () != 0)
        return -1;

	return 0;
}

static int
lv_config (const char *key, const char *value)
{
    if (virInitialize () != 0)
        return 1;

    if (il_domains == NULL)
        il_domains = ignorelist_create (1);
    if (il_block_devices == NULL)
        il_block_devices = ignorelist_create (1);
    if (il_interface_devices == NULL)
        il_interface_devices = ignorelist_create (1);

    if (strcasecmp (key, "Connection") == 0) {
        char *tmp = strdup (value);
        if (tmp == NULL) {
            ERROR ("libvirt plugin: Connection strdup failed.");
            return 1;
        }
        sfree (conn_string);
        conn_string = tmp;
        return 0;
    }

    if (strcasecmp (key, "RefreshInterval") == 0) {
        char *eptr = NULL;
        interval = strtol (value, &eptr, 10);
        if (eptr == NULL || *eptr != '\0') return 1;
        return 0;
    }

    if (strcasecmp (key, "Domain") == 0) {
        if (ignorelist_add (il_domains, value)) return 1;
        return 0;
    }
    if (strcasecmp (key, "BlockDevice") == 0) {
        if (ignorelist_add (il_block_devices, value)) return 1;
        return 0;
    }
    if (strcasecmp (key, "InterfaceDevice") == 0) {
        if (ignorelist_add (il_interface_devices, value)) return 1;
        return 0;
    }

    if (strcasecmp (key, "IgnoreSelected") == 0) {
        if (IS_TRUE (value))
        {
            ignorelist_set_invert (il_domains, 0);
            ignorelist_set_invert (il_block_devices, 0);
            ignorelist_set_invert (il_interface_devices, 0);
        }
        else
        {
            ignorelist_set_invert (il_domains, 1);
            ignorelist_set_invert (il_block_devices, 1);
            ignorelist_set_invert (il_interface_devices, 1);
        }
        return 0;
    }

    if (strcasecmp (key, "HostnameFormat") == 0) {
        char *value_copy;
        char *fields[HF_MAX_FIELDS];
        int i, n;

        value_copy = strdup (value);
        if (value_copy == NULL) {
            ERROR ("libvirt plugin: strdup failed.");
            return -1;
        }

        n = strsplit (value_copy, fields, HF_MAX_FIELDS);
        if (n < 1) {
            sfree (value_copy);
            ERROR ("HostnameFormat: no fields");
            return -1;
        }

        for (i = 0; i < n; ++i) {
            if (strcasecmp (fields[i], "hostname") == 0)
                hostname_format[i] = hf_hostname;
            else if (strcasecmp (fields[i], "name") == 0)
                hostname_format[i] = hf_name;
            else if (strcasecmp (fields[i], "uuid") == 0)
                hostname_format[i] = hf_uuid;
            else {
                sfree (value_copy);
                ERROR ("unknown HostnameFormat field: %s", fields[i]);
                return -1;
            }
        }
        sfree (value_copy);

        for (i = n; i < HF_MAX_FIELDS; ++i)
            hostname_format[i] = hf_none;

        return 0;
    }

    if (strcasecmp (key, "InterfaceFormat") == 0) {
        if (strcasecmp (value, "name") == 0)
            interface_format = if_name;
        else if (strcasecmp (value, "address") == 0)
            interface_format = if_address;
        else {
            ERROR ("unknown InterfaceFormat: %s", value);
            return -1;
        }
        return 0;
    }

    /* Unrecognised option. */
    return -1;
}

static int
lv_read (void)
{
    time_t t;
    int i;

    if (conn == NULL) {
        /* `conn_string == NULL' is acceptable. */
        conn = virConnectOpenReadOnly (conn_string);
        if (conn == NULL) {
            c_complain (LOG_ERR, &conn_complain,
                    "libvirt plugin: Unable to connect: "
                    "virConnectOpenReadOnly failed.");
            return -1;
        }
    }
    c_release (LOG_NOTICE, &conn_complain,
            "libvirt plugin: Connection established.");

    time (&t);

    /* Need to refresh domain or device lists? */
    if ((last_refresh == (time_t) 0) ||
            ((interval > 0) && ((last_refresh + interval) <= t))) {
        if (refresh_lists () != 0) {
            if (conn != NULL)
                virConnectClose (conn);
            conn = NULL;
            return -1;
        }
        last_refresh = t;
    }

#if 0
    for (i = 0; i < nr_domains; ++i)
        fprintf (stderr, "domain %s\n", virDomainGetName (domains[i]));
    for (i = 0; i < nr_block_devices; ++i)
        fprintf  (stderr, "block device %d %s:%s\n",
                  i, virDomainGetName (block_devices[i].dom),
                  block_devices[i].path);
    for (i = 0; i < nr_interface_devices; ++i)
        fprintf (stderr, "interface device %d %s:%s\n",
                 i, virDomainGetName (interface_devices[i].dom),
                 interface_devices[i].path);
#endif

    /* Get CPU usage, VCPU usage for each domain. */
    for (i = 0; i < nr_domains; ++i) {
        virDomainInfo info;
        virVcpuInfoPtr vinfo = NULL;
        int status;
        int j;

        status = virDomainGetInfo (domains[i], &info);
        if (status != 0)
        {
            ERROR ("libvirt plugin: virDomainGetInfo failed with status %i.",
                    status);
            continue;
        }

        cpu_submit (info.cpuTime, domains[i], "virt_cpu_total");

        vinfo = malloc (info.nrVirtCpu * sizeof (vinfo[0]));
        if (vinfo == NULL) {
            ERROR ("libvirt plugin: malloc failed.");
            continue;
        }

        status = virDomainGetVcpus (domains[i], vinfo, info.nrVirtCpu,
                /* cpu map = */ NULL, /* cpu map length = */ 0);
        if (status < 0)
        {
            ERROR ("libvirt plugin: virDomainGetVcpus failed with status %i.",
                    status);
            free (vinfo);
            continue;
        }

        for (j = 0; j < info.nrVirtCpu; ++j)
            vcpu_submit (vinfo[j].cpuTime,
                    domains[i], vinfo[j].number, "virt_vcpu");

        sfree (vinfo);
    }

    /* Get block device stats for each domain. */
    for (i = 0; i < nr_block_devices; ++i) {
        struct _virDomainBlockStats stats;

        if (virDomainBlockStats (block_devices[i].dom, block_devices[i].path,
                    &stats, sizeof stats) != 0)
            continue;

        if ((stats.rd_req != -1) && (stats.wr_req != -1))
            submit_derive2 ("disk_ops",
                    (derive_t) stats.rd_req, (derive_t) stats.wr_req,
                    block_devices[i].dom, block_devices[i].path);

        if ((stats.rd_bytes != -1) && (stats.wr_bytes != -1))
            submit_derive2 ("disk_octets",
                    (derive_t) stats.rd_bytes, (derive_t) stats.wr_bytes,
                    block_devices[i].dom, block_devices[i].path);
    } /* for (nr_block_devices) */

    /* Get interface stats for each domain. */
    for (i = 0; i < nr_interface_devices; ++i) {
        struct _virDomainInterfaceStats stats;
        char *display_name = interface_devices[i].path;

        if (interface_format == if_address)
            display_name = interface_devices[i].address;

        if (virDomainInterfaceStats (interface_devices[i].dom,
                    interface_devices[i].path,
                    &stats, sizeof stats) != 0)
            continue;

	if ((stats.rx_bytes != -1) && (stats.tx_bytes != -1))
	    submit_derive2 ("if_octets",
		    (derive_t) stats.rx_bytes, (derive_t) stats.tx_bytes,
		    interface_devices[i].dom, display_name);

	if ((stats.rx_packets != -1) && (stats.tx_packets != -1))
	    submit_derive2 ("if_packets",
		    (derive_t) stats.rx_packets, (derive_t) stats.tx_packets,
		    interface_devices[i].dom, display_name);

	if ((stats.rx_errs != -1) && (stats.tx_errs != -1))
	    submit_derive2 ("if_errors",
		    (derive_t) stats.rx_errs, (derive_t) stats.tx_errs,
		    interface_devices[i].dom, display_name);

	if ((stats.rx_drop != -1) && (stats.tx_drop != -1))
	    submit_derive2 ("if_dropped",
		    (derive_t) stats.rx_drop, (derive_t) stats.tx_drop,
		    interface_devices[i].dom, display_name);
    } /* for (nr_interface_devices) */

    return 0;
}

static int
refresh_lists (void)
{
    int n;

    n = virConnectNumOfDomains (conn);
    if (n < 0) {
        VIRT_ERROR (conn, "reading number of domains");
        return -1;
    }

    if (n > 0) {
        int i;
        int *domids;

        /* Get list of domains. */
        domids = malloc (sizeof (int) * n);
        if (domids == 0) {
            ERROR ("libvirt plugin: malloc failed.");
            return -1;
        }

        n = virConnectListDomains (conn, domids, n);
        if (n < 0) {
            VIRT_ERROR (conn, "reading list of domains");
            sfree (domids);
            return -1;
        }

        free_block_devices ();
        free_interface_devices ();
        free_domains ();

        /* Fetch each domain and add it to the list, unless ignore. */
        for (i = 0; i < n; ++i) {
            virDomainPtr dom = NULL;
            const char *name;
            char *xml = NULL;
            xmlDocPtr xml_doc = NULL;
            xmlXPathContextPtr xpath_ctx = NULL;
            xmlXPathObjectPtr xpath_obj = NULL;
            int j;

            dom = virDomainLookupByID (conn, domids[i]);
            if (dom == NULL) {
                VIRT_ERROR (conn, "virDomainLookupByID");
                /* Could be that the domain went away -- ignore it anyway. */
                continue;
            }

            name = virDomainGetName (dom);
            if (name == NULL) {
                VIRT_ERROR (conn, "virDomainGetName");
                goto cont;
            }

            if (il_domains && ignorelist_match (il_domains, name) != 0)
                goto cont;

            if (add_domain (dom) < 0) {
                ERROR ("libvirt plugin: malloc failed.");
                goto cont;
            }

            /* Get a list of devices for this domain. */
            xml = virDomainGetXMLDesc (dom, 0);
            if (!xml) {
                VIRT_ERROR (conn, "virDomainGetXMLDesc");
                goto cont;
            }

            /* Yuck, XML.  Parse out the devices. */
            xml_doc = xmlReadDoc ((xmlChar *) xml, NULL, NULL, XML_PARSE_NONET);
            if (xml_doc == NULL) {
                VIRT_ERROR (conn, "xmlReadDoc");
                goto cont;
            }

            xpath_ctx = xmlXPathNewContext (xml_doc);

            /* Block devices. */
            xpath_obj = xmlXPathEval
                ((xmlChar *) "/domain/devices/disk/target[@dev]",
                 xpath_ctx);
            if (xpath_obj == NULL || xpath_obj->type != XPATH_NODESET ||
                xpath_obj->nodesetval == NULL)
                goto cont;

            for (j = 0; j < xpath_obj->nodesetval->nodeNr; ++j) {
                xmlNodePtr node;
                char *path = NULL;

                node = xpath_obj->nodesetval->nodeTab[j];
                if (!node) continue;
                path = (char *) xmlGetProp (node, (xmlChar *) "dev");
                if (!path) continue;

                if (il_block_devices &&
                    ignore_device_match (il_block_devices, name, path) != 0)
                    goto cont2;

                add_block_device (dom, path);
            cont2:
                if (path) xmlFree (path);
            }
            xmlXPathFreeObject (xpath_obj);

            /* Network interfaces. */
            xpath_obj = xmlXPathEval
                ((xmlChar *) "/domain/devices/interface[target[@dev]]",
                 xpath_ctx);
            if (xpath_obj == NULL || xpath_obj->type != XPATH_NODESET ||
                xpath_obj->nodesetval == NULL)
                goto cont;

            xmlNodeSetPtr xml_interfaces = xpath_obj->nodesetval;

            for (j = 0; j < xml_interfaces->nodeNr; ++j) {
                char *path = NULL;
                char *address = NULL;
                xmlNodePtr xml_interface;

                xml_interface = xml_interfaces->nodeTab[j];
                if (!xml_interface) continue;
                xmlNodePtr child = NULL;

                for (child = xml_interface->children; child; child = child->next) {
                    if (child->type != XML_ELEMENT_NODE) continue;

                    if (xmlStrEqual(child->name, (const xmlChar *) "target")) {
                        path = (char *) xmlGetProp (child, (const xmlChar *) "dev");
                        if (!path) continue;
                    } else if (xmlStrEqual(child->name, (const xmlChar *) "mac")) {
                        address = (char *) xmlGetProp (child, (const xmlChar *) "address");
                        if (!address) continue;
                    }
                }

                if (il_interface_devices &&
                    (ignore_device_match (il_interface_devices, name, path) != 0 ||
                     ignore_device_match (il_interface_devices, name, address) != 0))
                    goto cont3;

                add_interface_device (dom, path, address);
                cont3:
                    if (path) xmlFree (path);
                    if (address) xmlFree (address);
            }

        cont:
            if (xpath_obj) xmlXPathFreeObject (xpath_obj);
            if (xpath_ctx) xmlXPathFreeContext (xpath_ctx);
            if (xml_doc) xmlFreeDoc (xml_doc);
            sfree (xml);
        }

        sfree (domids);
    }

    return 0;
}

static void
free_domains ()
{
    int i;

    if (domains) {
        for (i = 0; i < nr_domains; ++i)
            virDomainFree (domains[i]);
        sfree (domains);
    }
    domains = NULL;
    nr_domains = 0;
}

static int
add_domain (virDomainPtr dom)
{
    virDomainPtr *new_ptr;
    int new_size = sizeof (domains[0]) * (nr_domains+1);

    if (domains)
        new_ptr = realloc (domains, new_size);
    else
        new_ptr = malloc (new_size);

    if (new_ptr == NULL)
        return -1;

    domains = new_ptr;
    domains[nr_domains] = dom;
    return nr_domains++;
}

static void
free_block_devices ()
{
    int i;

    if (block_devices) {
        for (i = 0; i < nr_block_devices; ++i)
            sfree (block_devices[i].path);
        sfree (block_devices);
    }
    block_devices = NULL;
    nr_block_devices = 0;
}

static int
add_block_device (virDomainPtr dom, const char *path)
{
    struct block_device *new_ptr;
    int new_size = sizeof (block_devices[0]) * (nr_block_devices+1);
    char *path_copy;

    path_copy = strdup (path);
    if (!path_copy)
        return -1;

    if (block_devices)
        new_ptr = realloc (block_devices, new_size);
    else
        new_ptr = malloc (new_size);

    if (new_ptr == NULL) {
        sfree (path_copy);
        return -1;
    }
    block_devices = new_ptr;
    block_devices[nr_block_devices].dom = dom;
    block_devices[nr_block_devices].path = path_copy;
    return nr_block_devices++;
}

static void
free_interface_devices ()
{
    int i;

    if (interface_devices) {
        for (i = 0; i < nr_interface_devices; ++i) {
            sfree (interface_devices[i].path);
            sfree (interface_devices[i].address);
        }
        sfree (interface_devices);
    }
    interface_devices = NULL;
    nr_interface_devices = 0;
}

static int
add_interface_device (virDomainPtr dom, const char *path, const char *address)
{
    struct interface_device *new_ptr;
    int new_size = sizeof (interface_devices[0]) * (nr_interface_devices+1);
    char *path_copy, *address_copy;

    path_copy = strdup (path);
    if (!path_copy) return -1;

    address_copy = strdup (address);
    if (!address_copy) return -1;

    if (interface_devices)
        new_ptr = realloc (interface_devices, new_size);
    else
        new_ptr = malloc (new_size);

    if (new_ptr == NULL) {
        sfree (path_copy);
        sfree (address_copy);
        return -1;
    }
    interface_devices = new_ptr;
    interface_devices[nr_interface_devices].dom = dom;
    interface_devices[nr_interface_devices].path = path_copy;
    interface_devices[nr_interface_devices].address = address_copy;
    return nr_interface_devices++;
}

static int
ignore_device_match (ignorelist_t *il, const char *domname, const char *devpath)
{
    char *name;
    int n, r;

    n = sizeof (char) * (strlen (domname) + strlen (devpath) + 2);
    name = malloc (n);
    if (name == NULL) {
        ERROR ("libvirt plugin: malloc failed.");
        return 0;
    }
    ssnprintf (name, n, "%s:%s", domname, devpath);
    r = ignorelist_match (il, name);
    sfree (name);
    return r;
}

static int
lv_shutdown (void)
{
    free_block_devices ();
    free_interface_devices ();
    free_domains ();

    if (conn != NULL)
	virConnectClose (conn);
    conn = NULL;

    ignorelist_free (il_domains);
    il_domains = NULL;
    ignorelist_free (il_block_devices);
    il_block_devices = NULL;
    ignorelist_free (il_interface_devices);
    il_interface_devices = NULL;

    return 0;
}

void
module_register (void)
{
    plugin_register_config ("libvirt",
	    lv_config,
	    config_keys, NR_CONFIG_KEYS);
    plugin_register_init ("libvirt", lv_init);
    plugin_register_read ("libvirt", lv_read);
    plugin_register_shutdown ("libvirt", lv_shutdown);
}

/*
 * vim: shiftwidth=4 tabstop=8 softtabstop=4 expandtab fdm=marker
 */
