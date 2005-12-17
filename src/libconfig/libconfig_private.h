#ifndef _RSK_LIBCONFIG_PRIVATE_H
#define _RSK_LIBCONFIG_PRIVATE_H

#include "libconfig.h"

extern lc_err_t lc_errno;
extern int lc_errline;
extern const char *lc_errfile;

int lc_process_var(const char *var, const char *varargs, const char *value, lc_flags_t flags);

struct lc_varhandler_st;

struct lc_varhandler_st {
	struct lc_varhandler_st *_next;
	char *var;
	char opt;
	lc_var_type_t type;
	enum {
		LC_MODE_CALLBACK,
		LC_MODE_VAR
	} mode;
	int (*callback)();
	void *data;
	void *extra;
};

#endif
