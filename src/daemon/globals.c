#include "globals.h"
#include "plugin.h"
/*
 * Global variables
 */
char hostname_g[DATA_MAX_NAME_LEN];
const int hostname_g_size = sizeof (hostname_g);
cdtime_t interval_g;
int  pidfile_from_cli = 0;
int  timeout_g;
#if HAVE_LIBKSTAT
kstat_ctl_t *kc;
#endif /* HAVE_LIBKSTAT */
