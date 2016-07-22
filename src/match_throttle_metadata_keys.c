/**
 * collectd - src/match_throttle_gcm.c
 * Copyright 2010 Google Inc. All Rights Reserved.
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
 *   Corey Kosak <kosak at google.com>
 **/

/*
 * This module establishes a global cache which tracks estimated memory usage
 * of the Google Cloud Monitoring server, and optionally throttles points when
 * usage exceeds a certain value. The estimated memory usage is based on the
 * string length of various keys (namely, host, plugin, plugin_instance, type,
 * type_instance, data_source_name, and whatever metadata keys are specified
 * in the plugin configuration).
 */

#include <pthread.h>
#include <sys/types.h>
#include <stdlib.h>
#include <strings.h>
#include <memory.h>

#include "collectd.h"
#include "common.h"
#include "daemon/utils_cache.h"
#include "filter_chain.h"
#include "liboconfig/oconfig.h"
#include "stackdriver-agent-keys.h"
#include "utils_avltree.h"

static char this_plugin_name[] = "match_throttle_metadata_keys";

typedef struct mtg_key_history_s
{
    struct mtg_key_history_s *next;

    time_t creation_time;
    time_t last_append_time;
    int num_hashes;
    uint32_t hashes[1024];
} mtg_key_history_t;

typedef struct
{
  // Mode: whether throttling is on right now.
  _Bool is_throttling;
  // Estimated amount of memory in use at the server (in bytes).
  size_t server_memory_in_use;
  // All keys sent in the past 24 hours.
  struct mtg_key_history_s *key_history_head;
  struct mtg_key_history_s *key_history_tail;
  // map<hash code, hash_count_value_t>
  c_avl_tree_t *hash_counts;

  // Certain diagnostic counters.
  uint32_t num_key_history_entries;
  uint32_t num_distinct_keys;

  // Configuration parameters:

  // When 'server_memory_in_use' is less than this value, throttling is turned
  // off.
  int low_water_mark_bytes;

  // When 'server_memory_in_use' is greater than this value, throttling is
  // turned on.
  int high_water_mark_bytes;

  // How long to keep adding hashes to the same chunk before making a new chunk
  // (typically 1/2 hour).
  int chunk_interval_secs;

  // How long to keep wg_key_history_s chunks before purging them (typically
  // 24 hours).
  int purge_interval_secs;
} mtg_key_tracker_t;

typedef struct
{
  uint32_t count;
  uint32_t memory_impact;
} mtg_hash_count_value_t;

typedef struct {
    _Bool ok_to_throttle;
    char **tracked_metadata_keys;
    int num_tracked_metadata_keys;
    pthread_mutex_t *mutex;
    // Protected by *mutex.
    mtg_key_tracker_t *key_tracker;
} mtg_context_t;

static int wg_compare_uint32_t(const void *lhs, const void *rhs)
{
    uint32_t *l = (uint32_t*)lhs;
    uint32_t *r = (uint32_t*)rhs;
    if (*l < *r)
    {
        return -1;
    }
    if (*l > *r)
    {
        return 1;
    }
    return 0;
}

static mtg_key_history_t *mtg_key_history_create (time_t now)
{
    mtg_key_history_t *result = calloc(1, sizeof(*result));
    if (result == NULL) {
        ERROR("%s: wg_key_history_create: calloc failed", this_plugin_name);
        return NULL;
    }
    result->creation_time = now;
    return result;
}

static void mtg_key_history_destroy(mtg_key_history_t *item) {
    sfree(item);
}

static void mtg_context_destroy (mtg_context_t *ctx)
{
    int i;
    if (ctx == NULL)
    {
        return;
    }
    if (ctx->tracked_metadata_keys != NULL)
    {
        for (i = 0; i < ctx->num_tracked_metadata_keys; ++i)
        {
            sfree (ctx->tracked_metadata_keys[i]);
        }
        sfree (ctx->tracked_metadata_keys);
    }
    sfree (ctx);
}

