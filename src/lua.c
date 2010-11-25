/**
 * collectd - src/lua.c
 * Copyright (C) 2010       Julien Ammous
 * Copyright (C) 2010       Florian Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; only version 2.1 of the License is
 * applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *   Julien Ammous
 *   Florian Forster <octo at collectd.org>
 **/

/* <lua5.1/luaconf.h> defines a macro using "sprintf". Although not used here,
 * GCC will complain about the macro definition. */
#define DONT_POISON_SPRINTF_YET

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "configfile.h"
#include "utils_cache.h"

/* Include the Lua API header files. */
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#if defined(COLLECT_DEBUG) && COLLECT_DEBUG && defined(__GNUC__) && __GNUC__
# undef sprintf
# pragma GCC poison sprintf
#endif

typedef struct lua_script_s {
  char          *script_path;
  lua_State     *lua_state;
  
  struct lua_script_s  *next;
} lua_script_t;

struct clua_read_function_s
{
  lua_State *lua_state;
  char *lua_function_name;
};
typedef struct clua_read_function_s clua_read_function_t;

struct lua_c_functions_s
{
  char         *name;
  lua_CFunction func;
};
typedef struct lua_c_functions_s lua_c_functions_t;

static char           base_path[PATH_MAX + 1] = "";
static lua_script_t  *scripts = NULL;

static int clua_read (user_data_t *ud) /* {{{ */
{
  clua_read_function_t *rf = ud->data;
  int status;

  /* Load the function to the stack */
  lua_pushstring (rf->lua_state, rf->lua_function_name);
  lua_gettable (rf->lua_state, LUA_REGISTRYINDEX);

  if (!lua_isfunction (rf->lua_state, /* stack pos = */ -1))
  {
    ERROR ("lua plugin: Unable to lookup the read function \"%s\".",
        rf->lua_function_name);
    /* pop the value again */
    lua_settop (rf->lua_state, -2);
    return (-1);
  }

  lua_call (rf->lua_state, /* nargs = */ 0, /* nresults = */ 1);

  if (lua_isnumber (rf->lua_state, -1))
  {
    status = (int) lua_tonumber (rf->lua_state, -1);
  }
  else
  {
    ERROR ("lua plugin: Read function \"%s\" did not return a numeric status.",
        rf->lua_function_name);
    status = -1;
  }
  /* pop the value */
  lua_settop (rf->lua_state, -2);

  return (status);
} /* }}} int clua_read */

/* Cleans up the stack, pushes the return value as a number onto the stack and
 * returns the number of values returned (1). */
#define RETURN_LUA(l,status) do {                    \
  lua_State *_l_state = (l);                         \
  lua_settop (_l_state, 0);                          \
  lua_pushnumber (_l_state, (lua_Number) (status));  \
  return (1);                                        \
} while (0)

/*
 * Exported functions
 */
static int lua_cb_log (lua_State *l) /* {{{ */
{
  int nargs = lua_gettop (l); /* number of arguments */
  int severity;
  const char *msg;

  if (nargs != 2)
  {
    WARNING ("lua plugin: collectd_log() called with an invalid number of arguments (%i).",
        nargs);
    RETURN_LUA (l, -1);
  }

  if (!lua_isnumber (l, 1))
  {
    WARNING ("lua plugin: The first argument to collectd_log() must be a number.");
    RETURN_LUA (l, -1);
  }

  if (!lua_isstring (l, 2))
  {
    WARNING ("lua plugin: The second argument to collectd_log() must be a string.");
    RETURN_LUA (l, -1);
  }

  severity = (int) lua_tonumber (l, /* stack pos = */ 1);
  if ((severity != LOG_ERR)
      && (severity != LOG_WARNING)
      && (severity != LOG_NOTICE)
      && (severity != LOG_INFO)
      && (severity != LOG_DEBUG))
    severity = LOG_ERR;

  msg = lua_tostring (l, 2);
  if (msg == NULL)
  {
    ERROR ("lua plugin: lua_tostring failed.");
    RETURN_LUA (l, -1);
  }

  plugin_log (severity, "%s", msg);

  RETURN_LUA (l, 0);
} /* }}} int lua_cb_log */

