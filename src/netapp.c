/**
 * collectd - src/netapp.c
 * Copyright (C) 2009  Sven Trenkel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Sven Trenkel <sven.trenkel at noris.net>
 **/

#include "collectd.h"
#include "common.h"

#include <netapp_api.h>

#define HAS_ALL_FLAGS(has,needs) (((has) & (needs)) == (needs))

typedef struct host_config_s host_config_t;
typedef void service_handler_t(host_config_t *host, na_elem_t *result, void *data);

/*!
 * \brief Persistent data for system performance counters
 */
#define CFG_SYSTEM_CPU  0x01
#define CFG_SYSTEM_NET  0x02
#define CFG_SYSTEM_OPS  0x04
#define CFG_SYSTEM_DISK 0x08
#define CFG_SYSTEM_ALL  0x0F
typedef struct {
	uint32_t flags;
} cfg_system_t;

/*!
 * \brief Persistent data for WAFL performance counters. (a.k.a. cache performance)
 *
 * The cache counters use old counter values to calculate a hit ratio for each
 * counter. The "data_wafl_t" struct therefore contains old counter values
 * along with flags, which are set if the counter is valid.
 *
 * The function "query_wafl_data" will fill a new structure of this kind with
 * new values, then pass both, new and old data, to "submit_wafl_data". That
 * function calculates the hit ratios, submits the calculated values and
 * updates the old counter values for the next iteration.
 */
#define CFG_WAFL_NAME_CACHE        0x0001
#define CFG_WAFL_DIR_CACHE         0x0002
#define CFG_WAFL_BUF_CACHE         0x0004
#define CFG_WAFL_INODE_CACHE       0x0008
#define CFG_WAFL_ALL               0x000F
#define HAVE_WAFL_NAME_CACHE_HIT   0x0100
#define HAVE_WAFL_NAME_CACHE_MISS  0x0200
#define HAVE_WAFL_NAME_CACHE       (HAVE_WAFL_NAME_CACHE_HIT | HAVE_WAFL_NAME_CACHE_MISS)
#define HAVE_WAFL_FIND_DIR_HIT     0x0400
#define HAVE_WAFL_FIND_DIR_MISS    0x0800
#define HAVE_WAFL_FIND_DIR         (HAVE_WAFL_FIND_DIR_HIT | HAVE_WAFL_FIND_DIR_MISS)
#define HAVE_WAFL_BUF_HASH_HIT     0x1000
#define HAVE_WAFL_BUF_HASH_MISS    0x2000
#define HAVE_WAFL_BUF_HASH         (HAVE_WAFL_BUF_HASH_HIT | HAVE_WAFL_BUF_HASH_MISS)
#define HAVE_WAFL_INODE_CACHE_HIT  0x4000
#define HAVE_WAFL_INODE_CACHE_MISS 0x8000
#define HAVE_WAFL_INODE_CACHE      (HAVE_WAFL_INODE_CACHE_HIT | HAVE_WAFL_INODE_CACHE_MISS)
#define HAVE_WAFL_ALL              0xff00
typedef struct {
	uint32_t flags;
	time_t timestamp;
	uint64_t name_cache_hit;
	uint64_t name_cache_miss;
	uint64_t find_dir_hit;
	uint64_t find_dir_miss;
	uint64_t buf_hash_hit;
	uint64_t buf_hash_miss;
	uint64_t inode_cache_hit;
	uint64_t inode_cache_miss;
} data_wafl_t;

/*!
 * \brief Persistent data for volume performance data.
 *
 * The code below uses the difference of the operations and latency counters to
 * calculate an average per-operation latency. For this, old counters need to
 * be stored in the "data_volume_perf_t" structure. The byte-counters are just
 * kept for completeness sake. The "flags" member indicates if each counter is
 * valid or not.
 *
 * The "query_volume_perf_data" function will fill a new struct of this type
 * and pass both, old and new data, to "submit_volume_perf_data". In that
 * function, the per-operation latency is calculated and dispatched, then the
 * old counters are updated.
 */
#define CFG_VOLUME_PERF_INIT           0x0001
#define CFG_VOLUME_PERF_IO             0x0002
#define CFG_VOLUME_PERF_OPS            0x0003
#define CFG_VOLUME_PERF_LATENCY        0x0008
#define CFG_VOLUME_PERF_ALL            0x000F
#define HAVE_VOLUME_PERF_BYTES_READ    0x0010
#define HAVE_VOLUME_PERF_BYTES_WRITE   0x0020
#define HAVE_VOLUME_PERF_OPS_READ      0x0040
#define HAVE_VOLUME_PERF_OPS_WRITE     0x0080
#define HAVE_VOLUME_PERF_LATENCY_READ  0x0100
#define HAVE_VOLUME_PERF_LATENCY_WRITE 0x0200
#define HAVE_VOLUME_PERF_ALL           0x03F0
typedef struct {
	uint32_t flags;
} cfg_volume_perf_t;

typedef struct {
	uint32_t flags;
	time_t timestamp;
	uint64_t read_bytes;
	uint64_t write_bytes;
	uint64_t read_ops;
	uint64_t write_ops;
	uint64_t read_latency;
	uint64_t write_latency;
} data_volume_perf_t;

/*!
 * \brief Configuration struct for volume usage data (free / used).
 */
#define VOLUME_INIT           0x01
#define VOLUME_DF             0x02
#define VOLUME_SNAP           0x04
typedef struct {
	uint32_t flags;
} cfg_volume_usage_t;

typedef struct service_config_s {
	na_elem_t *query;
	service_handler_t *handler;
	int multiplier;
	int skip_countdown;
	int interval;
	void *data;
	struct service_config_s *next;
} cfg_service_t;
#define SERVICE_INIT {0, 0, 1, 1, 0, 0, 0}

/*!
 * \brief Struct representing a volume.
 *
 * A volume currently has a name and two sets of values:
 *
 *  - Performance data, such as bytes read/written, number of operations
 *    performed and average time per operation.
 *
 *  - Usage data, i. e. amount of used and free space in the volume.
 */
typedef struct volume_s {
	char *name;
	data_volume_perf_t perf_data;
	cfg_volume_usage_t cfg_volume_usage;
	struct volume_s *next;
} volume_t;

#define CFG_DISK_BUSIEST 0x01
#define CFG_DISK_ALL     0x01
#define HAVE_DISK_BUSY   0x10
#define HAVE_DISK_BASE   0x20
#define HAVE_DISK_ALL    0x30
typedef struct {
	uint32_t flags;
} cfg_disk_t;

/*!
 * \brief A disk in the NetApp.
 *
 * A disk doesn't have any more information than its name at the moment.
 * The name includes the "disk_" prefix.
 */
typedef struct disk_s {
	char *name;
	uint32_t flags;
	time_t timestamp;
	uint64_t disk_busy;
	uint64_t base_for_disk_busy;
	double disk_busy_percent;
	struct disk_s *next;
} disk_t;

struct host_config_s {
	na_server_t *srv;
	char *name;
	na_server_transport_t protocol;
	char *host;
	int port;
	char *username;
	char *password;
	int interval;
	cfg_service_t *services;
	disk_t *disks;
	volume_t *volumes;
	struct host_config_s *next;
};
#define HOST_INIT {NULL, NULL, NA_SERVER_TRANSPORT_HTTPS, NULL, 0, NULL, NULL, 10, NULL, NULL, NULL, NULL}

static host_config_t *host_config;

/*
 * Auxiliary functions
 *
 * Used to look up volumes and disks or to handle flags.
 */
