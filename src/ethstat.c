/**
 * collectd - src/ethstat.c
 * Copyright (C) 2011       Cyril Feraudet
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *   Cyril Feraudet <cyril at feraudet.com>
 **/

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include <sys/types.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <net/if.h>
#include <linux/sockios.h> 
#include "ethstat.h"

# if KERNEL_LINUX


static int ethstat_config (const char *key, const char *value)
{
	if (strcasecmp (key, "Iface") == 0) {
		ifacelist[ifacenumber] = malloc(strlen(value) + 1);
		strcpy(ifacelist[ifacenumber++], value);
		INFO("ethstat: Registred iface %s", value);
	}
	return (0);
}


static void ethstat_submit_value (char *devname, char *counter, unsigned long long value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "ethstat", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, devname, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, "derive", sizeof (vl.type));
	sstrncpy (vl.type_instance, counter, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}


static int getstats(char *devname, struct ifreq *ifr) {
        int fd;
	struct ethtool_drvinfo drvinfo;
	struct ethtool_gstrings *strings;
	struct ethtool_stats *stats;
	unsigned int n_stats, sz_str, sz_stats, i;
	int err;


	fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
                ERROR("ethstat - %s : Cannot get control socket", devname);
                return 1;
        }

        drvinfo.cmd = ETHTOOL_GDRVINFO;
        ifr->ifr_data = (caddr_t)&drvinfo;
        err = ioctl(fd, SIOCETHTOOL, ifr);
        if (err < 0) {
                ERROR("ethstat - %s : Cannot get driver information", devname);
		close(fd);
                return 1;
        }


        n_stats = drvinfo.n_stats;
        if (n_stats < 1) {
                ERROR("ethstat - %s : No stats available", devname);
		close(fd);
                return 1;
        }

        sz_str = n_stats * ETH_GSTRING_LEN;
        sz_stats = n_stats * sizeof(u64);

        strings = calloc(1, sz_str + sizeof(struct ethtool_gstrings));
        stats = calloc(1, sz_stats + sizeof(struct ethtool_stats));
        if (!strings || !stats) {
                ERROR("ethstat - %s No memory available", devname);
		close(fd);
                return 1;
        }

        strings->cmd = ETHTOOL_GSTRINGS;
        strings->string_set = ETH_SS_STATS;
        strings->len = n_stats;
        ifr->ifr_data = (caddr_t) strings;
        err = ioctl(fd, SIOCETHTOOL, ifr);
        if (err < 0) {
                ERROR("ethstat - %s : Cannot get stats strings information", devname);
                free(strings);
                free(stats);
		close(fd);
                return 96;
        }

        stats->cmd = ETHTOOL_GSTATS;
        stats->n_stats = n_stats;
        ifr->ifr_data = (caddr_t) stats;
        err = ioctl(fd, SIOCETHTOOL, ifr);
        if (err < 0) {
                ERROR("ethstat - %s : Cannot get stats information", devname);
                free(strings);
                free(stats);
		close(fd);
                return 97;
        }

        for (i = 0; i < n_stats; i++) {
                DEBUG("ethstat - %s : %s: %llu",
			devname,
                        &strings->data[i * ETH_GSTRING_LEN],
                        stats->data[i]);
		ethstat_submit_value (
			devname,
			(char*)&strings->data[i * ETH_GSTRING_LEN],
			stats->data[i]);
        }
        free(strings);
        free(stats);

	close(fd);

	return 0;
}


static int ethstat_read(void)
{
	struct ifreq ifr;
	int i;

	for (i = 0 ; i < ifacenumber ; i++) {
		DEBUG("ethstat - Processing : %s\n", ifacelist[i]);
		memset(&ifr, 0, sizeof(ifr));
        	strcpy(ifr.ifr_name, ifacelist[i]);
		getstats(ifacelist[i], &ifr);
	}
	return 0;
}

void module_register (void)
{
	ifacelist = malloc(sizeof(char*));
	plugin_register_config ("ethstat", ethstat_config,
			config_keys, config_keys_num);
	plugin_register_read ("ethstat", ethstat_read);
}

# endif
