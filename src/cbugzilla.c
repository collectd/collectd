/**
 * collectd - src/curl_bugzilla.c
 * Copyright (C) 2006-2009  Florian octo Forster
 * Copyright (C) 2009       Aman Gupta
 * Copyright (C) 2013       Jan Matejka
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
 *   Florian octo Forster <octo at verplant.org>
 *   Aman Gupta <aman at tmm1.net>
 *   Jan Matejka <yac at gentoo.org>
 **/
#include <sys/stat.h>
#include <errno.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include <libcbugzilla/cb.h>
#include <libcbugzilla/config.h>

#define CCBERROR(...) ERROR ("cbugzilla plugin: " __VA_ARGS__)
#define CCBINFO(...) INFO ("cbugzilla plugin: " __VA_ARGS__)
#define CCBWARNING(...) WARNING ("cbugzilla plugin: " __VA_ARGS__)

struct namedmcd_s;
typedef struct namedcmd_s *namedcmd_t;
struct namedcmd_s
{
  char *namedcmd;
  int response_time;

  namedcmd_t next;
};

struct bugzilla_s;
typedef struct bugzilla_s *bugzilla_t;
struct bugzilla_s /* {{{ */
{
  char *instance;

  int   verify_peer;
  int   verify_host;
  namedcmd_t  namedcmd;
  cbi_t cbi;

  int http_log;

  bugzilla_t next;
}; /* }}} */

char *ccb_getcwd()
{
  int len = 100;
  char *cerr, *cwd=NULL;

  while(1) {
    if(cwd != NULL)
      sfree(cwd);

    cwd = calloc(len, sizeof(char));
    cerr = getcwd(cwd, len);
    if(cerr != NULL)
      break;

    if(errno != ERANGE)
    {
      CCBERROR("getcwd: %s", strerror(errno));
      return NULL;
    }

    len+=100;
  }

  return cwd;
}

/*
 * Global variables;
 */
static bugzilla_t bugzies_g = NULL;

static char *basedir;
static char *basepath;

static void ccb_namedcmd_free (namedcmd_t ncmd)
{
  if(ncmd == NULL)
    return;

  if (ncmd->namedcmd != NULL)
    sfree(ncmd->namedcmd);

  ccb_namedcmd_free(ncmd->next);
  sfree(ncmd);
}

int ccb_mkdir(const char *path)
{
  if(path == NULL)
  {
    CCBERROR("mkdir failed: invalid argument");
    return -EINVAL;
  }

  char buf[1000];
  getcwd(buf, 100);

  errno = 0;
  if(0 != mkdir(path, S_IRWXU | S_IRWXG))
    if(errno != EEXIST) {
      CCBERROR("mkdir failed: %s, %s", path, strerror(errno));
      return (-1);
    }
  return 0;
}

char * ccb_bugzilla_get_basepath(bugzilla_t self, char *suffix)
{
  if(basepath == NULL)
  {
    CCBERROR("basepath is NULL");
    return NULL;
  }

  int len, err;
  len = strlen(basepath) + strlen(self->instance) +2;
  if(suffix != NULL)
    len += strlen(suffix) +1;

  char *bp = calloc(len, sizeof(char));
  if(bp == NULL)
  {
    CCBERROR("calloc failed, %s:%d", __FILE__, __LINE__);
    return NULL;
  }

  if(0 > (err = snprintf(bp, len, "%s/%s", basepath, self->instance)))
  {
    CCBERROR("sprintf failed: %s:%d %d", __FILE__, __LINE__, err);
    return NULL;
  }

  if(suffix != NULL)
  {
    bp = strcat(bp, "/");
    bp = strncat(bp, suffix, strlen(suffix));
  }

  return bp;
}

static void ccb_bugzilla_free (bugzilla_t b) /* {{{ */
{
  if (b == NULL)
    return;

  if (b->cbi != NULL)
    b->cbi->free(b->cbi);
  b->cbi = NULL;

  ccb_namedcmd_free(b->namedcmd);

  sfree (b->instance);

  ccb_bugzilla_free (b->next);
  sfree (b);
} /* }}} void ccb_bugzilla_free */

