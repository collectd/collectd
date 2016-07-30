/**
 * collectd - src/lua_exports.c
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


/* this file contains the functions exported to lua scripts */

/* log_info(string) */
static int log_info(lua_State *l)
{
  const char *message = lua_tostring(l, 1);
  INFO("%s", message);
  // return the number of values pushed on stack
  return 0;
}


static int register_exported_functions(lua_State *l){
  lua_register(l, "log_info", log_info);
  return 0;
}

