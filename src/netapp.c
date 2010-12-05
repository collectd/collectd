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
 *   Sven Trenkel <collectd at semidefinite.de>  
 **/

#include "collectd.h"
#include "common.h"
#include "utils_ignorelist.h"

#include <netapp_api.h>
#include <netapp_errno.h>

#define HAS_ALL_FLAGS(has,needs) (((has) & (needs)) == (needs))

typedef struct host_config_s host_config_t;
typedef void service_handler_t(host_config_t *host, na_elem_t *result, void *data);

struct cna_interval_s
{
	cdtime_t interval;
	cdtime_t last_read;
};
typedef struct cna_interval_s cna_interval_t;

/*! Data types for WAFL statistics {{{
 *
 * \brief Persistent data for WAFL performance counters. (a.k.a. cache performance)
 *
 * The cache counters use old counter values to calculate a hit ratio for each
 * counter. The "cfg_wafl_t" struct therefore contains old counter values along
 * with flags, which are set if the counter is valid.
 *
 * The function "cna_handle_wafl_data" will fill a new structure of this kind
 * with new values, then pass both, new and old data, to "submit_wafl_data".
 * That function calculates the hit ratios, submits the calculated values and
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
	cna_interval_t interval;
	na_elem_t *query;

	cdtime_t timestamp;
	uint64_t name_cache_hit;
	uint64_t name_cache_miss;
	uint64_t find_dir_hit;
	uint64_t find_dir_miss;
	uint64_t buf_hash_hit;
	uint64_t buf_hash_miss;
	uint64_t inode_cache_hit;
	uint64_t inode_cache_miss;
} cfg_wafl_t;
/* }}} cfg_wafl_t */

/*! Data types for disk statistics {{{
 *
 * \brief A disk in the NetApp.
 *
 * A disk doesn't have any more information than its name at the moment.
 * The name includes the "disk_" prefix.
 */
#define HAVE_DISK_BUSY   0x10
#define HAVE_DISK_BASE   0x20
#define HAVE_DISK_ALL    0x30
typedef struct disk_s {
	char *name;
	uint32_t flags;
	cdtime_t timestamp;
	uint64_t disk_busy;
	uint64_t base_for_disk_busy;
	double disk_busy_percent;
	struct disk_s *next;
} disk_t;

#define CFG_DISK_BUSIEST 0x01
#define CFG_DISK_ALL     0x01
typedef struct {
	uint32_t flags;
	cna_interval_t interval;
	na_elem_t *query;
	disk_t *disks;
} cfg_disk_t;
/* }}} cfg_disk_t */

/*! Data types for volume performance statistics {{{
 *
 * \brief Persistent data for volume performance data.
 *
 * The code below uses the difference of the operations and latency counters to
 * calculate an average per-operation latency. For this, old counters need to
 * be stored in the "data_volume_perf_t" structure. The byte-counters are just
 * kept for completeness sake. The "flags" member indicates if each counter is
 * valid or not.
 *
 * The "cna_handle_volume_perf_data" function will fill a new struct of this
 * type and pass both, old and new data, to "submit_volume_perf_data". In that
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
struct data_volume_perf_s;
typedef struct data_volume_perf_s data_volume_perf_t;
struct data_volume_perf_s {
	char *name;
	uint32_t flags;
	cdtime_t timestamp;

	uint64_t read_bytes;
	uint64_t write_bytes;
	uint64_t read_ops;
	uint64_t write_ops;
	uint64_t read_latency;
	uint64_t write_latency;

	data_volume_perf_t *next;
};

typedef struct {
	cna_interval_t interval;
	na_elem_t *query;

	ignorelist_t *il_octets;
	ignorelist_t *il_operations;
	ignorelist_t *il_latency;

	data_volume_perf_t *volumes;
} cfg_volume_perf_t;
/* }}} data_volume_perf_t */

/*! Data types for volume usage statistics {{{
 *
 * \brief Configuration struct for volume usage data (free / used).
 */
#define CFG_VOLUME_USAGE_DF             0x0002
#define CFG_VOLUME_USAGE_SNAP           0x0004
#define CFG_VOLUME_USAGE_ALL            0x0006
#define HAVE_VOLUME_USAGE_NORM_FREE     0x0010
#define HAVE_VOLUME_USAGE_NORM_USED     0x0020
#define HAVE_VOLUME_USAGE_SNAP_RSVD     0x0040
#define HAVE_VOLUME_USAGE_SNAP_USED     0x0080
#define HAVE_VOLUME_USAGE_SIS_SAVED     0x0100
#define HAVE_VOLUME_USAGE_ALL           0x01f0
#define IS_VOLUME_USAGE_OFFLINE         0x0200
struct data_volume_usage_s;
typedef struct data_volume_usage_s data_volume_usage_t;
struct data_volume_usage_s {
	char *name;
	uint32_t flags;

	na_elem_t *snap_query;

	uint64_t norm_free;
	uint64_t norm_used;
	uint64_t snap_reserved;
	uint64_t snap_used;
	uint64_t sis_saved;

	data_volume_usage_t *next;
};

typedef struct {
	cna_interval_t interval;
	na_elem_t *query;

	ignorelist_t *il_capacity;
	ignorelist_t *il_snapshot;

	data_volume_usage_t *volumes;
} cfg_volume_usage_t;
/* }}} cfg_volume_usage_t */

/*! Data types for system statistics {{{
 *
 * \brief Persistent data for system performance counters
 */
#define CFG_SYSTEM_CPU  0x01
#define CFG_SYSTEM_NET  0x02
#define CFG_SYSTEM_OPS  0x04
#define CFG_SYSTEM_DISK 0x08
#define CFG_SYSTEM_ALL  0x0F
typedef struct {
	uint32_t flags;
	cna_interval_t interval;
	na_elem_t *query;
} cfg_system_t;
/* }}} cfg_system_t */

struct host_config_s {
	char *name;
	na_server_transport_t protocol;
	char *host;
	int port;
	char *username;
	char *password;
	cdtime_t interval;

	na_server_t *srv;
	cfg_wafl_t *cfg_wafl;
	cfg_disk_t *cfg_disk;
	cfg_volume_perf_t *cfg_volume_perf;
	cfg_volume_usage_t *cfg_volume_usage;
	cfg_system_t *cfg_system;

	struct host_config_s *next;
};

/*
 * Free functions
 *
 * Used to free the various structures above.
 */
static void free_disk (disk_t *disk) /* {{{ */
{
	disk_t *next;

	if (disk == NULL)
		return;

	next = disk->next;

	sfree (disk->name);
	sfree (disk);

	free_disk (next);
} /* }}} void free_disk */

static void free_cfg_wafl (cfg_wafl_t *cw) /* {{{ */
{
	if (cw == NULL)
		return;

	if (cw->query != NULL)
		na_elem_free (cw->query);

	sfree (cw);
} /* }}} void free_cfg_wafl */

static void free_cfg_disk (cfg_disk_t *cfg_disk) /* {{{ */
{
	if (cfg_disk == NULL)
		return;

	if (cfg_disk->query != NULL)
		na_elem_free (cfg_disk->query);

	free_disk (cfg_disk->disks);
	sfree (cfg_disk);
} /* }}} void free_cfg_disk */

static void free_cfg_volume_perf (cfg_volume_perf_t *cvp) /* {{{ */
{
	data_volume_perf_t *data;

	if (cvp == NULL)
		return;

	/* Free the ignorelists */
	ignorelist_free (cvp->il_octets);
	ignorelist_free (cvp->il_operations);
	ignorelist_free (cvp->il_latency);

	/* Free the linked list of volumes */
	data = cvp->volumes;
	while (data != NULL)
	{
		data_volume_perf_t *next = data->next;
		sfree (data->name);
		sfree (data);
		data = next;
	}

	if (cvp->query != NULL)
		na_elem_free (cvp->query);

	sfree (cvp);
} /* }}} void free_cfg_volume_perf */

static void free_cfg_volume_usage (cfg_volume_usage_t *cvu) /* {{{ */
{
	data_volume_usage_t *data;

	if (cvu == NULL)
		return;

	/* Free the ignorelists */
	ignorelist_free (cvu->il_capacity);
	ignorelist_free (cvu->il_snapshot);

	/* Free the linked list of volumes */
	data = cvu->volumes;
	while (data != NULL)
	{
		data_volume_usage_t *next = data->next;
		sfree (data->name);
		if (data->snap_query != NULL)
			na_elem_free(data->snap_query);
		sfree (data);
		data = next;
	}

	if (cvu->query != NULL)
		na_elem_free (cvu->query);

	sfree (cvu);
} /* }}} void free_cfg_volume_usage */

static void free_cfg_system (cfg_system_t *cs) /* {{{ */
{
	if (cs == NULL)
		return;

	if (cs->query != NULL)
		na_elem_free (cs->query);

	sfree (cs);
} /* }}} void free_cfg_system */

static void free_host_config (host_config_t *hc) /* {{{ */
{
	host_config_t *next;

	if (hc == NULL)
		return;

	next = hc->next;

	sfree (hc->name);
	sfree (hc->host);
	sfree (hc->username);
	sfree (hc->password);

	free_cfg_disk (hc->cfg_disk);
	free_cfg_wafl (hc->cfg_wafl);
	free_cfg_volume_perf (hc->cfg_volume_perf);
	free_cfg_volume_usage (hc->cfg_volume_usage);
	free_cfg_system (hc->cfg_system);

	if (hc->srv != NULL)
		na_server_close (hc->srv);

	sfree (hc);

	free_host_config (next);
} /* }}} void free_host_config */

