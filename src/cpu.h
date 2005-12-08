#ifndef CPU_H
#define CPU_H

#include "collectd.h"

#ifndef COLLECT_CPU
#if defined(KERNEL_LINUX) || defined(HAVE_LIBKSTAT) || defined(HAVE_SYSCTLBYNAME)
#define COLLECT_CPU 1
#else
#define COLLECT_CPU 0
#endif
#endif /* !defined(COLLECT_CPU) */

#if COLLECT_CPU

void cpu_init (void);
void cpu_read (void);

#endif /* COLLECT_CPU */
#endif /* CPU_H */
