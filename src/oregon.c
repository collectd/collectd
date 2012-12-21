/**
 * collectd - src/oregon.c
 * Copyright (C) 2010  Manuel CISSÉ
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
 *   Manuel CISSÉ <manuel_cisse at yahoo.fr>
 * based on the ping plugin written by Florian octo Forster <octo at verplant.org>
 * Protocol decoding based on the work of Per Ejeklint <ejeklint at mac.com> :
 * http://github.com/ejeklint/WLoggerDaemon/blob/master/Station_protocol.md
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <linux/hidraw.h>

static const uint16_t oregon_device_id[][2] = {
  { 0x0FDE /* Oregon */, 0xCA01 /* RMS300 and possibly WMR100/WMR200 */ },
};

#define IDENTIFIER_TEMP_HUMIDITY 0x42
#define IDENTIFIER_DATE_TIME     0x60 /* not used at the moment */
#define IDENTIFIER_WIND          0x48 /* not used at the moment */
#define IDENTIFIER_PRESSURE      0x46 /* not used at the moment */
#define IDENTIFIER_RAIN          0x41 /* not used at the moment */
#define IDENTIFIER_UV_RADIATION  0x47 /* not used at the moment */
#define LENGTH_TEMP_HUMIDITY     12
#define LENGTH_DATE_TIME         12 /* not used at the moment */
#define LENGTH_WIND              11 /* not used at the moment */
#define LENGTH_PRESSURE          8  /* not used at the moment */
#define LENGTH_RAIN              17 /* not used at the moment */
#define LENGTH_UV_RADIATION      5  /* not used at the moment */

#ifndef FALSE
#define FALSE 0
#define TRUE !FALSE
#endif

typedef struct OregonData {
  double          temperature[16];
  double          humidity[16];
  time_t          last_update[16];
  char            hidraw_dev[32];
  uint16_t        vendor_id;
  uint16_t        product_id;
  int             device_fd;
  int             thread_loop;
  int             thread_error;
  pthread_t       thread_id;
  pthread_mutex_t lock;
} OregonData;

OregonData gdata;

static const char *config_keys[] =
  {
    "Device",
    "VendorID",
    "ProductID"
  };
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static int check_device(OregonData *data, const char *path);

static void close_device(OregonData *data)
{
  close(data->device_fd);
}

static int open_device(OregonData *data, int max_retries)
{
  unsigned int retry_count;
  DIR *dir;
  struct dirent *dirent;
  char buff[32];
  char found;

  close_device(data);

  retry_count = 0;
  found = FALSE;
  while(found == FALSE && retry_count <= max_retries)
    {
      /* first try to open configured or last used device */
      if(strlen(data->hidraw_dev) > 0 &&
	 check_device(data, data->hidraw_dev) == 0)
	return 0;

      /* if it fails, scan each device */
      dir = opendir("/dev");
      if(dir != NULL)
	{
	  while(found == FALSE && (dirent = readdir(dir)) != NULL)
	    {
	      if(strncmp(dirent->d_name, "hidraw", 6) == 0)
		{
		  ssnprintf(buff, sizeof(buff), "/dev/%s", dirent->d_name);
		  if(check_device(data, buff) == 0)
		    {
		      sstrncpy(data->hidraw_dev, buff, sizeof(data->hidraw_dev));
		      found = TRUE;
		    }
		}
	    }
	  closedir(dir);
	}
      retry_count++;
      if(found == FALSE && retry_count <= max_retries)
	{
	  WARNING("oregon plugin: open failed, will retry in 5s");
	  sleep(5);
	}
    }
  if(found == FALSE)
    {
      ERROR("oregon plugin: open failed, aborting");

      return -1;
    }
  return 0;
}

static int oregon_checksum(const unsigned char *data, unsigned int len)
{
  unsigned int i;
  uint16_t checksum;

  assert(len >= 2);

  checksum = 0;
  for(i = 0; i < len - 2; i++)
    checksum += data[i];

  if(checksum != (data[i] + data[i + 1] * 256))
    return -1;

  return 0;
}

/* TODO: process rain/... measurements */
static void oregon_process_measurement(OregonData *data,
				       unsigned char *msg,
				       unsigned int len)
{
  if(len == LENGTH_TEMP_HUMIDITY && msg[1] == IDENTIFIER_TEMP_HUMIDITY)
    {				/* all data received -> parse it */
      unsigned int channel;
      double temperature;
      unsigned int humidity;

      if(oregon_checksum(msg, LENGTH_TEMP_HUMIDITY) == 0)
	{
	  channel = msg[2] & 0x0F;
	  temperature = (((uint16_t) (msg[4] & 0x7F)) << 8 | msg[3]) / (double) 10;
	  humidity = msg[5];
	  if(msg[4] & 0x80)
	    temperature = -temperature;

	  data->temperature[channel] = temperature;
	  data->humidity[channel] = humidity;
	  if(data->last_update[channel] == 0)
	    INFO("oregon plugin: now monitoring channel %i", channel);
	  data->last_update[channel] = time(NULL);
	  /* TODO: battery state */
	}
      else
	WARNING("oregon plugin: invalid checksum!");
    }
  else if(len == LENGTH_DATE_TIME && msg[1] == IDENTIFIER_DATE_TIME)
    {
      if(oregon_checksum(msg, LENGTH_DATE_TIME) == 0)
	{
	  /* TODO: battery state */
	}
      else
	WARNING("oregon plugin: invalid checksum!");
    }
}

