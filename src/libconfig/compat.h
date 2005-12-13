#ifndef RSK_COMPAT_H
#define RSK_COMPAT_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef __WIN32__
#include "win32.h"
#endif

#ifndef LC_LINEBUF_LEN
#define LC_LINEBUF_LEN 1024
#endif

#ifndef HAVE_GETUID
#include "getuid.h"
#endif
#ifndef HAVE_STRTOLL
#include "strtoll.h"
#endif
#ifndef HAVE_STRSEP
#include "strsep.h"
#endif
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif
#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif

#ifdef HAVE_OPENNET_H
#include <opennet.h>
#endif
#ifdef HAVE_LIBOPENNET
#define lc_fopen(path, mode) fopen_net(path, mode)
#define lc_fgets(buf, size, stream) fgets_net(buf, size, stream)
#define lc_feof(stream) feof_net(stream)
#define lc_fclose(stream) fclose_net(stream)
#define LC_FILE NETFILE
#else
#define lc_fopen(path, mode) fopen(path, mode)
#define lc_fgets(buf, size, stream) fgets(buf, size, stream)
#define lc_feof(stream) feof(stream)
#define lc_fclose(stream) fclose(stream)
#define LC_FILE FILE
#endif

#endif