static int ccb_cbi_set_string (const char *name, cbi_t cbi, /* {{{ */
    int (*fn)(cbi_t cbi, const char *c), oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    CCBWARNING ("`%s' needs exactly one string argument.", name);
    return (-1);
  }

  if(CB_SUCCESS != fn(cbi, ci->values[0].value.string))
    return (-1);

  return (0);
} /* }}} int ccb_cbi_set_string */

static int ccb_cbi_set_boolean (const char *name, cbi_t cbi, /* {{{ */
    int (*fn)(cbi_t cbi, const int i), oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
  {
    CCBWARNING ("`%s' needs exactly one boolean argument.", name);
    return (-1);
  }

  if(CB_SUCCESS != fn(cbi, (ci->values[0].value.boolean ? 1 : 0)))
    return (-1);

  return (0);
} /* }}} int ccb_cbi_set_boolean */


static int ccb_config_set_boolean (const char *name, /* {{{ */
    int *dest, oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
  {
    CCBWARNING ("`%s' needs exactly one boolean argument.", name);
    return (-1);
  }

  *dest = (ci->values[0].value.boolean ? 1 : 0);

  return (0);
} /* }}} int ccb_config_set_boolean */

int ccb_config_add_namedcmd(bugzilla_t b, oconfig_item_t *ci) /* {{{ */
{
  int i;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    CCBWARNING ("`NamedCmd' needs exactly one string argument.");
    return (-1);
  }

  namedcmd_t ncmd;
  ncmd = calloc(1, sizeof(*ncmd));
  if(ncmd == NULL)
  {
    CCBWARNING ("calloc failed");
    return (-1);
  }

  ncmd->namedcmd = strdup(ci->values[0].value.string);
  if(ncmd->namedcmd == NULL)
  {
    CCBWARNING("ncmd->namedcmd = strdup failed");
    return (-1);
  }

  ncmd->response_time = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if(strcasecmp ("MeasureResponseTime", child->key) == 0) {
      ccb_config_set_boolean("NamedCmd.MeasureResponseTime",
        &ncmd->response_time, child);
    }else{
      CCBWARNING ("Option `%s' not allowed in `Bugzilla.NamedCmd`",
        child->key);
    }
  }

  /* Add the new namedcmd to the linked list */
  if (b->namedcmd == NULL)
    b->namedcmd = ncmd;
  else
  {
    namedcmd_t prev;

    prev = b->namedcmd;
    while ((prev != NULL) && (prev->next != NULL))
      prev = prev->next;
    prev->next = ncmd;
  }

  return (0);
} /* ccb_config_add_namedcmd */

