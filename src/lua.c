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

#include <unistd.h> // access

/* Include the Lua API header files. */
#define lua_c
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "configfile.h"
#include "utils_cache.h"

#include "lua_exports.c"

typedef struct lua_script_s {
  char          *script_path;
  lua_State     *lua_state;
  
  struct lua_script_s  *next;
} lua_script_t;


static char           base_path[MAXPATHLEN];
static lua_script_t   scripts;



/* Declare the Lua libraries we wish to use. */
/* Note: If you are opening and running a file containing Lua code */
/* using 'lua_dofile(l, "myfile.lua") - you must delcare all the libraries */
/* used in that file here also. */
static const luaL_reg lualibs[] =
{
        // { "base",       luaopen_base },
        { NULL,         NULL }
};

/* A function to open up all the Lua libraries you declared above. */
static void openlualibs(lua_State *l)
{
  const luaL_reg *lib;
  for( lib = lualibs; lib->func != NULL; lib++) {
    lib->func(l);
    lua_settop(l, 0);
  }
}





static int lua_config_base_path (const oconfig_item_t *ci)
{
  if( (ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING) ){
    ERROR("lua plugin: The '%s' config option requires a single string argument", ci->key);
    return -1;
  }
  
  strncpy(base_path, ci->values[0].value.string, sizeof(base_path));
  
  /* add ending slash if not provided */
  if( base_path[strlen(base_path) - 1] != '/' ){
    base_path[strlen(base_path)] = '/';
  }
  
  INFO("lua plugin: BasePath = '%s'", base_path);
  return 0;
}

static int lua_config_script (const oconfig_item_t *ci)
{
  lua_script_t *script = &scripts;
  
  if( (ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING) ){
    ERROR("lua plugin: The '%s' config option requires a single string argument", ci->key);
    return -1;
  }
  
  /* find a free slot for the structure */
  while( script->next != NULL ){
    script = script->next;
  }
  
  /* build full path : base_path + given path + \0 */
  script->script_path = malloc(
      strlen(base_path) +
      strlen(ci->values[0].value.string) + 1
    );
  
  strncpy(script->script_path, base_path, sizeof(script->script_path));
  strncpy(script->script_path + strlen(base_path), ci->values[0].value.string, sizeof(script->script_path));
  
  /* check if the file exists and we can read it */
  if( access(script->script_path, R_OK) == -1 ) {
    ERROR("lua plugin: Cannot read file '%s' : %s", script->script_path, strerror(errno));
    free(script->script_path);
    return -1;
  }
  
  /* initialize the lua context */
  script->lua_state = lua_open();
  
  openlualibs(script->lua_state);
  
  if( register_exported_functions(script->lua_state) != 0 ) {
    ERROR("lua plugin: Cannot register exported functions, aborting");
    free(script->script_path);
    return -1;
  }
  
  /* and try to load the file */
  if( luaL_dofile(script->lua_state, "script.lua") != 0 ) {
    ERROR("lua plugin: error while loading '%s' => %s\n", script->script_path, lua_tostring(script->lua_state, -1));
    free(script->script_path);
    return -1;
  }
  
  INFO("lua plugin: file '%s' loaded succesfully", script->script_path);
  
  return 0;
}

// <Plugin lua>
//   BasePath /
//   Script script1.lua
//   Script script2.lua
// </Plugin>
static int lua_config(oconfig_item_t *ci)
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
}

static int lua_init()
{
  INFO("Lua plugin loaded.");
  
  return 0;
}

void module_register()
{
  plugin_register_complex_config("lua", lua_config);
  plugin_register_init("lua", lua_init);
}



