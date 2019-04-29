/**
 * collectd - src/utils_ovs.h
 *
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
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
 *   Volodymyr Mytnyk <volodymyrx.mytnyk@intel.com>
 *
 * Description:
 *  The OVS util module provides the following features:
 *   - Implements the OVS DB communication transport specified
 *     by RFC7047:
 *     * Connect/disconnect to OVS DB;
 *     * Recovery mechanism in case of OVS DB connection lost;
 *     * Subscription mechanism to OVS DB table update events
 *       (insert/modify/delete);
 *     * Send custom JSON request to OVS DB (poll table data, etc.)
 *     * Handling of echo request from OVS DB server to verify the
 *       liveness of the connection.
 *   - Provides YAJL helpers functions.
 *
 *  OVS DB API User Guide:
 *    All OVS DB function/structure names begins from 'ovs_db_*' prefix. To
 *   start using OVS DB API, client (plugin) should initialize the OVS DB
 *   object (`ovs_db_t') by calling `ovs_db_init' function. It initializes
 *   internal data and creates two main workers (threads). The result of the
 *   function is a pointer to new OVS DB object which can be used by other
 *   OVS DB API later and must be released by `ovs_db_destroy' function if
 *   the object isn't needed anymore.
 *    Once OVS DB API is initialized, the `init_cb' callback is called if
 *   the connection to OVS DB has been established. This callback is called
 *   every time the OVS DB is reconnected. So, if the client registers table
 *   update event callbacks or does any other OVS DB setup that can be lost
 *   after OVS DB reconnecting, it should be done in `init_cb' callback.
 *    The `ovs_db_table_cb_register` function is used to register OVS DB
 *   table update event callback and receive the table update notification
 *   when requested event occurs (registered callback is called). See
 *   function API for more info.
 *    To send custom JSON-RPC request to OVS DB, the `ovs_db_send_request'
 *   function is used. Please note, that connection to OVS DB should be
 *   established otherwise the function will return error.
 *    To verify the liveness of established connection, the OVS DB server
 *   sends echo request to the client with a given interval. The OVS utils
 *   takes care about this request and handles it properly.
 **/

#ifndef UTILS_OVS_H
#define UTILS_OVS_H

#include <yajl/yajl_gen.h>
#include <yajl/yajl_tree.h>

/* Forward declaration */
typedef struct ovs_db_s ovs_db_t;

/* OVS DB callback type declaration */
typedef void (*ovs_db_table_cb_t)(yajl_val jupdates);
typedef void (*ovs_db_result_cb_t)(yajl_val jresult, yajl_val jerror);

/* OVS DB structures */
struct ovs_db_callback_s {
  /*
   * This callback is called when OVS DB connection
   * has been established and ready to use. Client
   * can use this callback to configure OVS DB, e.g.
   * to subscribe to table update notification or poll
   * some OVS DB data. This field can be NULL.
   */
  void (*post_conn_init)(ovs_db_t *pdb);
  /*
   * This callback is called when OVS DB connection
   * has been lost. This field can be NULL.
   */
  void (*post_conn_terminate)(void);
};
typedef struct ovs_db_callback_s ovs_db_callback_t;

/* OVS DB defines */
#define OVS_DB_ADDR_NODE_SIZE 256
#define OVS_DB_ADDR_SERVICE_SIZE 128
#define OVS_DB_ADDR_UNIX_SIZE 108

/* OVS DB prototypes */

/*
 * NAME
 *   ovs_db_init
 *
 * DESCRIPTION
 *   Initialize OVS DB internal data. The `ovs_db_destroy' function
 *   shall destroy the returned object.
 *
 * PARAMETERS
 *   `node'        OVS DB Address.
 *   `service'     OVS DB service name.
 *   `unix'        OVS DB unix socket path.
 *   `cb'          OVS DB callbacks.
 *
 * RETURN VALUE
 *   New ovs_db_t object upon success or NULL if an error occurred.
 */
ovs_db_t *ovs_db_init(const char *node, const char *service,
                      const char *unix_path, ovs_db_callback_t *cb);