static int ccb_config_add_bugzie (oconfig_item_t *ci) /* {{{ */
{
  bugzilla_t b;
  int status;
  int i;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    CCBWARNING ("`Bugzilla' blocks need exactly one string argument.");
    return (-1);
  }

  b =  calloc (1, sizeof (*b));
  b->http_log = 0;

  if (b == NULL)
  {
    CCBERROR ("calloc failed: %s:%d", __FILE__, __LINE__);
    return (-1);
  }
  b->cbi = cbi_new();

  b->instance = strdup (ci->values[0].value.string);
  if (b->instance == NULL)
  {
    CCBERROR ("strdup failed: %s:%d", __FILE__, __LINE__);
    sfree (b);
    return (-1);
  }

  int got_url=0;
  /* Process all children */
  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("URL", child->key) == 0) {
      status = ccb_cbi_set_string ("URL", b->cbi,
        b->cbi->set_url, child);
      got_url=1;
    }else if (strcasecmp ("User", child->key) == 0)
      status = ccb_cbi_set_string ("User", b->cbi,
        b->cbi->set_auth_user, child);
    else if (strcasecmp ("Password", child->key) == 0)
      status = ccb_cbi_set_string ("Password", b->cbi,
        b->cbi->set_auth_pass, child);
    else if (strcasecmp ("VerifyPeer", child->key) == 0)
      status = ccb_cbi_set_boolean ("VerifyPeer", b->cbi,
        b->cbi->set_verify_peer, child);
    else if (strcasecmp ("VerifyHost", child->key) == 0)
      status = ccb_cbi_set_boolean ("VerifyHost", b->cbi,
        b->cbi->set_verify_host, child);
    else if (strcasecmp ("HTTPLog", child->key) == 0)
    {
      if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
      {
        CCBWARNING ("`%s' needs exactly one boolean argument.", "HTTPLog");
        status = -1;
      }else{
        b->http_log = ci->values[0].value.boolean ? 1:0;
      }
    }
    else if (strcasecmp ("NamedCmd", child->key) == 0)
      status = ccb_config_add_namedcmd(b, child);
    else
    {
      CCBWARNING ("Option `%s' not allowed in `Bugzilla`.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  /* Additionial sanity checks. */
  if(0 == got_url)
  {
    CCBWARNING ("`URL' missing in `b' block.");
    status = -1;
  }

  if(NULL == b->namedcmd)
  {
    CCBWARNING ("`NamedCmd' missing in `b' block.");
    status = -1;
  }

  if (status != 0)
  {
    ccb_bugzilla_free (b);
    return (status);
  }

  /* Add the new b to the linked list */
  if (bugzies_g == NULL)
    bugzies_g = b;
  else
  {
    bugzilla_t prev;

    prev = bugzies_g;
    while ((prev != NULL) && (prev->next != NULL))
      prev = prev->next;
    prev->next = b;
  }

  return (0);
} /* }}} int ccb_config_add_bugzie */

static int ccb_config (oconfig_item_t *ci) /* {{{ */
{
  int success;
  int errors;
  int status;
  int i;

  success = 0;
  errors = 0;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Bugzilla", child->key) == 0)
    {
      status = ccb_config_add_bugzie (child);
      if (status == 0)
        success++;
      else
        errors++;
    }
    else
    {
      CCBWARNING ("Option `%s' not allowed here.", child->key);
      errors++;
    }
  }

  if ((success == 0) && (errors > 0))
  {
    CCBERROR ("All statements failed.");
    return (-1);
  }

  return (0);
} /* }}} int bugzilla_config */

static int ccb_init (void) /* {{{ */
{
  if (bugzies_g == NULL)
  {
    CCBINFO ("No Bugzillas have been defined.");
    return (-1);
  }

  basedir = ccb_getcwd();
  // FIXME: get basedir in safer way than with getcwd()
  if(basedir == NULL)
  {
    CCBERROR("couldn't initialize basedir");
    return (-1);
  }

  char plugin_ns[] = "cbugzilla";
  int bp_len = strlen(basedir) + strlen(plugin_ns) + 2;
  basepath = calloc(bp_len, sizeof(char));

  if(0 > sprintf(basepath, "%s/%s", basedir, plugin_ns))
  {
    CCBERROR("sprintf failed: %s:%d", __FILE__, __LINE__);
    return (-1);
  }

  if(0 != ccb_mkdir(basepath))
    return (-1);

  int status=1, success=0;

  bugzilla_t prev=NULL, b = bugzies_g;
  while(b != NULL)
  {
    char *instance_bp = ccb_bugzilla_get_basepath(b, NULL);
    if(0 != ccb_mkdir(instance_bp))
      goto next;

    if (b->http_log)
    {
      char *http_log = ccb_bugzilla_get_basepath(b, "http_log");
      if(CB_SUCCESS != b->cbi->set_http_log_f(b->cbi, http_log)) {
        CCBWARNING ("cbi(%s)->set_http_log_f failed", b->instance);
      }
      sfree(http_log);
    }

    char *cookiejar = ccb_bugzilla_get_basepath(b, "cookiejar");
    if(CB_SUCCESS != b->cbi->set_cookiejar_f(b->cbi, cookiejar)) {
      CCBERROR ("cbi(%s)->set_cookiejar_f failed", b->instance);
      goto next;
    }

    if(CB_SUCCESS != b->cbi->init_curl(b->cbi))
    {
      CCBERROR ("failed: cbi->init_curl(cbi)");
      goto next;
    }

    success += 1;
    status = 0;
    next:
      sfree(cookiejar);
      sfree(instance_bp);

      if(status != 0)
      {
        if(prev != NULL)
          prev->next = b->next;
        else
          bugzies_g = b->next;

        ccb_bugzilla_free(b);

        if(prev == NULL)
          b = bugzies_g;
        else
          b = prev->next;
      }else{
        CCBINFO("initialized: %s", b->instance);
        namedcmd_t prev;
        for(prev=b->namedcmd; prev != NULL; prev = prev->next)
          CCBINFO(" ncmd: %s", prev->namedcmd);

        prev = b;
        b = b->next;
      }
      status = 1;
  }

  CCBINFO ("cbugzilla version: %s", PACKAGE_VERSION);
  return (0);
} /* }}} int ccb_init */

