/**
 * collectd - src/infiniband.c
 * Copyright (C) 2014 Battelle Memorial Institute
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
 *   Ken Schmidt <kenneth.schmidt at pnnl.gov>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "utils_llist.h"
#include "utils_ignorelist.h"
#define ntohll
#define htonll
#include <infiniband/mad.h>
#include <infiniband/umad.h>

#define SYSFSDIR "/sys/class/infiniband/"

static const char *config_keys[] =
{
	"Ports",
	"IgnoreSelectedPorts"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static ignorelist_t *ports_ignore = NULL;

static int infiniband_config (const char *key, const char *value) {
	if (ports_ignore == NULL)
		ports_ignore = ignorelist_create(0);
	if (ports_ignore == NULL)
		return (1);

	if (strcasecmp("Ports",key) == 0)
	{
		ignorelist_add(ports_ignore,value);
	}
	else if (strcasecmp("IgnoreSelectedPorts",key) == 0)
	{
		int invert = 0;
		if (IS_TRUE(value))
			invert = 1;
		ignorelist_set_invert(ports_ignore,invert);
	}
	else
	{
		return (-1);
	}
	return (0);
}

static char *counterMap[][3] = {
	{ "excessive_buffer_overrun_errors", "ib_buffer_overruns", "value" },
	{ "link_downed", "ib_link_downed", "value" },
	{ "link_error_recovery", "ib_link_error_recovery", "value"},
	{ "local_link_integrity_errors", "ib_link_integrity_errors", "value"},
	{ "port_rcv_constraint_errors", "ib_constraint_errors", "rx"},
	{ "port_rcv_data", "ib_data", "rx" },
	{ "port_rcv_errors", "ib_errors", "rx" },
	{ "port_rcv_packets", "ib_packets", "rx" },
	{ "port_rcv_remote_physical_errors", "ib_remote_phys_errors", "rx" },
	{ "port_rcv_switch_relay_errors", "ib_switch_relay_errors", "rx" },
	{ "port_xmit_constraint_errors", "ib_constraint_errors", "tx" },
	{ "port_xmit_data", "ib_data", "tx" },
	{ "port_xmit_discards", "ib_discards", "tx" },
	{ "port_xmit_packets", "ib_packets", "tx" },
	{ "port_xmit_wait", "ib_wait", "tx" },
	{ "symbol_error", "ib_symbol_errors", "value"},
	{ "VL15_dropped", "ib_VL15_dropped", "value"},
};

static int resolve_self(char *ca_name, uint8_t ca_port, ib_portid_t *portid, int *portnum)
{
	umad_port_t port;
	int rc;

	if (!(portid || portnum))
		return (-1);

	if ((rc = umad_get_port(ca_name, ca_port, &port)) < 0)
		return rc;

	if (portid) {
		memset(portid, 0, sizeof(*portid));
		portid->lid = port.base_lid;
		portid->sl = port.sm_sl;
	}
	if (portnum)
		*portnum = port.portnum;

	umad_release_port(&port);

	return 0;
}

static void reset_counters(char *ibd_ca, int ibd_ca_port)
{
	uint8_t pc[1024];
	ib_portid_t portid;
	int mgmt_classes[] = { IB_SMI_CLASS, IB_SA_CLASS, IB_PERFORMANCE_CLASS };
	int mask = 0xffff;
	int timeout = 1000;
	int port;
	struct ibmad_port *srcport;

	memset(&portid, 0, sizeof(portid));
	srcport = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, sizeof(mgmt_classes)/sizeof(int));

	if(resolve_self(ibd_ca, ibd_ca_port, &portid, &port) < 0)
		ERROR("can't resolve self port %s:%d", ibd_ca, ibd_ca_port);

	memset(pc, 0, sizeof(pc));
	if(!performance_reset_via(pc, &portid, port, mask, timeout, IB_GSI_PORT_COUNTERS, srcport))
		ERROR("error doing infiniband performance counter reset");
	memset(pc, 0, sizeof(pc));
	if(!performance_reset_via(pc, &portid, port, mask, timeout, IB_GSI_PORT_COUNTERS_EXT, srcport))
		ERROR("error doing infiniband extended performance counter reset");

	mad_rpc_close_port(srcport);
}

static int ib_walk_counters(const char *dir, const char *counter, void *typesList)
{
	char counterFileName[256];
	char counterValue[256];
	value_t *value;
	llentry_t *cur;
	llist_t *typeInstanceList;
	int i;

	ssnprintf(counterFileName, sizeof(counterFileName), "%s/%s", dir, counter);
	read_file_contents(counterFileName, counterValue, sizeof(counterValue));

	//the counters seem to always pad binary data after the newline in the file
	strtok(counterValue, "\n");

	value = (value_t *)malloc(sizeof(value_t));
	if(parse_value(counterValue, value, DS_TYPE_COUNTER) == -1) {
		free(value);
		return 2;
	}
	for(i=0; i < sizeof(counterMap)/3/sizeof(char *); i++) {
		if(! strcmp(counterMap[i][0], counter)) {
			if((cur = llist_search(typesList, counterMap[i][1])) == NULL) {
				typeInstanceList = llist_create();
				cur = llentry_create(counterMap[i][1], (void *)typeInstanceList);
				llist_append(typesList, cur);
			} else {
				typeInstanceList = (llist_t *)cur->value;
			}
			cur = llentry_create(counterMap[i][2], (void *)value);
			llist_append(typeInstanceList, cur);
			break;
		}
	}
	return 0;
}

static int ib_walk_ports(const char *dir, const char *port, void *adapter)
{
	char portName[32];
	char counterDir[256];
	llist_t *typesList = llist_create();
	llentry_t *valEntry;
	int res;
	value_t *value;

	ssnprintf(portName, sizeof(portName), "%s:%s", (const char *)adapter, port);
	if (ignorelist_match(ports_ignore, portName) != 0)
		return 0;

	ssnprintf(counterDir, sizeof(counterDir), "%s/%s/counters", dir, (char *)port);
	res = walk_directory(counterDir, ib_walk_counters, typesList, 0);

	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;
	vl.values = values;

	strncpy(vl.host, hostname_g, sizeof(vl.host));
	strncpy(vl.plugin, "infiniband", sizeof(vl.plugin));
	ssnprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%s:%s", (char *)adapter, port);
	for(valEntry = llist_head(typesList); valEntry; valEntry = valEntry->next) {
		strncpy(vl.type, valEntry->key, sizeof(vl.type));
		vl.values_len = llist_size((llist_t *)valEntry->value);
		if(vl.values_len == 2) {
			value = (value_t *)llist_search(valEntry->value, "tx")->value;
			values[0].counter = value->counter;
			free(value);
			value = (value_t *)llist_search(valEntry->value, "rx")->value;
			values[1].counter = value->counter;
			free(value);
		} else {
			value = (value_t *)llist_head(valEntry->value)->value;
			values[0].counter = value->counter;
			free(value);
		}
		plugin_dispatch_values(&vl);
	}
	reset_counters((char *)adapter, strtol(port, NULL, 10));
	return 0;
}

static int ib_walk_adapters(const char *dir, const char *adapter, void *user_data)
{
	char portsDir[256];
	ssnprintf(portsDir, sizeof(portsDir), "%s/%s/ports", dir, adapter);
	return walk_directory(portsDir, ib_walk_ports, (void *)adapter, 0);
}

static int infiniband_read (void)
{
	return walk_directory (SYSFSDIR, ib_walk_adapters, NULL, 0);
}

void module_register (void)
{
	plugin_register_read ("infiniband", infiniband_read);
	plugin_register_config ("infiniband", infiniband_config, config_keys, config_keys_num);
} /* void module_register */

/*
 * vim: set shiftwidth=8 softtabstop=8 noet textwidth=78 :
 */
