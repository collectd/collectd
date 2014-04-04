/**
 * collectd - src/ceph.c
 * Copyright (C) 2011  New Dream Network
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
 *   Colin McCabe <cmccabe@alumni.cmu.edu>
 *   Dennis Zou <yunzou@cisco.com>
 *   Dan Ryder <daryder@cisco.com>
 **/

#define _BSD_SOURCE

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <json/json.h>
#include <json/json_object_private.h> /* need for struct json_object_iter */
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#define MAX_RRD_DS_NAME_LEN 20

#define RETRY_ON_EINTR(ret, expr) \
	while(1) { \
		ret = expr; \
		if (ret >= 0) \
			break; \
		ret = -errno; \
		if (ret != -EINTR) \
			break; \
	}

/** Timeout interval in seconds */
#define CEPH_TIMEOUT_INTERVAL 1

/** Maximum path length for a UNIX domain socket on this system */
#define UNIX_DOMAIN_SOCK_PATH_MAX (sizeof(((struct sockaddr_un*)0)->sun_path))

/******* ceph_daemon *******/
struct ceph_daemon
{
	/** Version of the admin_socket interface */
	uint32_t version;
	/** daemon name **/
	char name[DATA_MAX_NAME_LEN];

	int dset_num;

	/** Path to the socket that we use to talk to the ceph daemon */
	char asok_path[UNIX_DOMAIN_SOCK_PATH_MAX];

	/** The set of  key/value pairs that this daemon reports
	 * dset.type		The daemon name
	 * dset.ds_num		Number of data sources (key/value pairs) 
	 * dset.ds		Dynamically allocated array of key/value pairs
	 */
	//struct data_set_s dset;
	/** Dynamically allocated array **/
	struct data_set_s *dset;
	int **pc_types;
};

enum perfcounter_type_d
{
	PERFCOUNTER_LONGRUNAVG = 0x4, PERFCOUNTER_COUNTER = 0x8,
};

/** Array of daemons to monitor */
static struct ceph_daemon **g_daemons = NULL;

/** Number of elements in g_daemons */
static int g_num_daemons = 0;

static void ceph_daemon_print(const struct ceph_daemon *d)
{
	DEBUG("name=%s, asok_path=%s", d->name, d->asok_path);
}

static void ceph_daemons_print(void)
{
	int i;
	for (i = 0; i < g_num_daemons; ++i)
	{
		ceph_daemon_print(g_daemons[i]);
	}
}

/*static void ceph_daemon_free(struct ceph_daemon *d)
 {
 plugin_unregister_data_set(d->dset.type);
 sfree(d->dset.ds);
 sfree(d);
 }*/
static void ceph_daemon_free(struct ceph_daemon *d)
{
	int i = 0;
	for (; i < d->dset_num; i++)
	{
		plugin_unregister_data_set((d->dset + i)->type);
		sfree(d->dset->ds);
		sfree(d->pc_types[i]);
	}
	sfree(d->dset);
	sfree(d->pc_types);
	sfree(d);
}

