/**
 * collectd - src/ethstat.h
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

#define ETHTOOL_BUSINFO_LEN     32
#define ETHTOOL_GDRVINFO	0x00000003 /* Get driver info. */
#define ETH_GSTRING_LEN		32
#define ETHTOOL_GSTRINGS	0x0000001b /* get specified string set */
#define ETHTOOL_GSTATS          0x0000001d /* get NIC-specific statistics */

enum ethtool_stringset {
        ETH_SS_TEST             = 0,
        ETH_SS_STATS,
};

typedef unsigned long long u64;
typedef unsigned char __u8;
typedef unsigned short __u16;
typedef unsigned int __u32;
typedef unsigned long long  __u64;


static const char *config_keys[] =
{
	"Iface"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static char **ifacelist;
static int ifacenumber = 0;
struct ethtool_drvinfo {
        __u32   cmd;
        char    driver[32];     /* driver short name, "tulip", "eepro100" */
        char    version[32];    /* driver version string */
        char    fw_version[32]; /* firmware version string, if applicable */
        char    bus_info[ETHTOOL_BUSINFO_LEN];  /* Bus info for this IF. */
                                /* For PCI devices, use pci_name(pci_dev). */
        char    reserved1[32];
        char    reserved2[16];
        __u32   n_stats;        /* number of u64's from ETHTOOL_GSTATS */
        __u32   testinfo_len;
        __u32   eedump_len;     /* Size of data from ETHTOOL_GEEPROM (bytes) */
        __u32   regdump_len;    /* Size of data from ETHTOOL_GREGS (bytes) */
};

struct ethtool_gstrings {
        __u32   cmd;            /* ETHTOOL_GSTRINGS */
        __u32   string_set;     /* string set id e.c. ETH_SS_TEST, etc*/
        __u32   len;            /* number of strings in the string set */
        __u8    data[0];
};

struct ethtool_stats {
        __u32   cmd;            /* ETHTOOL_GSTATS */
        __u32   n_stats;        /* number of u64's being returned */
        __u64   data[0];
};



