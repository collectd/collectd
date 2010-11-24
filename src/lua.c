/**
 * collectd - src/lua.c
 * Copyright (C) 2010       Julien Ammous
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
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "configfile.h"
#include "utils_cache.h"

/* Include the Lua API header files. */
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lua_exports.c"

typedef struct lua_script_s {
  char          *script_path;
  lua_State     *lua_state;
  
  struct lua_script_s  *next;
} lua_script_t;

static char           base_path[PATH_MAX + 1] = "";
static lua_script_t  *scripts = NULL;

/* Declare the Lua libraries we wish to use.
 * Note: If you are opening and running a file containing Lua code using
 * 'lua_dofile(l, "myfile.lua") - you must delcare all the libraries used in
 * that file here also. */
static const luaL_reg lua_load_libs[] =
{
#if COLLECT_DEBUG
  { LUA_DBLIBNAME,   luaopen_debug  },
#endif
  { LUA_TABLIBNAME,  luaopen_table  },
  { LUA_IOLIBNAME,   luaopen_io     },
  { LUA_STRLIBNAME,  luaopen_string },
  { LUA_MATHLIBNAME, luaopen_math   }
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

  /* Open up all the Lua libraries declared above. */
  for (i = 0; i < STATIC_ARRAY_SIZE (lua_load_libs); i++)
  {
    int status;

    status = (*lua_load_libs[i].func) (script->lua_state);
    if (status != 0)
      WARNING ("lua plugin: Loading library \"%s\" failed.",
          lua_load_libs[i].name);
  }

  /* Register all the functions we implement in C */
  register_exported_functions (script->lua_state);

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
    const char *errmsg;

    switch (status)
    {
      case LUA_ERRSYNTAX: errmsg = "Syntax error"; break;
      case LUA_ERRFILE:   errmsg = "File I/O error"; break;
      case LUA_ERRMEM:    errmsg = "Memory allocation error"; break;
      default:            errmsg = "Unexpected error";
    }

    ERROR ("lua plugin: Loading script \"%s\" failed: %s",
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
