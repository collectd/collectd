/**
 * collectd - src/coretemp.c, based on src/protocols.c
 * Copyright (C) 2016       Slava Polyakov
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
 *   Slava Polyakov <sigsegv0x0b at gmail.com>
 **/
#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <search.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#if !KERNEL_LINUX
#error "No applicable input method."
#endif

#define CORETEMP_PATH "/sys/devices/platform/"
#define CORETEMP_NAME "coretemp."
#define CORETEMP_STRLEN (sizeof(CORETEMP_NAME)-1)

static const char *config_keys[] = {
  "MaxValues",			// [ true ], only, none
  "ValuesPercentage",		// [ true ], false
  "ValuesDegrees",		// [ true ], false
};

// global config vars :)
int             MaxValues = 0;
int             ValuesPercentage = 0;
int             ValuesDegrees = 1;

struct coretemp_core
{
  unsigned int    tjmax;
  unsigned int    socket;
  unsigned int    core;
  unsigned int    hwmon;
  int             input;
  char           *label;
};

struct coretemp_core **c = 0;
unsigned int    core_count = 0;

// free the coretemp_core struct list
static int coretemp_cleanup()
{
  int             i;
  for (i = 0; i < core_count; i++)
  {
    if (c[i]->input)
      close(c[i]->input);
    if (c[i]->label)
      free(c[i]->label);
    free(c[i]);
  }
  if (c)
    free(c);
  c = 0;

  return 0;
}