static mtg_context_t *mtg_context_create (int num_metadata_keys) {
    // Singleton mutex that protects the singleton key tracker.
    static pthread_mutex_t the_mutex = PTHREAD_MUTEX_INITIALIZER;
    // The singleton key tracker, with some defaults
    static mtg_key_tracker_t the_key_tracker =
    {
        .low_water_mark_bytes = 800000000,  // 800M
        .high_water_mark_bytes = 950000000,  // 950M
        .chunk_interval_secs = 30 * 60,  // 30 minutes
        .purge_interval_secs = 24 * 60 * 60  // 24 hours
    };

    mtg_context_t *ctx = NULL;

    // One-time initialization of the singleton key_tracker.
    pthread_mutex_lock(&the_mutex);
    if (the_key_tracker.hash_counts == NULL)  // First time?
    {
        mtg_key_tracker_t *kt = &the_key_tracker;  // alias
        kt->hash_counts = c_avl_create(&wg_compare_uint32_t);
        if (kt->hash_counts == NULL)
        {
            pthread_mutex_unlock(&the_mutex);
            ERROR("%s: c_avl_create failed", this_plugin_name);
            return NULL;
        }
    }
    pthread_mutex_unlock(&the_mutex);

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
    {
        ERROR("%s: mtg_context_create: calloc (ctx) failed", this_plugin_name);
        goto error;
    }
    ctx->mutex = &the_mutex;
    ctx->key_tracker = &the_key_tracker;

    ctx->tracked_metadata_keys = calloc(num_metadata_keys,
                                        sizeof(*ctx->tracked_metadata_keys));
    if (ctx->tracked_metadata_keys == NULL) {
        ERROR("%s: mtg_context_create: calloc (keys) failed", this_plugin_name);
        goto error;
    }
    ctx->num_tracked_metadata_keys = num_metadata_keys;

    return ctx;

  error:
    mtg_context_destroy(ctx);
    return NULL;
}

static int mtg_create (const oconfig_item_t *ci, void **user_data)
{
    mtg_context_t *ctx = NULL;
    int parse_errors = 0;
    int i;
    int num_metadata_keys;

    // Make a first pass over the config to count the number of TrackedMetadata
    // keys.
    num_metadata_keys = 0;
    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;
        if (strcasecmp ("TrackedMetadata", child->key) == 0)
        {
            ++num_metadata_keys;
        }
    }
    ctx = mtg_context_create(num_metadata_keys);
    if (ctx == NULL)
    {
        ERROR("%s: mtg_context_create failed", this_plugin_name);
        goto error;
    }

    // Make a second pass over the config to actually pull out the data.
    num_metadata_keys = 0;
    for (i = 0; i < ci->children_num; i++)
    {
        oconfig_item_t *child = ci->children + i;

        if (strcasecmp ("OKToThrottle", child->key) == 0)
        {
            if (cf_util_get_boolean(child, &ctx->ok_to_throttle) != 0)
            {
              ERROR("%s: cf_util_get_boolean failed for key %s",
                    this_plugin_name, child->key);
              ++parse_errors;
            }
        }
        else if (strcasecmp ("TrackedMetadata", child->key) == 0)
        {
            assert(num_metadata_keys < ctx->num_tracked_metadata_keys);
            if (cf_util_get_string(child,
                    &ctx->tracked_metadata_keys[num_metadata_keys++]) != 0)
            {
                ERROR("%s: cf_util_get_string failed for key %s",
                      this_plugin_name, child->key);
                ++parse_errors;
            }
        }
        else
        {
            int k;
            mtg_key_tracker_t *kt = ctx->key_tracker;  // Alias
            static const char *int_keys[4] =
            {
                "LowWaterMark",
                "HighWaterMark",
                "ChunkInterval",
                "PurgeInterval",
            };
            int *int_locations[] =
            {
                &kt->low_water_mark_bytes,
                &kt->high_water_mark_bytes,
                &kt->chunk_interval_secs,
                &kt->purge_interval_secs,
            };
            assert (STATIC_ARRAY_SIZE(int_keys) ==
                    STATIC_ARRAY_SIZE(int_locations));
            for (k = 0; k < STATIC_ARRAY_SIZE(int_keys); ++k)
            {
                if (strcasecmp (int_keys[k], child->key) == 0)
                {
                    if (cf_util_get_int(child, int_locations[k]) != 0) {
                      ERROR("%s: cf_util_get_int failed for key %s",
                            this_plugin_name, child->key);
                      ++parse_errors;
                    }
                    break;
                }
            }
            if (k < STATIC_ARRAY_SIZE(int_keys))
            {
                continue;
            }
            ERROR("%s: Unknown configuration option %s",
                  this_plugin_name, child->key);
            ++parse_errors;
        }
    }
    assert(num_metadata_keys == ctx->num_tracked_metadata_keys);
    if (parse_errors > 0) {
        ERROR("%s: There were %d errors reading the configuration",
              this_plugin_name, parse_errors);
        goto error;
    }

    // Success!
    *user_data = ctx;
    return 0;

  error:
    mtg_context_destroy(ctx);
    return -1;
}

