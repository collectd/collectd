/**
 * collectd - src/utils_lua.h
 * Copyright (C) 2010       Florian Forster
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
 *   Florian Forster <octo at collectd.org>
 **/

#ifndef UTILS_LUA_H
#define UTILS_LUA_H 1

#include "collectd.h"
#include "plugin.h"

#ifndef DONT_POISON_SPRINTF_YET
#error "Files including utils_lua.h need to define DONT_POISON_SPRINTF_YET."
#endif
#include <lua.h>

/*
 * access functions (stack -> C)
 */
cdtime_t luaC_tocdtime(lua_State *l, int idx);
int luaC_tostringbuffer(lua_State *l, int idx, char *buffer,
                        size_t buffer_size);
value_t luaC_tovalue(lua_State *l, int idx, int ds_type);
value_list_t *luaC_tovaluelist(lua_State *l, int idx);

/*
 * push functions (C -> stack)
 */
int luaC_pushcdtime(lua_State *l, cdtime_t t);
int luaC_pushvalue(lua_State *l, value_t v, int ds_type);
int luaC_pushvaluelist(lua_State *l, const data_set_t *ds,
                       const value_list_t *vl);

#endif /* UTILS_LUA_H */
/* vim: set sw=2 sts=2 et fdm=marker : */