static void compact_ds_name(char *source, char *dest)
{
	int keys_num = 0, i;
	char *save_ptr = NULL, *tmp_ptr = source;
	char *keys[16];
	char len_str[3];
	char tmp[DATA_MAX_NAME_LEN];
	int reserved = 0;
	int offset = 0;
	memset(tmp, 0, sizeof(tmp));
	if (source == NULL || dest == NULL || source[0] == '\0' || dest[0] != '\0')
	{
		return;
	}
	size_t src_len = strlen(source);
	snprintf(len_str, sizeof(len_str), "%zu", src_len);
	unsigned char append_status = 0x0;
	append_status |= (source[src_len - 1] == '-') ? 0x1 : 0x0;
	append_status |= (source[src_len - 1] == '+') ? 0x2 : 0x0;
	while ((keys[keys_num] = strtok_r(tmp_ptr, ":_-+", &save_ptr)) != NULL)
	{
		tmp_ptr = NULL;
		/** capitalize 1st char **/
		keys[keys_num][0] = toupper(keys[keys_num][0]);
		keys_num++;
		if (keys_num >= 16)
			break;
	}
	/** concatenate each part of source string **/
	for (i = 0; i < keys_num; i++)
	{
		strcat(tmp, keys[i]);
	}
	tmp[DATA_MAX_NAME_LEN - 1] = '\0';
	/** to coordinate limitation of length of ds name from RRD
	 *  we will truncate ds_name
	 *  when the its length is more than
	 *  MAX_RRD_DS_NAME_LEN
	 */
	if (strlen(tmp) > MAX_RRD_DS_NAME_LEN - 1)
	{
		append_status |= 0x4;
		/** we should reserve space for
		 * len_str
		 */
		reserved += 2;
	}
	if (append_status & 0x1)
	{
		/** we should reserve space for
		 * "Minus"
		 */
		reserved += 5;
	}
	if (append_status & 0x2)
	{
		/** we should reserve space for
		 * "Plus"
		 */
		reserved += 4;
	}
	snprintf(dest, MAX_RRD_DS_NAME_LEN - reserved, "%s", tmp);
	offset = strlen(dest);
	switch (append_status)
	{
	case 0x1:
		memcpy(dest + offset, "Minus", 5);
		break;
	case 0x2:
		memcpy(dest + offset, "Plus", 5);
		break;
	case 0x4:
		memcpy(dest + offset, len_str, 2);
		break;
	case 0x5:
		memcpy(dest + offset, "Minus", 5);
		memcpy(dest + offset + 5, len_str, 2);
		break;
	case 0x6:
		memcpy(dest + offset, "Plus", 4);
		memcpy(dest + offset + 4, len_str, 2);
		break;
	default:
		break;
	}
}
static int parse_keys(const char *key_str, char *dset_name, char *ds_name)
{
	char *ptr, *rptr;
	size_t dset_name_len = 0;
	size_t ds_name_len = 0;
	char tmp_ds_name[DATA_MAX_NAME_LEN];
	memset(tmp_ds_name, 0, sizeof(tmp_ds_name));
	if (dset_name == NULL || ds_name == NULL || key_str == NULL
			|| key_str[0] == '\0' || dset_name[0] != '\0' || ds_name[0] != '\0')
	{
		return -1;
	}
	if ((ptr = strchr(key_str, '.')) == NULL
			|| (rptr = strrchr(key_str, '.')) == NULL)
	{
		strncpy(dset_name, key_str, DATA_MAX_NAME_LEN - 1);
		strncpy(tmp_ds_name, key_str, DATA_MAX_NAME_LEN - 1);
		goto compact;
	}
	dset_name_len =
			(ptr - key_str) > (DATA_MAX_NAME_LEN - 1) ?
					(DATA_MAX_NAME_LEN - 1) : (ptr - key_str);
	memcpy(dset_name, key_str, dset_name_len);
	ds_name_len =
			(rptr - ptr) > DATA_MAX_NAME_LEN ? DATA_MAX_NAME_LEN : (rptr - ptr);
	if (ds_name_len == 0)
	{ /** only have two keys **/
		if (!strncmp(rptr + 1, "type", 4))
		{/** if last key is "type",ignore **/
			strncpy(tmp_ds_name, dset_name, DATA_MAX_NAME_LEN - 1);
		}
		else
		{/** if last key isn't "type", copy last key **/
			strncpy(tmp_ds_name, rptr + 1, DATA_MAX_NAME_LEN - 1);
		}
	}
	else if (!strncmp(rptr + 1, "type", 4))
	{/** more than two keys **/
		memcpy(tmp_ds_name, ptr + 1, ds_name_len - 1);
	}
	else
	{/** copy whole keys **/
		strncpy(tmp_ds_name, ptr + 1, DATA_MAX_NAME_LEN - 1);
	}
	compact: compact_ds_name(tmp_ds_name, ds_name);
	return 0;
}

int get_matching_dset(const struct ceph_daemon *d, const char *name)
{
	int idx;
	for (idx = 0; idx < d->dset_num; ++idx)
	{
		if (strcmp(d->dset[idx].type, name) == 0)
		{
			return idx;
		}
	}
	return -1;
}

int get_matching_value(const struct data_set_s *dset, const char *name,
		int num_values)
{
	int idx;
	for (idx = 0; idx < num_values; ++idx)
	{
		if (strcmp(dset->ds[idx].name, name) == 0)
		{
			return idx;
		}
	}
	return -1;
}

