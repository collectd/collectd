#include "compat.h"
#include "libconfig.h"
#include "libconfig_private.h"
#include "conf_space.h"

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

int lc_process_conf_space(const char *appname, const char *configfile) {
	LC_FILE *configfp = NULL;
	const char *local_lc_errfile;
	char linebuf[LC_LINEBUF_LEN] = {0}, *linebuf_ptr = NULL;
	char *cmd = NULL, *value = NULL, *sep = NULL;
	char *fgetsret = NULL;
	int local_lc_errline;
	int lcpvret = -1;
	int retval = 0;
	lc_err_t save_lc_errno = LC_ERR_NONE;

	local_lc_errfile = configfile;
	local_lc_errline = 0;

	if (appname == NULL || configfile == NULL) {
		lc_errfile = local_lc_errfile;
		lc_errline = local_lc_errline;
		lc_errno = LC_ERR_INVDATA;
		return(-1);
	}

	configfp = lc_fopen(configfile, "r");

	if (configfp == NULL) {
		lc_errfile = local_lc_errfile;
		lc_errline = local_lc_errline;
		lc_errno = LC_ERR_CANTOPEN;
		return(-1);
	}

	while (1) {
		fgetsret = lc_fgets(linebuf, sizeof(linebuf) - 1, configfp);
		if (fgetsret == NULL) {
			break;
		}
		if (lc_feof(configfp)) {
			break;
		}

		local_lc_errline++;

		linebuf_ptr = &linebuf[strlen(linebuf) - 1];
		while (*linebuf_ptr < ' ' && linebuf_ptr >= linebuf) {
			*linebuf_ptr = '\0';
			linebuf_ptr--;
		}

		linebuf_ptr = &linebuf[0];
		while (*linebuf_ptr == ' ') {
			linebuf_ptr++;
		}

		if (*linebuf_ptr == '#' || *linebuf_ptr == '\0') {
			continue;
		}


		sep = strpbrk(linebuf_ptr, " \t");
		cmd = linebuf_ptr;
		if (sep != NULL) {
			*sep = '\0';
			sep++;
			while (*sep == ' ' || *sep == '\t') {
				sep++;
			}
			value = sep;
		} else {
			value = NULL;
		}

		save_lc_errno = lc_errno;
		lc_errno = LC_ERR_NONE;
		lcpvret = lc_process_var(cmd, NULL, value, LC_FLAGS_VAR);
		if (lcpvret < 0) {
			if (lc_errno == LC_ERR_NONE) {
#ifdef DEBUG
				fprintf(stderr, "Invalid command: \"%s\"\n", cmd);
#endif
				lc_errno = LC_ERR_INVCMD;
			} else {
#ifdef DEBUG
				fprintf(stderr, "Error processing command (command was valid, but an error occured, errno was set)\n");
#endif
			}
			lc_errfile = local_lc_errfile;
			lc_errline = local_lc_errline;
			retval = -1;
		} else {
			lc_errno = save_lc_errno;
		}
	}

	lc_fclose(configfp);

	return(retval);
}