static int lua_cb_register_read (lua_State *l) /* {{{ */
{
  static int count = 0;
  int nargs = lua_gettop (l); /* number of arguments */
  clua_read_function_t *rf;

  int num = count++; /* XXX FIXME: Not thread-safe! */
  char function_name[64];

  if (nargs != 1)
  {
    WARNING ("lua plugin: collectd_register_read() called with an invalid "
        "number of arguments (%i).", nargs);
    RETURN_LUA (l, -1);
  }

  /* If the argument is a string, assume it's a global function and try to look
   * it up. */
  if (lua_isstring (l, /* stack pos = */ 1))
    lua_gettable (l, LUA_GLOBALSINDEX);

  if (!lua_isfunction (l, /* stack pos = */ 1))
  {
    WARNING ("lua plugin: The first argument of collectd_register_read() "
        "must be a function.");
    RETURN_LUA (l, -1);
  }

  ssnprintf (function_name, sizeof (function_name), "collectd.read_func_%i", num);

  /* Push the name of the global variable */
  lua_pushstring (l, function_name);
  /* Push the name down to the first position */
  lua_insert (l, 1);
  /* Now set the global variable called "collectd". */
  lua_settable (l, LUA_REGISTRYINDEX);

  rf = malloc (sizeof (*rf));
  if (rf == NULL)
  {
    ERROR ("lua plugin: malloc failed.");
    RETURN_LUA (l, -1);
  }
  else
  {
    user_data_t ud = { rf, /* free func */ NULL /* FIXME */ };
    char cb_name[DATA_MAX_NAME_LEN];

    memset (rf, 0, sizeof (*rf));
    rf->lua_state = l;
    rf->lua_function_name = strdup (function_name);

    ssnprintf (cb_name, sizeof (cb_name), "lua/read_func_%i", num);

    plugin_register_complex_read (/* group = */ "lua",
        /* name      = */ cb_name,
        /* callback  = */ clua_read,
        /* interval  = */ NULL,
        /* user_data = */ &ud);
  }

  DEBUG ("lua plugin: Successful call to lua_cb_register_read().");

  RETURN_LUA (l, 0);
} /* }}} int lua_cb_register_read */

static lua_c_functions_t lua_c_functions[] =
{
  { "collectd_log", lua_cb_log },
  { "collectd_register_read", lua_cb_register_read }
};

static void lua_script_free (lua_script_t *script) /* {{{ */
{
  lua_script_t *next;

  if (script == NULL)
    return;

  next = script->next;

  if (script->lua_state != NULL)
  {
    lua_close (script->lua_state);
    script->lua_state = NULL;
  }

  sfree (script->script_path);
  sfree (script);

  lua_script_free (next);
} /* }}} void lua_script_free */

static int lua_script_init (lua_script_t *script) /* {{{ */
{
  size_t i;

  memset (script, 0, sizeof (*script));
  script->script_path = NULL;
  script->next = NULL;

  /* initialize the lua context */
  script->lua_state = lua_open();
  if (script->lua_state == NULL)
  {
    ERROR ("lua plugin: lua_open failed.");
    return (-1);
  }

  /* Open up all the standard Lua libraries. */
  luaL_openlibs (script->lua_state);

  /* Register all the functions we implement in C */
  for (i = 0; i < STATIC_ARRAY_SIZE (lua_c_functions); i++)
    lua_register (script->lua_state,
        lua_c_functions[i].name, lua_c_functions[i].func);

  return (0);
} /* }}} int lua_script_init */