static int ceph_daemon_add_ds_entry(struct ceph_daemon *d, const char *name,
		int pc_type)
{
	struct data_source_s *ds;
	struct data_set_s *dset;
	struct data_set_s *dset_array;
	int **pc_types_array = NULL;
	int *pc_types;
	int *pc_types_new;
	int idx = 0;
	if (strlen(name) + 1 > DATA_MAX_NAME_LEN)
		return -ENAMETOOLONG;
	char dset_name[DATA_MAX_NAME_LEN];
	char ds_name[MAX_RRD_DS_NAME_LEN];
	memset(dset_name, 0, sizeof(dset_name));
	memset(ds_name, 0, sizeof(ds_name));
	if (parse_keys(name, dset_name, ds_name))
		return 1;
	idx = get_matching_dset(d, dset_name);
	if (idx == -1)
	{/* need to add a dset **/
		dset_array = realloc(d->dset,
				sizeof(struct data_set_s) * (d->dset_num + 1));
		if (!dset_array)
			return -ENOMEM;
		pc_types_array = realloc(d->pc_types,
				sizeof(int *) * (d->dset_num + 1));
		if (!pc_types_array)
			return -ENOMEM;
		dset = &dset_array[d->dset_num];
		/** this step is very important, otherwise,
		 *  realloc for dset->ds will tricky because of
		 *  a random addr in dset->ds
		 */
		memset(dset, 0, sizeof(struct data_set_s));
		dset->ds_num = 0;
		snprintf(dset->type, DATA_MAX_NAME_LEN, "%s", dset_name);
		pc_types = pc_types_array[d->dset_num] = NULL;
		d->dset = dset_array;
	}
	else
	{
		dset = &d->dset[idx];
		pc_types = d->pc_types[idx];
	}
	struct data_source_s *ds_array = realloc(dset->ds,
			sizeof(struct data_source_s) * (dset->ds_num + 1));
	if (!ds_array)
	{
		return -ENOMEM;
	}
	pc_types_new = realloc(pc_types, sizeof(int) * (dset->ds_num + 1));
	if (!pc_types_new)
	{
		return -ENOMEM;
	}
	dset->ds = ds_array;
	if (idx == -1)
	{
		pc_types_array[d->dset_num] = pc_types_new;
		d->pc_types = pc_types_array;
		d->pc_types[d->dset_num][dset->ds_num] = pc_type;
		d->dset_num++;
	}
	else
	{
		d->pc_types[idx] = pc_types_new;
		d->pc_types[idx][dset->ds_num] = pc_type;
	}
	ds = &ds_array[dset->ds_num++];
	snprintf(ds->name, MAX_RRD_DS_NAME_LEN, "%s", ds_name);
	ds->type =
			(pc_type & PERFCOUNTER_COUNTER) ? DS_TYPE_COUNTER : DS_TYPE_GAUGE;
	ds->min = NAN;
	ds->max = NAN;
	return 0;
}

/******* ceph_config *******/
static int cc_handle_str(struct oconfig_item_s *item, char *dest, int dest_len)
{
	const char *val;
	if (item->values_num != 1)
	{
		return -ENOTSUP;
	}
	if (item->values[0].type != OCONFIG_TYPE_STRING)
	{
		return -ENOTSUP;
	}
	val = item->values[0].value.string;
	if (snprintf(dest, dest_len, "%s", val) > (dest_len - 1))
	{
		ERROR("ceph plugin: configuration parameter '%s' is too long.\n",
				item->key);
		return -ENAMETOOLONG;
	}
	return 0;
}

static int cc_add_daemon_config(oconfig_item_t *ci)
{
        int ret, i;
        struct ceph_daemon *array, *nd, cd;
        memset(&cd, 0, sizeof(struct ceph_daemon));

        if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
        {
                WARNING("ceph plugin: `Daemon' blocks need exactly one string argument.");
                return (-1);
        }

        ret = cc_handle_str(ci, cd.name, DATA_MAX_NAME_LEN);
        if (ret)
                return ret;

        for (i=0; i < ci->children_num; i++)
        {
                oconfig_item_t *child = ci->children + i;

                if (strcasecmp("SocketPath", child->key) == 0)
                {
                        ret = cc_handle_str(child, cd.asok_path, sizeof(cd.asok_path));
                        if (ret)
                                return ret;
                }
                else
                {
                        WARNING("ceph plugin: ignoring unknown option %s", child->key);
                }
        }
        if (cd.name[0] == '\0')
        {
                ERROR("ceph plugin: you must configure a daemon name.\n");
                return -EINVAL;
        }
        else if (cd.asok_path[0] == '\0')
        {
                ERROR("ceph plugin(name=%s): you must configure an administrative "
                "socket path.\n", cd.name);
                return -EINVAL;
        }
        else if (!((cd.asok_path[0] == '/')
                        || (cd.asok_path[0] == '.' && cd.asok_path[1] == '/')))
        {
                ERROR("ceph plugin(name=%s): administrative socket paths must begin with "
                                "'/' or './' Can't parse: '%s'\n", cd.name, cd.asok_path);
                return -EINVAL;
        }
        array = realloc(g_daemons,
                        sizeof(struct ceph_daemon *) * (g_num_daemons + 1));
        if (array == NULL)
        {
                /* The positive return value here indicates that this is a
                 * runtime error, not a configuration error.  */
                return ENOMEM;
        }
        g_daemons = (struct ceph_daemon**) array;
        nd = malloc(sizeof(struct ceph_daemon));
        if (!nd)
                return ENOMEM;
        memcpy(nd, &cd, sizeof(struct ceph_daemon));
        g_daemons[g_num_daemons++] = nd;
        return 0;
}