static void ccb_submit_gauge (const bugzilla_t b, const namedcmd_t ncmd,
  const int records, const char *type) /* {{{ */
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = records;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "cbugzilla", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, b->instance, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, type, sizeof (vl.type));
  sstrncpy (vl.type_instance, ncmd->namedcmd, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* }}} void ccb_submit */

static int ccb_read_bugzilla (bugzilla_t b) /* {{{ */
{
  unsigned long int records;
  int rc=0;

  if(b->namedcmd == NULL)
    return (0);

  namedcmd_t ncmd;
  ncmd = b->namedcmd;

  while(ncmd != NULL)
  {
    if (CB_SUCCESS == (
        rc = b->cbi->get_records_count(b->cbi, ncmd->namedcmd, &records)))
    {
      ccb_submit_gauge(b, ncmd, records, "bugs");

      double delta;

      if (CB_SUCCESS == (
      rc = b->cbi->get_total_response_time(b->cbi, &delta)))
        ccb_submit_gauge(b, ncmd, delta, "response_time");
      else
        CCBERROR ("cbi (%s)->get_total_response_time failed because %d",
          b->instance, rc);

      if (CB_SUCCESS == (
      rc = b->cbi->get_namelookup_time(b->cbi, &delta)))
        ccb_submit_gauge(b, ncmd, delta, "namelookup_time");
      else
        CCBERROR ("cbi (%s)->get_namelookup_time failed because %d",
          b->instance, rc);

      if (CB_SUCCESS == (
      rc = b->cbi->get_pretransfer_time(b->cbi, &delta)))
        ccb_submit_gauge(b, ncmd, delta, "pretransfer_time");
      else
        CCBERROR ("cbi (%s)->get_pretransfer_time failed because %d",
          b->instance, rc);

      if (CB_SUCCESS == (
      rc = b->cbi->get_starttransfer_time(b->cbi, &delta)))
        ccb_submit_gauge(b, ncmd, delta, "starttransfer_time");
      else
        CCBERROR ("cbi (%s)->get_starttransfer_time failed because %d",
          b->instance, rc);

      if (CB_SUCCESS == (
      rc = b->cbi->get_connect_time(b->cbi, &delta)))
        ccb_submit_gauge(b, ncmd, delta, "connect_time");
      else
        CCBERROR ("cbi (%s)->get_connect_time failed because %d",
          b->instance, rc);

      if (CB_SUCCESS == (
      rc = b->cbi->get_total_time(b->cbi, &delta)))
        ccb_submit_gauge(b, ncmd, delta, "total_time");
      else
        CCBERROR ("cbi (%s)->get_total_time failed because %d",
          b->instance, rc);

    }else{
      CCBERROR ("cbi (%s)->get_recordsCount(%s) failed because %d",
        b->instance, ncmd->namedcmd, rc);
    }

    ncmd = ncmd->next;
  }

  return (0);
} /* }}} int ccb_read_bugzilla */

static int ccb_read (void) /* {{{ */
{
  bugzilla_t b;

  for (b = bugzies_g; b != NULL; b = b->next)
    ccb_read_bugzilla (b);

  return (0);
} /* }}} int ccb_read */

static int ccb_shutdown (void) /* {{{ */
{
  ccb_bugzilla_free (bugzies_g);
  bugzies_g = NULL;

  return (0);
} /* }}} int ccb_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("cbugzilla", ccb_config);
  plugin_register_init ("cbugzilla", ccb_init);
  plugin_register_read ("cbugzilla", ccb_read);
  plugin_register_shutdown ("cbugzilla", ccb_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 et fdm=marker : */
