/**
 * collectd - src/libvirtstats.c
 * Copyright (C) 2006,2007  Red Hat Inc.
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

#include <libvirt/libvirt.h>
#include <libvirt/virterror.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#define LIBVIRTSTATS_DEBUG 0

static const char *config_keys[] = {
    "Connection",

    "RefreshInterval",

    "Domain",
    "BlockDevice",
    "InterfaceDevice",
    "IgnoreSelected",
    NULL
};
#define NR_CONFIG_KEYS ((sizeof config_keys / sizeof config_keys[0]) - 1)

/* Connection. */
static virConnectPtr conn = 0;

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
};

static struct interface_device *interface_devices = NULL;
static int nr_interface_devices = 0;

static void free_interface_devices (void);
static int add_interface_device (virDomainPtr dom, const char *path);

/* Time that we last refreshed. */
static time_t last_refresh = (time_t) 0;

static int refresh_lists (void);

/* Submit functions. */
static void cpu_submit (unsigned long long cpu_time,
                        time_t t,
                        const char *domname, const char *type);
static void vcpu_submit (unsigned long long cpu_time,
                         time_t t,
                         const char *domname, int vcpu_nr, const char *type);
static void disk_submit (long long read, long long write,
                         time_t t,
                         const char *domname, const char *devname,
                         const char *type);
static void if_submit (long long rx, long long tx,
                       time_t t,
                       const char *domname, const char *devname,
                       const char *type);

/* ERROR(...) macro for virterrors. */
#define VIRT_ERROR(conn,s) do {                 \
        virErrorPtr err;                        \
        err = (conn) ? virConnGetLastError ((conn)) : virGetLastError (); \
        if (err) ERROR ("%s: %s", (s), err->message);                   \
    } while(0)

static int
libvirtstats_init (void)
{
    if (virInitialize () == -1)
        return -1;

	return 0;
}

static int
libvirtstats_config (const char *key, const char *value)
{
    if (virInitialize () == -1)
        return 1;

    if (il_domains == NULL)
        il_domains = ignorelist_create (1);
    if (il_block_devices == NULL)
        il_block_devices = ignorelist_create (1);
    if (il_interface_devices == NULL)
        il_interface_devices = ignorelist_create (1);

    if (strcasecmp (key, "Connection") == 0) {
        if (conn != 0) {
            ERROR ("Connection may only be given once in config file");
            return 1;
        }
        conn = virConnectOpenReadOnly (value);
        if (!conn) {
            VIRT_ERROR (NULL, "connection failed");
            return 1;
        }
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
        if (strcasecmp (value, "True") == 0 ||
            strcasecmp (value, "Yes") == 0 ||
            strcasecmp (value, "On") == 0)
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
    }

    /* Unrecognised option. */
    return -1;
}