static int ceph_config(oconfig_item_t *ci)
{
	int ret, i;

	for (i = 0; i < ci->children_num; ++i)
	{
		oconfig_item_t *child = ci->children + i;
		if (strcasecmp("Daemon", child->key) == 0)
		{
			ret = cc_add_daemon_config(child);
			if (ret)
				return ret;
		}
		else
		{
			WARNING("ceph plugin: ignoring unknown option %s", child->key);
		}
	}
	return 0;
}

/******* JSON parsing *******/
typedef int (*node_handler_t)(void*, json_object*, const char*);

/** Perform a depth-first traversal of the JSON parse tree,
 * calling node_handler at each node.*/
static int traverse_json_impl(json_object *jo, char *key, int max_key,
		node_handler_t handler, void *handler_arg)
{
	struct json_object_iter iter;
	int ret, plen, klen;

	if (json_object_get_type(jo) != json_type_object)
		return 0;
	plen = strlen(key);
	json_object_object_foreachC(jo, iter)
	{
		klen = strlen(iter.key);
		if (plen + klen + 2 > max_key)
			return -ENAMETOOLONG;
		if (plen != 0)
			strncat(key, ".", max_key); /* really should be strcat */
		strncat(key, iter.key, max_key);

		ret = handler(handler_arg, iter.val, key);
		if (ret == 1)
		{
			ret = traverse_json_impl(iter.val, key, max_key, handler,
					handler_arg);
		}
		else if (ret != 0)
		{
			return ret;
		}

		key[plen] = '\0';
	}
	return 0;
}

static int traverse_json(const char *json, node_handler_t handler,
		void *handler_arg)
{
	json_object *root;
	char buf[128];
	buf[0] = '\0';
	root = json_tokener_parse(json);
	if (!root)
		return -EDOM;
	int result = traverse_json_impl(root, buf, sizeof(buf), handler, handler_arg);
	json_object_put(root);
	return result;
}

static int node_handler_define_schema(void *arg, json_object *jo,
		const char *key)
{
	struct ceph_daemon *d = (struct ceph_daemon *) arg;
	int pc_type;
	if (json_object_get_type(jo) == json_type_object)
		return 1;
	else if (json_object_get_type(jo) != json_type_int)
		return -EDOM;
	pc_type = json_object_get_int(jo);
	DEBUG("\nceph_daemon_add_ds_entry(d=%s,key=%s,pc_type=%04x)",
			d->name, key, pc_type);
	return ceph_daemon_add_ds_entry(d, key, pc_type);
}
struct values_holder
{
	int values_len;
	value_t *values;
};

/** A set of values_t data that we build up in memory while parsing the JSON. */
struct values_tmp
{
	struct ceph_daemon *d;
	int holder_num;
	struct values_holder vh[0];
};

static int node_handler_fetch_data(void *arg, json_object *jo, const char *key)
{
	int dset_idx, ds_idx;
	value_t *uv;
	char dset_name[DATA_MAX_NAME_LEN];
	char ds_name[MAX_RRD_DS_NAME_LEN];
	struct values_tmp *vtmp = (struct values_tmp*) arg;
	memset(dset_name, 0, sizeof(dset_name));
	memset(ds_name, 0, sizeof(ds_name));
	if (parse_keys(key, dset_name, ds_name))
		return 1;DEBUG("enter node_handler_fetch_data");
	dset_idx = get_matching_dset(vtmp->d, dset_name);
	if (dset_idx == -1)
		return 1;
	ds_idx = get_matching_value(&vtmp->d->dset[dset_idx], ds_name,
			vtmp->d->dset[dset_idx].ds_num);
	if (ds_idx == -1)
		return 1;DEBUG("DSet:%s, DS:%s, DSet idx:%d, DS idx:%d",
			dset_name,ds_name,dset_idx,ds_idx);
	uv = &(vtmp->vh[dset_idx].values[ds_idx]);
	if (vtmp->d->pc_types[dset_idx][ds_idx] & PERFCOUNTER_LONGRUNAVG)
	{
		json_object *avgcount, *sum;
		uint64_t avgcounti;
		double sumd;
		if (json_object_get_type(jo) != json_type_object)
			return -EINVAL;
		avgcount = json_object_object_get(jo, "avgcount");
		sum = json_object_object_get(jo, "sum");
		if ((!avgcount) || (!sum))
			return -EINVAL;
		avgcounti = json_object_get_int(avgcount);
		DEBUG("avgcounti:%ld",avgcounti);
		if (avgcounti == 0)
			avgcounti = 1;
		sumd = json_object_get_int(sum);
		DEBUG("sumd:%lf",sumd);
		uv->gauge = sumd / avgcounti;
		DEBUG("uv->gauge = sumd / avgcounti = :%lf",uv->gauge);
	}
	else if (vtmp->d->pc_types[dset_idx][ds_idx] & PERFCOUNTER_COUNTER)
	{
		/* We use json_object_get_double here because anything > 32 
		 * bits may get truncated by json_object_get_int */
		uv->counter = json_object_get_double(jo);
		DEBUG("uv->counter %ld",(long)uv->counter);
	}
	else
	{
		uv->gauge = json_object_get_double(jo);
		DEBUG("uv->gauge %lf",uv->gauge);
	}
	return 0;
}