static int coretemp_findcores()
{
  DIR            *dp = 0, *dp2 = 0, *dp3 = 0, *dp4 = 0;
  struct dirent  *de, *de2, *de3, *de4;
  unsigned int    socket = 0;
  int             l, core, idx, i, hwmon = 0;
  char            core_name[100];
  char           *s, *b, t;
  void           *realloc_sucks;
  FILE           *f = 0;

  if (chdir(CORETEMP_PATH))
  {
    ERROR("coretemp plugin: unable to find coretemp path (%s)",
	  CORETEMP_PATH);
    return (-1);
  }

  if (!(dp = opendir(".")))
  {
    ERROR("coretemp plugin: unable to diropen() coretemp path (%s)",
	  CORETEMP_PATH);
    return (-1);
  }

  while ((de = readdir(dp)))
  {
    if (de->d_name[0] == '.')
      continue;
    if (!strncmp(de->d_name, CORETEMP_NAME, CORETEMP_STRLEN))
    {
      socket = atoi(&de->d_name[CORETEMP_STRLEN]);
      if (chdir(de->d_name))
      {
	ERROR
	    ("coretemp plugin: unable to chdir into [ %s ] for coretemp data",
	     de->d_name);
	goto error_exit;
      }
      dp2 = opendir(".");
      int             hwmon_mode = 1;
      while ((de2 = readdir(dp2)))
      {
	if (de2->d_name[0] == '.')
	  continue;
	if (!(s = strchr(&de2->d_name[4], '_')))
	  continue;
	*s = 0;
	if (!strncmp(de2->d_name, "temp", 4))
	{
	  hwmon_mode = 0;
	  break;
	}
      }
      rewinddir(dp2);

      if (hwmon_mode)
      {
	if (chdir("hwmon"))
	{
	  ERROR
	      ("coretemp plugin: unable to figure out the coretemp format");
	  goto error_exit;
	}
	dp3 = opendir(".");
      }
      else
      {
	rewinddir(dp2);
	dp4 = dp2;
	dp3 = 0;
	hwmon = 0;
	goto no_hwmon;
      }

      while (dp3 && (de3 = readdir(dp3)))
      {
	if (de3->d_name[0] == '.')
	  continue;
	if (strncmp(de3->d_name, "hwmon", 5))
	  continue;
	hwmon = atoi(&de3->d_name[5]);
	if (chdir(de3->d_name))
	{
	  ERROR
	      ("coretemp plugin: unable to chdir into [ %s ] for coretemp data",
	       de3->d_name);
	  goto error_exit;
	}
	dp4 = opendir(".");

      no_hwmon:
	while ((de4 = readdir(dp4)))
	{
	  if (de4->d_name[0] == '.')
	    continue;
	  if ((l = strlen(de4->d_name)) < 5)
	    continue;
	  if (!(s = strchr(&de4->d_name[4], '_')))
	    continue;

	  b = s;
	  t = *b;
	  *s = 0;
	  core = atoi(&de4->d_name[4]);
	  idx = -1;
	  for (i = 0; i < core_count; i++)
	  {
	    if (c[i]->socket == socket && c[i]->core == core
		&& c[i]->hwmon == hwmon)
	      idx = i;
	  }

	  if (idx < 0)
	  {
	    realloc_sucks = c;
	    if (NULL ==
		(c =
		 realloc(c,
			 sizeof(struct coretemp_core *) *
			 (core_count + 1))))
	    {
	      c = realloc_sucks;
	      ERROR("coretemp plugin: realloc failed");
	      goto error_exit;
	    }
	    if (NULL ==
		(c[core_count] = malloc(sizeof(struct coretemp_core))))
	    {
	      ERROR("coretemp plugin: malloc failed");
	      goto error_exit;
	    }
	    memset(c[core_count], 0, sizeof(struct coretemp_core));
	    c[core_count]->socket = socket;
	    c[core_count]->core = core;
	    c[core_count]->hwmon = hwmon;
	    idx = core_count;
	    ++core_count;
	  }

	  ++s;
	  *b = t;
	  if (!strcmp(s, "max"))
	  {
	    if (NULL == (f = fopen(de4->d_name, "r")))
	    {
	      ERROR("coretemp plugin: unable to open (%s)", de4->d_name);
	      goto error_exit;
	    }

	    if (!fgets(core_name, sizeof(core_name) - 1, f))
	    {
	      ERROR("coretemp plugin: unable to read tjmax");
	      goto error_exit;
	    }
	    fclose(f);
	    f = 0;
	    c[idx]->tjmax = atoi(core_name);
	  }
	  else if (!strcmp(s, "label"))
	  {
	    if (NULL == (f = fopen(de4->d_name, "r")))
	    {
	      ERROR("coretemp plugin: unable to open (%s)", de4->d_name);
	      goto error_exit;
	    }
	    if (!fgets(core_name, sizeof(core_name) - 1, f))
	    {
	      ERROR("coretemp plugin: unable to read core label");
	      goto error_exit;
	    }
	    fclose(f);
	    f = 0;
	    i = strlen(core_name);
	    core_name[i - 1] = 0;
	    // c[idx]->label = malloc(sizeof(char)*i);
	    // strncpy(c[idx]->label, core_name, i-1);
	    if (asprintf(&c[idx]->label, "%s", core_name) == -1)
	    {
	      c[idx]->label = 0;
	      ERROR
		  ("coretemp plugin: unable to allocate memory for label");
	      goto error_exit;
	    }
	    s = c[idx]->label;
	    while (*s)
	    {
	      if (*s == ' ')
		*s = '_';
	      ++s;
	    }
	  }
	  else if (!strcmp(s, "input"))
	  {
	    c[idx]->input = open(de4->d_name, O_RDONLY);
	  }
	}
	closedir(dp4);
	dp4 = 0;

	if (chdir(".."))
	{
	  ERROR
	      ("coretemp plugin: unable to parse coretemp directory structure");
	  goto error_exit;
	}

      }
      if (dp3)
      {
	closedir(dp3);
	dp3 = 0;
      }
    }

  };
  closedir(dp);
  dp = 0;

#if COLLECT_DEBUG
  DEBUG("coretemp: found cores %d", core_count);
  for (i = 0; i < core_count; i++)
  {
    DEBUG("coretemp: socket=%d core=%d tjmax=%d label=%s",
	  c[i]->socket, c[i]->core, c[i]->tjmax, c[i]->label);
  }
#endif				/* COLLECT_DEBUG */

  return 0;


  // if we fail, go here and free up the cores
error_exit:
  coretemp_cleanup();

  if (f)
  {
    fclose(f);
    f = 0;
  }
  if (dp)
  {
    closedir(dp);
    dp = 0;
  }
  if (dp2)
  {
    closedir(dp2);
    dp2 = 0;
  }
  if (dp3)
  {
    closedir(dp3);
    dp3 = 0;
  }
  if (dp4)
  {
    closedir(dp4);
    dp4 = 0;
  }

  ERROR("coretemp plugin: unable to initialize");
  return (-1);

}



