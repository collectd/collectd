/*
 * MIT License
 *
 * Copyright(c) 2020 Intel Corporation. All rights reserved.
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
 * Kamil Wiatrowski <kamilx.wiatrowski@intel.com>
 *
 */

#include "collectd.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"

#include <linux/fs.h>
#include <sys/ioctl.h>

#define DISKSTATS_PLUGIN "diskstats"
#define STATS_PATH "/proc/diskstats"
#define DEFAULT_SECTOR_SIZE 512
#define DEFAULT_QUEUE_LEN 5
#define DS_NOT_SET 2

struct diskstats_s {
  unsigned long reads_completed;
  unsigned long reads_merged;
  unsigned long sectors_read;
  unsigned int ms_spent_reading;
  unsigned long writes_completed;
  unsigned long writes_merged;
  unsigned long sectors_written;
  unsigned int ms_spent_writing;
  unsigned int ios_in_progress;
  unsigned int ms_spent_ios;
  unsigned int weighted_ms_spent_ios;
  /* Kernel 4.18+ */
  unsigned long discards_completed;
  unsigned long discards_merged;
  unsigned long sectors_discared;
  unsigned int ms_spent_discarding;
  /* Kernel 5.5+ */
  unsigned long flush_req_completed;
  unsigned int ms_spent_flushing;
};
typedef struct diskstats_s diskstats_t;

struct rolling_array_s {
  unsigned int len;
  unsigned int idx;
  unsigned long *val_list;
  unsigned long sum;
};
typedef struct rolling_array_s rolling_array_t;

struct disklist_s {
  const char *name;
  bool in_progress;
  double sectors_to_mb;
  rolling_array_t avg_queue;
  /* For total await */
  rolling_array_t sum_time_ios;
  rolling_array_t sum_nr_ios;
  /* For await_read */
  rolling_array_t sum_time_reading;
  rolling_array_t sum_nr_reads;
  /* For await_write */
  rolling_array_t sum_time_writing;
  rolling_array_t sum_nr_writes;
  size_t prev; // Index of previous stats
  diskstats_t stats[2];
  cdtime_t last_update;
  struct disklist_s *next;
};
typedef struct disklist_s disklist_t;

static ignorelist_t *ignorelist;
static disklist_t *disklist;
static unsigned int queue_avg_len = DEFAULT_QUEUE_LEN;
static unsigned int await_avg_len = DEFAULT_QUEUE_LEN;

static int diskstats_config(oconfig_item_t *ci) {
  DEBUG(DISKSTATS_PLUGIN ": %s:%d", __FUNCTION__, __LINE__);

  if (ignorelist == NULL)
    ignorelist = ignorelist_create(/* invert = */ 1);
  if (ignorelist == NULL) {
    ERROR(DISKSTATS_PLUGIN ": Failed to create ignorelist.");
    return -1;
  }

  for (int i = 0; i < ci->children_num; i++) {
    int ret = 0;
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Disk", child->key) == 0) {
      char *disk = NULL;
      ret = cf_util_get_string(child, &disk);
      if (ret == 0) {
        DEBUG(DISKSTATS_PLUGIN ": adding disk: %s.", disk);
        ret = ignorelist_add(ignorelist, disk);
        sfree(disk);
      }
    } else if (strcasecmp("IgnoreSelected", child->key) == 0) {
      bool ignore = 0;
      ret = cf_util_get_boolean(child, &ignore);
      if (ret == 0)
        ignorelist_set_invert(ignorelist, ignore ? 0 : 1);
    } else if (strcasecmp("AvgQueueSize", child->key) == 0) {
      int len = 0;
      ret = cf_util_get_int(child, &len);
      if (ret == 0 && len > 0)
        queue_avg_len = (unsigned int)len;
      else {
        ERROR(DISKSTATS_PLUGIN
              ": Failed to read AvgQueueSize, it should be positive integer!");
        ret = -1;
      }
    } else if (strcasecmp("AwaitMovingWindowSize", child->key) == 0) {
      int len = 0;
      ret = cf_util_get_int(child, &len);
      if (ret == 0 && len > 0)
        await_avg_len = (unsigned int)len;
      else {
        ERROR(DISKSTATS_PLUGIN ": Failed to read AwaitMovingWindowSize, it "
                               "should be positive integer!");
        ret = -1;
      }
    } else {
      ERROR(DISKSTATS_PLUGIN ": Unknown configuration parameter \"%s\".",
            child->key);
      ret = -1;
    }

    if (ret != 0) {
      ERROR(DISKSTATS_PLUGIN ": %s:%d ret=%d", __FUNCTION__, __LINE__, ret);
      return ret;
    }
  }

  return 0;
}