static int mtg_destroy (void **user_data)
{
    mtg_context_destroy(*user_data);
    return 0;
}

// Austin Appleby's MurmurHash3, retrieved from Wikipedia on December 22, 2015.
// https://en.wikipedia.org/wiki/MurmurHash
//
// This version requires that your processor be tolerant of unaligned reads.
#define ROT32(x, y) ((x << y) | (x >> (32 - y))) // avoid effor
static uint32_t mtg_murmur3_32(const char *key, uint32_t len, uint32_t seed) {
  static const uint32_t c1 = 0xcc9e2d51;
  static const uint32_t c2 = 0x1b873593;
  static const uint32_t r1 = 15;
  static const uint32_t r2 = 13;
  static const uint32_t m = 5;
  static const uint32_t n = 0xe6546b64;

  uint32_t hash = seed;

  const int nblocks = len / 4;
  const uint32_t *blocks = (const uint32_t *) key;
  int i;
  uint32_t k;
  for (i = 0; i < nblocks; i++)
  {
    k = blocks[i];
    k *= c1;
    k = ROT32(k, r1);
    k *= c2;

    hash ^= k;
    hash = ROT32(hash, r2) * m + n;
  }

  const uint8_t *tail = (const uint8_t *) (key + nblocks * 4);
  uint32_t k1 = 0;

  switch (len & 3)
  {
    case 3:
      k1 ^= tail[2] << 16;
    case 2:
      k1 ^= tail[1] << 8;
    case 1:
      k1 ^= tail[0];

      k1 *= c1;
      k1 = ROT32(k1, r1);
      k1 *= c2;
      hash ^= k1;
  }

  hash ^= len;
  hash ^= (hash >> 16);
  hash *= 0x85ebca6b;
  hash ^= (hash >> 13);
  hash *= 0xc2b2ae35;
  hash ^= (hash >> 16);

  return hash;
}
#undef ROT32

static int mtg_lookup_or_create_hash_count_entry(mtg_key_tracker_t *tracker,
        uint32_t hash, mtg_hash_count_value_t **hc_value)
{
    // Items to clean up on exit.
    uint32_t *new_hash = NULL;
    mtg_hash_count_value_t *new_value = NULL;

    if (c_avl_get(tracker->hash_counts, &hash, (void**)hc_value) == 0)
    {
        // Existing value found!
        return 0;
    }
    new_hash = calloc(1, sizeof(*new_hash));
    new_value = calloc(1, sizeof(*new_value));
    if (new_hash == NULL || new_value == NULL)
    {
        ERROR("%s: mtg_lookup_or_create_hash_count_entry: calloc failed",
              this_plugin_name);
        goto error;
    }
    *new_hash = hash;

    if (c_avl_insert(tracker->hash_counts, new_hash, new_value) != 0)
    {
        ERROR("%s: mtg_lookup_or_create_hash_count_entry: c_avl_insert failed.",
              this_plugin_name);
        goto error;
    }

    // Successfully inserted!
    *hc_value = new_value;
    return 0;

  error:
    sfree(new_value);
    sfree(new_hash);
    return -1;
}

