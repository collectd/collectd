#include "compat.h"
#include "libconfig.h"
#include "libconfig_private.h"
#include "conf_section.h"
#include "conf_apache.h"
#include "conf_colon.h"
#include "conf_equal.h"
#include "conf_space.h"
#include "conf_xml.h"

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

struct lc_varhandler_st *varhandlers = NULL;
lc_err_t lc_errno = LC_ERR_NONE;
const char *lc_errfile = NULL;
int lc_optind = 0;
int lc_errline = 0;

extern char **environ;

static int lc_process_var_string(void *data, const char *value, const char **endptr) {
	char **dataval;

	dataval = data;
	*dataval = strdup(value);

	*endptr = NULL;

	return(0);
}

static int lc_process_var_cidr(void *data, const char *value, const char **endptr) {
	return(-1);
}

static int lc_process_var_hostname6(void *data, const char *value, const char **endptr) {
	return(-1);
}

static int lc_process_var_hostname4(void *data, const char *value, const char **endptr) {
	return(-1);
}

static int lc_process_var_ip6(void *data, const char *value, const char **endptr) {
	return(-1);
}

static int lc_process_var_ip4(void *data, const char *value, const char **endptr) {
	uint32_t *dataval, retval = 0;
	const char *dotptr = NULL;
	int tmpval = -1;
//	int dotcount

	dataval = data;

	dotptr = value;

	while (1) {
		tmpval = atoi(dotptr);
		if (tmpval < 0) {
			break;
		}

		retval <<= 8;
		retval |= tmpval;

		dotptr = strpbrk(dotptr, "./ \t");
		if (dotptr == NULL) {
			break;
		}
		if (*dotptr != '.') {
			break;
		}
		dotptr++;
	}

	*dataval = retval;

	*endptr = (char *) dotptr;

	return(0);
}

static int lc_process_var_longlong(void *data, const char *value, const char **endptr) {
	long long *dataval;

	dataval = data;
	*dataval = strtoll(value, (char **) endptr, 10);

	return(0);
}

static int lc_process_var_long(void *data, const char *value, const char **endptr) {
	long *dataval;

	dataval = data;
	*dataval = strtoll(value, (char **) endptr, 10);

	return(0);
}

static int lc_process_var_int(void *data, const char *value, const char **endptr) {
	int *dataval;

	dataval = data;
	*dataval = strtoll(value, (char **) endptr, 10);

	return(0);
}

static int lc_process_var_short(void *data, const char *value, const char **endptr) {
	short *dataval;

	dataval = data;
	*dataval = strtoll(value, (char **) endptr, 10);

	return(0);
}

static int lc_process_var_bool_byexistance(void *data, const char *value, const char **endptr) {
	int *dataval;

	dataval = data;

	*dataval = 1;

	*endptr = NULL;

	return(0);
}

static int lc_process_var_bool(void *data, const char *value, const char **endptr) {
	char *trueval[] = {"enable", "true", "yes", "on", "y", "1"};
	char *falseval[] = {"disable", "false", "no", "off", "n", "0"};
	size_t chkvallen, vallen;
	int *dataval;
	int i;

	dataval = data;

	*dataval = -1;

	vallen = strlen(value);

	for (i = 0; i < (sizeof(trueval) / sizeof(*trueval)); i++) {
		chkvallen = strlen(trueval[i]);

		/*
		 * Skip if there's no way we could find a match here.
		 */
		if (chkvallen > vallen) {
			continue;
		}

		/*
		 * Skip if there is no partial match.
		 */
		if (strncasecmp(value, trueval[i], chkvallen) != 0) {
			continue;
		}

		if (value[chkvallen] == '\0' || value[chkvallen] == ',' || \
		    value[chkvallen] == ' ') {
			/* Declare a winner and set the next token. */
			*endptr = value + chkvallen;
			*dataval = 1;
			return(0);
		}
	}

	for (i = 0; i < (sizeof(falseval) / sizeof(*falseval)); i++) {
		chkvallen = strlen(falseval[i]);

		/*
		 * Skip if there's no way we could find a match here.
		 */
		if (chkvallen > vallen) {
			continue;
		}

		/*
		 * Skip if there is no partial match.
		 */
		if (strncasecmp(value, falseval[i], chkvallen) != 0) {
			continue;
		}

		if (value[chkvallen] == '\0' || value[chkvallen] == ',' || \
		    value[chkvallen] == ' ') {
			/* Declare a winner and set the next token. */
			*endptr = value + chkvallen;
			*dataval = 0;
			return(0);
		}
	}

	lc_errno = LC_ERR_BADFORMAT;
	return(-1);
}