static volume_t *get_volume (host_config_t *host, const char *name, /* {{{ */
		uint32_t vol_usage_flags, uint32_t vol_perf_flags)
{
	volume_t *v;

	if (name == NULL)
		return (NULL);
	
	/* Make sure the default flags include the init-bit. */
	if (vol_usage_flags != 0)
		vol_usage_flags |= VOLUME_INIT;
	if (vol_perf_flags != 0)
		vol_perf_flags |= CFG_VOLUME_PERF_INIT;

	for (v = host->volumes; v; v = v->next) {
		if (strcmp(v->name, name) != 0)
			continue;

		/* Check if the flags have been initialized. */
		if (((v->cfg_volume_usage.flags & VOLUME_INIT) == 0)
				&& (vol_usage_flags != 0))
			v->cfg_volume_usage.flags = vol_usage_flags;
		if (((v->perf_data.flags & CFG_VOLUME_PERF_INIT) == 0)
				&& (vol_perf_flags != 0))
			v->perf_data.flags = vol_perf_flags;

		return v;
	}

	DEBUG ("netapp plugin: Allocating new entry for volume %s.", name);
	v = malloc(sizeof(*v));
	if (v == NULL)
		return (NULL);
	memset (v, 0, sizeof (*v));

	v->cfg_volume_usage.flags = vol_usage_flags;
	v->perf_data.flags = vol_perf_flags;

	v->name = strdup(name);
	if (v->name == NULL) {
		sfree (v);
		return (NULL);
	}

	v->next = host->volumes;
	host->volumes = v;

	return v;
} /* }}} volume_t *get_volume */

static disk_t *get_disk(host_config_t *host, const char *name) /* {{{ */
{
	disk_t *v;

	if (name == NULL)
		return (NULL);
	
	for (v = host->disks; v; v = v->next) {
		if (strcmp(v->name, name) == 0)
			return v;
	}
	v = malloc(sizeof(*v));
	if (v == NULL)
		return (NULL);
	memset (v, 0, sizeof (*v));
	v->next = NULL;

	v->name = strdup(name);
	if (v->name == NULL) {
		sfree (v);
		return (NULL);
	}

	v->next = host->disks;
	host->disks = v;

	return v;
} /* }}} disk_t *get_disk */

static void set_global_perf_vol_flag(const host_config_t *host, /* {{{ */
		uint32_t flag, _Bool set)
{
	volume_t *v;
	
	for (v = host->volumes; v; v = v->next) {
		if (set)
			v->perf_data.flags |= flag;
		else /* if (!set) */
			v->perf_data.flags &= ~flag;
	}
} /* }}} void set_global_perf_vol_flag */

static void set_global_vol_flag(const host_config_t *host, /* {{{ */
		uint32_t flag, _Bool set) {
	volume_t *v;
	
	for (v = host->volumes; v; v = v->next) {
		if (set)
			v->cfg_volume_usage.flags |= flag;
		else /* if (!set) */
			v->cfg_volume_usage.flags &= ~flag;
	}
} /* }}} void set_global_vol_flag */

/*
 * Various submit functions.
 *
 * They all eventually call "submit_values" which creates a value_list_t and
 * dispatches it to the daemon.
 */
static int submit_values (const char *host, /* {{{ */
		const char *plugin_inst,
		const char *type, const char *type_inst,
		value_t *values, int values_len,
		time_t timestamp)
{
	value_list_t vl = VALUE_LIST_INIT;

	vl.values = values;
	vl.values_len = values_len;

	if (timestamp > 0)
		vl.time = timestamp;

	if (host != NULL)
		sstrncpy (vl.host, host, sizeof (vl.host));
	else
		sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "netapp", sizeof (vl.plugin));
	if (plugin_inst != NULL)
		sstrncpy (vl.plugin_instance, plugin_inst, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));
	if (type_inst != NULL)
		sstrncpy (vl.type_instance, type_inst, sizeof (vl.type_instance));

	return (plugin_dispatch_values (&vl));
} /* }}} int submit_uint64 */

static int submit_two_counters (const char *host, const char *plugin_inst, /* {{{ */
		const char *type, const char *type_inst, counter_t val0, counter_t val1,
		time_t timestamp)
{
	value_t values[2];

	values[0].counter = val0;
	values[1].counter = val1;

	return (submit_values (host, plugin_inst, type, type_inst,
				values, 2, timestamp));
} /* }}} int submit_two_counters */

static int submit_counter (const char *host, const char *plugin_inst, /* {{{ */
		const char *type, const char *type_inst, counter_t counter, time_t timestamp)
{
	value_t v;

	v.counter = counter;

	return (submit_values (host, plugin_inst, type, type_inst,
				&v, 1, timestamp));
} /* }}} int submit_counter */

static int submit_two_gauge (const char *host, const char *plugin_inst, /* {{{ */
		const char *type, const char *type_inst, gauge_t val0, gauge_t val1,
		time_t timestamp)
{
	value_t values[2];

	values[0].gauge = val0;
	values[1].gauge = val1;

	return (submit_values (host, plugin_inst, type, type_inst,
				values, 2, timestamp));
} /* }}} int submit_two_gauge */

static int submit_double (const char *host, const char *plugin_inst, /* {{{ */
		const char *type, const char *type_inst, double d, time_t timestamp)
{
	value_t v;

	v.gauge = (gauge_t) d;

	return (submit_values (host, plugin_inst, type, type_inst,
				&v, 1, timestamp));
} /* }}} int submit_uint64 */

/* Calculate hit ratio from old and new counters and submit the resulting
 * percentage. Used by "submit_wafl_data". */
static int submit_cache_ratio (const char *host, /* {{{ */
		const char *plugin_inst,
		const char *type_inst,
		uint64_t new_hits,
		uint64_t new_misses,
		uint64_t old_hits,
		uint64_t old_misses,
		time_t timestamp)
{
	value_t v;

	if ((new_hits >= old_hits) && (new_misses >= old_misses)) {
		uint64_t hits;
		uint64_t misses;

		hits = new_hits - old_hits;
		misses = new_misses - old_misses;

		v.gauge = 100.0 * ((gauge_t) hits) / ((gauge_t) (hits + misses));
	} else {
		v.gauge = NAN;
	}

	return (submit_values (host, plugin_inst, "cache_ratio", type_inst,
				&v, 1, timestamp));
} /* }}} int submit_cache_ratio */

