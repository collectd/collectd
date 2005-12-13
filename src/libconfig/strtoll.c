#include <sys/types.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <stdio.h>

/* We only handle base 10. */
long long int strtoll(const char *nptr, char **endptr, int base) {
	long long int retval = 0;
	const char **endptrd = (const char **) endptr;
	const char *idx = NULL;
	int allowspace = 1;

	idx = nptr;
	while (1) {
		if (*idx == '\0') {
			break;
		}

		if (!isdigit(*idx)) {
			if (*idx == '-') {
				retval *= -1;
				continue;
			}
			if ((*idx == ' ' || *idx == '\t') && allowspace) {
				continue;
			}
			break;
		}

		retval *= 10;
		retval += (*idx - '0');

		allowspace = 0;
		idx++;
	}

	if (endptrd != NULL) {
		*endptrd = idx;
	}

	return(retval);
}
