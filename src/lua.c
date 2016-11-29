/**
 * collectd - src/lua.c
 * Copyright (C) 2010       Julien Ammous
 * Copyright (C) 2010       Florian Forster
 * Copyright (C) 2016       Ruben Kerkhof
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Julien Ammous
 *   Florian Forster <octo at collectd.org>
 *   Ruben Kerkhof <ruben at rubenkerkhof.com>
 **/

/* <lua5.1/luaconf.h> defines a macro using "sprintf". Although not used here,
 * GCC will complain about the macro definition. */
#define DONT_POISON_SPRINTF_YET

#include "common.h"
#include "plugin.h"
#include "collectd.h"

/* Include the Lua API header files. */
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include "utils_lua.h"

#include <pthread.h>

#if COLLECT_DEBUG && __GNUC__
#undef sprintf
#pragma GCC poison sprintf
#endif

typedef struct lua_script_s {
  char *script_path;
  lua_State *lua_state;
  struct lua_script_s *next;
} lua_script_t;

typedef struct {
  lua_State *lua_state;
  const char *lua_function_name;
  pthread_mutex_t lock;
  int callback_id;
} clua_callback_data_t;

static char base_path[PATH_MAX];
static lua_script_t *scripts;

static int clua_store_callback(lua_State *L, int idx) /* {{{ */
{
  /* Copy the function pointer */
  lua_pushvalue(L, idx);

  return luaL_ref(L, LUA_REGISTRYINDEX);
} /* }}} int clua_store_callback */

static int clua_load_callback(lua_State *L, int callback_ref) /* {{{ */
{
  lua_rawgeti(L, LUA_REGISTRYINDEX, callback_ref);

  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    return (-1);
  }

  return (0);
} /* }}} int clua_load_callback */

/* Store the threads in a global variable so they are not cleaned up by the
 * garbage collector. */
static int clua_store_thread(lua_State *L, int idx) /* {{{ */
{
  if (idx < 0)
    idx += lua_gettop(L) + 1;

  /* Copy the thread pointer */
  lua_pushvalue(L, idx); /* +1 = 3 */
  if (!lua_isthread(L, -1)) {
    lua_pop(L, 3); /* -3 = 0 */
    return (-1);
  }

  luaL_ref(L, LUA_REGISTRYINDEX);
  lua_pop(L, 1); /* -1 = 0 */
  return (0);
} /* }}} int clua_store_thread */

static int clua_read(user_data_t *ud) /* {{{ */
{
  clua_callback_data_t *cb = ud->data;

  pthread_mutex_lock(&cb->lock);

  lua_State *L = cb->lua_state;

  int status = clua_load_callback(L, cb->callback_id);
  if (status != 0) {
    ERROR("Lua plugin: Unable to load callback \"%s\" (id %i).",
          cb->lua_function_name, cb->callback_id);
    pthread_mutex_unlock(&cb->lock);
    return (-1);
  }
  /* +1 = 1 */

  status = lua_pcall(L, 0, 1, 0);
  if (status != 0) {
    const char *errmsg = lua_tostring(L, -1);
    if (errmsg == NULL)
      ERROR("Lua plugin: Calling a read callback failed. "
            "In addition, retrieving the error message failed.");
    else
      ERROR("Lua plugin: Calling a read callback failed: %s", errmsg);
    lua_pop(L, 1);
    pthread_mutex_unlock(&cb->lock);
    return (-1);
  }

  if (!lua_isnumber(L, -1)) {
    ERROR("Lua plugin: Read function \"%s\" (id %i) did not return a numeric "
          "status.",
          cb->lua_function_name, cb->callback_id);
    status = -1;
  } else {
    status = (int)lua_tointeger(L, -1);
  }

  /* pop return value and function */
  lua_pop(L, 1); /* -1 = 0 */

  pthread_mutex_unlock(&cb->lock);
  return (status);
} /* }}} int clua_read */

