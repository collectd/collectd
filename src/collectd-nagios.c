#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#include <sys/socket.h>
#include <sys/un.h>

/*
 * This weird macro cascade forces the glibc to define `NAN'. I don't know
 * another way to solve this, so more intelligent solutions are welcome. -octo
 */
#ifndef __USE_ISOC99
# define DISABLE__USE_ISOC99 1
# define __USE_ISOC99 1
#endif
#include <math.h>
#ifdef DISABLE__USE_ISOC99
# undef DISABLE__USE_ISOC99
# undef __USE_ISOC99
#endif

#define RET_OKAY     0
#define RET_WARNING  1
#define RET_CRITICAL 2
#define RET_UNKNOWN  3

#define CON_NONE     0
#define CON_AVERAGE  1
#define CON_SUM      2

struct range_s
{
	double min;
	double max;
	int    invert;
};
typedef struct range_s range_t;

extern char *optarg;
extern int optind, opterr, optopt;

static char *socket_file_g = NULL;
static char *value_string_g = NULL;
static range_t range_critical_g;
static range_t range_warning_g;
static int consolitation_g = CON_NONE;

static void parse_range (char *string, range_t *range)
{
	char *min_ptr;
	char *max_ptr;

	if (*string == '@')
	{
		range->invert = 1;
		string++;
	}

	max_ptr = strchr (string, ':');
	if (max_ptr == NULL)
	{
		min_ptr = NULL;
		max_ptr = string;
	}
	else
	{
		min_ptr = string;
		*max_ptr = '\0';
		max_ptr++;
	}

	assert (max_ptr != NULL);

	/* `10' == `0:10' */
	if (min_ptr == NULL)
		range->min = 0.0;
	/* :10 == ~:10 == -inf:10 */
	else if ((*min_ptr == '\0') || (*min_ptr == '~'))
		range->min = NAN;
	else
		range->min = atof (min_ptr);

	if ((*max_ptr == '\0') || (*max_ptr == '~'))
		range->max = NAN;
	else
		range->max = atof (max_ptr);
} /* void parse_range */

int match_range (range_t *range, double value)
{
	int ret = 0;

	if ((range->min != NAN) && (range->min > value))
		ret = 1;
	if ((range->max != NAN) && (range->max < value))
		ret = 1;

	return (((ret - range->invert) == 0) ? 0 : 1);
}

