#ifndef COMMON_H
#define COMMON_H

#include "collectd.h"

#define sfree(ptr) \
	if((ptr) != NULL) { \
		free(ptr); \
	} \
	(ptr) = NULL

void sstrncpy(char *d, const char *s, int len);
char *sstrdup(const char *s);
void *smalloc(size_t size);

int strsplit (char *string, char **fields, size_t size);

int rrd_update_file (char *host, char *file, char *values, char **ds_def,
		int ds_num);

#ifdef HAVE_LIBKSTAT
int get_kstat (kstat_t **ksp_ptr, char *module, int instance, char *name);
long long get_kstat_value (kstat_t *ksp, char *name);
#endif

#endif /* COMMON_H */