static void
coretemp_submit(const char *temp_type, const char *str_core_id,
		const char *str_value)
{
  value_t         values[1];
  value_list_t    vl = VALUE_LIST_INIT;

  if (parse_value(str_value, values, DS_TYPE_GAUGE))
  {
    ERROR("coretemp plugin: Parsing string as integer failed: %s",
	  str_value);
    return;
  }

  vl.values = values;
  vl.values_len = 1;

  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.plugin, "coretemp", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, temp_type, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "temperature", sizeof(vl.type));
  sstrncpy(vl.type_instance, str_core_id, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static int coretemp_read(void)
{
  int             pct, max_pct = 0, max_temp = 0;
  int             idx = 0;
  char            buf[100];
  int             temp;
  char            temp_str[20];

#if COLLECT_DEBUG
  DEBUG("coretemp: checking %d cores\n", core_count);
#endif

  for (idx = 0; idx < core_count; idx++)
  {
    lseek(c[idx]->input, 0, SEEK_SET);
    pct = read(c[idx]->input, buf, sizeof(buf) - 1);
    buf[pct] = 0;

    temp = atoi(buf);

    pct = temp / (c[idx]->tjmax / 100);

    if (max_pct < pct)
      max_pct = pct;
    if (max_temp < temp)
      max_temp = temp;

#if COLLECT_DEBUG
    DEBUG
	("coretemp: MaxValues=%d ValuesDegrees=%d ValuesPercentage=%d idx=%d core=%d socket=%d hwmon=%d tjmax=%d temp=%d pct=%d label=%s",
	 MaxValues, ValuesDegrees, ValuesPercentage, idx, c[idx]->core,
	 c[idx]->socket, c[idx]->hwmon, c[idx]->tjmax, temp / 1000,
	 pct, c[idx]->label);
    // fprintf(fdopen(0, "w"), "coretemp: idx=%d core=%d socket=%d
    // tjmax=%d temp=%d pct=%d label=%s\n", idx, c[idx]->core,
    // c[idx]->socket, c[idx]->tjmax, temp/1000, pct, c[idx]->label);
#endif				/* COLLECT_DEBUG */

    if (MaxValues == 2)
      continue;

    if (ValuesDegrees)
    {
      ssnprintf(temp_str, sizeof(temp_str) - 1, "%d", temp / 1000);
      coretemp_submit("temp", c[idx]->label, temp_str);
    }

    if (ValuesPercentage)
    {
      ssnprintf(temp_str, sizeof(temp_str) - 1, "%d", pct);
      coretemp_submit("percent", c[idx]->label, temp_str);
    }


  }

  if (MaxValues)
  {
    if (ValuesDegrees)
    {
      ssnprintf(temp_str, sizeof(temp_str) - 1, "%d", max_temp / 1000);
      coretemp_submit("temp", "max", temp_str);
    }

    if (ValuesPercentage)
    {
      ssnprintf(temp_str, sizeof(temp_str) - 1, "%d", max_pct);
      coretemp_submit("percent", "max", temp_str);
    }
  }

  return 0;
}

static int coretemp_config(const char *key, const char *value)
{
  if (!strcasecmp(key, "MaxValues"))
  {
    if (!strcasecmp(value, "true"))
      MaxValues = 1;
    else if (!strcasecmp(value, "yes"))
      MaxValues = 1;

    if (!strcasecmp(value, "no"))
      MaxValues = 0;
    else if (!strcasecmp(value, "false"))
      MaxValues = 0;
    else if (!strcasecmp(value, "none"))
      MaxValues = 0;

    if (!strcasecmp(value, "only"))
      MaxValues = 2;
  }

  if (!strcasecmp(key, "ValuesPercentage"))
    ValuesPercentage = IS_TRUE(value);
  if (!strcasecmp(key, "ValuesDegrees"))
    ValuesDegrees = IS_TRUE(value);

  return 0;
}

void module_register(void)
{

  plugin_register_config("coretemp", coretemp_config, config_keys,
			 STATIC_ARRAY_SIZE(config_keys));
  if (!ValuesPercentage && !ValuesDegrees)
  {
    ERROR
	("coretemp plugin: nothing to report! ValuesPercentage=false and ValuesDegrees=false, set at least one to true");
  }
  else
    coretemp_findcores();	// otherwise why waste cycles?

  plugin_register_read("coretemp", coretemp_read);
  plugin_register_shutdown("coretemp", coretemp_cleanup);
}				/* void module_register */
