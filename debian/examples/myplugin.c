/*
 * /usr/share/doc/collectd/examples/sample_plugin.c
 *
 * A sample plugin for collectd.
 *
 * Written by Sebastian Harl <sh@tokkee.org>
 *
 * This is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free 
 * Software Foundation; either version 2 of the License, or (at your 
 * option) any later version.
 */

#include <collectd/common.h>    /* rrd_update_file */
#include <collectd/plugin.h>    /* plugin_* */

#include <stdio.h>
#include <stdlib.h>

/* Optional config file support */
/* #include <collectd/configfile.h> */

/* Optional debugging support 
 * (only available if collectd was compiled with debugging support) */
/* #include <collectd/utils_debug.h> */

#define MODULE_NAME "myplugin"

/* Name of the rrd file under DataDir (/var/lib/collectd by default)
 *
 * The name may contain slashes to create subdirectories. */
static char *my_rrd = "myplugin.rrd";

/* DS definitions for the rrd file
 *
 * See the rrdcreate(1) manpage for details. The heartbeat is configurable in
 * collectd. It defaults to 25. */
static char *ds_def[] =
{
    "DS:my_ds:GAUGE:25:0:U",
    NULL
};

/* DS count */
static int ds_num = 1;

/* Time at which the read function is called */
extern time_t curtime;

/* Initialize the plugin
 *
 * This function is called to set up a plugin before using it. */
static void my_init(void)
{
    /* we have nothing to do here :-) */
    return;
}

/* Get the data
 *
 * This function implements the magic used to get the desired values that
 * should be stored in the rrd file. It uses plugin_submit to transfer the
 * data to whatever place is configured in the config file. If there are more
 * than one instances you should pass a uniq identifier as seconds argument to
 * the plugin_submit function. */
#define BUFSIZE 256
static void my_read(void)
{
    long int data = 0;
    char buf[BUFSIZE] = "";

    /* magic ;-) */
    data = random();

    if (snprintf(buf, BUFSIZE, "%u:%li", 
                (unsigned int)curtime, data) >= BUFSIZE)
        return;

    plugin_submit(MODULE_NAME, NULL, buf);
    return;
}
#undef BUFSIZE

/* Save the data
 *
 * This function saves the data to the appropriate location by calling
 * rrd_update_file. It is used to "calculate" the filename and DS definition
 * appropriate for the given instance. */
static void my_write(host, inst, val)
    char *host;
    char *inst;
    char *val;
{
    rrd_update_file(host, my_rrd, val, ds_def, ds_num);
    return;
}

/* Register the plugin
 *
 * This function registers the plugin with collectd. It has to be named
 * "module_register". */
void module_register(void)
{
    plugin_register(MODULE_NAME, my_init, my_read, my_write);
    return;
}