/* Submits all the caches used by WAFL. Uses "submit_cache_ratio". */
static int submit_wafl_data (const host_config_t *host, const char *instance, /* {{{ */
		data_wafl_t *old_data, const data_wafl_t *new_data)
{
	/* Submit requested counters */
	if (HAS_ALL_FLAGS (old_data->flags, CFG_WAFL_NAME_CACHE | HAVE_WAFL_NAME_CACHE)
			&& HAS_ALL_FLAGS (new_data->flags, HAVE_WAFL_NAME_CACHE))
		submit_cache_ratio (host->name, instance, "name_cache_hit",
				new_data->name_cache_hit, new_data->name_cache_miss,
				old_data->name_cache_hit, old_data->name_cache_miss,
				new_data->timestamp);

	if (HAS_ALL_FLAGS (old_data->flags, CFG_WAFL_DIR_CACHE | HAVE_WAFL_FIND_DIR)
			&& HAS_ALL_FLAGS (new_data->flags, HAVE_WAFL_FIND_DIR))
		submit_cache_ratio (host->name, instance, "find_dir_hit",
				new_data->find_dir_hit, new_data->find_dir_miss,
				old_data->find_dir_hit, old_data->find_dir_miss,
				new_data->timestamp);

	if (HAS_ALL_FLAGS (old_data->flags, CFG_WAFL_BUF_CACHE | HAVE_WAFL_BUF_HASH)
			&& HAS_ALL_FLAGS (new_data->flags, HAVE_WAFL_BUF_HASH))
		submit_cache_ratio (host->name, instance, "buf_hash_hit",
				new_data->buf_hash_hit, new_data->buf_hash_miss,
				old_data->buf_hash_hit, old_data->buf_hash_miss,
				new_data->timestamp);

	if (HAS_ALL_FLAGS (old_data->flags, CFG_WAFL_INODE_CACHE | HAVE_WAFL_INODE_CACHE)
			&& HAS_ALL_FLAGS (new_data->flags, HAVE_WAFL_INODE_CACHE))
		submit_cache_ratio (host->name, instance, "inode_cache_hit",
				new_data->inode_cache_hit, new_data->inode_cache_miss,
				old_data->inode_cache_hit, old_data->inode_cache_miss,
				new_data->timestamp);

	/* Clear old HAVE_* flags */
	old_data->flags &= ~HAVE_WAFL_ALL;

	/* Copy all counters */
	old_data->timestamp        = new_data->timestamp;
	old_data->name_cache_hit   = new_data->name_cache_hit;
	old_data->name_cache_miss  = new_data->name_cache_miss;
	old_data->find_dir_hit     = new_data->find_dir_hit;
	old_data->find_dir_miss    = new_data->find_dir_miss;
	old_data->buf_hash_hit     = new_data->buf_hash_hit;
	old_data->buf_hash_miss    = new_data->buf_hash_miss;
	old_data->inode_cache_hit  = new_data->inode_cache_hit;
	old_data->inode_cache_miss = new_data->inode_cache_miss;

	/* Copy HAVE_* flags */
	old_data->flags |= (new_data->flags & HAVE_WAFL_ALL);

	return (0);
} /* }}} int submit_wafl_data */

/* Submits volume performance data to the daemon, taking care to honor and
 * update flags appropriately. */
static int submit_volume_perf_data (const host_config_t *host, /* {{{ */
		volume_t *volume,
		const data_volume_perf_t *new_data)
{
	/* Check for and submit disk-octet values */
	if (HAS_ALL_FLAGS (volume->perf_data.flags, CFG_VOLUME_PERF_IO)
			&& HAS_ALL_FLAGS (new_data->flags, HAVE_VOLUME_PERF_BYTES_READ | HAVE_VOLUME_PERF_BYTES_WRITE))
	{
		submit_two_counters (host->name, volume->name, "disk_octets", /* type instance = */ NULL,
				(counter_t) new_data->read_bytes, (counter_t) new_data->write_bytes, new_data->timestamp);
	}

	/* Check for and submit disk-operations values */
	if (HAS_ALL_FLAGS (volume->perf_data.flags, CFG_VOLUME_PERF_OPS)
			&& HAS_ALL_FLAGS (new_data->flags, HAVE_VOLUME_PERF_OPS_READ | HAVE_VOLUME_PERF_OPS_WRITE))
	{
		submit_two_counters (host->name, volume->name, "disk_ops", /* type instance = */ NULL,
				(counter_t) new_data->read_ops, (counter_t) new_data->write_ops, new_data->timestamp);
	}

	/* Check for, calculate and submit disk-latency values */
	if (HAS_ALL_FLAGS (volume->perf_data.flags, CFG_VOLUME_PERF_LATENCY
				| HAVE_VOLUME_PERF_OPS_READ | HAVE_VOLUME_PERF_OPS_WRITE
				| HAVE_VOLUME_PERF_LATENCY_READ | HAVE_VOLUME_PERF_LATENCY_WRITE)
			&& HAS_ALL_FLAGS (new_data->flags, HAVE_VOLUME_PERF_OPS_READ | HAVE_VOLUME_PERF_OPS_WRITE
				| HAVE_VOLUME_PERF_LATENCY_READ | HAVE_VOLUME_PERF_LATENCY_WRITE))
	{
		gauge_t latency_per_op_read;
		gauge_t latency_per_op_write;

		latency_per_op_read = NAN;
		latency_per_op_write = NAN;

		/* Check if a counter wrapped around. */
		if ((new_data->read_ops > volume->perf_data.read_ops)
				&& (new_data->read_latency > volume->perf_data.read_latency))
		{
			uint64_t diff_ops_read;
			uint64_t diff_latency_read;

			diff_ops_read = new_data->read_ops - volume->perf_data.read_ops;
			diff_latency_read = new_data->read_latency - volume->perf_data.read_latency;

			if (diff_ops_read > 0)
				latency_per_op_read = ((gauge_t) diff_latency_read) / ((gauge_t) diff_ops_read);
		}

		if ((new_data->write_ops > volume->perf_data.write_ops)
				&& (new_data->write_latency > volume->perf_data.write_latency))
		{
			uint64_t diff_ops_write;
			uint64_t diff_latency_write;

			diff_ops_write = new_data->write_ops - volume->perf_data.write_ops;
			diff_latency_write = new_data->write_latency - volume->perf_data.write_latency;

			if (diff_ops_write > 0)
				latency_per_op_write = ((gauge_t) diff_latency_write) / ((gauge_t) diff_ops_write);
		}

		submit_two_gauge (host->name, volume->name, "disk_latency", /* type instance = */ NULL,
				latency_per_op_read, latency_per_op_write, new_data->timestamp);
	}

	/* Clear all HAVE_* flags. */
	volume->perf_data.flags &= ~HAVE_VOLUME_PERF_ALL;

	/* Copy all counters */
	volume->perf_data.timestamp = new_data->timestamp;
	volume->perf_data.read_bytes = new_data->read_bytes;
	volume->perf_data.write_bytes = new_data->write_bytes;
	volume->perf_data.read_ops = new_data->read_ops;
	volume->perf_data.write_ops = new_data->write_ops;
	volume->perf_data.read_latency = new_data->read_latency;
	volume->perf_data.write_latency = new_data->write_latency;

	/* Copy the HAVE_* flags */
	volume->perf_data.flags |= (new_data->flags & HAVE_VOLUME_PERF_ALL);

	return (0);
} /* }}} int submit_volume_perf_data */

/* 
 * Query functions
 *
 * These functions are called with appropriate data returned by the libnetapp
 * interface which is parsed and submitted with the above functions.
 */