/*
 * Auxiliary functions
 *
 * Used to look up volumes and disks or to handle flags.
 */
static disk_t *get_disk(cfg_disk_t *cd, const char *name) /* {{{ */
{
	disk_t *d;

	if ((cd == NULL) || (name == NULL))
		return (NULL);

	for (d = cd->disks; d != NULL; d = d->next) {
		if (strcmp(d->name, name) == 0)
			return d;
	}

	d = malloc(sizeof(*d));
	if (d == NULL)
		return (NULL);
	memset (d, 0, sizeof (*d));
	d->next = NULL;

	d->name = strdup(name);
	if (d->name == NULL) {
		sfree (d);
		return (NULL);
	}

	d->next = cd->disks;
	cd->disks = d;

	return d;
} /* }}} disk_t *get_disk */

static data_volume_usage_t *get_volume_usage (cfg_volume_usage_t *cvu, /* {{{ */
		const char *name)
{
	data_volume_usage_t *last;
	data_volume_usage_t *new;

	int ignore_capacity = 0;
	int ignore_snapshot = 0;

	if ((cvu == NULL) || (name == NULL))
		return (NULL);

	last = cvu->volumes;
	while (last != NULL)
	{
		if (strcmp (last->name, name) == 0)
			return (last);

		if (last->next == NULL)
			break;

		last = last->next;
	}

	/* Check the ignorelists. If *both* tell us to ignore a volume, return NULL. */
	ignore_capacity = ignorelist_match (cvu->il_capacity, name);
	ignore_snapshot = ignorelist_match (cvu->il_snapshot, name);
	if ((ignore_capacity != 0) && (ignore_snapshot != 0))
		return (NULL);

	/* Not found: allocate. */
	new = malloc (sizeof (*new));
	if (new == NULL)
		return (NULL);
	memset (new, 0, sizeof (*new));
	new->next = NULL;

	new->name = strdup (name);
	if (new->name == NULL)
	{
		sfree (new);
		return (NULL);
	}

	if (ignore_capacity == 0)
		new->flags |= CFG_VOLUME_USAGE_DF;
	if (ignore_snapshot == 0) {
		new->flags |= CFG_VOLUME_USAGE_SNAP;
		new->snap_query = na_elem_new ("snapshot-list-info");
		na_child_add_string(new->snap_query, "target-type", "volume");
		na_child_add_string(new->snap_query, "target-name", name);
	} else {
		new->snap_query = NULL;
	}

	/* Add to end of list. */
	if (last == NULL)
		cvu->volumes = new;
	else
		last->next = new;

	return (new);
} /* }}} data_volume_usage_t *get_volume_usage */

static data_volume_perf_t *get_volume_perf (cfg_volume_perf_t *cvp, /* {{{ */
		const char *name)
{
	data_volume_perf_t *last;
	data_volume_perf_t *new;

	int ignore_octets = 0;
	int ignore_operations = 0;
	int ignore_latency = 0;

	if ((cvp == NULL) || (name == NULL))
		return (NULL);

	last = cvp->volumes;
	while (last != NULL)
	{
		if (strcmp (last->name, name) == 0)
			return (last);

		if (last->next == NULL)
			break;

		last = last->next;
	}

	/* Check the ignorelists. If *all three* tell us to ignore a volume, return
	 * NULL. */
	ignore_octets = ignorelist_match (cvp->il_octets, name);
	ignore_operations = ignorelist_match (cvp->il_operations, name);
	ignore_latency = ignorelist_match (cvp->il_latency, name);
	if ((ignore_octets != 0) || (ignore_operations != 0)
			|| (ignore_latency != 0))
		return (NULL);

	/* Not found: allocate. */
	new = malloc (sizeof (*new));
	if (new == NULL)
		return (NULL);
	memset (new, 0, sizeof (*new));
	new->next = NULL;

	new->name = strdup (name);
	if (new->name == NULL)
	{
		sfree (new);
		return (NULL);
	}

	if (ignore_octets == 0)
		new->flags |= CFG_VOLUME_PERF_IO;
	if (ignore_operations == 0)
		new->flags |= CFG_VOLUME_PERF_OPS;
	if (ignore_latency == 0)
		new->flags |= CFG_VOLUME_PERF_LATENCY;

	/* Add to end of list. */
	if (last == NULL)
		cvp->volumes = new;
	else
		last->next = new;

	return (new);
} /* }}} data_volume_perf_t *get_volume_perf */

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
		cdtime_t timestamp, cdtime_t interval)
{
	value_list_t vl = VALUE_LIST_INIT;

	vl.values = values;
	vl.values_len = values_len;

	if (timestamp > 0)
		vl.time = timestamp;

	if (interval > 0)
		vl.interval = interval;

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

static int submit_two_derive (const char *host, const char *plugin_inst, /* {{{ */
		const char *type, const char *type_inst, derive_t val0, derive_t val1,
		cdtime_t timestamp, cdtime_t interval)
{
	value_t values[2];

	values[0].derive = val0;
	values[1].derive = val1;

	return (submit_values (host, plugin_inst, type, type_inst,
				values, 2, timestamp, interval));
} /* }}} int submit_two_derive */

static int submit_derive (const char *host, const char *plugin_inst, /* {{{ */
		const char *type, const char *type_inst, derive_t counter,
		cdtime_t timestamp, cdtime_t interval)
{
	value_t v;

	v.derive = counter;

	return (submit_values (host, plugin_inst, type, type_inst,
				&v, 1, timestamp, interval));
} /* }}} int submit_derive */

static int submit_two_gauge (const char *host, const char *plugin_inst, /* {{{ */
		const char *type, const char *type_inst, gauge_t val0, gauge_t val1,
		cdtime_t timestamp, cdtime_t interval)
{
	value_t values[2];

	values[0].gauge = val0;
	values[1].gauge = val1;

	return (submit_values (host, plugin_inst, type, type_inst,
				values, 2, timestamp, interval));
} /* }}} int submit_two_gauge */

static int submit_double (const char *host, const char *plugin_inst, /* {{{ */
		const char *type, const char *type_inst, double d,
		cdtime_t timestamp, cdtime_t interval)
{
	value_t v;

	v.gauge = (gauge_t) d;

	return (submit_values (host, plugin_inst, type, type_inst,
				&v, 1, timestamp, interval));
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
		cdtime_t timestamp,
		cdtime_t interval)
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
				&v, 1, timestamp, interval));
} /* }}} int submit_cache_ratio */