static unsigned long long lc_process_size(const char *value, const char **endptr) {
	unsigned long long retval = 0;
	char *mult = NULL;

	retval = strtoll(value, &mult, 10);
	if (mult != NULL) {
		switch (tolower(mult[0])) {
			case 'p':
				retval *= 1125899906842624LLU;
				break;
			case 't':
				retval *= 1958505086976LLU;
				break;
			case 'g':
				retval *= 1073741824;
				break;
			case 'm':
				retval *= 1048576;
				break;
			case 'k':
				retval *= 1024;
				break;
			default:
				break;
		}
	}

	return(retval);
}

static int lc_process_var_sizelonglong(void *data, const char *value, const char **endptr) {
	long long *dataval;

	dataval = data;
	*dataval = lc_process_size(value, endptr);

	return(0);
}

static int lc_process_var_sizelong(void *data, const char *value, const char **endptr) {
	long *dataval;

	dataval = data;
	*dataval = lc_process_size(value, endptr);

	return(0);
}

static int lc_process_var_sizeint(void *data, const char *value, const char **endptr) {
	int *dataval;

	dataval = data;
	*dataval = lc_process_size(value, endptr);

	return(0);
}

static int lc_process_var_sizeshort(void *data, const char *value, const char **endptr) {
	short *dataval;

	dataval = data;
	*dataval = lc_process_size(value, endptr);

	return(0);
}

static int lc_process_var_sizesizet(void *data, const char *value, const char **endptr) {
	size_t *dataval;

	dataval = data;
	*dataval = lc_process_size(value, endptr);

	return(0);
}


static int lc_handle_type(lc_var_type_t type, const char *value, void *data) {
	const char *next;
	int is_list;

	is_list = type & LC_VAR_LIST;

	if (is_list == LC_VAR_LIST) {
	}

	switch (type) {
		case LC_VAR_STRING:
			return(lc_process_var_string(data, value, &next));
			break;
		case LC_VAR_LONG_LONG:
			return(lc_process_var_longlong(data, value, &next));
			break;
		case LC_VAR_LONG:
			return(lc_process_var_long(data, value, &next));
			break;
		case LC_VAR_INT:
			return(lc_process_var_int(data, value, &next));
			break;
		case LC_VAR_SHORT:
			return(lc_process_var_short(data, value, &next));
			break;
		case LC_VAR_BOOL:
			return(lc_process_var_bool(data, value, &next));
			break;
		case LC_VAR_SIZE_LONG_LONG:
			return(lc_process_var_sizelonglong(data, value, &next));
			break;
		case LC_VAR_SIZE_LONG:
			return(lc_process_var_sizelong(data, value, &next));
			break;
		case LC_VAR_SIZE_INT:
			return(lc_process_var_sizeint(data, value, &next));
			break;
		case LC_VAR_SIZE_SHORT:
			return(lc_process_var_sizeshort(data, value, &next));
			break;
		case LC_VAR_BOOL_BY_EXISTANCE:
			return(lc_process_var_bool_byexistance(data, value, &next));
			break;
		case LC_VAR_SIZE_SIZE_T:
			return(lc_process_var_sizesizet(data, value, &next));
			break;
		case LC_VAR_IP:
		case LC_VAR_IP4:
			return(lc_process_var_ip4(data, value, &next));
			break;
		case LC_VAR_IP6:
			return(lc_process_var_ip6(data, value, &next));
			break;
		case LC_VAR_HOSTNAME4:
			return(lc_process_var_hostname4(data, value, &next));
			break;
		case LC_VAR_HOSTNAME6:
			return(lc_process_var_hostname6(data, value, &next));
			break;
		case LC_VAR_CIDR:
			return(lc_process_var_cidr(data, value, &next));
			break;
		case LC_VAR_TIME:
		case LC_VAR_DATE:
		case LC_VAR_FILENAME:
		case LC_VAR_DIRECTORY:
#ifdef DEBUG
			fprintf(stderr, "Not implemented yet!\n");
#endif
			return(-1);
		case LC_VAR_NONE:
		case LC_VAR_UNKNOWN:
		case LC_VAR_SECTION:
		case LC_VAR_SECTIONSTART:
		case LC_VAR_SECTIONEND:
			return(0);
			break;
	}

	return(-1);
}

