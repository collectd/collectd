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

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"
#include "utils_lua.h"

/* Include the Lua API header files. */
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <libgen.h>
#include <pthread.h>

#define PLUGIN_INIT 0
#define PLUGIN_READ 1
#define PLUGIN_WRITE 2
#define PLUGIN_SHUTDOWN 3
#define PLUGIN_CONFIG 4
#define PLUGIN_NOTIFICATION 5

typedef struct lua_script_s {
  lua_State *lua_state;
  struct lua_script_s *next;
  char *script_path;
} lua_script_t;

typedef struct {
  lua_State *lua_state;
  char *lua_function_name;
  int callback_id;
} clua_callback_data_t;

static char base_path[PATH_MAX];
static lua_script_t *scripts;
static pthread_mutex_t lua_lock = PTHREAD_MUTEX_INITIALIZER;

static size_t lua_init_callbacks_num = 0;
static clua_callback_data_t **lua_init_callbacks = NULL;
static size_t lua_config_callbacks_num = 0;
static clua_callback_data_t **lua_config_callbacks = NULL;
static size_t lua_shutdown_callbacks_num = 0;
static clua_callback_data_t **lua_shutdown_callbacks = NULL;

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
    return -1;
  }

  return 0;
} /* }}} int clua_load_callback */

/* Store the threads in a global variable so they are not cleaned up by the
 * garbage collector. */
static int clua_store_thread(lua_State *L, int idx) /* {{{ */
{
  if (!lua_isthread(L, idx)) {
    return -1;
  }

  /* Copy the thread pointer */
  lua_pushvalue(L, idx);

  luaL_ref(L, LUA_REGISTRYINDEX);
  return 0;
} /* }}} int clua_store_thread */

