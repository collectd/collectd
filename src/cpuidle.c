/**
 * collectd - src/cpuidle.c
 * Copyright (C) 2005-2007  Peter Holik
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
 *   rinigus <http://github.com/rinigus>
 *
 * This plugin records the time spent in different CPU idle states.
 * Time share for CPU idle state is reported in s/s. Type instance is
 * used to indicate the idle state name. If per CPU data is requested,
 * plugin instance corresponds to the number of CPU.
 *
 * Documentation on used sysfs interface: https://www.kernel.org/doc/Documentation/cpuidle/sysfs.txt
 * 
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <search.h>
#include <stdlib.h>
#include <string.h>

// Configuration options
static const char *config_keys[] =
{
	"ReportByCpu"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

// Global variables

static _Bool report_by_cpu = 0; 		// option

static size_t num_cpu = 0;               // number of cpus
static size_t *cpu_states_num = NULL;    // number of states for each of the cpus

// used when collecting average states statistics
static size_t num_all_states = 0;        // number of different states among all CPU
static char **cpu_states_names = NULL;   // string array with names of cpu states
static derive_t *cpu_states_times = NULL;  // array of times spent among all CPUs in particular state

static size_t reported_last_run = 0;

///////////////////
// Helper functions

static int cmpstring_pp(const void *p1, const void *p2)
{
	return strcmp( * (const char * const *) p1, * (const char * const *) p2 );
}

static int cmpstring_dir(const void *p1, const void *p2)
{
	return strcmp( (const char *) p1, * (const char * const *) p2 );
}

static _Bool getstr(const char *fname, char *buffer, size_t buffer_size)
{
  FILE *fh;
  if ((fh = fopen(fname, "r")) == NULL)
    return (0);

  if ( fgets(buffer, buffer_size, fh) == NULL )
    {
      fclose(fh);
      return (0); // empty file
    }

  fclose(fh);

  return (1);
}

static _Bool getvalue(const char *fname, derive_t *value, char *buffer, size_t buffer_size)
{
  FILE *fh;
  if ((fh = fopen(fname, "r")) == NULL)
    return (0);

  if ( fgets(buffer, buffer_size, fh) == NULL )
    {
      fclose(fh);
      return (0); // empty file
    }

  (*value) = atoll( buffer );

  fclose(fh);

  return (1);
}

static void sanitize(char *buffer, size_t buffer_size)
{
	size_t i;
	for (i=0; i<buffer_size; ++i)
    {
		char c = buffer[i];
		if (c == '\0') return;
		if (c == '\n')
        {
			buffer[i] = '\0';
			return;
        }
      
		if (i == buffer_size-1)
        {
			buffer[i] = '\0';
			return;
        }
      
		if (c == ' ' || c == '-' || c == '/')
			buffer[i] = '_';
    }
}

// end of helper functions

///////////////////////////
// Interface with collectd

static int cpuidle_config (const char *key, const char *value)
{
	if (strcasecmp (key, "ReportByCpu") == 0)
		report_by_cpu = IS_TRUE(value);
	else return (-1);

	return (0);
}


static int cpuidle_init (void)
{
	char filename[128];
	int status;
	size_t i;
	_Bool done;
	
	// determine number of cpus
	done = 0;
	for (num_cpu = 0; !done; ++num_cpu)
	{
		status = ssnprintf (filename, sizeof (filename),
							"/sys/devices/system/cpu/cpu%zu/cpuidle",
							num_cpu);
		
		if ( (status < 1) || ((unsigned int)status >= sizeof (filename)) )
		{
			INFO("cpuidle plugin: error in ssnprintf");
			break;
		}
		
		if (access (filename, R_OK))
			break;
	}

	INFO ("cpuidle plugin: Found %zu CPU%s with cpuidle support", num_cpu,
		  (num_cpu == 1) ? "" : "s");
	
	if (num_cpu == 0)
	{
		plugin_unregister_read ("cpuidle");
		return (0);
	}

	// determine number of states for each cpu (could be different)
	if ( (cpu_states_num = malloc(sizeof(size_t) * num_cpu)) == NULL )
	{
		INFO("cpuidle plugin: failed to allocate memory for cpu_states_nr");
		plugin_unregister_read ("cpuidle");
		return (0);
	}
	
	for (i=0; i < num_cpu; ++i)
	{
		size_t nstate = 0;
		while (1) 
		{
			status = ssnprintf (filename, sizeof (filename),
								"/sys/devices/system/cpu/cpu%zu/cpuidle/state%zu",
								i, nstate);
			
			if ((status < 1) || ((unsigned int)status >= sizeof (filename)))
			{
				INFO("cpuidle plugin: error in ssnprintf/state");
				plugin_unregister_read ("cpuidle");
				return (0);
			}

			if (access (filename, R_OK))
				break;

			++nstate;
		}
		cpu_states_num[i] = nstate;
	}

	// initialize index arrays if the average data among all cpus is reported
	if (!report_by_cpu)
	{
		char filename[128];		
		char state_name[256];

		size_t i, j;
		for (i=0; i < num_cpu; ++i)
			for (j=0; j < cpu_states_num[i]; ++j)
			{
				// state name
				status = ssnprintf (filename, sizeof (filename),
									"/sys/devices/system/cpu/cpu%zu/cpuidle/state%zu/name",
									i, j);
				
				if ( (status < 1) || ((unsigned int)status >= sizeof (filename)) )
				{
					WARNING("cpuidle plugin: error in ssnprintf/state/time");
					plugin_unregister_read ("cpuidle");
					return (0);
				}
				
				if ( !getstr( filename, state_name, sizeof(state_name) ) )
				{
					ERROR ("cpuidle plugin: error reading %s.", filename);
					plugin_unregister_read ("cpuidle");
					return (0);
				}
				
				sanitize( state_name, sizeof(state_name) );

				// check if we have a state with the same name
				if ( cpu_states_names == NULL ||
					 lfind( state_name, cpu_states_names,
							&num_all_states, sizeof(char*), cmpstring_dir ) == NULL )
				{
					// insert new element
					cpu_states_names = realloc( cpu_states_names, sizeof(char*)*(num_all_states+1) );
					if ( cpu_states_names == NULL )
					{
						ERROR ("cpuidle plugin: error in realloc" );
						plugin_unregister_read ("cpuidle");
						return (0);
					}
					
					if ( ( cpu_states_names[num_all_states] = strndup(state_name, sizeof(state_name)) ) == NULL ) 
					{
						ERROR( "cpuidle plugin: error in strndup" );
						plugin_unregister_read ("cpuidle");
						return (0);
					}

					num_all_states++;
				}
			} // for loop over all states and cpus

		INFO( "cpuidle plugin: found %zu states covering all CPUs", num_all_states );

		if ( (cpu_states_times = malloc( num_all_states*sizeof(derive_t) )) == NULL )
		{
			ERROR ("cpuidle plugin: error in malloc of cpu_states_times" );
			plugin_unregister_read ("cpuidle");
			return (0);
		}

		qsort( cpu_states_names, num_all_states, sizeof(char*), cmpstring_pp );
	}

	return (0);
} /* int cpuidle_init */