static int lc_handle(struct lc_varhandler_st *handler, const char *var, const char *varargs, const char *value, lc_flags_t flags) {
	const char *localvar = NULL;
	int retval;

	if (var != NULL) {
		localvar = strrchr(var, '.');
		if (localvar == NULL) {
			localvar = var;
		} else {
			localvar++;
		}
	} else {
		localvar = NULL;
	}

	switch (handler->mode) {
		case LC_MODE_CALLBACK:
			if (handler->callback != NULL) {
				retval = handler->callback(localvar, var, varargs, value, flags, handler->extra);
				if (retval < 0) {
					lc_errno = LC_ERR_CALLBACK;
				}
				return(retval);
			}
			break;
		case LC_MODE_VAR:
			return(lc_handle_type(handler->type, value, handler->data));
			break;
	}

	return(-1);
}

static int lc_process_environment(const char *appname) {
#ifndef ENABLE_SMALL
	struct lc_varhandler_st *handler = NULL;
	size_t appnamelen = 0;
	char varnamebuf[128] = {0};
	char **currvar;
	char *sep = NULL, *value = NULL, *cmd = NULL;
	char *ucase_appname = NULL, *ucase_appname_itr = NULL;
	char *lastcomponent_handler = NULL;
	int varnamelen = 0;
	char *local_lc_errfile;
	int local_lc_errline;

	/* Make sure we have an environment to screw with, if not,
	   no arguments were found to be in error */
	if (environ == NULL || appname == NULL) {
		return(0);
	}

	local_lc_errfile = "<environment>";
	local_lc_errline = 0;

	/* Allocate and create our uppercase appname. */
	ucase_appname = strdup(appname);
	if (ucase_appname == NULL) {
		lc_errfile = local_lc_errfile;
		lc_errline = local_lc_errline;
		lc_errno = LC_ERR_ENOMEM;
		return(-1);
	}
	for (ucase_appname_itr = ucase_appname; *ucase_appname_itr != '\0'; ucase_appname_itr++) {
		*ucase_appname_itr = toupper(*ucase_appname_itr);
	}

	appnamelen = strlen(ucase_appname);

	for (currvar = environ; *currvar != NULL; currvar++) {
		/* If it doesn't begin with our appname ignore it completely. */
		if (strncmp(*currvar, ucase_appname, appnamelen) != 0) {
			continue;
		}

		/* Find our seperator. */
		sep = strchr(*currvar, '=');
		if (sep == NULL) {
			continue;
		}

		varnamelen = sep - *currvar;

		/* Skip variables that would overflow our buffer. */
		if (varnamelen >= sizeof(varnamebuf)) {
			continue;
		}

		strncpy(varnamebuf, *currvar, varnamelen);

		varnamebuf[varnamelen] = '\0';
		value = sep + 1;

		/* We ignore APPNAME by itself. */
		if (strlen(varnamebuf) <= appnamelen) {
			continue;
		}

		/* Further it must be <APPNAME>_ */
		if (varnamebuf[appnamelen] != '_') {
			continue;
		}

		cmd = varnamebuf + appnamelen + 1;

		/* We don't allow section specifiers, for reasons see notes in
		   the cmdline processor (below). */
		if (strchr(cmd, '.') != NULL) {
			continue;
		}

		for (handler = varhandlers; handler != NULL; handler = handler->_next) {
			if (handler->var == NULL) {
				continue;
			}

			/* Skip handlers which don't agree with being
			   processed outside a config file */
			if (handler->type == LC_VAR_SECTION ||
			    handler->type == LC_VAR_SECTIONSTART ||
			    handler->type == LC_VAR_SECTIONEND ||
			    handler->type == LC_VAR_UNKNOWN) {
				continue;
			}

			/* Find the last part of the variable and compare it with 
			   the option being processed, if a wildcard is given. */
			if (handler->var[0] == '*' && handler->var[1] == '.') {
				lastcomponent_handler = strrchr(handler->var, '.');
				if (lastcomponent_handler == NULL) {
					lastcomponent_handler = handler->var;
				} else {
					lastcomponent_handler++;
				}
			} else {
				lastcomponent_handler = handler->var;
			}

			/* Ignore this handler if they don't match. */
			if (strcasecmp(lastcomponent_handler, cmd) != 0) {
				continue;
			}

			if (handler->type == LC_VAR_NONE || handler->type == LC_VAR_BOOL_BY_EXISTANCE) {
				value = NULL;
			}

			/* We ignore errors from the environment variables,
			   they're mostly insignificant. */
			lc_handle(handler, cmd, NULL, value, LC_FLAGS_ENVIRON);

			break;
		}
	}

	free(ucase_appname);

#endif
	return(0);
}

