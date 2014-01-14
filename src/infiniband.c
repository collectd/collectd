/**
 * collectd - src/infinibandc
 * Copyright (C) 2013 Battelle Memorial Institute
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

/*
static int const therm_ids[] = {
	eMicThermalDie, eMicThermalDevMem, eMicThermalFin, eMicThermalFout,
	eMicThermalVccp, eMicThermalVddg, eMicThermalVddq };
static char const * const therm_names[] = {
	"die", "devmem", "fin", "fout",
	"vccp", "vddg", "vddq" };

static const char *config_keys[] =
{
	"Ports",
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int mic_config (const char *key, const char *value) {
	if (temp_ignore == NULL)
		temp_ignore = ignorelist_create(1);
	if (power_ignore == NULL)
		power_ignore = ignorelist_create(1);
	if (temp_ignore == NULL || power_ignore == NULL)
		return (1);

	if (strcasecmp("ShowCPU",key) == 0)
	{
		show_cpu = IS_TRUE(value);
	}
	else if (strcasecmp("ShowCPUCores",key) == 0)
	{
		show_cpu_cores = IS_TRUE(value);
	}
	else if (strcasecmp("ShowTemperatures",key) == 0)
	{
		show_temps = IS_TRUE(value);
	}
	else if (strcasecmp("ShowMemory",key) == 0)
	{
		show_memory = IS_TRUE(value);
	}
	else if (strcasecmp("ShowPower",key) == 0)
	{
		show_power = IS_TRUE(value);
	}
	else if (strcasecmp("Temperature",key) == 0)
	{
		ignorelist_add(temp_ignore,value);
	}
	else if (strcasecmp("IgnoreSelectedTemperature",key) == 0)
	{
		int invert = 1;
		if (IS_TRUE(value))
			invert = 0;
		ignorelist_set_invert(temp_ignore,invert);
	}
	else if (strcasecmp("Power",key) == 0)
	{
		ignorelist_add(power_ignore,value);
	}
	else if (strcasecmp("IgnoreSelectedPower",key) == 0)
	{
		int invert = 1;
		if (IS_TRUE(value))
			invert = 0;
		ignorelist_set_invert(power_ignore,invert);
	}
	else
	{
		return (-1);
	}
	return (0);
}
*/

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
	{ "symbol_error", "symbol_errors", "value"},
	{ "VL15_dropped", "VL15_dropped", "value"},
};

static int walk_counters(const char *dir, const char *counter, void *dirParts)
{
	char *adapter = (char *)dirParts[0];
	char *port = (char *)dirParts[1];
	llist_t *typeList = (llist_t *)dirParts[2];
	FILE *counterFile;
	char counterFileName[256];
	char counterValue[256];
	value_t value;
	llentry_t *cur;
	llist_t *typeInstanceList;

	char *endVal;

	ssnprintf(counterFileName, sizeof(counterFileName), "%s/%s", dir, counter);
	if((countersFile = fopen(counterFileName, "r") == NULL) {
		return 1;
	}
	fread(counterValue, sizeof(char), sizeof(counterValue), countersFile);
	if(parse_value(counterValue, &value, DS_TYPE_DERIVE) != -1) {
		return 2;
	}
	for(i=0; i < sizeof(counterMap)/3/sizeof(char *); i++) {
		if(! strcmp(countermap[i][0], counterfilename)) {
			if((cur = llist_search(typeList, countermap[i]]0])) == NULL) {
				typeInstanceList = llist_create();
				cur = llentry_create(countermap[i][1], (void *)typeInstanceList);
				llist_append(typeInstanceList, cur);
			} else {
				typeInstanceList = (llist_t *)cur->value;
			}
			cur = llentry_create(countermap[i][2], (void *)countervalue);
			llist_append(typeinstancelist, cur);
		}
	}

	plugin_dispatch_values
	return 0;
}

static int walk_ports(const char *dir, const char *port, void *adapter)
{
	char counterDir[256];
	llist_t *typesList = llist_create();
	llentry_t *valEntry;
	llentry_t *valInstEntry;
	int i;
	int res;

	void *dirParts[] = {adapter, port, typesList};
	ssnprintf(countersDir, sizeof(countersDir), "%s/%s/counters", dir, port);
	res = walk_directory(countersDir, walk_counters);

	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;
	vl.values = values;

	strncpy(vl.host, hostname_g, sizeof(vl.host));
	strncpy(vl.plugin, "infiniband", sizeof(vl.plugin));
	ssnprintf(vl.plugin_instance, "%s:%s", adapter, port);
	for(valEntry = llist_head(typesList); valEntry; valEntry = valEntry->next) {
		strncpy(vl.type, valEntry->key, sizeof(vl.type));
		vl.values_len = llist_size((llist *)valEntry->value);
		if(vl.values_len == 2) {
			values[0].derive = (derive_t)llist_search(valEntry->value, "tx")->value;
			values[1].derive = (derive_t)llist_search(valEntry->value, "rx")->value;
		} else {
			values[0].derive = (derive_t)llist_head(valEntry->value)->value;
		}
		plugin_dispatch_values(vl);
	}
}

static int walk_adapters(const char *dir, const char *adapter, void *user_data)
{
	char portsDir[256];
	ssnprintf(portsDir, sizeof(portsDir), "%s/%s/ports", dir, adapter);
	return walk_directory(portsDir, walk_ports, adapter, 0);
}

static int infiniband_read (void)
{
	char *baseDir = "/sys/class/infiniband/";

	return walk_directory (baseDir, walk_adapters NULL, 0);
}

void module_register (void)
{
	//plugin_register_init ("infiniband", infiniband_init);
	plugin_register_read ("infiniband", infiniband_read);
	//plugin_register_config ("mic",mic_config, config_keys, config_keys_num);
} /* void module_register */

/*
 * vim: set shiftwidth=8 softtabstop=8 noet textwidth=78 :
 */
