/**
 * collectd - src/multimeter.c
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
 *   Peter Holik <peter at holik.at>
 *
 * Used multimeter: Metex M-4650CR
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if HAVE_TERMIOS_H && HAVE_SYS_IOCTL_H && HAVE_MATH_H
# include <termios.h>
# include <sys/ioctl.h>
# include <math.h>
#else
# error "No applicable input method."
#endif

static int fd = -1;

#define LINE_LENGTH 14
static int multimeter_read_value(double *value)
{
	int retry = 3; /* sometimes we receive garbadge */

	do
	{
		struct timeval time_end;

		tcflush(fd, TCIFLUSH);

		if (gettimeofday (&time_end, NULL) < 0)
	        {
			char errbuf[1024];
	                ERROR ("multimeter plugin: gettimeofday failed: %s",
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
	                return (-1);
	        }
	        time_end.tv_sec++;	

		while (1)
		{
		        char buf[LINE_LENGTH];
			char *range;
			int status;
			fd_set rfds;
    			struct timeval timeout;
    			struct timeval time_now;

			status = swrite (fd, "D", 1);
			if (status < 0)
			{
				ERROR ("multimeter plugin: swrite failed.");
				return (-1);
			}

			FD_ZERO(&rfds);
			FD_SET(fd, &rfds);

			if (gettimeofday (&time_now, NULL) < 0)
	                {
				char errbuf[1024];
		                ERROR ("multimeter plugin: "
						"gettimeofday failed: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
	                        return (-1);
	                }
			if (timeval_cmp (time_end, time_now, &timeout) < 0)
	                        break;

			status = select(fd+1, &rfds, NULL, NULL, &timeout);

			if (status > 0) /* usually we succeed */
			{
				status = read(fd, buf, LINE_LENGTH);

				if ((status < 0) && ((errno == EAGAIN) || (errno == EINTR)))
				        continue;

				/* Format: "DC 00.000mV  \r" */
				if (status > 0 && status == LINE_LENGTH)
				{
					*value = strtod(buf + 2, &range);

					if ( range > (buf + 6) )
					{
			    			range = buf + 9;

						switch ( *range )
						{
			    				case 'p': *value *= 1.0E-12; break;
					    		case 'n': *value *= 1.0E-9; break;
							case 'u': *value *= 1.0E-6; break;
			    				case 'm': *value *= 1.0E-3; break;
							case 'k': *value *= 1.0E3; break;
							case 'M': *value *= 1.0E6; break;
							case 'G': *value *= 1.0E9; break;
						}
					}
					else
						return (-1); /* Overflow */

					return (0); /* value received */
				}
				else break;
			}
			else if (!status) /* Timeout */
            		{
	                        break;
			}
			else if ((status == -1) && ((errno == EAGAIN) || (errno == EINTR)))
			{
			        continue;
			}
			else /* status == -1 */
            		{
				char errbuf[1024];
		                ERROR ("multimeter plugin: "
						"select failed: %s",
						sstrerror (errno, errbuf, sizeof (errbuf)));
	                        break;
			}
		}
	} while (--retry);

	return (-2);  /* no value received */
} /* int multimeter_read_value */

static int multimeter_init (void)
{
	int i;
	char device[] = "/dev/ttyS ";

	for (i = 0; i < 10; i++)
	{
		device[strlen(device)-1] = i + '0'; 

		if ((fd = open(device, O_RDWR | O_NOCTTY)) > 0)
		{
			struct termios tios;
			int rts = TIOCM_RTS;
			double value;

			tios.c_cflag = B1200 | CS7 | CSTOPB | CREAD | CLOCAL;
			tios.c_iflag = IGNBRK | IGNPAR;
	    		tios.c_oflag = 0;
			tios.c_lflag = 0;
			tios.c_cc[VTIME] = 3;
			tios.c_cc[VMIN]  = LINE_LENGTH;

			tcflush(fd, TCIFLUSH);
			tcsetattr(fd, TCSANOW, &tios);
			ioctl(fd, TIOCMBIC, &rts);
			
    			if (multimeter_read_value (&value) < -1)
			{
				close (fd);
				fd = -1;
			}
			else
			{
				INFO ("multimeter plugin: Device "
						"found at %s", device);
				return (0);
			}
		}
	}

	ERROR ("multimeter plugin: No device found");
	return (-1);
}
#undef LINE_LENGTH

static void multimeter_submit (double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "multimeter", sizeof (vl.plugin));
	sstrncpy (vl.type, "multimeter", sizeof (vl.type));

	plugin_dispatch_values (&vl);
}

static int multimeter_read (void)
{
	double value;

	if (fd < 0)
		return (-1);

	if (multimeter_read_value (&value) != 0)
		return (-1);

	multimeter_submit (value);
	return (0);
} /* int multimeter_read */

static int multimeter_shutdown (void)
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
	plugin_register_init ("multimeter", multimeter_init);
	plugin_register_read ("multimeter", multimeter_read);
	plugin_register_shutdown ("multimeter", multimeter_shutdown);
} /* void module_register */
