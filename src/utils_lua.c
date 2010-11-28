/**
 * collectd - src/utils_lua.c
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
 *   Florian Forster <octo at collectd.org>
 **/

#include "utils_lua.h"
#include "common.h"

static int ltoc_values (lua_State *l, /* {{{ */
    const data_set_t *ds,
    value_t *ret_values)
{
  size_t i;
  int status = 0;

  if (!lua_istable (l, -1))
    return (-1);

  /* Push initial key */
  lua_pushnil (l);
  for (i = 0; i < ((size_t) ds->ds_num); i++)
  {
    /* Pops old key and pushed new key and value. */
    status = lua_next (l, -2);
    if (status == 0) /* no more elements */
      break;

    ret_values[i] = luaC_tovalue (l, /* idx = */ -1, ds->ds[i].type);

    /* Pop the value */
    lua_pop (l, /* nelems = */ 1);
  }
  /* Pop the key */
  lua_pop (l, /* nelems = */ 1);

  return (status);
} /* }}} int ltoc_values */

static int ltoc_table_values (lua_State *l, int idx, /* {{{ */
    const data_set_t *ds, value_list_t *vl)
{
  int status;

  /* We're only called from "luaC_tovaluelist", which ensures that "idx" is an
   * absolute index (i.e. a positive number) */
  assert (idx > 0);

  lua_pushstring (l, "values");
  lua_gettable (l, idx);

  if (!lua_istable (l, -1))
  {
    NOTICE ("lua plugin: ltoc_table_values: The \"values\" member is not a table.");
    lua_pop (l, /* nelem = */ 1);
    return (-1);
  }

  vl->values_len = ds->ds_num;
  vl->values = calloc ((size_t) vl->values_len, sizeof (*vl->values));
  if (vl->values == NULL)
  {
    ERROR ("lua plugin: calloc failed.");
    vl->values_len = 0;
    lua_pop (l, /* nelem = */ 1);
    return (-1);
  }

  status = ltoc_values (l, ds, vl->values);

  lua_pop (l, /* nelem = */ 1);

  if (status != 0)
  {
    vl->values_len = 0;
    sfree (vl->values);
  }

  return (status);
} /* }}} int ltoc_table_values */

static int luaC_pushvalues (lua_State *l, const data_set_t *ds, const value_list_t *vl) /* {{{ */
{
  int i;

  assert (vl->values_len == ds->ds_num);

  lua_newtable (l);
  for (i = 0; i < vl->values_len; i++)
  {
    lua_pushinteger (l, (lua_Integer) i);
    luaC_pushvalue (l, vl->values[i], ds->ds[i].type);
    lua_settable (l, /* idx = */ -3);
  }

  return (0);
} /* }}} int luaC_pushvalues */

static int luaC_pushdstypes (lua_State *l, const data_set_t *ds) /* {{{ */
{
  int i;

  lua_newtable (l);
  for (i = 0; i < ds->ds_num; i++)
  {
    lua_pushinteger (l, (lua_Integer) i);
    lua_pushstring (l, DS_TYPE_TO_STRING (ds->ds[i].type));
    lua_settable (l, /* idx = */ -3);
  }

  return (0);
} /* }}} int luaC_pushdstypes */

static int luaC_pushdsnames (lua_State *l, const data_set_t *ds) /* {{{ */
{
  int i;

  lua_newtable (l);
  for (i = 0; i < ds->ds_num; i++)
  {
    lua_pushinteger (l, (lua_Integer) i);
    lua_pushstring (l, ds->ds[i].name);
    lua_settable (l, /* idx = */ -3);
  }

  return (0);
} /* }}} int luaC_pushdsnames */

/*
 * Public functions
 */
cdtime_t luaC_tocdtime (lua_State *l, int idx) /* {{{ */
{
  double d;

  if (!lua_isnumber (l, /* stack pos = */ idx))
    return (0);

  d = (double) lua_tonumber (l, idx);

  return (DOUBLE_TO_CDTIME_T (d));
} /* }}} int ltoc_table_cdtime */

int luaC_tostringbuffer (lua_State *l, int idx, /* {{{ */
    char *buffer, size_t buffer_size)
{
  const char *str;

  str = lua_tostring (l, idx);
  if (str == NULL)
    return (-1);

  sstrncpy (buffer, str, buffer_size);
  return (0);
} /* }}} int luaC_tostringbuffer */

value_t luaC_tovalue (lua_State *l, int idx, int ds_type) /* {{{ */
{
  value_t v;

  memset (&v, 0, sizeof (v));

  if (!lua_isnumber (l, idx))
    return (v);

  if (ds_type == DS_TYPE_GAUGE)
    v.gauge = (gauge_t) lua_tonumber (l, /* stack pos = */ -1);
  else if (ds_type == DS_TYPE_DERIVE)
    v.derive = (derive_t) lua_tointeger (l, /* stack pos = */ -1);
  else if (ds_type == DS_TYPE_COUNTER)
    v.counter = (counter_t) lua_tointeger (l, /* stack pos = */ -1);
  else if (ds_type == DS_TYPE_ABSOLUTE)
    v.absolute = (absolute_t) lua_tointeger (l, /* stack pos = */ -1);

  return (v);
} /* }}} value_t luaC_tovalue */

