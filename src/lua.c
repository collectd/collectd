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

#include "collectd.h"
#include "common.h"
#include "plugin.h"

/* Include the Lua API header files. */
#include "utils_lua.h"
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

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

typedef struct {
  const char *name;
  lua_CFunction func;
} lua_c_function_t;

static char base_path[PATH_MAX];
static lua_script_t *scripts;

// store a reference to the function on the top of the stack
static int clua_store_callback(lua_State *L, int idx) /* {{{ */
{
  /* Copy the function pointer */
  lua_pushvalue(L, idx); /* +1 = 3 */

  /* Lookup function if it's a string */
  if (lua_isstring(L, -1))
    lua_gettable(L, LUA_GLOBALSINDEX); /* +-0 = 3 */

  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 3); /* -3 = 0 */
    return (-1);
  }

  int callback_ref = luaL_ref(L, LUA_REGISTRYINDEX);

  lua_pop(L, 1); /* -1 = 0 */
  return (callback_ref);
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

#define LUA_FILTER_COPY_FIELD(field)                                           \
  do {                                                                         \
    lua_getfield(L, -1, #field);                                               \
    if (lua_isstring(L, -1) == 0) {                                            \
      WARNING("Lua plugin: filter callback: wrong type for " #field ": %d",    \
              lua_type(L, -1));                                                \
    } else if (luaC_tostringbuffer(L, -1, vl->field, sizeof(vl->field)) !=     \
               0) {                                                            \
      WARNING("Lua plugin: filter callback : " #field " is missing");          \
    }                                                                          \
                                                                               \
    lua_pop(L, 1);                                                             \
  } while (0)

static int clua_filter(const data_set_t *ds, value_list_t *vl,
                       user_data_t *ud) /* {{{ */
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

  status = luaC_pushvaluelist(L, ds, vl);
  if (status != 0) {
    lua_pop(L, 1);
    ERROR("Lua plugin: luaC_pushvaluelist failed.");
    pthread_mutex_unlock(&cb->lock);
    return -1;
  }

  status = lua_pcall(L, 1, 1, 0);
  if (status != 0) {
    const char *errmsg = lua_tostring(L, -1);
    if (errmsg == NULL) {
      ERROR("Lua plugin: Calling a filter callback failed. In addition, "
            "retrieving the error message failed.");
    } else {
      ERROR("Lua plugin: Calling a filter callback failed: %s", errmsg);
    }
    lua_pop(L, 1); /* -1 = 0 */
    pthread_mutex_unlock(&cb->lock);
    return -1;
  }

  if (lua_istable(L, -1)) {
    // report changes to the value_list_t structure
    // values
    lua_getfield(L, -1, "values");
    if (lua_istable(L, -1)) {
      // check size
      size_t s = lua_objlen(L, -1);

      if (s == vl->values_len) {
        for (size_t i = 0; i < vl->values_len; i++) {
          lua_rawgeti(L, -1, i);
          if (lua_isnumber(L, -1)) {
            vl->values[i] = luaC_tovalue(L, -1, ds->ds[i].type);
          }
          lua_pop(L, 1);
        }

        // pop values table
        lua_pop(L, 1);

        // copy other fields
        LUA_FILTER_COPY_FIELD(host);
        LUA_FILTER_COPY_FIELD(plugin);
        LUA_FILTER_COPY_FIELD(plugin_instance);
        LUA_FILTER_COPY_FIELD(type);
        LUA_FILTER_COPY_FIELD(type_instance);
      } else {
        ERROR("Lua plugin: filter callback tried to change values count %zu != "
              "%zu (%s)",
              s, vl->values_len, cb->lua_function_name);
      }
    }
  } else if (lua_isnumber(L, -1)) {
    lua_Integer ret = lua_tointeger(L, -1);

    if (ret == 1) {
      /* discard the value list */
      status = 1;
    } else {
      ERROR("Lua plugin: unknown integer returned: %d", (int)ret);
    }
  }

  /* pop return value and function */
  lua_pop(L, 1); /* -1 = 0 */

  pthread_mutex_unlock(&cb->lock);
  return (status);
} /* }}} int clua_filter */

#undef LUA_FILTER_COPY_FIELD

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

/* Cleans up the stack, pushes the return value as a number onto the stack and
 * returns the number of values returned (1). */
#define RETURN_LUA(L, status)                                                  \
  do {                                                                         \
    lua_State *_l_state = (L);                                                 \
    lua_settop(_l_state, 0);                                                   \
    lua_pushnumber(_l_state, (lua_Number)(status));                            \
    return (1);                                                                \
  } while (0)

/*
 * Exported functions
 */
static int lua_cb_log(lua_State *L) /* {{{ */
{
  int nargs = lua_gettop(L);

  if (nargs != 2) {
    WARNING("Lua plugin: collectd_log() called with an invalid number of "
            "arguments (%i).",
            nargs);
    RETURN_LUA(L, -1);
  }

  if (!lua_isnumber(L, 1)) {
    WARNING(
        "Lua plugin: The first argument to collectd_log() must be a number.");
    RETURN_LUA(L, -1);
  }

  if (!lua_isstring(L, 2)) {
    WARNING(
        "Lua plugin: The second argument to collectd_log() must be a string.");
    RETURN_LUA(L, -1);
  }

  int severity = (int)lua_tonumber(L, 1);
  if ((severity != LOG_ERR) && (severity != LOG_WARNING) &&
      (severity != LOG_NOTICE) && (severity != LOG_INFO) &&
      (severity != LOG_DEBUG))
    severity = LOG_ERR;

  const char *msg = lua_tostring(L, 2);
  if (msg == NULL) {
    ERROR("Lua plugin: lua_tostring failed.");
    RETURN_LUA(L, -1);
  }

  plugin_log(severity, "%s", msg);

  RETURN_LUA(L, 0);
} /* }}} int lua_cb_log */

static int lua_cb_dispatch_values(lua_State *L) /* {{{ */
{
  int nargs = lua_gettop(L);

  if (nargs != 1) {
    WARNING("Lua plugin: collectd_dispatch_values() called "
            "with an invalid number of arguments (%i).",
            nargs);
    RETURN_LUA(L, -1);
  }

  if (!lua_istable(L, 1)) {
    WARNING("Lua plugin: The first argument to collectd_dispatch_values() "
            "must be a \"value list\" (i.e. a table).");
    RETURN_LUA(L, -1);
  }

  value_list_t *vl = luaC_tovaluelist(L, -1);
  if (vl == NULL) {
    WARNING("Lua plugin: luaC_tovaluelist failed.");
    RETURN_LUA(L, -1);
  }

  char identifier[6 * DATA_MAX_NAME_LEN];
  FORMAT_VL(identifier, sizeof(identifier), vl);

  DEBUG("Lua plugin: collectd_dispatch_values: Received value list \"%s\", "
        "time %.3f, interval %.3f.",
        identifier, CDTIME_T_TO_DOUBLE(vl->time),
        CDTIME_T_TO_DOUBLE(vl->interval));

  plugin_dispatch_values(vl);

  sfree(vl->values);
  sfree(vl);
  RETURN_LUA(L, 0);
} /* }}} lua_cb_dispatch_values */

static int lua_cb_register_read(lua_State *L) /* {{{ */
{
  int nargs = lua_gettop(L);

  if (nargs != 1) {
    WARNING("Lua plugin: collectd_register_read() called with an invalid "
            "number of arguments (%i).",
            nargs);
    RETURN_LUA(L, -1);
  }

  char function_name[DATA_MAX_NAME_LEN] = "";

  if (lua_isstring(L, 1)) {
    const char *tmp = lua_tostring(L, 1);
    ssnprintf(function_name, sizeof(function_name), "lua/%s", tmp);
  }

  int callback_id = clua_store_callback(L, 1);
  if (callback_id < 0) {
    ERROR("Lua plugin: Storing callback function failed.");
    RETURN_LUA(L, -1);
  }

  lua_State *thread = lua_newthread(L);
  if (thread == NULL) {
    ERROR("Lua plugin: lua_newthread failed.");
    RETURN_LUA(L, -1);
  }
  clua_store_thread(L, -1);
  lua_pop(L, 1);

  if (function_name[0] == '\0')
    ssnprintf(function_name, sizeof(function_name), "lua/callback_%i",
              callback_id);

  clua_callback_data_t *cb = calloc(1, sizeof(*cb));
  if (cb == NULL) {
    ERROR("Lua plugin: calloc failed.");
    RETURN_LUA(L, -1);
  }

  cb->lua_state = thread;
  cb->callback_id = callback_id;
  cb->lua_function_name = strdup(function_name);
  pthread_mutex_init(&cb->lock, NULL);

  user_data_t ud = {
    .data = cb
  };

  plugin_register_complex_read(/* group = */ "lua",
                               /* name      = */ function_name,
                               /* callback  = */ clua_read,
                               /* interval  = */ 0,
                               /* user_data = */ &ud);

  DEBUG("Lua plugin: Successful call to lua_cb_register_read().");

  RETURN_LUA(L, 0);
} /* }}} int lua_cb_register_read */

static int lua_cb_register_write(lua_State *L) /* {{{ */
{
  int nargs = lua_gettop(L);

  if (nargs != 1) {
    WARNING("Lua plugin: collectd_register_read() called with an invalid "
            "number of arguments (%i).",
            nargs);
    RETURN_LUA(L, -1);
  }

  char function_name[DATA_MAX_NAME_LEN] = "";

  if (lua_isstring(L, 1)) {
    const char *tmp = lua_tostring(L, 1);
    ssnprintf(function_name, sizeof(function_name), "lua/%s", tmp);
  }

  int callback_id = clua_store_callback(L, 1);
  if (callback_id < 0) {
    ERROR("Lua plugin: Storing callback function failed.");
    RETURN_LUA(L, -1);
  }

  lua_State *thread = lua_newthread(L);
  if (thread == NULL) {
    ERROR("Lua plugin: lua_newthread failed.");
    RETURN_LUA(L, -1);
  }
  clua_store_thread(L, -1);
  lua_pop(L, 1);

  if (function_name[0] == '\0')
    ssnprintf(function_name, sizeof(function_name), "lua/callback_%i",
              callback_id);

  clua_callback_data_t *cb = calloc(1, sizeof(*cb));
  if (cb == NULL) {
    ERROR("Lua plugin: calloc failed.");
    RETURN_LUA(L, -1);
  }

  cb->lua_state = thread;
  cb->callback_id = callback_id;
  cb->lua_function_name = strdup(function_name);
  pthread_mutex_init(&cb->lock, NULL);

  user_data_t ud = {
    .data = cb
  };

  plugin_register_write(/* name = */ function_name,
                        /* callback  = */ clua_write,
                        /* user_data = */ &ud);

  DEBUG("Lua plugin: Successful call to lua_cb_register_write().");

  RETURN_LUA(L, 0);
} /* }}} int lua_cb_register_write */

static int lua_cb_register_filter(lua_State *L) /* {{{ */
{
  int nargs = lua_gettop(L);

  if (nargs != 1) {
    WARNING("Lua plugin: collectd_register_filter() called with an invalid "
            "number of arguments (%i).",
            nargs);
    RETURN_LUA(L, -1);
  }

  char function_name[DATA_MAX_NAME_LEN] = "";

  if (lua_isstring(L, 1)) {
    const char *tmp = lua_tostring(L, 1);
    ssnprintf(function_name, sizeof(function_name), "lua/%s", tmp);
  }

  int callback_id = clua_store_callback(L, 1);
  if (callback_id < 0) {
    ERROR("Lua plugin: Storing callback function failed.");
    RETURN_LUA(L, -1);
  }

  lua_State *thread = lua_newthread(L);
  if (thread == NULL) {
    ERROR("Lua plugin: lua_newthread failed.");
    RETURN_LUA(L, -1);
  }

  clua_store_thread(L, -1);
  lua_pop(L, 1);

  if (function_name[0] == '\0') {
    ssnprintf(function_name, sizeof(function_name), "lua/callback_%i",
              callback_id);
  }

  clua_callback_data_t *cb = calloc(1, sizeof(*cb));
  if (cb == NULL) {
    ERROR("Lua plugin: malloc failed.");
    RETURN_LUA(L, -1);
  }

  cb->lua_state = thread;
  cb->callback_id = callback_id;
  cb->lua_function_name = strdup(function_name);
  pthread_mutex_init(&cb->lock, NULL);

  user_data_t ud = {
    .data = cb
  };

  plugin_register_filter(
      /* name = */ function_name,
      /* callback  = */ clua_filter,
      /* user_data = */ &ud);

  DEBUG("Lua plugin: Successful call to lua_cb_register_filter().");
  RETURN_LUA(L, 0);
} /* }}} int lua_cb_register_filter */

static lua_c_function_t lua_c_functions[] = {
    {"log", lua_cb_log},
    {"dispatch_values", lua_cb_dispatch_values},
    {"register_read", lua_cb_register_read},
    {"register_write", lua_cb_register_write},
    {"register_filter", lua_cb_register_filter}};

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

  /* Register all the functions we implement in C */
  lua_newtable(script->lua_state);
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(lua_c_functions); i++) {
    lua_pushcfunction(script->lua_state, lua_c_functions[i].func);
    lua_setfield(script->lua_state, -2, lua_c_functions[i].name);
  }
  lua_setglobal(script->lua_state, "collectd");

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

  INFO("Lua plugin: File \"%s\" loaded succesfully", abs_path);

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

void module_register() {
  plugin_register_complex_config("lua", lua_config);
  plugin_register_shutdown("lua", lua_shutdown);
}

/* vim: set sw=2 sts=2 et fdm=marker : */