static int get_values (int *ret_values_num, double **ret_values,
		char ***ret_values_names)
{
	struct sockaddr_un sa;
	int status;
	int fd;
	FILE *fh;
	char buffer[4096];

	int values_num;
	double *values;
	char **values_names;

	int i;

	fd = socket (PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
	{
		fprintf (stderr, "socket failed: %s\n",
				strerror (errno));
		return (-1);
	}

	memset (&sa, '\0', sizeof (sa));
	sa.sun_family = AF_UNIX;
	strncpy (sa.sun_path, socket_file_g,
			sizeof (sa.sun_path) - 1);

	status = connect (fd, (struct sockaddr *) &sa, sizeof (sa));
	if (status != 0)
	{
		fprintf (stderr, "connect failed: %s\n",
				strerror (errno));
		return (-1);
	}

	fh = fdopen (fd, "r+");
	if (fh == NULL)
	{
		fprintf (stderr, "fdopen failed: %s\n",
				strerror (errno));
		close (fd);
		return (-1);
	}

	fprintf (fh, "GETVAL %s\n", value_string_g);
	fflush (fh);

	if (fgets (buffer, sizeof (buffer), fh) == NULL)
	{
		fprintf (stderr, "fgets failed: %s\n",
				strerror (errno));
		close (fd);
		return (-1);
	}
	close (fd); fd = -1;

	values_num = atoi (buffer);
	if (values_num < 1)
		return (-1);

	values = (double *) malloc (values_num * sizeof (double));
	if (values == NULL)
	{
		fprintf (stderr, "malloc failed: %s\n",
				strerror (errno));
		return (-1);
	}

	values_names = (char **) malloc (values_num * sizeof (char *));
	if (values_names == NULL)
	{
		fprintf (stderr, "malloc failed: %s\n",
				strerror (errno));
		free (values);
		return (-1);
	}

	{
		char *ptr = strchr (buffer, ' ') + 1;
		char *key;
		char *value;

		i = 0;
		while ((key = strtok (ptr, " \t")) != NULL)
		{
			ptr = NULL;
			value = strchr (key, '=');
			if (value == NULL)
				continue;
			*value = '\0'; value++;

			values_names[i] = strdup (key);
			values[i] = atof (value);

			i++;
			if (i >= values_num)
				break;
		}
		values_num = i;
	}

	*ret_values_num = values_num;
	*ret_values = values;
	*ret_values_names = values_names;

	return (0);
} /* int get_values */

static void usage (const char *name)
{
	fprintf (stderr, "Usage: %s <-s socket> <-n value_spec> [options]\n"
			"\n"
			"Valid options are:\n"
			" -s <socket>   Path to collectd's UNIX-socket\n"
			" -n <v_spec>   Value specification to get from collectd\n"
			" -c <range>    Critical range\n"
			" -w <range>    Range for critical values\n",
			name);
	exit (1);
} /* void usage */

int do_check_con_none (int values_num, double *values, char **values_names)
{
	int i;

	int num_critical = 0;
	int num_warning  = 0;
	int num_okay = 0;

	for (i = 0; i < values_num; i++)
	{
		if (values[i] == NAN)
			num_warning++;
		else if (match_range (&range_critical_g, values[i]) != 0)
			num_critical++;
		else if (match_range (&range_warning_g, values[i]) != 0)
			num_warning++;
		else
			num_okay++;
	}

	if ((num_critical != 0) || (values_num == 0))
	{
		printf ("CRITICAL: %i critical, %i warning, %i okay\n",
				num_critical, num_warning, num_okay);
		return (RET_CRITICAL);
	}
	else if (num_warning != 0)
	{
		printf ("WARNING: %i warning, %i okay\n",
				num_warning, num_okay);
		return (RET_WARNING);
	}
	else
	{
		printf ("OKAY: %i okay\n", num_okay);
		return (RET_OKAY);
	}

	return (RET_UNKNOWN);
} /* int do_check_con_none */

int do_check_con_average (int values_num, double *values, char **values_names)
{
	int i;
	double total;
	int total_num;

	total = 0.0;
	total_num = 0;
	for (i = 0; i < values_num; i++)
	{
		if (values[i] != NAN)
		{
			total += values[i];
			total_num++;
		}
	}

	if (total_num == 0)
	{
		printf ("WARNING: No defined values found\n");
		return (RET_WARNING);
	}

	if (match_range (&range_critical_g, total / total_num) != 0)
	{
		printf ("CRITICAL: Average = %lf\n",
				(double) (total / total_num));
		return (RET_CRITICAL);
	}
	else if (match_range (&range_warning_g, total / total_num) != 0)
	{
		printf ("WARNING: Average = %lf\n",
				(double) (total / total_num));
		return (RET_WARNING);
	}
	else
	{
		printf ("OKAY: Average = %lf\n",
				(double) (total / total_num));
		return (RET_OKAY);
	}

	return (RET_UNKNOWN);
} /* int do_check_con_average */

int do_check_con_sum (int values_num, double *values, char **values_names)
{
	int i;
	double total;
	int total_num;

	total = 0.0;
	total_num = 0;
	for (i = 0; i < values_num; i++)
	{
		if (values[i] != NAN)
		{
			total += values[i];
			total_num++;
		}
	}

	if (total_num == 0)
	{
		printf ("WARNING: No defined values found\n");
		return (RET_WARNING);
	}

	if (match_range (&range_critical_g, total) != 0)
	{
		printf ("CRITICAL: Sum = %lf\n", total);
		return (RET_CRITICAL);
	}
	else if (match_range (&range_warning_g, total) != 0)
	{
		printf ("WARNING: Sum = %lf\n", total);
		return (RET_WARNING);
	}
	else
	{
		printf ("OKAY: Sum = %lf\n", total);
		return (RET_OKAY);
	}

	return (RET_UNKNOWN);
} /* int do_check_con_sum */

int do_check (void)
{
	double  *values;
	char   **values_names;
	int      values_num;
	int i;

	if (get_values (&values_num, &values, &values_names) != 0)
	{
		fputs ("ERROR: Cannot get values from daemon\n", stdout);
		return (RET_CRITICAL);
	}

	for (i = 0; i < values_num; i++)
		printf ("%s=%lf\n", values_names[i], values[i]);

	if (consolitation_g == CON_NONE)
		return (do_check_con_none (values_num, values, values_names));
	else if (consolitation_g == CON_AVERAGE)
		return (do_check_con_average (values_num, values, values_names));
	else if (consolitation_g == CON_SUM)
		return (do_check_con_sum (values_num, values, values_names));

	return (RET_UNKNOWN);
}

int main (int argc, char **argv)
{
	range_critical_g.min = NAN;
	range_critical_g.max = NAN;
	range_critical_g.invert = 0;

	range_warning_g.min = NAN;
	range_warning_g.max = NAN;
	range_warning_g.invert = 0;

	while (42)
	{
		int c;

		c = getopt (argc, argv, "w:c:s:n:g:h");
		if (c < 0)
			break;

		switch (c)
		{
			case 'c':
				parse_range (optarg, &range_critical_g);
				break;
			case 'w':
				parse_range (optarg, &range_warning_g);
				break;
			case 's':
				socket_file_g = optarg;
				break;
			case 'n':
				value_string_g = optarg;
				break;
			case 'g':
				if (strcasecmp (optarg, "none") == 0)
					consolitation_g = CON_NONE;
				else if (strcasecmp (optarg, "average") == 0)
					consolitation_g = CON_AVERAGE;
				else if (strcasecmp (optarg, "sum") == 0)
					consolitation_g = CON_SUM;
				else
					usage (argv[0]);
				break;
			default:
				usage (argv[0]);
		} /* switch (c) */
	}

	if ((socket_file_g == NULL) || (value_string_g == NULL))
		usage (argv[0]);

	return (do_check ());
} /* int main */