value_list_t *luaC_tovaluelist (lua_State *l, int idx) /* {{{ */
{
  const data_set_t *ds;
  value_list_t *vl;
  int status;
#if COLLECT_DEBUG
  int stack_top_before = lua_gettop (l);
#endif

  /* Convert relative indexes to absolute indexes, so it doesn't change when we
   * push / pop stuff. */
  if (idx < 1)
    idx += lua_gettop (l) + 1;

  /* Check that idx is in the valid range */
  if ((idx < 1) || (idx > lua_gettop (l)))
    return (NULL);

  vl = malloc (sizeof (*vl));
  if (vl == NULL)
    return (NULL);
  memset (vl, 0, sizeof (*vl));
  vl->values = NULL;
  vl->meta = NULL;

  /* Push initial key */
  lua_pushnil (l);
  while (lua_next (l, idx) != 0)
  {
    const char *key = lua_tostring (l, /* stack pos = */ -2);

    if (key == NULL)
    {
      DEBUG ("luaC_tovaluelist: Ignoring non-string key.");
    }
    else if (strcasecmp ("host", key) == 0)
      luaC_tostringbuffer (l, /* idx = */ -1,
          vl->host, sizeof (vl->host));
    else if (strcasecmp ("plugin", key) == 0)
      luaC_tostringbuffer (l, /* idx = */ -1,
          vl->plugin, sizeof (vl->plugin));
    else if (strcasecmp ("plugin_instance", key) == 0)
      luaC_tostringbuffer (l, /* idx = */ -1,
          vl->plugin_instance, sizeof (vl->plugin_instance));
    else if (strcasecmp ("type", key) == 0)
      luaC_tostringbuffer (l, /* idx = */ -1,
          vl->type, sizeof (vl->type));
    else if (strcasecmp ("type_instance", key) == 0)
      luaC_tostringbuffer (l, /* idx = */ -1,
          vl->type_instance, sizeof (vl->type_instance));
    else if (strcasecmp ("time", key) == 0)
      vl->time = luaC_tocdtime (l, -1);
    else if (strcasecmp ("interval", key) == 0)
      vl->interval = luaC_tocdtime (l, -1);
    else if (strcasecmp ("values", key) == 0)
    {
      /* This key is not handled here, because we have to assure "type" is read
       * first. */
    }
    else
    {
      DEBUG ("luaC_tovaluelist: Ignoring unknown key \"%s\".", key);
    }

    /* Pop the value */
    lua_pop (l, 1);
  }
  /* Pop the key */
  lua_pop (l, 1);

  ds = plugin_get_ds (vl->type);
  if (ds == NULL)
  {
    INFO ("lua plugin: Unable to lookup type \"%s\".", vl->type);
    sfree (vl);
    return (NULL);
  }

  status = ltoc_table_values (l, idx, ds, vl);
  if (status != 0)
  {
    WARNING ("lua plugin: ltoc_table_values failed.");
    sfree (vl);
    return (NULL);
  }

#if COLLECT_DEBUG
  assert (stack_top_before == lua_gettop (l));
#endif
  return (vl);
} /* }}} value_list_t *luaC_tovaluelist */

int luaC_pushcdtime (lua_State *l, cdtime_t t) /* {{{ */
{
  double d = CDTIME_T_TO_DOUBLE (t);

  lua_pushnumber (l, (lua_Number) d);
  return (0);
} /* }}} int luaC_pushcdtime */

int luaC_pushvalue (lua_State *l, value_t v, int ds_type) /* {{{ */
{
  if (ds_type == DS_TYPE_GAUGE)
    lua_pushnumber (l, (lua_Number) v.gauge);
  else if (ds_type == DS_TYPE_DERIVE)
    lua_pushinteger (l, (lua_Integer) v.derive);
  else if (ds_type == DS_TYPE_COUNTER)
    lua_pushinteger (l, (lua_Integer) v.counter);
  else if (ds_type == DS_TYPE_ABSOLUTE)
    lua_pushinteger (l, (lua_Integer) v.absolute);
  else
    return (-1);
  return (0);
} /* }}} int luaC_pushvalue */

int luaC_pushvaluelist (lua_State *l, const data_set_t *ds, const value_list_t *vl) /* {{{ */
{
  lua_newtable (l);

  lua_pushstring (l, vl->host);
  lua_setfield (l, /* idx = */ -2, "host");

  lua_pushstring (l, vl->plugin);
  lua_setfield (l, /* idx = */ -2, "plugin");
  lua_pushstring (l, vl->plugin_instance);
  lua_setfield (l, /* idx = */ -2, "plugin_instance");

  lua_pushstring (l, vl->type);
  lua_setfield (l, /* idx = */ -2, "type");
  lua_pushstring (l, vl->type_instance);
  lua_setfield (l, /* idx = */ -2, "type_instance");

  luaC_pushvalues (l, ds, vl);
  lua_setfield (l, /* idx = */ -2, "values");

  luaC_pushdstypes (l, ds);
  lua_setfield (l, /* idx = */ -2, "dstypes");

  luaC_pushdsnames (l, ds);
  lua_setfield (l, /* idx = */ -2, "dsnames");

  luaC_pushcdtime (l, vl->time);
  lua_setfield (l, /* idx = */ -2, "time");

  luaC_pushcdtime (l, vl->interval);
  lua_setfield (l, /* idx = */ -2, "interval");

  return (0);
} /* }}} int luaC_pushvaluelist */

/* vim: set sw=2 sts=2 et fdm=marker : */
