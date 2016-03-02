#ifndef GLOBALS_H
#define GLOBALS_H

#include <inttypes.h>

/* Type for time as used by "utils_time.h" */
typedef uint64_t cdtime_t;

extern char       hostname_g[];
extern const int  hostname_g_size;
extern cdtime_t   interval_g;
extern int        pidfile_from_cli;
extern int        timeout_g;
#endif /* GLOBALS_H */