/* Submits all the caches used by WAFL. Uses "submit_cache_ratio". */
static int submit_wafl_data (const char *hostname, const char *instance, /* {{{ */
		cfg_wafl_t *old_data, const cfg_wafl_t *new_data, int interval)
{
	/* Submit requested counters */
	if (HAS_ALL_FLAGS (old_data->flags, CFG_WAFL_NAME_CACHE | HAVE_WAFL_NAME_CACHE)
			&& HAS_ALL_FLAGS (new_data->flags, HAVE_WAFL_NAME_CACHE))
		submit_cache_ratio (hostname, instance, "name_cache_hit",
				new_data->name_cache_hit, new_data->name_cache_miss,
				old_data->name_cache_hit, old_data->name_cache_miss,
				new_data->timestamp, interval);

	if (HAS_ALL_FLAGS (old_data->flags, CFG_WAFL_DIR_CACHE | HAVE_WAFL_FIND_DIR)
			&& HAS_ALL_FLAGS (new_data->flags, HAVE_WAFL_FIND_DIR))
		submit_cache_ratio (hostname, instance, "find_dir_hit",
				new_data->find_dir_hit, new_data->find_dir_miss,
				old_data->find_dir_hit, old_data->find_dir_miss,
				new_data->timestamp, interval);

	if (HAS_ALL_FLAGS (old_data->flags, CFG_WAFL_BUF_CACHE | HAVE_WAFL_BUF_HASH)
			&& HAS_ALL_FLAGS (new_data->flags, HAVE_WAFL_BUF_HASH))
		submit_cache_ratio (hostname, instance, "buf_hash_hit",
				new_data->buf_hash_hit, new_data->buf_hash_miss,
				old_data->buf_hash_hit, old_data->buf_hash_miss,
				new_data->timestamp, interval);

	if (HAS_ALL_FLAGS (old_data->flags, CFG_WAFL_INODE_CACHE | HAVE_WAFL_INODE_CACHE)
			&& HAS_ALL_FLAGS (new_data->flags, HAVE_WAFL_INODE_CACHE))
		submit_cache_ratio (hostname, instance, "inode_cache_hit",
				new_data->inode_cache_hit, new_data->inode_cache_miss,
				old_data->inode_cache_hit, old_data->inode_cache_miss,
				new_data->timestamp, interval);

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
static int submit_volume_perf_data (const char *hostname, /* {{{ */
		data_volume_perf_t *old_data,
		const data_volume_perf_t *new_data, int interval)
{
	char plugin_instance[DATA_MAX_NAME_LEN];

	if ((hostname == NULL) || (old_data == NULL) || (new_data == NULL))
		return (-1);

	ssnprintf (plugin_instance, sizeof (plugin_instance),
			"volume-%s", old_data->name);

	/* Check for and submit disk-octet values */
	if (HAS_ALL_FLAGS (old_data->flags, CFG_VOLUME_PERF_IO)
			&& HAS_ALL_FLAGS (new_data->flags, HAVE_VOLUME_PERF_BYTES_READ | HAVE_VOLUME_PERF_BYTES_WRITE))
	{
		submit_two_derive (hostname, plugin_instance, "disk_octets", /* type instance = */ NULL,
				(derive_t) new_data->read_bytes, (derive_t) new_data->write_bytes, new_data->timestamp, interval);
	}

	/* Check for and submit disk-operations values */
	if (HAS_ALL_FLAGS (old_data->flags, CFG_VOLUME_PERF_OPS)
			&& HAS_ALL_FLAGS (new_data->flags, HAVE_VOLUME_PERF_OPS_READ | HAVE_VOLUME_PERF_OPS_WRITE))
	{
		submit_two_derive (hostname, plugin_instance, "disk_ops", /* type instance = */ NULL,
				(derive_t) new_data->read_ops, (derive_t) new_data->write_ops, new_data->timestamp, interval);
	}

	/* Check for, calculate and submit disk-latency values */
	if (HAS_ALL_FLAGS (old_data->flags, CFG_VOLUME_PERF_LATENCY
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
		if ((new_data->read_ops > old_data->read_ops)
				&& (new_data->read_latency > old_data->read_latency))
		{
			uint64_t diff_ops_read;
			uint64_t diff_latency_read;

			diff_ops_read = new_data->read_ops - old_data->read_ops;
			diff_latency_read = new_data->read_latency - old_data->read_latency;

			if (diff_ops_read > 0)
				latency_per_op_read = ((gauge_t) diff_latency_read) / ((gauge_t) diff_ops_read);
		}

		if ((new_data->write_ops > old_data->write_ops)
				&& (new_data->write_latency > old_data->write_latency))
		{
			uint64_t diff_ops_write;
			uint64_t diff_latency_write;

			diff_ops_write = new_data->write_ops - old_data->write_ops;
			diff_latency_write = new_data->write_latency - old_data->write_latency;

			if (diff_ops_write > 0)
				latency_per_op_write = ((gauge_t) diff_latency_write) / ((gauge_t) diff_ops_write);
		}

		submit_two_gauge (hostname, plugin_instance, "disk_latency", /* type instance = */ NULL,
				latency_per_op_read, latency_per_op_write, new_data->timestamp, interval);
	}

	/* Clear all HAVE_* flags. */
	old_data->flags &= ~HAVE_VOLUME_PERF_ALL;

	/* Copy all counters */
	old_data->timestamp = new_data->timestamp;
	old_data->read_bytes = new_data->read_bytes;
	old_data->write_bytes = new_data->write_bytes;
	old_data->read_ops = new_data->read_ops;
	old_data->write_ops = new_data->write_ops;
	old_data->read_latency = new_data->read_latency;
	old_data->write_latency = new_data->write_latency;

	/* Copy the HAVE_* flags */
	old_data->flags |= (new_data->flags & HAVE_VOLUME_PERF_ALL);

	return (0);
} /* }}} int submit_volume_perf_data */

static cdtime_t cna_child_get_cdtime (na_elem_t *data) /* {{{ */
{
	time_t t;

	t = (time_t) na_child_get_uint64 (data, "timestamp", /* default = */ 0);

	return (TIME_T_TO_CDTIME_T (t));
} /* }}} cdtime_t cna_child_get_cdtime */


/* 
 * Query functions
 *
 * These functions are called with appropriate data returned by the libnetapp
 * interface which is parsed and submitted with the above functions.
 */
/* Data corresponding to <WAFL /> */
static int cna_handle_wafl_data (const char *hostname, cfg_wafl_t *cfg_wafl, /* {{{ */
		na_elem_t *data, int interval)
{
	cfg_wafl_t perf_data;
	const char *plugin_inst;

	na_elem_t *instances;
	na_elem_t *counter;
	na_elem_iter_t counter_iter;

	memset (&perf_data, 0, sizeof (perf_data));
	
	perf_data.timestamp = cna_child_get_cdtime (data);

	instances = na_elem_child(na_elem_child (data, "instances"), "instance-data");
	if (instances == NULL)
	{
		ERROR ("netapp plugin: cna_handle_wafl_data: "
				"na_elem_child (\"instances\") failed "
				"for host %s.", hostname);
		return (-1);
	}

	plugin_inst = na_child_get_string(instances, "name");
	if (plugin_inst == NULL)
	{
		ERROR ("netapp plugin: cna_handle_wafl_data: "
				"na_child_get_string (\"name\") failed "
				"for host %s.", hostname);
		return (-1);
	}

	/* Iterate over all counters */
	counter_iter = na_child_iterator (na_elem_child (instances, "counters"));
	for (counter = na_iterator_next (&counter_iter);
			counter != NULL;
			counter = na_iterator_next (&counter_iter))
	{
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
			DEBUG("netapp plugin: cna_handle_wafl_data: "
					"Found unexpected child: %s "
					"for host %s.", name, hostname);
		}
	}

	return (submit_wafl_data (hostname, plugin_inst, cfg_wafl, &perf_data, interval));
} /* }}} void cna_handle_wafl_data */

static int cna_setup_wafl (cfg_wafl_t *cw) /* {{{ */
{
	na_elem_t *e;

	if (cw == NULL)
		return (EINVAL);

	if (cw->query != NULL)
		return (0);

	cw->query = na_elem_new("perf-object-get-instances");
	if (cw->query == NULL)
	{
		ERROR ("netapp plugin: na_elem_new failed.");
		return (-1);
	}
	na_child_add_string (cw->query, "objectname", "wafl");

	e = na_elem_new("counters");
	if (e == NULL)
	{
		na_elem_free (cw->query);
		cw->query = NULL;
		ERROR ("netapp plugin: na_elem_new failed.");
		return (-1);
	}
	na_child_add_string(e, "counter", "name_cache_hit");
	na_child_add_string(e, "counter", "name_cache_miss");
	na_child_add_string(e, "counter", "find_dir_hit");
	na_child_add_string(e, "counter", "find_dir_miss");
	na_child_add_string(e, "counter", "buf_hash_hit");
	na_child_add_string(e, "counter", "buf_hash_miss");
	na_child_add_string(e, "counter", "inode_cache_hit");
	na_child_add_string(e, "counter", "inode_cache_miss");

	na_child_add(cw->query, e);

	return (0);
} /* }}} int cna_setup_wafl */

static int cna_query_wafl (host_config_t *host) /* {{{ */
{
	na_elem_t *data;
	int status;
	cdtime_t now;

	if (host == NULL)
		return (EINVAL);

	/* If WAFL was not configured, return without doing anything. */
	if (host->cfg_wafl == NULL)
		return (0);

	now = cdtime ();
	if ((host->cfg_wafl->interval.interval + host->cfg_wafl->interval.last_read) > now)
		return (0);

	status = cna_setup_wafl (host->cfg_wafl);
	if (status != 0)
		return (status);
	assert (host->cfg_wafl->query != NULL);

	data = na_server_invoke_elem(host->srv, host->cfg_wafl->query);
	if (na_results_status (data) != NA_OK)
	{
		ERROR ("netapp plugin: cna_query_wafl: na_server_invoke_elem failed for host %s: %s",
				host->name, na_results_reason (data));
		na_elem_free (data);
		return (-1);
	}

	status = cna_handle_wafl_data (host->name, host->cfg_wafl, data, host->interval);

	if (status == 0)
		host->cfg_wafl->interval.last_read = now;

	na_elem_free (data);
	return (status);
} /* }}} int cna_query_wafl */

/* Data corresponding to <Disks /> */
static int cna_handle_disk_data (const char *hostname, /* {{{ */
		cfg_disk_t *cfg_disk, na_elem_t *data, cdtime_t interval)
{
	cdtime_t timestamp;
	na_elem_t *instances;
	na_elem_t *instance;
	na_elem_iter_t instance_iter;
	disk_t *worst_disk = NULL;

	if ((cfg_disk == NULL) || (data == NULL))
		return (EINVAL);
	
	timestamp = cna_child_get_cdtime (data);

	instances = na_elem_child (data, "instances");
	if (instances == NULL)
	{
		ERROR ("netapp plugin: cna_handle_disk_data: "
				"na_elem_child (\"instances\") failed "
				"for host %s.", hostname);
		return (-1);
	}

	/* Iterate over all children */
	instance_iter = na_child_iterator (instances);
	for (instance = na_iterator_next (&instance_iter);
			instance != NULL;
			instance = na_iterator_next(&instance_iter))
	{
		disk_t *old_data;
		disk_t  new_data;

		na_elem_iter_t counter_iterator;
		na_elem_t *counter;

		memset (&new_data, 0, sizeof (new_data));
		new_data.timestamp = timestamp;
		new_data.disk_busy_percent = NAN;

		old_data = get_disk(cfg_disk, na_child_get_string (instance, "name"));
		if (old_data == NULL)
			continue;

		/* Look for the "disk_busy" and "base_for_disk_busy" counters */
		counter_iterator = na_child_iterator(na_elem_child(instance, "counters"));
		for (counter = na_iterator_next(&counter_iterator);
				counter != NULL;
				counter = na_iterator_next(&counter_iterator))
		{
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
			else
			{
				DEBUG ("netapp plugin: cna_handle_disk_data: "
						"Counter not handled: %s = %"PRIu64,
						name, value);
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
		submit_double (hostname, "system", "percent", "disk_busy",
				worst_disk->disk_busy_percent, timestamp, interval);

	return (0);
} /* }}} int cna_handle_disk_data */

static int cna_setup_disk (cfg_disk_t *cd) /* {{{ */
{
	na_elem_t *e;

	if (cd == NULL)
		return (EINVAL);

	if (cd->query != NULL)
		return (0);

	cd->query = na_elem_new ("perf-object-get-instances");
	if (cd->query == NULL)
	{
		ERROR ("netapp plugin: na_elem_new failed.");
		return (-1);
	}
	na_child_add_string (cd->query, "objectname", "disk");

	e = na_elem_new("counters");
	if (e == NULL)
	{
		na_elem_free (cd->query);
		cd->query = NULL;
		ERROR ("netapp plugin: na_elem_new failed.");
		return (-1);
	}
	na_child_add_string(e, "counter", "disk_busy");
	na_child_add_string(e, "counter", "base_for_disk_busy");
	na_child_add(cd->query, e);

	return (0);
} /* }}} int cna_setup_disk */

static int cna_query_disk (host_config_t *host) /* {{{ */
{
	na_elem_t *data;
	int status;
	cdtime_t now;

	if (host == NULL)
		return (EINVAL);

	/* If the user did not configure disk statistics, return without doing
	 * anything. */
	if (host->cfg_disk == NULL)
		return (0);

	now = cdtime ();
	if ((host->cfg_disk->interval.interval + host->cfg_disk->interval.last_read) > now)
		return (0);

	status = cna_setup_disk (host->cfg_disk);
	if (status != 0)
		return (status);
	assert (host->cfg_disk->query != NULL);

	data = na_server_invoke_elem(host->srv, host->cfg_disk->query);
	if (na_results_status (data) != NA_OK)
	{
		ERROR ("netapp plugin: cna_query_disk: na_server_invoke_elem failed for host %s: %s",
				host->name, na_results_reason (data));
		na_elem_free (data);
		return (-1);
	}

	status = cna_handle_disk_data (host->name, host->cfg_disk, data, host->interval);

	if (status == 0)
		host->cfg_disk->interval.last_read = now;

	na_elem_free (data);
	return (status);
} /* }}} int cna_query_disk */

/* Data corresponding to <VolumePerf /> */
static int cna_handle_volume_perf_data (const char *hostname, /* {{{ */
		cfg_volume_perf_t *cvp, na_elem_t *data, cdtime_t interval)
{
	cdtime_t timestamp;
	na_elem_t *elem_instances;
	na_elem_iter_t iter_instances;
	na_elem_t *elem_instance;
	
	timestamp = cna_child_get_cdtime (data);

	elem_instances = na_elem_child(data, "instances");
	if (elem_instances == NULL)
	{
		ERROR ("netapp plugin: handle_volume_perf_data: "
				"na_elem_child (\"instances\") failed "
				"for host %s.", hostname);
		return (-1);
	}

	iter_instances = na_child_iterator (elem_instances);
	for (elem_instance = na_iterator_next(&iter_instances);
			elem_instance != NULL;
			elem_instance = na_iterator_next(&iter_instances))
	{
		const char *name;

		data_volume_perf_t perf_data;
		data_volume_perf_t *v;

		na_elem_t *elem_counters;
		na_elem_iter_t iter_counters;
		na_elem_t *elem_counter;

		memset (&perf_data, 0, sizeof (perf_data));
		perf_data.timestamp = timestamp;

		name = na_child_get_string (elem_instance, "name");
		if (name == NULL)
			continue;

		/* get_volume_perf may return NULL if this volume is to be ignored. */
		v = get_volume_perf (cvp, name);
		if (v == NULL)
			continue;

		elem_counters = na_elem_child (elem_instance, "counters");
		if (elem_counters == NULL)
			continue;

		iter_counters = na_child_iterator (elem_counters);
		for (elem_counter = na_iterator_next(&iter_counters);
				elem_counter != NULL;
				elem_counter = na_iterator_next(&iter_counters))
		{
			const char *name;
			uint64_t value;

			name = na_child_get_string (elem_counter, "name");
			if (name == NULL)
				continue;

			value = na_child_get_uint64 (elem_counter, "value", UINT64_MAX);
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
		} /* for (elem_counter) */

		submit_volume_perf_data (hostname, v, &perf_data, interval);
	} /* for (volume) */

	return (0);
} /* }}} int cna_handle_volume_perf_data */

static int cna_setup_volume_perf (cfg_volume_perf_t *cd) /* {{{ */
{
	na_elem_t *e;

	if (cd == NULL)
		return (EINVAL);

	if (cd->query != NULL)
		return (0);

	cd->query = na_elem_new ("perf-object-get-instances");
	if (cd->query == NULL)
	{
		ERROR ("netapp plugin: na_elem_new failed.");
		return (-1);
	}
	na_child_add_string (cd->query, "objectname", "volume");

	e = na_elem_new("counters");
	if (e == NULL)
	{
		na_elem_free (cd->query);
		cd->query = NULL;
		ERROR ("netapp plugin: na_elem_new failed.");
		return (-1);
	}
	na_child_add_string(e, "counter", "read_ops");
	na_child_add_string(e, "counter", "write_ops");
	na_child_add_string(e, "counter", "read_data");
	na_child_add_string(e, "counter", "write_data");
	na_child_add_string(e, "counter", "read_latency");
	na_child_add_string(e, "counter", "write_latency");
	na_child_add(cd->query, e);

	return (0);
} /* }}} int cna_setup_volume_perf */

static int cna_query_volume_perf (host_config_t *host) /* {{{ */
{
	na_elem_t *data;
	int status;
	cdtime_t now;

	if (host == NULL)
		return (EINVAL);

	/* If the user did not configure volume performance statistics, return
	 * without doing anything. */
	if (host->cfg_volume_perf == NULL)
		return (0);

	now = cdtime ();
	if ((host->cfg_volume_perf->interval.interval + host->cfg_volume_perf->interval.last_read) > now)
		return (0);

	status = cna_setup_volume_perf (host->cfg_volume_perf);
	if (status != 0)
		return (status);
	assert (host->cfg_volume_perf->query != NULL);

	data = na_server_invoke_elem (host->srv, host->cfg_volume_perf->query);
	if (na_results_status (data) != NA_OK)
	{
		ERROR ("netapp plugin: cna_query_volume_perf: na_server_invoke_elem failed for host %s: %s",
				host->name, na_results_reason (data));
		na_elem_free (data);
		return (-1);
	}

	status = cna_handle_volume_perf_data (host->name, host->cfg_volume_perf, data, host->interval);

	if (status == 0)
		host->cfg_volume_perf->interval.last_read = now;

	na_elem_free (data);
	return (status);
} /* }}} int cna_query_volume_perf */

/* Data corresponding to <VolumeUsage /> */
static int cna_submit_volume_usage_data (const char *hostname, /* {{{ */
		cfg_volume_usage_t *cfg_volume, int interval)
{
	data_volume_usage_t *v;

	for (v = cfg_volume->volumes; v != NULL; v = v->next)
	{
		char plugin_instance[DATA_MAX_NAME_LEN];

		uint64_t norm_used = v->norm_used;
		uint64_t norm_free = v->norm_free;
		uint64_t sis_saved = v->sis_saved;
		uint64_t snap_reserve_used = 0;
		uint64_t snap_reserve_free = v->snap_reserved;
		uint64_t snap_norm_used = v->snap_used;

		ssnprintf (plugin_instance, sizeof (plugin_instance),
				"volume-%s", v->name);

		if (HAS_ALL_FLAGS (v->flags, HAVE_VOLUME_USAGE_SNAP_USED | HAVE_VOLUME_USAGE_SNAP_RSVD)) {
			if (v->snap_reserved > v->snap_used) {
				snap_reserve_free = v->snap_reserved - v->snap_used;
				snap_reserve_used = v->snap_used;
				snap_norm_used = 0;
			} else {
				snap_reserve_free = 0;
				snap_reserve_used = v->snap_reserved;
				snap_norm_used = v->snap_used - v->snap_reserved;
			}
		}

		/* The space used by snapshots but not reserved for them is included in
		 * both, norm_used and snap_norm_used. If possible, subtract this here. */
		if (HAS_ALL_FLAGS (v->flags, HAVE_VOLUME_USAGE_NORM_USED | HAVE_VOLUME_USAGE_SNAP_USED))
		{
			if (norm_used >= snap_norm_used)
				norm_used -= snap_norm_used;
			else
			{
				ERROR ("netapp plugin: (norm_used = %"PRIu64") < (snap_norm_used = "
						"%"PRIu64") for host %s. Invalidating both.",
						norm_used, snap_norm_used, hostname);
				v->flags &= ~(HAVE_VOLUME_USAGE_NORM_USED | HAVE_VOLUME_USAGE_SNAP_USED);
			}
		}

		if (HAS_ALL_FLAGS (v->flags, HAVE_VOLUME_USAGE_NORM_FREE))
			submit_double (hostname, /* plugin instance = */ plugin_instance,
					"df_complex", "free",
					(double) norm_free, /* timestamp = */ 0, interval);

		if (HAS_ALL_FLAGS (v->flags, HAVE_VOLUME_USAGE_SIS_SAVED))
			submit_double (hostname, /* plugin instance = */ plugin_instance,
					"df_complex", "sis_saved",
					(double) sis_saved, /* timestamp = */ 0, interval);

		if (HAS_ALL_FLAGS (v->flags, HAVE_VOLUME_USAGE_NORM_USED))
			submit_double (hostname, /* plugin instance = */ plugin_instance,
					"df_complex", "used",
					(double) norm_used, /* timestamp = */ 0, interval);

		if (HAS_ALL_FLAGS (v->flags, HAVE_VOLUME_USAGE_SNAP_RSVD))
			submit_double (hostname, /* plugin instance = */ plugin_instance,
					"df_complex", "snap_reserved",
					(double) snap_reserve_free, /* timestamp = */ 0, interval);

		if (HAS_ALL_FLAGS (v->flags, HAVE_VOLUME_USAGE_SNAP_USED | HAVE_VOLUME_USAGE_SNAP_RSVD))
			submit_double (hostname, /* plugin instance = */ plugin_instance,
					"df_complex", "snap_reserve_used",
					(double) snap_reserve_used, /* timestamp = */ 0, interval);

		if (HAS_ALL_FLAGS (v->flags, HAVE_VOLUME_USAGE_SNAP_USED))
			submit_double (hostname, /* plugin instance = */ plugin_instance,
					"df_complex", "snap_normal_used",
					(double) snap_norm_used, /* timestamp = */ 0, interval);

		/* Clear all the HAVE_* flags */
		v->flags &= ~HAVE_VOLUME_USAGE_ALL;
	} /* for (v = cfg_volume->volumes) */

	return (0);
} /* }}} int cna_submit_volume_usage_data */

/* Switch the state of a volume between online and offline and send out a
 * notification. */
static int cna_change_volume_status (const char *hostname, /* {{{ */
		data_volume_usage_t *v)
{
	notification_t n;

	memset (&n, 0, sizeof (&n));
	n.time = cdtime ();
	sstrncpy (n.host, hostname, sizeof (n.host));
	sstrncpy (n.plugin, "netapp", sizeof (n.plugin));
	sstrncpy (n.plugin_instance, v->name, sizeof (n.plugin_instance));

	if ((v->flags & IS_VOLUME_USAGE_OFFLINE) != 0) {
		n.severity = NOTIF_OKAY;
		ssnprintf (n.message, sizeof (n.message),
				"Volume %s is now online.", v->name);
		v->flags &= ~IS_VOLUME_USAGE_OFFLINE;
	} else {
		n.severity = NOTIF_WARNING;
		ssnprintf (n.message, sizeof (n.message),
				"Volume %s is now offline.", v->name);
		v->flags |= IS_VOLUME_USAGE_OFFLINE;
	}

	return (plugin_dispatch_notification (&n));
} /* }}} int cna_change_volume_status */

static void cna_handle_volume_snap_usage(const host_config_t *host, /* {{{ */
		data_volume_usage_t *v)
{
	uint64_t snap_used = 0, value;
	na_elem_t *data, *elem_snap, *elem_snapshots;
	na_elem_iter_t iter_snap;

	data = na_server_invoke_elem(host->srv, v->snap_query);
	if (na_results_status(data) != NA_OK)
	{
		if (na_results_errno(data) == EVOLUMEOFFLINE) {
			if ((v->flags & IS_VOLUME_USAGE_OFFLINE) == 0)
				cna_change_volume_status (host->name, v);
		} else {
			ERROR ("netapp plugin: cna_handle_volume_snap_usage: na_server_invoke_elem for "
					"volume \"%s\" on host %s failed with error %d: %s", v->name,
					host->name, na_results_errno(data), na_results_reason(data));
		}
		na_elem_free(data);
		return;
	}

	if ((v->flags & IS_VOLUME_USAGE_OFFLINE) != 0)
		cna_change_volume_status (host->name, v);

	elem_snapshots = na_elem_child (data, "snapshots");
	if (elem_snapshots == NULL)
	{
		ERROR ("netapp plugin: cna_handle_volume_snap_usage: "
				"na_elem_child (\"snapshots\") failed "
				"for host %s.", host->name);
		na_elem_free(data);
		return;
	}

	iter_snap = na_child_iterator (elem_snapshots);
	for (elem_snap = na_iterator_next (&iter_snap);
			elem_snap != NULL;
			elem_snap = na_iterator_next (&iter_snap))
	{
		value = na_child_get_uint64(elem_snap, "cumulative-total", 0);
		/* "cumulative-total" is the total size of the oldest snapshot plus all
		 * newer ones in blocks (1KB). We therefore are looking for the highest
		 * number of all snapshots - that's the size required for the snapshots. */
		if (value > snap_used)
			snap_used = value;
	}
	na_elem_free (data);
	/* snap_used is in 1024 byte blocks */
	v->snap_used = snap_used * 1024;
	v->flags |= HAVE_VOLUME_USAGE_SNAP_USED;
} /* }}} void cna_handle_volume_snap_usage */

static int cna_handle_volume_usage_data (const host_config_t *host, /* {{{ */
		cfg_volume_usage_t *cfg_volume, na_elem_t *data)
{
	na_elem_t *elem_volume;
	na_elem_t *elem_volumes;
	na_elem_iter_t iter_volume;

	elem_volumes = na_elem_child (data, "volumes");
	if (elem_volumes == NULL)
	{
		ERROR ("netapp plugin: cna_handle_volume_usage_data: "
				"na_elem_child (\"volumes\") failed "
				"for host %s.", host->name);
		return (-1);
	}

	iter_volume = na_child_iterator (elem_volumes);
	for (elem_volume = na_iterator_next (&iter_volume);
			elem_volume != NULL;
			elem_volume = na_iterator_next (&iter_volume))
	{
		const char *volume_name, *state;

		data_volume_usage_t *v;
		uint64_t value;

		na_elem_t *sis;
		const char *sis_state;
		uint64_t sis_saved_reported;

		volume_name = na_child_get_string (elem_volume, "name");
		if (volume_name == NULL)
			continue;

		state = na_child_get_string (elem_volume, "state");
		if ((state == NULL) || (strcmp(state, "online") != 0))
			continue;

		/* get_volume_usage may return NULL if the volume is to be ignored. */
		v = get_volume_usage (cfg_volume, volume_name);
		if (v == NULL)
			continue;

		if ((v->flags & CFG_VOLUME_USAGE_SNAP) != 0)
			cna_handle_volume_snap_usage(host, v);
		
		if ((v->flags & CFG_VOLUME_USAGE_DF) == 0)
			continue;

		/* 2^4 exa-bytes? This will take a while ;) */
		value = na_child_get_uint64(elem_volume, "size-available", UINT64_MAX);
		if (value != UINT64_MAX) {
			v->norm_free = value;
			v->flags |= HAVE_VOLUME_USAGE_NORM_FREE;
		}

		value = na_child_get_uint64(elem_volume, "size-used", UINT64_MAX);
		if (value != UINT64_MAX) {
			v->norm_used = value;
			v->flags |= HAVE_VOLUME_USAGE_NORM_USED;
		}

		value = na_child_get_uint64(elem_volume, "snapshot-blocks-reserved", UINT64_MAX);
		if (value != UINT64_MAX) {
			/* 1 block == 1024 bytes  as per API docs */
			v->snap_reserved = 1024 * value;
			v->flags |= HAVE_VOLUME_USAGE_SNAP_RSVD;
		}

		sis = na_elem_child(elem_volume, "sis");
		if (sis == NULL)
			continue;

		if (na_elem_child(sis, "sis-info"))
			sis = na_elem_child(sis, "sis-info");
		
		sis_state = na_child_get_string(sis, "state");
		if (sis_state == NULL)
			continue;

		/* If SIS is not enabled, there's nothing left to do for this volume. */
		if (strcmp ("enabled", sis_state) != 0)
			continue;

		sis_saved_reported = na_child_get_uint64(sis, "size-saved", UINT64_MAX);
		if (sis_saved_reported == UINT64_MAX)
			continue;

		/* size-saved is actually a 32 bit number, so ... time for some guesswork. */
		if ((sis_saved_reported >> 32) != 0) {
			/* In case they ever fix this bug. */
			v->sis_saved = sis_saved_reported;
			v->flags |= HAVE_VOLUME_USAGE_SIS_SAVED;
		} else { /* really hacky work-around code. {{{ */
			uint64_t sis_saved_percent;
			uint64_t sis_saved_guess;
			uint64_t overflow_guess;
			uint64_t guess1, guess2, guess3;

			/* Check if we have v->norm_used. Without it, we cannot calculate
			 * sis_saved_guess. */
			if ((v->flags & HAVE_VOLUME_USAGE_NORM_USED) == 0)
				continue;

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
				sis_saved_guess = v->norm_used * sis_saved_percent / (100 - sis_saved_percent);
			else
				sis_saved_guess = v->norm_used;

			overflow_guess = sis_saved_guess >> 32;
			guess1 = overflow_guess ? ((overflow_guess - 1) << 32) + sis_saved_reported : sis_saved_reported;
			guess2 = (overflow_guess << 32) + sis_saved_reported;
			guess3 = ((overflow_guess + 1) << 32) + sis_saved_reported;

			if (sis_saved_guess < guess2) {
				if ((sis_saved_guess - guess1) < (guess2 - sis_saved_guess))
					v->sis_saved = guess1;
				else
					v->sis_saved = guess2;
			} else {
				if ((sis_saved_guess - guess2) < (guess3 - sis_saved_guess))
					v->sis_saved = guess2;
				else
					v->sis_saved = guess3;
			}
			v->flags |= HAVE_VOLUME_USAGE_SIS_SAVED;
		} /* }}} end of 32-bit workaround */
	} /* for (elem_volume) */

	return (cna_submit_volume_usage_data (host->name, cfg_volume, host->interval));
} /* }}} int cna_handle_volume_usage_data */

static int cna_setup_volume_usage (cfg_volume_usage_t *cvu) /* {{{ */
{
	if (cvu == NULL)
		return (EINVAL);

	if (cvu->query != NULL)
		return (0);

	cvu->query = na_elem_new ("volume-list-info");
	if (cvu->query == NULL)
	{
		ERROR ("netapp plugin: na_elem_new failed.");
		return (-1);
	}

	return (0);
} /* }}} int cna_setup_volume_usage */

static int cna_query_volume_usage (host_config_t *host) /* {{{ */
{
	na_elem_t *data;
	int status;
	cdtime_t now;

	if (host == NULL)
		return (EINVAL);

	/* If the user did not configure volume_usage statistics, return without
	 * doing anything. */
	if (host->cfg_volume_usage == NULL)
		return (0);

	now = cdtime ();
	if ((host->cfg_volume_usage->interval.interval + host->cfg_volume_usage->interval.last_read) > now)
		return (0);

	status = cna_setup_volume_usage (host->cfg_volume_usage);
	if (status != 0)
		return (status);
	assert (host->cfg_volume_usage->query != NULL);

	data = na_server_invoke_elem(host->srv, host->cfg_volume_usage->query);
	if (na_results_status (data) != NA_OK)
	{
		ERROR ("netapp plugin: cna_query_volume_usage: na_server_invoke_elem failed for host %s: %s",
				host->name, na_results_reason (data));
		na_elem_free (data);
		return (-1);
	}

	status = cna_handle_volume_usage_data (host, host->cfg_volume_usage, data);

	if (status == 0)
		host->cfg_volume_usage->interval.last_read = now;

	na_elem_free (data);
	return (status);
} /* }}} int cna_query_volume_usage */

/* Data corresponding to <System /> */
static int cna_handle_system_data (const char *hostname, /* {{{ */
		cfg_system_t *cfg_system, na_elem_t *data, int interval)
{
	na_elem_t *instances;
	na_elem_t *counter;
	na_elem_iter_t counter_iter;

	derive_t disk_read = 0, disk_written = 0;
	derive_t net_recv = 0, net_sent = 0;
	derive_t cpu_busy = 0, cpu_total = 0;
	uint32_t counter_flags = 0;

	const char *instance;
	cdtime_t timestamp;
	
	timestamp = cna_child_get_cdtime (data);

	instances = na_elem_child(na_elem_child (data, "instances"), "instance-data");
	if (instances == NULL)
	{
		ERROR ("netapp plugin: cna_handle_system_data: "
				"na_elem_child (\"instances\") failed "
				"for host %s.", hostname);
		return (-1);
	}

	instance = na_child_get_string (instances, "name");
	if (instance == NULL)
	{
		ERROR ("netapp plugin: cna_handle_system_data: "
				"na_child_get_string (\"name\") failed "
				"for host %s.", hostname);
		return (-1);
	}

	counter_iter = na_child_iterator (na_elem_child (instances, "counters"));
	for (counter = na_iterator_next (&counter_iter);
			counter != NULL;
			counter = na_iterator_next (&counter_iter))
	{
		const char *name;
		uint64_t value;

		name = na_child_get_string(counter, "name");
		if (name == NULL)
			continue;

		value = na_child_get_uint64(counter, "value", UINT64_MAX);
		if (value == UINT64_MAX)
			continue;

		if (!strcmp(name, "disk_data_read")) {
			disk_read = (derive_t) (value * 1024);
			counter_flags |= 0x01;
		} else if (!strcmp(name, "disk_data_written")) {
			disk_written = (derive_t) (value * 1024);
			counter_flags |= 0x02;
		} else if (!strcmp(name, "net_data_recv")) {
			net_recv = (derive_t) (value * 1024);
			counter_flags |= 0x04;
		} else if (!strcmp(name, "net_data_sent")) {
			net_sent = (derive_t) (value * 1024);
			counter_flags |= 0x08;
		} else if (!strcmp(name, "cpu_busy")) {
			cpu_busy = (derive_t) value;
			counter_flags |= 0x10;
		} else if (!strcmp(name, "cpu_elapsed_time")) {
			cpu_total = (derive_t) value;
			counter_flags |= 0x20;
		} else if ((cfg_system->flags & CFG_SYSTEM_OPS)
				&& (value > 0) && (strlen(name) > 4)
				&& (!strcmp(name + strlen(name) - 4, "_ops"))) {
			submit_derive (hostname, instance, "disk_ops_complex", name,
					(derive_t) value, timestamp, interval);
		}
	} /* for (counter) */

	if ((cfg_system->flags & CFG_SYSTEM_DISK)
			&& (HAS_ALL_FLAGS (counter_flags, 0x01 | 0x02)))
		submit_two_derive (hostname, instance, "disk_octets", NULL,
				disk_read, disk_written, timestamp, interval);
				
	if ((cfg_system->flags & CFG_SYSTEM_NET)
			&& (HAS_ALL_FLAGS (counter_flags, 0x04 | 0x08)))
		submit_two_derive (hostname, instance, "if_octets", NULL,
				net_recv, net_sent, timestamp, interval);

	if ((cfg_system->flags & CFG_SYSTEM_CPU)
			&& (HAS_ALL_FLAGS (counter_flags, 0x10 | 0x20)))
	{
		submit_derive (hostname, instance, "cpu", "system",
				cpu_busy, timestamp, interval);
		submit_derive (hostname, instance, "cpu", "idle",
				cpu_total - cpu_busy, timestamp, interval);
	}

	return (0);
} /* }}} int cna_handle_system_data */

static int cna_setup_system (cfg_system_t *cs) /* {{{ */
{
	if (cs == NULL)
		return (EINVAL);

	if (cs->query != NULL)
		return (0);

	cs->query = na_elem_new ("perf-object-get-instances");
	if (cs->query == NULL)
	{
		ERROR ("netapp plugin: na_elem_new failed.");
		return (-1);
	}
	na_child_add_string (cs->query, "objectname", "system");

	return (0);
} /* }}} int cna_setup_system */

static int cna_query_system (host_config_t *host) /* {{{ */
{
	na_elem_t *data;
	int status;
	cdtime_t now;

	if (host == NULL)
		return (EINVAL);

	/* If system statistics were not configured, return without doing anything. */
	if (host->cfg_system == NULL)
		return (0);

	now = cdtime ();
	if ((host->cfg_system->interval.interval + host->cfg_system->interval.last_read) > now)
		return (0);

	status = cna_setup_system (host->cfg_system);
	if (status != 0)
		return (status);
	assert (host->cfg_system->query != NULL);

	data = na_server_invoke_elem(host->srv, host->cfg_system->query);
	if (na_results_status (data) != NA_OK)
	{
		ERROR ("netapp plugin: cna_query_system: na_server_invoke_elem failed for host %s: %s",
				host->name, na_results_reason (data));
		na_elem_free (data);
		return (-1);
	}

	status = cna_handle_system_data (host->name, host->cfg_system, data, host->interval);

	if (status == 0)
		host->cfg_system->interval.last_read = now;

	na_elem_free (data);
	return (status);
} /* }}} int cna_query_system */

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

/* Handling of the "Interval" option which is allowed in every block. */
static int cna_config_get_interval (const oconfig_item_t *ci, /* {{{ */
		cna_interval_t *out_interval)
{
	cdtime_t tmp = 0;
	int status;

	status = cf_util_get_cdtime (ci, &tmp);
	if (status != 0)
		return (status);

	out_interval->interval = tmp;
	out_interval->last_read = 0;

	return (0);
} /* }}} int cna_config_get_interval */

/* Handling of the "GetIO", "GetOps" and "GetLatency" options within a
 * <VolumePerf /> block. */
static void cna_config_volume_perf_option (cfg_volume_perf_t *cvp, /* {{{ */
		const oconfig_item_t *ci)
{
	char *name;
	ignorelist_t * il;

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
	{
		WARNING ("netapp plugin: The %s option requires exactly one string argument.",
				ci->key);
		return;
	}

	name = ci->values[0].value.string;

	if (strcasecmp ("GetIO", ci->key) == 0)
		il = cvp->il_octets;
	else if (strcasecmp ("GetOps", ci->key) == 0)
		il = cvp->il_operations;
	else if (strcasecmp ("GetLatency", ci->key) == 0)
		il = cvp->il_latency;
	else
		return;

	ignorelist_add (il, name);
} /* }}} void cna_config_volume_perf_option */

/* Handling of the "IgnoreSelectedIO", "IgnoreSelectedOps" and
 * "IgnoreSelectedLatency" options within a <VolumePerf /> block. */
static void cna_config_volume_perf_default (cfg_volume_perf_t *cvp, /* {{{ */
		const oconfig_item_t *ci)
{
	ignorelist_t *il;

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
	{
		WARNING ("netapp plugin: The %s option requires exactly one string argument.",
				ci->key);
		return;
	}

	if (strcasecmp ("IgnoreSelectedIO", ci->key) == 0)
		il = cvp->il_octets;
	else if (strcasecmp ("IgnoreSelectedOps", ci->key) == 0)
		il = cvp->il_operations;
	else if (strcasecmp ("IgnoreSelectedLatency", ci->key) == 0)
		il = cvp->il_latency;
	else
		return;

	if (ci->values[0].value.boolean)
		ignorelist_set_invert (il, /* invert = */ 0);
	else
		ignorelist_set_invert (il, /* invert = */ 1);
} /* }}} void cna_config_volume_perf_default */

/* Corresponds to a <Disks /> block */
/*
 * <VolumePerf>
 *   GetIO "vol0"
 *   GetIO "vol1"
 *   IgnoreSelectedIO false
 *
 *   GetOps "vol0"
 *   GetOps "vol2"
 *   IgnoreSelectedOps false
 *
 *   GetLatency "vol2"
 *   GetLatency "vol3"
 *   IgnoreSelectedLatency false
 * </VolumePerf>
 */
/* Corresponds to a <VolumePerf /> block */
static int cna_config_volume_performance (host_config_t *host, /* {{{ */
		const oconfig_item_t *ci)
{
	cfg_volume_perf_t *cfg_volume_perf;
	int i;

	if ((host == NULL) || (ci == NULL))
		return (EINVAL);

	if (host->cfg_volume_perf == NULL)
	{
		cfg_volume_perf = malloc (sizeof (*cfg_volume_perf));
		if (cfg_volume_perf == NULL)
			return (ENOMEM);
		memset (cfg_volume_perf, 0, sizeof (*cfg_volume_perf));

		/* Set default flags */
		cfg_volume_perf->query = NULL;
		cfg_volume_perf->volumes = NULL;

		cfg_volume_perf->il_octets = ignorelist_create (/* invert = */ 1);
		if (cfg_volume_perf->il_octets == NULL)
		{
			sfree (cfg_volume_perf);
			return (ENOMEM);
		}

		cfg_volume_perf->il_operations = ignorelist_create (/* invert = */ 1);
		if (cfg_volume_perf->il_operations == NULL)
		{
			ignorelist_free (cfg_volume_perf->il_octets);
			sfree (cfg_volume_perf);
			return (ENOMEM);
		}

		cfg_volume_perf->il_latency = ignorelist_create (/* invert = */ 1);
		if (cfg_volume_perf->il_latency == NULL)
		{
			ignorelist_free (cfg_volume_perf->il_octets);
			ignorelist_free (cfg_volume_perf->il_operations);
			sfree (cfg_volume_perf);
			return (ENOMEM);
		}

		host->cfg_volume_perf = cfg_volume_perf;
	}
	cfg_volume_perf = host->cfg_volume_perf;
	
	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *item = ci->children + i;
		
		/* if (!item || !item->key || !*item->key) continue; */
		if (strcasecmp(item->key, "Interval") == 0)
			cna_config_get_interval (item, &cfg_volume_perf->interval);
		else if (!strcasecmp(item->key, "GetIO"))
			cna_config_volume_perf_option (cfg_volume_perf, item);
		else if (!strcasecmp(item->key, "GetOps"))
			cna_config_volume_perf_option (cfg_volume_perf, item);
		else if (!strcasecmp(item->key, "GetLatency"))
			cna_config_volume_perf_option (cfg_volume_perf, item);
		else if (!strcasecmp(item->key, "IgnoreSelectedIO"))
			cna_config_volume_perf_default (cfg_volume_perf, item);
		else if (!strcasecmp(item->key, "IgnoreSelectedOps"))
			cna_config_volume_perf_default (cfg_volume_perf, item);
		else if (!strcasecmp(item->key, "IgnoreSelectedLatency"))
			cna_config_volume_perf_default (cfg_volume_perf, item);
		else
			WARNING ("netapp plugin: The option %s is not allowed within "
					"`VolumePerf' blocks.", item->key);
	}

	return (0);
} /* }}} int cna_config_volume_performance */

/* Handling of the "GetCapacity" and "GetSnapshot" options within a
 * <VolumeUsage /> block. */
static void cna_config_volume_usage_option (cfg_volume_usage_t *cvu, /* {{{ */
		const oconfig_item_t *ci)
{
	char *name;
	ignorelist_t * il;

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
	{
		WARNING ("netapp plugin: The %s option requires exactly one string argument.",
				ci->key);
		return;
	}

	name = ci->values[0].value.string;

	if (strcasecmp ("GetCapacity", ci->key) == 0)
		il = cvu->il_capacity;
	else if (strcasecmp ("GetSnapshot", ci->key) == 0)
		il = cvu->il_snapshot;
	else
		return;

	ignorelist_add (il, name);
} /* }}} void cna_config_volume_usage_option */

/* Handling of the "IgnoreSelectedCapacity" and "IgnoreSelectedSnapshot"
 * options within a <VolumeUsage /> block. */
static void cna_config_volume_usage_default (cfg_volume_usage_t *cvu, /* {{{ */
		const oconfig_item_t *ci)
{
	ignorelist_t *il;

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
	{
		WARNING ("netapp plugin: The %s option requires exactly one string argument.",
				ci->key);
		return;
	}

	if (strcasecmp ("IgnoreSelectedCapacity", ci->key) == 0)
		il = cvu->il_capacity;
	else if (strcasecmp ("IgnoreSelectedSnapshot", ci->key) == 0)
		il = cvu->il_snapshot;
	else
		return;

	if (ci->values[0].value.boolean)
		ignorelist_set_invert (il, /* invert = */ 0);
	else
		ignorelist_set_invert (il, /* invert = */ 1);
} /* }}} void cna_config_volume_usage_default */

/* Corresponds to a <Disks /> block */
static int cna_config_disk(host_config_t *host, oconfig_item_t *ci) { /* {{{ */
	cfg_disk_t *cfg_disk;
	int i;

	if ((host == NULL) || (ci == NULL))
		return (EINVAL);

	if (host->cfg_disk == NULL)
	{
		cfg_disk = malloc (sizeof (*cfg_disk));
		if (cfg_disk == NULL)
			return (ENOMEM);
		memset (cfg_disk, 0, sizeof (*cfg_disk));

		/* Set default flags */
		cfg_disk->flags = CFG_DISK_ALL;
		cfg_disk->query = NULL;
		cfg_disk->disks = NULL;

		host->cfg_disk = cfg_disk;
	}
	cfg_disk = host->cfg_disk;
	
	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *item = ci->children + i;
		
		/* if (!item || !item->key || !*item->key) continue; */
		if (strcasecmp(item->key, "Interval") == 0)
			cna_config_get_interval (item, &cfg_disk->interval);
		else if (strcasecmp(item->key, "GetBusy") == 0)
			cna_config_bool_to_flag (item, &cfg_disk->flags, CFG_DISK_BUSIEST);
	}

	if ((cfg_disk->flags & CFG_DISK_ALL) == 0)
	{
		NOTICE ("netapp plugin: All disk related values have been disabled. "
				"Collection of per-disk data will be disabled entirely.");
		free_cfg_disk (host->cfg_disk);
		host->cfg_disk = NULL;
	}

	return (0);
} /* }}} int cna_config_disk */

/* Corresponds to a <WAFL /> block */
static int cna_config_wafl(host_config_t *host, oconfig_item_t *ci) /* {{{ */
{
	cfg_wafl_t *cfg_wafl;
	int i;

	if ((host == NULL) || (ci == NULL))
		return (EINVAL);

	if (host->cfg_wafl == NULL)
	{
		cfg_wafl = malloc (sizeof (*cfg_wafl));
		if (cfg_wafl == NULL)
			return (ENOMEM);
		memset (cfg_wafl, 0, sizeof (*cfg_wafl));

		/* Set default flags */
		cfg_wafl->flags = CFG_WAFL_ALL;

		host->cfg_wafl = cfg_wafl;
	}
	cfg_wafl = host->cfg_wafl;

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *item = ci->children + i;
		
		if (strcasecmp(item->key, "Interval") == 0)
			cna_config_get_interval (item, &cfg_wafl->interval);
		else if (!strcasecmp(item->key, "GetNameCache"))
			cna_config_bool_to_flag (item, &cfg_wafl->flags, CFG_WAFL_NAME_CACHE);
		else if (!strcasecmp(item->key, "GetDirCache"))
			cna_config_bool_to_flag (item, &cfg_wafl->flags, CFG_WAFL_DIR_CACHE);
		else if (!strcasecmp(item->key, "GetBufferCache"))
			cna_config_bool_to_flag (item, &cfg_wafl->flags, CFG_WAFL_BUF_CACHE);
		else if (!strcasecmp(item->key, "GetInodeCache"))
			cna_config_bool_to_flag (item, &cfg_wafl->flags, CFG_WAFL_INODE_CACHE);
		else
			WARNING ("netapp plugin: The %s config option is not allowed within "
					"`WAFL' blocks.", item->key);
	}

	if ((cfg_wafl->flags & CFG_WAFL_ALL) == 0)
	{
		NOTICE ("netapp plugin: All WAFL related values have been disabled. "
				"Collection of WAFL data will be disabled entirely.");
		free_cfg_wafl (host->cfg_wafl);
		host->cfg_wafl = NULL;
	}

	return (0);
} /* }}} int cna_config_wafl */

/*
 * <VolumeUsage>
 *   GetCapacity "vol0"
 *   GetCapacity "vol1"
 *   GetCapacity "vol2"
 *   GetCapacity "vol3"
 *   GetCapacity "vol4"
 *   IgnoreSelectedCapacity false
 *
 *   GetSnapshot "vol0"
 *   GetSnapshot "vol3"
 *   GetSnapshot "vol4"
 *   GetSnapshot "vol7"
 *   IgnoreSelectedSnapshot false
 * </VolumeUsage>
 */
/* Corresponds to a <VolumeUsage /> block */
static int cna_config_volume_usage(host_config_t *host, /* {{{ */
		const oconfig_item_t *ci)
{
	cfg_volume_usage_t *cfg_volume_usage;
	int i;

	if ((host == NULL) || (ci == NULL))
		return (EINVAL);

	if (host->cfg_volume_usage == NULL)
	{
		cfg_volume_usage = malloc (sizeof (*cfg_volume_usage));
		if (cfg_volume_usage == NULL)
			return (ENOMEM);
		memset (cfg_volume_usage, 0, sizeof (*cfg_volume_usage));

		/* Set default flags */
		cfg_volume_usage->query = NULL;
		cfg_volume_usage->volumes = NULL;

		cfg_volume_usage->il_capacity = ignorelist_create (/* invert = */ 1);
		if (cfg_volume_usage->il_capacity == NULL)
		{
			sfree (cfg_volume_usage);
			return (ENOMEM);
		}

		cfg_volume_usage->il_snapshot = ignorelist_create (/* invert = */ 1);
		if (cfg_volume_usage->il_snapshot == NULL)
		{
			ignorelist_free (cfg_volume_usage->il_capacity);
			sfree (cfg_volume_usage);
			return (ENOMEM);
		}

		host->cfg_volume_usage = cfg_volume_usage;
	}
	cfg_volume_usage = host->cfg_volume_usage;
	
	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *item = ci->children + i;
		
		/* if (!item || !item->key || !*item->key) continue; */
		if (strcasecmp(item->key, "Interval") == 0)
			cna_config_get_interval (item, &cfg_volume_usage->interval);
		else if (!strcasecmp(item->key, "GetCapacity"))
			cna_config_volume_usage_option (cfg_volume_usage, item);
		else if (!strcasecmp(item->key, "GetSnapshot"))
			cna_config_volume_usage_option (cfg_volume_usage, item);
		else if (!strcasecmp(item->key, "IgnoreSelectedCapacity"))
			cna_config_volume_usage_default (cfg_volume_usage, item);
		else if (!strcasecmp(item->key, "IgnoreSelectedSnapshot"))
			cna_config_volume_usage_default (cfg_volume_usage, item);
		else
			WARNING ("netapp plugin: The option %s is not allowed within "
					"`VolumeUsage' blocks.", item->key);
	}

	return (0);
} /* }}} int cna_config_volume_usage */