/*
 * NAME
 *   ovs_db_destroy
 *
 * DESCRIPTION
 *   Destroy OVS DB object referenced by `pdb'.
 *
 * PARAMETERS
 *   `pdb'         Pointer to OVS DB object.
 *
 * RETURN VALUE
 *   Zero upon success or non-zero if an error occurred.
 */
int ovs_db_destroy(ovs_db_t *pdb);

/*
 * NAME
 *   ovs_db_send_request
 *
 * DESCRIPTION
 *   Send JSON request to OVS DB server.
 *
 * PARAMETERS
 *   `pdb'         Pointer to OVS DB object.
 *   `method'      Request method name.
 *   `params'      Method params to be sent (JSON value as a string).
 *   `cb'          Result callback of the request. If NULL, the request
 *                 is sent asynchronously.
 *
 * RETURN VALUE
 *   Zero upon success or non-zero if an error occurred.
 */
int ovs_db_send_request(ovs_db_t *pdb, const char *method, const char *params,
                        ovs_db_result_cb_t cb);

/* callback types */
#define OVS_DB_TABLE_CB_FLAG_INITIAL 0x01U
#define OVS_DB_TABLE_CB_FLAG_INSERT 0x02U
#define OVS_DB_TABLE_CB_FLAG_DELETE 0x04U
#define OVS_DB_TABLE_CB_FLAG_MODIFY 0x08U
#define OVS_DB_TABLE_CB_FLAG_ALL 0x0FU

/*
 * NAME
 *   ovs_db_table_cb_register
 *
 * DESCRIPTION
 *   Subscribe a callback on OVS DB table event. It allows to
 *   receive notifications (`update_cb' callback is called) of
 *   changes to requested table.
 *
 * PARAMETERS
 *   `pdb'         Pointer to OVS DB object.
 *   `tb_name'     OVS DB Table name to be monitored.
 *   `tb_column'   OVS DB Table columns to be monitored. Last
 *                 element in the array should be NULL.
 *   `update_cb'   Callback function that is called when
 *                 requested table columns are changed.
 *   `cb'          Result callback of the request. If NULL, the call
 *                 becomes asynchronous.
 *                 Useful, if OVS_DB_TABLE_CB_FLAG_INITIAL is set.
 *   `flags'       Bit mask of:
 *                   OVS_DB_TABLE_CB_FLAG_INITIAL Receive initial values in
 *                                               result callback.
 *                   OVS_DB_TABLE_CB_FLAG_INSERT  Receive table insert events.
 *                   OVS_DB_TABLE_CB_FLAG_DELETE  Receive table remove events.
 *                   OVS_DB_TABLE_CB_FLAG_MODIFY  Receive table update events.
 *                   OVS_DB_TABLE_CB_FLAG_ALL     Receive all events.
 *
 * RETURN VALUE
 *   Zero upon success or non-zero if an error occurred.
 */
int ovs_db_table_cb_register(ovs_db_t *pdb, const char *tb_name,
                             const char **tb_column,
                             ovs_db_table_cb_t update_cb,
                             ovs_db_result_cb_t result_cb, unsigned int flags);

/*
 * OVS utils API
 */

/*
 * NAME
 *   ovs_utils_get_value_by_key
 *
 * DESCRIPTION
 *   Get YAJL value by object name.
 *
 * PARAMETERS
 *   `jval'        YAJL object value.
 *   `key'         Object key name.
 *
 * RETURN VALUE
 *   YAJL value upon success or NULL if key not found.
 */
yajl_val ovs_utils_get_value_by_key(yajl_val jval, const char *key);

/*
 * NAME
 *   ovs_utils_get_map_value
 *
 * DESCRIPTION
 *   Get OVS DB map value by given map key (rfc7047, "Notation" section).
 *
 * PARAMETERS
 *   `jval'        A 2-element YAJL array that represents a OVS DB map value.
 *   `key'         OVS DB map key name.
 *
 * RETURN VALUE
 *   YAJL value upon success or NULL if key not found.
 */
yajl_val ovs_utils_get_map_value(yajl_val jval, const char *key);

#endif