static void *oregon_thread(OregonData *data)
{
  unsigned char buff[8];
  unsigned char msg[32];	/* longest msg is 17 bytes, 32 should be enough */
  unsigned int n;
  unsigned int p;
  unsigned int i;
  fd_set fdset;
  struct timeval tv;
  unsigned int no_data_times;

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
  pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

  p = 2;
  msg[0] = 0;			/* flags */
  msg[1] = 0;			/* identifier */
  no_data_times = 0;
  while(1)
    {
      FD_ZERO(&fdset);
      FD_SET(data->device_fd, &fdset);
      tv.tv_sec = 5;
      tv.tv_usec = 0;
      n = select(data->device_fd + 1, &fdset, NULL, NULL, &tv);
      if(n < 0)
	{
	  ERROR("oregon plugin: device read error: %s", strerror(errno));
	  ERROR("oregon plugin: reopening device");
	  if(open_device(data, 5) == 0)
	    {
	      p = 2;
	      msg[0] = 0;	/* flags */
	      msg[1] = 0;	/* identifier */
	      no_data_times = 0;
	      continue;
	    }
	  ERROR("oregon: reopen failed, aborting");
	  break;
	}
      else if(n == 0)
	{
	  no_data_times++;
	  if(no_data_times >= 200)
	    {
	      ERROR("oregon plugin: no data for 1000s, reopening device");
	      if(open_device(data, 5) == 0)
		{
		  p = 2;
		  msg[0] = 0;	/* flags */
		  msg[1] = 0;	/* identifier */
		  no_data_times = 0;
		  continue;
		}
	      ERROR("oregon: reopen failed, aborting");
	      break;
	    }
	}
      else
	{
	  no_data_times = 0;

	  n = read(data->device_fd, buff, sizeof(buff));
	  if(n <= 0)
	    {
	      WARNING("oregon plugin: read failed (%s), reopening device", strerror(errno));
	      if(open_device(data, 5) == 0)
		{
		  p = 2;
		  msg[0] = 0;	/* flags */
		  msg[1] = 0;	/* identifier */
		  continue;
		}
	      ERROR("oregon: reopen failed, aborting");
	      break;
	    }
	  if(n != 8 || n < buff[0] + (unsigned int) 1)
	    {
	      WARNING("oregon plugin: protocol error, reopening device");
	      if(open_device(data, 5) == 0)
		{
		  p = 2;
		  msg[0] = 0;	/* flags */
		  msg[1] = 0;	/* identifier */
		  continue;
		}
	      ERROR("oregon: reopen failed, aborting");
	      break;
	    }
	  for(i = 1; i <= buff[0]; i++)
	    {
	      msg[p] = buff[i];
	      oregon_process_measurement(data, msg, p+1);
	      if(p > 0 && msg[p - 1] == 0xFF &&
		 msg[p] == 0xFF)	/* we have found a separator (0xFF 0xFF) */
		p = 0;			/* -> restart at the beginning of the buffer */
	      else
		p++;
	      if(p >= sizeof(msg))	/* too much data for us to handle -> cycle through the buffer without  */
		p = 2;			/* overwriting flags & identifier */
	    }
	}

      pthread_mutex_lock(&data->lock);
      if(data->thread_loop <= 0)
	{
	  pthread_mutex_unlock(&data->lock);
	  break;
	}
      pthread_mutex_unlock(&data->lock);
    }

  return NULL;
}

static int start_thread(OregonData *data)
{
  int status;

  pthread_mutex_lock(&data->lock);

  if(data->thread_loop != 0)
    {
      pthread_mutex_unlock(&data->lock);
      return -1;
    }

  data->thread_loop = 1;
  data->thread_error = 0;
  status = pthread_create(&data->thread_id, NULL,
			  (void * (*)(void *)) oregon_thread, data);
  if(status != 0)
    {
      data->thread_loop = 0;
      ERROR("oregon plugin: Starting thread failed.");
      pthread_mutex_unlock(&data->lock);
      return -1;
    }

  pthread_mutex_unlock(&data->lock);
  return 0;
}

