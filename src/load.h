#ifndef LOAD_H
#define LOAD_H

#include "collectd.h"
#include "common.h"

#ifndef COLLECT_LOAD
#if defined(HAVE_GETLOADAVG) || defined(KERNEL_LINUX) || defined(HAVE_LIBSTATGRAB)
#define COLLECT_LOAD 1
#else
#define COLLECT_LOAD 0
#endif
#endif /* !defined(COLLECT_LOAD) */

#endif /* LOAD_H */