static int lc_process_cmdline(int argc, char **argv) {
	struct lc_varhandler_st *handler = NULL;
	char *cmdarg = NULL, *cmdoptarg = NULL;
	char *lastcomponent_handler = NULL;
	char **newargv = NULL;
	char *usedargv = NULL;
	int cmdargidx = 0;
	int newargvidx = 0;
	int retval = 0, chkretval = 0;
	int ch = 0;
	char *local_lc_errfile;
	int local_lc_errline;

	local_lc_errfile = "<cmdline>";
	local_lc_errline = 0;

	/* Allocate "argc + 1" (+1 for the NULL terminator) elements. */
	newargv = malloc((argc + 1) * sizeof(*newargv));
	if (newargv == NULL) {
		lc_errfile = local_lc_errfile;
		lc_errline = local_lc_errline;
		lc_errno = LC_ERR_ENOMEM;
		return(-1);
	}
	newargv[newargvidx++] = argv[0];
	newargv[argc] = NULL;

	/* Allocate space to indicate which arguments have been used. */
	usedargv = malloc(argc * sizeof(*usedargv));
	if (usedargv == NULL) {
		lc_errfile = local_lc_errfile;
		lc_errline = local_lc_errline;
		lc_errno = LC_ERR_ENOMEM;
		free(newargv);
		return(-1);
	}
	for (cmdargidx = 0; cmdargidx < argc; cmdargidx++) {
		usedargv[cmdargidx] = 0;
	}

	for (cmdargidx = 1; cmdargidx < argc; cmdargidx++) {
		cmdarg = argv[cmdargidx];

		/* Make sure we have an argument here. */
		if (cmdarg == NULL) {
			break;
		}

		/* If the argument isn't an option, skip. */
		if (cmdarg[0] != '-') {
			continue;
		}

		/* Setup a pointer in the new array for the actual argument. */
		newargv[newargvidx++] = cmdarg;
		usedargv[cmdargidx] = 1;

		/* Then shift the argument past the '-' so we can ignore it. */
		cmdarg++;

		/* Handle long options. */
		if (cmdarg[0] == '-') {
			cmdarg++;

			/* Don't process arguments after the '--' option. */
			if (cmdarg[0] == '\0') {
				break;
			}

			/* Look for a variable name that matches */
			for (handler = varhandlers; handler != NULL; handler = handler->_next) {
				/* Skip handlers with no variable name. */
				if (handler->var == NULL) {
					continue;
				}
				/* Skip handlers which don't agree with being
				   processed on the command line. */
				if (handler->type == LC_VAR_SECTION ||
				    handler->type == LC_VAR_SECTIONSTART ||
				    handler->type == LC_VAR_SECTIONEND ||
				    handler->type == LC_VAR_UNKNOWN) {
					continue;
				}

				/* Find the last part of the variable and compare it with 
				   the option being processed, if a wildcard is given. */
				if (handler->var[0] == '*' && handler->var[1] == '.') {
					lastcomponent_handler = strrchr(handler->var, '.');
					if (lastcomponent_handler == NULL) {
						lastcomponent_handler = handler->var;
					} else {
						lastcomponent_handler++;
					}
				} else {
					/* Disallow use of the fully qualified name
					   since there was no sectionstart portion
					   we cannot allow it to handle children of it. */
					if (strchr(cmdarg, '.') != NULL) {
						continue;
					}
					lastcomponent_handler = handler->var;
				}

				/* Ignore this handler if they don't match. */
				if (strcasecmp(lastcomponent_handler, cmdarg) != 0) {
					continue;
				}

				if (handler->type == LC_VAR_NONE || handler->type == LC_VAR_BOOL_BY_EXISTANCE) {
					cmdoptarg = NULL;
				} else {
					cmdargidx++;
					if (cmdargidx >= argc) {
						fprintf(stderr, "Argument required.\n");
						lc_errfile = local_lc_errfile;
						lc_errline = local_lc_errline;
						lc_errno = LC_ERR_BADFORMAT;
						free(usedargv);
						free(newargv);
						return(-1);
					}
					cmdoptarg = argv[cmdargidx];
					newargv[newargvidx++] = cmdoptarg;
					usedargv[cmdargidx] = 1;
				}

				chkretval = lc_handle(handler, handler->var, NULL, cmdoptarg, LC_FLAGS_CMDLINE);
				if (chkretval < 0) {
					retval = -1;
				}

				break;
			}

			if (handler == NULL) {
				fprintf(stderr, "Unknown option: --%s\n", cmdarg);
				lc_errfile = local_lc_errfile;
				lc_errline = local_lc_errline;
				lc_errno = LC_ERR_INVCMD;
				free(usedargv);
				free(newargv);
				return(-1);
			}
		} else {
			for (; *cmdarg != '\0'; cmdarg++) {
				ch = *cmdarg;

				for (handler = varhandlers; handler != NULL; handler = handler->_next) {
					if (handler->opt != ch || handler->opt == '\0') {
						continue;
					}
					/* Skip handlers which don't agree with being
					   processed on the command line. */
					if (handler->type == LC_VAR_SECTION ||
					    handler->type == LC_VAR_SECTIONSTART ||
					    handler->type == LC_VAR_SECTIONEND ||
					    handler->type == LC_VAR_UNKNOWN) {
						continue;
					}

					if (handler->type == LC_VAR_NONE || handler->type == LC_VAR_BOOL_BY_EXISTANCE) {
						cmdoptarg = NULL;
					} else {
						cmdargidx++;
						if (cmdargidx >= argc) {
							fprintf(stderr, "Argument required.\n");
							lc_errfile = local_lc_errfile;
							lc_errline = local_lc_errline;
							lc_errno = LC_ERR_BADFORMAT;
							free(usedargv);
							free(newargv);
							return(-1);
						}
						cmdoptarg = argv[cmdargidx];
						newargv[newargvidx++] = cmdoptarg;
						usedargv[cmdargidx] = 1;
					}

					chkretval = lc_handle(handler, handler->var, NULL, cmdoptarg, LC_FLAGS_CMDLINE);
					if (chkretval < 0) {
						lc_errfile = local_lc_errfile;
						lc_errline = local_lc_errline;
						retval = -1;
					}

					break;
				}

				if (handler == NULL) {
					fprintf(stderr, "Unknown option: -%c\n", ch);
					lc_errfile = local_lc_errfile;
					lc_errline = local_lc_errline;
					lc_errno = LC_ERR_INVCMD;
					free(usedargv);
					free(newargv);
					return(-1);
				}
			}
		}
	}

	if (retval >= 0) {
		lc_optind = newargvidx;
		for (cmdargidx = 1; cmdargidx < argc; cmdargidx++) {
			if (usedargv[cmdargidx] != 0) {
				continue;
			}
			
			cmdarg = argv[cmdargidx];

			newargv[newargvidx++] = cmdarg;
		}
		for (cmdargidx = 1; cmdargidx < argc; cmdargidx++) {
			argv[cmdargidx] = newargv[cmdargidx];
		}
	}

	free(usedargv);
	free(newargv);

	return(retval);
}