/* Data corresponding to <GetWaflPerfData /> */
static void query_wafl_data(host_config_t *host, na_elem_t *out, void *data) { /* {{{ */
	data_wafl_t *wafl = data;
	data_wafl_t perf_data;
	const char *plugin_inst;
	na_elem_t *counter;

	memset (&perf_data, 0, sizeof (perf_data));
	
	perf_data.timestamp = (time_t) na_child_get_uint64(out, "timestamp", 0);

	out = na_elem_child(na_elem_child(out, "instances"), "instance-data");
	if (out == NULL)
		return;

	plugin_inst = na_child_get_string(out, "name");
	if (plugin_inst == NULL)
		return;

	/* Iterate over all counters */
	na_elem_iter_t iter = na_child_iterator(na_elem_child(out, "counters"));
	for (counter = na_iterator_next(&iter); counter; counter = na_iterator_next(&iter)) {
		const char *name;
		uint64_t value;

		name = na_child_get_string(counter, "name");
		if (name == NULL)
			continue;

		value = na_child_get_uint64(counter, "value", UINT64_MAX);
		if (value == UINT64_MAX)
			continue;

		if (!strcmp(name, "name_cache_hit")) {
			perf_data.name_cache_hit = value;
			perf_data.flags |= HAVE_WAFL_NAME_CACHE_HIT;
		} else if (!strcmp(name, "name_cache_miss")) {
			perf_data.name_cache_miss = value;
			perf_data.flags |= HAVE_WAFL_NAME_CACHE_MISS;
		} else if (!strcmp(name, "find_dir_hit")) {
			perf_data.find_dir_hit = value;
			perf_data.flags |= HAVE_WAFL_FIND_DIR_HIT;
		} else if (!strcmp(name, "find_dir_miss")) {
			perf_data.find_dir_miss = value;
			perf_data.flags |= HAVE_WAFL_FIND_DIR_MISS;
		} else if (!strcmp(name, "buf_hash_hit")) {
			perf_data.buf_hash_hit = value;
			perf_data.flags |= HAVE_WAFL_BUF_HASH_HIT;
		} else if (!strcmp(name, "buf_hash_miss")) {
			perf_data.buf_hash_miss = value;
			perf_data.flags |= HAVE_WAFL_BUF_HASH_MISS;
		} else if (!strcmp(name, "inode_cache_hit")) {
			perf_data.inode_cache_hit = value;
			perf_data.flags |= HAVE_WAFL_INODE_CACHE_HIT;
		} else if (!strcmp(name, "inode_cache_miss")) {
			perf_data.inode_cache_miss = value;
			perf_data.flags |= HAVE_WAFL_INODE_CACHE_MISS;
		} else {
			DEBUG("netapp plugin: query_wafl_data: Found unexpected child: %s",
					name);
		}
	}

	submit_wafl_data (host, plugin_inst, wafl, &perf_data);
} /* }}} void query_wafl_data */

/* Data corresponding to <GetDiskPerfData /> */
static void query_submit_disk_data(host_config_t *host, na_elem_t *out, void *data) { /* {{{ */
	cfg_disk_t *cfg_disk = data;
	time_t timestamp;
	na_elem_t *counter, *inst;
	disk_t *worst_disk = 0;
	
	timestamp = (time_t) na_child_get_uint64(out, "timestamp", 0);
	out = na_elem_child(out, "instances");

	/* Iterate over all children */
	na_elem_iter_t inst_iter = na_child_iterator(out);
	for (inst = na_iterator_next(&inst_iter); inst; inst = na_iterator_next(&inst_iter)) {
		disk_t *old_data;
		disk_t  new_data;

		memset (&new_data, 0, sizeof (new_data));
		new_data.timestamp = timestamp;
		new_data.disk_busy_percent = NAN;

		old_data = get_disk(host, na_child_get_string(inst, "name"));
		if (old_data == NULL)
			continue;

		/* Look for the "disk_busy" and "base_for_disk_busy" counters */
		na_elem_iter_t count_iter = na_child_iterator(na_elem_child(inst, "counters"));
		for (counter = na_iterator_next(&count_iter); counter; counter = na_iterator_next(&count_iter)) {
			const char *name;
			uint64_t value;

			name = na_child_get_string(counter, "name");
			if (name == NULL)
				continue;

			value = na_child_get_uint64(counter, "value", UINT64_MAX);
			if (value == UINT64_MAX)
				continue;

			if (strcmp(name, "disk_busy") == 0)
			{
				new_data.disk_busy = value;
				new_data.flags |= HAVE_DISK_BUSY;
			}
			else if (strcmp(name, "base_for_disk_busy") == 0)
			{
				new_data.base_for_disk_busy = value;
				new_data.flags |= HAVE_DISK_BASE;
			}
		}

		/* If all required counters are available and did not just wrap around,
		 * calculate the busy percentage. Otherwise, the value is initialized to
		 * NAN at the top of the for-loop. */
		if (HAS_ALL_FLAGS (old_data->flags, HAVE_DISK_BUSY | HAVE_DISK_BASE)
				&& HAS_ALL_FLAGS (new_data.flags, HAVE_DISK_BUSY | HAVE_DISK_BASE)
				&& (new_data.disk_busy >= old_data->disk_busy)
				&& (new_data.base_for_disk_busy > old_data->base_for_disk_busy))
		{
			uint64_t busy_diff;
			uint64_t base_diff;

			busy_diff = new_data.disk_busy - old_data->disk_busy;
			base_diff = new_data.base_for_disk_busy - old_data->base_for_disk_busy;

			new_data.disk_busy_percent = 100.0
				* ((gauge_t) busy_diff) / ((gauge_t) base_diff);
		}

		/* Clear HAVE_* flags */
		old_data->flags &= ~HAVE_DISK_ALL;

		/* Copy data */
		old_data->timestamp = new_data.timestamp;
		old_data->disk_busy = new_data.disk_busy;
		old_data->base_for_disk_busy = new_data.base_for_disk_busy;
		old_data->disk_busy_percent = new_data.disk_busy_percent;

		/* Copy flags */
		old_data->flags |= (new_data.flags & HAVE_DISK_ALL);

		if ((worst_disk == NULL)
				|| (worst_disk->disk_busy_percent < old_data->disk_busy_percent))
			worst_disk = old_data;
	} /* for (all disks) */

	if ((cfg_disk->flags & CFG_DISK_BUSIEST) && (worst_disk != NULL))
		submit_double (host->name, "system", "percent", "disk_busy",
				worst_disk->disk_busy_percent, timestamp);
} /* }}} void query_submit_disk_data */

/* Data corresponding to <GetVolumeData /> */
static void collect_volume_data(host_config_t *host, na_elem_t *out, void *data) { /* {{{ */
	na_elem_t *inst;
	volume_t *volume;
	cfg_volume_usage_t *cfg_volume_data = data;

	out = na_elem_child(out, "volumes");
	na_elem_iter_t inst_iter = na_child_iterator(out);
	for (inst = na_iterator_next(&inst_iter); inst; inst = na_iterator_next(&inst_iter)) {
		uint64_t size_free = 0, size_used = 0, snap_reserved = 0;

		na_elem_t *sis;
		const char *sis_state;
		uint64_t sis_saved_reported;
		uint64_t sis_saved;

		volume = get_volume(host, na_child_get_string(inst, "name"),
				cfg_volume_data->flags, /* perf_flags = */ 0);
		if (volume == NULL)
			continue;

		if (!(volume->cfg_volume_usage.flags & VOLUME_DF))
			continue;

		/* 2^4 exa-bytes? This will take a while ;) */
		size_free = na_child_get_uint64(inst, "size-available", UINT64_MAX);
		if (size_free != UINT64_MAX)
			submit_double (host->name, volume->name, "df_complex", "used",
					(double) size_used, /* time = */ 0);

		size_used = na_child_get_uint64(inst, "size-used", UINT64_MAX);
		if (size_free != UINT64_MAX)
			submit_double (host->name, volume->name, "df_complex", "free",
					(double) size_free, /* time = */ 0);

		snap_reserved = na_child_get_uint64(inst, "snapshot-blocks-reserved", UINT64_MAX);
		if (snap_reserved != UINT64_MAX)
			/* 1 block == 1024 bytes  as per API docs */
			submit_double (host->name, volume->name, "df_complex", "snap_reserved",
					(double) (1024 * snap_reserved), /* time = */ 0);

		sis = na_elem_child(inst, "sis");
		if (sis == NULL)
			continue;

		sis_state = na_child_get_string(sis, "state");
		if ((sis_state == NULL)
				|| (strcmp ("enabled", sis_state) != 0))
			continue;

		sis_saved_reported = na_child_get_uint64(sis, "size-saved", UINT64_MAX);
		if (sis_saved_reported == UINT64_MAX)
			continue;

		/* size-saved is actually a 32 bit number, so ... time for some guesswork. */
		if ((sis_saved_reported >> 32) != 0) {
			/* In case they ever fix this bug. */
			sis_saved = sis_saved_reported;
		} else {
			uint64_t sis_saved_percent;
			uint64_t sis_saved_guess;
			uint64_t overflow_guess;
			uint64_t guess1, guess2, guess3;

			sis_saved_percent = na_child_get_uint64(sis, "percentage-saved", UINT64_MAX);
			if (sis_saved_percent > 100)
				continue;

			/* The "size-saved" value is a 32bit unsigned integer. This is a bug and
			 * will hopefully be fixed in later versions. To work around the bug, try
			 * to figure out how often the 32bit integer wrapped around by using the
			 * "percentage-saved" value. Because the percentage is in the range
			 * [0-100], this should work as long as the saved space does not exceed
			 * 400 GBytes. */
			/* percentage-saved = size-saved / (size-saved + size-used) */
			if (sis_saved_percent < 100)
				sis_saved_guess = size_used * sis_saved_percent / (100 - sis_saved_percent);
			else
				sis_saved_guess = size_used;

			overflow_guess = sis_saved_guess >> 32;
			guess1 = overflow_guess ? ((overflow_guess - 1) << 32) + sis_saved_reported : sis_saved_reported;
			guess2 = (overflow_guess << 32) + sis_saved_reported;
			guess3 = ((overflow_guess + 1) << 32) + sis_saved_reported;

			if (sis_saved_guess < guess2) {
				if ((sis_saved_guess - guess1) < (guess2 - sis_saved_guess))
					sis_saved = guess1;
				else
					sis_saved = guess2;
			} else {
				if ((sis_saved_guess - guess2) < (guess3 - sis_saved_guess))
					sis_saved = guess2;
				else
					sis_saved = guess3;
			}
		} /* end of 32-bit workaround */

		submit_double (host->name, volume->name, "df_complex", "sis_saved",
				(double) sis_saved, /* time = */ 0);
	}
} /* }}} void collect_volume_data */