/******* network I/O *******/
enum cstate_t
{
	CSTATE_UNCONNECTED = 0,
	CSTATE_WRITE_REQUEST,
	CSTATE_READ_VERSION,
	CSTATE_READ_AMT,
	CSTATE_READ_JSON,
};

enum request_type_t
{
	ASOK_REQ_VERSION = 0,
	ASOK_REQ_DATA = 1,
	ASOK_REQ_SCHEMA = 2,
	ASOK_REQ_NONE = 1000,
};

struct cconn
{
	/** The Ceph daemon that we're talking to */
	struct ceph_daemon *d;

	/** Request type */
	uint32_t request_type;

	/** The connection state */
	enum cstate_t state;

	/** The socket we use to talk to this daemon */
	int asok;

	/** The amount of data remaining to read / write. */
	uint32_t amt;

	/** Length of the JSON to read */
	uint32_t json_len;

	/** Buffer containing JSON data */
	char *json;
};

static int cconn_connect(struct cconn *io)
{
	struct sockaddr_un address;
	int flags, fd, err;
	if (io->state != CSTATE_UNCONNECTED)
	{
		ERROR("cconn_connect: io->state != CSTATE_UNCONNECTED");
		return -EDOM;
	}
	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
	{
		int err = -errno;
		ERROR("cconn_connect: socket(PF_UNIX, SOCK_STREAM, 0) failed: "
		"error %d", err);
		return err;
	}
	memset(&address, 0, sizeof(struct sockaddr_un));
	address.sun_family = AF_UNIX;
	snprintf(address.sun_path, sizeof(address.sun_path), "%s",
			io->d->asok_path);
	RETRY_ON_EINTR(err,
		connect(fd, (struct sockaddr *) &address, sizeof(struct sockaddr_un)));
	if (err < 0)
	{
		ERROR("cconn_connect: connect(%d) failed: error %d", fd, err);
		return err;
	}

	flags = fcntl(fd, F_GETFL, 0);
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
	{
		err = -errno;
		ERROR("cconn_connect: fcntl(%d, O_NONBLOCK) error %d", fd, err);
		return err;
	}
	io->asok = fd;
	io->state = CSTATE_WRITE_REQUEST;
	io->amt = 0;
	io->json_len = 0;
	io->json = NULL;
	return 0;
}

static void cconn_close(struct cconn *io)
{
	io->state = CSTATE_UNCONNECTED;
	if (io->asok != -1)
	{
		int res;
		RETRY_ON_EINTR(res, close(io->asok));
	}
	io->asok = -1;
	io->amt = 0;
	io->json_len = 0;
	sfree(io->json);
	io->json = NULL;
}

/* Process incoming JSON counter data */
/*static int cconn_process_data(struct cconn *io)
 {
 int ret;
 value_list_t vl = VALUE_LIST_INIT;
 struct values_tmp *vtmp = calloc(1, sizeof(struct values_tmp) +
 (sizeof(value_t) * io->d->dset.ds_num));
 if (!vtmp)
 return -ENOMEM;
 vtmp->d = io->d;
 vtmp->values_len = io->d->dset.ds_num;
 ret = traverse_json(io->json, node_handler_fetch_data, vtmp);
 if (ret)
 goto done;
 sstrncpy(vl.host, hostname_g, sizeof(vl.host));
 sstrncpy(vl.plugin, "ceph", sizeof(vl.plugin));
 sstrncpy(vl.type, io->d->dset.type, sizeof(vl.type));
 vl.values = vtmp->values;
 vl.values_len = vtmp->values_len;
 DEBUG("cconn_process_data(io=%s): vl.values_len=%d, json=\"%s\"",
 io->d->dset.type, vl.values_len, io->json);
 ret = plugin_dispatch_values(&vl);
 done:
 sfree(vtmp);
 return ret;
 }*/