int lc_process_var(const char *var, const char *varargs, const char *value, lc_flags_t flags) {
	struct lc_varhandler_st *handler = NULL;
	const char *lastcomponent_handler = NULL, *lastcomponent_var = NULL;

	lastcomponent_var = strrchr(var, '.');
	if (lastcomponent_var == NULL) {
		lastcomponent_var = var;
	} else {
		lastcomponent_var++;
	}

	for (handler = varhandlers; handler != NULL; handler = handler->_next) {
		/* If either handler->var or var is NULL, skip, unless both are NULL. */
		if (handler->var != var && (handler->var == NULL || var == NULL)) {
			continue;
		}

		/* If both are not-NULL, compare them. */
		if (handler->var != NULL) {
			/* Wild-card-ish match. */
			if (handler->var[0] == '*' && handler->var[1] == '.') {
				/* Only compare the last components */

				lastcomponent_handler = strrchr(handler->var, '.') + 1; /* strrchr() won't return NULL, because we already checked it. */

				if (strcasecmp(lastcomponent_handler, lastcomponent_var) != 0) {
					continue;
				}
			} else if (strcasecmp(handler->var, var) != 0) {
				/* Exact (case-insensitive comparison) failed. */
				continue;
			}
		}

		if (value == NULL &&
		    handler->type != LC_VAR_NONE &&
		    handler->type != LC_VAR_BOOL_BY_EXISTANCE &&
		    handler->type != LC_VAR_SECTION &&
		    handler->type != LC_VAR_SECTIONSTART &&
		    handler->type != LC_VAR_SECTIONEND) {
			lc_errno = LC_ERR_BADFORMAT;
			break;
		}

		return(lc_handle(handler, var, varargs, value, flags));
	}

	return(-1);
}