/* Data corresponding to <GetVolumePerfData /> */
static void query_volume_perf_data(host_config_t *host, na_elem_t *out, void *data) { /* {{{ */
	cfg_volume_perf_t *cfg_volume_perf = data;
	time_t timestamp;
	na_elem_t *counter, *inst;
	
	timestamp = (time_t) na_child_get_uint64(out, "timestamp", 0);

	out = na_elem_child(out, "instances");
	na_elem_iter_t inst_iter = na_child_iterator(out);
	for (inst = na_iterator_next(&inst_iter); inst; inst = na_iterator_next(&inst_iter)) {
		data_volume_perf_t perf_data;
		volume_t *volume;

		memset (&perf_data, 0, sizeof (perf_data));
		perf_data.timestamp = timestamp;

		volume = get_volume(host, na_child_get_string(inst, "name"),
				/* data_flags = */ 0, cfg_volume_perf->flags);
		if (volume == NULL)
			continue;

		na_elem_iter_t count_iter = na_child_iterator(na_elem_child(inst, "counters"));
		for (counter = na_iterator_next(&count_iter); counter; counter = na_iterator_next(&count_iter)) {
			const char *name;
			uint64_t value;

			name = na_child_get_string(counter, "name");
			if (name == NULL)
				continue;

			value = na_child_get_uint64(counter, "value", UINT64_MAX);
			if (value == UINT64_MAX)
				continue;

			if (!strcmp(name, "read_data")) {
				perf_data.read_bytes = value;
				perf_data.flags |= HAVE_VOLUME_PERF_BYTES_READ;
			} else if (!strcmp(name, "write_data")) {
				perf_data.write_bytes = value;
				perf_data.flags |= HAVE_VOLUME_PERF_BYTES_WRITE;
			} else if (!strcmp(name, "read_ops")) {
				perf_data.read_ops = value;
				perf_data.flags |= HAVE_VOLUME_PERF_OPS_READ;
			} else if (!strcmp(name, "write_ops")) {
				perf_data.write_ops = value;
				perf_data.flags |= HAVE_VOLUME_PERF_OPS_WRITE;
			} else if (!strcmp(name, "read_latency")) {
				perf_data.read_latency = value;
				perf_data.flags |= HAVE_VOLUME_PERF_LATENCY_READ;
			} else if (!strcmp(name, "write_latency")) {
				perf_data.write_latency = value;
				perf_data.flags |= HAVE_VOLUME_PERF_LATENCY_WRITE;
			}
		}

		submit_volume_perf_data (host, volume, &perf_data);
	} /* for (volume) */
} /* }}} void query_volume_perf_data */

/* Data corresponding to <GetSystemPerfData /> */
static void collect_perf_system_data(host_config_t *host, na_elem_t *out, void *data) { /* {{{ */
	counter_t disk_read = 0, disk_written = 0;
	counter_t net_recv = 0, net_sent = 0;
	counter_t cpu_busy = 0, cpu_total = 0;
	unsigned int counter_flags = 0;

	cfg_system_t *cfg_system = data;
	const char *instance;
	time_t timestamp;
	na_elem_t *counter;
	
	timestamp = (time_t) na_child_get_uint64(out, "timestamp", 0);
	out = na_elem_child(na_elem_child(out, "instances"), "instance-data");
	instance = na_child_get_string(out, "name");

	na_elem_iter_t iter = na_child_iterator(na_elem_child(out, "counters"));
	for (counter = na_iterator_next(&iter); counter; counter = na_iterator_next(&iter)) {
		const char *name;
		uint64_t value;

		name = na_child_get_string(counter, "name");
		if (name == NULL)
			continue;

		value = na_child_get_uint64(counter, "value", UINT64_MAX);
		if (value == UINT64_MAX)
			continue;

		if (!strcmp(name, "disk_data_read")) {
			disk_read = (counter_t) (value * 1024);
			counter_flags |= 0x01;
		} else if (!strcmp(name, "disk_data_written")) {
			disk_written = (counter_t) (value * 1024);
			counter_flags |= 0x02;
		} else if (!strcmp(name, "net_data_recv")) {
			net_recv = (counter_t) (value * 1024);
			counter_flags |= 0x04;
		} else if (!strcmp(name, "net_data_sent")) {
			net_sent = (counter_t) (value * 1024);
			counter_flags |= 0x08;
		} else if (!strcmp(name, "cpu_busy")) {
			cpu_busy = (counter_t) value;
			counter_flags |= 0x10;
		} else if (!strcmp(name, "cpu_elapsed_time")) {
			cpu_total = (counter_t) value;
			counter_flags |= 0x20;
		} else if ((cfg_system->flags & CFG_SYSTEM_OPS)
				&& (strlen(name) > 4)
				&& (!strcmp(name + strlen(name) - 4, "_ops"))) {
			submit_counter (host->name, instance, "disk_ops_complex", name,
					(counter_t) value, timestamp);
		}
	} /* for (counter) */

	if ((cfg_system->flags & CFG_SYSTEM_DISK)
			&& ((counter_flags & 0x03) == 0x03))
		submit_two_counters (host->name, instance, "disk_octets", NULL,
				disk_read, disk_written, timestamp);
				
	if ((cfg_system->flags & CFG_SYSTEM_NET)
			&& ((counter_flags & 0x0c) == 0x0c))
		submit_two_counters (host->name, instance, "if_octets", NULL,
				net_recv, net_sent, timestamp);

	if ((cfg_system->flags & CFG_SYSTEM_CPU)
			&& ((counter_flags & 0x30) == 0x30)) {
		submit_counter (host->name, instance, "cpu", "system",
				cpu_busy, timestamp);
		submit_counter (host->name, instance, "cpu", "idle",
				cpu_total - cpu_busy, timestamp);
	}
} /* }}} void collect_perf_system_data */

