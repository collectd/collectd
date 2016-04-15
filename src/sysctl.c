#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "sysctl.h"

static _Bool sysctl_have_instances = 0;

int trim(char *src, char **dstp)
{
  int inspace = 0;
  int i = 0, j = 0;
  int ret;
  char *dst = NULL;
  ssize_t len = 0;

  /* sanity check */
  if (! src)
    {
      ret = -1;
      goto end;
    }

  // remove leading spaces
  while (src && *src && isspace(*src))
    src++;

  len = strlen(src);

  while (isspace(src[len - 1]))
    len--;

  dst = malloc(len + 1);
  if (! dst)
    {
      ret = -2;
      goto end;
    }

  while (i < len)
    {
      if (inspace)
        {
          while (src[i] && isspace(src[i]))
            i++;

          inspace = 0;
        }
      else
        {
          if (isspace(src[i]))
            {
              inspace = 1;
              dst[j++] = ' ';
              i++;
            }
          else
            {
              dst[j++] = src[i++];
            }
        }
    }

  dst[j] = 0;
  ret = 0;

 end:
  if (dstp)
    *dstp = dst;

  return ret;
}

int slashdot(char * p, char old, char new) {
  int warned = 1;

  p = strpbrk(p, "/.");
  if (!p)
  {
    return 1;
  }

  if (*p == new)
  {
    return 1;
  }

  while (p) {
    char c = *p;
    if ((*(p + 1) == '/' || *(p + 1) == '.') && warned) {
      printf("separators should not be repeated: %s\n", p);
      warned = 0;
    }
    if (c == old)
    {
      *p = new;
    }
    if (c == new)
    {
      *p = old;
    }
    p = strpbrk(p + 1, "/.");
  }
  return 0;
}

static void submit_gauge(const char *type, const char *type_inst,
                         gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;
  vl.values = values;
  vl.values_len = 1;
  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.plugin, "sysctl", sizeof(vl.plugin));
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_inst != NULL)
  {
    sstrncpy(vl.type_instance, type_inst, sizeof(vl.type_instance));
  }

  plugin_dispatch_values(&vl);
}

int sysctl_read(user_data_t *user_data)
{
  struct sysctl_t *st;
  int sysctl_value = 0;
  st = user_data->data;

#ifdef __FreeBSD__
  size_t len = sizeof(int);
  /* no need to split on BSD */
  sysctlbyname(st->name, &sysctl_value, &len, NULL, 0);
#endif

#ifdef __linux__
#define PROC_PATH "/proc/sys/"
#define MAX_BUF 4096
  FILE *fp;
  struct stat ts;
  char *tmpname = malloc(strlen(st->name) + strlen(PROC_PATH) +2);
  char *tmpout = malloc(MAX_BUF);
  char *trimmed=NULL, *token = NULL, *tmp_buf;
  int i = 0, ret;

  strcpy(tmpname, PROC_PATH);
  strcat(tmpname, st->name);
  slashdot(tmpname + strlen(PROC_PATH), '.', '/');

  if (stat(tmpname, &ts) < 0) {
    WARNING("could not stat %s : %s", tmpname, strerror(errno));
    return 1;
  }

  fp = fopen(tmpname, "r");
  if (!fp)
  {
    WARNING("could not open %s for reading : %s", tmpname, strerror(errno));
    return 1;
  }
  if (fgets(tmpout, MAX_BUF, fp)) {
    /*
      tmpout may be (part after the = of course)
        fs.mqueue.queues_max = 256
      or
        fs.file-nr = 3424       0       610991
      so we want to split the fields accordingly
    */
    ret = trim(tmpout, &trimmed);
    if (ret != 0)
    {
      WARNING("could not process string");
      return 1;
    }

    tmp_buf = trimmed;
    while(i < st->index)
    {
      token = strsep(&tmp_buf, " ");
      i++;
    }

    if (token != NULL)
    {
      sysctl_value = atoi(token);
    }
    else
    {
      WARNING("called atoi() on NULL; failing");
      return 1;
    }
  }
  else
  {
    WARNING("Reading %s failed", tmpname);
    return 1;
  }
  sfree(tmpname);
  sfree(tmpout);
  sfree(trimmed);
  fclose(fp);
#endif

  submit_gauge("gauge", st->name, sysctl_value);
  return 0;
}

