/*
 * vminfo.h - parse libvirt bulk stats records into C structures
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

#ifndef VMINFO_H
#define VMINFO_H

#include <stdio.h>

#include <libvirt/libvirt.h>

enum {
  STATS_NAME_LEN = 128,
  BLOCK_STATS_NUM = 8,
  IFACE_STATS_NUM = 8,
  VCPU_STATS_NUM = 16
};

typedef struct BlockStats BlockStats;
struct BlockStats {
  char *xname;
  char name[STATS_NAME_LEN];

  unsigned long long rd_reqs;
  unsigned long long rd_bytes;
  unsigned long long rd_times;
  unsigned long long wr_reqs;
  unsigned long long wr_bytes;
  unsigned long long wr_times;
  unsigned long long fl_reqs;
  unsigned long long fl_times;

  unsigned long long allocation;
  unsigned long long capacity;
  unsigned long long physical;
};

typedef struct BlockInfo BlockInfo;
struct BlockInfo {
  size_t nstats;
  BlockStats *xstats;
  BlockStats stats[BLOCK_STATS_NUM];
};

typedef struct IFaceStats IFaceStats;
struct IFaceStats {
  char *xname;
  char name[STATS_NAME_LEN];

  unsigned long long rx_bytes;
  unsigned long long rx_pkts;
  unsigned long long rx_errs;
  unsigned long long rx_drop;

  unsigned long long tx_bytes;
  unsigned long long tx_pkts;
  unsigned long long tx_errs;
  unsigned long long tx_drop;
};

typedef struct IFaceInfo IFaceInfo;
struct IFaceInfo {
  size_t nstats;
  IFaceStats *xstats;
  IFaceStats stats[IFACE_STATS_NUM];
};

typedef struct PCpuInfo PCpuInfo;
struct PCpuInfo {
  unsigned long long time;
  unsigned long long user;
  unsigned long long system;
};

typedef struct BalloonInfo BalloonInfo;
struct BalloonInfo {
  unsigned long long current;
  unsigned long long maximum;
};

typedef struct VCpuStats VCpuStats;
struct VCpuStats {
  int present;
  int state;
  unsigned long long time;
};

typedef struct VCpuInfo VCpuInfo;
struct VCpuInfo {
  size_t nstats; /* aka maximum */
  VCpuStats *xstats;
  VCpuStats stats[VCPU_STATS_NUM];

  size_t current;
};

typedef struct StateInfo StateInfo;
struct StateInfo {
  int state;
  int reason;
};

typedef struct VMInfo VMInfo;
struct VMInfo {
  char uuid[VIR_UUID_STRING_BUFLEN];
  const char *name;
  virDomainInfo info;
  virDomainMemoryStatStruct memstats[VIR_DOMAIN_MEMORY_STAT_NR];
  int memstats_count;

  StateInfo state;
  PCpuInfo pcpu;
  BalloonInfo balloon;
  VCpuInfo vcpu;
  BlockInfo block;
  IFaceInfo iface;
};

typedef struct VMChecks VMChecks;
struct VMChecks {
  int disk_usage_perc;
};

int vminfo_parse(VMInfo *vm, const virDomainStatsRecordPtr record,
                 int extrainfo);

void vminfo_init(VMInfo *vm);

void vminfo_free(VMInfo *vm);

#endif /* VMINFO_H */

/* vim: set sw=2 sts=2 et : */
