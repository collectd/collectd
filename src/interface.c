/**
 * collectd - src/interface.c
 * Copyright (C) 2005-2010  Florian octo Forster
 * Copyright (C) 2009       Manuel Sanmartin
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
 *   Florian octo Forster <octo at collectd.org>
 *   Sune Marcher <sm at flork.dk>
 *   Manuel Sanmartin
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_ignorelist.h"

#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif

/* One cannot include both. This sucks. */
#if HAVE_LINUX_IF_H
#  include <linux/if.h>
#elif HAVE_NET_IF_H
#  include <net/if.h>
#endif

#if HAVE_LINUX_NETDEVICE_H
#  include <linux/netdevice.h>
#endif
#if HAVE_IFADDRS_H
#  include <ifaddrs.h>
#endif

#if HAVE_STATGRAB_H
# include <statgrab.h>
#endif

#if HAVE_PERFSTAT
# include <sys/protosw.h>
# include <libperfstat.h>
#endif

#if HAVE_ZONE_H
# include <zone.h>
#endif

/*
 * Various people have reported problems with `getifaddrs' and varying versions
 * of `glibc'. That's why it's disabled by default. Since more statistics are
 * available this way one may enable it using the `--enable-getifaddrs' option
 * of the configure script. -octo
 */
#if KERNEL_LINUX
# if !COLLECT_GETIFADDRS
#  undef HAVE_GETIFADDRS
# endif /* !COLLECT_GETIFADDRS */
#endif /* KERNEL_LINUX */

#if HAVE_PERFSTAT
static perfstat_netinterface_t *ifstat;
static int nif;
static int pnif;
#endif /* HAVE_PERFSTAT */

#if !HAVE_GETIFADDRS && !KERNEL_LINUX && !HAVE_LIBKSTAT && !HAVE_LIBSTATGRAB && !HAVE_PERFSTAT
# error "No applicable input method."
#endif

/*
 * (Module-)Global variables
 */
static const char *config_keys[] =
{
	"Interface",
	"IgnoreSelected",
	NULL
};
static int config_keys_num = 2;

static ignorelist_t *ignorelist = NULL;

#ifdef HAVE_LIBKSTAT
extern kstat_ctl_t *kc;
static kstat_set_t kstats;
#endif /* HAVE_LIBKSTAT */

static int interface_config (const char *key, const char *value)
{
	if (ignorelist == NULL)
		ignorelist = ignorelist_create (/* invert = */ 1);

	if (strcasecmp (key, "Interface") == 0)
	{
		ignorelist_add (ignorelist, value);
	}
	else if (strcasecmp (key, "IgnoreSelected") == 0)
	{
		int invert = 1;
		if (IS_TRUE (value))
			invert = 0;
		ignorelist_set_invert (ignorelist, invert);
	}
	else
	{
		return (-1);
	}

	return (0);
}

#if HAVE_LIBKSTAT
static void resolve_zonename (int id, char *buf, int bufsize)
{
#if HAVE_ZONE_H
	if (getzonenamebyid (id, buf, bufsize) >= 0)
	{
		/* null-terminate for safety */
		buf[bufsize - 1] = 0;
		return;
	}
	else
	{
		WARNING ("Failed to resolve zoneid %d: %s", id, strerror (errno));
	}
#endif

	ssnprintf (buf, bufsize, "zone%d", id);
}

static int kstat_sol11_module (const char *module)
{
	/* On Solaris 11, we're interested in kstats from two modules.
	 * The "link" module covers physical and virtual datalink
	 * interfaces, i.e. everything that can be seen via the
	 * "dladm" command.
	 * The "ipmp" module covers IPMP interfaces.
	 */
	if (strcmp (module, "link") == 0)
		return (0);
	if (strcmp (module, "ipmp") == 0)
		return (0);

	return (-1);
}

static int kstat_filter (const kstat_info_t *info)
{
	if (kstat_sol11_module (info->module) == 0)
		return (0);

	/* Solaris 10 and older: Interesting kstats have an interface name
	 * in the name field, which consists of the module name plus an
	 * instance number. These are not found on Solaris 11. */
	int modlen = strlen (info->module);
	if (strncmp (info->module, info->name, modlen) == 0
			&& isdigit (info->name[modlen]))
		return (0);

	/* Anything else can be thrown away. */
	return (-1);
}


