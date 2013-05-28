/**
 * collectd - src/jsonrpc_cb_topps.h
 * Copyright (C) 2013 Yves Mettier
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
 **/

#ifndef JSONRPC_CB_TOPPS_H
#define JSONRPC_CB_TOPPS_H

#define JSONRPC_CB_TABLE_TOPPS \
	{ "topps_get_top",   jsonrpc_cb_topps_get_top  },

int jsonrpc_cb_topps_get_top (struct json_object *params, struct json_object *result, const char **errorstring);

#endif /* JSONRPC_CB_TABLE_TOPPS_H */