static int mtg_destroy_hash_count_entry(mtg_key_tracker_t *tracker,
        uint32_t hash)
{
    uint32_t *tree_key;
    mtg_hash_count_value_t *tree_value;
    if (c_avl_remove(tracker->hash_counts, &hash, (void**)&tree_key,
                     (void**)&tree_value) != 0)
    {
        ERROR("%s: wg_destroy_hash_count_entry: c_avl_remove failed",
              this_plugin_name);
        return -1;
    }
    sfree(tree_value);
    sfree(tree_key);
    return 0;
}

static int mtg_compute_hash_code_and_memory_impact(const value_list_t *vl,
        const mtg_context_t *ctx, uint32_t seed,
        uint32_t *hash_code, uint32_t *memory_impact)
{
  int host_len = strlen(vl->host);
  int plugin_len = strlen(vl->plugin);
  int plugin_instance_len = strlen(vl->plugin_instance);
  int type_len = strlen(vl->type);
  int type_instance_len = strlen(vl->type_instance);
  int i;

  *hash_code = seed;
  *memory_impact = 0;

  *hash_code = mtg_murmur3_32(vl->host, host_len, *hash_code);
  *hash_code = mtg_murmur3_32(vl->plugin, plugin_len, *hash_code);
  *hash_code = mtg_murmur3_32(vl->plugin_instance, plugin_instance_len,
                              *hash_code);
  *hash_code = mtg_murmur3_32(vl->type, type_len, *hash_code);
  *hash_code = mtg_murmur3_32(vl->type_instance, type_instance_len, *hash_code);

  *memory_impact += host_len;
  *memory_impact += plugin_len;
  *memory_impact += plugin_instance_len;
  *memory_impact += type_len;
  *memory_impact += type_instance_len;

  // Also hash on the values of certain items in the metadata collection.
  if (vl->meta == NULL) {
      return 0;
  }
  for (i = 0; i < ctx->num_tracked_metadata_keys; ++i)
  {
      const char *key = ctx->tracked_metadata_keys[i];
      int type = meta_data_type(vl->meta, key);
      char *value;
      int value_len;

      if (type != MD_TYPE_STRING)
      {
          // Key not found, or key found but value not of type string.
          continue;
      }
      if (meta_data_get_string(vl->meta, key, &value) != 0)
      {
          ERROR("%s: error calling meta_data_get_string", this_plugin_name);
          return -1;
      }
      value_len = strlen(value);
      *hash_code = mtg_murmur3_32(value, value_len, *hash_code);
      *memory_impact += value_len;
      sfree(value);
  }
  return 0;
}

static int mtg_update_stats(size_t server_memory_in_use, _Bool is_throttling)
{
    data_set_t ds = {};  // zero-fill
    value_list_t vl = {
        .plugin = "stackdriver_agent",
        .time = cdtime()
    };
    if (uc_update(&ds, &vl) != 0)
    {
        ERROR("%s: uc_update returned an error", this_plugin_name);
        return -1;
    }
    // The corresponding uc_meta_data_set calls are in stackdriver_agent.c.
    // The key names (between uc_get and uc_set) must be kept in sync.
    if (uc_meta_data_add_unsigned_int(&vl, SAGT_STREAMSPACE_SIZE, server_memory_in_use) != 0 ||
            uc_meta_data_add_boolean(&vl, SAGT_STREAMSPACE_SIZE_THROTTLING, is_throttling) != 0)
    {
        ERROR("%s: uc_meta_data_add returned an error", this_plugin_name);
        return -1;
    }
    return 0;
}

