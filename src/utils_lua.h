/**
 * collectd - src/utils_lua.h
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
#ifndef UTILS_LUA_H
#define UTILS_LUA_H 1

#include "collectd.h"
#include "plugin.h"

#include <lua.h>

/*
 * access functions (stack -> C)
 */
cdtime_t      luaC_tocdtime (lua_State *l, int idx);
int           luaC_tostringbuffer (lua_State *l, int idx, char *buffer, size_t buffer_size);
value_t       luaC_tovalue (lua_State *l, int idx, int ds_type);
value_list_t *luaC_tovaluelist (lua_State *l, int idx);

#endif /* UTILS_LUA_H */
/* vim: set sw=2 sts=2 et fdm=marker : */
