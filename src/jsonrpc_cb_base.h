/**
 * collectd - src/jsonrpc_cb_base.h
 * Copyright (C) 2012 Yves Mettier, Cyril Feraudet
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Yves Mettier <ymettier at free dot fr>
 *   Cyril Feraudet <cyril at feraudet dot com>
 **/

#ifndef JSONRPC_CB_BASE_H
#define JSONRPC_CB_BASE_H

#define JSONRPC_CB_TABLE_BASE \
	{ "listval" , jsonrpc_cb_listval }, \
	{ "getval",   jsonrpc_cb_getval  },

int jsonrpc_cb_listval (struct json_object *params, struct json_object *result, const char **errorstring);
int jsonrpc_cb_getval (struct json_object *params, struct json_object *result, const char **errorstring);

#endif /* JSONRPC_CB_BASE_H */
