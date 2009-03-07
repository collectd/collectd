/**
 * collectd - src/ted.c
 * Copyright (C) 2009  Eric Reed
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
 *   Eric Reed <ericr at reedhome.net>
 *
 *  This is a collectd module for The Energy Detective: A low-cost whole
 * house energy monitoring system. For more information on TED, see
 * http://theenergydetective.com
 *
 * This module was not created by Energy, Inc. nor is it supported by
 * them in any way. It was created using information from two sources:
 * David Satterfield's TED module for Misterhouse, and Micah Dowty's TED
 * Python Module.
 *
 * This has only tested with the model 1001 RDU, with
 * firmware version 9.01U. The USB port is uses the very common FTDI
 * USB-to-serial chip, so the RDU will show up as a serial device on
 * Windows, Mac OS, or Linux.
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#if HAVE_TERMIOS_H && HAVE_SYS_IOCTL_H && HAVE_MATH_H
# include <termios.h>
# include <sys/ioctl.h>
# include <math.h>
#else
# error "No applicable input method."
#endif

#define PKT_LENGTH 278
#define MAX_PKT 300
#define ESCAPE       0x10
#define PKT_BEGIN    0x04
#define PKT_END      0x03

#define DEFAULT_DEVICE "/dev/ttyUSB "

static char *device = NULL;
static int fd = -1;

static const char *config_keys[] = { "Device" };
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int ted_read_value(double *kw, double *voltage)
{
    unsigned char sResult[MAX_PKT];
    unsigned char package_buffer[MAX_PKT];
    char sResultByte;
    int status;
    int byte;
    int package_length;
    int escape_flag;
    int end_flag;
    int sResultnum;
    struct timeval timeout;
    char pkt_request[1] = {0xAA};
    fd_set input;

    int retry = 3; /* sometimes we receive garbadge */

    /* Initialize the input set*/
    FD_ZERO(&input);
    FD_SET(fd, &input);

    /* Initialize timout structure, set to 1 second    */
    do
    {
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        escape_flag = 0;
        end_flag = 0;
        package_length = 0;
        /* clear out anything in the buffer */
        tcflush(fd, TCIFLUSH);

        status = write (fd, pkt_request,sizeof(pkt_request));
        DEBUG ("status of write %d",status);
        if (status < 0)
        {
            ERROR ("ted plugin: swrite failed.");
            return (-1);
        }


        /* Loop until we find the end of the package */
        while (end_flag == 0)
        {
            /* check for timeout or input error*/
            status = select(fd+1, &input, NULL, NULL, &timeout);
            if (status == 0) /* Timeout */
            {
                INFO ("Timeout");
                break;
            }
            else if ((status == -1) && ((errno == EAGAIN) || (errno == EINTR)))
            {
                DEBUG ("Not Ready");
                continue;
            }
            else if (status == -1)
            {
                char errbuf[1024];
                ERROR ("ted plugin: select failed: %s",
                        sstrerror (errno, errbuf, sizeof (errbuf)));
                break;
            }

            else
            {
                /* find out how may bytes are in the input buffer*/
                ioctl(fd, FIONREAD, &byte);
                DEBUG  ("bytes in buffer %d",byte);
                if (byte <= 0)
                {
                    continue;
                }

                sResultnum = read(fd, sResult, MAX_PKT);
                DEBUG  ("bytes read %d",sResultnum);

                /*
                 * packet filter loop
                 */
                for (byte=0; byte< sResultnum; byte++)
                {
                    sResultByte = sResult[byte];
                    /* was byte before escape */
                    if (escape_flag == 1)
                    {
                        escape_flag = 0;
                        /* escape escape = single escape */
                        if ((sResultByte==ESCAPE) & (package_length > 0))
                        {
                            package_buffer[package_length] = ESCAPE;
                            package_length++;
                        }
                        else if (sResultByte==PKT_BEGIN)
                        {
                            package_length=0;
                        }
                        else if  (sResultByte==PKT_END)
                        {
                            end_flag = 1;
                            break;
                        }
                    }
                    else if (sResultByte == ESCAPE)
                    {
                        escape_flag = 1;
                    }
                    /* if we are in a package add byte to buffer
                     * otherwise throw away */
                    else if (package_length >= 0)
                    {
                        package_buffer[package_length] = sResultByte;
                        package_length++;
                    }
                }
            }
        }
        DEBUG ("read package_length %d",package_length);

        if (package_length != PKT_LENGTH)
        {
            INFO ("Not the correct package");
            /* delay until next package is loaded into TED TED is updated once
             * per second */
            usleep (1000000);
            continue;
        }

        /* part of the info in the package get KiloWatts at char 247 and 248
         * get Voltage at char 251 and 252 */
        *kw = ((package_buffer[248] * 256) + package_buffer[247])*10.0;
        DEBUG ("kw %f",*kw);
        *voltage = ((package_buffer[252] * 256) + package_buffer[251])/10.0;
        DEBUG ("voltage %f",*voltage);
        return (0); /* value received */


    } while (--retry);

    return (-2);  /* no value received */
} /* int ted_read_value */