/*
 * Configuration handling
 */
/* Sets a given flag if the boolean argument is true and unsets the flag if it
 * is false. On error, the flag-field is not changed. */
static int cna_config_bool_to_flag (const oconfig_item_t *ci, /* {{{ */
		uint32_t *flags, uint32_t flag)
{
	if ((ci == NULL) || (flags == NULL))
		return (EINVAL);

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
	{
		WARNING ("netapp plugin: The %s option needs exactly one boolean argument.",
				ci->key);
		return (-1);
	}

	if (ci->values[0].value.boolean)
		*flags |= flag;
	else
		*flags &= ~flag;

	return (0);
} /* }}} int cna_config_bool_to_flag */

/* Handling of the "Multiplier" option which is allowed in every block. */
static int cna_config_get_multiplier (const oconfig_item_t *ci, /* {{{ */
		cfg_service_t *service)
{
	int tmp;

	if ((ci == NULL) || (service == NULL))
		return (EINVAL);

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
	{
		WARNING ("netapp plugin: The `Multiplier' option needs exactly one numeric argument.");
		return (-1);
	}

	tmp = (int) (ci->values[0].value.number + .5);
	if (tmp < 1)
	{
		WARNING ("netapp plugin: The `Multiplier' option needs a positive integer argument.");
		return (-1);
	}

	service->multiplier = tmp;
	service->skip_countdown = tmp;

	return (0);
} /* }}} int cna_config_get_multiplier */

/* Handling of the "GetIO", "GetOps" and "GetLatency" options within a
 * <GetVolumePerfData /> block. */
static void cna_config_volume_performance_option (host_config_t *host, /* {{{ */
		cfg_volume_perf_t *perf_volume, const oconfig_item_t *item,
		uint32_t flag)
{
	int i;
	
	for (i = 0; i < item->values_num; ++i) {
		const char *name;
		volume_t *v;
		_Bool set = true;

		if (item->values[i].type != OCONFIG_TYPE_STRING) {
			WARNING("netapp plugin: Ignoring non-string argument in "
					"\"GetVolumePerfData\" block for host %s", host->name);
			continue;
		}

		name = item->values[i].value.string;
		if (name[0] == '+') {
			set = true;
			++name;
		} else if (name[0] == '-') {
			set = false;
			++name;
		}

		if (!name[0]) {
			if (set)
				perf_volume->flags |= flag;
			else /* if (!set) */
				perf_volume->flags &= ~flag;

			set_global_perf_vol_flag(host, flag, set);
			continue;
		}

		v = get_volume (host, name, /* data_flags = */ 0, perf_volume->flags);
		if (v == NULL)
			continue;

		if (set)
			v->perf_data.flags |= flag;
		else /* if (!set) */
			v->perf_data.flags &= ~flag;
	} /* for (i = 0 .. item->values_num) */
} /* }}} void cna_config_volume_performance_option */

/* Corresponds to a <GetDiskPerfData /> block */
static void cna_config_volume_performance(host_config_t *host, const oconfig_item_t *ci) { /* {{{ */
	int i, had_io = 0, had_ops = 0, had_latency = 0;
	cfg_service_t *service;
	cfg_volume_perf_t *perf_volume;
	
	service = malloc(sizeof(*service));
	service->query = 0;
	service->handler = query_volume_perf_data;
	perf_volume = service->data = malloc(sizeof(*perf_volume));
	perf_volume->flags = CFG_VOLUME_PERF_INIT;
	service->next = host->services;
	host->services = service;
	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *item = ci->children + i;
		
		/* if (!item || !item->key || !*item->key) continue; */
		if (!strcasecmp(item->key, "Multiplier")) {
			cna_config_get_multiplier (item, service);
		} else if (!strcasecmp(item->key, "GetIO")) {
			had_io = 1;
			cna_config_volume_performance_option(host, perf_volume, item, CFG_VOLUME_PERF_IO);
		} else if (!strcasecmp(item->key, "GetOps")) {
			had_ops = 1;
			cna_config_volume_performance_option(host, perf_volume, item, CFG_VOLUME_PERF_OPS);
		} else if (!strcasecmp(item->key, "GetLatency")) {
			had_latency = 1;
			cna_config_volume_performance_option(host, perf_volume, item, CFG_VOLUME_PERF_LATENCY);
		}
	}
	if (!had_io) {
		perf_volume->flags |= CFG_VOLUME_PERF_IO;
		set_global_perf_vol_flag(host, CFG_VOLUME_PERF_IO, /* set = */ true);
	}
	if (!had_ops) {
		perf_volume->flags |= CFG_VOLUME_PERF_OPS;
		set_global_perf_vol_flag(host, CFG_VOLUME_PERF_OPS, /* set = */ true);
	}
	if (!had_latency) {
		perf_volume->flags |= CFG_VOLUME_PERF_LATENCY;
		set_global_perf_vol_flag(host, CFG_VOLUME_PERF_LATENCY, /* set = */ true);
	}
} /* }}} void cna_config_volume_performance */

/* Handling of the "GetDiskUtil" option within a <GetVolumeData /> block. */
static void cna_config_volume_usage_option (host_config_t *host, /* {{{ */
		cfg_volume_usage_t *cfg_volume_data, const oconfig_item_t *item, uint32_t flag)
{
	int i;
	
	for (i = 0; i < item->values_num; ++i) {
		const char *name;
		volume_t *v;
		_Bool set = true;

		if (item->values[i].type != OCONFIG_TYPE_STRING) {
			WARNING("netapp plugin: Ignoring non-string argument in \"GetVolData\""
					"block for host %s", host->name);
			continue;
		}

		name = item->values[i].value.string;
		if (name[0] == '+') {
			set = true;
			++name;
		} else if (name[0] == '-') {
			set = false;
			++name;
		}

		if (!name[0]) {
			if (set)
				cfg_volume_data->flags |= flag;
			else /* if (!set) */
				cfg_volume_data->flags &= ~flag;

			set_global_vol_flag(host, flag, set);
			continue;
		}

		v = get_volume(host, name, cfg_volume_data->flags, /* perf_flags = */ 0);
		if (v == NULL)
			continue;

		if (!v->cfg_volume_usage.flags)
			v->cfg_volume_usage.flags = cfg_volume_data->flags;

		if (set)
			v->cfg_volume_usage.flags |= flag;
		else /* if (!set) */
			v->cfg_volume_usage.flags &= ~flag;
	}
} /* }}} void cna_config_volume_usage_option */

/* Corresponds to a <GetVolumeData /> block */
static void cna_config_volume_usage(host_config_t *host, oconfig_item_t *ci) { /* {{{ */
	int i, had_df = 0;
	cfg_service_t *service;
	cfg_volume_usage_t *cfg_volume_data;
	
	service = malloc(sizeof(*service));
	service->query = 0;
	service->handler = collect_volume_data;
	cfg_volume_data = service->data = malloc(sizeof(*cfg_volume_data));
	cfg_volume_data->flags = VOLUME_INIT;
	service->next = host->services;
	host->services = service;
	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *item = ci->children + i;
		
		/* if (!item || !item->key || !*item->key) continue; */
		if (!strcasecmp(item->key, "Multiplier")) {
			cna_config_get_multiplier (item, service);
		} else if (!strcasecmp(item->key, "GetDiskUtil")) {
			had_df = 1;
			cna_config_volume_usage_option(host, cfg_volume_data, item, VOLUME_DF);
		}
	}
	if (!had_df) {
		cfg_volume_data->flags |= VOLUME_DF;
		set_global_vol_flag(host, VOLUME_DF, /* set = */ true);
	}
} /* }}} void cna_config_volume_usage */