static int cpuidle_shutdown (void)
{
	sfree( cpu_states_num );
	cpu_states_num = NULL;
	
	if ( !report_by_cpu )
	{
		size_t i;
		
		sfree(cpu_states_times);
		cpu_states_times = NULL;

		for (i=0; i < num_all_states; ++i)
			sfree( cpu_states_names[i] );
		sfree( cpu_states_names );
		num_all_states = 0;
		cpu_states_names = NULL;
	}
	
	return (0);
} /* int cpuidle_shutdown */

static void cpuidle_submit_value (const char *plugin_instance, const char *state_name, derive_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].derive = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "cpuidle", sizeof (vl.plugin));
	if ( plugin_instance != NULL )
		sstrncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, "total_time_in_ms", sizeof (vl.type));
	sstrncpy (vl.type_instance, state_name, sizeof (vl.type));

	plugin_dispatch_values (&vl);
	++reported_last_run;
}


static int cpuidle_read (void)
{
	char filename[128];		
	char buffer[512];
	char state_name[256];
	char statind[128];
	int status;
	derive_t value;
	size_t i, j;

	reported_last_run = 0;

	if ( !report_by_cpu )
		for (i=0; i < num_all_states; ++i)
			cpu_states_times[i] = 0.0;

	for (i=0; i < num_cpu; ++i)
		for (j=0; j < cpu_states_num[i]; ++j)
		{
			// state name
			status = ssnprintf (filename, sizeof (filename),
								"/sys/devices/system/cpu/cpu%zu/cpuidle/state%zu/name",
								i, j);
		
			if ( (status < 1) || ((unsigned int)status >= sizeof (filename)) )
			{
				WARNING("cpuidle plugin: error in ssnprintf/state/time");
				return (-1);
			}

			if ( !getstr( filename, state_name, sizeof(state_name) ) )
			{
				ERROR ("cpuidle plugin: error reading %s.", filename);
				return (-1);
			}
			
			sanitize( state_name, sizeof(state_name) );
			
			// value
			status = ssnprintf (filename, sizeof (filename),
								"/sys/devices/system/cpu/cpu%zu/cpuidle/state%zu/time",
								i, j);
		
			if ( (status < 1) || ((unsigned int)status >= sizeof (filename)) )
			{
				WARNING("cpuidle plugin: error in ssnprintf/state/time");
				return (-1);
			}

			if ( !getvalue( filename, &value, buffer, sizeof(buffer) ) )
			{
				WARNING("cpuidle plugin: error reading %s", filename);
				return (-2);
			}

			value = value*1e-6; // values in sysfs are given in microseconds

			if ( report_by_cpu )
			{
				status = ssnprintf (statind, sizeof (statind),
									"%zu", i);
				
				if ((status < 1) || ((unsigned int)status >= sizeof (statind)))
				{
					WARNING("cpuidle plugin: error in ssnprintf");
					return (-1);
				}
				
				cpuidle_submit_value( statind, state_name, value );
			}
			else
			{
				char **ind;
				ind = (char **)bsearch( state_name, cpu_states_names,
										num_all_states, sizeof(char*), cmpstring_dir );
				if ( ind == NULL )
				{
					ERROR("cpuidle plugin: state %s not found in internal database", state_name );
					return (-3);
				}

				cpu_states_times[ ind - cpu_states_names ] += value;
			}
		}

	for (i=0; i < num_all_states; ++i)
		cpuidle_submit_value( NULL, cpu_states_names[i], cpu_states_times[i] / num_cpu );

	if ( reported_last_run == 0 )
	{
		WARNING("cpuidle plugin: nothing was reported, possibly cpuidle is not supported");
		return (-1);
	}

	return (0);
} /* int cpuidle_read */

void module_register (void)
{
	plugin_register_config ("cpuidle", cpuidle_config, config_keys, config_keys_num);
	plugin_register_init ("cpuidle", cpuidle_init);
	plugin_register_read ("cpuidle", cpuidle_read);
	plugin_register_shutdown ("cpuidle", cpuidle_shutdown);
}