static int cconn_process_data(struct cconn *io)
{
	int i, ret = 0;
	struct values_tmp *vtmp = calloc(1,
			sizeof(struct values_tmp)
					+ (sizeof(struct values_holder)) * io->d->dset_num);
	if (!vtmp)
		return -ENOMEM;
	for (i = 0; i < io->d->dset_num; i++)
	{
		value_t *val = calloc(1, (sizeof(value_t) * io->d->dset[i].ds_num));
		vtmp->vh[i].values = val;
		vtmp->vh[i].values_len = io->d->dset[i].ds_num;
	}
	vtmp->d = io->d;
	vtmp->holder_num = io->d->dset_num;
	ret = traverse_json(io->json, node_handler_fetch_data, vtmp);
	if (ret)
		goto done;
	for (i = 0; i < vtmp->holder_num; i++)
	{
		value_list_t vl = VALUE_LIST_INIT;
		sstrncpy(vl.host, hostname_g, sizeof(vl.host));
		sstrncpy(vl.plugin, "ceph", sizeof(vl.plugin));
		strncpy(vl.plugin_instance, io->d->name, sizeof(vl.plugin_instance));
		sstrncpy(vl.type, io->d->dset[i].type, sizeof(vl.type));
		vl.values = vtmp->vh[i].values;
		vl.values_len = vtmp->vh[i].values_len;
		DEBUG("cconn_process_data(io=%s): vl.values_len=%d, json=\"%s\"",
				io->d->name, vl.values_len, io->json);
		ret = plugin_dispatch_values(&vl);
		if (ret)
			goto done;
	}

	done: for (i = 0; i < vtmp->holder_num; i++)
	{
		sfree(vtmp->vh[i].values);
	}
	sfree(vtmp);
	return ret;
}

static int cconn_process_json(struct cconn *io)
{
	switch (io->request_type)
	{
	case ASOK_REQ_DATA:
		return cconn_process_data(io);
	case ASOK_REQ_SCHEMA:
		return traverse_json(io->json, node_handler_define_schema, io->d);
	default:
		return -EDOM;
	}
}

static int cconn_validate_revents(struct cconn *io, int revents)
{
	if (revents & POLLERR)
	{
		ERROR("cconn_validate_revents(name=%s): got POLLERR", io->d->name);
		return -EIO;
	}
	switch (io->state)
	{
	case CSTATE_WRITE_REQUEST:
		return (revents & POLLOUT) ? 0 : -EINVAL;
	case CSTATE_READ_VERSION:
	case CSTATE_READ_AMT:
	case CSTATE_READ_JSON:
		return (revents & POLLIN) ? 0 : -EINVAL;
		return (revents & POLLIN) ? 0 : -EINVAL;
	default:
		ERROR("cconn_validate_revents(name=%s) got to illegal state on line %d",
				io->d->name, __LINE__);
		return -EDOM;
	}
}