static int clua_write(const data_set_t *ds, const value_list_t *vl, /* {{{ */
                      user_data_t *ud) {
  clua_callback_data_t *cb = ud->data;

  pthread_mutex_lock(&cb->lock);

  lua_State *L = cb->lua_state;

  int status = clua_load_callback(L, cb->callback_id);
  if (status != 0) {
    ERROR("Lua plugin: Unable to load callback \"%s\" (id %i).",
          cb->lua_function_name, cb->callback_id);
    pthread_mutex_unlock(&cb->lock);
    return (-1);
  }
  /* +1 = 1 */

  status = luaC_pushvaluelist(L, ds, vl);
  if (status != 0) {
    lua_pop(L, 1); /* -1 = 0 */
    pthread_mutex_unlock(&cb->lock);
    ERROR("Lua plugin: luaC_pushvaluelist failed.");
    return (-1);
  }
  /* +1 = 2 */

  status = lua_pcall(L, 1, 1, 0); /* -2+1 = 1 */
  if (status != 0) {
    const char *errmsg = lua_tostring(L, -1);
    if (errmsg == NULL)
      ERROR("Lua plugin: Calling the write callback failed. "
            "In addition, retrieving the error message failed.");
    else
      ERROR("Lua plugin: Calling the write callback failed:\n%s", errmsg);
    lua_pop(L, 1); /* -1 = 0 */
    pthread_mutex_unlock(&cb->lock);
    return (-1);
  }

  if (!lua_isnumber(L, -1)) {
    ERROR("Lua plugin: Write function \"%s\" (id %i) did not return a numeric "
          "value.",
          cb->lua_function_name, cb->callback_id);
    status = -1;
  } else {
    status = (int)lua_tointeger(L, -1);
  }

  lua_pop(L, 1); /* -1 = 0 */
  pthread_mutex_unlock(&cb->lock);
  return (status);
} /* }}} int clua_write */

/*
 * Exported functions
 */

static int lua_cb_log_debug(lua_State *L) /* {{{ */
{
  const char *msg = luaL_checkstring(L, 1);
  plugin_log(LOG_DEBUG, "%s", msg);
  return 0;
} /* }}} int lua_cb_log_debug */

static int lua_cb_log_error(lua_State *L) /* {{{ */
{
  const char *msg = luaL_checkstring(L, 1);
  plugin_log(LOG_ERR, "%s", msg);
  return 0;
} /* }}} int lua_cb_log_error */

static int lua_cb_log_info(lua_State *L) /* {{{ */
{
  const char *msg = luaL_checkstring(L, 1);
  plugin_log(LOG_INFO, "%s", msg);
  return 0;
} /* }}} int lua_cb_log_info */

static int lua_cb_log_notice(lua_State *L) /* {{{ */
{
  const char *msg = luaL_checkstring(L, 1);
  plugin_log(LOG_NOTICE, "%s", msg);
  return 0;
} /* }}} int lua_cb_log_notice */

static int lua_cb_log_warning(lua_State *L) /* {{{ */
{
  const char *msg = luaL_checkstring(L, 1);
  plugin_log(LOG_WARNING, "%s", msg);
  return 0;
} /* }}} int lua_cb_log_warning */

static int lua_cb_dispatch_values(lua_State *L) /* {{{ */
{
  int nargs = lua_gettop(L);

  if (nargs != 1)
    return luaL_error(L, "Invalid number of arguments (%d != 1)", nargs);

  luaL_checktype(L, 1, LUA_TTABLE);

  value_list_t *vl = luaC_tovaluelist(L, -1);
  if (vl == NULL)
    return luaL_error(L, "%s", "luaC_tovaluelist failed");

#if COLLECT_DEBUG
  char identifier[6 * DATA_MAX_NAME_LEN];
  FORMAT_VL(identifier, sizeof(identifier), vl);

  DEBUG("Lua plugin: collectd.dispatch_values(): Received value list \"%s\", "
        "time %.3f, interval %.3f.",
        identifier, CDTIME_T_TO_DOUBLE(vl->time),
        CDTIME_T_TO_DOUBLE(vl->interval));
#endif

  plugin_dispatch_values(vl);

  sfree(vl->values);
  sfree(vl);
  return 0;
} /* }}} lua_cb_dispatch_values */