int lc_register_callback(const char *var, char opt, lc_var_type_t type, int (*callback)(const char *, const char *, const char *, const char *, lc_flags_t, void *), void *extra) {
	struct lc_varhandler_st *newhandler = NULL;

	newhandler = malloc(sizeof(*newhandler));

	if (newhandler == NULL) {
		return(-1);
	}

	if (var == NULL) {
		newhandler->var = NULL;
	} else {
		newhandler->var = strdup(var);
	}
	newhandler->mode = LC_MODE_CALLBACK;
	newhandler->type = type;
	newhandler->callback = callback;
	newhandler->opt = opt;
	newhandler->extra = extra;
	newhandler->_next = varhandlers;

	varhandlers = newhandler;

	return(0);
}

int lc_register_var(const char *var, lc_var_type_t type, void *data, char opt) {
	struct lc_varhandler_st *newhandler = NULL;

	newhandler = malloc(sizeof(*newhandler));

	if (newhandler == NULL) {
		return(-1);
	}

	if (var == NULL) {
		newhandler->var = NULL;
	} else {
		newhandler->var = strdup(var);
	}
	newhandler->mode = LC_MODE_VAR;
	newhandler->type = type;
	newhandler->data = data;
	newhandler->opt = opt;
	newhandler->extra = NULL;
	newhandler->_next = varhandlers;

	varhandlers = newhandler;

	return(0);
}