/** Handle a network event for a connection */
static int cconn_handle_event(struct cconn *io)
{
	int ret;
	switch (io->state)
	{
	case CSTATE_UNCONNECTED:
		ERROR("cconn_handle_event(name=%s) got to illegal state on line %d",
				io->d->name, __LINE__);

		return -EDOM;
	case CSTATE_WRITE_REQUEST:
	{
		char cmd[32];
		/*snprintf(cmd, sizeof(cmd), "%s%d%s", "{\"prefix\":\"", io->request_type,
		 "\"}");*/
		char req_type_str[2];
		snprintf(req_type_str, sizeof(req_type_str), "%1.1d", io->request_type);
		json_object *cmd_object = json_object_new_object();
		json_object_object_add(cmd_object, "prefix",
				json_object_new_string(req_type_str));
		const char *cmd_json = json_object_to_json_string(cmd_object);
		/** we should send '\n' to server **/
		snprintf(cmd, sizeof(cmd), "%s\n", cmd_json);
		size_t cmd_len = strlen(cmd);
		RETRY_ON_EINTR(ret,
				write(io->asok, ((char*)&cmd) + io->amt, cmd_len - io->amt));
		DEBUG("cconn_handle_event(name=%s,state=%d,amt=%d,ret=%d)",
				io->d->name, io->state, io->amt, ret);
		if (ret < 0)
			return ret;
		io->amt += ret;
		if (io->amt >= cmd_len)
		{
			io->amt = 0;
			switch (io->request_type)
			{
			case ASOK_REQ_VERSION:
				io->state = CSTATE_READ_VERSION;
				break;
			default:
				io->state = CSTATE_READ_AMT;
				break;
			}
		}
		json_object_put(cmd_object);
		return 0;
	}
	case CSTATE_READ_VERSION:
	{
		RETRY_ON_EINTR(ret,
				read(io->asok, ((char*)(&io->d->version)) + io->amt,
						sizeof(io->d->version) - io->amt));
		DEBUG("cconn_handle_event(name=%s,state=%d,ret=%d)",
				io->d->name, io->state, ret);
		if (ret < 0)
			return ret;
		io->amt += ret;
		if (io->amt >= sizeof(io->d->version))
		{
			io->d->version = ntohl(io->d->version);
			if (io->d->version != 1)
			{
				ERROR("cconn_handle_event(name=%s) not "
				"expecting version %d!", io->d->name, io->d->version);
				return -ENOTSUP;
			}DEBUG("cconn_handle_event(name=%s): identified as "
					"version %d", io->d->name, io->d->version);
			io->amt = 0;
			cconn_close(io);
			io->request_type = ASOK_REQ_SCHEMA;
		}
		return 0;
	}
	case CSTATE_READ_AMT:
	{
		RETRY_ON_EINTR(ret,
				read(io->asok, ((char*)(&io->json_len)) + io->amt,
						sizeof(io->json_len) - io->amt));
		DEBUG("cconn_handle_event(name=%s,state=%d,ret=%d)",
				io->d->name, io->state, ret);
		if (ret < 0)
			return ret;
		io->amt += ret;
		if (io->amt >= sizeof(io->json_len))
		{
			io->json_len = ntohl(io->json_len);
			io->amt = 0;
			io->state = CSTATE_READ_JSON;
			io->json = calloc(1, io->json_len + 1);
			if (!io->json)
				return -ENOMEM;
		}
		return 0;
	}
	case CSTATE_READ_JSON:
	{
		RETRY_ON_EINTR(ret,
				read(io->asok, io->json + io->amt, io->json_len - io->amt));
		DEBUG("cconn_handle_event(name=%s,state=%d,ret=%d)",
				io->d->name, io->state, ret);
		if (ret < 0)
			return ret;
		io->amt += ret;
		if (io->amt >= io->json_len)
		{
			ret = cconn_process_json(io);
			if (ret)
				return ret;
			cconn_close(io);
			io->request_type = ASOK_REQ_NONE;
		}
		return 0;
	}
	default:
		ERROR("cconn_handle_event(name=%s) got to illegal state on "
		"line %d", io->d->name, __LINE__);
		return -EDOM;
	}
}

static int cconn_prepare(struct cconn *io, struct pollfd* fds)
{
	int ret;
	if (io->request_type == ASOK_REQ_NONE)
	{
		/* The request has already been serviced. */
		return 0;
	}
	else if ((io->request_type == ASOK_REQ_DATA) && (io->d->dset_num == 0))
	{
		/* If there are no counters to report on, don't bother
		 * connecting */
		return 0;
	}

	switch (io->state)
	{
	case CSTATE_UNCONNECTED:
		ret = cconn_connect(io);
		if (ret > 0)
			return -ret;
		else if (ret < 0)
			return ret;
		fds->fd = io->asok;
		fds->events = POLLOUT;
		return 1;
	case CSTATE_WRITE_REQUEST:
		fds->fd = io->asok;
		fds->events = POLLOUT;
		return 1;
	case CSTATE_READ_VERSION:
	case CSTATE_READ_AMT:
	case CSTATE_READ_JSON:
		fds->fd = io->asok;
		fds->events = POLLIN;
		return 1;
	default:
		ERROR("cconn_prepare(name=%s) got to illegal state on line %d",
				io->d->name, __LINE__);
		return -EDOM;
	}
}

/** Returns the difference between two struct timevals in milliseconds.
 * On overflow, we return max/min int.
 */
static int milli_diff(const struct timeval *t1, const struct timeval *t2)
{
	int64_t ret;
	int sec_diff = t1->tv_sec - t2->tv_sec;
	int usec_diff = t1->tv_usec - t2->tv_usec;
	ret = usec_diff / 1000;
	ret += (sec_diff * 1000);
	if (ret > INT_MAX)
		return INT_MAX;
	else if (ret < INT_MIN)
		return INT_MIN;
	return (int) ret;
}

/** This handles the actual network I/O to talk to the Ceph daemons.
 */
