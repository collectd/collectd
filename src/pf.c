/*
 * Copyright (c) 2010 Pierre-Yves Ritschard <pyr@openbsd.org>
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
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/pfvar.h>

#include <limits.h>
#include <fcntl.h>
#include <paths.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#ifndef TEST
#include "collectd.h"
#include "plugin.h"
#else
typedef u_int64_t	counter_t;
#endif

#define PF_SOCKET "/dev/pf"

struct pfdata {
	int		pd_dev;
};

static struct pfdata 	pd;

static int		pf_init(void);
static int		pf_read(void);
static void		submit_counter(const char *, const char *, counter_t);

void
submit_counter(const char *type, const char *inst, counter_t val)
{
#ifndef TEST
	value_t		values[1];
	value_list_t	vl = VALUE_LIST_INIT;

	values[0].counter = val;
	vl.values = values;
	vl.values_len = 1;

	strlcpy(vl.host, hostname_g, sizeof(vl.host));
	strlcpy(vl.plugin, "pf", sizeof(vl.plugin));
	strlcpy(vl.type, type, sizeof(vl.type));
	strlcpy(vl.type_instance, inst, sizeof(vl.type_instance));
	plugin_dispatch_values(&vl);
#else
	printf("%s.%s: %lld\n", type, inst, val);
#endif
}


int
pf_init(void)
{
	struct pf_status	status;

	memset(&pd, '\0', sizeof(pd));
	
	if ((pd.pd_dev = open(PF_SOCKET, O_RDWR)) == -1) {
		return (-1);
	}
	if (ioctl(pd.pd_dev, DIOCGETSTATUS, &status) == -1) {
		return (-1);
	}
        close(pd.pd_dev);
	if (!status.running)
		return (-1);
	
	return (0);
}

int
pf_read(void)
{
	int			 i;
	struct pf_status	 status;

	char 		*cnames[] = PFRES_NAMES;
	char 		*lnames[] = LCNT_NAMES;
	char 		*names[] = { "searches", "inserts", "removals" };

	if ((pd.pd_dev = open(PF_SOCKET, O_RDWR)) == -1) {
		return (-1);
	}
	if (ioctl(pd.pd_dev, DIOCGETSTATUS, &status) == -1) {
		return (-1);
	}
        close(pd.pd_dev);
	for (i = 0; i < PFRES_MAX; i++)
		submit_counter("pf_counters", cnames[i], status.counters[i]);
	for (i = 0; i < LCNT_MAX; i++)
		submit_counter("pf_limits", lnames[i], status.lcounters[i]);
	for (i = 0; i < FCNT_MAX; i++)
		submit_counter("pf_state", names[i], status.fcounters[i]);
	for (i = 0; i < SCNT_MAX; i++)
		submit_counter("pf_source", names[i], status.scounters[i]);
	return (0);
}

#ifdef TEST
int
main(int argc, char *argv[])
{
	if (pf_init())
		err(1, "pf_init");
	if (pf_read())
		err(1, "pf_read");
	return (0);
}
#else
void module_register(void) {
	plugin_register_init("pf", pf_init);
	plugin_register_read("pf", pf_read);
}
#endif