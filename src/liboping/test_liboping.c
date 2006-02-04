#include <stdlib.h>
#include <stdio.h>

#include "liboping.h"

int main (int argc, char **argv)
{
	pingobj_t      *ping;
	pingobj_iter_t *iter;

	int i;

	if (argc < 2)
	{
		printf ("Usage: %s <host> [host [host [...]]]\n", argv[0]);
		return (1);
	}

	if ((ping = ping_construct ()) == NULL)
	{
		fprintf (stderr, "ping_construct failed\n");
		return (-1);
	}

	for (i = 1; i < argc; i++)
	{
		printf ("Adding host `%s'..\n", argv[i]);

		if (ping_host_add (ping, argv[i]) > 0)
		{
			fprintf (stderr, "ping_host_add (verplant.org) failed\n");
			return (-1);
		}
	}

	while (1)
	{
		if (ping_send (ping) < 0)
		{
			fprintf (stderr, "ping_send failed\n");
			return (-1);
		}

		for (iter = ping_iterator_get (ping); iter != NULL; iter = ping_iterator_next (iter))
		{
			const char *host;
			double      latency;

			host    = ping_iterator_get_host (iter);
			latency = ping_iterator_get_latency (iter);

			printf ("host = %s, latency = %f\n", host, latency);
		}

		sleep (5);
	}

	return (0);
}
