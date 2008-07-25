/*
 * /usr/share/doc/collectd/examples/myplugin.c
 *
 * A plugin template for collectd.
 *
 * Written by Sebastian Harl <sh@tokkee.org>
 *
 * This is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; only version 2 of the License is applicable.
 */

/*
 * Notes:
 * - plugins are executed in parallel, thus, thread-safe
 *   functions need to be used
 * - each of the functions below (except module_register)
 *   is optional
 */

#if ! HAVE_CONFIG_H

#include <stdlib.h>

#include <string.h>

#ifndef __USE_ISOC99 /* required for NAN */
# define DISABLE_ISOC99 1
# define __USE_ISOC99 1
#endif /* !defined(__USE_ISOC99) */
#include <math.h>
#if DISABLE_ISOC99
# undef DISABLE_ISOC99
# undef __USE_ISOC99
#endif /* DISABLE_ISOC99 */

#include <time.h>

#endif /* ! HAVE_CONFIG */

#include <collectd/collectd.h>
#include <collectd/common.h>
#include <collectd/plugin.h>

/*
 * data source definition:
 * - name of the data source
 * - type of the data source (DS_TYPE_GAUGE, DS_TYPE_COUNTER)
 * - minimum allowed value
 * - maximum allowed value
 */
static data_source_t dsrc[1] =
{
	{ "my_ds", DS_TYPE_GAUGE, 0, NAN }
};

/*
 * data set definition:
 * - name of the data set
 * - number of data sources
 * - list of data sources
 *
 * NOTE: If you're defining a custom data-set, you have to make that known to
 * any servers as well. Else, the server is not able to store values using the
 * type defined by that data-set.
 * It is strongly recommended to use one of the types and data-sets
 * pre-defined in the types.db file.
 */
static data_set_t ds =
{
	"myplugin", STATIC_ARRAY_SIZE (dsrc), dsrc
};

/*
 * This function is called once upon startup to initialize the plugin.
 */
static int my_init (void)
{
	/* open sockets, initialize data structures, ... */

	/* A return value != 0 indicates an error and causes the plugin to be
	   disabled. */
    return 0;
} /* static int my_init (void) */

/*
 * This function is called in regular intervalls to collect the data.
 */
static int my_read (void)
{
	value_t values[1]; /* the size of this list should equal the number of
						  data sources */
	value_list_t vl = VALUE_LIST_INIT;

	/* do the magic to read the data */
	values[0].gauge = random ();

	vl.values     = values;
	vl.values_len = 1;
	vl.time       = time (NULL);
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "myplugin", sizeof (vl.plugin));
	/* optionally set vl.plugin_instance and vl.type_instance to reasonable
	 * values (default: "") */

	/* dispatch the values to collectd which passes them on to all registered
	 * write functions - the first argument is used to lookup the data set
	 * definition (it is strongly recommended to use a type defined in the
	 * types.db file) */
	plugin_dispatch_values ("myplugin", &vl);

	/* A return value != 0 indicates an error and the plugin will be skipped
	 * for an increasing amount of time. */
    return 0;
} /* static int my_read (void) */

/*
 * This function is called after values have been dispatched to collectd.
 */
static int my_write (const data_set_t *ds, const value_list_t *vl)
{
	char name[1024] = "";
	int i = 0;

	if (ds->ds_num != vl->values_len) {
		plugin_log (LOG_WARNING, "DS number does not match values length");
		return -1;
	}

	/* get the default base filename for the output file - depending on the
	 * provided values this will be something like
	 * <host>/<plugin>[-<plugin_type>]/<instance>[-<instance_type>] */
	if (0 != format_name (name, 1024, vl->host, vl->plugin,
			vl->plugin_instance, ds->type, vl->type_instance))
		return -1;

	for (i = 0; i < ds->ds_num; ++i) {
		/* do the magic to output the data */
		printf ("%s (%s) at %i: ", name,
				(ds->ds->type == DS_TYPE_GAUGE) ? "GAUGE" : "COUNTER",
				(int)vl->time);

		if (ds->ds->type == DS_TYPE_GAUGE)
			printf ("%f\n", vl->values[i].gauge);
		else
			printf ("%lld\n", vl->values[i].counter);
	}
	return 0;
} /* static int my_write (data_set_t *, value_list_t *) */

/*
 * This function is called when plugin_log () has been used.
 */
static void my_log (int severity, const char *msg)
{
	printf ("LOG: %i - %s\n", severity, msg);
	return;
} /* static void my_log (int, const char *) */

/*
 * This function is called when plugin_dispatch_notification () has been used.
 */
static int my_notify (const notification_t *notif)
{
	char time_str[32] = "";
	struct tm *tm = NULL;

	int n = 0;

	if (NULL == (tm = localtime (&notif->time)))
		time_str[0] = '\0';

	n = strftime (time_str, 32, "%F %T", tm);
	if (n >= 32) n = 31;
	time_str[n] = '\0';

	printf ("NOTIF (%s): %i - ", time_str, notif->severity);

	if ('\0' != *notif->host)
		printf ("%s: ", notif->host);

	if ('\0' != *notif->plugin)
		printf ("%s: ", notif->plugin);

	if ('\0' != *notif->plugin_instance)
		printf ("%s: ", notif->plugin_instance);

	if ('\0' != *notif->type)
		printf ("%s: ", notif->type);

	if ('\0' != *notif->type_instance)
		printf ("%s: ", notif->type_instance);

	printf ("%s\n", notif->message);
	return 0;
} /* static int my_notify (notification_t *) */

/*
 * This function is called before shutting down collectd.
 */
static int my_shutdown (void)
{
	/* close sockets, free data structures, ... */
	return 0;
} /* static int my_shutdown (void) */

/*
 * This function is called after loading the plugin to register it with
 * collectd.
 */
void module_register (void)
{
	plugin_register_log ("myplugin", my_log);
	plugin_register_notification ("myplugin", my_notify);
	plugin_register_data_set (&ds);
	plugin_register_read ("myplugin", my_read);
	plugin_register_init ("myplugin", my_init);
	plugin_register_write ("myplugin", my_write);
	plugin_register_shutdown ("myplugin", my_shutdown);
    return;
} /* void module_register (void) */