static int stop_thread(OregonData *data)
{
  int status;

  pthread_mutex_lock(&data->lock);

  if(data->thread_loop == 0)
    {
      pthread_mutex_unlock(&data->lock);
      return -1;
    }

  data->thread_loop = 0;
  pthread_mutex_unlock(&data->lock);

  pthread_cancel(data->thread_id);
  status = pthread_join(data->thread_id, NULL);
  if(status != 0)
    {
      ERROR("oregon plugin: Stopping thread failed.");
      status = -1;
    }

  return status;
}

static int check_device(OregonData *data, const char *path)
{
  struct hidraw_devinfo devinfo;
  unsigned int i;

  data->device_fd = open(path, O_RDWR);
  if(data->device_fd < 0)
    {
      ERROR("oregon plugin: failed to open device '%s': %s", path, strerror(errno));
      return -1;
    }

  if(ioctl(data->device_fd, HIDIOCGRAWINFO, &devinfo) < 0)
    {
      ERROR("oregon plugin: RAWINFO ioctl failed: %s", strerror(errno));
      close(data->device_fd);
      return -1;
    }
  if((uint16_t) devinfo.vendor != gdata.vendor_id || (uint16_t) devinfo.product != gdata.product_id)
    {
      for(i = 0; i < STATIC_ARRAY_SIZE(oregon_device_id); i++)
	if(oregon_device_id[i][0] == (uint16_t) devinfo.vendor &&
	   oregon_device_id[i][1] == (uint16_t) devinfo.product)
	  break;
      if(i == STATIC_ARRAY_SIZE(oregon_device_id))
	{
	  close(data->device_fd);
	  return -1;
	}
    }
  INFO("oregon plugin: using device '%s'", path);

  /* RMS300 works fine without sending this init packet but WMR100/200 may need it */
  if(write(data->device_fd, "\x20\x00\x08\x01\x00\x00\x00\x00", 8) != 8)
    WARNING("oregon plugin: write init failed: %s!", strerror(errno));

  return 0;
}

static int oregon_init(void)
{
  unsigned int i;

  for(i = 0; i < STATIC_ARRAY_SIZE(gdata.last_update); i++)
    gdata.last_update[i] = 0;
  gdata.thread_loop = 0;
  gdata.thread_error = 0;
  pthread_mutex_init(&gdata.lock, NULL);

  if(open_device(&gdata, 0) < 0)
    {
      ERROR("oregon plugin: no suitable device found");

      return -1;
    }

  if(start_thread(&gdata) != 0)
    {
      close_device(&gdata);
      return -1;
    }

  return 0;
}

static int oregon_config(const char *key, const char *value)
{
  if(strcasecmp(key, "Device") == 0)
    sstrncpy(gdata.hidraw_dev, value, sizeof(gdata.hidraw_dev));
  else if(strcasecmp(key, "VendorID") == 0)
    gdata.vendor_id = strtol(value, NULL, 0);
  else if(strcasecmp(key, "ProductID") == 0)
    gdata.product_id = strtol(value, NULL, 0);

  return 0;
}

static void submit(int channel, const char *type,
		   gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;
  char channel_s[8];

  ssnprintf(channel_s, sizeof(channel_s), "%d", channel);

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.plugin, "oregon", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, "", sizeof(vl.plugin_instance));
  sstrncpy(vl.type_instance, channel_s, sizeof(vl.type_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));

  plugin_dispatch_values(&vl);
}

static int oregon_read(void)
{
  time_t min_time;
  unsigned int i;
  OregonData *data;

  data = &gdata;

  min_time = time(NULL) - 90;	/* don't submit values older than 90s */
  pthread_mutex_lock(&data->lock);
  for(i = 0; i < STATIC_ARRAY_SIZE(data->temperature); i++)
    {
      if(data->last_update[i] > min_time)
	{
	  submit(i, "temperature", data->temperature[i]);
	  submit(i, "humidity", data->humidity[i]);
	}
      else
	{
	  if(data->last_update[i] != 0)
	    {
	      WARNING("oregon: channel %d: no update since %" PRId64 "s",
		      i, time(NULL) - data->last_update[i]);
	      if((time(NULL) - data->last_update[i]) >= 300)
		{
		  WARNING("oregon: channel %d: disabling ", i);
		  data->last_update[i] = 0;
		}
	    }
	}
    }
  pthread_mutex_unlock(&data->lock);

  return 0;
}

static int oregon_shutdown(void)
{
  INFO("oregon plugin: Shutting down thread.");
  if(stop_thread(&gdata) < 0)
    {
      WARNING("oregon plugin: Failed to stop thread.");
      return -1;
    }
  INFO("oregon plugin: Thread stopped.");

  return 0;
}

void module_register(void)
{
  plugin_register_config("oregon", oregon_config,
			 config_keys, config_keys_num);
  plugin_register_init("oregon", oregon_init);
  plugin_register_read("oregon", oregon_read);
  plugin_register_shutdown("oregon", oregon_shutdown);
}