static int
libvirtstats_read (void)
{
    time_t t;
    int i;

    if (conn == NULL) {
        ERROR ("Not connected.  Use Connection in config file to supply connection URI.  For more information see http://libvirt.org/uri.html");
        return -1;
    }

    time (&t);

    /* Need to refresh domain or device lists? */
    if (last_refresh == (time_t) 0 ||
        (interval > 0 && last_refresh + interval <= t)) {
        if (refresh_lists () == -1) return -1;
        last_refresh = t;
    }

#if LIBVIRTSTATS_DEBUG
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
        const char *name;
        virDomainInfo info;
        virVcpuInfoPtr vinfo = NULL;
        int j;

        name = virDomainGetName (domains[i]);
        if (name == NULL) continue;

        if (virDomainGetInfo (domains[i], &info) == -1) continue;

        cpu_submit (info.cpuTime, t, name, "virt_cpu_total");

        vinfo = malloc (info.nrVirtCpu * sizeof vinfo[0]);
        if (vinfo == NULL) {
            ERROR ("malloc: %s", strerror (errno));
            continue;
        }

        if (virDomainGetVcpus (domains[i], vinfo, info.nrVirtCpu,
                               NULL, 0) == -1) {
            free (vinfo);
            continue;
        }

        for (j = 0; j < info.nrVirtCpu; ++j)
            vcpu_submit (vinfo[j].cpuTime,
                         t, name, vinfo[j].number, "virt_vcpu");

        free (vinfo);
    }

    /* Get block device stats for each domain. */
    for (i = 0; i < nr_block_devices; ++i) {
        const char *name;
        struct _virDomainBlockStats stats;

        name = virDomainGetName (block_devices[i].dom);
        if (name == NULL) continue;

        if (virDomainBlockStats (block_devices[i].dom, block_devices[i].path,
                                 &stats, sizeof stats) == -1)
            continue;

        disk_submit (stats.rd_req, stats.wr_req,
                     t, name, block_devices[i].path,
                     "disk_ops");
        disk_submit (stats.rd_bytes, stats.wr_bytes,
                     t, name, block_devices[i].path,
                     "disk_octets");
    }

    /* Get interface stats for each domain. */
    for (i = 0; i < nr_interface_devices; ++i) {
        const char *name;
        struct _virDomainInterfaceStats stats;

        name = virDomainGetName (interface_devices[i].dom);
        if (name == NULL) continue;

        if (virDomainInterfaceStats (interface_devices[i].dom,
                                     interface_devices[i].path,
                                     &stats, sizeof stats) == -1)
            continue;

        if_submit (stats.rx_bytes, stats.tx_bytes,
                   t, name, interface_devices[i].path,
                   "if_octets");
        if_submit (stats.rx_packets, stats.tx_packets,
                   t, name, interface_devices[i].path,
                   "if_packets");
        if_submit (stats.rx_errs, stats.tx_errs,
                   t, name, interface_devices[i].path,
                   "if_errors");
        if_submit (stats.rx_drop, stats.tx_drop,
                   t, name, interface_devices[i].path,
                   "if_dropped");
    }

    return 0;
}

