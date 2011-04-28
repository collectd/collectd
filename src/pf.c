/*
 * Copyright (c) 2010 Pierre-Yves Ritschard <pyr@openbsd.org>
 * Copyright (c) 2011 Stefan Rinkes <stefan.rinkes@gmail.org>
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

#include "pfcommon.h"

static int	 pf_init(void);
static int	 pf_read(void);
static void	 submit_counter(const char *, const char *, counter_t, int);

char		*pf_device = "/dev/pf";

int
pf_init(void)
{
	struct pf_status	status;
	int			pfdev = -1;

	if ((pfdev = open(pf_device, O_RDONLY)) == -1) {
		ERROR("unable to open %s", pf_device);
		return (-1);
	}

	if (ioctl(pfdev, DIOCGETSTATUS, &status) == -1) {
		ERROR("DIOCGETSTATUS: %i", pfdev);
		close(pfdev);
		return (-1);
	}

	close(pfdev);
	if (!status.running)
		return (-1);

	return (0);
}

int
pf_read(void)
{
	struct pf_status	status;
	int			pfdev = -1;
	int			i;

	char		*cnames[] = PFRES_NAMES;
	char		*lnames[] = LCNT_NAMES;
	char		*names[] = { "searches", "inserts", "removals" };

	if ((pfdev = open(pf_device, O_RDONLY)) == -1) {
		ERROR("unable to open %s", pf_device);
		return (-1);
	}

	if (ioctl(pfdev, DIOCGETSTATUS, &status) == -1) {
		ERROR("DIOCGETSTATUS: %i", pfdev);
		close(pfdev);
		return (-1);
	}

	close(pfdev);

	for (i = 0; i < PFRES_MAX; i++)
		submit_counter("pf_counters", cnames[i], status.counters[i], 0);
	for (i = 0; i < LCNT_MAX; i++)
		submit_counter("pf_limits", lnames[i], status.lcounters[i], 0);
	for (i = 0; i < FCNT_MAX; i++)
		submit_counter("pf_state", names[i], status.fcounters[i], 0);
	for (i = 0; i < SCNT_MAX; i++)
		submit_counter("pf_source", names[i], status.scounters[i], 0);

	submit_counter("pf_states", "current", status.states, 1);

	return (0);
}

void
submit_counter(const char *type, const char *inst, counter_t val, int usegauge)
{
#ifndef TEST
	value_t		values[1];
	value_list_t	vl = VALUE_LIST_INIT;

	if (usegauge)
		values[0].gauge = val;
	else
		values[0].counter = val;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "pf", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof(vl.type));
	sstrncpy (vl.type_instance, inst, sizeof(vl.type_instance));
	plugin_dispatch_values(&vl);
#else
	printf("%s.%s: %lld\n", type, inst, val);
#endif
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