/* Corresponds to a <System /> block */
static int cna_config_system (host_config_t *host, /* {{{ */
		oconfig_item_t *ci)
{
	cfg_system_t *cfg_system;
	int i;
	
	if ((host == NULL) || (ci == NULL))
		return (EINVAL);

	if (host->cfg_system == NULL)
	{
		cfg_system = malloc (sizeof (*cfg_system));
		if (cfg_system == NULL)
			return (ENOMEM);
		memset (cfg_system, 0, sizeof (*cfg_system));

		/* Set default flags */
		cfg_system->flags = CFG_SYSTEM_ALL;
		cfg_system->query = NULL;

		host->cfg_system = cfg_system;
	}
	cfg_system = host->cfg_system;

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *item = ci->children + i;

		if (strcasecmp(item->key, "Interval") == 0) {
			cna_config_get_interval (item, &cfg_system->interval);
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
					"`System' blocks.", item->key);
		}
	}

	if ((cfg_system->flags & CFG_SYSTEM_ALL) == 0)
	{
		NOTICE ("netapp plugin: All system related values have been disabled. "
				"Collection of system data will be disabled entirely.");
		free_cfg_system (host->cfg_system);
		host->cfg_system = NULL;
	}

	return (0);
} /* }}} int cna_config_system */

/* Corresponds to a <Host /> block. */
static host_config_t *cna_config_host (const oconfig_item_t *ci) /* {{{ */
{
	oconfig_item_t *item;
	host_config_t *host;
	int status;
	int i;
	
	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
		WARNING("netapp plugin: \"Host\" needs exactly one string argument. Ignoring host block.");
		return 0;
	}

	host = malloc(sizeof(*host));
	memset (host, 0, sizeof (*host));
	host->name = NULL;
	host->protocol = NA_SERVER_TRANSPORT_HTTPS;
	host->host = NULL;
	host->username = NULL;
	host->password = NULL;
	host->srv = NULL;
	host->cfg_wafl = NULL;
	host->cfg_disk = NULL;
	host->cfg_volume_perf = NULL;
	host->cfg_volume_usage = NULL;
	host->cfg_system = NULL;

	status = cf_util_get_string (ci, &host->name);
	if (status != 0)
	{
		sfree (host);
		return (NULL);
	}

	for (i = 0; i < ci->children_num; ++i) {
		item = ci->children + i;

		status = 0;

		if (!strcasecmp(item->key, "Address")) {
			status = cf_util_get_string (item, &host->host);
		} else if (!strcasecmp(item->key, "Port")) {
			int tmp;

			tmp = cf_util_get_port_number (item);
			if (tmp > 0)
				host->port = tmp;
		} else if (!strcasecmp(item->key, "Protocol")) {
			if ((item->values_num != 1) || (item->values[0].type != OCONFIG_TYPE_STRING) || (strcasecmp(item->values[0].value.string, "http") && strcasecmp(item->values[0].value.string, "https"))) {
				WARNING("netapp plugin: \"Protocol\" needs to be either \"http\" or \"https\". Ignoring host block \"%s\".", ci->values[0].value.string);
				return 0;
			}
			if (!strcasecmp(item->values[0].value.string, "http")) host->protocol = NA_SERVER_TRANSPORT_HTTP;
			else host->protocol = NA_SERVER_TRANSPORT_HTTPS;
		} else if (!strcasecmp(item->key, "User")) {
			status = cf_util_get_string (item, &host->username);
		} else if (!strcasecmp(item->key, "Password")) {
			status = cf_util_get_string (item, &host->password);
		} else if (!strcasecmp(item->key, "Interval")) {
			status = cf_util_get_cdtime (item, &host->interval);
		} else if (!strcasecmp(item->key, "WAFL")) {
			cna_config_wafl(host, item);
		} else if (!strcasecmp(item->key, "Disks")) {
			cna_config_disk(host, item);
		} else if (!strcasecmp(item->key, "VolumePerf")) {
			cna_config_volume_performance(host, item);
		} else if (!strcasecmp(item->key, "VolumeUsage")) {
			cna_config_volume_usage(host, item);
		} else if (!strcasecmp(item->key, "System")) {
			cna_config_system(host, item);
		} else {
			WARNING("netapp plugin: Ignoring unknown config option \"%s\" in host block \"%s\".",
					item->key, ci->values[0].value.string);
		}

		if (status != 0)
			break;
	}

	if (host->host == NULL)
		host->host = strdup (host->name);

	if (host->host == NULL)
		status = -1;

	if (host->port <= 0)
		host->port = (host->protocol == NA_SERVER_TRANSPORT_HTTP) ? 80 : 443;

	if ((host->username == NULL) || (host->password == NULL)) {
		WARNING("netapp plugin: Please supply login information for host \"%s\". "
				"Ignoring host block.", host->name);
		status = -1;
	}

	if (status != 0)
	{
		free_host_config (host);
		return (NULL);
	}

	return host;
} /* }}} host_config_t *cna_config_host */