static int
refresh_lists (void)
{
    int n;

    n = virConnectNumOfDomains (conn);
    if (n == -1) {
        VIRT_ERROR (conn, "reading number of domains");
        return -1;
    }

    if (n > 0) {
        int i;
        int *domids;

        /* Get list of domains. */
        domids = malloc (sizeof (int) * n);
        if (domids == 0) {
            ERROR ("malloc failed: %s", strerror (errno));
            return -1;
        }

        n = virConnectListDomains (conn, domids, n);
        if (n == -1) {
            VIRT_ERROR (conn, "reading list of domains");
            free (domids);
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

            if (add_domain (dom) == -1) {
                ERROR ("malloc: %s", strerror (errno));
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
                ((xmlChar *) "/domain/devices/interface/target[@dev]",
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

                if (il_interface_devices &&
                    ignore_device_match (il_interface_devices, name, path) != 0)
                    goto cont3;

                add_interface_device (dom, path);
            cont3:
                if (path) xmlFree (path);
            }

        cont:
            if (xpath_obj) xmlXPathFreeObject (xpath_obj);
            if (xpath_ctx) xmlXPathFreeContext (xpath_ctx);
            if (xml_doc) xmlFreeDoc (xml_doc);
            if (xml) free (xml);
        }

        free (domids);
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
        free (domains);
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

    if (new_ptr == NULL) return -1;
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
            free (block_devices[i].path);
        free (block_devices);
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
    if (!path_copy) return -1;

    if (block_devices)
        new_ptr = realloc (block_devices, new_size);
    else
        new_ptr = malloc (new_size);

    if (new_ptr == NULL) {
        free (path_copy);
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
        for (i = 0; i < nr_interface_devices; ++i)
            free (interface_devices[i].path);
        free (interface_devices);
    }
    interface_devices = NULL;
    nr_interface_devices = 0;
}

static int
add_interface_device (virDomainPtr dom, const char *path)
{
    struct interface_device *new_ptr;
    int new_size = sizeof (interface_devices[0]) * (nr_interface_devices+1);
    char *path_copy;

    path_copy = strdup (path);
    if (!path_copy) return -1;

    if (interface_devices)
        new_ptr = realloc (interface_devices, new_size);
    else
        new_ptr = malloc (new_size);

    if (new_ptr == NULL) {
        free (path_copy);
        return -1;
    }
    interface_devices = new_ptr;
    interface_devices[nr_interface_devices].dom = dom;
    interface_devices[nr_interface_devices].path = path_copy;
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
        ERROR ("malloc: %s", strerror (errno));
        return 0;
    }
    snprintf (name, n, "%s:%s", domname, devpath);
    r = ignorelist_match (il, name);
    free (name);
    return r;
}

static void
cpu_submit (unsigned long long cpu_time,
            time_t t,
            const char *domname, const char *type)
{
    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;

    values[0].counter = cpu_time;

    vl.values = values;
    vl.values_len = 1;
    vl.time = t;
    vl.interval = interval_g;
    strncpy (vl.plugin, "libvirtstats", DATA_MAX_NAME_LEN);
    strncpy (vl.host, domname, DATA_MAX_NAME_LEN);
    /*strncpy (vl.type_instance, ?, DATA_MAX_NAME_LEN);*/

    plugin_dispatch_values (type, &vl);
}

static void
vcpu_submit (unsigned long long cpu_time,
             time_t t,
             const char *domname, int vcpu_nr, const char *type)
{
    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;

    values[0].counter = cpu_time;

    vl.values = values;
    vl.values_len = 1;
    vl.time = t;
    vl.interval = interval_g;
    strncpy (vl.plugin, "libvirtstats", DATA_MAX_NAME_LEN);
    strncpy (vl.host, domname, DATA_MAX_NAME_LEN);
    snprintf (vl.type_instance, DATA_MAX_NAME_LEN, "%d", vcpu_nr);

    plugin_dispatch_values (type, &vl);
}

static void
disk_submit (long long read, long long write,
             time_t t,
             const char *domname, const char *devname,
             const char *type)
{
    value_t values[2];
    value_list_t vl = VALUE_LIST_INIT;

    values[0].counter = read >= 0 ? (unsigned long long) read : 0;
    values[1].counter = write >= 0 ? (unsigned long long) write : 0;

    vl.values = values;
    vl.values_len = 2;
    vl.time = t;
    vl.interval = interval_g;
    strncpy (vl.plugin, "libvirtstats", DATA_MAX_NAME_LEN);
    strncpy (vl.host, domname, DATA_MAX_NAME_LEN);
    strncpy (vl.type_instance, devname, DATA_MAX_NAME_LEN);

    plugin_dispatch_values (type, &vl);
}

static void
if_submit (long long rx, long long tx,
           time_t t,
           const char *domname, const char *devname,
           const char *type)
{
    value_t values[2];
    value_list_t vl = VALUE_LIST_INIT;

    values[0].counter = rx >= 0 ? (unsigned long long) rx : 0;
    values[1].counter = tx >= 0 ? (unsigned long long) tx : 0;

    vl.values = values;
    vl.values_len = 2;
    vl.time = t;
    vl.interval = interval_g;
    strncpy (vl.plugin, "libvirtstats", DATA_MAX_NAME_LEN);
    strncpy (vl.host, domname, DATA_MAX_NAME_LEN);
    strncpy (vl.type_instance, devname, DATA_MAX_NAME_LEN);

    plugin_dispatch_values (type, &vl);
}

static int
libvirtstats_shutdown (void)
{
    free_block_devices ();
    free_interface_devices ();
    free_domains ();

    if (conn) virConnectClose (conn);
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
	plugin_register_config ("libvirtstats",
                            libvirtstats_config,
                            config_keys, NR_CONFIG_KEYS);
    plugin_register_init ("libvirtstats", libvirtstats_init);
	plugin_register_read ("libvirtstats", libvirtstats_read);
	plugin_register_shutdown ("libvirtstats", libvirtstats_shutdown);
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
