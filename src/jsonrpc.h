/**
 * collectd - src/jsonrpc.h
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

#ifndef JSONRPC_H
#define JSONRPC_H

#define JSONRPC_ERROR_CODE_32700_PARSE_ERROR        (-32700)
#define JSONRPC_ERROR_CODE_32600_INVALID_REQUEST    (-32600)
#define JSONRPC_ERROR_CODE_32601_METHOD_NOT_FOUND   (-32601)
#define JSONRPC_ERROR_CODE_32602_INVALID_PARAMS     (-32602)
#define JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR     (-32603)

int jsonrpc_cache_last_entry_find_and_ref(char ***ret_names, cdtime_t **ret_times, size_t *ret_number);
void jsonrpc_cache_entry_unref(int cache_id);


#endif /* JSONRPC_H */

