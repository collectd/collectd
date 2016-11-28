/*
 * vminfo.c - parse libvirt bulk stats records into C structures
 * Copyright (C) 2014-2016 Red Hat, Inc.
 * Written by Francesco Romani <fromani@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;
 * if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vminfo.h"

static int strequals(const char *s1, const char *s2) {
  return strcmp(s1, s2) == 0;
}

static int strstartswith(const char *longest, const char *prefix) {
  char *ret = strstr(longest, prefix);
  return ret == longest;
}

#define DISPATCH(NAME, FIELD)                                                  \
  do {                                                                         \
    if (strequals(name, #NAME)) {                                              \
      stats->FIELD = item->value.ul;                                           \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define SETUP(STATS, NAME, ITEM)                                               \
  do {                                                                         \
    if (strequals(NAME, "name")) {                                             \
      size_t len = strlen(ITEM->value.s);                                      \
      if (len > (STATS_NAME_LEN - 1)) {                                        \
        STATS->xname = strdup(ITEM->value.s);                                  \
      } else {                                                                 \
        strncpy(STATS->name, ITEM->value.s, STATS_NAME_LEN);                   \
      }                                                                        \
      return;                                                                  \
    }                                                                          \
  } while (0)

static void blockinfo_parse_field(BlockStats *stats, const char *name,
                                  const virTypedParameterPtr item) {
  SETUP(stats, name, item);

  DISPATCH(rd.reqs, rd_reqs);
  DISPATCH(rd.bytes, rd_bytes);
  DISPATCH(rd.times, rd_times);

  DISPATCH(wr.reqs, wr_reqs);
  DISPATCH(wr.bytes, wr_bytes);
  DISPATCH(wr.times, wr_times);

  DISPATCH(fl.reqs, fl_reqs);
  DISPATCH(fl.times, fl_times);

  DISPATCH(allocation, allocation);
  DISPATCH(capacity, capacity);
  DISPATCH(physical, physical);
}

static void ifaceinfo_parse_field(IFaceStats *stats, const char *name,
                                  const virTypedParameterPtr item) {
  SETUP(stats, name, item);

  DISPATCH(rx.bytes, rx_bytes);
  DISPATCH(rx.pkts, rx_pkts);
  DISPATCH(rx.errs, rx_errs);
  DISPATCH(rx.drop, rx_drop);

  DISPATCH(tx.bytes, tx_bytes);
  DISPATCH(tx.pkts, tx_pkts);
  DISPATCH(tx.errs, tx_errs);
  DISPATCH(tx.drop, tx_drop);
}

#undef SETUP

#undef DISPATCH

static void vcpuinfo_parse_field(VCpuStats *stats, const char *name,
                                 const virTypedParameterPtr item) {
  if (strequals(name, "state")) {
    stats->present = 1;
    stats->state = item->value.i;
    return;
  }
  if (strequals(name, "time")) {
    stats->present = 1;
    stats->time = item->value.ul;
    return;
  }
  return;
}

#define ALLOC_XSTATS(subset, MAXSTATS, ITEMSIZE)                               \
  do {                                                                         \
    if (subset->nstats > MAXSTATS) {                                           \
      subset->xstats = calloc(subset->nstats, ITEMSIZE);                       \
      if (subset->xstats == NULL) {                                            \
        goto cleanup;                                                          \
      }                                                                        \
    }                                                                          \
  } while (0)

static int vminfo_setup(VMInfo *vm, const virDomainStatsRecordPtr record) {
  VCpuInfo *vcpu = &vm->vcpu;
  BlockInfo *block = &vm->block;
  IFaceInfo *iface = &vm->iface;
  int i;

  for (i = 0; i < record->nparams; i++) {
    const virTypedParameterPtr item = &record->params[i]; /* shortcut */

    if (strequals(item->field, "block.count")) {
      block->nstats = item->value.ul;
    }
    if (strequals(item->field, "net.count")) {
      iface->nstats = item->value.ul;
    }
    if (strequals(item->field, "vcpu.current")) {
      vcpu->current = item->value.ul;
    } else if (strequals(item->field, "vcpu.maximum")) {
      vcpu->nstats = item->value.ul;
    }
  }

  ALLOC_XSTATS(vcpu, VCPU_STATS_NUM, sizeof(VCpuInfo));
  ALLOC_XSTATS(block, BLOCK_STATS_NUM, sizeof(BlockStats));
  ALLOC_XSTATS(iface, IFACE_STATS_NUM, sizeof(IFaceStats));

  return 0;

cleanup:
  free(block->xstats);
  free(vcpu->xstats);
  return -1;
}

#undef ALLOC_XSTATS

static int pcpuinfo_parse(PCpuInfo *pcpu, const virTypedParameterPtr item) {
  if (strequals(item->field, "cpu.time")) {
    pcpu->time = item->value.ul;
    return 0;
  }
  if (strequals(item->field, "cpu.user")) {
    pcpu->user = item->value.ul;
    return 0;
  }
  if (strequals(item->field, "cpu.system")) {
    pcpu->system = item->value.ul;
    return 0;
  }
  return 0;
}

static int ballooninfo_parse(BalloonInfo *balloon,
                             const virTypedParameterPtr item) {
  if (strequals(item->field, "balloon.current")) {
    balloon->current = item->value.ul;
    return 0;
  }
  if (strequals(item->field, "balloon.maximum")) {
    balloon->maximum = item->value.ul;
  }
  return 0;
}