static int lua_script_load (const char *script_path) /* {{{ */
{
  lua_script_t *script;
  int status;

  script = malloc (sizeof (*script));
  if (script == NULL)
  {
    ERROR ("lua plugin: malloc failed.");
    return (-1);
  }

  status = lua_script_init (script);
  if (status != 0)
  {
    lua_script_free (script);
    return (status);
  }

  script->script_path = strdup (script_path);
  if (script->script_path == NULL)
  {
    ERROR ("lua plugin: strdup failed.");
    lua_script_free (script);
    return (-1);
  }

  status = luaL_loadfile (script->lua_state, script->script_path);
  if (status != 0)
  {
    ERROR ("lua plugin: luaL_loadfile failed with status %i", status);
    lua_script_free (script);
    return (-1);
  }

  status = lua_pcall (script->lua_state,
      /* nargs = */    0,
      /* nresults = */ LUA_MULTRET,
      /* errfunc = */  0);
  if (status != 0)
  {
    const char *errmsg;

    errmsg = lua_tostring (script->lua_state, /* stack pos = */ -1);

    if (errmsg == NULL)
      ERROR ("lua plugin: lua_pcall failed with status %i. "
          "In addition, no error message could be retrieved from the stack.",
          status);
    else
      ERROR ("lua plugin: Executing script \"%s\" failed:\n%s",
          script->script_path, errmsg);

    lua_script_free (script);
    return (-1);
  }

  /* Append this script to the global list of scripts. */
  if (scripts == NULL)
  {
    scripts = script;
  }
  else
  {
    lua_script_t *last;

    last = scripts;
    while (last->next != NULL)
      last = last->next;

    last->next = script;
  }

  return (0);
} /* }}} int lua_script_load */

static int lua_config_base_path (const oconfig_item_t *ci) /* {{{ */
{
  int status;
  size_t len;

  status = cf_util_get_string_buffer (ci, base_path, sizeof (base_path));
  if (status != 0)
    return (status);

  len = strlen (base_path);
  while ((len > 0) && (base_path[len - 1] == '/'))
  {
    len--;
    base_path[len] = 0;
  }

  DEBUG ("lua plugin: base_path = \"%s\";", base_path);

  return (0);
} /* }}} int lua_config_base_path */

static int lua_config_script (const oconfig_item_t *ci) /* {{{ */
{
  char rel_path[PATH_MAX + 1];
  char abs_path[PATH_MAX + 1];
  int status;

  status = cf_util_get_string_buffer (ci, rel_path, sizeof (rel_path));
  if (status != 0)
    return (status);

  if (base_path[0] == 0)
    sstrncpy (abs_path, rel_path, sizeof (abs_path));
  else
    ssnprintf (abs_path, sizeof (abs_path), "%s/%s", base_path, rel_path);

  DEBUG ("lua plugin: abs_path = \"%s\";", abs_path);

  status = lua_script_load (abs_path);
  if (status != 0)
    return (status);

  INFO("lua plugin: File \"%s\" loaded succesfully", abs_path);
  
  return 0;
} /* }}} int lua_config_script */

/*
 * <Plugin lua>
 *   BasePath "/"
 *   Script "script1.lua"
 *   Script "script2.lua"
 * </Plugin>
 */
static int lua_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("BasePath", child->key) == 0) {
      lua_config_base_path(child);
    }
    else if (strcasecmp ("Script", child->key) == 0){
      lua_config_script(child);
    }
    else
    {
      WARNING ("network plugin: Option `%s' is not allowed here.",
          child->key);
    }
  }
  
  return 0;
} /* }}} int lua_config */

static int lua_shutdown (void) /* {{{ */
{
  lua_script_free (scripts);
  scripts = NULL;

  return (0);
} /* }}} int lua_shutdown */

void module_register()
{
  plugin_register_complex_config("lua", lua_config);
  plugin_register_shutdown("lua", lua_shutdown);
}

/* vim: set sw=2 sts=2 et fdm=marker : */