/*
 * Callbacks registered with the daemon
 *
 * Pretty standard stuff here.
 */
static int cna_init_host (host_config_t *host) /* {{{ */
{
	if (host == NULL)
		return (EINVAL);

	if (host->srv != NULL)
		return (0);

	/* Request version 1.1 of the ONTAP API */
	host->srv = na_server_open(host->host,
			/* major version = */ 1, /* minor version = */ 1); 
	if (host->srv == NULL) {
		ERROR ("netapp plugin: na_server_open (%s) failed.", host->host);
		return (-1);
	}

	na_server_set_transport_type(host->srv, host->protocol,
			/* transportarg = */ NULL);
	na_server_set_port(host->srv, host->port);
	na_server_style(host->srv, NA_STYLE_LOGIN_PASSWORD);
	na_server_adminuser(host->srv, host->username, host->password);
	na_server_set_timeout(host->srv, 5 /* seconds */);

	return 0;
} /* }}} int cna_init_host */

static int cna_init (void) /* {{{ */
{
	char err[256];

	memset (err, 0, sizeof (err));
	if (!na_startup(err, sizeof(err))) {
		err[sizeof (err) - 1] = 0;
		ERROR("netapp plugin: Error initializing netapp API: %s", err);
		return 1;
	}

	return (0);
} /* }}} cna_init */