/* Corresponds to a <GetDiskPerfData /> block */
static void cna_config_disk(host_config_t *temp, oconfig_item_t *ci) { /* {{{ */
	int i;
	cfg_service_t *service;
	cfg_disk_t *cfg_disk;
	
	service = malloc(sizeof(*service));
	service->query = 0;
	service->handler = query_submit_disk_data;
	cfg_disk = service->data = malloc(sizeof(*cfg_disk));
	cfg_disk->flags = CFG_DISK_ALL;
	service->next = temp->services;
	temp->services = service;
	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *item = ci->children + i;
		
		/* if (!item || !item->key || !*item->key) continue; */
		if (!strcasecmp(item->key, "Multiplier")) {
			cna_config_get_multiplier (item, service);
		} else if (!strcasecmp(item->key, "GetBusy")) {
			cna_config_bool_to_flag (item, &cfg_disk->flags, CFG_SYSTEM_CPU);
		}
	}
} /* }}} void cna_config_disk */

/* Corresponds to a <GetWaflPerfData /> block */
static void cna_config_wafl(host_config_t *host, oconfig_item_t *ci) { /* {{{ */
	int i;
	cfg_service_t *service;
	data_wafl_t *perf_wafl;
	
	service = malloc(sizeof(*service));
	if (service == NULL)
		return;
	memset (service, 0, sizeof (*service));

	service->query = 0;
	service->handler = query_wafl_data;
	perf_wafl = service->data = malloc(sizeof(*perf_wafl));
	perf_wafl->flags = CFG_WAFL_ALL;

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *item = ci->children + i;
		
		if (!strcasecmp(item->key, "Multiplier")) {
			cna_config_get_multiplier (item, service);
		} else if (!strcasecmp(item->key, "GetNameCache")) {
			cna_config_bool_to_flag (item, &perf_wafl->flags, CFG_WAFL_NAME_CACHE);
		} else if (!strcasecmp(item->key, "GetDirCache")) {
			cna_config_bool_to_flag (item, &perf_wafl->flags, CFG_WAFL_DIR_CACHE);
		} else if (!strcasecmp(item->key, "GetBufCache")) {
			cna_config_bool_to_flag (item, &perf_wafl->flags, CFG_WAFL_BUF_CACHE);
		} else if (!strcasecmp(item->key, "GetInodeCache")) {
			cna_config_bool_to_flag (item, &perf_wafl->flags, CFG_WAFL_INODE_CACHE);
		} else {
			WARNING ("netapp plugin: The %s config option is not allowed within "
					"`GetWaflPerfData' blocks.", item->key);
		}
	}

	service->next = host->services;
	host->services = service;
} /* }}} void cna_config_wafl */

/* Corresponds to a <GetSystemPerfData /> block */
static int cna_config_system (host_config_t *host, /* {{{ */
		oconfig_item_t *ci, const cfg_service_t *default_service)
{
	int i;
	cfg_service_t *service;
	cfg_system_t *cfg_system;
	
	service = malloc(sizeof(*service));
	if (service == NULL)
		return (-1);
	memset (service, 0, sizeof (*service));
	*service = *default_service;
	service->handler = collect_perf_system_data;

	cfg_system = malloc(sizeof(*cfg_system));
	if (cfg_system == NULL) {
		sfree (service);
		return (-1);
	}
	memset (cfg_system, 0, sizeof (*cfg_system));
	cfg_system->flags = CFG_SYSTEM_ALL;
	service->data = cfg_system;

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *item = ci->children + i;

		if (!strcasecmp(item->key, "Multiplier")) {
			cna_config_get_multiplier (item, service);
		} else if (!strcasecmp(item->key, "GetCPULoad")) {
			cna_config_bool_to_flag (item, &cfg_system->flags, CFG_SYSTEM_CPU);
		} else if (!strcasecmp(item->key, "GetInterfaces")) {
			cna_config_bool_to_flag (item, &cfg_system->flags, CFG_SYSTEM_NET);
		} else if (!strcasecmp(item->key, "GetDiskOps")) {
			cna_config_bool_to_flag (item, &cfg_system->flags, CFG_SYSTEM_OPS);
		} else if (!strcasecmp(item->key, "GetDiskIO")) {
			cna_config_bool_to_flag (item, &cfg_system->flags, CFG_SYSTEM_DISK);
		} else {
			WARNING ("netapp plugin: The %s config option is not allowed within "
					"`GetSystemPerfData' blocks.", item->key);
		}
	}

	service->next = host->services;
	host->services = service;

	return (0);
} /* }}} int cna_config_system */

/* Corresponds to a <Host /> block. */
static host_config_t *cna_config_host (const oconfig_item_t *ci, /* {{{ */
		const host_config_t *default_host, const cfg_service_t *def_def_service)
{
	int i;
	oconfig_item_t *item;
	host_config_t *host, *hc, temp = *default_host;
	cfg_service_t default_service = *def_def_service;
	
	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
		WARNING("netapp plugin: \"Host\" needs exactly one string argument. Ignoring host block.");
		return 0;
	}

	temp.name = ci->values[0].value.string;
	for (i = 0; i < ci->children_num; ++i) {
		item = ci->children + i;

		/* if (!item || !item->key || !*item->key) continue; */
		if (!strcasecmp(item->key, "Address")) {
			if ((item->values_num != 1) || (item->values[0].type != OCONFIG_TYPE_STRING)) {
				WARNING("netapp plugin: \"Name\" needs exactly one string argument. Ignoring host block \"%s\".", ci->values[0].value.string);
				return 0;
			}
			temp.host = item->values[0].value.string;
		} else if (!strcasecmp(item->key, "Port")) {
			if ((item->values_num != 1) || (item->values[0].type != OCONFIG_TYPE_NUMBER) || (item->values[0].value.number != (int) (item->values[0].value.number)) || (item->values[0].value.number < 1) || (item->values[0].value.number > 65535)) {
				WARNING("netapp plugin: \"Port\" needs exactly one integer argument in the range of 1-65535. Ignoring host block \"%s\".", ci->values[0].value.string);
				return 0;
			}
			temp.port = item->values[0].value.number;
		} else if (!strcasecmp(item->key, "Protocol")) {
			if ((item->values_num != 1) || (item->values[0].type != OCONFIG_TYPE_STRING) || (strcasecmp(item->values[0].value.string, "http") && strcasecmp(item->values[0].value.string, "https"))) {
				WARNING("netapp plugin: \"Protocol\" needs to be either \"http\" or \"https\". Ignoring host block \"%s\".", ci->values[0].value.string);
				return 0;
			}
			if (!strcasecmp(item->values[0].value.string, "http")) temp.protocol = NA_SERVER_TRANSPORT_HTTP;
			else temp.protocol = NA_SERVER_TRANSPORT_HTTPS;
		} else if (!strcasecmp(item->key, "Login")) {
			if ((item->values_num != 2) || (item->values[0].type != OCONFIG_TYPE_STRING) || (item->values[1].type != OCONFIG_TYPE_STRING)) {
				WARNING("netapp plugin: \"Login\" needs exactly two string arguments, username and password. Ignoring host block \"%s\".", ci->values[0].value.string);
				return 0;
			}
			temp.username = item->values[0].value.string;
			temp.password = item->values[1].value.string;
		} else if (!strcasecmp(item->key, "Interval")) {
			if (item->values_num != 1 || item->values[0].type != OCONFIG_TYPE_NUMBER || item->values[0].value.number != (int) item->values[0].value.number || item->values[0].value.number < 2) {
				WARNING("netapp plugin: \"Interval\" of host %s needs exactly one integer argument.", ci->values[0].value.string);
				continue;
			}
			temp.interval = item->values[0].value.number;
		} else if (!strcasecmp(item->key, "GetVolumePerfData")) {
			cna_config_volume_performance(&temp, item);
		} else if (!strcasecmp(item->key, "GetSystemPerfData")) {
			cna_config_system(&temp, item, &default_service);
		} else if (!strcasecmp(item->key, "GetWaflPerfData")) {
			cna_config_wafl(&temp, item);
		} else if (!strcasecmp(item->key, "GetDiskPerfData")) {
			cna_config_disk(&temp, item);
		} else if (!strcasecmp(item->key, "GetVolumeData")) {
			cna_config_volume_usage(&temp, item);
		} else {
			WARNING("netapp plugin: Ignoring unknown config option \"%s\" in host block \"%s\".",
					item->key, ci->values[0].value.string);
		}
	}
	
	if (!temp.host) temp.host = temp.name;
	if (!temp.port) temp.port = temp.protocol == NA_SERVER_TRANSPORT_HTTP ? 80 : 443;
	if (!temp.username) {
		WARNING("netapp plugin: Please supply login information for host \"%s\". Ignoring host block.", temp.name);
		return 0;
	}
	for (hc = host_config; hc; hc = hc->next) {
		if (!strcasecmp(hc->name, temp.name)) WARNING("netapp plugin: Duplicate definition of host \"%s\". This is probably a bad idea.", hc->name);
	}
	host = malloc(sizeof(*host));
	*host = temp;
	host->name = strdup(temp.name);
	host->protocol = temp.protocol;
	host->host = strdup(temp.host);
	host->username = strdup(temp.username);
	host->password = strdup(temp.password);
	host->next = host_config;
	host_config = host;
	return host;
} /* }}} host_config_t *cna_config_host */

