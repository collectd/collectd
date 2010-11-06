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

#define EXPECTED_PACKAGE_LENGTH 278
#define ESCAPE       0x10
#define PKT_BEGIN    0x04
#define PKT_END      0x03

#define DEFAULT_DEVICE "/dev/ttyUSB0"

static char *conf_device = NULL;
static int   conf_retries = 0;

static int fd = -1;

static const char *config_keys[] =
{
    "Device",
    "Retries"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int ted_read_value(double *ret_power, double *ret_voltage)
{
    unsigned char receive_buffer[300];
    unsigned char package_buffer[300];
    char pkt_request[1] = {0xAA};
    int package_buffer_pos;

    fd_set input;
    struct timeval timeout;

    int end_flag;
    int escape_flag;

    int status;

    assert (fd >= 0);

    /* Initialize the input set*/
    FD_ZERO (&input);
    FD_SET (fd, &input);

    /* Initialize timeout structure, set to 2 seconds */
    memset (&timeout, 0, sizeof (timeout));
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    /* clear out anything in the buffer */
    tcflush (fd, TCIFLUSH);

    status = write (fd, pkt_request, sizeof(pkt_request));
    if (status <= 0)
    {
        ERROR ("ted plugin: swrite failed.");
        return (-1);
    }

    /* Loop until we find the end of the package */
    end_flag = 0;
    escape_flag = 0;
    package_buffer_pos = 0;
    while (end_flag == 0)
    {
        ssize_t receive_buffer_length;
        ssize_t i;

        /* check for timeout or input error*/
        status = select (fd + 1, &input, NULL, NULL, &timeout);
        if (status == 0) /* Timeout */
        {
            WARNING ("ted plugin: Timeout while waiting for file descriptor "
                    "to become ready.");
            return (-1);
        }
        else if ((status < 0) && ((errno == EAGAIN) || (errno == EINTR)))
        {
            /* Some signal or something. Start over.. */
            continue;
        }
        else if (status < 0)
        {
            char errbuf[1024];
            ERROR ("ted plugin: select failed: %s",
                    sstrerror (errno, errbuf, sizeof (errbuf)));
            return (-1);
        }

        receive_buffer_length = read (fd, receive_buffer, sizeof (receive_buffer));
        if (receive_buffer_length < 0)
        {
            char errbuf[1024];
            if ((errno == EAGAIN) || (errno == EINTR))
                continue;
            ERROR ("ted plugin: read(2) failed: %s",
                    sstrerror (errno, errbuf, sizeof (errbuf)));
            return (-1);
        }
        else if (receive_buffer_length == 0)
        {
            /* Should we close the FD in this case? */
            WARNING ("ted plugin: Received EOF from file descriptor.");
            return (-1);
        }
        else if (receive_buffer_length > sizeof (receive_buffer))
        {
            ERROR ("ted plugin: read(2) returned invalid value %zi.",
                    receive_buffer_length);
            return (-1);
        }

        /*
         * packet filter loop
         *
         * Handle escape sequences in `receive_buffer' and put the
         * result in `package_buffer'.
         */
        /* We need to see the begin sequence first. When we receive `ESCAPE
         * PKT_BEGIN', we set `package_buffer_pos' to zero to signal that
         * the beginning of the package has been found. */

        escape_flag = 0;
        for (i = 0; i < receive_buffer_length; i++)
        {
            /* Check if previous byte was the escape byte. */
            if (escape_flag == 1)
            {
                escape_flag = 0;
                /* escape escape = single escape */
                if ((receive_buffer[i] == ESCAPE)
                        && (package_buffer_pos >= 0))
                {
                    package_buffer[package_buffer_pos] = ESCAPE;
                    package_buffer_pos++;
                }
                else if (receive_buffer[i] == PKT_BEGIN)
                {
                    package_buffer_pos = 0;
                }
                else if  (receive_buffer[i] == PKT_END)
                {
                    end_flag = 1;
                    break;
                }
                else
                {
                    DEBUG ("ted plugin: Unknown escaped byte: %#x",
                            (unsigned int) receive_buffer[i]);
                }
            }
            else if (receive_buffer[i] == ESCAPE)
            {
                escape_flag = 1;
            }
            /* if we are in a package add byte to buffer
             * otherwise throw away */
            else if (package_buffer_pos >= 0)
            {
                package_buffer[package_buffer_pos] = receive_buffer[i];
                package_buffer_pos++;
            }
        } /* for (i = 0; i < receive_buffer_length; i++) */
    } /* while (end_flag == 0) */

    /* Check for errors inside the loop. */
    if ((end_flag == 0) || (package_buffer_pos != EXPECTED_PACKAGE_LENGTH))
        return (-1);

    /*
     * Power is at positions 247 and 248 (LSB first) in [10kW].
     * Voltage is at positions 251 and 252 (LSB first) in [.1V].
     *
     * Power is in 10 Watt steps
     * Voltage is in volts
     */
    *ret_power = 10.0 * (double) ((((int) package_buffer[248]) * 256)
            + ((int) package_buffer[247]));
    *ret_voltage = 0.1 * (double) ((((int) package_buffer[252]) * 256)
            + ((int) package_buffer[251]));

    /* success */
    return (0);
} /* int ted_read_value */

static int ted_open_device (void)
{
    const char *dev;
    struct termios options;

    if (fd >= 0)
        return (0);

    dev = DEFAULT_DEVICE;
    if (conf_device != NULL)
        dev = conf_device;

    fd = open (dev, O_RDWR | O_NOCTTY | O_NDELAY | O_NONBLOCK);
    if (fd < 0)
    {
        ERROR ("ted plugin: Unable to open device %s.", dev);
        return (-1);
    }

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

    INFO ("ted plugin: Successfully opened %s.", dev);
    return (0);
} /* int ted_open_device */

static void ted_submit (char *type, double value)
{
    value_t values[1];
    value_list_t vl = VALUE_LIST_INIT;

    values[0].gauge = value;

    vl.values = values;
    vl.values_len = 1;
    sstrncpy (vl.host, hostname_g, sizeof (vl.host));
    sstrncpy (vl.plugin, "ted", sizeof (vl.plugin));
    sstrncpy (vl.type, type, sizeof (vl.type));

    plugin_dispatch_values (&vl);
}

static int ted_config (const char *key, const char *value)
{
    if (strcasecmp ("Device", key) == 0)
    {
        sfree (conf_device);
        conf_device = sstrdup (value);
    }
    else if (strcasecmp ("Retries", key) == 0)
    {
        int tmp;

        tmp = atoi (value);
        if (tmp < 0)
        {
            WARNING ("ted plugin: Invalid retry count: %i", tmp);
            return (1);
        }
        conf_retries = tmp;
    }
    else
    {
        ERROR ("ted plugin: Unknown config option: %s", key);
        return (-1);
    }

    return (0);
} /* int ted_config */

static int ted_read (void)
{
    double power;
    double voltage;
    int status;
    int i;

    status = ted_open_device ();
    if (status != 0)
        return (-1);

    power = NAN;
    voltage = NAN;
    for (i = 0; i <= conf_retries; i++)
    {
        status = ted_read_value (&power, &voltage);
        if (status == 0)
            break;
    }

    if (status != 0)
        return (-1);

    ted_submit ("power", power);
    ted_submit ("voltage", voltage);

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
    plugin_register_read ("ted", ted_read);
    plugin_register_shutdown ("ted", ted_shutdown);
} /* void module_register */

/* vim: set sw=4 et : */
