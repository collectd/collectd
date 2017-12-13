/**
 * collectd - src/utils_taskstats.h
 * Copyright (C) 2017       Florian octo Forster
 *
 * ISC License (ISC)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 */

#ifndef UTILS_TASKSTATS_H
#define UTILS_TASKSTATS_H 1

#include "collectd.h"

#include "utils_time.h"

struct ts_s;
typedef struct ts_s ts_t;

typedef struct {
  uint64_t cpu_ns;
  uint64_t blkio_ns;
  uint64_t swapin_ns;
  uint64_t freepages_ns;
} ts_delay_t;

ts_t *ts_create(void);
void ts_destroy(ts_t *);

/* ts_delay_by_tgid returns Linux delay accounting information for the task
 * identified by tgid. Returns zero on success and an errno otherwise. */
int ts_delay_by_tgid(ts_t *ts, uint32_t tgid, ts_delay_t *out);

#endif /* UTILS_TASKSTATS_H */
