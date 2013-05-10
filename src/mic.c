/**
 * collectd - src/mic.c
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
 *   Evan Felix <evan.felix@pnnl.gov>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "utils_ignorelist.h"

#include <MicAccessTypes.h>
#include <MicAccessErrorTypes.h>
#include <MicAccessApi.h>
#include <MicThermalAPI.h>

#define MAX_MICS 32
#define MAX_CORES 256

static MicDeviceOnSystem mics[MAX_MICS];
static U32 num_mics = 0;
static HANDLE mic_handle = NULL;
#define NUM_THERMS 7
static const int therms[NUM_THERMS] = {eMicThermalDie,eMicThermalDevMem,eMicThermalFin,eMicThermalFout,eMicThermalVccp,eMicThermalVddg,eMicThermalVddq};
static const char *therm_names[NUM_THERMS] = {"die","devmem","fin","fout","vccp","vddg","vddq"};

static const char *config_keys[] =
{
	"ShowTotalCPU",
	"ShowPerCPU",
	"ShowTemps",
	"ShowMemory",
	"TempSensor",
	"IgnoreTempSelected",
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static _Bool show_total_cpu = 1;
static _Bool show_per_cpu = 1;
static _Bool show_temps = 1;
static _Bool show_memory = 1;
static ignorelist_t *temp_ignore = NULL;


static int mic_init (void)
{
	U32 ret;
	U32 mic_count;

	if (mic_handle)
		return (0);

	mic_count = (U32) STATIC_ARRAY_SIZE(mics);
	ret = MicInitAPI(&mic_handle,  eTARGET_SCIF_DRIVER, mics, &mic_count);
	if (ret != MIC_ACCESS_API_SUCCESS) {
		ERROR("mic plugin: Problem initializing MicAccessAPI: %s",MicGetErrorString(ret));
	}
	DEBUG("mic plugin: found: %"PRIu32" MIC(s)",mic_count);
	
	if (mic_count<0 || mic_count>=MAX_MICS) {
		ERROR("mic plugin: No Intel MICs in system");
		return (1);
	}
	else {
		num_mics = mic_count;
		return (0);
	}
}

static int mic_config (const char *key, const char *value) {
	if (temp_ignore == NULL)
		temp_ignore = ignorelist_create(1);
	if (temp_ignore == NULL)
		return (1);

	if (strcasecmp("ShowTotalCPU",key) == 0)
	{
		show_total_cpu = IS_TRUE(value);
	}
	else if (strcasecmp("ShowPerCPU",key) == 0)
	{
		show_per_cpu = IS_TRUE(value);
	}
	else if (strcasecmp("ShowTemps",key) == 0)
	{
		show_temps = IS_TRUE(value);
	}
	else if (strcasecmp("ShowMemory",key) == 0)
	{
		show_memory = IS_TRUE(value);
	}
	else if (strcasecmp("TempSensor",key) == 0)
	{
		ignorelist_add(temp_ignore,value);
	}
	else if (strcasecmp("IgnoreTempSelected",key) == 0)
	{
		int invert = 1;
		if (IS_TRUE(value))
			invert = 0;
		ignorelist_set_invert(temp_ignore,invert);
	}
	else
	{
		return (-1);
	}
	return (0);
}

static void mic_submit_memory_use(int micnumber, const char *type_instance, U32 val)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	/* MicAccessAPI reports KB's of memory, adjust for this */ 
	DEBUG("mic plugin: Memory Value Report; %u %lf",val,((gauge_t)val)*1024.0);
	values[0].gauge = ((gauge_t)val)*1024.0;

	vl.values=values;
	vl.values_len=1;

	strncpy (vl.host, hostname_g, sizeof (vl.host));
	strncpy (vl.plugin, "mic", sizeof (vl.plugin));
	ssnprintf (vl.plugin_instance, sizeof (vl.plugin_instance), "%i", micnumber);
	strncpy (vl.type, "memory", sizeof (vl.type));
	strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} 

/* Gather memory Utilization */
static int mic_read_memory(int mic)
{
	U32 ret;
	U32 mem_total,mem_free,mem_bufs;
	
	ret = MicGetMemoryUtilization(mic_handle,&mem_total,&mem_free,&mem_bufs);
	if (ret != MIC_ACCESS_API_SUCCESS) {
		ERROR("mic plugin: Problem getting Memory Utilization: %s",MicGetErrorString(ret));
		return (1);
	}
	mic_submit_memory_use(mic,"free",mem_free);
	mic_submit_memory_use(mic,"used",mem_total-mem_free-mem_bufs);
	mic_submit_memory_use(mic,"buffered",mem_bufs);
	DEBUG("mic plugin: Memory Read: %u %u %u",mem_total,mem_free,mem_bufs);
	return (0);
}