static int lua_cb_register_read(lua_State *L) /* {{{ */
{
  int nargs = lua_gettop(L);

  if (nargs != 1)
    return luaL_error(L, "Invalid number of arguments (%d != 1)", nargs);

  luaL_checktype(L, 1, LUA_TFUNCTION);

  char function_name[DATA_MAX_NAME_LEN];
  ssnprintf(function_name, sizeof(function_name), "lua/%s", lua_tostring(L, 1));

  int callback_id = clua_store_callback(L, 1);
  if (callback_id < 0)
    return luaL_error(L, "%s", "Storing callback function failed");

  lua_State *thread = lua_newthread(L);
  if (thread == NULL)
    return luaL_error(L, "%s", "lua_newthread failed");
  clua_store_thread(L, -1);
  lua_pop(L, 1);

  clua_callback_data_t *cb = calloc(1, sizeof(*cb));
  if (cb == NULL)
    return luaL_error(L, "%s", "calloc failed");

  cb->lua_state = thread;
  cb->callback_id = callback_id;
  cb->lua_function_name = strdup(function_name);
  pthread_mutex_init(&cb->lock, NULL);

  int status = plugin_register_complex_read(/* group = */ "lua",
                                            /* name      = */ function_name,
                                            /* callback  = */ clua_read,
                                            /* interval  = */ 0, &(user_data_t){
                                                                     .data = cb,
                                                                 });

  if (status != 0)
    return luaL_error(L, "%s", "plugin_register_complex_read failed");
  return 0;
} /* }}} int lua_cb_register_read */

static int lua_cb_register_write(lua_State *L) /* {{{ */
{
  int nargs = lua_gettop(L);

  if (nargs != 1)
    return luaL_error(L, "Invalid number of arguments (%d != 1)", nargs);

  luaL_checktype(L, 1, LUA_TFUNCTION);

  char function_name[DATA_MAX_NAME_LEN] = "";
  ssnprintf(function_name, sizeof(function_name), "lua/%s", lua_tostring(L, 1));

  int callback_id = clua_store_callback(L, 1);
  if (callback_id < 0)
    return luaL_error(L, "%s", "Storing callback function failed");

  lua_State *thread = lua_newthread(L);
  if (thread == NULL)
    return luaL_error(L, "%s", "lua_newthread failed");
  clua_store_thread(L, -1);
  lua_pop(L, 1);

  clua_callback_data_t *cb = calloc(1, sizeof(*cb));
  if (cb == NULL)
    return luaL_error(L, "%s", "calloc failed");

  cb->lua_state = thread;
  cb->callback_id = callback_id;
  cb->lua_function_name = strdup(function_name);
  pthread_mutex_init(&cb->lock, NULL);

  int status =
      plugin_register_write(/* name = */ function_name,
                            /* callback  = */ clua_write, &(user_data_t){
                                                              .data = cb,
                                                          });

  if (status != 0)
    return luaL_error(L, "%s", "plugin_register_write failed");
  return 0;
} /* }}} int lua_cb_register_write */

static const luaL_Reg collectdlib[] = {
    {"log_debug", lua_cb_log_debug},
    {"log_error", lua_cb_log_error},
    {"log_info", lua_cb_log_info},
    {"log_notice", lua_cb_log_notice},
    {"log_warning", lua_cb_log_warning},
    {"dispatch_values", lua_cb_dispatch_values},
    {"register_read", lua_cb_register_read},
    {"register_write", lua_cb_register_write},
    {NULL, NULL}};

static int open_collectd(lua_State *L) /* {{{ */
{
#if LUA_VERSION_NUM < 502
  luaL_register(L, "collectd", collectdlib);
#else
  luaL_newlib(L, collectdlib);
#endif
  return 1;
} /* }}} */

static void lua_script_free(lua_script_t *script) /* {{{ */
{
  if (script == NULL)
    return;

  lua_script_t *next = script->next;

  if (script->lua_state != NULL) {
    lua_close(script->lua_state);
    script->lua_state = NULL;
  }

  sfree(script->script_path);
  sfree(script);

  lua_script_free(next);
} /* }}} void lua_script_free */

static int lua_script_init(lua_script_t *script) /* {{{ */
{
  memset(script, 0, sizeof(*script));

  /* initialize the lua context */
  script->lua_state = luaL_newstate();
  if (script->lua_state == NULL) {
    ERROR("Lua plugin: luaL_newstate() failed.");
    return (-1);
  }

  /* Open up all the standard Lua libraries. */
  luaL_openlibs(script->lua_state);

/* Load the 'collectd' library */
#if LUA_VERSION_NUM < 502
  lua_pushcfunction(script->lua_state, open_collectd);
  lua_pushstring(script->lua_state, "collectd");
  lua_call(script->lua_state, 1, 0);
#else
  luaL_requiref(script->lua_state, "collectd", open_collectd, 1);
  lua_pop(script->lua_state, 1);
#endif

  /* Prepend BasePath to package.path */
  if (base_path[0] != '\0') {
    lua_getglobal(script->lua_state, "package");
    lua_getfield(script->lua_state, -1, "path");

    const char *cur_path = lua_tostring(script->lua_state, -1);
    char *new_path = ssnprintf_alloc("%s/?.lua;%s", base_path, cur_path);

    lua_pop(script->lua_state, 1);
    lua_pushstring(script->lua_state, new_path);

    free(new_path);

    lua_setfield(script->lua_state, -2, "path");
    lua_pop(script->lua_state, 1);
  }

  return (0);
} /* }}} int lua_script_init */