/*
 * Callbacks registered with the daemon
 *
 * Pretty standard stuff here.
 */
static int cna_init(void) { /* {{{ */
	char err[256];
	na_elem_t *e;
	host_config_t *host;
	cfg_service_t *service;
	
	if (!host_config) {
		WARNING("netapp plugin: Plugin loaded but no hosts defined.");
		return 1;
	}

	if (!na_startup(err, sizeof(err))) {
		ERROR("netapp plugin: Error initializing netapp API: %s", err);
		return 1;
	}

	for (host = host_config; host; host = host->next) {
		host->srv = na_server_open(host->host, 1, 1); 
		na_server_set_transport_type(host->srv, host->protocol, 0);
		na_server_set_port(host->srv, host->port);
		na_server_style(host->srv, NA_STYLE_LOGIN_PASSWORD);
		na_server_adminuser(host->srv, host->username, host->password);
		na_server_set_timeout(host->srv, 5);
		for (service = host->services; service; service = service->next) {
			service->interval = host->interval * service->multiplier;
			if (service->handler == collect_perf_system_data) {
				service->query = na_elem_new("perf-object-get-instances");
				na_child_add_string(service->query, "objectname", "system");
			} else if (service->handler == query_volume_perf_data) {
				service->query = na_elem_new("perf-object-get-instances");
				na_child_add_string(service->query, "objectname", "volume");
/*				e = na_elem_new("instances");
				na_child_add_string(e, "foo", "system");
				na_child_add(root, e);*/
				e = na_elem_new("counters");
				na_child_add_string(e, "foo", "read_ops");
				na_child_add_string(e, "foo", "write_ops");
				na_child_add_string(e, "foo", "read_data");
				na_child_add_string(e, "foo", "write_data");
				na_child_add_string(e, "foo", "read_latency");
				na_child_add_string(e, "foo", "write_latency");
				na_child_add(service->query, e);
			} else if (service->handler == query_wafl_data) {
				service->query = na_elem_new("perf-object-get-instances");
				na_child_add_string(service->query, "objectname", "wafl");
/*				e = na_elem_new("instances");
				na_child_add_string(e, "foo", "system");
				na_child_add(root, e);*/
				e = na_elem_new("counters");
				na_child_add_string(e, "foo", "name_cache_hit");
				na_child_add_string(e, "foo", "name_cache_miss");
				na_child_add_string(e, "foo", "find_dir_hit");
				na_child_add_string(e, "foo", "find_dir_miss");
				na_child_add_string(e, "foo", "buf_hash_hit");
				na_child_add_string(e, "foo", "buf_hash_miss");
				na_child_add_string(e, "foo", "inode_cache_hit");
				na_child_add_string(e, "foo", "inode_cache_miss");
				/* na_child_add_string(e, "foo", "inode_eject_time"); */
				/* na_child_add_string(e, "foo", "buf_eject_time"); */
				na_child_add(service->query, e);
			} else if (service->handler == query_submit_disk_data) {
				service->query = na_elem_new("perf-object-get-instances");
				na_child_add_string(service->query, "objectname", "disk");
				e = na_elem_new("counters");
				na_child_add_string(e, "foo", "disk_busy");
				na_child_add_string(e, "foo", "base_for_disk_busy");
				na_child_add(service->query, e);
			} else if (service->handler == collect_volume_data) {
				service->query = na_elem_new("volume-list-info");
				/* na_child_add_string(service->query, "objectname", "volume"); */
				/* } else if (service->handler == collect_snapshot_data) { */
				/* service->query = na_elem_new("snapshot-list-info"); */
			}
		}
	}
	return 0;
} /* }}} int cna_init */

static int cna_config (oconfig_item_t *ci) { /* {{{ */
	int i;
	oconfig_item_t *item;
	host_config_t default_host = HOST_INIT;
	cfg_service_t default_service = SERVICE_INIT;
	
	for (i = 0; i < ci->children_num; ++i) {
		item = ci->children + i;

		/* if (!item || !item->key || !*item->key) continue; */
		if (!strcasecmp(item->key, "Host")) {
			cna_config_host(item, &default_host, &default_service);
		} else {
			WARNING("netapp plugin: Ignoring unknown config option \"%s\".", item->key);
		}
	}
	return 0;
} /* }}} int cna_config */

static int cna_read(void) { /* {{{ */
	na_elem_t *out;
	host_config_t *host;
	cfg_service_t *service;
	
	for (host = host_config; host; host = host->next) {
		for (service = host->services; service; service = service->next) {
			if (--service->skip_countdown > 0) continue;
			service->skip_countdown = service->multiplier;
			out = na_server_invoke_elem(host->srv, service->query);
			if (na_results_status(out) != NA_OK) {
				int netapp_errno = na_results_errno(out);
				ERROR("netapp plugin: Error %d from host %s: %s", netapp_errno, host->name, na_results_reason(out));
				na_elem_free(out);
				if (netapp_errno == EIO || netapp_errno == ETIMEDOUT) {
					/* Network problems. Just give up on all other services on this host. */
					break;
				}
				continue;
			}
			service->handler(host, out, service->data);
			na_elem_free(out);
		}
	}
	return 0;
} /* }}} int cna_read */

void module_register(void) {
	plugin_register_complex_config("netapp", cna_config);
	plugin_register_init("netapp", cna_init);
	plugin_register_read("netapp", cna_read);
}

/* vim: set sw=2 ts=2 noet fdm=marker : */
