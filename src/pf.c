/*
 * Copyright (c) 2010 Pierre-Yves Ritschard
 * Copyright (c) 2011 Stefan Rinkes
 *
 * Permission to use, copy, modify, and distribute this software for any
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
 *   Pierre-Yves Ritschard <pyr at openbsd.org>
 *   Stefan Rinkes <stefan.rinkes at gmail.org>
 */

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#if HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif
#if HAVE_NET_IF_H
#include <net/if.h>
#endif
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#include <net/pfvar.h>

#ifndef FCNT_NAMES
#if FCNT_MAX != 3
#error "Unexpected value for FCNT_MAX"
#endif
#define FCNT_NAMES {"search", "insert", "removals", NULL};
#endif

#ifndef SCNT_NAMES
#if SCNT_MAX != 3
#error "Unexpected value for SCNT_MAX"
#endif
#define SCNT_NAMES {"search", "insert", "removals", NULL};
#endif

static char const *pf_reasons[PFRES_MAX + 1] = PFRES_NAMES;
static char const *pf_lcounters[LCNT_MAX + 1] = LCNT_NAMES;
static char const *pf_fcounters[FCNT_MAX + 1] = FCNT_NAMES;
static char const *pf_scounters[SCNT_MAX + 1] = SCNT_NAMES;

static char const *pf_device = "/dev/pf";

static void pf_submit(char const *type, char const *type_instance, uint64_t val,
                      _Bool is_gauge) {
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  if (is_gauge)
    values[0].gauge = (gauge_t)val;
  else
    values[0].derive = (derive_t)val;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy(vl.plugin, "pf", sizeof(vl.plugin));
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* void pf_submit */

static int pf_read(void) {
  struct pf_status state;
  int fd;
  int status;

  fd = open(pf_device, O_RDONLY);
  if (fd < 0) {
    ERROR("pf plugin: Unable to open %s: %s", pf_device, STRERRNO);
    return -1;
  }

  status = ioctl(fd, DIOCGETSTATUS, &state);
  if (status != 0) {
    ERROR("pf plugin: ioctl(DIOCGETSTATUS) failed: %s", STRERRNO);
    close(fd);
    return -1;
  }

  close(fd);

  if (!state.running) {
    WARNING("pf plugin: PF is not running.");
    return -1;
  }

  for (int i = 0; i < PFRES_MAX; i++)
    pf_submit("pf_counters", pf_reasons[i], state.counters[i],
              /* is gauge = */ 0);
  for (int i = 0; i < LCNT_MAX; i++)
    pf_submit("pf_limits", pf_lcounters[i], state.lcounters[i],
              /* is gauge = */ 0);
  for (int i = 0; i < FCNT_MAX; i++)
    pf_submit("pf_state", pf_fcounters[i], state.fcounters[i],
              /* is gauge = */ 0);
  for (int i = 0; i < SCNT_MAX; i++)
    pf_submit("pf_source", pf_scounters[i], state.scounters[i],
              /* is gauge = */ 0);

  pf_submit("pf_states", "current", (uint32_t)state.states,
            /* is gauge = */ 1);

  return 0;
} /* int pf_read */

void module_register(void) { plugin_register_read("pf", pf_read); }