static int interface_init (void)
{
	if (kstat_set_init (&kstats) != 0)
		return (-1);

	static kstat_filter_t filter = KSTAT_FILTER_INIT;
	filter.class = "net";
	filter.filter_func = kstat_filter;
	plugin_register_kstat_set ("interface", &kstats, &filter);

	return (0);
} /* int interface_init */
#endif /* HAVE_LIBKSTAT */

static void if_submit (const char *dev, const char *type,
		derive_t rx,
		derive_t tx)
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	if (ignorelist_match (ignorelist, dev) != 0)
		return;

	values[0].derive = rx;
	values[1].derive = tx;

	vl.values = values;
	vl.values_len = 2;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "interface", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, dev, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));

	plugin_dispatch_values (&vl);
} /* void if_submit */

static int interface_read (void)
{
#if HAVE_GETIFADDRS
	struct ifaddrs *if_list;
	struct ifaddrs *if_ptr;

/* Darin/Mac OS X and possible other *BSDs */
#if HAVE_STRUCT_IF_DATA
#  define IFA_DATA if_data
#  define IFA_RX_BYTES ifi_ibytes
#  define IFA_TX_BYTES ifi_obytes
#  define IFA_RX_PACKT ifi_ipackets
#  define IFA_TX_PACKT ifi_opackets
#  define IFA_RX_ERROR ifi_ierrors
#  define IFA_TX_ERROR ifi_oerrors
/* #endif HAVE_STRUCT_IF_DATA */

#elif HAVE_STRUCT_NET_DEVICE_STATS
#  define IFA_DATA net_device_stats
#  define IFA_RX_BYTES rx_bytes
#  define IFA_TX_BYTES tx_bytes
#  define IFA_RX_PACKT rx_packets
#  define IFA_TX_PACKT tx_packets
#  define IFA_RX_ERROR rx_errors
#  define IFA_TX_ERROR tx_errors
#else
#  error "No suitable type for `struct ifaddrs->ifa_data' found."
#endif

	struct IFA_DATA *if_data;

	if (getifaddrs (&if_list) != 0)
		return (-1);

	for (if_ptr = if_list; if_ptr != NULL; if_ptr = if_ptr->ifa_next)
	{
		if (if_ptr->ifa_addr != NULL && if_ptr->ifa_addr->sa_family == AF_LINK) {
			if_data = (struct IFA_DATA *) if_ptr->ifa_data;

			if_submit (if_ptr->ifa_name, "if_octets",
				if_data->IFA_RX_BYTES,
				if_data->IFA_TX_BYTES);
			if_submit (if_ptr->ifa_name, "if_packets",
				if_data->IFA_RX_PACKT,
				if_data->IFA_TX_PACKT);
			if_submit (if_ptr->ifa_name, "if_errors",
				if_data->IFA_RX_ERROR,
				if_data->IFA_TX_ERROR);
		}
	}

	freeifaddrs (if_list);
/* #endif HAVE_GETIFADDRS */

#elif KERNEL_LINUX
	FILE *fh;
	char buffer[1024];
	derive_t incoming, outgoing;
	char *device;

	char *dummy;
	char *fields[16];
	int numfields;

	if ((fh = fopen ("/proc/net/dev", "r")) == NULL)
	{
		char errbuf[1024];
		WARNING ("interface plugin: fopen: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	while (fgets (buffer, 1024, fh) != NULL)
	{
		if (!(dummy = strchr(buffer, ':')))
			continue;
		dummy[0] = '\0';
		dummy++;

		device = buffer;
		while (device[0] == ' ')
			device++;

		if (device[0] == '\0')
			continue;

		numfields = strsplit (dummy, fields, 16);

		if (numfields < 11)
			continue;

		incoming = atoll (fields[0]);
		outgoing = atoll (fields[8]);
		if_submit (device, "if_octets", incoming, outgoing);

		incoming = atoll (fields[1]);
		outgoing = atoll (fields[9]);
		if_submit (device, "if_packets", incoming, outgoing);

		incoming = atoll (fields[2]);
		outgoing = atoll (fields[10]);
		if_submit (device, "if_errors", incoming, outgoing);
	}

	fclose (fh);
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
	int i;
	derive_t rx;
	derive_t tx;

	if (kc == NULL)
		return (-1);

	for (i = 0; i < kstats.len; i++)
	{
		kstat_t *ks = kstats.items[i].kstat;

		if (kstat_read (kc, ks, NULL) == -1)
			continue;

		char *ifname = NULL;
		char ifname_buf[128];
		if (kstat_sol11_module (ks->ks_module) != 0)
		{
			/* We're on Solaris 10 or older. Interface name in ks_name. */
			ifname = ks->ks_name;
		}
		else
		{
			/* Solaris 11. Instance number contains the zone id.
			 * Interface name is in ks_name, but need not be unique
			 * across zones. For the global zone (id 0), we use the
			 * interface name as-is. For non-global zones, prepend
			 * the zone name to the interface name with a '/' as
			 * separator, consistent with the output of "dladm". */
			if (ks->ks_instance == 0)
			{
				ifname = ks->ks_name;
			}
			else
			{
				/* Would be better to use ZONENAME_MAX for the buffer
				 * size, but we might be on an ancient solaris release
				 * that doesn't have zones. */
				char zonename[128];
				resolve_zonename (ks->ks_instance, zonename, sizeof (zonename));
				ssnprintf (ifname_buf, sizeof (ifname_buf),
						"%s/%s", zonename, ks->ks_name);
				ifname = ifname_buf;
			}
		}

		/* try to get 64bit counters */
		rx = get_kstat_value (ks, "rbytes64");
		tx = get_kstat_value (ks, "obytes64");
		/* or fallback to 32bit */
		if (rx == -1LL)
			rx = get_kstat_value (ks, "rbytes");
		if (tx == -1LL)
			tx = get_kstat_value (ks, "obytes");
		if ((rx != -1LL) || (tx != -1LL))
			if_submit (ifname, "if_octets", rx, tx);

		/* try to get 64bit counters */
		rx = get_kstat_value (ks, "ipackets64");
		tx = get_kstat_value (ks, "opackets64");
		/* or fallback to 32bit */
		if (rx == -1LL)
			rx = get_kstat_value (ks, "ipackets");
		if (tx == -1LL)
			tx = get_kstat_value (ks, "opackets");
		if ((rx != -1LL) || (tx != -1LL))
			if_submit (ifname, "if_packets", rx, tx);

		/* no 64bit error counters yet */
		rx = get_kstat_value (ks, "ierrors");
		tx = get_kstat_value (ks, "oerrors");
		if ((rx != -1LL) || (tx != -1LL))
			if_submit (ifname, "if_errors", rx, tx);
	}
/* #endif HAVE_LIBKSTAT */

#elif defined(HAVE_LIBSTATGRAB)
	sg_network_io_stats *ios;
	int i, num;

	ios = sg_get_network_io_stats (&num);

	for (i = 0; i < num; i++)
		if_submit (ios[i].interface_name, "if_octets", ios[i].rx, ios[i].tx);
/* #endif HAVE_LIBSTATGRAB */

#elif defined(HAVE_PERFSTAT)
	perfstat_id_t id;
	int i, ifs;

	if ((nif =  perfstat_netinterface(NULL, NULL, sizeof(perfstat_netinterface_t), 0)) < 0)
	{
		char errbuf[1024];
		WARNING ("interface plugin: perfstat_netinterface: %s",
			sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	if (pnif != nif || ifstat == NULL)
	{
		if (ifstat != NULL)
			free(ifstat);
		ifstat = malloc(nif * sizeof(perfstat_netinterface_t));
	}
	pnif = nif;

	id.name[0]='\0';
	if ((ifs = perfstat_netinterface(&id, ifstat, sizeof(perfstat_netinterface_t), nif)) < 0)
	{
		char errbuf[1024];
		WARNING ("interface plugin: perfstat_netinterface (interfaces=%d): %s",
			nif, sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	for (i = 0; i < ifs; i++)
	{
		if_submit (ifstat[i].name, "if_octets", ifstat[i].ibytes, ifstat[i].obytes);
		if_submit (ifstat[i].name, "if_packets", ifstat[i].ipackets ,ifstat[i].opackets);
		if_submit (ifstat[i].name, "if_errors", ifstat[i].ierrors, ifstat[i].oerrors );
	}
#endif /* HAVE_PERFSTAT */

	return (0);
} /* int interface_read */

void module_register (void)
{
	plugin_register_config ("interface", interface_config,
			config_keys, config_keys_num);
#if HAVE_LIBKSTAT
	plugin_register_init ("interface", interface_init);
#endif
	plugin_register_read ("interface", interface_read);
} /* void module_register */