int lc_process_file(const char *appname, const char *pathname, lc_conf_type_t type) {
	int chkretval = 0;

	switch (type) {
		case LC_CONF_SECTION:
			chkretval = lc_process_conf_section(appname, pathname);
			break;
		case LC_CONF_APACHE:
			chkretval = lc_process_conf_apache(appname, pathname);
			break;
		case LC_CONF_COLON:
			chkretval = lc_process_conf_colon(appname, pathname);
			break;
		case LC_CONF_EQUAL:
			chkretval = lc_process_conf_equal(appname, pathname);
			break;
		case LC_CONF_SPACE:
			chkretval = lc_process_conf_space(appname, pathname);
			break;
		case LC_CONF_XML:
			chkretval = lc_process_conf_xml(appname, pathname);
			break;
		default:
			chkretval = -1;
			lc_errno = LC_ERR_INVDATA;
			break;
	}

	return(chkretval);
}

static int lc_process_files(const char *appname, lc_conf_type_t type, const char *extraconfig) {
#ifdef HAVE_GETPWUID
	struct passwd *pwinfo = NULL;
#endif
	char configfiles[3][13][512] = {{{0}}};
	char *configfile = NULL;
	char *homedir = NULL;
	int configsetidx = 0, configidx = 0;
	int chkretval = 0, retval = 0;

	if (extraconfig != NULL) {
		snprintf(configfiles[0][0], sizeof(**configfiles) - 1, "%s", extraconfig);
	}
	snprintf(configfiles[1][0], sizeof(**configfiles) - 1, "/etc/%s.cfg", appname);
	snprintf(configfiles[1][1], sizeof(**configfiles) - 1, "/etc/%s.conf", appname);
	snprintf(configfiles[1][2], sizeof(**configfiles) - 1, "/etc/%s/%s.cfg", appname, appname);
	snprintf(configfiles[1][3], sizeof(**configfiles) - 1, "/etc/%s/%s.conf", appname, appname);
	snprintf(configfiles[1][4], sizeof(**configfiles) - 1, "/usr/etc/%s.cfg", appname);
	snprintf(configfiles[1][5], sizeof(**configfiles) - 1, "/usr/etc/%s.conf", appname);
	snprintf(configfiles[1][6], sizeof(**configfiles) - 1, "/usr/etc/%s/%s.cfg", appname, appname);
	snprintf(configfiles[1][7], sizeof(**configfiles) - 1, "/usr/etc/%s/%s.conf", appname, appname);
	snprintf(configfiles[1][8], sizeof(**configfiles) - 1, "/usr/local/etc/%s.cfg", appname);
	snprintf(configfiles[1][9], sizeof(**configfiles) - 1, "/usr/local/etc/%s.conf", appname);
	snprintf(configfiles[1][10], sizeof(**configfiles) - 1, "/usr/local/etc/%s/%s.cfg", appname, appname);
	snprintf(configfiles[1][11], sizeof(**configfiles) - 1, "/usr/local/etc/%s/%s.conf", appname, appname);
	if (getuid() != 0) {
		homedir = getenv("HOME");
#ifdef HAVE_GETPWUID
		if (homedir == NULL) {
			pwinfo = getpwuid(getuid());
			if (pwinfo != NULL) {
				homedir = pwinfo->pw_dir;
			}
		}
#endif
		if (homedir != NULL) {
			if (strcmp(homedir, "/") != 0 && access(homedir, R_OK|W_OK|X_OK) == 0) {
				snprintf(configfiles[2][0], sizeof(**configfiles) - 1, "%s/.%src", homedir, appname);
				snprintf(configfiles[2][1], sizeof(**configfiles) - 1, "%s/.%s.cfg", homedir, appname);
				snprintf(configfiles[2][2], sizeof(**configfiles) - 1, "%s/.%s.conf", homedir, appname);
				snprintf(configfiles[2][3], sizeof(**configfiles) - 1, "%s/.%s/%s.cfg", homedir, appname, appname);
				snprintf(configfiles[2][4], sizeof(**configfiles) - 1, "%s/.%s/%s.conf", homedir, appname, appname);
				snprintf(configfiles[2][5], sizeof(**configfiles) - 1, "%s/.%s/config", homedir, appname);
			}
		}
	}

	for (configsetidx = 0; configsetidx < 3; configsetidx++) {
		for (configidx = 0; configidx < 13; configidx++) {
			configfile = configfiles[configsetidx][configidx];
			if (configfile[0] == '\0') {
				break;
			}
			if (access(configfile, R_OK) == 0) {
				chkretval = lc_process_file(appname, configfile, type);
				if (chkretval < 0) {
					retval = -1;
				}
				break;
			}
		}
	}

	return(retval);
}