static int cconn_main_loop(uint32_t request_type)
{
	int i, ret, some_unreachable = 0;
	struct timeval end_tv;
	struct cconn io_array[g_num_daemons];

	DEBUG("entering cconn_main_loop(request_type = %d)", request_type);

	/* create cconn array */
	memset(io_array, 0, sizeof(io_array));
	for (i = 0; i < g_num_daemons; ++i)
	{
		io_array[i].d = g_daemons[i];
		io_array[i].request_type = request_type;
		io_array[i].state = CSTATE_UNCONNECTED;
	}

	/** Calculate the time at which we should give up */
	gettimeofday(&end_tv, NULL);
	end_tv.tv_sec += CEPH_TIMEOUT_INTERVAL;

	while (1)
	{
		int nfds, diff;
		struct timeval tv;
		struct cconn *polled_io_array[g_num_daemons];
		struct pollfd fds[g_num_daemons];
		memset(fds, 0, sizeof(fds));
		nfds = 0;
		for (i = 0; i < g_num_daemons; ++i)
		{
			struct cconn *io = io_array + i;
			ret = cconn_prepare(io, fds + nfds);
			if (ret < 0)
			{
				WARNING("ERROR: cconn_prepare(name=%s,i=%d,st=%d)=%d",
						io->d->name, i, io->state, ret);
				cconn_close(io);
				io->request_type = ASOK_REQ_NONE;
				some_unreachable = 1;
			}
			else if (ret == 1)
			{
				DEBUG("did cconn_prepare(name=%s,i=%d,st=%d)",
						io->d->name, i, io->state);
				polled_io_array[nfds++] = io_array + i;
			}
		}
		if (nfds == 0)
		{
			/* finished */
			ret = 0;
			DEBUG("cconn_main_loop: no more cconn to manage.");
			goto done;
		}
		gettimeofday(&tv, NULL);
		diff = milli_diff(&end_tv, &tv);
		if (diff <= 0)
		{
			/* Timed out */
			ret = -ETIMEDOUT;
			WARNING("ERROR: cconn_main_loop: timed out.\n");
			goto done;
		}
		RETRY_ON_EINTR(ret, poll(fds, nfds, diff));
		if (ret < 0)
		{
			ERROR("poll(2) error: %d", ret);
			goto done;
		}
		for (i = 0; i < nfds; ++i)
		{
			struct cconn *io = polled_io_array[i];
			int revents = fds[i].revents;
			if (revents == 0)
			{
				/* do nothing */
			}
			else if (cconn_validate_revents(io, revents))
			{
				WARNING("ERROR: cconn(name=%s,i=%d,st=%d): "
				"revents validation error: "
				"revents=0x%08x", io->d->name, i, io->state, revents);
				cconn_close(io);
				io->request_type = ASOK_REQ_NONE;
				some_unreachable = 1;
			}
			else
			{
				int ret = cconn_handle_event(io);
				if (ret)
				{
					WARNING("ERROR: cconn_handle_event(name=%s,"
					"i=%d,st=%d): error %d", io->d->name, i, io->state, ret);
					cconn_close(io);
					io->request_type = ASOK_REQ_NONE;
					some_unreachable = 1;
				}
			}
		}
	}
	done: for (i = 0; i < g_num_daemons; ++i)
	{
		cconn_close(io_array + i);
	}
	if (some_unreachable)
	{
		DEBUG("cconn_main_loop: some Ceph daemons were unreachable.");
	}
	else
	{
		DEBUG("cconn_main_loop: reached all Ceph daemons :)");
	}
	return ret;
}

static int ceph_read(void)
{
	return cconn_main_loop(ASOK_REQ_DATA);
}

/******* lifecycle *******/
static int ceph_init(void)
{
	int i, ret, j;
	DEBUG("ceph_init");
	ceph_daemons_print();

	ret = cconn_main_loop(ASOK_REQ_VERSION);
	if (ret)
		return ret;
	for (i = 0; i < g_num_daemons; ++i)
	{
		struct ceph_daemon *d = g_daemons[i];
		for (j = 0; j < d->dset_num; j++)
		{
			ret = plugin_register_data_set(d->dset + j);
			if (ret)
			{
				ERROR("plugin_register_data_set(%s) failed!", d->name);
			}
			else
			{
				DEBUG("plugin_register_data_set(%s): "
						"(d->dset)[%d]->ds_num=%d",
						d->name, j, d->dset[j].ds_num);
			}
		}
	}
	return 0;
}

static int ceph_shutdown(void)
{
	int i;
	for (i = 0; i < g_num_daemons; ++i)
	{
		ceph_daemon_free(g_daemons[i]);
	}
	sfree(g_daemons);
	g_daemons = NULL;
	g_num_daemons = 0;
	DEBUG("finished ceph_shutdown");
	return 0;
}

void module_register(void)
{
	plugin_register_complex_config("ceph", ceph_config);
	plugin_register_init("ceph", ceph_init);
	plugin_register_read("ceph", ceph_read);
	plugin_register_shutdown("ceph", ceph_shutdown);
}