static int ted_init (void)
{
    int i;
    double kw;
    double voltage;

    if (device == NULL)
    {
        device = DEFAULT_DEVICE;
    }

    for (i = 0; i < 10; i++)
    {
        device[strlen(device)-1] = i + '0';

        if ((fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY | O_NONBLOCK)) <= 0)
        {
            DEBUG ("No device at fd %d", fd);
            close (fd);
            continue;
        }
        struct termios options;
        /* Get the current options for the port... */
        tcgetattr(fd, &options);
        options.c_cflag = B19200 | CS8 | CSTOPB | CREAD | CLOCAL;
        options.c_iflag = IGNBRK | IGNPAR;
        options.c_oflag = 0;
        options.c_lflag = 0;
        options.c_cc[VTIME] = 20;
        options.c_cc[VMIN]  = 250;

        /* Set the new options for the port... */
        tcflush(fd, TCIFLUSH);
        tcsetattr(fd, TCSANOW, &options);

        if (ted_read_value (&kw,&voltage) != 0)
        {
            DEBUG ("No device at fd %d", fd);
            close (fd);
            continue;
        }

        INFO ("ted plugin: Device found at %s", device);
        return (0);
    }

    ERROR ("ted plugin: No device found");
    return (-1);
}

static void ted_submit (char *type_instance, double value)
{
    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;

    values[0].gauge = value;

    vl.values = values;
    vl.values_len = 1;
    sstrncpy (vl.host, hostname_g, sizeof (vl.host));
    sstrncpy (vl.plugin, "ted", sizeof (vl.plugin));
    sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));
    sstrncpy (vl.type, "ted", sizeof (vl.type));

    plugin_dispatch_values (&vl);
}

static int ted_config (const char *key, const char *value)
{
    if (strcasecmp ("Device", key) != 0)
    {
        return (-1);
    }

    sfree (device);
    device = sstrdup (value);
    return (0);
} /* int ted_config */

static int ted_read (void)
{
    double kw;
    double voltage;

    if (fd < 0)
        return (-1);

    if (ted_read_value (&kw,&voltage) != 0)
        return (-1);

    ted_submit ("ted_kw", kw);
    ted_submit ("ted_voltage", voltage);
    return (0);
} /* int ted_read */

static int ted_shutdown (void)
{
    if (fd >= 0)
    {
        close (fd);
        fd = -1;
    }

    return (0);
} /* int ted_shutdown */

void module_register (void)
{
    plugin_register_config ("ted", ted_config,
            config_keys, config_keys_num);
    plugin_register_init ("ted", ted_init);
    plugin_register_read ("ted", ted_read);
    plugin_register_shutdown ("ted", ted_shutdown);
} /* void module_register */

/* vim: set sw=4 et : */