static int clua_read(user_data_t *ud) /* {{{ */
{
  clua_callback_data_t *cb = ud->data;

  pthread_mutex_lock(&lua_lock);

  lua_State *L = cb->lua_state;

  int status = clua_load_callback(L, cb->callback_id);
  if (status != 0) {
    ERROR("Lua plugin: Unable to load callback \"%s\" (id %i).",
          cb->lua_function_name, cb->callback_id);
    pthread_mutex_unlock(&lua_lock);
    return -1;
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
    pthread_mutex_unlock(&lua_lock);
    return -1;
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

  pthread_mutex_unlock(&lua_lock);
  return status;
} /* }}} int clua_read */

static int clua_write(const data_set_t *ds, const value_list_t *vl, /* {{{ */
                      user_data_t *ud) {
  clua_callback_data_t *cb = ud->data;

  pthread_mutex_lock(&lua_lock);

  lua_State *L = cb->lua_state;

  int status = clua_load_callback(L, cb->callback_id);
  if (status != 0) {
    ERROR("Lua plugin: Unable to load callback \"%s\" (id %i).",
          cb->lua_function_name, cb->callback_id);
    pthread_mutex_unlock(&lua_lock);
    return -1;
  }
  /* +1 = 1 */

  status = luaC_pushvaluelist(L, ds, vl);
  if (status != 0) {
    lua_pop(L, 1); /* -1 = 0 */
    pthread_mutex_unlock(&lua_lock);
    ERROR("Lua plugin: luaC_pushvaluelist failed.");
    return -1;
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
    pthread_mutex_unlock(&lua_lock);
    return -1;
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
  pthread_mutex_unlock(&lua_lock);
  return status;
} /* }}} int clua_write */

static int clua_notification(const notification_t *notify,
                             user_data_t *ud) /* {{{ */
{
  clua_callback_data_t *cb = ud->data;

  pthread_mutex_lock(&lua_lock);

  lua_State *L = cb->lua_state;

  int status = clua_load_callback(L, cb->callback_id);
  if (status != 0) {
    ERROR("Lua plugin: Unable to load callback \"%s\" (id %i).",
          cb->lua_function_name, cb->callback_id);
    pthread_mutex_unlock(&lua_lock);
    return -1;
  }
  /* +1 = 1 */

  /* convert notification_t to table on stack */
  status = luaC_pushnotification(L, notify);
  if (status != 0) {
    lua_pop(L, 1); /* -1 = 0 */
    pthread_mutex_unlock(&lua_lock);
    ERROR("Lua plugin: luaC_notification failed.");
    return -1;
  }
  /* +1 = 2 */

  status = lua_pcall(L, 1, 1, 0); /* -2+1 = 1 */
  if (status != 0) {
    const char *errmsg = lua_tostring(L, -1);
    if (errmsg == NULL)
      ERROR("Lua plugin: Calling the notification callback failed. "
            "In addition, retrieving the error message failed.");
    else
      ERROR("Lua plugin: Calling the notification callback failed:\n%s",
            errmsg);
    lua_pop(L, 1); /* -1 = 0 */
    pthread_mutex_unlock(&lua_lock);
    return -1;
  }

  if (!lua_isnumber(L, -1)) {
    ERROR("Lua plugin: Notification function \"%s\" (id %i) did not return a "
          "numeric value.",
          cb->lua_function_name, cb->callback_id);
    status = -1;
  } else {
    status = (int)lua_tointeger(L, -1);
  }

  lua_pop(L, 1); /* -1 = 0 */
  pthread_mutex_unlock(&lua_lock);
  return status;
} /* }}} int clua_notification */

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

static void lua_cb_free(void *data) {
  clua_callback_data_t *cb = data;
  free(cb->lua_function_name);
  free(cb);
}

static int lua_cb_register_plugin_callbacks(lua_State *L,
                                            const char *function_name,
                                            clua_callback_data_t ***callbacks,
                                            size_t *callbacks_num,
                                            clua_callback_data_t *cb) /* {{{ */
{
  clua_callback_data_t **new_callbacks = NULL;

  pthread_mutex_lock(&lua_lock);
  new_callbacks = realloc(*callbacks, (*callbacks_num + 1) *
                                          sizeof(clua_callback_data_t *));
  if (new_callbacks == NULL) {
    pthread_mutex_unlock(&lua_lock);
    return luaL_error(L, "Reallocate %s callback stack (%d + 1) failed",
                      function_name, *callbacks_num);
  }
  new_callbacks[*callbacks_num] = cb;
  *callbacks = new_callbacks;
  *callbacks_num = *callbacks_num + 1;
  pthread_mutex_unlock(&lua_lock);

  return 0;
} /* }}} int lua_cb_register_plugin_callbacks */

static int lua_cb_register_generic(lua_State *L, int type) /* {{{ */
{
  int nargs = lua_gettop(L);

  if (nargs != 1)
    return luaL_error(L, "Invalid number of arguments (%d != 1)", nargs);

  char subname[DATA_MAX_NAME_LEN];
  if (!lua_isfunction(L, 1) && lua_isstring(L, 1)) {
    const char *fname = lua_tostring(L, 1);
    ssnprintf(subname, sizeof(subname), "%s()", fname);

    lua_getglobal(L, fname); // Push function into stack
    lua_remove(L, 1);        // Remove string from stack
    if (!lua_isfunction(L, -1)) {
      return luaL_error(L, "Unable to find function '%s'", fname);
    }
  } else {
    lua_getfield(L, LUA_REGISTRYINDEX, "collectd:callback_num");
    int tmp = lua_tointeger(L, -1);
    ssnprintf(subname, sizeof(subname), "callback_%d", tmp);
    lua_pop(L, 1); // Remove old value from stack
    lua_pushinteger(L, tmp + 1);
    lua_setfield(L, LUA_REGISTRYINDEX, "collectd:callback_num"); // pops value
  }

  luaL_checktype(L, 1, LUA_TFUNCTION);

  lua_getfield(L, LUA_REGISTRYINDEX, "collectd:script_path");
  char function_name[DATA_MAX_NAME_LEN];
  ssnprintf(function_name, sizeof(function_name), "lua/%s/%s",
            lua_tostring(L, -1), subname);
  lua_pop(L, 1);

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

  if (PLUGIN_READ == type) {
    int status = plugin_register_complex_read(/* group = */ "lua",
                                              /* name      = */ function_name,
                                              /* callback  = */ clua_read,
                                              /* interval  = */ 0,
                                              &(user_data_t){
                                                  .data = cb,
                                                  .free_func = lua_cb_free,
                                              });

    if (status != 0)
      return luaL_error(L, "%s", "plugin_register_complex_read failed");
    return 0;
  } else if (PLUGIN_WRITE == type) {
    int status = plugin_register_write(/* name = */ function_name,
                                       /* callback  = */ clua_write,
                                       &(user_data_t){
                                           .data = cb,
                                           .free_func = lua_cb_free,
                                       });

    if (status != 0)
      return luaL_error(L, "%s", "plugin_register_write failed");
    return 0;
  } else if (PLUGIN_INIT == type) {
    int status = lua_cb_register_plugin_callbacks(L, cb->lua_function_name,
                                                  &lua_init_callbacks,
                                                  &lua_init_callbacks_num, cb);
    if (status != 0)
      return luaL_error(L, "lua_cb_register_plugin_callbacks(init) failed");
    return 0;
  } else if (PLUGIN_SHUTDOWN == type) {
    int status = lua_cb_register_plugin_callbacks(
        L, cb->lua_function_name, &lua_shutdown_callbacks,
        &lua_shutdown_callbacks_num, cb);
    if (status != 0)
      return luaL_error(L, "lua_cb_register_plugin_callbacks(shutdown) failed");
    return 0;
  } else if (PLUGIN_CONFIG == type) {
    int status = lua_cb_register_plugin_callbacks(
        L, cb->lua_function_name, &lua_config_callbacks,
        &lua_config_callbacks_num, cb);
    if (status != 0)
      return luaL_error(L, "lua_cb_register_plugin_callbacks(config) failed");
    return 0;
  } else if (PLUGIN_NOTIFICATION == type) {
    int status =
        plugin_register_notification(/* name = */ function_name,
                                     /* callback  = */ clua_notification,
                                     &(user_data_t){
                                         .data = cb,
                                         .free_func = lua_cb_free,
                                     });
    if (status != 0)
      return luaL_error(L, "plugin_register_notification failed");
    return 0;
  } else {
    return luaL_error(L, "%s", "lua_cb_register_generic unsupported type");
  }
} /* }}} int lua_cb_register_generic */

static int lua_cb_register_read(lua_State *L) {
  return lua_cb_register_generic(L, PLUGIN_READ);
}

static int lua_cb_register_write(lua_State *L) {
  return lua_cb_register_generic(L, PLUGIN_WRITE);
}

static int lua_cb_register_init(lua_State *L) {
  return lua_cb_register_generic(L, PLUGIN_INIT);
}

static int lua_cb_register_shutdown(lua_State *L) {
  return lua_cb_register_generic(L, PLUGIN_SHUTDOWN);
}

static int lua_cb_register_config(lua_State *L) {
  return lua_cb_register_generic(L, PLUGIN_CONFIG);
}

static int lua_cb_register_notification(lua_State *L) {
  return lua_cb_register_generic(L, PLUGIN_NOTIFICATION);
}

static const luaL_Reg collectdlib[] = {
    {"log_debug", lua_cb_log_debug},
    {"log_error", lua_cb_log_error},
    {"log_info", lua_cb_log_info},
    {"log_notice", lua_cb_log_notice},
    {"log_warning", lua_cb_log_warning},
    {"dispatch_values", lua_cb_dispatch_values},
    {"register_read", lua_cb_register_read},
    {"register_write", lua_cb_register_write},
    {"register_init", lua_cb_register_init},
    {"register_shutdown", lua_cb_register_shutdown},
    {"register_config", lua_cb_register_config},
    {"register_notification", lua_cb_register_notification},
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
    free(script->script_path);
  }

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
    return -1;
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

  return 0;
} /* }}} int lua_script_init */

static int lua_script_load(const char *script_path) /* {{{ */
{
  lua_script_t *script = malloc(sizeof(*script));
  if (script == NULL) {
    ERROR("Lua plugin: malloc failed.");
    return -1;
  }

  int status = lua_script_init(script);
  if (status != 0) {
    lua_script_free(script);
    return status;
  }

  status = luaL_loadfile(script->lua_state, script_path);
  if (status != 0) {
    ERROR("Lua plugin: luaL_loadfile failed: %s",
          lua_tostring(script->lua_state, -1));
    lua_pop(script->lua_state, 1);
    lua_script_free(script);
    return -1;
  }
  script->script_path = strdup(script_path);

  lua_pushstring(script->lua_state, script_path);
  lua_setfield(script->lua_state, LUA_REGISTRYINDEX, "collectd:script_path");
  lua_pushinteger(script->lua_state, 0);
  lua_setfield(script->lua_state, LUA_REGISTRYINDEX, "collectd:callback_num");

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
      ERROR("Lua plugin: Executing script \"%s\" failed: %s", script_path,
            errmsg);
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

  if (status != 0)
    return -1;

  return 0;
} /* }}} int lua_script_load */

static int lua_config_base_path(const oconfig_item_t *ci) /* {{{ */
{
  int status = cf_util_get_string_buffer(ci, base_path, sizeof(base_path));
  if (status != 0)
    return status;

  size_t len = strlen(base_path);
  while ((len > 0) && (base_path[len - 1] == '/')) {
    len--;
    base_path[len] = '\0';
  }

  DEBUG("Lua plugin: base_path = \"%s\";", base_path);

  return 0;
} /* }}} int lua_config_base_path */

static int lua_config_script(const oconfig_item_t *ci) /* {{{ */
{
  char rel_path[PATH_MAX];

  int status = cf_util_get_string_buffer(ci, rel_path, sizeof(rel_path));
  if (status != 0)
    return status;

  char abs_path[PATH_MAX];

  if (base_path[0] == '\0')
    sstrncpy(abs_path, rel_path, sizeof(abs_path));
  else
    ssnprintf(abs_path, sizeof(abs_path), "%s/%s", base_path, rel_path);

  DEBUG("Lua plugin: abs_path = \"%s\";", abs_path);

  status = lua_script_load(abs_path);
  if (status != 0)
    return status;

  INFO("Lua plugin: File \"%s\" loaded successfully", abs_path);

  return 0;
} /* }}} int lua_config_script */

/*
 * <Plugin lua>
 *   ...
 *   Script "script1.lua"
 *   <Module "script1.lua" "...">
 *     Key1 Value1
 *     ...
 *     KeyN ValueN
 *   </Module>
 * </Plugin>
 *
 * e.g. Use common configuration for script1.lua and script2.lua and
 *      apply different Url only for script2.lua
 *
 * <Plugin lua>
 *  Script "script1.lua"
 *  Script "script2.lua"
 *  <Module>
 *   Url "example.com"
 *   ... many key value pairs ...
 *  </Module>
 *  <Module "script2.lua">
 *    Url "example.org"
 *  </Module>
 * </Plugin>
 */
static int lua_config_module(oconfig_item_t *ci) {
  lua_script_t *script = scripts;
  if (!script) {
    /* Script ... must be placed in advance before Module ... block. */
    if (ci->values_num && ci->values->type == OCONFIG_TYPE_STRING) {
      ERROR("Lua plugin: <Module '%s'> does not match any existing Script ...",
            ci->values->value.string);
    } else {
      ERROR("Lua plugin: <Module ...> does not match any existing Script ...");
    }
    return -1;
  }
  while (script) {
    bool matched = false;
    if (ci->values) {
      /* <Module "..."> block, search matched Script ... */
      for (int i = 0; i < ci->values_num; i++) {
        if (ci->values[i].type == OCONFIG_TYPE_STRING) {
          char path[PATH_MAX];
          ssnprintf(path, sizeof(path), "%s/%s", base_path,
                    ci->values[i].value.string);
          if (strcmp(script->script_path, path) == 0) {
            matched = true;
          }
        }
      }
      if (!matched) {
        WARNING(
            "Lua plugin: <Module '%s'> does not match any existing Script ...",
            script->script_path);
      }
    } else {
      /* <Module> block, then apply it to every Lua scripts. */
      DEBUG("Lua plugin: <Module> matches any existing Script...");
      matched = true;
    }
    if (matched) {
      lua_newtable(script->lua_state);
      for (int i = 0; i < ci->children_num; i++) {
        luaC_pushoconfigitem(script->lua_state, &ci->children[i]);
      }
      /* associate module configuration by script_path. */
      lua_setglobal(script->lua_state, script->script_path);
    }
    script = script->next;
  }
  return 0;
}

/*
 * <Plugin lua>
 *   BasePath "/"
 *   Script "script1.lua"
 *   Script "script2.lua"
 *   <Module>
 *      ...
 *   </Module>
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
    } else if (strcasecmp("Module", child->key) == 0) {
      status = lua_config_module(child);
    } else {
      ERROR("Lua plugin: Option `%s' is not allowed here.", child->key);
      status = 1;
    }
  }

  return status;
} /* }}} int lua_config */

static int lua_push_callback_data(int callback_type,
                                  clua_callback_data_t *callback) {
  if (callback_type != PLUGIN_CONFIG) {
    return -1;
  }
  lua_script_t *script = scripts;
  while (script) {
    char function_name[DATA_MAX_NAME_LEN];
    ssnprintf(function_name, sizeof(function_name), "lua/%s/callback_",
              script->script_path);
    /* search /lua/SCRIPT_PATH/callback_N */
    if (strstr(callback->lua_function_name, function_name)) {
      DEBUG("Lua plugin: Global script configuration found '%s'",
            script->script_path);
      /* load configuration onto stack */
      lua_getglobal(callback->lua_state, script->script_path);
      break;
    }
    script = script->next;
  }
  return 0;
}

static int lua_execute_callback(int callback_type, clua_callback_data_t *cb,
                                const char *label) /* {{{ */
{
  lua_State *L = cb->lua_state;
  int status = clua_load_callback(L, cb->callback_id);
  if (status != 0) {
    ERROR("Lua plugin: Unable to load %s callback \"%s\" (id %i).", label,
          cb->lua_function_name, cb->callback_id);
    return -1;
  }

  lua_push_callback_data(callback_type, cb);
  if (callback_type == PLUGIN_CONFIG) {
    status = lua_pcall(L, 1, 1, 0);
  } else {
    status = lua_pcall(L, 0, 1, 0);
  }
  if (status != 0) {
    const char *errmsg = lua_tostring(L, -1);
    if (errmsg == NULL) {
      ERROR("Lua plugin: Calling a %s callback failed. "
            "In addition, retrieving the error message failed.",
            label);
    } else {
      ERROR("Lua plugin: Calling a %s callback failed: %s", label, errmsg);
    }
    lua_pop(L, 1);
    return -1;
  }
  if (!lua_isnumber(L, -1)) {
    ERROR("Lua plugin: %s function \"%s\" (id %i) did not return a numeric "
          "status.",
          label, cb->lua_function_name, cb->callback_id);
    status = -1;
  } else {
    status = (int)lua_tointeger(L, -1);
  }
  lua_pop(L, 1);
  return 0;
} /* }}} lua_execute_callback */

static int lua_execute_callbacks(int callback_type, const char *label,
                                 clua_callback_data_t **callbacks,
                                 size_t callbacks_num) /* {{{ */
{
  for (size_t i = 0; i < callbacks_num; i++) {
    pthread_mutex_lock(&lua_lock);
    lua_execute_callback(callback_type, callbacks[i], label);
    pthread_mutex_unlock(&lua_lock);
  }
  return 0;
} /* }}} lua_execute_callbacks */

static void lua_free_callbacks(const char *label,
                               clua_callback_data_t ***callbacks,
                               size_t *callbacks_num) /* {{{ */
{
  pthread_mutex_lock(&lua_lock);
  for (size_t i = 0; i < *callbacks_num; i++) {
    clua_callback_data_t *cb = (*callbacks)[i];
    free(cb->lua_function_name);
    free(cb);
  }
  if (*callbacks_num) {
    free(*callbacks);
    *callbacks = NULL;
    *callbacks_num = 0;
  }
  pthread_mutex_unlock(&lua_lock);
} /* }}} lua_free_callbacks */

static int lua_shutdown(void) /* {{{ */
{
  if (lua_shutdown_callbacks_num > 0) {
    /* call every shutdown callbacks here */
    lua_execute_callbacks(PLUGIN_SHUTDOWN, "shutdown", lua_shutdown_callbacks,
                          lua_shutdown_callbacks_num);
  }
  lua_free_callbacks("init", &lua_init_callbacks, &lua_init_callbacks_num);
  lua_free_callbacks("config", &lua_config_callbacks,
                     &lua_config_callbacks_num);
  lua_free_callbacks("shutdown", &lua_shutdown_callbacks,
                     &lua_shutdown_callbacks_num);
  lua_script_free(scripts);

  return 0;
} /* }}} int lua_shutdown */

static int lua_init(void) {
  int status = 0;

  if (lua_config_callbacks_num > 0) {
    status =
        lua_execute_callbacks(PLUGIN_CONFIG, "config", lua_config_callbacks,
                              lua_config_callbacks_num);
    if (status != 0) {
      ERROR("Lua plugin: lua_execute_callbacks failed '%d'", status);
      return status;
    }
  }

  if (lua_init_callbacks_num > 0) {
    status = lua_execute_callbacks(PLUGIN_INIT, "init", lua_init_callbacks,
                                   lua_init_callbacks_num);
    if (status != 0) {
      ERROR("Lua plugin: lua_execute_callbacks failed '%d'", status);
      return status;
    }
  }

  return status;
}

void module_register(void) {
  plugin_register_complex_config("lua", lua_config);
  plugin_register_init("lua", lua_init);
  plugin_register_shutdown("lua", lua_shutdown);
}