static int mtg_retire_old_entries(mtg_key_tracker_t *tracker, cdtime_t now)
{
    // Trim the key history (removing entries older than 'purge_time')
    cdtime_t purge_time = now - TIME_T_TO_CDTIME_T(tracker->purge_interval_secs);

    while (tracker->key_history_head != NULL &&
            tracker->key_history_head->last_append_time < purge_time)
    {
        mtg_key_history_t *kh = tracker->key_history_head;
        int i;
        for (i = 0; i < kh->num_hashes; ++i)
        {
            uint32_t hash = kh->hashes[i];
            mtg_hash_count_value_t *hc_value;
            if (mtg_lookup_or_create_hash_count_entry(tracker, hash, &hc_value)
                    != 0)
            {
                ERROR("%s: mtg_retire_old_entries: "
                      "wg_lookup_or_create_hash_count_value failed.",
                      this_plugin_name);
                return -1;
            }
            if (hc_value->count == 0)
            {
                // We called "lookup_or_create" (there is no plain "lookup"),
                // which will create the item (with a count of 0) if it was not
                // found. This is something that should never happen. Since this
                // is impossible, I'm not going to worry too much about the
                // consequences of creating an entry that should have been there
                // in the first place.
                ERROR("%s: Impossible: failed to find existing hash entry.",
                      this_plugin_name);
                return -1;
            }
            --hc_value->count;
            if (hc_value->count == 0)
            {
                // The last instance! We get to delete it and reduce our
                // estimate of server memory impact.
                tracker->server_memory_in_use -= hc_value->memory_impact;
                if (mtg_destroy_hash_count_entry(tracker, hash) != 0)
                {
                    ERROR("write_gcm: wg_destroy_hash_count_entry failed");
                    return -1;
                }
                --tracker->num_distinct_keys;
                hc_value = NULL;  // Hygeine.
            }
        }
        // Pop head.
        tracker->key_history_head = kh->next;
        kh->next = NULL;
        if (tracker->key_history_head == NULL)
        {
            tracker->key_history_tail = NULL;
        }
        mtg_key_history_destroy(kh);
        --tracker->num_key_history_entries;
    }
    return 0;
}

static int mtg_add_new_entries(const mtg_context_t *ctx, cdtime_t now,
        const value_list_t *vl)
{
    uint32_t hash_code;
    uint32_t memory_impact;
    mtg_hash_count_value_t *hc_value;
    mtg_key_tracker_t *tracker = ctx->key_tracker;  // Alias.

    if (mtg_compute_hash_code_and_memory_impact(
            vl, ctx, 0, &hash_code, &memory_impact) != 0)
    {
        ERROR("%s: mtg_compute_hash_code failed", this_plugin_name);
        return -1;
    }
    if (mtg_lookup_or_create_hash_count_entry(tracker, hash_code,
            &hc_value) != 0)
    {
        ERROR("%s: mtg_add_new_entries: "
              "mtg_lookup_or_create_hash_count_entry failed.",
              this_plugin_name);
        return -1;
    }
    if (hc_value->count == 0) {
        // New entry!
        hc_value->memory_impact = memory_impact;
        tracker->server_memory_in_use += memory_impact;
        ++tracker->num_distinct_keys;
    }
    ++hc_value->count;

    // Update history. We will make a new history node if any of the following
    // holds:
    // 1. There is no current history node.
    // 2. The current history node is full.
    // 3. The current history node was created prior to 'chunk_time'.
    cdtime_t chunk_time = now -
            TIME_T_TO_CDTIME_T(tracker->chunk_interval_secs);
    mtg_key_history_t *tail = tracker->key_history_tail;  // alias
    if (tail == NULL ||
        tail->num_hashes == STATIC_ARRAY_SIZE(tail->hashes) ||
        tail->creation_time < chunk_time)
    {
        mtg_key_history_t *new = mtg_key_history_create(now);
        if (new == NULL)
        {
            ERROR("%s: mtg_key_history_create failed", this_plugin_name);
            return -1;
        }
        if (tail == NULL)
        {
            tracker->key_history_head = new;
        }
        else
        {
            tail->next = new;
        }
        tracker->key_history_tail = new;
        tail = new;  // Keep our alias up to date.
        ++tracker->num_key_history_entries;

        INFO("%s: %u history entries, %u distinct keys,"
             " %zd bytes server memory.",
             this_plugin_name, tracker->num_key_history_entries,
             tracker->num_distinct_keys, tracker->server_memory_in_use);
    }
    tail->last_append_time = now;
    tail->hashes[tail->num_hashes++] = hash_code;
    return 0;
}

