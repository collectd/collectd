/**
 * collectd - src/xmms.c
 * Copyright (C) 2013  
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
 *   Evan Felix <evan.felix@pnnl.gov
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"

#include <MicAccessTypes.h>
#include <MicAccessErrorTypes.h>
#include <MicAccessApi.h>
#include <MicThermalAPI.h>

#define MAX_MICS 32

static MicDeviceOnSystem mics[MAX_MICS];
static U32 numMics = MAX_MICS;
static HANDLE micHandle=NULL;
#define NUM_THERMS 7
static const int therms[NUM_THERMS] = {eMicThermalDie,eMicThermalDevMem,eMicThermalFin,eMicThermalFout,eMicThermalVccp,eMicThermalVddg,eMicThermalVddq};
static const char *thermNames[NUM_THERMS] = {"die","devmem","fin","fout","vccp","vddg","vddq"};


static int mic_init (void)
{
  U32 ret;

  ret = MicInitAPI(&micHandle,  eTARGET_SCIF_DRIVER, mics, &numMics);
  if (ret != MIC_ACCESS_API_SUCCESS) {
	ERROR("Problem initializing MicAccessAPI: %s",MicGetErrorString(ret));
  }
  INFO("MICs found: %d",numMics);
  if (numMics<0 || numMics>=MAX_MICS)
	return (1);
  else
	return (0);
}

static void mic_submit_memory_use(int micnumber, const char *type, gauge_t val)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = val;

  vl.values=values;
  vl.values_len=1;

  strncpy (vl.host, hostname_g, sizeof (vl.host));
  strncpy (vl.plugin, "mic", sizeof (vl.plugin));
  ssnprintf (vl.plugin_instance, sizeof (vl.plugin_instance), "%i", micnumber);
  strncpy (vl.type, "memory", sizeof (vl.type));
  strncpy (vl.type_instance, type, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
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


static int mic_read (void)
{
  int i,j;
  U32 ret,bufferSize;
  U32 *tempBuffer;
  int error;
  U32 mem_total,mem_used,mem_bufs;

  error=0;
  for (i=0;i<numMics;i++) {
	ret = MicInitAdapter(&micHandle,&mics[i]);
	if (ret != MIC_ACCESS_API_SUCCESS) {
	  ERROR("Problem initializing MicAdapter: %s",MicGetErrorString(ret));
	  error=1;
	  break;
	}

	/* Gather memory Utilization */
	ret = MicGetMemoryUtilization(micHandle,&mem_total,&mem_used,&mem_bufs);
	if (ret != MIC_ACCESS_API_SUCCESS) {
	  ERROR("Problem getting Memory Utilization: %s",MicGetErrorString(ret));
	  error=3;
	  break;
	}
	/* API reprots KB's of memory, adjust for this */ 
	mic_submit_memory_use(i,"total",mem_total*1024);
	mic_submit_memory_use(i,"used",mem_used*1024);
	mic_submit_memory_use(i,"bufs",mem_bufs*1024);
	/*INFO("Memory Read: %u %u %u",mem_total,mem_used,mem_bufs);*/

	/* Gather Temperature Information */
	bufferSize = sizeof(U32);
	tempBuffer = malloc(bufferSize);
	for (j=0;j<NUM_THERMS;j++) {
	  ret = MicGetTemperature(micHandle,therms[j],tempBuffer,&bufferSize);
	  if (ret != MIC_ACCESS_API_SUCCESS) {
		ERROR("Problem getting Temperature(%d) %s",j,MicGetErrorString(ret));
		error=4;
		break;
	  }
	  /*INFO("Temp Read: %u: %u %s",j,tempBuffer[0],thermNames[j]);*/
	  mic_submit_temp(i,thermNames[j],tempBuffer[0]);
	}
	if (error)
	  break;

	ret = MicCloseAdapter(micHandle);
	if (ret != MIC_ACCESS_API_SUCCESS) {
	  ERROR("Problem initializing MicAdapter: %s",MicGetErrorString(ret));
	  error=2;
	  break;
	}
  }
  return error;
}


static int mic_shutdown (void)
{
  if (micHandle)
	MicCloseAPI(micHandle);
  return (0);
}

void module_register (void)
{
  plugin_register_init ("mic", mic_init);
  plugin_register_shutdown ("mic", mic_shutdown);
  plugin_register_read ("mic", mic_read);
} /* void module_register */

/*
 * vim: shiftwidth=2:softtabstop=2:textwidth=78
 */