void lc_cleanup(void) {
	struct lc_varhandler_st *handler = NULL, *next = NULL;

	handler = varhandlers;
	while (handler != NULL) {
		if (handler->var != NULL) {
			free(handler->var);
		}

		next = handler->_next;

		free(handler);

		handler = next;
	}

	varhandlers = NULL;

	return;
}

int lc_process(int argc, char **argv, const char *appname, lc_conf_type_t type, const char *extra) {
	int retval = 0, chkretval = 0;

	/* Handle config files. */
	chkretval = lc_process_files(appname, type, extra);
	if (chkretval < 0) {
		retval = -1;
	}

	/* Handle environment variables.*/
	chkretval = lc_process_environment(appname);
	if (chkretval < 0) {
		retval = -1;
	}

	/* Handle command line arguments */
	chkretval = lc_process_cmdline(argc, argv);
	if (chkretval < 0) {
		retval = -1;
	}

	return(retval);
}


lc_err_t lc_geterrno(void) {
	lc_err_t retval;

	retval = lc_errno;

	lc_errno = LC_ERR_NONE;

	return(retval);
}

char *lc_geterrstr(void) {
	static char retval[512];
	char *errmsg = NULL;

	switch (lc_errno) {
		case LC_ERR_NONE:
			errmsg = "Success";
			break;
		case LC_ERR_INVCMD:
			errmsg = "Invalid command or option";
			break;
		case LC_ERR_INVSECTION:
			errmsg = "Invalid section";
			break;
		case LC_ERR_INVDATA:
			errmsg = "Invalid application data (internal error)";
			break;
		case LC_ERR_BADFORMAT:
			errmsg = "Bad data specified or incorrect format.";
			break;
		case LC_ERR_CANTOPEN:
			errmsg = "Can't open file.";
			break;
		case LC_ERR_CALLBACK:
			errmsg = "Error return from application handler.";
			break;
		case LC_ERR_ENOMEM:
			errmsg = "Insuffcient memory.";
			break;
	}

	/*
	 * This is not part of the switch statement so we will get warnings
	 * about unhandled enum values.
	 */
	if (errmsg == NULL) {
		errmsg = "Unknown error";
	}

	if (lc_errfile == NULL) {
		snprintf(retval, sizeof(retval), "%s:%i: %s", "<no file>", lc_errline, errmsg);
	} else {
		snprintf(retval, sizeof(retval), "%s:%i: %s", lc_errfile, lc_errline, errmsg);
	}

	retval[sizeof(retval) - 1] = '\0';

	return(retval);
}