enum { OFF_BUF_LEN = 128 };

struct FieldScanner {
  const char *prefix;
  size_t maxoffset;
};

struct FieldMatch {
  const char *suffix;
  size_t offset;
};

static void scan_init(struct FieldScanner *scan, const char *prefix,
                      size_t maxoffset) {
  scan->prefix = prefix;
  scan->maxoffset = maxoffset;
}

static int scan_field(struct FieldScanner *scan, const char *virFieldName,
                      struct FieldMatch *match) {
  const char *pc = virFieldName;
  char buf[OFF_BUF_LEN] = {'\0'};
  size_t j = 0;

  if (!scan || !strstartswith(virFieldName, scan->prefix)) {
    return 0;
  }

  for (j = 0; j < sizeof(buf) - 1 && virFieldName && isdigit(*pc); j++) {
    buf[j] = *pc++;
  }
  pc++; /* skip '.' separator */

  if (match) {
    match->suffix = pc;
    match->offset = atol(buf);
    return (pc != NULL && match->offset < scan->maxoffset);
  }
  return (pc != NULL);
}

static int vcpuinfo_parse(VCpuInfo *vcpu, const virTypedParameterPtr item) {
  VCpuStats *stats = (vcpu->xstats) ? vcpu->xstats : vcpu->stats;
  struct FieldScanner scan;
  struct FieldMatch match;

  scan_init(&scan, "vcpu.", vcpu->nstats);

  if (scan_field(&scan, item->field, &match)) {
    vcpuinfo_parse_field(stats + match.offset, match.suffix, item);
  }

  return 0;
}

static int blockinfo_parse(BlockInfo *block, const virTypedParameterPtr item) {
  BlockStats *stats = (block->xstats) ? block->xstats : block->stats;
  struct FieldScanner scan;
  struct FieldMatch match;

  scan_init(&scan, "block.", block->nstats);

  if (scan_field(&scan, item->field, &match)) {
    blockinfo_parse_field(stats + match.offset, match.suffix, item);
  }

  return 0;
}

static int ifaceinfo_parse(IFaceInfo *iface, const virTypedParameterPtr item) {
  IFaceStats *stats = (iface->xstats) ? iface->xstats : iface->stats;
  struct FieldScanner scan;
  struct FieldMatch match;

  scan_init(&scan, "iface.", iface->nstats);

  if (scan_field(&scan, item->field, &match)) {
    ifaceinfo_parse_field(stats + match.offset, match.suffix, item);
  }

  return 0;
}

#define TRY_TO_PARSE(subset, vm, record, i)                                    \
  do {                                                                         \
    if (subset##info_parse(&vm->subset, &record->params[i]) < 0) {             \
      /* TODO: logging? */                                                     \
      return -1;                                                               \
    }                                                                          \
  } while (0)

int vminfo_parse(VMInfo *vm, const virDomainStatsRecordPtr record,
                 int extrainfo) {
  int i = 0;

  if (vminfo_setup(vm, record)) {
    return -1;
  }

  vm->name = virDomainGetName(record->dom);
  if (vm->name == NULL) {
    return -1;
  }
  if (virDomainGetUUIDString(record->dom, vm->uuid) < 0) {
    return -1;
  }
  if (extrainfo) {
    int ret;
    if (virDomainGetInfo(record->dom, &vm->info) < 0) {
      return -1;
    }
    ret = virDomainMemoryStats(record->dom, vm->memstats,
                               VIR_DOMAIN_MEMORY_STAT_NR, 0);
    if (ret < 0) {
      return -1;
    }
    vm->memstats_count = ret;
  } else {
    memset(&vm->info, 0, sizeof(vm->info));
    memset(&vm->memstats, 0, sizeof(vm->memstats));
  }

  for (i = 0; i < record->nparams; i++) {
    /* intentionally ignore state, yet */
    TRY_TO_PARSE(pcpu, vm, record, i);
    TRY_TO_PARSE(balloon, vm, record, i);
    TRY_TO_PARSE(vcpu, vm, record, i);
    TRY_TO_PARSE(block, vm, record, i);
    TRY_TO_PARSE(iface, vm, record, i);
  }

  return 0;
}

#undef TRY_TO_PARSE

static void vcpuinfo_free(VCpuInfo *vcpu) { free(vcpu->xstats); }

static void blockinfo_free(BlockInfo *block) {
  size_t i;
  const BlockStats *stats = (block->xstats) ? block->xstats : block->stats;

  for (i = 0; i < block->nstats; i++)
    free(stats[i].xname);

  free(block->xstats);
}

static void ifaceinfo_free(IFaceInfo *iface) {
  size_t i;
  const IFaceStats *stats = (iface->xstats) ? iface->xstats : iface->stats;

  for (i = 0; i < iface->nstats; i++)
    free(stats[i].xname);

  free(iface->xstats);
}

void vminfo_init(VMInfo *vm) { memset(vm, 0, sizeof(*vm)); }

void vminfo_free(VMInfo *vm) {
  vcpuinfo_free(&vm->vcpu);
  blockinfo_free(&vm->block);
  ifaceinfo_free(&vm->iface);
}

/* vim: set sw=2 sts=2 et : */