static int cna_read (user_data_t *ud) { /* {{{ */
	host_config_t *host;
	int status;

	if ((ud == NULL) || (ud->data == NULL))
		return (-1);

	host = ud->data;

	status = cna_init_host (host);
	if (status != 0)
		return (status);
	
	cna_query_wafl (host);
	cna_query_disk (host);
	cna_query_volume_perf (host);
	cna_query_volume_usage (host);
	cna_query_system (host);

	return 0;
} /* }}} int cna_read */

static int cna_config (oconfig_item_t *ci) { /* {{{ */
	int i;
	oconfig_item_t *item;

	for (i = 0; i < ci->children_num; ++i) {
		item = ci->children + i;

		if (strcasecmp(item->key, "Host") == 0)
		{
			host_config_t *host;
			char cb_name[256];
			struct timespec interval;
			user_data_t ud;

			host = cna_config_host (item);
			if (host == NULL)
				continue;

			ssnprintf (cb_name, sizeof (cb_name), "netapp-%s", host->name);

			CDTIME_T_TO_TIMESPEC (host->interval, &interval);

			memset (&ud, 0, sizeof (ud));
			ud.data = host;
			ud.free_func = (void (*) (void *)) free_host_config;

			plugin_register_complex_read (/* group = */ NULL, cb_name,
					/* callback  = */ cna_read, 
					/* interval  = */ (host->interval > 0) ? &interval : NULL,
					/* user data = */ &ud);
			continue;
		}
		else /* if (item->key != "Host") */
		{
			WARNING("netapp plugin: Ignoring unknown config option \"%s\".", item->key);
		}
	}
	return 0;
} /* }}} int cna_config */

static int cna_shutdown (void) /* {{{ */
{
	/* Clean up system resources and stuff. */
	na_shutdown ();

	return (0);
} /* }}} int cna_shutdown */

void module_register(void) {
	plugin_register_complex_config("netapp", cna_config);
	plugin_register_init("netapp", cna_init);
	plugin_register_shutdown("netapp", cna_shutdown);
}

/* vim: set sw=2 ts=2 noet fdm=marker : */