static void mic_submit_temp(int micnumber, const char *type, gauge_t val)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = val;

	vl.values=values;
	vl.values_len=1;

	strncpy (vl.host, hostname_g, sizeof (vl.host));
	strncpy (vl.plugin, "mic", sizeof (vl.plugin));
	ssnprintf (vl.plugin_instance, sizeof (vl.plugin_instance), "%i", micnumber);
	strncpy (vl.type, "temperature", sizeof (vl.type));
	strncpy (vl.type_instance, type, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} 

/* Gather Temperature Information */
static int mic_read_temps(int mic)
{
	int j;
	U32 ret;
	U32 temp_buffer;
	U32 buffer_size = (U32)sizeof(temp_buffer);
	
	for (j=0;j<NUM_THERMS;j++) {
		if (ignorelist_match(temp_ignore,therm_names[j])!=0)
			continue;
		ret = MicGetTemperature(mic_handle,therms[j],&temp_buffer,&buffer_size);
		if (ret != MIC_ACCESS_API_SUCCESS) {
			ERROR("mic plugin: Problem getting Temperature(%d) %s",j,MicGetErrorString(ret));
			return (1);
		}
		mic_submit_temp(mic,therm_names[j],temp_buffer);
	}
	return (0);
}

static void mic_submit_cpu(int micnumber, const char *type_instance, int core, derive_t val)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].derive = val;

	vl.values=values;
	vl.values_len=1;

	strncpy (vl.host, hostname_g, sizeof (vl.host));
	strncpy (vl.plugin, "mic", sizeof (vl.plugin));
	ssnprintf (vl.plugin_instance, sizeof (vl.plugin_instance), "%i", micnumber);
	strncpy (vl.type, "cpu", sizeof (vl.type));
	if (core < 0)
		strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));
	else
		ssnprintf (vl.type_instance, sizeof (vl.type_instance), "%i-%s", core, type_instance);

	plugin_dispatch_values (&vl);
} 

/*Gather CPU Utilization Information */
static int mic_read_cpu(int mic)
{
	U32 ret;
	U32 buffer_size;
	int j;
	MicCoreUtil core_util;
	MicCoreJiff core_jiffs[MAX_CORES];

	buffer_size=MAX_CORES*sizeof(MicCoreJiff);
	ret = MicGetCoreUtilization(mic_handle,&core_util,core_jiffs,&buffer_size);
	if (ret != MIC_ACCESS_API_SUCCESS) {
		ERROR("mic plugin: Problem getting CPU utilization: %s",MicGetErrorString(ret));
		return(0);
	}
	if (show_total_cpu) {
		mic_submit_cpu(mic,"user",-1,core_util.sum.user);
		mic_submit_cpu(mic,"sys",-1,core_util.sum.sys);
		mic_submit_cpu(mic,"nice",-1,core_util.sum.nice);
		mic_submit_cpu(mic,"idle",-1,core_util.sum.idle);
	}
	if (show_per_cpu) {
		for (j=0;j<core_util.core;j++) {
			mic_submit_cpu(mic,"user",j,core_jiffs[j].user);
			mic_submit_cpu(mic,"sys",j,core_jiffs[j].sys);
			mic_submit_cpu(mic,"nice",j,core_jiffs[j].nice);
			mic_submit_cpu(mic,"idle",j,core_jiffs[j].idle);
		}
	}
	return (0);
}

static int mic_read (void)
{
	int i;
	U32 ret;
	int error;

	error=0;
	for (i=0;i<num_mics;i++) {
		ret = MicInitAdapter(&mic_handle,&mics[i]);
		if (ret != MIC_ACCESS_API_SUCCESS) {
			ERROR("mic plugin: Problem initializing MicAdapter: %s",MicGetErrorString(ret));
			error=1;
		}

		if (error == 0 && show_memory)
			error = mic_read_memory(i);

		if (error == 0 && show_temps)
			error = mic_read_temps(i);

		if (error == 0 && (show_total_cpu || show_per_cpu))
			error = mic_read_cpu(i);

		ret = MicCloseAdapter(mic_handle);
		if (ret != MIC_ACCESS_API_SUCCESS) {
			ERROR("mic plugin: Problem closing MicAdapter: %s",MicGetErrorString(ret));
			error=2;
			break;
		}
	}
	if (num_mics==0)
		error=3;
	return error;
}


static int mic_shutdown (void)
{
	if (mic_handle)
	MicCloseAPI(&mic_handle);
	return (0);
}

void module_register (void)
{
	plugin_register_init ("mic", mic_init);
	plugin_register_shutdown ("mic", mic_shutdown);
	plugin_register_read ("mic", mic_read);
	plugin_register_config ("mic",mic_config, config_keys, config_keys_num);
} /* void module_register */

/*
 * vim: shiftwidth=2:softtabstop=2:textwidth=78
 */