static void sysctl_free(struct sysctl_t *st)
{
  if (st == NULL)
  {
    return;
  }

  sfree (st->name);
}

static int sysctl_add_read_callback (struct sysctl_t *st)
{
  user_data_t ud;
  char callback_name[3*DATA_MAX_NAME_LEN];
  int status;

  memset (&ud, 0, sizeof (ud));
  ud.data = st;
  ud.free_func = (void *) sysctl_free;

  assert (st->name != NULL);
  ssnprintf (callback_name, sizeof (callback_name), "sysctl/%s", st->name);

  status = plugin_register_complex_read (/* group = */ "sysctl",
      /* name      = */ callback_name,
      /* callback  = */ sysctl_read,
      /* interval  = */ NULL,
      /* user_data = */ &ud);
  return (status);
} /* int sysctl_add_read_callback */

static int config_add_instance(oconfig_item_t *ci)
{
  struct sysctl_t *st;
  int i;
  int status = 0;

  /* Disable automatic generation of default instance in the init callback. */
  sysctl_have_instances = 1;

  st = malloc (sizeof (*st));
  if (st == NULL)
  {
    ERROR ("sysctl plugin: malloc failed.");
    return (-1);
  }

  memset (st, 0, sizeof (*st));
  st->name = NULL;
  st->index = 1;

  if (strcasecmp (ci->key, "Plugin") == 0)
  {
    st->name = sstrdup ("__legacy__");
  }
  else
  {
    status = cf_util_get_string (ci, &st->name);
  }

  if (status != 0)
  {
    sfree (st);
    return (status);
  }
  assert (st->name != NULL);

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Key", child->key) == 0)
      status = cf_util_get_string (child, &st->name);
    else if (strcasecmp ("Index", child->key) == 0)
      status = cf_util_get_int (child, &st->index);
    else
    {
      WARNING ("sysctl plugin: Option `%s' not allowed here.",
          child->key);
      status = -1;
    }

    if (status != 0)
    {
      break;
    }
  }

  if (status == 0)
  {
    status = sysctl_add_read_callback (st);
  }

  if (status != 0)
  {
    sysctl_free(st);
    return (-1);
  }

  return (0);
}

static int sysctl_config (oconfig_item_t *ci)
{
  int status = 0;
  _Bool have_instance_block = 0;
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Instance", child->key) == 0)
    {
      config_add_instance (child);
      have_instance_block = 1;
    }
    else if (!have_instance_block)
    {
      /* Non-instance option: Assume legacy configuration (without <Instance />
       * blocks) and call config_add_instance() with the <Plugin /> block. */
      return (config_add_instance (ci));
    }
    else
    {
      WARNING ("sysctl plugin: The configuration option "
          "\"%s\" is not allowed here. Did you "
          "forget to add an <Instance /> block "
          "around the configuration?",
          child->key);
    }
  } /* for (ci->children) */

  return (status);
}

static int sysctl_init (void)
{
  struct sysctl_t *st;
  int status;

  if (sysctl_have_instances)
  {
    return (0);
  }

  /* No instances were configured, lets start a default instance. */
  st = malloc (sizeof (*st));
  if (st == NULL)
  {
    return (ENOMEM);
  }
  memset(st, 0, sizeof (*st));
  st->name = sstrdup("__legacy__");
  st->index = 1;

  status = sysctl_add_read_callback(st);
  if (status == 0)
  {
    sysctl_have_instances = 1;
  }
  else
  {
    sysctl_free(st);
  }

  return (status);
} /* int sysctl_init */

void module_register(void)
{
  plugin_register_complex_config("sysctl", sysctl_config);
  plugin_register_init("sysctl", sysctl_init);
}