// Performs the following steps:
// 1. Update our estimate of memory usage by cleaning out stale entries.
// 2. Compare estimated memory usage to low and high water marks to decide
//    whether or not we're in a throttling scenario.
// 3. If we're in the throttling scenario, and the match configuration says
//    the point is filterable, then filter it (returning FC_MATCH_NO_MATCH).
// 4. Otherwise (if we're not in the throttling scenario, or the point is
//    not filterable), then update the estimate of memory usage with this
//    point and return FC_MATCH_MATCHES.
static int mtg_match_helper (const value_list_t *vl, mtg_context_t *context)
{
    cdtime_t now = cdtime();
    mtg_key_tracker_t *tracker = context->key_tracker;  // Alias.
    if (mtg_retire_old_entries(tracker, now) != 0)
    {
        ERROR("%s: mtg_retire_old_entries failed.", this_plugin_name);
        return -1;
    }

    if (tracker->is_throttling)
    {
        if (tracker->server_memory_in_use < tracker->low_water_mark_bytes)
        {
            WARNING("%s: Throttling OFF (estimated server memory %zd).",
                    this_plugin_name, tracker->server_memory_in_use);
            tracker->is_throttling = 0;
        }
    }
    else
    {
        if (tracker->server_memory_in_use > tracker->high_water_mark_bytes)
        {
            WARNING("%s: Throttling ON (estimated server memory %zd).",
                    this_plugin_name, tracker->server_memory_in_use);
            tracker->is_throttling = 1;
        }
    }

    // Let's update our stats here so that the "stackdriver_agent" plugin can pick them up.
    if (mtg_update_stats(tracker->server_memory_in_use, tracker->is_throttling) != 0)
    {
        ERROR("%s: mtg_update_stats failed.", this_plugin_name);
        return -1;
    }

    if (tracker->is_throttling && context->ok_to_throttle)
    {
        return FC_MATCH_NO_MATCH;
    }

    if (mtg_add_new_entries(context, now, vl) != 0)
    {
        ERROR("%s: mtg_add_new_entries failed.", this_plugin_name);
        return -1;
    }
    return FC_MATCH_MATCHES;
}

static int mtg_match (const data_set_t __attribute__((unused)) *ds,
        const value_list_t *vl,
        notification_meta_t __attribute__((unused)) **meta,
        void **user_data)
{
    int result;
    mtg_context_t *context = *user_data;
    // I'm not really sure about the thread safety of filters, so take a big
    // lock and do the actual work inside mtg_match_helper.
    pthread_mutex_lock(context->mutex);
    result = mtg_match_helper(vl, context);
    pthread_mutex_unlock(context->mutex);
    return result;
}

void module_register (void)
{
    match_proc_t m = {
        .create = &mtg_create,
        .destroy = &mtg_destroy,
        .match = &mtg_match
    };
    fc_register_match ("throttle_metadata_keys", m);
}
