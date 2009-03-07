/**
 * collectd - src/ted.c
 * Copyright (C) 2005,2006  Peter Holik
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



#define LINE_LENGTH 282
#define PKT_REQUEST  "\xAA"
#define ESCAPE       0x10
#define PKT_BEGIN    0x04
#define PKT_END      0x03

#define DEFAULT_DEVICE "/dev/ttyUSB"
#define CLIENT_LIST_PREFIX  "CLIENT_LIST,"

static char *device = NULL;
static int fd = -1;

static const char *config_keys[] = { "Device" };
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);




static int ted_read_value(double *kv, double *voltage)
{
	int retry = 3; /* sometimes we receive garbadge */

	do
	{
		struct timeval time_end;

		tcflush(fd, TCIFLUSH);

		if (gettimeofday (&time_end, NULL) < 0)
	        {
			char errbuf[1024];
	                ERROR ("ted plugin: gettimeofday failed: %s",
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
	                return (-1);
	        }
	        time_end.tv_sec++;	

		while (1)
		{
		        unsigned char buf[4096];
		        unsigned char package_buffer[4096];
                        char sResultByte;
                        char sCmd[1];
			int status;
                        int byte;
                        int package_length=-1;
                        int start_flag=0;
                        int escape_flag=0;
    			struct timeval timeout;
    			struct timeval time_now;
                        sCmd[0] = 0xAA;

			status = write (fd, sCmd, 1);
                        INFO ("status of write %d",status);
			if (status < 0)
			{
				ERROR ("ted plugin: swrite failed.");
				return (-1);
			}


			if (gettimeofday (&time_now, NULL) < 0)
	                {
				char errbuf[1024];
		                ERROR ("ted plugin: "
						"gettimeofday failed: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
	                        return (-1);
	                }
			/*if (timeval_cmp (time_end, time_now, &timeout) < 0)
	                        break; */

                        usleep(700000);
			status = select(fd+1, NULL, NULL, NULL, &timeout);
                        INFO ("status 1 %d",status);
                        status = 1;



			if (status > 0) /* usually we succeed */
			{
				status = read(fd, buf, 4096);
                                INFO ("status of read %d",status);

				if ((status < 0) && ((errno == EAGAIN) || (errno == EINTR)))
				        continue;

                                       for (byte=0; byte< status; byte++) {
                                            sResultByte = buf[byte];
                                            if (escape_flag) {
                                                escape_flag = 0;
                                                if ((sResultByte==ESCAPE) & (package_length > 0)){
                                                    package_buffer[package_length] = ESCAPE;
                                                    package_length++;  
                                                    }          
                                                else if (sResultByte==PKT_BEGIN){
                                                    start_flag = 1;
                                                    package_length=0;
                                                    }
                                                else if  (sResultByte==PKT_END){
                                                    package_buffer[package_length] = '\0';
                                                    package_length++;
                                                    }
                                                }
                                            else if (sResultByte == ESCAPE)
                                                escape_flag = 1;
                                            else if (package_length >= 0){
                                                package_buffer[package_length] = sResultByte;
                                                package_length++;  
                                                }

                                        }

                                 INFO ("read package_length %d",package_length);
				
				if (package_length == 279)
				{
                                    *kv = ((package_buffer[248] * 256) + package_buffer[247])*10.0;
                                    INFO ("kv %f",*kv);
                                    *voltage = ((package_buffer[252] * 256) + package_buffer[251])/10.0;
                                    INFO ("voltage %f",*voltage);
                                    return (0); /* value received */
                                }
                                else
                                    INFO ("Not the correct package");
                                    usleep(700000);
                                    continue;
                                    //return (-1); /* Not pro package */
			}
			else if (!status) /* Timeout */
            		{
	                        break;
			}
			else if ((status == -1) && ((errno == EAGAIN) || (errno == EINTR)))
			{
                                usleep(700000);
			        continue;
			}
			else /* status == -1 */
            		{
				char errbuf[1024];
		                ERROR ("ted plugin: "
						"select failed: %s",
						sstrerror (errno, errbuf, sizeof (errbuf)));
	                        break;
			}
		}
	} while (--retry);

	return (-2);  /* no value received */
} /* int ted_read_value */

static int ted_init (void)
{
	int i;
        int status;
	//char device[] = "/dev/ttyUSB ";
        char sCmd[1];

        char buf[4096];
        sCmd[0] = 0xAA;
        
        if (device == NULL)
            device = DEFAULT_DEVICE;
        
	for (i = 0; i < 10; i++)
	{
		device[strlen(device)-1] = i + '0'; 

		if ((fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY | O_NONBLOCK)) > 0)
		{
                        struct termios options;
                        // Get the current options for the port...
                        tcgetattr(fd, &options);                        
                        options.c_cflag = B19200 | CS8 | CSTOPB | CREAD | CLOCAL;
			options.c_iflag = IGNBRK | IGNPAR;
	    		options.c_oflag = 0;
			options.c_lflag = 0;
			options.c_cc[VTIME] = 3;
			options.c_cc[VMIN]  = 50;
                                            
                        // Set the new options for the port...
                        tcflush(fd, TCIFLUSH);
                        tcsetattr(fd, TCSANOW, &options);
                        
			status = swrite (fd, sCmd, 1);
                        if (status < 0)
                            continue;
                        usleep(900000);
                        status = read(fd, buf, 4096);
                        if (status < 0)
                            continue;
                        INFO ("status of read %d",status);
                        INFO ("length of read %d", strlen(buf));
			
				INFO ("ted plugin: Device "
						"found at %s", device);
				return (0);
			
		}
	}

	ERROR ("ted plugin: No device found");
	return (-1);
}
#undef LINE_LENGTH

static void ted_submit (char *type_instance, double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "ted", sizeof (vl.plugin));
	sstrncpy (vl.type, "ted", sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}

static int ted_config (const char *key, const char *value)
{
	if (strcasecmp ("Device", key) == 0)
	{
		sfree (device);
		device = sstrdup (value);
	}
	else
	{
		return (-1);
	}
        return (0);
} /* int openvpn_config */


static int ted_read (void)
{
	double kv;
        double voltage;

	if (fd < 0)
		return (-1);

	if (ted_read_value (&kv,&voltage) != 0)
		return (-1);

	ted_submit ("kv", kv);	
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
}

void module_register (void)
{
	plugin_register_config ("ted", ted_config,
				config_keys, config_keys_num);
	plugin_register_init ("ted", ted_init);
	plugin_register_read ("ted", ted_read);
	plugin_register_shutdown ("ted", ted_shutdown);
} /* void module_register */