static int rolling_array_init(rolling_array_t *ra, unsigned int size) {
  ra->idx = 0;
  ra->sum = 0;
  ra->len = size;
  ra->val_list = calloc(size, sizeof(*ra->val_list));
  if (ra->val_list == NULL) {
    ERROR(DISKSTATS_PLUGIN ": Failed to calloc rolling list.");
    return -1;
  }

  return 0;
}

static void rolling_array_add(rolling_array_t *ra, unsigned long val) {
  /* Subtract oldest value and add new value */
  ra->sum = ra->sum - ra->val_list[ra->idx] + val;
  ra->val_list[ra->idx++] = val;
  if (ra->idx >= ra->len)
    ra->idx = 0;
}

static inline double rolling_array_avg(rolling_array_t *ra) {
  return (double)ra->sum / (double)ra->len;
}

static inline double rolling_arrays_ratio(rolling_array_t *ra1,
                                          rolling_array_t *ra2) {
  return ra2->sum == 0 ? 0.0 : (double)ra1->sum / (double)ra2->sum;
}

static void diskstats_submit_gauge(const char *dev, const char *type_instance,
                                   gauge_t value) {
  value_list_t vl = VALUE_LIST_INIT;
  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;

  sstrncpy(vl.plugin, DISKSTATS_PLUGIN, sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, dev, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "diskstat_gauge", sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static void diskstats_submit_counter(const char *dev, const char *type_instance,
                                     counter_t value) {
  value_list_t vl = VALUE_LIST_INIT;
  vl.values = &(value_t){.counter = value};
  vl.values_len = 1;

  sstrncpy(vl.plugin, DISKSTATS_PLUGIN, sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, dev, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "diskstat_counter", sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static void diskstats_free_entry(disklist_t *disk) {
  if (disk->name)
    free((void *)disk->name);
  if (disk->avg_queue.val_list)
    free((void *)disk->avg_queue.val_list);
  if (disk->sum_time_ios.val_list)
    free((void *)disk->sum_time_ios.val_list);
  if (disk->sum_nr_ios.val_list)
    free((void *)disk->sum_nr_ios.val_list);
  if (disk->sum_time_reading.val_list)
    free((void *)disk->sum_time_reading.val_list);
  if (disk->sum_nr_reads.val_list)
    free((void *)disk->sum_nr_reads.val_list);
  if (disk->sum_time_writing.val_list)
    free((void *)disk->sum_time_writing.val_list);
  if (disk->sum_nr_writes.val_list)
    free((void *)disk->sum_nr_writes.val_list);
}

static disklist_t *diskstats_create_entry(const char *name, int sector_size) {
  disklist_t *disk = calloc(1, sizeof(*disk));
  if (disk == NULL) {
    ERROR(DISKSTATS_PLUGIN ": Failed to calloc disklist entry.");
    return NULL;
  }

  disk->name = strdup(name);
  if (disk->name == NULL) {
    ERROR(DISKSTATS_PLUGIN ": Failed to copy name.");
    free(disk);
    return NULL;
  }
  if (rolling_array_init(&disk->avg_queue, queue_avg_len) < 0) {
    diskstats_free_entry(disk);
    free(disk);
    return NULL;
  }
  if (rolling_array_init(&disk->sum_time_ios, await_avg_len) < 0) {
    diskstats_free_entry(disk);
    free(disk);
    return NULL;
  }
  if (rolling_array_init(&disk->sum_nr_ios, await_avg_len) < 0) {
    diskstats_free_entry(disk);
    free(disk);
    return NULL;
  }
  if (rolling_array_init(&disk->sum_time_reading, await_avg_len) < 0) {
    diskstats_free_entry(disk);
    free(disk);
    return NULL;
  }
  if (rolling_array_init(&disk->sum_nr_reads, await_avg_len) < 0) {
    diskstats_free_entry(disk);
    free(disk);
    return NULL;
  }
  if (rolling_array_init(&disk->sum_time_writing, await_avg_len) < 0) {
    diskstats_free_entry(disk);
    free(disk);
    return NULL;
  }
  if (rolling_array_init(&disk->sum_nr_writes, await_avg_len) < 0) {
    diskstats_free_entry(disk);
    free(disk);
    return NULL;
  }
  /* Get sector size in MiB */
  disk->sectors_to_mb = (double)sector_size / (double)(1024 * 1024);
  disk->prev = DS_NOT_SET;

  disk->next = disklist;
  disklist = disk;

  return disk;
}

static disklist_t *diskstats_find_entry(const char *name) {
  disklist_t *disk;
  for (disk = disklist; disk != NULL; disk = disk->next) {
    if (strcmp(disk->name, name) == 0)
      break;
  }

  if (disk != NULL)
    return disk;

  /* Get sector size */
  int sector_size = DEFAULT_SECTOR_SIZE;
#ifdef BLKSSZGET
  char dev_path[PATH_MAX];
  ssnprintf(dev_path, sizeof(dev_path), "/dev/%s", name);
  int fd = open(dev_path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    ERROR(DISKSTATS_PLUGIN ": Failed to open disk device: %s.", name);
    return NULL;
  }
  int rc = ioctl(fd, BLKSSZGET, &sector_size);

  close(fd);
  if (rc < 0) {
    ERROR(DISKSTATS_PLUGIN ": Failed to read sector size: %s.", name);
    return NULL;
  }
#else
  WARNING(DISKSTATS_PLUGIN
          ": BLKSSZGET is not supported, assuming default sector size = %d.",
          DEFAULT_SECTOR_SIZE);
#endif

  /* For DIF/DIX, VSS */
  if (sector_size > 512 && sector_size <= 528)
    sector_size = 512;
  if (sector_size > 4096 && sector_size <= 4224)
    sector_size = 4096;

  DEBUG(DISKSTATS_PLUGIN ": %s sector size = %d.", name, sector_size);

  /* Create new entry in disklist */
  disk = diskstats_create_entry(name, sector_size);

  return disk;
}

static unsigned int diskstats_diff_ui(unsigned int curr, unsigned int prev) {
  if (curr < prev)
    return 1 + curr + (UINT_MAX - prev);

  return curr - prev;
}

static unsigned long diskstats_diff_ul(unsigned long curr, unsigned long prev) {
  if (curr < prev)
    return 1 + curr + (ULONG_MAX - prev);

  return curr - prev;
}

#define DISKSTAT_DIFF(type, name) diskstats_diff_##type(ds->name, ds_prev->name)

static int diskstats_read(__attribute__((unused)) user_data_t *ud) {
  char buff[1024];
  FILE *fh = fopen(STATS_PATH, "r");
  if (fh == NULL) {
    ERROR(DISKSTATS_PLUGIN ": fopen(" STATS_PATH "): %s", STRERRNO);
    return -1;
  }

  while (fgets(buff, sizeof(buff), fh) != NULL) {
    cdtime_t now = cdtime();
    char *fields[32];
    int numfields = strsplit(buff, fields, STATIC_ARRAY_SIZE(fields));
    /* 7 is for partition without extended statistics */
    if (numfields != 7 && numfields < 14) {
      WARNING(DISKSTATS_PLUGIN ": Failed to read entry from: %s.", STATS_PATH);
      continue;
    }

    char *name = fields[2];
    if (ignorelist_match(ignorelist, name))
      continue;

    DEBUG(DISKSTATS_PLUGIN ": dev %s, num of fields = %d.", name, numfields);

    disklist_t *disk = diskstats_find_entry(name);
    if (disk == NULL) {
      fclose(fh);
      return -1;
    }
    disk->in_progress = true;

    /* On first read set index to 0, on subsequent intervals set index to
     * reverse of previous one using XOR (0 or 1) */
    diskstats_t *ds =
        disk->prev == DS_NOT_SET ? disk->stats : disk->stats + (disk->prev ^ 1);

    if (numfields == 7) {
      ds->reads_completed = atoll(fields[3]);
      ds->sectors_read = atoll(fields[4]);
      ds->writes_completed = atoll(fields[5]);
      ds->sectors_written = atoll(fields[6]);
    } else if (numfields >= 14) {
      ds->reads_completed = atoll(fields[3]);
      ds->reads_merged = atoll(fields[4]);
      ds->sectors_read = atoll(fields[5]);
      ds->ms_spent_reading = (unsigned int)atoll(fields[6]);
      ds->writes_completed = atoll(fields[7]);
      ds->writes_merged = atoll(fields[8]);
      ds->sectors_written = atoll(fields[9]);
      ds->ms_spent_writing = (unsigned int)atoll(fields[10]);
      ds->ios_in_progress = (unsigned int)atoll(fields[11]);
      ds->ms_spent_ios = (unsigned int)atoll(fields[12]);
      ds->weighted_ms_spent_ios = (unsigned int)atoll(fields[13]);

      rolling_array_add(&disk->avg_queue, ds->ios_in_progress);
    }
    if (numfields >= 18) {
      ds->discards_completed = atoll(fields[14]);
      ds->discards_merged = atoll(fields[15]);
      ds->sectors_discared = atoll(fields[16]);
      ds->ms_spent_discarding = (unsigned int)atoll(fields[17]);
    }
    if (numfields >= 20) {
      ds->flush_req_completed = atoll(fields[18]);
      ds->ms_spent_flushing = (unsigned int)atoll(fields[19]);
    }

    if (disk->prev == DS_NOT_SET) {
      /* On fisrt read just store values */
      disk->prev = 0;
      disk->last_update = now;
      continue;
    }

    diskstats_t *ds_prev = disk->stats + disk->prev;

    counter_t mb_read =
        (counter_t)(ds->sectors_read * disk->sectors_to_mb + 0.5);
    counter_t mb_wrtn =
        (counter_t)(ds->sectors_written * disk->sectors_to_mb + 0.5);
    diskstats_submit_counter(name, "mb_read", mb_read);
    diskstats_submit_counter(name, "mb_wrtn", mb_wrtn);

    double interval = CDTIME_T_TO_DOUBLE(now - disk->last_update);
    disk->last_update = now;

    unsigned long sectors_r_diff = DISKSTAT_DIFF(ul, sectors_read);
    unsigned long sectors_w_diff = DISKSTAT_DIFF(ul, sectors_written);

    gauge_t mb_read_s =
        ((double)sectors_r_diff / interval) * disk->sectors_to_mb;
    gauge_t mb_wrtn_s =
        ((double)sectors_w_diff / interval) * disk->sectors_to_mb;
    diskstats_submit_gauge(name, "mb_read_s", mb_read_s);
    diskstats_submit_gauge(name, "mb_wrtn_s", mb_wrtn_s);

    if (numfields == 7) {
      /* No data for more stats */
      disk->prev ^= 1;
      continue;
    }

    rolling_array_add(&disk->sum_time_reading,
                      DISKSTAT_DIFF(ui, ms_spent_reading));
    rolling_array_add(&disk->sum_nr_reads, DISKSTAT_DIFF(ul, reads_completed));
    rolling_array_add(&disk->sum_time_writing,
                      DISKSTAT_DIFF(ui, ms_spent_writing));
    rolling_array_add(&disk->sum_nr_writes,
                      DISKSTAT_DIFF(ul, writes_completed));

    unsigned int time_ios = DISKSTAT_DIFF(ui, ms_spent_reading) +
                            DISKSTAT_DIFF(ui, ms_spent_writing) +
                            DISKSTAT_DIFF(ui, ms_spent_discarding);
    unsigned long nr_ios = DISKSTAT_DIFF(ul, reads_completed) +
                           DISKSTAT_DIFF(ul, writes_completed) +
                           DISKSTAT_DIFF(ul, discards_completed);
    rolling_array_add(&disk->sum_time_ios, time_ios);
    rolling_array_add(&disk->sum_nr_ios, nr_ios);

    gauge_t await =
        rolling_arrays_ratio(&disk->sum_time_ios, &disk->sum_nr_ios);
    diskstats_submit_gauge(name, "await", await);

    gauge_t await_read =
        rolling_arrays_ratio(&disk->sum_time_reading, &disk->sum_nr_reads);
    diskstats_submit_gauge(name, "await_read", await_read);

    gauge_t await_write =
        rolling_arrays_ratio(&disk->sum_time_writing, &disk->sum_nr_writes);
    diskstats_submit_gauge(name, "await_write", await_write);

    diskstats_submit_gauge(name, "avg_queue",
                           rolling_array_avg(&disk->avg_queue));

    if (numfields < 18) {
      /* No data for more stats */
      disk->prev ^= 1;
      continue;
    }

    unsigned long sectors_d_diff = DISKSTAT_DIFF(ul, sectors_discared);
    unsigned long discards_diff = DISKSTAT_DIFF(ul, discards_completed);
    gauge_t mb_discarded_s =
        ((double)sectors_d_diff / interval) * disk->sectors_to_mb;
    gauge_t discards_s = (double)discards_diff / interval;
    diskstats_submit_gauge(name, "mb_discarded_s", mb_discarded_s);
    diskstats_submit_gauge(name, "discards_s", discards_s);

    disk->prev ^= 1;
  }
  fclose(fh);

  /* Clean unused devices from the list */
  disklist_t *disk = disklist;
  disklist_t *prev = NULL;
  while (disk != NULL) {
    if (disk->in_progress) {
      disk->in_progress = false;
      prev = disk;
      disk = disk->next;
      continue;
    }

    if (prev == NULL)
      disklist = disk->next;
    else
      prev->next = disk->next;

    disklist_t *tmp = disk;
    disk = disk->next;
    diskstats_free_entry(tmp);
    free(tmp);
  }

  return 0;
}

static int diskstats_init(void) {
  DEBUG(DISKSTATS_PLUGIN ": AvgQueueSize = %u.", queue_avg_len);
  DEBUG(DISKSTATS_PLUGIN ": AwaitMovingWindowSize = %u.", await_avg_len);
  return 0;
}

static int diskstats_shutdown(void) {
  ignorelist_free(ignorelist);
  ignorelist = NULL;

  disklist_t *disk = disklist;
  disklist_t *tmp;
  while (disk != NULL) {
    tmp = disk;
    disk = disk->next;
    diskstats_free_entry(tmp);
    free(tmp);
  }
  disklist = NULL;

  return 0;
}

void module_register(void) {
  plugin_register_complex_config(DISKSTATS_PLUGIN, diskstats_config);
  plugin_register_init(DISKSTATS_PLUGIN, diskstats_init);
  plugin_register_complex_read(NULL, DISKSTATS_PLUGIN, diskstats_read, 0, NULL);
  plugin_register_shutdown(DISKSTATS_PLUGIN, diskstats_shutdown);
}
