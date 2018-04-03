/**
 * collectd - src/uuid.c
 * Copyright (C) 2007  Red Hat Inc.
 * Copyright (C) 2015  Ruben Kerkhof
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
 *   Dan Berrange <berrange@redhat.com>
 *   Richard W.M. Jones <rjones@redhat.com>
 *
 * Derived from UUID detection code by Dan Berrange <berrange@redhat.com>
 * http://hg.et.redhat.com/virt/daemons/spectre--devel?f=f6e3a1b06433;file=lib/uuid.c
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#if HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif

#define UUID_RAW_LENGTH 16
#define UUID_PRINTABLE_COMPACT_LENGTH (UUID_RAW_LENGTH * 2)
#define UUID_PRINTABLE_NORMAL_LENGTH (UUID_PRINTABLE_COMPACT_LENGTH + 4)

static char *uuidfile = NULL;

static const char *config_keys[] = {"UUIDFile"};

static int looks_like_a_uuid(const char *uuid) {
  int len;

  if (!uuid)
    return 0;

  len = strlen(uuid);

  if (len < UUID_PRINTABLE_COMPACT_LENGTH)
    return 0;

  while (*uuid) {
    if (!isxdigit((int)*uuid) && *uuid != '-')
      return 0;
    uuid++;
  }
  return 1;
}

static char *uuid_parse_dmidecode(FILE *file) {
  char line[1024];

  while (fgets(line, sizeof(line), file) != NULL) {
    char *fields[4];
    int fields_num;

    strstripnewline(line);

    /* Look for a line reading:
     *   UUID: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
     */
    fields_num = strsplit(line, fields, STATIC_ARRAY_SIZE(fields));
    if (fields_num != 2)
      continue;

    if (strcmp("UUID:", fields[0]) != 0)
      continue;

    if (!looks_like_a_uuid(fields[1]))
      continue;

    return strdup(fields[1]);
  }
  return NULL;
}

static char *uuid_get_from_dmidecode(void) {
  FILE *dmidecode = popen("dmidecode -t system 2>/dev/null", "r");
  char *uuid;

  if (!dmidecode)
    return NULL;

  uuid = uuid_parse_dmidecode(dmidecode);

  pclose(dmidecode);
  return uuid;
}

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__)
static char *uuid_get_from_sysctlbyname(const char *name) {
  char uuid[UUID_PRINTABLE_NORMAL_LENGTH + 1];
  size_t len = sizeof(uuid);
  if (sysctlbyname(name, &uuid, &len, NULL, 0) == -1)
    return NULL;
  return strdup(uuid);
}
#elif defined(__OpenBSD__)
static char *uuid_get_from_sysctl(void) {
  char uuid[UUID_PRINTABLE_NORMAL_LENGTH + 1];
  size_t len = sizeof(uuid);
  int mib[2];

  mib[0] = CTL_HW;
  mib[1] = HW_UUID;

  if (sysctl(mib, 2, uuid, &len, NULL, 0) == -1)
    return NULL;
  return strdup(uuid);
}
#endif

static char *uuid_get_from_file(const char *path) {
  FILE *file;
  char uuid[UUID_PRINTABLE_NORMAL_LENGTH + 1] = "";

  file = fopen(path, "r");
  if (file == NULL)
    return NULL;

  if (!fgets(uuid, sizeof(uuid), file)) {
    fclose(file);
    return NULL;
  }
  fclose(file);
  strstripnewline(uuid);

  return strdup(uuid);
}

static char *uuid_get_local(void) {
  char *uuid;

  /* Check /etc/uuid / UUIDFile before any other method. */
  if ((uuid = uuid_get_from_file(uuidfile ? uuidfile : "/etc/uuid")) != NULL)
    return uuid;

#if defined(__APPLE__)
  if ((uuid = uuid_get_from_sysctlbyname("kern.uuid")) != NULL)
    return uuid;
#elif defined(__FreeBSD__)
  if ((uuid = uuid_get_from_sysctlbyname("kern.hostuuid")) != NULL)
    return uuid;
#elif defined(__NetBSD__)
  if ((uuid = uuid_get_from_sysctlbyname("machdep.dmi.system-uuid")) != NULL)
    return uuid;
#elif defined(__OpenBSD__)
  if ((uuid = uuid_get_from_sysctl()) != NULL)
    return uuid;
#elif defined(__linux__)
  if ((uuid = uuid_get_from_file("/sys/class/dmi/id/product_uuid")) != NULL)
    return uuid;
#endif

  if ((uuid = uuid_get_from_dmidecode()) != NULL)
    return uuid;

#if defined(__linux__)
  if ((uuid = uuid_get_from_file("/sys/hypervisor/uuid")) != NULL)
    return uuid;
#endif

  return NULL;
}

static int uuid_config(const char *key, const char *value) {
  if (strcasecmp(key, "UUIDFile") == 0) {
    char *tmp = strdup(value);
    if (tmp == NULL)
      return -1;
    sfree(uuidfile);
    uuidfile = tmp;
    return 0;
  }

  return 1;
}

static int uuid_init(void) {
  char *uuid = uuid_get_local();

  if (uuid) {
    hostname_set(uuid);
    sfree(uuid);
    return 0;
  }

  WARNING("uuid: could not read UUID using any known method");
  return 0;
}

void module_register(void) {
  plugin_register_config("uuid", uuid_config, config_keys,
                         STATIC_ARRAY_SIZE(config_keys));
  plugin_register_init("uuid", uuid_init);
}