static int lua_script_load(const char *script_path) /* {{{ */
{
  lua_script_t *script = malloc(sizeof(*script));
  if (script == NULL) {
    ERROR("Lua plugin: malloc failed.");
    return (-1);
  }

  int status = lua_script_init(script);
  if (status != 0) {
    lua_script_free(script);
    return (status);
  }

  script->script_path = strdup(script_path);
  if (script->script_path == NULL) {
    ERROR("Lua plugin: strdup failed.");
    lua_script_free(script);
    return (-1);
  }

  status = luaL_loadfile(script->lua_state, script->script_path);
  if (status != 0) {
    ERROR("Lua plugin: luaL_loadfile failed: %s",
          lua_tostring(script->lua_state, -1));
    lua_pop(script->lua_state, 1);
    lua_script_free(script);
    return (-1);
  }

  status = lua_pcall(script->lua_state,
                     /* nargs = */ 0,
                     /* nresults = */ LUA_MULTRET,
                     /* errfunc = */ 0);
  if (status != 0) {
    const char *errmsg = lua_tostring(script->lua_state, -1);

    if (errmsg == NULL)
      ERROR("Lua plugin: lua_pcall failed with status %i. "
            "In addition, no error message could be retrieved from the stack.",
            status);
    else
      ERROR("Lua plugin: Executing script \"%s\" failed:\n%s",
            script->script_path, errmsg);

    lua_script_free(script);
    return (-1);
  }

  /* Append this script to the global list of scripts. */
  if (scripts) {
    lua_script_t *last = scripts;
    while (last->next)
      last = last->next;

    last->next = script;
  } else {
    scripts = script;
  }

  return (0);
} /* }}} int lua_script_load */

static int lua_config_base_path(const oconfig_item_t *ci) /* {{{ */
{
  int status = cf_util_get_string_buffer(ci, base_path, sizeof(base_path));
  if (status != 0)
    return (status);

  size_t len = strlen(base_path);
  while ((len > 0) && (base_path[len - 1] == '/')) {
    len--;
    base_path[len] = '\0';
  }

  DEBUG("Lua plugin: base_path = \"%s\";", base_path);

  return (0);
} /* }}} int lua_config_base_path */

static int lua_config_script(const oconfig_item_t *ci) /* {{{ */
{
  char rel_path[PATH_MAX];

  int status = cf_util_get_string_buffer(ci, rel_path, sizeof(rel_path));
  if (status != 0)
    return (status);

  char abs_path[PATH_MAX];

  if (base_path[0] == '\0')
    sstrncpy(abs_path, rel_path, sizeof(abs_path));
  else
    ssnprintf(abs_path, sizeof(abs_path), "%s/%s", base_path, rel_path);

  DEBUG("Lua plugin: abs_path = \"%s\";", abs_path);

  status = lua_script_load(abs_path);
  if (status != 0)
    return (status);

  INFO("Lua plugin: File \"%s\" loaded successfully", abs_path);

  return 0;
} /* }}} int lua_config_script */

/*
 * <Plugin lua>
 *   BasePath "/"
 *   Script "script1.lua"
 *   Script "script2.lua"
 * </Plugin>
 */
static int lua_config(oconfig_item_t *ci) /* {{{ */
{
  int status = 0;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("BasePath", child->key) == 0) {
      status = lua_config_base_path(child);
    } else if (strcasecmp("Script", child->key) == 0) {
      status = lua_config_script(child);
    } else {
      ERROR("Lua plugin: Option `%s' is not allowed here.", child->key);
      status = 1;
    }
  }

  return status;
} /* }}} int lua_config */

static int lua_shutdown(void) /* {{{ */
{
  lua_script_free(scripts);

  return (0);
} /* }}} int lua_shutdown */

void module_register(void) {
  plugin_register_complex_config("lua", lua_config);
  plugin_register_shutdown("lua", lua_shutdown);
}

/* vim: set sw=2 sts=2 et fdm=marker : */
