dnl Process this file with autoconf to produce a configure script.
AC_INIT([collectd],[m4_esyscmd(./version-gen.sh)])
AC_CONFIG_SRCDIR(src/)
AC_CONFIG_HEADERS(src/config.h)
AC_CONFIG_AUX_DIR([libltdl/config])

m4_ifdef([LT_PACKAGE_VERSION],
	# libtool >= 2.2
	[
	 LT_CONFIG_LTDL_DIR([libltdl])
	 LT_INIT([dlopen])
	 LTDL_INIT([convenience])
	 AC_DEFINE(LIBTOOL_VERSION, 2, [Define to used libtool version.])
	]
,
	# libtool <= 1.5
	[
	 AC_LIBLTDL_CONVENIENCE
	 AC_SUBST(LTDLINCL)
	 AC_SUBST(LIBLTDL)
	 AC_LIBTOOL_DLOPEN
	 AC_CONFIG_SUBDIRS(libltdl)
	 AC_DEFINE(LIBTOOL_VERSION, 1, [Define to used libtool version.])
	]
)

AM_INIT_AUTOMAKE([tar-pax dist-bzip2 foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_LANG(C)

AC_PREFIX_DEFAULT("/opt/collectd")

AC_SYS_LARGEFILE

#
# Checks for programs.
#
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AM_PROG_CC_C_O
AM_CONDITIONAL(COMPILER_IS_GCC, test "x$GCC" = "xyes")

AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AC_PROG_LEX
AC_PROG_YACC
PKG_PROG_PKG_CONFIG

AC_CHECK_PROG([have_protoc_c], [protoc-c], [yes], [no])
AC_CHECK_HEADERS([google/protobuf-c/protobuf-c.h],
                 [have_protobuf_c_h="yes"],
                 [have_protobuf_c_h="no"])
if test "x$have_protoc_c" = "xyes" && test "x$have_protobuf_c_h" != "xyes"
then
	have_protoc_c="no (unable to find <google/protobuf-c/protobuf-c.h>)"
fi
AM_CONDITIONAL(HAVE_PROTOC_C, test "x$have_protoc_c" = "xyes")

AC_MSG_CHECKING([for kernel type ($host_os)])
case $host_os in
	*linux*)
	AC_DEFINE([KERNEL_LINUX], 1, [True if program is to be compiled for a Linux kernel])
	ac_system="Linux"
	;;
	*solaris*)
	AC_DEFINE([KERNEL_SOLARIS], 1, [True if program is to be compiled for a Solaris kernel])
	ac_system="Solaris"
	;;
	*darwin*)
	AC_DEFINE([KERNEL_DARWIN], 1, [True if program is to be compiled for a Darwin kernel])
	ac_system="Darwin"
	;;
	*openbsd*)
	AC_DEFINE([KERNEL_OPENBSD], 1, [True if program is to be compiled for an OpenBSD kernel])
	ac_system="OpenBSD"
	;;
	*aix*)
	AC_DEFINE([KERNEL_AIX], 1, [True if program is to be compiled for a AIX kernel])
	ac_system="AIX"
	;;
	*freebsd*)
	AC_DEFINE([KERNEL_FREEBSD], 1, [True if program is to be compiled for a FreeBSD kernel])
	ac_system="FreeBSD"
	;;
	*)
	ac_system="unknown"
esac
AC_MSG_RESULT([$ac_system])

AM_CONDITIONAL([BUILD_LINUX],[test "x$x$ac_system" = "xLinux"])
AM_CONDITIONAL([BUILD_SOLARIS],[test "x$x$ac_system" = "xSolaris"])
AM_CONDITIONAL([BUILD_DARWIN],[test "x$x$ac_system" = "xDarwin"])
AM_CONDITIONAL([BUILD_OPENBSD],[test "x$x$ac_system" = "xOpenBSD"])
AM_CONDITIONAL([BUILD_AIX],[test "x$x$ac_system" = "xAIX"])
AM_CONDITIONAL([BUILD_FREEBSD],[test "x$x$ac_system" = "xFreeBSD"])

if test "x$ac_system" = "xLinux"
then
	AC_ARG_VAR([KERNEL_DIR], [path to Linux kernel sources])
	if test -z "$KERNEL_DIR"
	then
		KERNEL_DIR="/lib/modules/`uname -r`/source"
	fi

	KERNEL_CFLAGS="-I$KERNEL_DIR/include"
	AC_SUBST(KERNEL_CFLAGS)
fi

if test "x$ac_system" = "xSolaris"
then
	AC_DEFINE(_POSIX_PTHREAD_SEMANTICS, 1, [Define to enforce POSIX thread semantics under Solaris.])
	AC_DEFINE(_REENTRANT,               1, [Define to enable reentrancy interfaces.])
fi
if test "x$ac_system" = "xAIX"
then
	AC_DEFINE(_THREAD_SAFE_ERRNO, 1, [Define to use the thread-safe version of errno under AIX.])
fi

# Where to install .pc files.
pkgconfigdir="${libdir}/pkgconfig"
AC_SUBST(pkgconfigdir)

# Check for standards compliance mode
AC_ARG_ENABLE(standards,
	      AS_HELP_STRING([--enable-standards], [Enable standards compliance mode]),
	      [enable_standards="$enableval"],
	      [enable_standards="no"])
if test "x$enable_standards" = "xyes"
then
	AC_DEFINE(_ISOC99_SOURCE,        1, [Define to enforce ISO C99 compliance.])
	AC_DEFINE(_POSIX_C_SOURCE, 200809L, [Define to enforce POSIX.1-2008 compliance.])
	AC_DEFINE(_XOPEN_SOURCE,       700, [Define to enforce X/Open 7 (XSI) compliance.])
	AC_DEFINE(_REENTRANT,            1, [Define to enable reentrancy interfaces.])
	if test "x$GCC" = "xyes"
	then
		CFLAGS="$CFLAGS -std=c99"
	fi
fi
AM_CONDITIONAL(BUILD_FEATURE_STANDARDS, test "x$enable_standards" = "xyes")

#
# Checks for header files.
#
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_HEADER_DIRENT
AC_HEADER_STDBOOL

AC_CHECK_HEADERS(stdio.h errno.h math.h stdarg.h syslog.h fcntl.h signal.h assert.h sys/types.h sys/socket.h sys/select.h poll.h netdb.h arpa/inet.h sys/resource.h sys/param.h kstat.h regex.h sys/ioctl.h endian.h sys/isa_defs.h fnmatch.h libgen.h)

# For ping library
AC_CHECK_HEADERS(netinet/in_systm.h, [], [],
[#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
])
AC_CHECK_HEADERS(netinet/in.h, [], [],
[#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
])
AC_CHECK_HEADERS(netinet/ip.h, [], [],
[#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
])
AC_CHECK_HEADERS(netinet/ip_icmp.h, [], [],
[#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#if HAVE_NETINET_IP_H
# include <netinet/ip.h>
#endif
])
AC_CHECK_HEADERS(netinet/ip_var.h, [], [],
[#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#if HAVE_NETINET_IP_H
# include <netinet/ip.h>
#endif
])
AC_CHECK_HEADERS(netinet/ip6.h, [], [],
[#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
])
AC_CHECK_HEADERS(netinet/icmp6.h, [], [],
[#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#if HAVE_NETINET_IP6_H
# include <netinet/ip6.h>
#endif
])
AC_CHECK_HEADERS(netinet/tcp.h, [], [],
[#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#if HAVE_NETINET_IP_H
# include <netinet/ip.h>
#endif
])
AC_CHECK_HEADERS(netinet/udp.h, [], [],
[#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#if HAVE_NETINET_IP_H
# include <netinet/ip.h>
#endif
])

# For cpu modules
AC_CHECK_HEADERS(sys/dkstat.h)
if test "x$ac_system" = "xDarwin"
then
	AC_CHECK_HEADERS(mach/mach_init.h mach/host_priv.h mach/mach_error.h mach/mach_host.h mach/mach_port.h mach/mach_types.h mach/message.h mach/processor_set.h mach/processor.h mach/processor_info.h mach/task.h mach/thread_act.h mach/vm_region.h mach/vm_map.h mach/vm_prot.h mach/vm_statistics.h mach/kern_return.h)
	AC_CHECK_HEADERS(CoreFoundation/CoreFoundation.h IOKit/IOKitLib.h IOKit/IOTypes.h IOKit/ps/IOPSKeys.h IOKit/IOBSD.h IOKit/storage/IOBlockStorageDriver.h)
	# For the battery plugin
	AC_CHECK_HEADERS(IOKit/ps/IOPowerSources.h, [], [],
[
#if HAVE_IOKIT_IOKITLIB_H
#  include <IOKit/IOKitLib.h>
#endif
#if HAVE_IOKIT_IOTYPES_H
#  include <IOKit/IOTypes.h>
#endif
])

fi

AC_CHECK_HEADERS(sys/sysctl.h, [], [],
[
#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#if HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
])

AC_MSG_CHECKING([for sysctl kern.cp_times])
if test -x /sbin/sysctl
then
	/sbin/sysctl kern.cp_times 2>/dev/null
	if test $? -eq 0
	then
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_SYSCTL_KERN_CP_TIMES, 1,
		[Define if sysctl supports kern.cp_times])
	else
		AC_MSG_RESULT([no])
	fi
else
	AC_MSG_RESULT([no])
fi

# For hddtemp module
AC_CHECK_HEADERS(linux/major.h)

# For md module (Linux only)
if test "x$ac_system" = "xLinux"
then
	AC_CHECK_HEADERS(linux/raid/md_u.h,
			 [have_linux_raid_md_u_h="yes"],
			 [have_linux_raid_md_u_h="no"],
[
#include <sys/ioctl.h>
#include <linux/major.h>
#include <linux/types.h>
])
else
	have_linux_raid_md_u_h="no"
fi

# For the swap module
have_linux_wireless_h="no"
if test "x$ac_system" = "xLinux"
then
  AC_CHECK_HEADERS(linux/wireless.h,
		   [have_linux_wireless_h="yes"],
		   [have_linux_wireless_h="no"],
[
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
])
fi

# For the swap module
have_sys_swap_h="yes"
AC_CHECK_HEADERS(sys/swap.h vm/anon.h, [], [have_sys_swap_h="no"],
[
#undef _FILE_OFFSET_BITS
#undef _LARGEFILE64_SOURCE
#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#if HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
])

if test "x$have_sys_swap_h$ac_system" = "xnoSolaris"
then
	hint_64=""
	if test "x$GCC" = "xyes"
	then
		hint_64="CFLAGS='-m64'"
	else
		hint_64="CFLAGS='-xarch=v9'"
	fi
	AC_MSG_NOTICE([Solaris detected and sys/swap.h not usable. Try building a 64-bit binary ($hint_64 ./configure).])
fi

# For load module
# For the processes plugin
# For users module
AC_CHECK_HEADERS(sys/loadavg.h linux/config.h utmp.h utmpx.h)

# For interface plugin
AC_CHECK_HEADERS(ifaddrs.h)
AC_CHECK_HEADERS(net/if.h, [], [],
[
#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#if HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
#endif
])
AC_CHECK_HEADERS(linux/if.h, [], [],
[
#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#if HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
#endif
])
AC_CHECK_HEADERS(linux/inet_diag.h, [], [],
[
#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#if HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
#endif
#if HAVE_LINUX_INET_DIAG_H
# include <linux/inet_diag.h>
#endif
])
AC_CHECK_HEADERS(linux/netdevice.h, [], [],
[
#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#if HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
#endif
#if HAVE_LINUX_IF_H
# include <linux/if.h>
#endif
])

# For ethstat module
AC_CHECK_HEADERS(linux/sockios.h,
    [have_linux_sockios_h="yes"],
    [have_linux_sockios_h="no"],
    [
#if HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#if HAVE_NET_IF_H
# include <net/if.h>
#endif
    ])
AC_CHECK_HEADERS(linux/ethtool.h,
    [have_linux_ethtool_h="yes"],
    [have_linux_ethtool_h="no"],
    [
#if HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#if HAVE_NET_IF_H
# include <net/if.h>
#endif
#if HAVE_LINUX_SOCKIOS_H
# include <linux/sockios.h>
#endif
    ])

# For ipvs module
have_linux_ip_vs_h="no"
have_net_ip_vs_h="no"
have_ip_vs_h="no"
ip_vs_h_needs_kernel_cflags="no"
if test "x$ac_system" = "xLinux"
then
	AC_CHECK_HEADERS(linux/ip_vs.h, [have_linux_ip_vs_h="yes"])
	AC_CHECK_HEADERS(net/ip_vs.h, [have_net_ip_vs_h="yes"])
	AC_CHECK_HEADERS(ip_vs.h, [have_ip_vs_h="yes"])

	if test "x$have_linux_ip_vs_h$have_net_ip_vs_h$have_ip_vs_h" = "xnonono" && test -d "$KERNEL_DIR"
	then
		SAVE_CFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS $KERNEL_CFLAGS"

		AC_MSG_NOTICE([Did not find ip_vs.h. Trying again using headers from $KERNEL_DIR.])

		AC_CHECK_HEADERS(linux/ip_vs.h, [have_linux_ip_vs_h="yes"])
		AC_CHECK_HEADERS(net/ip_vs.h, [have_net_ip_vs_h="yes"])
		AC_CHECK_HEADERS(ip_vs.h, [have_ip_vs_h="yes"])

		if test "x$have_linux_ip_vs_h" = "xyes" || test "x$have_net_ip_vs_h" = "xyes" || test "x$have_ip_vs_h" = "xyes"
		then
			ip_vs_h_needs_kernel_cflags="yes"
		fi

		CFLAGS="$SAVE_CFLAGS"
	fi
fi
AM_CONDITIONAL(IP_VS_H_NEEDS_KERNEL_CFLAGS, test "x$ip_vs_h_needs_kernel_cflags" = "xyes")

# For quota module
AC_CHECK_HEADERS(sys/ucred.h, [], [],
[
#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#if HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
])

# For mount interface
AC_CHECK_HEADERS(sys/mount.h, [], [],
[
#if HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#if HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
])

# For the email plugin
AC_CHECK_HEADERS(linux/un.h, [], [],
[
#if HAVE_SYS_SOCKET_H
#	include <sys/socket.h>
#endif
])

AC_CHECK_HEADERS(pwd.h grp.h sys/un.h ctype.h limits.h xfs/xqm.h fs_info.h fshelp.h paths.h mntent.h mnttab.h sys/fstyp.h sys/fs_types.h sys/mntent.h sys/mnttab.h sys/statfs.h sys/statvfs.h sys/vfs.h sys/vfstab.h sys/vmmeter.h kvm.h wordexp.h locale.h)

# For the dns plugin
AC_CHECK_HEADERS(arpa/nameser.h)
AC_CHECK_HEADERS(arpa/nameser_compat.h, [], [],
[
#if HAVE_ARPA_NAMESER_H
# include <arpa/nameser.h>
#endif
])

AC_CHECK_HEADERS(net/if_arp.h, [], [],
[#if HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
])
AC_CHECK_HEADERS(net/ppp_defs.h)
AC_CHECK_HEADERS(net/if_ppp.h, [], [],
[#if HAVE_NET_PPP_DEFS_H
# include <net/ppp_defs.h>
#endif
])
AC_CHECK_HEADERS(netinet/if_ether.h, [], [],
[#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#if HAVE_NET_IF_H
# include <net/if.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
])

AC_CHECK_HEADERS(netinet/ip_compat.h)

have_net_pfvar_h="no"
AC_CHECK_HEADERS(net/pfvar.h,
               [have_net_pfvar_h="yes"],
               [have_net_pfvar_h="no"],
[
#if HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#if HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#if HAVE_NET_IF_H
# include <net/if.h>
#endif
])

# For the multimeter plugin
have_termios_h="no"
AC_CHECK_HEADERS(termios.h, [have_termios_h="yes"])

# For the turbostat plugin
have_asm_msrindex_h="no"
AC_CHECK_HEADERS(asm/msr-index.h, [have_asm_msrindex_h="yes"])

if test "x$have_asm_msrindex_h" = "xyes"
then
  AC_CACHE_CHECK([whether asm/msr-index.h has MSR_CORE_C3_RESIDENCY],
                 [c_cv_have_usable_asm_msrindex_h],
                 AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[[
#include<asm/msr-index.h>
]]],
[[[
int y = MSR_CORE_C3_RESIDENCY;
return(y);
]]]
  )],
                 [c_cv_have_usable_asm_msrindex_h="yes"],
                 [c_cv_have_usable_asm_msrindex_h="no"],
                                  )
                 )
fi

have_cpuid_h="no"
AC_CHECK_HEADERS(cpuid.h, [have_cpuid_h="yes"])

AC_CHECK_HEADERS(sys/capability.h)
#
# Checks for typedefs, structures, and compiler characteristics.
#
AC_C_CONST
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UID_T
AC_TYPE_UINT32_T
AC_HEADER_TIME

#
# Checks for library functions.
#
AC_PROG_GCC_TRADITIONAL
AC_CHECK_FUNCS(gettimeofday select strdup strtol getaddrinfo getnameinfo strchr memcpy strstr strcmp strncmp strncpy strlen strncasecmp strcasecmp openlog closelog sysconf setenv if_indextoname setlocale)

AC_FUNC_STRERROR_R

SAVE_CFLAGS="$CFLAGS"
# Emulate behavior of src/Makefile.am
if test "x$GCC" = "xyes"
then
	CFLAGS="$CFLAGS -Wall -Werror"
fi

AC_CACHE_CHECK([for strtok_r],
  [c_cv_have_strtok_r_default],
  AC_LINK_IFELSE(
    [AC_LANG_PROGRAM(
[[[
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
]]],
[[[
      char buffer[] = "foo,bar,baz";
      char *token;
      char *dummy;
      char *saveptr;

      dummy = buffer;
      saveptr = NULL;
      while ((token = strtok_r (dummy, ",", &saveptr)) != NULL)
      {
        dummy = NULL;
        printf ("token = %s;\n", token);
      }
]]]
    )],
    [c_cv_have_strtok_r_default="yes"],
    [c_cv_have_strtok_r_default="no"]
  )
)

if test "x$c_cv_have_strtok_r_default" = "xno"
then
  CFLAGS="$CFLAGS -D_REENTRANT=1"

  AC_CACHE_CHECK([if strtok_r needs _REENTRANT],
    [c_cv_have_strtok_r_reentrant],
    AC_LINK_IFELSE(
      [AC_LANG_PROGRAM(
[[[
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
]]],
[[[
        char buffer[] = "foo,bar,baz";
        char *token;
        char *dummy;
        char *saveptr;

        dummy = buffer;
        saveptr = NULL;
        while ((token = strtok_r (dummy, ",", &saveptr)) != NULL)
        {
          dummy = NULL;
          printf ("token = %s;\n", token);
        }
]]]
      )],
      [c_cv_have_strtok_r_reentrant="yes"],
      [AC_MSG_FAILURE([strtok_r isn't available. Please file a bugreport!])]
    )
  )
fi

CFLAGS="$SAVE_CFLAGS"
if test "x$c_cv_have_strtok_r_reentrant" = "xyes"
then
	CFLAGS="$CFLAGS -D_REENTRANT=1"
fi

AC_CHECK_FUNCS(getpwnam_r getgrnam_r setgroups regcomp regerror regexec regfree)

socket_needs_socket="no"
AC_CHECK_FUNCS(socket, [], AC_CHECK_LIB(socket, socket, [socket_needs_socket="yes"], AC_MSG_ERROR(cannot find socket)))
AM_CONDITIONAL(BUILD_WITH_LIBSOCKET, test "x$socket_needs_socket" = "xyes")

clock_gettime_needs_rt="no"
clock_gettime_needs_posix4="no"
have_clock_gettime="no"
AC_CHECK_FUNCS(clock_gettime, [have_clock_gettime="yes"])
if test "x$have_clock_gettime" = "xno"
then
	AC_CHECK_LIB(rt, clock_gettime, [clock_gettime_needs_rt="yes"
					 have_clock_gettime="yes"])
fi
if test "x$have_clock_gettime" = "xno"
then
	AC_CHECK_LIB(posix4, clock_gettime, [clock_gettime_needs_posix4="yes"
					     have_clock_gettime="yes"])
fi
if test "x$have_clock_gettime" = "xyes"
then
	AC_DEFINE(HAVE_CLOCK_GETTIME, 1, [Define if the clock_gettime(2) function is available.])
else
	AC_MSG_WARN(cannot find clock_gettime)
fi

nanosleep_needs_rt="no"
nanosleep_needs_posix4="no"
AC_CHECK_FUNCS(nanosleep,
    [],
    AC_CHECK_LIB(rt, nanosleep,
        [nanosleep_needs_rt="yes"],
        AC_CHECK_LIB(posix4, nanosleep,
            [nanosleep_needs_posix4="yes"],
            AC_MSG_ERROR(cannot find nanosleep))))

AM_CONDITIONAL(BUILD_WITH_LIBRT, test "x$clock_gettime_needs_rt" = "xyes" || test "x$nanosleep_needs_rt" = "xyes")
AM_CONDITIONAL(BUILD_WITH_LIBPOSIX4, test "x$clock_gettime_needs_posix4" = "xyes" || test "x$nanosleep_needs_posix4" = "xyes")

AC_CHECK_FUNCS(sysctl, [have_sysctl="yes"], [have_sysctl="no"])
AC_CHECK_FUNCS(sysctlbyname, [have_sysctlbyname="yes"], [have_sysctlbyname="no"])
AC_CHECK_FUNCS(host_statistics, [have_host_statistics="yes"], [have_host_statistics="no"])
AC_CHECK_FUNCS(processor_info, [have_processor_info="yes"], [have_processor_info="no"])
AC_CHECK_FUNCS(thread_info, [have_thread_info="yes"], [have_thread_info="no"])
AC_CHECK_FUNCS(statfs, [have_statfs="yes"], [have_statfs="no"])
AC_CHECK_FUNCS(statvfs, [have_statvfs="yes"], [have_statvfs="no"])
AC_CHECK_FUNCS(getifaddrs, [have_getifaddrs="yes"], [have_getifaddrs="no"])
AC_CHECK_FUNCS(getloadavg, [have_getloadavg="yes"], [have_getloadavg="no"])
AC_CHECK_FUNCS(syslog, [have_syslog="yes"], [have_syslog="no"])
AC_CHECK_FUNCS(getutent, [have_getutent="yes"], [have_getutent="no"])
AC_CHECK_FUNCS(getutxent, [have_getutxent="yes"], [have_getutxent="no"])

# Check for strptime {{{
if test "x$GCC" = "xyes"
then
	SAVE_CFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS -Wall -Wextra -Werror"
fi

AC_CHECK_FUNCS(strptime, [have_strptime="yes"], [have_strptime="no"])
if test "x$have_strptime" = "xyes"
then
	AC_CACHE_CHECK([whether strptime is exported by default],
		       [c_cv_have_strptime_default],
		       AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[[
#include <time.h>
]]],
[[[
 struct tm stm;
 (void) strptime ("2010-12-30%13:42:42", "%Y-%m-%dT%T", &stm);
]]]
		       )],
		       [c_cv_have_strptime_default="yes"],
		       [c_cv_have_strptime_default="no"]))
fi
if test "x$have_strptime" = "xyes" && test "x$c_cv_have_strptime_default" = "xno"
then
	AC_CACHE_CHECK([whether strptime needs standards mode],
		       [c_cv_have_strptime_standards],
		       AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[[
#ifndef _ISOC99_SOURCE
# define _ISOC99_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 200112L
#endif
#ifndef _XOPEN_SOURCE
# define _XOPEN_SOURCE 500
#endif
#include <time.h>
]]],
[[[
 struct tm stm;
 (void) strptime ("2010-12-30%13:42:42", "%Y-%m-%dT%T", &stm);
]]]
		       )],
		       [c_cv_have_strptime_standards="yes"],
		       [c_cv_have_strptime_standards="no"]))

	if test "x$c_cv_have_strptime_standards" = "xyes"
	then
		AC_DEFINE([STRPTIME_NEEDS_STANDARDS], 1, [Set to true if strptime is only exported in X/Open mode (GNU libc).])
	else
		have_strptime="no"
	fi
fi

if test "x$GCC" = "xyes"
then
	CFLAGS="$SAVE_CFLAGS"
fi
# }}} Check for strptime

AC_CHECK_FUNCS(swapctl, [have_swapctl="yes"], [have_swapctl="no"])
if test "x$have_swapctl" = "xyes"; then
        AC_CACHE_CHECK([whether swapctl takes two arguments],
                [c_cv_have_swapctl_two_args],
                AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[[
#if HAVE_SYS_SWAP_H && !defined(_LP64) && _FILE_OFFSET_BITS == 64
#  undef _FILE_OFFSET_BITS
#  undef _LARGEFILE64_SOURCE
#endif
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/swap.h>
#include <unistd.h>
]]],
[[[
int num = swapctl(0, NULL);
]]]
                        )],
                        [c_cv_have_swapctl_two_args="yes"],
                        [c_cv_have_swapctl_two_args="no"]
                )
        )
        AC_CACHE_CHECK([whether swapctl takes three arguments],
                [c_cv_have_swapctl_three_args],
                AC_COMPILE_IFELSE(
                        [AC_LANG_PROGRAM(
[[[
#if HAVE_SYS_SWAP_H && !defined(_LP64) && _FILE_OFFSET_BITS == 64
#  undef _FILE_OFFSET_BITS
#  undef _LARGEFILE64_SOURCE
#endif
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/swap.h>
#include <unistd.h>
]]],
[[[
int num = swapctl(0, NULL, 0);
]]]
                        )],
                        [c_cv_have_swapctl_three_args="yes"],
                        [c_cv_have_swapctl_three_args="no"]
                )
        )
fi
# Check for different versions of `swapctl' here..
if test "x$have_swapctl" = "xyes"; then
        if test "x$c_cv_have_swapctl_two_args" = "xyes"; then
                AC_DEFINE(HAVE_SWAPCTL_TWO_ARGS, 1,
                          [Define if the function swapctl exists and takes two arguments.])
        fi
        if test "x$c_cv_have_swapctl_three_args" = "xyes"; then
                AC_DEFINE(HAVE_SWAPCTL_THREE_ARGS, 1,
                          [Define if the function swapctl exists and takes three arguments.])
        fi
fi

# Check for NAN
AC_ARG_WITH(nan-emulation, [AS_HELP_STRING([--with-nan-emulation], [use emulated NAN. For crosscompiling only.])],
[
 if test "x$withval" = "xno"; then
	 nan_type="none"
 else if test "x$withval" = "xyes"; then
	 nan_type="zero"
 else
	 nan_type="$withval"
 fi; fi
],
[nan_type="none"])
if test "x$nan_type" = "xnone"; then
  AC_CACHE_CHECK([whether NAN is defined by default],
    [c_cv_have_nan_default],
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[[
#include <stdlib.h>
#include <math.h>
static double foo = NAN;
]]],
[[[
       if (isnan (foo))
        return 0;
       else
	return 1;
]]]
      )],
      [c_cv_have_nan_default="yes"],
      [c_cv_have_nan_default="no"]
    )
  )
  if test "x$c_cv_have_nan_default" = "xyes"
  then
    nan_type="default"
  fi
fi
if test "x$nan_type" = "xnone"; then
  AC_CACHE_CHECK([whether NAN is defined by __USE_ISOC99],
    [c_cv_have_nan_isoc],
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[[
#include <stdlib.h>
#define __USE_ISOC99 1
#include <math.h>
static double foo = NAN;
]]],
[[[
       if (isnan (foo))
        return 0;
       else
	return 1;
]]]
      )],
      [c_cv_have_nan_isoc="yes"],
      [c_cv_have_nan_isoc="no"]
    )
  )
  if test "x$c_cv_have_nan_isoc" = "xyes"
  then
    nan_type="isoc99"
  fi
fi
if test "x$nan_type" = "xnone"; then
  SAVE_LDFLAGS=$LDFLAGS
  LDFLAGS="$LDFLAGS -lm"
  AC_CACHE_CHECK([whether NAN can be defined by 0/0],
    [c_cv_have_nan_zero],
    AC_RUN_IFELSE([AC_LANG_PROGRAM(
[[[
#include <stdlib.h>
#include <math.h>
#ifdef NAN
# undef NAN
#endif
#define NAN (0.0 / 0.0)
#ifndef isnan
# define isnan(f) ((f) != (f))
#endif
static double foo = NAN;
]]],
[[[
       if (isnan (foo))
        return 0;
       else
	return 1;
]]]
      )],
      [c_cv_have_nan_zero="yes"],
      [c_cv_have_nan_zero="no"]
    )
  )
  LDFLAGS=$SAVE_LDFLAGS
  if test "x$c_cv_have_nan_zero" = "xyes"
  then
    nan_type="zero"
  fi
fi

if test "x$nan_type" = "xdefault"; then
  AC_DEFINE(NAN_STATIC_DEFAULT, 1,
    [Define if NAN is defined by default and can initialize static variables.])
else if test "x$nan_type" = "xisoc99"; then
  AC_DEFINE(NAN_STATIC_ISOC, 1,
    [Define if NAN is defined by __USE_ISOC99 and can initialize static variables.])
else if test "x$nan_type" = "xzero"; then
  AC_DEFINE(NAN_ZERO_ZERO, 1,
    [Define if NAN can be defined as (0.0 / 0.0)])
else
  AC_MSG_ERROR([Didn't find out how to statically initialize variables to NAN. Sorry.])
fi; fi; fi

AC_ARG_WITH(fp-layout, [AS_HELP_STRING([--with-fp-layout], [set the memory layout of doubles. For crosscompiling only.])],
[
 if test "x$withval" = "xnothing"; then
 	fp_layout_type="nothing"
 else if test "x$withval" = "xendianflip"; then
 	fp_layout_type="endianflip"
 else if test "x$withval" = "xintswap"; then
 	fp_layout_type="intswap"
 else
 	AC_MSG_ERROR([Invalid argument for --with-fp-layout. Valid arguments are: nothing, endianflip, intswap]);
fi; fi; fi
],
[fp_layout_type="unknown"])

if test "x$fp_layout_type" = "xunknown"; then
  AC_CACHE_CHECK([if doubles are stored in x86 representation],
    [c_cv_fp_layout_need_nothing],
    AC_RUN_IFELSE([AC_LANG_PROGRAM(
[[[
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_INTTYPES_H
# include <inttypes.h>
#endif
#if HAVE_STDBOOL_H
# include <stdbool.h>
#endif
]]],
[[[
	uint64_t i0;
	uint64_t i1;
	uint8_t c[8];
	double d;

	d = 8.642135e130;
	memcpy ((void *) &i0, (void *) &d, 8);

	i1 = i0;
	memcpy ((void *) c, (void *) &i1, 8);

	if ((c[0] == 0x2f) && (c[1] == 0x25)
			&& (c[2] == 0xc0) && (c[3] == 0xc7)
			&& (c[4] == 0x43) && (c[5] == 0x2b)
			&& (c[6] == 0x1f) && (c[7] == 0x5b))
		return (0);
	else
		return (1);
]]]
      )],
      [c_cv_fp_layout_need_nothing="yes"],
      [c_cv_fp_layout_need_nothing="no"]
    )
  )
  if test "x$c_cv_fp_layout_need_nothing" = "xyes"; then
    fp_layout_type="nothing"
  fi
fi
if test "x$fp_layout_type" = "xunknown"; then
  AC_CACHE_CHECK([if endianflip converts to x86 representation],
    [c_cv_fp_layout_need_endianflip],
    AC_RUN_IFELSE([AC_LANG_PROGRAM(
[[[
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_INTTYPES_H
# include <inttypes.h>
#endif
#if HAVE_STDBOOL_H
# include <stdbool.h>
#endif
#define endianflip(A) ((((uint64_t)(A) & 0xff00000000000000LL) >> 56) | \
                       (((uint64_t)(A) & 0x00ff000000000000LL) >> 40) | \
                       (((uint64_t)(A) & 0x0000ff0000000000LL) >> 24) | \
                       (((uint64_t)(A) & 0x000000ff00000000LL) >> 8)  | \
                       (((uint64_t)(A) & 0x00000000ff000000LL) << 8)  | \
                       (((uint64_t)(A) & 0x0000000000ff0000LL) << 24) | \
                       (((uint64_t)(A) & 0x000000000000ff00LL) << 40) | \
                       (((uint64_t)(A) & 0x00000000000000ffLL) << 56))
]]],
[[[
	uint64_t i0;
	uint64_t i1;
	uint8_t c[8];
	double d;

	d = 8.642135e130;
	memcpy ((void *) &i0, (void *) &d, 8);

	i1 = endianflip (i0);
	memcpy ((void *) c, (void *) &i1, 8);

	if ((c[0] == 0x2f) && (c[1] == 0x25)
			&& (c[2] == 0xc0) && (c[3] == 0xc7)
			&& (c[4] == 0x43) && (c[5] == 0x2b)
			&& (c[6] == 0x1f) && (c[7] == 0x5b))
		return (0);
	else
		return (1);
]]]
      )],
      [c_cv_fp_layout_need_endianflip="yes"],
      [c_cv_fp_layout_need_endianflip="no"]
    )
  )
  if test "x$c_cv_fp_layout_need_endianflip" = "xyes"; then
    fp_layout_type="endianflip"
  fi
fi
if test "x$fp_layout_type" = "xunknown"; then
  AC_CACHE_CHECK([if intswap converts to x86 representation],
    [c_cv_fp_layout_need_intswap],
    AC_RUN_IFELSE([AC_LANG_PROGRAM(
[[[
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_INTTYPES_H
# include <inttypes.h>
#endif
#if HAVE_STDBOOL_H
# include <stdbool.h>
#endif
#define intswap(A)    ((((uint64_t)(A) & 0xffffffff00000000LL) >> 32) | \
                       (((uint64_t)(A) & 0x00000000ffffffffLL) << 32))
]]],
[[[
	uint64_t i0;
	uint64_t i1;
	uint8_t c[8];
	double d;

	d = 8.642135e130;
	memcpy ((void *) &i0, (void *) &d, 8);

	i1 = intswap (i0);
	memcpy ((void *) c, (void *) &i1, 8);

	if ((c[0] == 0x2f) && (c[1] == 0x25)
			&& (c[2] == 0xc0) && (c[3] == 0xc7)
			&& (c[4] == 0x43) && (c[5] == 0x2b)
			&& (c[6] == 0x1f) && (c[7] == 0x5b))
		return (0);
	else
		return (1);
]]]
      )],
      [c_cv_fp_layout_need_intswap="yes"],
      [c_cv_fp_layout_need_intswap="no"]
    )
  )
  if test "x$c_cv_fp_layout_need_intswap" = "xyes"; then
    fp_layout_type="intswap"
  fi
fi

if test "x$fp_layout_type" = "xnothing"; then
  AC_DEFINE(FP_LAYOUT_NEED_NOTHING, 1,
  [Define if doubles are stored in x86 representation.])
else if test "x$fp_layout_type" = "xendianflip"; then
  AC_DEFINE(FP_LAYOUT_NEED_ENDIANFLIP, 1,
  [Define if endianflip is needed to convert to x86 representation.])
else if test "x$fp_layout_type" = "xintswap"; then
  AC_DEFINE(FP_LAYOUT_NEED_INTSWAP, 1,
  [Define if intswap is needed to convert to x86 representation.])
else
  AC_MSG_ERROR([Didn't find out how doubles are stored in memory. Sorry.])
fi; fi; fi

# --with-useragent {{{
AC_ARG_WITH(useragent, [AS_HELP_STRING([--with-useragent@<:@=AGENT@:>@], [User agent to use on http requests])],
[
    if test "x$withval" != "xno" && test "x$withval" != "xyes"
    then
        AC_DEFINE_UNQUOTED(COLLECTD_USERAGENT, ["$withval"], [User agent for http requests])
    fi
])

# }}}

have_getfsstat="no"
AC_CHECK_FUNCS(getfsstat, [have_getfsstat="yes"])
have_getvfsstat="no"
AC_CHECK_FUNCS(getvfsstat, [have_getvfsstat="yes"])
have_listmntent="no"
AC_CHECK_FUNCS(listmntent, [have_listmntent="yes"])

have_getmntent="no"
AC_CHECK_FUNCS(getmntent, [have_getmntent="c"])
if test "x$have_getmntent" = "xno"; then
	AC_CHECK_LIB(sun, getmntent, [have_getmntent="sun"])
fi
if test "x$have_getmntent" = "xno"; then
	AC_CHECK_LIB(seq, getmntent, [have_getmntent="seq"])
fi
if test "x$have_getmntent" = "xno"; then
	AC_CHECK_LIB(gen, getmntent, [have_getmntent="gen"])
fi

if test "x$have_getmntent" = "xc"; then
	AC_CACHE_CHECK([whether getmntent takes one argument],
		[c_cv_have_one_getmntent],
		AC_COMPILE_IFELSE(
			[AC_LANG_PROGRAM(
[[[
#include "$srcdir/src/utils_mount.h"
]]],
[[[
FILE *fh;
struct mntent *me;
fh = setmntent ("/etc/mtab", "r");
me = getmntent (fh);
return(me->mnt_passno);
]]]
			)],
			[c_cv_have_one_getmntent="yes"],
			[c_cv_have_one_getmntent="no"]
		)
	)
	AC_CACHE_CHECK([whether getmntent takes two arguments],
		[c_cv_have_two_getmntent],
		AC_COMPILE_IFELSE(
			[AC_LANG_PROGRAM(
[[[
#include "$srcdir/src/utils_mount.h"
]]],
[[[
				 FILE *fh;
				 struct mnttab mt;
				 int status;
				 fh = fopen ("/etc/mnttab", "r");
				 status = getmntent (fh, &mt);
				 return(status);
]]]
			)],
			[c_cv_have_two_getmntent="yes"],
			[c_cv_have_two_getmntent="no"]
		)
	)
fi

# Check for different versions of `getmntent' here..

if test "x$have_getmntent" = "xc"; then
	if test "x$c_cv_have_one_getmntent" = "xyes"; then
		AC_DEFINE(HAVE_ONE_GETMNTENT, 1,
			  [Define if the function getmntent exists and takes one argument.])
	fi
	if test "x$c_cv_have_two_getmntent" = "xyes"; then
		AC_DEFINE(HAVE_TWO_GETMNTENT, 1,
			  [Define if the function getmntent exists and takes two arguments.])
	fi
fi
if test "x$have_getmntent" = "xsun"; then
	AC_DEFINE(HAVE_SUN_GETMNTENT, 1,
		  [Define if the function getmntent exists. It's the version from libsun.])
fi
if test "x$have_getmntent" = "xseq"; then
	AC_DEFINE(HAVE_SEQ_GETMNTENT, 1,
		  [Define if the function getmntent exists. It's the version from libseq.])
fi
if test "x$have_getmntent" = "xgen"; then
	AC_DEFINE(HAVE_GEN_GETMNTENT, 1,
		  [Define if the function getmntent exists. It's the version from libgen.])
fi

# Check for htonll
AC_CACHE_CHECK([if have htonll defined],
                  [c_cv_have_htonll],
                  AC_LINK_IFELSE([AC_LANG_PROGRAM(
[[[
#include <sys/types.h>
#include <netinet/in.h>
#if HAVE_INTTYPES_H
# include <inttypes.h>
#endif
]]],
[[[
          return htonll(0);
]]]
    )],
    [c_cv_have_htonll="yes"],
    [c_cv_have_htonll="no"]
  )
)
if test "x$c_cv_have_htonll" = "xyes"
then
    AC_DEFINE(HAVE_HTONLL, 1, [Define if the function htonll exists.])
fi

# Check for structures
AC_CHECK_MEMBERS([struct if_data.ifi_ibytes, struct if_data.ifi_opackets, struct if_data.ifi_ierrors],
	[AC_DEFINE(HAVE_STRUCT_IF_DATA, 1, [Define if struct if_data exists and is usable.])],
	[],
	[
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <net/if.h>
	])
AC_CHECK_MEMBERS([struct net_device_stats.rx_bytes, struct net_device_stats.tx_packets, struct net_device_stats.rx_errors],
	[AC_DEFINE(HAVE_STRUCT_NET_DEVICE_STATS, 1, [Define if struct net_device_stats exists and is usable.])],
	[],
	[
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <linux/if.h>
	#include <linux/netdevice.h>
	])
AC_CHECK_MEMBERS([struct inet_diag_req.id, struct inet_diag_req.idiag_states],
	[AC_DEFINE(HAVE_STRUCT_LINUX_INET_DIAG_REQ, 1, [Define if struct inet_diag_req exists and is usable.])],
	[],
	[
	#include <linux/inet_diag.h>
	])


AC_CHECK_MEMBERS([struct ip_mreqn.imr_ifindex], [],
	[],
	[
	#include <netinet/in.h>
	#include <net/if.h>
	])

AC_CHECK_MEMBERS([struct kinfo_proc.ki_pid, struct kinfo_proc.ki_rssize, struct kinfo_proc.ki_rusage],
	[
		AC_DEFINE(HAVE_STRUCT_KINFO_PROC_FREEBSD, 1,
			[Define if struct kinfo_proc exists in the FreeBSD variant.])
		have_struct_kinfo_proc_freebsd="yes"
	],
	[
		have_struct_kinfo_proc_freebsd="no"
	],
	[
#include <kvm.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
	])

AC_CHECK_MEMBERS([struct kinfo_proc.p_pid, struct kinfo_proc.p_vm_rssize],
	[
		AC_DEFINE(HAVE_STRUCT_KINFO_PROC_OPENBSD, 1,
			[Define if struct kinfo_proc exists in the OpenBSD variant.])
		have_struct_kinfo_proc_openbsd="yes"
	],
	[
		have_struct_kinfo_proc_openbsd="no"
	],
	[
#include <sys/param.h>
#include <sys/sysctl.h>
#include <kvm.h>
	])

AC_CHECK_MEMBERS([struct udphdr.uh_dport, struct udphdr.uh_sport], [], [],
[#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#if HAVE_NETINET_IP_H
# include <netinet/ip.h>
#endif
#if HAVE_NETINET_UDP_H
# include <netinet/udp.h>
#endif
])
AC_CHECK_MEMBERS([struct udphdr.dest, struct udphdr.source], [], [],
[#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#if HAVE_NETINET_IP_H
# include <netinet/ip.h>
#endif
#if HAVE_NETINET_UDP_H
# include <netinet/udp.h>
#endif
])

AC_CHECK_MEMBERS([kstat_io_t.nwritten, kstat_io_t.writes, kstat_io_t.nwrites, kstat_io_t.wtime],
	[],
	[],
	[
#if HAVE_KSTAT_H
# include <kstat.h>
#endif
	])

#
# Checks for libraries begin here
#

with_libresolv="yes"
AC_CHECK_LIB(resolv, res_search,
[
	AC_DEFINE(HAVE_LIBRESOLV, 1, [Define to 1 if you have the 'resolv' library (-lresolv).])
],
[with_libresolv="no"])
AM_CONDITIONAL(BUILD_WITH_LIBRESOLV, test "x$with_libresolv" = "xyes")

dnl Check for HAL (hardware abstraction library)
with_libhal="yes"
AC_CHECK_LIB(hal,libhal_device_property_exists,
	     [AC_DEFINE(HAVE_LIBHAL, 1, [Define to 1 if you have 'hal' library])],
	     [with_libhal="no"])
if test "x$with_libhal" = "xyes"; then
	if test "x$PKG_CONFIG" != "x"; then
		BUILD_WITH_LIBHAL_CFLAGS="`$PKG_CONFIG --cflags hal`"
		BUILD_WITH_LIBHAL_LIBS="`$PKG_CONFIG --libs hal`"
		AC_SUBST(BUILD_WITH_LIBHAL_CFLAGS)
		AC_SUBST(BUILD_WITH_LIBHAL_LIBS)
	fi
fi

m4_divert_once([HELP_WITH], [
collectd additional packages:])

if test "x$ac_system" = "xAIX"
then
	with_perfstat="yes"
	with_procinfo="yes"
else
	with_perfstat="no (AIX only)"
	with_procinfo="no (AIX only)"
fi

if test "x$with_perfstat" = "xyes"
then
	AC_CHECK_LIB(perfstat, perfstat_reset, [with_perfstat="yes"], [with_perfstat="no (perfstat not found)"], [])
#	AC_CHECK_HEADERS(sys/protosw.h libperfstat.h,, [with_perfstat="no (perfstat not found)"])
fi
if test "x$with_perfstat" = "xyes"
then
	 AC_DEFINE(HAVE_PERFSTAT, 1, [Define to 1 if you have the 'perfstat' library (-lperfstat)])
	 # struct members pertaining to donation have been added to libperfstat somewhere between AIX5.3ML5 and AIX5.3ML9
	 AC_CHECK_MEMBER([perfstat_partition_type_t.b.donate_enabled], [], [], [[#include <libperfstat.h]])
	 if test "x$av_cv_member_perfstat_partition_type_t_b_donate_enabled" = "xyes"
	 then
		AC_DEFINE(PERFSTAT_SUPPORTS_DONATION, 1, [Define to 1 if your version of the 'perfstat' library supports donation])
	 fi
fi
AM_CONDITIONAL(BUILD_WITH_PERFSTAT, test "x$with_perfstat" = "xyes")

# Processes plugin under AIX.
if test "x$with_procinfo" = "xyes"
then
	AC_CHECK_HEADERS(procinfo.h,, [with_procinfo="no (procinfo.h not found)"])
fi
if test "x$with_procinfo" = "xyes"
then
	 AC_DEFINE(HAVE_PROCINFO_H, 1, [Define to 1 if you have the procinfo.h])
fi

if test "x$ac_system" = "xSolaris"
then
	with_kstat="yes"
	with_devinfo="yes"
else
	with_kstat="no (Solaris only)"
	with_devinfo="no (Solaris only)"
fi

if test "x$with_kstat" = "xyes"
then
	AC_CHECK_LIB(kstat, kstat_open, [with_kstat="yes"], [with_kstat="no (libkstat not found)"], [])
fi
if test "x$with_kstat" = "xyes"
then
	AC_CHECK_LIB(devinfo, di_init, [with_devinfo="yes"], [with_devinfo="no (not found)"], [])
	AC_CHECK_HEADERS(kstat.h,, [with_kstat="no (kstat.h not found)"])
fi
if test "x$with_kstat" = "xyes"
then
	AC_DEFINE(HAVE_LIBKSTAT, 1,
		  [Define to 1 if you have the 'kstat' library (-lkstat)])
fi
AM_CONDITIONAL(BUILD_WITH_LIBKSTAT, test "x$with_kstat" = "xyes")
AM_CONDITIONAL(BUILD_WITH_LIBDEVINFO, test "x$with_devinfo" = "xyes")

with_libiokit="no"
if test "x$ac_system" = "xDarwin"
then
	with_libiokit="yes"
else
	with_libiokit="no"
fi
AM_CONDITIONAL(BUILD_WITH_LIBIOKIT, test "x$with_libiokit" = "xyes")

with_libkvm="no"
AC_CHECK_LIB(kvm, kvm_getprocs, [with_kvm_getprocs="yes"], [with_kvm_getprocs="no"])
if test "x$with_kvm_getprocs" = "xyes"
then
	AC_DEFINE(HAVE_LIBKVM_GETPROCS, 1,
		  [Define to 1 if you have the 'kvm' library with the 'kvm_getprocs' symbol (-lkvm)])
	with_libkvm="yes"
fi
AM_CONDITIONAL(BUILD_WITH_LIBKVM_GETPROCS, test "x$with_kvm_getprocs" = "xyes")

AC_CHECK_LIB(kvm, kvm_getswapinfo, [with_kvm_getswapinfo="yes"], [with_kvm_getswapinfo="no"])
if test "x$with_kvm_getswapinfo" = "xyes"
then
	AC_DEFINE(HAVE_LIBKVM_GETSWAPINFO, 1,
		  [Define to 1 if you have the 'kvm' library with the 'kvm_getswapinfo' symbol (-lkvm)])
	with_libkvm="yes"
fi
AM_CONDITIONAL(BUILD_WITH_LIBKVM_GETSWAPINFO, test "x$with_kvm_getswapinfo" = "xyes")

AC_CHECK_LIB(kvm, kvm_nlist, [with_kvm_nlist="yes"], [with_kvm_nlist="no"])
if test "x$with_kvm_nlist" = "xyes"
then
	AC_CHECK_HEADERS(bsd/nlist.h nlist.h)
	AC_DEFINE(HAVE_LIBKVM_NLIST, 1,
		  [Define to 1 if you have the 'kvm' library with the 'kvm_nlist' symbol (-lkvm)])
	with_libkvm="yes"
fi
AM_CONDITIONAL(BUILD_WITH_LIBKVM_NLIST, test "x$with_kvm_nlist" = "xyes")

AC_CHECK_LIB(kvm, kvm_openfiles, [with_kvm_openfiles="yes"], [with_kvm_openfiles="no"])
if test "x$with_kvm_openfiles" = "xyes"
then
	AC_DEFINE(HAVE_LIBKVM_NLIST, 1,
		  [Define to 1 if you have the 'kvm' library with the 'kvm_openfiles' symbol (-lkvm)])
	with_libkvm="yes"
fi
AM_CONDITIONAL(BUILD_WITH_LIBKVM_OPENFILES, test "x$with_kvm_openfiles" = "xyes")

# --with-libaquaero5 {{{
AC_ARG_WITH(libaquaero5, [AS_HELP_STRING([--with-libaquaero5@<:@=PREFIX@:>@], [Path to aquatools-ng source code.])],
[
 if test "x$withval" = "xyes"
 then
	 with_libaquaero5="yes"
 else if test "x$withval" = "xno"
 then
	 with_libaquaero5="no"
 else
	 with_libaquaero5="yes"
	 LIBAQUAERO5_CFLAGS="$LIBAQUAERO5_CFLAGS -I$withval/src"
	 LIBAQUAERO5_LDFLAGS="$LIBAQUAERO5_LDFLAGS -L$withval/obj"
 fi; fi
],
[with_libaquaero5="yes"])

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"

CPPFLAGS="$CPPFLAGS $LIBAQUAERO5_CFLAGS"
LDFLAGS="$LDFLAGS $LIBAQUAERO5_LDFLAGS"

if test "x$with_libaquaero5" = "xyes"
then
	if test "x$LIBAQUAERO5_CFLAGS" != "x"
	then
		AC_MSG_NOTICE([libaquaero5 CPPFLAGS: $LIBAQUAERO5_CFLAGS])
	fi
	AC_CHECK_HEADERS(libaquaero5.h,
	[with_libaquaero5="yes"],
	[with_libaquaero5="no (libaquaero5.h not found)"])
fi
if test "x$with_libaquaero5" = "xyes"
then
	if test "x$LIBAQUAERO5_LDFLAGS" != "x"
	then
		AC_MSG_NOTICE([libaquaero5 LDFLAGS: $LIBAQUAERO5_LDFLAGS])
	fi
	AC_CHECK_LIB(aquaero5, libaquaero5_poll,
	[with_libaquaero5="yes"],
	[with_libaquaero5="no (symbol 'libaquaero5_poll' not found)"])
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

if test "x$with_libaquaero5" = "xyes"
then
	BUILD_WITH_LIBAQUAERO5_CFLAGS="$LIBAQUAERO5_CFLAGS"
	BUILD_WITH_LIBAQUAERO5_LDFLAGS="$LIBAQUAERO5_LDFLAGS"
	AC_SUBST(BUILD_WITH_LIBAQUAERO5_CFLAGS)
	AC_SUBST(BUILD_WITH_LIBAQUAERO5_LDFLAGS)
fi
AM_CONDITIONAL(BUILD_WITH_LIBAQUAERO5, test "x$with_libaquaero5" = "xyes")
# }}}

# --with-libhiredis {{{
AC_ARG_WITH(libhiredis, [AS_HELP_STRING([--with-libhiredis@<:@=PREFIX@:>@],
      [Path to libhiredis.])],
[
 if test "x$withval" = "xyes"
 then
	 with_libhiredis="yes"
 else if test "x$withval" = "xno"
 then
	 with_libhiredis="no"
 else
	 with_libhiredis="yes"
	 LIBHIREDIS_CPPFLAGS="$LIBHIREDIS_CPPFLAGS -I$withval/include"
	 LIBHIREDIS_LDFLAGS="$LIBHIREDIS_LDFLAGS -L$withval/lib"
 fi; fi
],
[with_libhiredis="yes"])

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"

CPPFLAGS="$CPPFLAGS $LIBHIREDIS_CPPFLAGS"
LDFLAGS="$LDFLAGS $LIBHIREDIS_LDFLAGS"

if test "x$with_libhiredis" = "xyes"
then
	if test "x$LIBHIREDIS_CPPFLAGS" != "x"
	then
		AC_MSG_NOTICE([libhiredis CPPFLAGS: $LIBHIREDIS_CPPFLAGS])
	fi
	AC_CHECK_HEADERS(hiredis/hiredis.h,
	[with_libhiredis="yes"],
	[with_libhiredis="no (hiredis.h not found)"])
fi
if test "x$with_libhiredis" = "xyes"
then
	if test "x$LIBHIREDIS_LDFLAGS" != "x"
	then
		AC_MSG_NOTICE([libhiredis LDFLAGS: $LIBHIREDIS_LDFLAGS])
	fi
	AC_CHECK_LIB(hiredis, redisCommand,
	[with_libhiredis="yes"],
	[with_libhiredis="no (symbol 'redisCommand' not found)"])

fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

if test "x$with_libhiredis" = "xyes"
then
	BUILD_WITH_LIBHIREDIS_CPPFLAGS="$LIBHIREDIS_CPPFLAGS"
	BUILD_WITH_LIBHIREDIS_LDFLAGS="$LIBHIREDIS_LDFLAGS"
	AC_SUBST(BUILD_WITH_LIBHIREDIS_CPPFLAGS)
	AC_SUBST(BUILD_WITH_LIBHIREDIS_LDFLAGS)
fi
AM_CONDITIONAL(BUILD_WITH_LIBHIREDIS, test "x$with_libhiredis" = "xyes")
# }}}

# --with-libcurl {{{
with_curl_config="curl-config"
with_curl_cflags=""
with_curl_libs=""
AC_ARG_WITH(libcurl, [AS_HELP_STRING([--with-libcurl@<:@=PREFIX@:>@], [Path to libcurl.])],
[
	if test "x$withval" = "xno"
	then
		with_libcurl="no"
	else if test "x$withval" = "xyes"
	then
		with_libcurl="yes"
	else
		if test -f "$withval" && test -x "$withval"
		then
			with_curl_config="$withval"
			with_libcurl="yes"
		else if test -x "$withval/bin/curl-config"
		then
			with_curl_config="$withval/bin/curl-config"
			with_libcurl="yes"
		fi; fi
		with_libcurl="yes"
	fi; fi
],
[
	with_libcurl="yes"
])
if test "x$with_libcurl" = "xyes"
then
	with_curl_cflags=`$with_curl_config --cflags 2>/dev/null`
	curl_config_status=$?

	if test $curl_config_status -ne 0
	then
		with_libcurl="no ($with_curl_config failed)"
	else
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_curl_cflags"

		AC_CHECK_HEADERS(curl/curl.h, [], [with_libcurl="no (curl/curl.h not found)"], [])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
fi
if test "x$with_libcurl" = "xyes"
then
	with_curl_libs=`$with_curl_config --libs 2>/dev/null`
	curl_config_status=$?

	if test $curl_config_status -ne 0
	then
		with_libcurl="no ($with_curl_config failed)"
	else
		AC_CHECK_LIB(curl, curl_easy_init,
		 [with_libcurl="yes"],
		 [with_libcurl="no (symbol 'curl_easy_init' not found)"],
		 [$with_curl_libs])
		AC_CHECK_DECL(CURLOPT_USERNAME,
		 [have_curlopt_username="yes"],
		 [have_curlopt_username="no"],
		 [[#include <curl/curl.h>]])
		AC_CHECK_DECL(CURLOPT_TIMEOUT_MS,
		 [have_curlopt_timeout="yes"],
		 [have_curlopt_timeout="no"],
		 [[#include <curl/curl.h>]])
	fi
fi
if test "x$with_libcurl" = "xyes"
then
	BUILD_WITH_LIBCURL_CFLAGS="$with_curl_cflags"
	BUILD_WITH_LIBCURL_LIBS="$with_curl_libs"
	AC_SUBST(BUILD_WITH_LIBCURL_CFLAGS)
	AC_SUBST(BUILD_WITH_LIBCURL_LIBS)

	if test "x$have_curlopt_username" = "xyes"
	then
		AC_DEFINE(HAVE_CURLOPT_USERNAME, 1, [Define if libcurl supports CURLOPT_USERNAME option.])
	fi

	if test "x$have_curlopt_timeout" = "xyes"
	then
		AC_DEFINE(HAVE_CURLOPT_TIMEOUT_MS, 1, [Define if libcurl supports CURLOPT_TIMEOUT_MS option.])
	fi
fi
AM_CONDITIONAL(BUILD_WITH_LIBCURL, test "x$with_libcurl" = "xyes")
# }}}

# --with-libdbi {{{
with_libdbi_cppflags=""
with_libdbi_ldflags=""
AC_ARG_WITH(libdbi, [AS_HELP_STRING([--with-libdbi@<:@=PREFIX@:>@], [Path to libdbi.])],
[
	if test "x$withval" != "xno" && test "x$withval" != "xyes"
	then
		with_libdbi_cppflags="-I$withval/include"
		with_libdbi_ldflags="-L$withval/lib"
		with_libdbi="yes"
	else
		with_libdbi="$withval"
	fi
],
[
	with_libdbi="yes"
])
if test "x$with_libdbi" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libdbi_cppflags"

	AC_CHECK_HEADERS(dbi/dbi.h, [with_libdbi="yes"], [with_libdbi="no (dbi/dbi.h not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libdbi" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libdbi_cppflags"
	LDFLAGS="$LDFLAGS $with_libdbi_ldflags"

	AC_CHECK_LIB(dbi, dbi_initialize, [with_libdbi="yes"], [with_libdbi="no (Symbol 'dbi_initialize' not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
fi
if test "x$with_libdbi" = "xyes"
then
	BUILD_WITH_LIBDBI_CPPFLAGS="$with_libdbi_cppflags"
	BUILD_WITH_LIBDBI_LDFLAGS="$with_libdbi_ldflags"
	BUILD_WITH_LIBDBI_LIBS="-ldbi"
	AC_SUBST(BUILD_WITH_LIBDBI_CPPFLAGS)
	AC_SUBST(BUILD_WITH_LIBDBI_LDFLAGS)
	AC_SUBST(BUILD_WITH_LIBDBI_LIBS)
fi
AM_CONDITIONAL(BUILD_WITH_LIBDBI, test "x$with_libdbi" = "xyes")
# }}}

# --with-libesmtp {{{
AC_ARG_WITH(libesmtp, [AS_HELP_STRING([--with-libesmtp@<:@=PREFIX@:>@], [Path to libesmtp.])],
[
	if test "x$withval" != "xno" && test "x$withval" != "xyes"
	then
		LDFLAGS="$LDFLAGS -L$withval/lib"
		CPPFLAGS="$CPPFLAGS -I$withval/include -D_THREAD_SAFE"
		with_libesmtp="yes"
	else
		with_libesmtp="$withval"
	fi
],
[
	with_libesmtp="yes"
])
if test "x$with_libesmtp" = "xyes"
then
	AC_CHECK_LIB(esmtp, smtp_create_session,
	[
		AC_DEFINE(HAVE_LIBESMTP, 1, [Define to 1 if you have the esmtp library (-lesmtp).])
	], [with_libesmtp="no (libesmtp not found)"])
fi
if test "x$with_libesmtp" = "xyes"
then
	AC_CHECK_HEADERS(libesmtp.h,
	[
		AC_DEFINE(HAVE_LIBESMTP_H, 1, [Define to 1 if you have the <libesmtp.h> header file.])
	], [with_libesmtp="no (libesmtp.h not found)"])
fi
if test "x$with_libesmtp" = "xyes"
then
	collect_libesmtp=1
else
	collect_libesmtp=0
fi
AC_DEFINE_UNQUOTED(COLLECT_LIBESMTP, [$collect_libesmtp],
	[Wether or not to use the esmtp library])
AM_CONDITIONAL(BUILD_WITH_LIBESMTP, test "x$with_libesmtp" = "xyes")
# }}}

# --with-libganglia {{{
AC_ARG_WITH(libganglia, [AS_HELP_STRING([--with-libganglia@<:@=PREFIX@:>@], [Path to libganglia.])],
[
 if test -f "$withval" && test -x "$withval"
 then
	 with_libganglia_config="$withval"
	 with_libganglia="yes"
 else if test -f "$withval/bin/ganglia-config" && test -x "$withval/bin/ganglia-config"
 then
	 with_libganglia_config="$withval/bin/ganglia-config"
	 with_libganglia="yes"
 else if test -d "$withval"
 then
	 GANGLIA_CPPFLAGS="-I$withval/include"
	 GANGLIA_LDFLAGS="-L$withval/lib"
	 with_libganglia="yes"
 else
	 with_libganglia_config="ganglia-config"
	 with_libganglia="$withval"
 fi; fi; fi
],
[
 with_libganglia_config="ganglia-config"
 with_libganglia="yes"
])

if test "x$with_libganglia" = "xyes" && test "x$with_libganglia_config" != "x"
then
	if test "x$GANGLIA_CPPFLAGS" = "x"
	then
		GANGLIA_CPPFLAGS=`"$with_libganglia_config" --cflags 2>/dev/null`
	fi

	if test "x$GANGLIA_LDFLAGS" = "x"
	then
		GANGLIA_LDFLAGS=`"$with_libganglia_config" --ldflags 2>/dev/null`
	fi

	if test "x$GANGLIA_LIBS" = "x"
	then
		GANGLIA_LIBS=`"$with_libganglia_config" --libs 2>/dev/null`
	fi
fi

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"
CPPFLAGS="$CPPFLAGS $GANGLIA_CPPFLAGS"
LDFLAGS="$LDFLAGS $GANGLIA_LDFLAGS"

if test "x$with_libganglia" = "xyes"
then
	AC_CHECK_HEADERS(gm_protocol.h,
	[
		AC_DEFINE(HAVE_GM_PROTOCOL_H, 1,
			  [Define to 1 if you have the <gm_protocol.h> header file.])
	], [with_libganglia="no (gm_protocol.h not found)"])
fi

if test "x$with_libganglia" = "xyes"
then
	AC_CHECK_LIB(ganglia, xdr_Ganglia_value_msg,
	[
		AC_DEFINE(HAVE_LIBGANGLIA, 1,
			  [Define to 1 if you have the ganglia library (-lganglia).])
	], [with_libganglia="no (symbol xdr_Ganglia_value_msg not found)"])
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

AC_SUBST(GANGLIA_CPPFLAGS)
AC_SUBST(GANGLIA_LDFLAGS)
AC_SUBST(GANGLIA_LIBS)
AM_CONDITIONAL(BUILD_WITH_LIBGANGLIA, test "x$with_libganglia" = "xyes")
# }}}

# --with-libgcrypt {{{
GCRYPT_CPPFLAGS="$GCRYPT_CPPFLAGS"
GCRYPT_LDFLAGS="$GCRYPT_LDFLAGS"
GCRYPT_LIBS="$GCRYPT_LIBS"
AC_ARG_WITH(libgcrypt, [AS_HELP_STRING([--with-libgcrypt@<:@=PREFIX@:>@], [Path to libgcrypt.])],
[
 if test -f "$withval" && test -x "$withval"
 then
	 with_libgcrypt_config="$withval"
	 with_libgcrypt="yes"
 else if test -f "$withval/bin/gcrypt-config" && test -x "$withval/bin/gcrypt-config"
 then
	 with_libgcrypt_config="$withval/bin/gcrypt-config"
	 with_libgcrypt="yes"
 else if test -d "$withval"
 then
	 GCRYPT_CPPFLAGS="$GCRYPT_CPPFLAGS -I$withval/include"
	 GCRYPT_LDFLAGS="$GCRYPT_LDFLAGS -L$withval/lib"
	 with_libgcrypt="yes"
 else
	 with_libgcrypt_config="gcrypt-config"
	 with_libgcrypt="$withval"
 fi; fi; fi
],
[
 with_libgcrypt_config="libgcrypt-config"
 with_libgcrypt="yes"
])

if test "x$with_libgcrypt" = "xyes" && test "x$with_libgcrypt_config" != "x"
then
	if test "x$GCRYPT_CPPFLAGS" = "x"
	then
		GCRYPT_CPPFLAGS=`"$with_libgcrypt_config" --cflags 2>/dev/null`
	fi

	if test "x$GCRYPT_LDFLAGS" = "x"
	then
		gcrypt_exec_prefix=`"$with_libgcrypt_config" --exec-prefix 2>/dev/null`
		GCRYPT_LDFLAGS="-L$gcrypt_exec_prefix/lib"
	fi

	if test "x$GCRYPT_LIBS" = "x"
	then
		GCRYPT_LIBS=`"$with_libgcrypt_config" --libs 2>/dev/null`
	fi
fi

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"
CPPFLAGS="$CPPFLAGS $GCRYPT_CPPFLAGS"
LDFLAGS="$LDFLAGS $GCRYPT_LDFLAGS"

if test "x$with_libgcrypt" = "xyes"
then
	if test "x$GCRYPT_CPPFLAGS" != "x"
	then
		AC_MSG_NOTICE([gcrypt CPPFLAGS: $GCRYPT_CPPFLAGS])
	fi
	AC_CHECK_HEADERS(gcrypt.h,
		[with_libgcrypt="yes"],
		[with_libgcrypt="no (gcrypt.h not found)"])
fi

if test "x$with_libgcrypt" = "xyes"
then
	if test "x$GCRYPT_LDFLAGS" != "x"
	then
		AC_MSG_NOTICE([gcrypt LDFLAGS: $GCRYPT_LDFLAGS])
	fi
	AC_CHECK_LIB(gcrypt, gcry_md_hash_buffer,
		[with_libgcrypt="yes"],
		[with_libgcrypt="no (symbol gcry_md_hash_buffer not found)"])

	if test "$with_libgcrypt" != "no"; then
		m4_ifdef([AM_PATH_LIBGCRYPT],[AM_PATH_LIBGCRYPT(1:1.2.0,,with_libgcrypt="no (version 1.2.0+ required)")])
		GCRYPT_CPPFLAGS="$LIBGCRYPT_CPPFLAGS $LIBGCRYPT_CFLAGS"
		GCRYPT_LIBS="$LIBGCRYPT_LIBS"
	fi
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

if test "x$with_libgcrypt" = "xyes"
then
	AC_DEFINE(HAVE_LIBGCRYPT, 1, [Define to 1 if you have the gcrypt library (-lgcrypt).])
fi

AC_SUBST(GCRYPT_CPPFLAGS)
AC_SUBST(GCRYPT_LDFLAGS)
AC_SUBST(GCRYPT_LIBS)
AM_CONDITIONAL(BUILD_WITH_LIBGCRYPT, test "x$with_libgcrypt" = "xyes")
# }}}

# --with-libiptc {{{
AC_ARG_WITH(libiptc, [AS_HELP_STRING([--with-libiptc@<:@=PREFIX@:>@], [Path to libiptc.])],
[
	if test "x$withval" = "xyes"
	then
		with_libiptc="pkgconfig"
	else if test "x$withval" = "xno"
	then
		with_libiptc="no"
	else
		with_libiptc="yes"
		with_libiptc_cflags="-I$withval/include"
		with_libiptc_libs="-L$withval/lib"
	fi; fi
],
[
	if test "x$ac_system" = "xLinux"
	then
		with_libiptc="pkgconfig"
	else
		with_libiptc="no (Linux only)"
	fi
])

if test "x$with_libiptc" = "xpkgconfig" && test "x$PKG_CONFIG" = "x"
then
	with_libiptc="no (Don't have pkg-config)"
fi

if test "x$with_libiptc" = "xpkgconfig"
then
	$PKG_CONFIG --exists 'libiptc' 2>/dev/null
	if test $? -ne 0
	then
		with_libiptc="no (pkg-config doesn't know libiptc)"
	fi
fi
if test "x$with_libiptc" = "xpkgconfig"
then
	with_libiptc_cflags="`$PKG_CONFIG --cflags 'libiptc'`"
	if test $? -ne 0
	then
		with_libiptc="no ($PKG_CONFIG failed)"
	fi
	with_libiptc_libs="`$PKG_CONFIG --libs 'libiptc'`"
	if test $? -ne 0
	then
		with_libiptc="no ($PKG_CONFIG failed)"
	fi
fi

SAVE_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $with_libiptc_cflags"

# check whether the header file for libiptc is available.
if test "x$with_libiptc" = "xpkgconfig"
then
	AC_CHECK_HEADERS(libiptc/libiptc.h libiptc/libip6tc.h, ,
			[with_libiptc="no (header file missing)"])
fi
# If the header file is available, check for the required type declaractions.
# They may be missing in old versions of libiptc. In that case, they will be
# declared in the iptables plugin.
if test "x$with_libiptc" = "xpkgconfig"
then
	AC_CHECK_TYPES([iptc_handle_t, ip6tc_handle_t], [], [])
fi
# Check for the iptc_init symbol in the library.
# This could be in iptc or ip4tc
if test "x$with_libiptc" = "xpkgconfig"
then
	SAVE_LIBS="$LIBS"
	AC_SEARCH_LIBS(iptc_init, [iptc ip4tc],
			[with_libiptc="pkgconfig"],
			[with_libiptc="no"],
			[$with_libiptc_libs])
	LIBS="$SAVE_LIBS"
fi
if test "x$with_libiptc" = "xpkgconfig"
then
	with_libiptc="yes"
fi

CPPFLAGS="$SAVE_CPPFLAGS"

AM_CONDITIONAL(BUILD_WITH_LIBIPTC, test "x$with_libiptc" = "xyes")
if test "x$with_libiptc" = "xyes"
then
	BUILD_WITH_LIBIPTC_CPPFLAGS="$with_libiptc_cflags"
	BUILD_WITH_LIBIPTC_LDFLAGS="$with_libiptc_libs"
	AC_SUBST(BUILD_WITH_LIBIPTC_CPPFLAGS)
	AC_SUBST(BUILD_WITH_LIBIPTC_LDFLAGS)
fi
# }}}

# --with-java {{{
with_java_home="$JAVA_HOME"
if test "x$with_java_home" = "x"
then
	with_java_home="/usr/lib/jvm"
fi
with_java_vmtype="client"
with_java_cflags=""
with_java_libs=""
JAVAC="$JAVAC"
JAR="$JAR"
AC_ARG_WITH(java, [AS_HELP_STRING([--with-java@<:@=PREFIX@:>@], [Path to Java home.])],
[
	if test "x$withval" = "xno"
	then
		with_java="no"
	else if test "x$withval" = "xyes"
	then
		with_java="yes"
	else
		with_java_home="$withval"
		with_java="yes"
	fi; fi
],
[with_java="yes"])
if test "x$with_java" = "xyes"
then
	if test -d "$with_java_home"
	then
		AC_MSG_CHECKING([for jni.h])
		TMPVAR=`find -L "$with_java_home" -name jni.h -type f -exec 'dirname' '{}' ';' 2>/dev/null | head -n 1`
		if test "x$TMPVAR" != "x"
		then
			AC_MSG_RESULT([found in $TMPVAR])
			JAVA_CPPFLAGS="$JAVA_CPPFLAGS -I$TMPVAR"
		else
			AC_MSG_RESULT([not found])
		fi

		AC_MSG_CHECKING([for jni_md.h])
		TMPVAR=`find -L "$with_java_home" -name jni_md.h -type f -exec 'dirname' '{}' ';' 2>/dev/null | head -n 1`
		if test "x$TMPVAR" != "x"
		then
			AC_MSG_RESULT([found in $TMPVAR])
			JAVA_CPPFLAGS="$JAVA_CPPFLAGS -I$TMPVAR"
		else
			AC_MSG_RESULT([not found])
		fi

		AC_MSG_CHECKING([for libjvm.so])
		TMPVAR=`find -L "$with_java_home" -name libjvm.so -type f -exec 'dirname' '{}' ';' 2>/dev/null | head -n 1`
		if test "x$TMPVAR" != "x"
		then
			AC_MSG_RESULT([found in $TMPVAR])
			JAVA_LDFLAGS="$JAVA_LDFLAGS -L$TMPVAR -Wl,-rpath -Wl,$TMPVAR"
		else
			AC_MSG_RESULT([not found])
		fi

		if test "x$JAVAC" = "x"
		then
			AC_MSG_CHECKING([for javac])
			TMPVAR=`find -L "$with_java_home" -name javac -type f 2>/dev/null | head -n 1`
			if test "x$TMPVAR" != "x"
			then
				JAVAC="$TMPVAR"
				AC_MSG_RESULT([$JAVAC])
			else
				AC_MSG_RESULT([not found])
			fi
		fi
		if test "x$JAR" = "x"
		then
			AC_MSG_CHECKING([for jar])
			TMPVAR=`find -L "$with_java_home" -name jar -type f 2>/dev/null | head -n 1`
			if test "x$TMPVAR" != "x"
			then
				JAR="$TMPVAR"
				AC_MSG_RESULT([$JAR])
			else
				AC_MSG_RESULT([not found])
			fi
		fi
	else if test "x$with_java_home" != "x"
	then
		AC_MSG_WARN([JAVA_HOME: No such directory: $with_java_home])
	fi; fi
fi

if test "x$JAVA_CPPFLAGS" != "x"
then
	AC_MSG_NOTICE([Building with JAVA_CPPFLAGS set to: $JAVA_CPPFLAGS])
fi
if test "x$JAVA_CFLAGS" != "x"
then
	AC_MSG_NOTICE([Building with JAVA_CFLAGS set to: $JAVA_CFLAGS])
fi
if test "x$JAVA_LDFLAGS" != "x"
then
	AC_MSG_NOTICE([Building with JAVA_LDFLAGS set to: $JAVA_LDFLAGS])
fi
if test "x$JAVAC" = "x"
then
	with_javac_path="$PATH"
	if test "x$with_java_home" != "x"
	then
		with_javac_path="$with_java_home:with_javac_path"
		if test -d "$with_java_home/bin"
		then
			with_javac_path="$with_java_home/bin:with_javac_path"
		fi
	fi

	AC_PATH_PROG(JAVAC, javac, [], "$with_javac_path")
fi
if test "x$JAVAC" = "x"
then
	with_java="no (javac not found)"
fi
if test "x$JAR" = "x"
then
	with_jar_path="$PATH"
	if test "x$with_java_home" != "x"
	then
		with_jar_path="$with_java_home:$with_jar_path"
		if test -d "$with_java_home/bin"
		then
			with_jar_path="$with_java_home/bin:$with_jar_path"
		fi
	fi

	AC_PATH_PROG(JAR, jar, [], "$with_jar_path")
fi
if test "x$JAR" = "x"
then
	with_java="no (jar not found)"
fi

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_CFLAGS="$CFLAGS"
SAVE_LDFLAGS="$LDFLAGS"
CPPFLAGS="$CPPFLAGS $JAVA_CPPFLAGS"
CFLAGS="$CFLAGS $JAVA_CFLAGS"
LDFLAGS="$LDFLAGS $JAVA_LDFLAGS"

if test "x$with_java" = "xyes"
then
	AC_CHECK_HEADERS(jni.h, [], [with_java="no (jni.h not found)"])
fi
if test "x$with_java" = "xyes"
then
	AC_CHECK_LIB(jvm, JNI_CreateJavaVM,
	[with_java="yes"],
	[with_java="no (libjvm not found)"],
	[$JAVA_LIBS])
fi
if test "x$with_java" = "xyes"
then
	JAVA_LIBS="$JAVA_LIBS -ljvm"
	AC_MSG_NOTICE([Building with JAVA_LIBS set to: $JAVA_LIBS])
fi

CPPFLAGS="$SAVE_CPPFLAGS"
CFLAGS="$SAVE_CFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

AC_SUBST(JAVA_CPPFLAGS)
AC_SUBST(JAVA_CFLAGS)
AC_SUBST(JAVA_LDFLAGS)
AC_SUBST(JAVA_LIBS)
AM_CONDITIONAL(BUILD_WITH_JAVA, test "x$with_java" = "xyes")
# }}}

# --with-libldap {{{
AC_ARG_WITH(libldap, [AS_HELP_STRING([--with-libldap@<:@=PREFIX@:>@], [Path to libldap.])],
[
 if test "x$withval" = "xyes"
 then
	 with_libldap="yes"
 else if test "x$withval" = "xno"
 then
	 with_libldap="no"
 else
	 with_libldap="yes"
	 LIBLDAP_CPPFLAGS="$LIBLDAP_CPPFLAGS -I$withval/include"
	 LIBLDAP_LDFLAGS="$LIBLDAP_LDFLAGS -L$withval/lib"
 fi; fi
],
[with_libldap="yes"])

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"

CPPFLAGS="$CPPFLAGS $LIBLDAP_CPPFLAGS"
LDFLAGS="$LDFLAGS $LIBLDAP_LDFLAGS"

if test "x$with_libldap" = "xyes"
then
	if test "x$LIBLDAP_CPPFLAGS" != "x"
	then
		AC_MSG_NOTICE([libldap CPPFLAGS: $LIBLDAP_CPPFLAGS])
	fi
	AC_CHECK_HEADERS(ldap.h,
	[with_libldap="yes"],
	[with_libldap="no ('ldap.h' not found)"])
fi
if test "x$with_libldap" = "xyes"
then
	if test "x$LIBLDAP_LDFLAGS" != "x"
	then
		AC_MSG_NOTICE([libldap LDFLAGS: $LIBLDAP_LDFLAGS])
	fi
	AC_CHECK_LIB(ldap, ldap_initialize,
	[with_libldap="yes"],
	[with_libldap="no (symbol 'ldap_initialize' not found)"])

fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

if test "x$with_libldap" = "xyes"
then
	BUILD_WITH_LIBLDAP_CPPFLAGS="$LIBLDAP_CPPFLAGS"
	BUILD_WITH_LIBLDAP_LDFLAGS="$LIBLDAP_LDFLAGS"
	AC_SUBST(BUILD_WITH_LIBLDAP_CPPFLAGS)
	AC_SUBST(BUILD_WITH_LIBLDAP_LDFLAGS)
fi
AM_CONDITIONAL(BUILD_WITH_LIBLDAP, test "x$with_libldap" = "xyes")
# }}}

# --with-liblvm2app {{{
with_liblvm2app_cppflags=""
with_liblvm2app_ldflags=""
AC_ARG_WITH(liblvm2app, [AS_HELP_STRING([--with-liblvm2app@<:@=PREFIX@:>@], [Path to liblvm2app.])],
[
        if test "x$withval" != "xno" && test "x$withval" != "xyes"
        then
                with_liblvm2app_cppflags="-I$withval/include"
                with_liblvm2app_ldflags="-L$withval/lib"
                with_liblvm2app="yes"
        else
                with_liblvm2app="$withval"
        fi
],
[
        with_liblvm2app="yes"
])
if test "x$with_liblvm2app" = "xyes"
then
        SAVE_CPPFLAGS="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS $with_liblvm2app_cppflags"

        AC_CHECK_HEADERS(lvm2app.h, [with_liblvm2app="yes"], [with_liblvm2app="no (lvm2app.h not found)"])

        CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_liblvm2app" = "xyes"
then
        SAVE_CPPFLAGS="$CPPFLAGS"
        SAVE_LDFLAGS="$LDFLAGS"
        CPPFLAGS="$CPPFLAGS $with_liblvm2app_cppflags"
        LDFLAGS="$LDFLAGS $with_liblvm2app_ldflags"

        AC_CHECK_LIB(lvm2app, lvm_lv_get_property, [with_liblvm2app="yes"], [with_liblvm2app="no (Symbol 'lvm_lv_get_property' not found)"])

        CPPFLAGS="$SAVE_CPPFLAGS"
        LDFLAGS="$SAVE_LDFLAGS"
fi
if test "x$with_liblvm2app" = "xyes"
then
        BUILD_WITH_LIBLVM2APP_CPPFLAGS="$with_liblvm2app_cppflags"
        BUILD_WITH_LIBLVM2APP_LDFLAGS="$with_liblvm2app_ldflags"
        BUILD_WITH_LIBLVM2APP_LIBS="-llvm2app"
        AC_SUBST(BUILD_WITH_LIBLVM2APP_CPPFLAGS)
        AC_SUBST(BUILD_WITH_LIBLVM2APP_LDFLAGS)
        AC_SUBST(BUILD_WITH_LIBLVM2APP_LIBS)
        AC_DEFINE(HAVE_LIBLVM2APP, 1, [Define if liblvm2app is present and usable.])
fi
AM_CONDITIONAL(BUILD_WITH_LIBLVM2APP, test "x$with_liblvm2app" = "xyes")
# }}}

# --with-libmemcached {{{
with_libmemcached_cppflags=""
with_libmemcached_ldflags=""
AC_ARG_WITH(libmemcached, [AS_HELP_STRING([--with-libmemcached@<:@=PREFIX@:>@], [Path to libmemcached.])],
[
	if test "x$withval" != "xno" && test "x$withval" != "xyes"
	then
		with_libmemcached_cppflags="-I$withval/include"
		with_libmemcached_ldflags="-L$withval/lib"
		with_libmemcached="yes"
	else
		with_libmemcached="$withval"
	fi
],
[
	with_libmemcached="yes"
])
if test "x$with_libmemcached" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libmemcached_cppflags"

	AC_CHECK_HEADERS(libmemcached/memcached.h, [with_libmemcached="yes"], [with_libmemcached="no (libmemcached/memcached.h not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libmemcached" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libmemcached_cppflags"
	LDFLAGS="$LDFLAGS $with_libmemcached_ldflags"

	AC_CHECK_LIB(memcached, memcached_create, [with_libmemcached="yes"], [with_libmemcached="no (Symbol 'memcached_create' not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
fi
if test "x$with_libmemcached" = "xyes"
then
	BUILD_WITH_LIBMEMCACHED_CPPFLAGS="$with_libmemcached_cppflags"
	BUILD_WITH_LIBMEMCACHED_LDFLAGS="$with_libmemcached_ldflags"
	BUILD_WITH_LIBMEMCACHED_LIBS="-lmemcached"
	AC_SUBST(BUILD_WITH_LIBMEMCACHED_CPPFLAGS)
	AC_SUBST(BUILD_WITH_LIBMEMCACHED_LDFLAGS)
	AC_SUBST(BUILD_WITH_LIBMEMCACHED_LIBS)
	AC_DEFINE(HAVE_LIBMEMCACHED, 1, [Define if libmemcached is present and usable.])
fi
AM_CONDITIONAL(BUILD_WITH_LIBMEMCACHED, test "x$with_libmemcached" = "xyes")
# }}}

# --with-libmodbus {{{
with_libmodbus_config=""
with_libmodbus_cflags=""
with_libmodbus_libs=""
AC_ARG_WITH(libmodbus, [AS_HELP_STRING([--with-libmodbus@<:@=PREFIX@:>@], [Path to the modbus library.])],
[
	if test "x$withval" = "xno"
	then
		with_libmodbus="no"
	else if test "x$withval" = "xyes"
	then
		with_libmodbus="use_pkgconfig"
	else if test -d "$with_libmodbus/lib"
	then
		AC_MSG_NOTICE([Not checking for libmodbus: Manually configured])
		with_libmodbus_cflags="-I$withval/include"
		with_libmodbus_libs="-L$withval/lib -lmodbus"
		with_libmodbus="yes"
	fi; fi; fi
],
[with_libmodbus="use_pkgconfig"])

# configure using pkg-config
if test "x$with_libmodbus" = "xuse_pkgconfig"
then
	if test "x$PKG_CONFIG" = "x"
	then
		with_libmodbus="no (Don't have pkg-config)"
	fi
fi
if test "x$with_libmodbus" = "xuse_pkgconfig"
then
	AC_MSG_NOTICE([Checking for libmodbus using $PKG_CONFIG])
	$PKG_CONFIG --exists 'libmodbus' 2>/dev/null
	if test $? -ne 0
	then
		with_libmodbus="no (pkg-config doesn't know libmodbus)"
	fi
fi
if test "x$with_libmodbus" = "xuse_pkgconfig"
then
	with_libmodbus_cflags="`$PKG_CONFIG --cflags 'libmodbus'`"
	if test $? -ne 0
	then
		with_libmodbus="no ($PKG_CONFIG failed)"
	fi
	with_libmodbus_libs="`$PKG_CONFIG --libs 'libmodbus'`"
	if test $? -ne 0
	then
		with_libmodbus="no ($PKG_CONFIG failed)"
	fi
fi
if test "x$with_libmodbus" = "xuse_pkgconfig"
then
	with_libmodbus="yes"
fi

# with_libmodbus_cflags and with_libmodbus_libs are set up now, let's do
# the actual checks.
if test "x$with_libmodbus" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libmodbus_cflags"

	AC_CHECK_HEADERS(modbus/modbus.h, [], [with_libmodbus="no (modbus/modbus.h not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libmodbus" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"

	CPPFLAGS="$CPPFLAGS $with_libmodbus_cflags"
	LDFLAGS="$LDFLAGS $with_libmodbus_libs"

	AC_CHECK_LIB(modbus, modbus_connect,
		     [with_libmodbus="yes"],
		     [with_libmodbus="no (symbol modbus_connect not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
fi
if test "x$with_libmodbus" = "xyes"
then
	BUILD_WITH_LIBMODBUS_CFLAGS="$with_libmodbus_cflags"
	BUILD_WITH_LIBMODBUS_LIBS="$with_libmodbus_libs"
	AC_SUBST(BUILD_WITH_LIBMODBUS_CFLAGS)
	AC_SUBST(BUILD_WITH_LIBMODBUS_LIBS)
fi
# }}}

# --with-libmongoc {{{
AC_ARG_WITH(libmongoc, [AS_HELP_STRING([--with-libmongoc@<:@=PREFIX@:>@], [Path to libmongoc.])],
[
 if test "x$withval" = "xyes"
 then
	 with_libmongoc="yes"
 else if test "x$withval" = "xno"
 then
	 with_libmongoc="no"
 else
	 with_libmongoc="yes"
	 LIBMONGOC_CPPFLAGS="$LIBMONGOC_CPPFLAGS -I$withval/include"
	 LIBMONGOC_LDFLAGS="$LIBMONGOC_LDFLAGS -L$withval/lib"
 fi; fi
],
[with_libmongoc="yes"])

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"

CPPFLAGS="$CPPFLAGS $LIBMONGOC_CPPFLAGS"
LDFLAGS="$LDFLAGS $LIBMONGOC_LDFLAGS"

if test "x$with_libmongoc" = "xyes"
then
	if test "x$LIBMONGOC_CPPFLAGS" != "x"
	then
		AC_MSG_NOTICE([libmongoc CPPFLAGS: $LIBMONGOC_CPPFLAGS])
	fi
	AC_CHECK_HEADERS(mongo.h,
	[with_libmongoc="yes"],
	[with_libmongoc="no ('mongo.h' not found)"],
[#if HAVE_STDINT_H
# define MONGO_HAVE_STDINT 1
#else
# define MONGO_USE_LONG_LONG_INT 1
#endif
])
fi
if test "x$with_libmongoc" = "xyes"
then
	if test "x$LIBMONGOC_LDFLAGS" != "x"
	then
		AC_MSG_NOTICE([libmongoc LDFLAGS: $LIBMONGOC_LDFLAGS])
	fi
	AC_CHECK_LIB(mongoc, mongo_run_command,
	[with_libmongoc="yes"],
	[with_libmongoc="no (symbol 'mongo_run_command' not found)"])
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

if test "x$with_libmongoc" = "xyes"
then
	BUILD_WITH_LIBMONGOC_CPPFLAGS="$LIBMONGOC_CPPFLAGS"
	BUILD_WITH_LIBMONGOC_LDFLAGS="$LIBMONGOC_LDFLAGS"
	AC_SUBST(BUILD_WITH_LIBMONGOC_CPPFLAGS)
	AC_SUBST(BUILD_WITH_LIBMONGOC_LDFLAGS)
fi
AM_CONDITIONAL(BUILD_WITH_LIBMONGOC, test "x$with_libmongoc" = "xyes")
# }}}

# --with-libmysql {{{
with_mysql_config="mysql_config"
with_mysql_cflags=""
with_mysql_libs=""
AC_ARG_WITH(libmysql, [AS_HELP_STRING([--with-libmysql@<:@=PREFIX@:>@], [Path to libmysql.])],
[
	if test "x$withval" = "xno"
	then
		with_libmysql="no"
	else if test "x$withval" = "xyes"
	then
		with_libmysql="yes"
	else
		if test -f "$withval" && test -x "$withval";
		then
			with_mysql_config="$withval"
		else if test -x "$withval/bin/mysql_config"
		then
			with_mysql_config="$withval/bin/mysql_config"
		fi; fi
		with_libmysql="yes"
	fi; fi
],
[
	with_libmysql="yes"
])
if test "x$with_libmysql" = "xyes"
then
	with_mysql_cflags=`$with_mysql_config --cflags 2>/dev/null`
	mysql_config_status=$?

	if test $mysql_config_status -ne 0
	then
		with_libmysql="no ($with_mysql_config failed)"
	else
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_mysql_cflags"

		have_mysql_h="no"
		have_mysql_mysql_h="no"
		AC_CHECK_HEADERS(mysql.h, [have_mysql_h="yes"])

		if test "x$have_mysql_h" = "xno"
		then
			AC_CHECK_HEADERS(mysql/mysql.h, [have_mysql_mysql_h="yes"])
		fi

		if test "x$have_mysql_h$have_mysql_mysql_h" = "xnono"
		then
			with_libmysql="no (mysql.h not found)"
		fi

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
fi
if test "x$with_libmysql" = "xyes"
then
	with_mysql_libs=`$with_mysql_config --libs_r 2>/dev/null`
	mysql_config_status=$?

	if test $mysql_config_status -ne 0
	then
		with_libmysql="no ($with_mysql_config failed)"
	else
		AC_CHECK_LIB(mysqlclient, mysql_init,
		 [with_libmysql="yes"],
		 [with_libmysql="no (symbol 'mysql_init' not found)"],
		 [$with_mysql_libs])

		AC_CHECK_LIB(mysqlclient, mysql_get_server_version,
		 [with_libmysql="yes"],
		 [with_libmysql="no (symbol 'mysql_get_server_version' not found)"],
		 [$with_mysql_libs])
	fi
fi
if test "x$with_libmysql" = "xyes"
then
	BUILD_WITH_LIBMYSQL_CFLAGS="$with_mysql_cflags"
	BUILD_WITH_LIBMYSQL_LIBS="$with_mysql_libs"
	AC_SUBST(BUILD_WITH_LIBMYSQL_CFLAGS)
	AC_SUBST(BUILD_WITH_LIBMYSQL_LIBS)
fi
AM_CONDITIONAL(BUILD_WITH_LIBMYSQL, test "x$with_libmysql" = "xyes")
# }}}

# --with-libmnl {{{
with_libmnl_cflags=""
with_libmnl_libs=""
AC_ARG_WITH(libmnl, [AS_HELP_STRING([--with-libmnl@<:@=PREFIX@:>@], [Path to libmnl.])],
[
 echo "libmnl: withval = $withval"
 if test "x$withval" = "xyes"
 then
	 with_libmnl="yes"
 else if test "x$withval" = "xno"
 then
	 with_libmnl="no"
 else
	 if test -d "$withval/include"
	 then
		 with_libmnl_cflags="-I$withval/include"
		 with_libmnl_libs="-L$withval/lib -lmnl"
		 with_libmnl="yes"
	 else
		 AC_MSG_ERROR("no such directory: $withval/include")
	 fi
 fi; fi
],
[
 if test "x$ac_system" = "xLinux"
 then
	 with_libmnl="yes"
 else
	 with_libmnl="no (Linux only library)"
 fi
])
if test "x$PKG_CONFIG" = "x"
then
	with_libmnl="no (Don't have pkg-config)"
fi
if test "x$with_libmnl" = "xyes"
then
	if $PKG_CONFIG --exists libmnl 2>/dev/null; then
	  with_libmnl_cflags="$with_libmnl_ldflags `$PKG_CONFIG --cflags libmnl`"
	  with_libmnl_libs="$with_libmnl_libs `$PKG_CONFIG --libs libmnl`"
	fi

	AC_CHECK_HEADERS(libmnl.h libmnl/libmnl.h,
	[
	 with_libmnl="yes"
	 break
	], [],
[#include <stdio.h>
#include <sys/types.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>])
	AC_CHECK_HEADERS(linux/gen_stats.h linux/pkt_sched.h, [], [],
[#include <stdio.h>
#include <sys/types.h>
#include <asm/types.h>
#include <sys/socket.h>])

        AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[
#include <stdio.h>
#include <sys/types.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
]],
[[
int retval = TCA_STATS2;
return (retval);
]]
	)],
	[AC_DEFINE([HAVE_TCA_STATS2], [1], [True if the enum-member TCA_STATS2 exists])])

	AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[
#include <stdio.h>
#include <sys/types.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
]],
[[
int retval = TCA_STATS;
return (retval);
]]
	)],
	[AC_DEFINE([HAVE_TCA_STATS], 1, [True if the enum-member TCA_STATS exists])])
fi
if test "x$with_libmnl" = "xyes"
then
	AC_CHECK_MEMBERS([struct rtnl_link_stats64.tx_window_errors],
	[AC_DEFINE(HAVE_RTNL_LINK_STATS64, 1, [Define if struct rtnl_link_stats64 exists and is usable.])],
	[],
	[
	#include <linux/if_link.h>
	])
fi
if test "x$with_libmnl" = "xyes"
then
	AC_CHECK_LIB(mnl, mnl_nlmsg_get_payload,
		     [with_libmnl="yes"],
		     [with_libmnl="no (symbol 'mnl_nlmsg_get_payload' not found)"],
		     [$with_libmnl_libs])
fi
if test "x$with_libmnl" = "xyes"
then
	AC_DEFINE(HAVE_LIBMNL, 1, [Define if libmnl is present and usable.])
	BUILD_WITH_LIBMNL_CFLAGS="$with_libmnl_cflags"
	BUILD_WITH_LIBMNL_LIBS="$with_libmnl_libs"
	AC_SUBST(BUILD_WITH_LIBMNL_CFLAGS)
	AC_SUBST(BUILD_WITH_LIBMNL_LIBS)
fi
AM_CONDITIONAL(BUILD_WITH_LIBMNL, test "x$with_libmnl" = "xyes")
# }}}

# --with-libnetapp {{{
AC_ARG_VAR([LIBNETAPP_CPPFLAGS], [C preprocessor flags required to build with libnetapp])
AC_ARG_VAR([LIBNETAPP_LDFLAGS],  [Linker flags required to build with libnetapp])
AC_ARG_VAR([LIBNETAPP_LIBS],     [Other libraries required to link against libnetapp])
LIBNETAPP_CPPFLAGS="$LIBNETAPP_CPPFLAGS"
LIBNETAPP_LDFLAGS="$LIBNETAPP_LDFLAGS"
LIBNETAPP_LIBS="$LIBNETAPP_LIBS"
AC_ARG_WITH(libnetapp, [AS_HELP_STRING([--with-libnetapp@<:@=PREFIX@:>@], [Path to libnetapp.])],
[
 if test -d "$withval"
 then
	 LIBNETAPP_CPPFLAGS="$LIBNETAPP_CPPFLAGS -I$withval/include"
	 LIBNETAPP_LDFLAGS="$LIBNETAPP_LDFLAGS -L$withval/lib"
	 with_libnetapp="yes"
 else
	 with_libnetapp="$withval"
 fi
],
[
 with_libnetapp="yes"
])

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"
CPPFLAGS="$CPPFLAGS $LIBNETAPP_CPPFLAGS"
LDFLAGS="$LDFLAGS $LIBNETAPP_LDFLAGS"

if test "x$with_libnetapp" = "xyes"
then
	if test "x$LIBNETAPP_CPPFLAGS" != "x"
	then
		AC_MSG_NOTICE([netapp CPPFLAGS: $LIBNETAPP_CPPFLAGS])
	fi
	AC_CHECK_HEADERS(netapp_api.h,
		[with_libnetapp="yes"],
		[with_libnetapp="no (netapp_api.h not found)"])
fi

if test "x$with_libnetapp" = "xyes"
then
	if test "x$LIBNETAPP_LDFLAGS" != "x"
	then
		AC_MSG_NOTICE([netapp LDFLAGS: $LIBNETAPP_LDFLAGS])
	fi

	if test "x$LIBNETAPP_LIBS" = "x"
	then
		LIBNETAPP_LIBS="-lpthread -lxml -ladt -lssl -lm -lcrypto -lz"
	fi
	AC_MSG_NOTICE([netapp LIBS: $LIBNETAPP_LIBS])

	AC_CHECK_LIB(netapp, na_server_invoke_elem,
		[with_libnetapp="yes"],
		[with_libnetapp="no (symbol na_server_invoke_elem not found)"],
		[$LIBNETAPP_LIBS])
	LIBNETAPP_LIBS="-lnetapp $LIBNETAPP_LIBS"
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

if test "x$with_libnetapp" = "xyes"
then
	AC_DEFINE(HAVE_LIBNETAPP, 1, [Define to 1 if you have the netapp library (-lnetapp).])
fi

AC_SUBST(LIBNETAPP_CPPFLAGS)
AC_SUBST(LIBNETAPP_LDFLAGS)
AC_SUBST(LIBNETAPP_LIBS)
AM_CONDITIONAL(BUILD_WITH_LIBNETAPP, test "x$with_libnetapp" = "xyes")
# }}}

# --with-libnetsnmp {{{
with_snmp_config="net-snmp-config"
with_snmp_cflags=""
with_snmp_libs=""
AC_ARG_WITH(libnetsnmp, [AS_HELP_STRING([--with-libnetsnmp@<:@=PREFIX@:>@], [Path to the Net-SNMPD library.])],
[
	if test "x$withval" = "xno"
	then
		with_libnetsnmp="no"
	else if test "x$withval" = "xyes"
	then
		with_libnetsnmp="yes"
	else
		if test -x "$withval"
		then
			with_snmp_config="$withval"
			with_libnetsnmp="yes"
		else
			with_snmp_config="$withval/bin/net-snmp-config"
			with_libnetsnmp="yes"
		fi
	fi; fi
],
[with_libnetsnmp="yes"])
if test "x$with_libnetsnmp" = "xyes"
then
	with_snmp_cflags=`$with_snmp_config --cflags 2>/dev/null`
	snmp_config_status=$?

	if test $snmp_config_status -ne 0
	then
		with_libnetsnmp="no ($with_snmp_config failed)"
	else
		SAVE_CPPFLAGS="$CPPFLAGS"
		CPPFLAGS="$CPPFLAGS $with_snmp_cflags"

		AC_CHECK_HEADERS(net-snmp/net-snmp-config.h, [], [with_libnetsnmp="no (net-snmp/net-snmp-config.h not found)"])

		CPPFLAGS="$SAVE_CPPFLAGS"
	fi
fi
if test "x$with_libnetsnmp" = "xyes"
then
	with_snmp_libs=`$with_snmp_config --libs 2>/dev/null`
	snmp_config_status=$?

	if test $snmp_config_status -ne 0
	then
		with_libnetsnmp="no ($with_snmp_config failed)"
	else
		AC_CHECK_LIB(netsnmp, init_snmp,
		[with_libnetsnmp="yes"],
		[with_libnetsnmp="no (libnetsnmp not found)"],
		[$with_snmp_libs])
	fi
fi
if test "x$with_libnetsnmp" = "xyes"
then
	BUILD_WITH_LIBSNMP_CFLAGS="$with_snmp_cflags"
	BUILD_WITH_LIBSNMP_LIBS="$with_snmp_libs"
	AC_SUBST(BUILD_WITH_LIBSNMP_CFLAGS)
	AC_SUBST(BUILD_WITH_LIBSNMP_LIBS)
fi
AM_CONDITIONAL(BUILD_WITH_LIBNETSNMP, test "x$with_libnetsnmp" = "xyes")
# }}}

# --with-liboconfig {{{
with_own_liboconfig="no"
liboconfig_LDFLAGS="$LDFLAGS"
liboconfig_CPPFLAGS="$CPPFLAGS"
AC_ARG_WITH(liboconfig, [AS_HELP_STRING([--with-liboconfig@<:@=PREFIX@:>@], [Path to liboconfig.])],
[
	if test "x$withval" != "xno" && test "x$withval" != "xyes"
	then
		if test -d "$withval/lib"
		then
			liboconfig_LDFLAGS="$LDFLAGS -L$withval/lib"
		fi
		if test -d "$withval/include"
		then
			liboconfig_CPPFLAGS="$CPPFLAGS -I$withval/include"
		fi
	fi
	if test "x$withval" = "xno"
	then
		AC_MSG_ERROR("liboconfig is required")
	fi
],
[
	with_liboconfig="yes"
])

save_LDFLAGS="$LDFLAGS"
save_CPPFLAGS="$CPPFLAGS"
LDFLAGS="$liboconfig_LDFLAGS"
CPPFLAGS="$liboconfig_CPPFLAGS"
AC_CHECK_LIB(oconfig, oconfig_parse_fh,
[
	with_liboconfig="yes"
	with_own_liboconfig="no"
],
[
	with_liboconfig="yes"
	with_own_liboconfig="yes"
	LDFLAGS="$save_LDFLAGS"
	CPPFLAGS="$save_CPPFLAGS"
])

AM_CONDITIONAL(BUILD_WITH_OWN_LIBOCONFIG, test "x$with_own_liboconfig" = "xyes")
if test "x$with_own_liboconfig" = "xyes"
then
	with_liboconfig="yes (shipped version)"
fi
# }}}

# --with-liboping {{{
AC_ARG_WITH(liboping, [AS_HELP_STRING([--with-liboping@<:@=PREFIX@:>@], [Path to liboping.])],
[
 if test "x$withval" = "xyes"
 then
	 with_liboping="yes"
 else if test "x$withval" = "xno"
 then
	 with_liboping="no"
 else
	 with_liboping="yes"
	 LIBOPING_CPPFLAGS="$LIBOPING_CPPFLAGS -I$withval/include"
	 LIBOPING_LDFLAGS="$LIBOPING_LDFLAGS -L$withval/lib"
 fi; fi
],
[with_liboping="yes"])

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"

CPPFLAGS="$CPPFLAGS $LIBOPING_CPPFLAGS"
LDFLAGS="$LDFLAGS $LIBOPING_LDFLAGS"

if test "x$with_liboping" = "xyes"
then
	if test "x$LIBOPING_CPPFLAGS" != "x"
	then
		AC_MSG_NOTICE([liboping CPPFLAGS: $LIBOPING_CPPFLAGS])
	fi
	AC_CHECK_HEADERS(oping.h,
	[with_liboping="yes"],
	[with_liboping="no (oping.h not found)"])
fi
if test "x$with_liboping" = "xyes"
then
	if test "x$LIBOPING_LDFLAGS" != "x"
	then
		AC_MSG_NOTICE([liboping LDFLAGS: $LIBOPING_LDFLAGS])
	fi
	AC_CHECK_LIB(oping, ping_construct,
	[with_liboping="yes"],
	[with_liboping="no (symbol 'ping_construct' not found)"])
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

if test "x$with_liboping" = "xyes"
then
	BUILD_WITH_LIBOPING_CPPFLAGS="$LIBOPING_CPPFLAGS"
	BUILD_WITH_LIBOPING_LDFLAGS="$LIBOPING_LDFLAGS"
	AC_SUBST(BUILD_WITH_LIBOPING_CPPFLAGS)
	AC_SUBST(BUILD_WITH_LIBOPING_LDFLAGS)
fi
AM_CONDITIONAL(BUILD_WITH_LIBOPING, test "x$with_liboping" = "xyes")
# }}}

# --with-oracle {{{
with_oracle_cppflags=""
with_oracle_libs=""
AC_ARG_WITH(oracle, [AS_HELP_STRING([--with-oracle@<:@=ORACLE_HOME@:>@], [Path to Oracle.])],
[
	if test "x$withval" = "xyes"
	then
		if test "x$ORACLE_HOME" = "x"
		then
			AC_MSG_WARN([Use of the Oracle library has been forced, but the environment variable ORACLE_HOME is not set.])
		fi
		with_oracle="yes"
	else if test "x$withval" = "xno"
	then
		with_oracle="no"
	else
		with_oracle="yes"
		ORACLE_HOME="$withval"
	fi; fi
],
[
	if test "x$ORACLE_HOME" = "x"
	then
		with_oracle="no (ORACLE_HOME is not set)"
	else
		with_oracle="yes"
	fi
])
if test "x$ORACLE_HOME" != "x"
then
	with_oracle_cppflags="-I$ORACLE_HOME/rdbms/public"

	if test -e "$ORACLE_HOME/lib/ldflags"
	then
		with_oracle_libs=`cat "$ORACLE_HOME/lib/ldflags"`
	fi
	#with_oracle_libs="-L$ORACLE_HOME/lib $with_oracle_libs -lclntsh"
	with_oracle_libs="-L$ORACLE_HOME/lib -lclntsh"
fi
if test "x$with_oracle" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_oracle_cppflags"

	AC_CHECK_HEADERS(oci.h, [with_oracle="yes"], [with_oracle="no (oci.h not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_oracle" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LIBS="$LIBS"
	CPPFLAGS="$CPPFLAGS $with_oracle_cppflags"
	LIBS="$LIBS $with_oracle_libs"

	AC_CHECK_FUNC(OCIEnvCreate, [with_oracle="yes"], [with_oracle="no (Symbol 'OCIEnvCreate' not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
	LIBS="$SAVE_LIBS"
fi
if test "x$with_oracle" = "xyes"
then
	BUILD_WITH_ORACLE_CFLAGS="$with_oracle_cppflags"
	BUILD_WITH_ORACLE_LIBS="$with_oracle_libs"
	AC_SUBST(BUILD_WITH_ORACLE_CFLAGS)
	AC_SUBST(BUILD_WITH_ORACLE_LIBS)
fi
# }}}

# --with-libowcapi {{{
with_libowcapi_cppflags=""
with_libowcapi_libs="-lowcapi"
AC_ARG_WITH(libowcapi, [AS_HELP_STRING([--with-libowcapi@<:@=PREFIX@:>@], [Path to libowcapi.])],
[
	if test "x$withval" != "xno" && test "x$withval" != "xyes"
	then
		with_libowcapi_cppflags="-I$withval/include"
		with_libowcapi_libs="-L$withval/lib -lowcapi"
		with_libowcapi="yes"
	else
		with_libowcapi="$withval"
	fi
],
[
	with_libowcapi="yes"
])
if test "x$with_libowcapi" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$with_libowcapi_cppflags"

	AC_CHECK_HEADERS(owcapi.h, [with_libowcapi="yes"], [with_libowcapi="no (owcapi.h not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libowcapi" = "xyes"
then
	SAVE_LDFLAGS="$LDFLAGS"
	SAVE_CPPFLAGS="$CPPFLAGS"
	LDFLAGS="$with_libowcapi_libs"
	CPPFLAGS="$with_libowcapi_cppflags"

	AC_CHECK_LIB(owcapi, OW_get, [with_libowcapi="yes"], [with_libowcapi="no (libowcapi not found)"])

	LDFLAGS="$SAVE_LDFLAGS"
	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libowcapi" = "xyes"
then
	BUILD_WITH_LIBOWCAPI_CPPFLAGS="$with_libowcapi_cppflags"
	BUILD_WITH_LIBOWCAPI_LIBS="$with_libowcapi_libs"
	AC_SUBST(BUILD_WITH_LIBOWCAPI_CPPFLAGS)
	AC_SUBST(BUILD_WITH_LIBOWCAPI_LIBS)
fi
# }}}

# --with-libpcap {{{
AC_ARG_WITH(libpcap, [AS_HELP_STRING([--with-libpcap@<:@=PREFIX@:>@], [Path to libpcap.])],
[
	if test "x$withval" != "xno" && test "x$withval" != "xyes"
	then
		LDFLAGS="$LDFLAGS -L$withval/lib"
		CPPFLAGS="$CPPFLAGS -I$withval/include"
		with_libpcap="yes"
	else
		with_libpcap="$withval"
	fi
],
[
	with_libpcap="yes"
])
if test "x$with_libpcap" = "xyes"
then
	AC_CHECK_LIB(pcap, pcap_open_live,
	[
		AC_DEFINE(HAVE_LIBPCAP, 1, [Define to 1 if you have the pcap library (-lpcap).])
	], [with_libpcap="no (libpcap not found)"])
fi
if test "x$with_libpcap" = "xyes"
then
	AC_CHECK_HEADERS(pcap.h,,
			 [with_libpcap="no (pcap.h not found)"])
fi
if test "x$with_libpcap" = "xyes"
then
	AC_CHECK_HEADERS(pcap-bpf.h,,
			 [with_libpcap="no (pcap-bpf.h not found)"])
fi
if test "x$with_libpcap" = "xyes"
then
	AC_CACHE_CHECK([whether libpcap has PCAP_ERROR_IFACE_NOT_UP],
		       [c_cv_libpcap_have_pcap_error_iface_not_up],
		       AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[[
#include <pcap.h>
]]],
[[[
  int val = PCAP_ERROR_IFACE_NOT_UP;
  return(val);
]]]
		       )],
		       [c_cv_libpcap_have_pcap_error_iface_not_up="yes"],
		       [c_cv_libpcap_have_pcap_error_iface_not_up="no"]))
fi
if test "x$c_cv_libpcap_have_pcap_error_iface_not_up" != "xyes"
then
		with_libpcap="no (pcap.h misses PCAP_ERROR_IFACE_NOT_UP)"
fi
AM_CONDITIONAL(BUILD_WITH_LIBPCAP, test "x$with_libpcap" = "xyes")
# }}}

# --with-libperl {{{
perl_interpreter="perl"
AC_ARG_WITH(libperl, [AS_HELP_STRING([--with-libperl@<:@=PREFIX@:>@], [Path to libperl.])],
[
	if test -f "$withval" && test -x "$withval"
	then
		perl_interpreter="$withval"
		with_libperl="yes"
	else if test "x$withval" != "xno" && test "x$withval" != "xyes"
	then
		LDFLAGS="$LDFLAGS -L$withval/lib"
		CPPFLAGS="$CPPFLAGS -I$withval/include"
		perl_interpreter="$withval/bin/perl"
		with_libperl="yes"
	else
		with_libperl="$withval"
	fi; fi
],
[
	with_libperl="yes"
])

AC_MSG_CHECKING([for perl])
perl_interpreter=`which "$perl_interpreter" 2> /dev/null`
if test -x "$perl_interpreter"
then
	AC_MSG_RESULT([yes ($perl_interpreter)])
else
	perl_interpreter=""
	AC_MSG_RESULT([no])
fi

AC_SUBST(PERL, "$perl_interpreter")

if test "x$with_libperl" = "xyes" \
	&& test -n "$perl_interpreter"
then
  SAVE_CFLAGS="$CFLAGS"
  SAVE_LIBS="$LIBS"
dnl ARCHFLAGS="" -> disable multi -arch on OSX (see Config_heavy.pl:fetch_string)
  PERL_CFLAGS=`ARCHFLAGS="" $perl_interpreter -MExtUtils::Embed -e ccopts`
  PERL_LIBS=`ARCHFLAGS="" $perl_interpreter -MExtUtils::Embed -e ldopts`
  CFLAGS="$CFLAGS $PERL_CFLAGS"
  LIBS="$LIBS $PERL_LIBS"

  AC_CACHE_CHECK([for libperl],
    [c_cv_have_libperl],
    AC_LINK_IFELSE([AC_LANG_PROGRAM(
[[[
#define PERL_NO_GET_CONTEXT
#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
]]],
[[[
       dTHX;
       load_module (PERL_LOADMOD_NOIMPORT,
			 newSVpv ("Collectd::Plugin::FooBar", 24),
			 Nullsv);
]]]
      )],
      [c_cv_have_libperl="yes"],
      [c_cv_have_libperl="no"]
    )
  )

  if test "x$c_cv_have_libperl" = "xyes"
  then
	  AC_DEFINE(HAVE_LIBPERL, 1, [Define if libperl is present and usable.])
	  AC_SUBST(PERL_CFLAGS)
	  AC_SUBST(PERL_LIBS)
  else
	  with_libperl="no"
  fi

  CFLAGS="$SAVE_CFLAGS"
  LIBS="$SAVE_LIBS"
else if test -z "$perl_interpreter"; then
  with_libperl="no (no perl interpreter found)"
  c_cv_have_libperl="no"
fi; fi
AM_CONDITIONAL(BUILD_WITH_LIBPERL, test "x$with_libperl" = "xyes")

if test "x$with_libperl" = "xyes"
then
	SAVE_CFLAGS="$CFLAGS"
	SAVE_LIBS="$LIBS"
	CFLAGS="$CFLAGS $PERL_CFLAGS"
	LIBS="$LIBS $PERL_LIBS"

	AC_CACHE_CHECK([if perl supports ithreads],
		[c_cv_have_perl_ithreads],
		AC_LINK_IFELSE([AC_LANG_PROGRAM(
[[[
#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#if !defined(USE_ITHREADS)
# error "Perl does not support ithreads!"
#endif /* !defined(USE_ITHREADS) */
]]],
[[[ ]]]
			)],
			[c_cv_have_perl_ithreads="yes"],
			[c_cv_have_perl_ithreads="no"]
		)
	)

	if test "x$c_cv_have_perl_ithreads" = "xyes"
	then
		AC_DEFINE(HAVE_PERL_ITHREADS, 1, [Define if Perl supports ithreads.])
	fi

	CFLAGS="$SAVE_CFLAGS"
	LIBS="$SAVE_LIBS"
fi

if test "x$with_libperl" = "xyes"
then
	SAVE_CFLAGS="$CFLAGS"
	SAVE_LIBS="$LIBS"
	# trigger an error if Perl_load_module*() uses __attribute__nonnull__(3)
	# (see issues #41 and #42)
	CFLAGS="$CFLAGS $PERL_CFLAGS -Wall -Werror"
	LIBS="$LIBS $PERL_LIBS"

	AC_CACHE_CHECK([for broken Perl_load_module()],
		[c_cv_have_broken_perl_load_module],
		AC_LINK_IFELSE([AC_LANG_PROGRAM(
[[[
#define PERL_NO_GET_CONTEXT
#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
]]],
[[[
			 dTHX;
			 load_module (PERL_LOADMOD_NOIMPORT,
			     newSVpv ("Collectd::Plugin::FooBar", 24),
			     Nullsv);
]]]
			)],
			[c_cv_have_broken_perl_load_module="no"],
			[c_cv_have_broken_perl_load_module="yes"]
		)
	)

	CFLAGS="$SAVE_CFLAGS"
	LIBS="$SAVE_LIBS"
fi
AM_CONDITIONAL(HAVE_BROKEN_PERL_LOAD_MODULE,
		test "x$c_cv_have_broken_perl_load_module" = "xyes")

if test "x$with_libperl" = "xyes"
then
	SAVE_CFLAGS="$CFLAGS"
	SAVE_LIBS="$LIBS"
	CFLAGS="$CFLAGS $PERL_CFLAGS"
	LIBS="$LIBS $PERL_LIBS"

	AC_CHECK_MEMBER(
		[struct mgvtbl.svt_local],
		[have_struct_mgvtbl_svt_local="yes"],
		[have_struct_mgvtbl_svt_local="no"],
		[
#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>
		])

	if test "x$have_struct_mgvtbl_svt_local" = "xyes"
	then
		AC_DEFINE(HAVE_PERL_STRUCT_MGVTBL_SVT_LOCAL, 1,
				  [Define if Perl's struct mgvtbl has member svt_local.])
	fi

	CFLAGS="$SAVE_CFLAGS"
	LIBS="$SAVE_LIBS"
fi
# }}}

# --with-libpq {{{
with_pg_config="pg_config"
with_libpq_includedir=""
with_libpq_libdir=""
with_libpq_cppflags=""
with_libpq_ldflags=""
AC_ARG_WITH(libpq, [AS_HELP_STRING([--with-libpq@<:@=PREFIX@:>@],
	[Path to libpq.])],
[
	if test "x$withval" = "xno"
	then
		with_libpq="no"
	else if test "x$withval" = "xyes"
	then
		with_libpq="yes"
	else
		if test -f "$withval" && test -x "$withval";
		then
			with_pg_config="$withval"
		else if test -x "$withval/bin/pg_config"
		then
			with_pg_config="$withval/bin/pg_config"
		fi; fi
		with_libpq="yes"
	fi; fi
],
[
	with_libpq="yes"
])
if test "x$with_libpq" = "xyes"
then
	with_libpq_includedir=`$with_pg_config --includedir 2> /dev/null`
	pg_config_status=$?

	if test $pg_config_status -eq 0
	then
		if test -n "$with_libpq_includedir"; then
			for dir in $with_libpq_includedir; do
				with_libpq_cppflags="$with_libpq_cppflags -I$dir"
			done
		fi
	else
		AC_MSG_WARN([$with_pg_config returned with status $pg_config_status])
	fi

	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libpq_cppflags"

	AC_CHECK_HEADERS(libpq-fe.h, [],
		[with_libpq="no (libpq-fe.h not found)"], [])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libpq" = "xyes"
then
	with_libpq_libdir=`$with_pg_config --libdir 2> /dev/null`
	pg_config_status=$?

	if test $pg_config_status -eq 0
	then
		if test -n "$with_libpq_libdir"; then
			for dir in $with_libpq_libdir; do
				with_libpq_ldflags="$with_libpq_ldflags -L$dir"
			done
		fi
	else
		AC_MSG_WARN([$with_pg_config returned with status $pg_config_status])
	fi

	SAVE_LDFLAGS="$LDFLAGS"
	LDFLAGS="$LDFLAGS $with_libpq_ldflags"

	AC_CHECK_LIB(pq, PQconnectdb,
		[with_libpq="yes"],
		[with_libpq="no (symbol 'PQconnectdb' not found)"])

	AC_CHECK_LIB(pq, PQserverVersion,
		[with_libpq="yes"],
		[with_libpq="no (symbol 'PQserverVersion' not found)"])

	LDFLAGS="$SAVE_LDFLAGS"
fi
if test "x$with_libpq" = "xyes"
then
	BUILD_WITH_LIBPQ_CPPFLAGS="$with_libpq_cppflags"
	BUILD_WITH_LIBPQ_LDFLAGS="$with_libpq_ldflags"
	AC_SUBST(BUILD_WITH_LIBPQ_CPPFLAGS)
	AC_SUBST(BUILD_WITH_LIBPQ_LDFLAGS)
fi
AM_CONDITIONAL(BUILD_WITH_LIBPQ, test "x$with_libpq" = "xyes")
# }}}

# --with-libpthread {{{
AC_ARG_WITH(libpthread, [AS_HELP_STRING([--with-libpthread=@<:@=PREFIX@:>@], [Path to libpthread.])],
[	if test "x$withval" != "xno" \
		&& test "x$withval" != "xyes"
	then
		LDFLAGS="$LDFLAGS -L$withval/lib"
		CPPFLAGS="$CPPFLAGS -I$withval/include"
		with_libpthread="yes"
	else
		if test "x$withval" = "xno"
		then
			with_libpthread="no (disabled)"
		fi
	fi
], [with_libpthread="yes"])
if test "x$with_libpthread" = "xyes"
then
	AC_CHECK_LIB(pthread, pthread_create, [with_libpthread="yes"], [with_libpthread="no (libpthread not found)"], [])
fi

if test "x$with_libpthread" = "xyes"
then
	AC_CHECK_HEADERS(pthread.h,, [with_libpthread="no (pthread.h not found)"])
fi
if test "x$with_libpthread" = "xyes"
then
	collect_pthread=1
else
	collect_pthread=0
fi
AC_DEFINE_UNQUOTED(HAVE_LIBPTHREAD, [$collect_pthread],
	[Wether or not to use pthread (POSIX threads) library])
AM_CONDITIONAL(BUILD_WITH_LIBPTHREAD, test "x$with_libpthread" = "xyes")
# }}}

# --with-python {{{
with_python_prog=""
with_python_path="$PATH"
AC_ARG_WITH(python, [AS_HELP_STRING([--with-python@<:@=PREFIX@:>@], [Path to the python interpreter.])],
[
 if test "x$withval" = "xyes" || test "x$withval" = "xno"
 then
	 with_python="$withval"
 else if test -x "$withval"
 then
	 with_python_prog="$withval"
	 with_python_path="`dirname \"$withval\"`$PATH_SEPARATOR$with_python_path"
	 with_python="yes"
 else if test -d "$withval"
 then
	 with_python_path="$withval$PATH_SEPARATOR$with_python_path"
	 with_python="yes"
 else
	 AC_MSG_WARN([Argument not recognized: $withval])
 fi; fi; fi
], [with_python="yes"])

SAVE_PATH="$PATH"
SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"
SAVE_LIBS="$LIBS"

PATH="$with_python_path"

if test "x$with_python" = "xyes" && test "x$with_python_prog" = "x"
then
	AC_MSG_CHECKING([for python])
	with_python_prog="`which python 2>/dev/null`"
	if test "x$with_python_prog" = "x"
	then
		AC_MSG_RESULT([not found])
		with_python="no (interpreter not found)"
	else
		AC_MSG_RESULT([$with_python_prog])
	fi
fi

if test "x$with_python" = "xyes"
then
	AC_MSG_CHECKING([for Python CPPFLAGS])
	python_include_path=`echo "import distutils.sysconfig;import sys;sys.stdout.write(distutils.sysconfig.get_python_inc())" | "$with_python_prog" 2>&1`
	python_config_status=$?

	if test "$python_config_status" -ne 0 || test "x$python_include_path" = "x"
	then
		AC_MSG_RESULT([failed with status $python_config_status (output: $python_include_path)])
		with_python="no"
	else
		AC_MSG_RESULT([$python_include_path])
	fi
fi

if test "x$with_python" = "xyes"
then
	CPPFLAGS="-I$python_include_path $CPPFLAGS"
	AC_CHECK_HEADERS(Python.h,
			 [with_python="yes"],
			 [with_python="no ('Python.h' not found)"])
fi

if test "x$with_python" = "xyes"
then
	AC_MSG_CHECKING([for Python LDFLAGS])
	python_library_path=`echo "import distutils.sysconfig;import sys;sys.stdout.write(distutils.sysconfig.get_config_vars(\"LIBDIR\").__getitem__(0))" | "$with_python_prog" 2>&1`
	python_config_status=$?

	if test "$python_config_status" -ne 0 || test "x$python_library_path" = "x"
	then
		AC_MSG_RESULT([failed with status $python_config_status (output: $python_library_path)])
		with_python="no"
	else
		AC_MSG_RESULT([$python_library_path])
	fi
fi

if test "x$with_python" = "xyes"
then
	AC_MSG_CHECKING([for Python LIBS])
	python_library_flags=`echo "import distutils.sysconfig;import sys;sys.stdout.write(distutils.sysconfig.get_config_vars(\"BLDLIBRARY\").__getitem__(0))" | "$with_python_prog" 2>&1`
	python_config_status=$?

	if test "$python_config_status" -ne 0 || test "x$python_library_flags" = "x"
	then
		AC_MSG_RESULT([failed with status $python_config_status (output: $python_library_flags)])
		with_python="no"
	else
		AC_MSG_RESULT([$python_library_flags])
	fi
fi

if test "x$with_python" = "xyes"
then
	LDFLAGS="-L$python_library_path $LDFLAGS"
	LIBS="$python_library_flags $LIBS"

	AC_CHECK_FUNC(PyObject_CallFunction,
		      [with_python="yes"],
		      [with_python="no (Symbol 'PyObject_CallFunction' not found)"])
fi

PATH="$SAVE_PATH"
CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"
LIBS="$SAVE_LIBS"

if test "x$with_python" = "xyes"
then
	BUILD_WITH_PYTHON_CPPFLAGS="-I$python_include_path"
	BUILD_WITH_PYTHON_LDFLAGS="-L$python_library_path"
	BUILD_WITH_PYTHON_LIBS="$python_library_flags"
	AC_SUBST(BUILD_WITH_PYTHON_CPPFLAGS)
	AC_SUBST(BUILD_WITH_PYTHON_LDFLAGS)
	AC_SUBST(BUILD_WITH_PYTHON_LIBS)
fi
# }}} --with-python

# --with-librabbitmq {{{
with_librabbitmq_cppflags=""
with_librabbitmq_ldflags=""
AC_ARG_WITH(librabbitmq, [AS_HELP_STRING([--with-librabbitmq@<:@=PREFIX@:>@], [Path to librabbitmq.])],
[
	if test "x$withval" != "xno" && test "x$withval" != "xyes"
	then
		with_librabbitmq_cppflags="-I$withval/include"
		with_librabbitmq_ldflags="-L$withval/lib"
		with_librabbitmq="yes"
	else
		with_librabbitmq="$withval"
	fi
],
[
	with_librabbitmq="yes"
])
SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"
CPPFLAGS="$CPPFLAGS $with_librabbitmq_cppflags"
LDFLAGS="$LDFLAGS $with_librabbitmq_ldflags"
if test "x$with_librabbitmq" = "xyes"
then
	AC_CHECK_HEADERS(amqp.h, [with_librabbitmq="yes"], [with_librabbitmq="no (amqp.h not found)"])
fi
if test "x$with_librabbitmq" = "xyes"
then
	# librabbitmq up to version 0.9.1 provides "library_errno", later
	# versions use "library_error". The library does not provide a version
	# macro :( Use "AC_CHECK_MEMBERS" (plural) for automatic defines.
	AC_CHECK_MEMBERS([amqp_rpc_reply_t.library_errno],,,
			 [
#if HAVE_STDLIB_H
# include <stdlib.h>
#endif
#if HAVE_STDIO_H
# include <stdio.h>
#endif
#if HAVE_STDINT_H
# include <stdint.h>
#endif
#if HAVE_INTTYPES_H
# include <inttypes.h>
#endif
#include <amqp.h>
                         ])
fi
if test "x$with_librabbitmq" = "xyes"
then
	AC_CHECK_LIB(rabbitmq, amqp_basic_publish, [with_librabbitmq="yes"], [with_librabbitmq="no (Symbol 'amqp_basic_publish' not found)"])
fi
if test "x$with_librabbitmq" = "xyes"
then
	BUILD_WITH_LIBRABBITMQ_CPPFLAGS="$with_librabbitmq_cppflags"
	BUILD_WITH_LIBRABBITMQ_LDFLAGS="$with_librabbitmq_ldflags"
	BUILD_WITH_LIBRABBITMQ_LIBS="-lrabbitmq"
	AC_SUBST(BUILD_WITH_LIBRABBITMQ_CPPFLAGS)
	AC_SUBST(BUILD_WITH_LIBRABBITMQ_LDFLAGS)
	AC_SUBST(BUILD_WITH_LIBRABBITMQ_LIBS)
	AC_DEFINE(HAVE_LIBRABBITMQ, 1, [Define if librabbitmq is present and usable.])
fi
CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"
AM_CONDITIONAL(BUILD_WITH_LIBRABBITMQ, test "x$with_librabbitmq" = "xyes")

with_amqp_tcp_socket="no"
if test "x$with_librabbitmq" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"
	SAVE_LIBS="$LIBS"
	CPPFLAGS="$CPPFLAGS $with_librabbitmq_cppflags"
	LDFLAGS="$LDFLAGS $with_librabbitmq_ldflags"
	LIBS="-lrabbitmq"

	AC_CHECK_HEADERS(amqp_tcp_socket.h amqp_socket.h)
	AC_CHECK_FUNC(amqp_tcp_socket_new, [with_amqp_tcp_socket="yes"], [with_amqp_tcp_socket="no"])
	if test "x$with_amqp_tcp_socket" = "xyes"
	then
		AC_DEFINE(HAVE_AMQP_TCP_SOCKET, 1,
				[Define if librabbitmq provides the new TCP socket interface.])
	fi

	AC_CHECK_DECLS(amqp_socket_close,
				[amqp_socket_close_decl="yes"], [amqp_socket_close_decl="no"],
				[[
#include <amqp.h>
#ifdef HAVE_AMQP_TCP_SOCKET_H
# include <amqp_tcp_socket.h>
#endif
#ifdef HAVE_AMQP_SOCKET_H
# include <amqp_socket.h>
#endif
				]])

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
	LIBS="$SAVE_LIBS"
fi
# }}}

# --with-librdkafka {{{
AC_ARG_WITH(librdkafka, [AS_HELP_STRING([--with-librdkafka@<:@=PREFIX@:>@], [Path to librdkafka.])],
[
  if test "x$withval" != "xno" && test "x$withval" != "xyes"
  then
    with_librdkafka_cppflags="-I$withval/include"
    with_librdkafka_ldflags="-L$withval/lib"
    with_librdkafka_rpath="$withval/lib"
    with_librdkafka="yes"
  else
    with_librdkafka="$withval"
  fi
],
[
  with_librdkafka="yes"
])
SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"

CPPFLAGS="$CPPFLAGS $with_librdkafka_cppflags"
LDFLAGS="$LDFLAGS $with_librdkafka_ldflags"

if test "x$with_librdkafka" = "xyes"
then
	AC_CHECK_HEADERS(librdkafka/rdkafka.h, [with_librdkafka="yes"], [with_librdkafka="no (librdkafka/rdkafka.h not found)"])
fi

if test "x$with_librdkafka" = "xyes"
then
	AC_CHECK_LIB(rdkafka, rd_kafka_new, [with_librdkafka="yes"], [with_librdkafka="no (Symbol 'rd_kafka_new' not found)"])
  AC_CHECK_LIB(rdkafka, rd_kafka_conf_set_log_cb, [with_librdkafka_log_cb="yes"], [with_librdkafka_log_cb="no"])
  AC_CHECK_LIB(rdkafka, rd_kafka_set_logger, [with_librdkafka_logger="yes"], [with_librdkafka_logger="no"])
fi
if test "x$with_librdkafka" = "xyes"
then
	BUILD_WITH_LIBRDKAFKA_CPPFLAGS="$with_librdkafka_cppflags"
	BUILD_WITH_LIBRDKAFKA_LDFLAGS="$with_librdkafka_ldflags"
	if test "x$with_librdkafka_rpath" != "x"
	then
		BUILD_WITH_LIBRDKAFKA_LIBS="-Wl,-rpath,$with_librdkafka_rpath -lrdkafka"
	else
		BUILD_WITH_LIBRDKAFKA_LIBS="-lrdkafka"
	fi
	AC_SUBST(BUILD_WITH_LIBRDKAFKA_CPPFLAGS)
	AC_SUBST(BUILD_WITH_LIBRDKAFKA_LDFLAGS)
	AC_SUBST(BUILD_WITH_LIBRDKAFKA_LIBS)
	AC_DEFINE(HAVE_LIBRDKAFKA, 1, [Define if librdkafka is present and usable.])
  if test "x$with_librdkafka_log_cb" = "xyes"
  then
        AC_DEFINE(HAVE_LIBRDKAFKA_LOG_CB, 1, [Define if librdkafka log facility is present and usable.])
  fi
  if test "x$with_librdkafka_logger" = "xyes"
  then
        AC_DEFINE(HAVE_LIBRDKAFKA_LOGGER, 1, [Define if librdkafka log facility is present and usable.])
  fi
fi
CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"
AM_CONDITIONAL(BUILD_WITH_LIBRDKAFKA, test "x$with_librdkafka" = "xyes")

# }}}

# --with-librouteros {{{
AC_ARG_WITH(librouteros, [AS_HELP_STRING([--with-librouteros@<:@=PREFIX@:>@], [Path to librouteros.])],
[
 if test "x$withval" = "xyes"
 then
	 with_librouteros="yes"
 else if test "x$withval" = "xno"
 then
	 with_librouteros="no"
 else
	 with_librouteros="yes"
	 LIBROUTEROS_CPPFLAGS="$LIBROUTEROS_CPPFLAGS -I$withval/include"
	 LIBROUTEROS_LDFLAGS="$LIBROUTEROS_LDFLAGS -L$withval/lib"
 fi; fi
],
[with_librouteros="yes"])

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"

CPPFLAGS="$CPPFLAGS $LIBROUTEROS_CPPFLAGS"
LDFLAGS="$LDFLAGS $LIBROUTEROS_LDFLAGS"

if test "x$with_librouteros" = "xyes"
then
	if test "x$LIBROUTEROS_CPPFLAGS" != "x"
	then
		AC_MSG_NOTICE([librouteros CPPFLAGS: $LIBROUTEROS_CPPFLAGS])
	fi
	AC_CHECK_HEADERS(routeros_api.h,
	[with_librouteros="yes"],
	[with_librouteros="no (routeros_api.h not found)"])
fi
if test "x$with_librouteros" = "xyes"
then
	if test "x$LIBROUTEROS_LDFLAGS" != "x"
	then
		AC_MSG_NOTICE([librouteros LDFLAGS: $LIBROUTEROS_LDFLAGS])
	fi
	AC_CHECK_LIB(routeros, ros_interface,
	[with_librouteros="yes"],
	[with_librouteros="no (symbol 'ros_interface' not found)"])
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

if test "x$with_librouteros" = "xyes"
then
	BUILD_WITH_LIBROUTEROS_CPPFLAGS="$LIBROUTEROS_CPPFLAGS"
	BUILD_WITH_LIBROUTEROS_LDFLAGS="$LIBROUTEROS_LDFLAGS"
	AC_SUBST(BUILD_WITH_LIBROUTEROS_CPPFLAGS)
	AC_SUBST(BUILD_WITH_LIBROUTEROS_LDFLAGS)
fi
AM_CONDITIONAL(BUILD_WITH_LIBROUTEROS, test "x$with_librouteros" = "xyes")
# }}}

# --with-librrd {{{
# AC_ARG_WITH (package, help-string, [action-if-given], [action-if-not-given])
librrd_cflags=""
librrd_ldflags=""
librrd_threadsafe="yes"
librrd_rrdc_update="no"
AC_ARG_WITH(librrd, [AS_HELP_STRING([--with-librrd@<:@=PREFIX@:>@], [Path to rrdtool.])],
[	if test "x$withval" != "xno" && test "x$withval" != "xyes"
	then
		librrd_cflags="-I$withval/include"
		librrd_ldflags="-L$withval/lib"
		with_librrd="yes"
	else
		with_librrd="$withval"
	fi
], [with_librrd="yes"])
if test "x$with_librrd" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"

	CPPFLAGS="$CPPFLAGS $librrd_cflags"
	LDFLAGS="$LDFLAGS $librrd_ldflags"

	AC_CHECK_HEADERS(rrd.h,, [with_librrd="no (rrd.h not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
fi
if test "x$with_librrd" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"

	CPPFLAGS="$CPPFLAGS $librrd_cflags"
	LDFLAGS="$LDFLAGS $librrd_ldflags"

	AC_CHECK_LIB(rrd_th, rrd_update_r,
	[with_librrd="yes"
	 librrd_ldflags="$librrd_ldflags -lrrd_th -lm"
	],
	[librrd_threadsafe="no"
	 AC_CHECK_LIB(rrd, rrd_update,
	 [with_librrd="yes"
	  librrd_ldflags="$librrd_ldflags -lrrd -lm"
	 ],
	 [with_librrd="no (symbol 'rrd_update' not found)"],
	 [-lm])
	],
	[-lm])

	if test "x$librrd_threadsafe" = "xyes"
	then
		AC_CHECK_LIB(rrd_th, rrdc_update, [librrd_rrdc_update="yes"], [librrd_rrdc_update="no"])
	else
		AC_CHECK_LIB(rrd, rrdc_update, [librrd_rrdc_update="yes"], [librrd_rrdc_update="no"])
	fi

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
fi
if test "x$with_librrd" = "xyes"
then
	BUILD_WITH_LIBRRD_CFLAGS="$librrd_cflags"
	BUILD_WITH_LIBRRD_LDFLAGS="$librrd_ldflags"
	AC_SUBST(BUILD_WITH_LIBRRD_CFLAGS)
	AC_SUBST(BUILD_WITH_LIBRRD_LDFLAGS)
fi
if test "x$librrd_threadsafe" = "xyes"
then
	AC_DEFINE(HAVE_THREADSAFE_LIBRRD, 1, [Define to 1 if you have the threadsafe rrd library (-lrrd_th).])
fi
# }}}

# --with-libsensors {{{
with_sensors_cflags=""
with_sensors_ldflags=""
AC_ARG_WITH(libsensors, [AS_HELP_STRING([--with-libsensors@<:@=PREFIX@:>@], [Path to lm_sensors.])],
[
	if test "x$withval" = "xno"
	then
		with_libsensors="no"
	else
		with_libsensors="yes"
		if test "x$withval" != "xyes"
		then
			with_sensors_cflags="-I$withval/include"
			with_sensors_ldflags="-L$withval/lib"
			with_libsensors="yes"
		fi
	fi
],
[
	if test "x$ac_system" = "xLinux"
	then
		with_libsensors="yes"
	else
		with_libsensors="no (Linux only library)"
	fi
])
if test "x$with_libsensors" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_sensors_cflags"

#	AC_CHECK_HEADERS(sensors/sensors.h,
#	[
#		AC_DEFINE(HAVE_SENSORS_SENSORS_H, 1, [Define to 1 if you have the <sensors/sensors.h> header file.])
#	],
#	[with_libsensors="no (sensors/sensors.h not found)"])
	AC_CHECK_HEADERS(sensors/sensors.h, [], [with_libsensors="no (sensors/sensors.h not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libsensors" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"
	CPPFLAGS="$CPPFLAGS $with_sensors_cflags"
	LDFLAGS="$LDFLAGS $with_sensors_ldflags"

	AC_CHECK_LIB(sensors, sensors_init,
	[
		AC_DEFINE(HAVE_LIBSENSORS, 1, [Define to 1 if you have the sensors library (-lsensors).])
	],
	[with_libsensors="no (libsensors not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
fi
if test "x$with_libsensors" = "xyes"
then
	BUILD_WITH_LIBSENSORS_CFLAGS="$with_sensors_cflags"
	BUILD_WITH_LIBSENSORS_LDFLAGS="$with_sensors_ldflags"
	AC_SUBST(BUILD_WITH_LIBSENSORS_CFLAGS)
	AC_SUBST(BUILD_WITH_LIBSENSORS_LDFLAGS)
fi
AM_CONDITIONAL(BUILD_WITH_LM_SENSORS, test "x$with_libsensors" = "xyes")
# }}}

# --with-libsigrok {{{
with_libsigrok_cflags=""
with_libsigrok_ldflags=""
AC_ARG_WITH(libsigrok, [AS_HELP_STRING([--with-libsigrok@<:@=PREFIX@:>@], [Path to libsigrok.])],
[
	if test "x$withval" = "xno"
	then
		with_libsigrok="no"
	else
		with_libsigrok="yes"
		if test "x$withval" != "xyes"
		then
			with_libsigrok_cflags="-I$withval/include"
			with_libsigrok_ldflags="-L$withval/lib"
		fi
	fi
],[with_libsigrok="yes"])

# libsigrok has a glib dependency
if test "x$with_libsigrok" = "xyes"
then
m4_ifdef([AM_PATH_GLIB_2_0],
	[
	 AM_PATH_GLIB_2_0([2.28.0],
	 	[with_libsigrok_cflags="$with_libsigrok_cflags $GLIB_CFLAGS"; with_libsigrok_ldflags="$with_libsigrok_ldflags $GLIB_LIBS"])
	],
	[
	 with_libsigrok="no (glib not available)"
	]
)
fi

# libsigrok headers
if test "x$with_libsigrok" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libsigrok_cflags"

	AC_CHECK_HEADERS(libsigrok/libsigrok.h, [], [with_libsigrok="no (libsigrok/libsigrok.h not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi

# libsigrok library
if test "x$with_libsigrok" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libsigrok_cflags"
	LDFLAGS="$LDFLAGS $with_libsigrok_ldflags"

	AC_CHECK_LIB(sigrok, sr_init,
	[
		AC_DEFINE(HAVE_LIBSIGROK, 1, [Define to 1 if you have the sigrok library (-lsigrok).])
	],
	[with_libsigrok="no (libsigrok not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
fi
if test "x$with_libsigrok" = "xyes"
then
	BUILD_WITH_LIBSIGROK_CFLAGS="$with_libsigrok_cflags"
	BUILD_WITH_LIBSIGROK_LDFLAGS="$with_libsigrok_ldflags"
	AC_SUBST(BUILD_WITH_LIBSIGROK_CFLAGS)
	AC_SUBST(BUILD_WITH_LIBSIGROK_LDFLAGS)
fi
AM_CONDITIONAL(BUILD_WITH_LIBSIGROK, test "x$with_libsigrok" = "xyes")
# }}}

# --with-libstatgrab {{{
with_libstatgrab_cflags=""
with_libstatgrab_ldflags=""
AC_ARG_WITH(libstatgrab, [AS_HELP_STRING([--with-libstatgrab@<:@=PREFIX@:>@], [Path to libstatgrab.])],
[
 if test "x$withval" != "xno" \
   && test "x$withval" != "xyes"
 then
   with_libstatgrab_cflags="-I$withval/include"
   with_libstatgrab_ldflags="-L$withval/lib -lstatgrab"
   with_libstatgrab="yes"
   with_libstatgrab_pkg_config="no"
 else
   with_libstatgrab="$withval"
   with_libstatgrab_pkg_config="yes"
 fi
 ],
[
 with_libstatgrab="yes"
 with_libstatgrab_pkg_config="yes"
])

if test "x$with_libstatgrab" = "xyes" \
  && test "x$with_libstatgrab_pkg_config" = "xyes"
then
  if test "x$PKG_CONFIG" != "x"
  then
    AC_MSG_CHECKING([pkg-config for libstatgrab])
    temp_result="found"
    $PKG_CONFIG --exists libstatgrab 2>/dev/null
    if test "$?" != "0"
    then
      with_libstatgrab_pkg_config="no"
      with_libstatgrab="no (pkg-config doesn't know libstatgrab)"
      temp_result="not found"
    fi
    AC_MSG_RESULT([$temp_result])
  else
    AC_MSG_NOTICE([pkg-config not available, trying to guess flags for the statgrab library.])
    with_libstatgrab_pkg_config="no"
    with_libstatgrab_ldflags="$with_libstatgrab_ldflags -lstatgrab"
  fi
fi

if test "x$with_libstatgrab" = "xyes" \
  && test "x$with_libstatgrab_pkg_config" = "xyes" \
  && test "x$with_libstatgrab_cflags" = "x"
then
  AC_MSG_CHECKING([for libstatgrab CFLAGS])
  temp_result="`$PKG_CONFIG --cflags libstatgrab`"
  if test "$?" = "0"
  then
    with_libstatgrab_cflags="$temp_result"
  else
    with_libstatgrab="no ($PKG_CONFIG --cflags libstatgrab failed)"
    temp_result="$PKG_CONFIG --cflags libstatgrab failed"
  fi
  AC_MSG_RESULT([$temp_result])
fi

if test "x$with_libstatgrab" = "xyes" \
  && test "x$with_libstatgrab_pkg_config" = "xyes" \
  && test "x$with_libstatgrab_ldflags" = "x"
then
  AC_MSG_CHECKING([for libstatgrab LDFLAGS])
  temp_result="`$PKG_CONFIG --libs libstatgrab`"
  if test "$?" = "0"
  then
    with_libstatgrab_ldflags="$temp_result"
  else
    with_libstatgrab="no ($PKG_CONFIG --libs libstatgrab failed)"
    temp_result="$PKG_CONFIG --libs libstatgrab failed"
  fi
  AC_MSG_RESULT([$temp_result])
fi

if test "x$with_libstatgrab" = "xyes"
then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libstatgrab_cflags"

  AC_CHECK_HEADERS(statgrab.h,
		   [with_libstatgrab="yes"],
		   [with_libstatgrab="no (statgrab.h not found)"])

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libstatgrab" = "xyes"
then
  SAVE_CFLAGS="$CFLAGS"
  SAVE_LDFLAGS="$LDFLAGS"

  CFLAGS="$CFLAGS $with_libstatgrab_cflags"
  LDFLAGS="$LDFLAGS $with_libstatgrab_ldflags"

  AC_CHECK_LIB(statgrab, sg_init,
	       [with_libstatgrab="yes"],
	       [with_libstatgrab="no (symbol sg_init not found)"])

  CFLAGS="$SAVE_CFLAGS"
  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libstatgrab" = "xyes"
then
  SAVE_CFLAGS="$CFLAGS"
  SAVE_LIBS="$LIBS"

  CFLAGS="$CFLAGS $with_libstatgrab_cflags"
  LDFLAGS="$LDFLAGS $with_libstatgrab_ldflags"
  LIBS="-lstatgrab $LIBS"

  AC_CACHE_CHECK([if libstatgrab >= 0.90],
          [c_cv_have_libstatgrab_0_90],
          AC_LINK_IFELSE([AC_LANG_PROGRAM(
[[[
#include <stdio.h>
#include <statgrab.h>
]]],
[[[
      if (sg_init()) return 0;
]]]
    )],
    [c_cv_have_libstatgrab_0_90="no"],
    [c_cv_have_libstatgrab_0_90="yes"]
          )
  )

  CFLAGS="$SAVE_CFLAGS"
  LDFLAGS="$SAVE_LDFLAGS"
  LIBS="$SAVE_LIBS"
fi

AM_CONDITIONAL(BUILD_WITH_LIBSTATGRAB, test "x$with_libstatgrab" = "xyes")
if test "x$with_libstatgrab" = "xyes"
then
  AC_DEFINE(HAVE_LIBSTATGRAB, 1, [Define to 1 if you have the 'statgrab' library (-lstatgrab)])
  BUILD_WITH_LIBSTATGRAB_CFLAGS="$with_libstatgrab_cflags"
  BUILD_WITH_LIBSTATGRAB_LDFLAGS="$with_libstatgrab_ldflags"
  AC_SUBST(BUILD_WITH_LIBSTATGRAB_CFLAGS)
  AC_SUBST(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
  if test "x$c_cv_have_libstatgrab_0_90" = "xyes"
  then
        AC_DEFINE(HAVE_LIBSTATGRAB_0_90, 1, [Define to 1 if libstatgrab version >= 0.90])
  fi
fi
# }}}

# --with-libtokyotyrant {{{
with_libtokyotyrant_cppflags=""
with_libtokyotyrant_ldflags=""
with_libtokyotyrant_libs=""
AC_ARG_WITH(libtokyotyrant, [AS_HELP_STRING([--with-libtokyotyrant@<:@=PREFIX@:>@], [Path to libtokyotyrant.])],
[
  if test "x$withval" = "xno"
  then
    with_libtokyotyrant="no"
  else if test "x$withval" = "xyes"
  then
    with_libtokyotyrant="yes"
  else
    with_libtokyotyrant_cppflags="-I$withval/include"
    with_libtokyotyrant_ldflags="-L$withval/include"
    with_libtokyotyrant_libs="-ltokyotyrant"
    with_libtokyotyrant="yes"
  fi; fi
],
[
  with_libtokyotyrant="yes"
])

if test "x$with_libtokyotyrant" = "xyes"
then
  if $PKG_CONFIG --exists tokyotyrant
  then
    with_libtokyotyrant_cppflags="$with_libtokyotyrant_cppflags `$PKG_CONFIG --cflags tokyotyrant`"
    with_libtokyotyrant_ldflags="$with_libtokyotyrant_ldflags `$PKG_CONFIG --libs-only-L tokyotyrant`"
    with_libtokyotyrant_libs="$with_libtokyotyrant_libs `$PKG_CONFIG --libs-only-l tokyotyrant`"
  fi
fi

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"
CPPFLAGS="$CPPFLAGS $with_libtokyotyrant_cppflags"
LDFLAGS="$LDFLAGS $with_libtokyotyrant_ldflags"

if test "x$with_libtokyotyrant" = "xyes"
then
  AC_CHECK_HEADERS(tcrdb.h,
  [
          AC_DEFINE(HAVE_TCRDB_H, 1,
                    [Define to 1 if you have the <tcrdb.h> header file.])
  ], [with_libtokyotyrant="no (tcrdb.h not found)"])
fi

if test "x$with_libtokyotyrant" = "xyes"
then
  AC_CHECK_LIB(tokyotyrant, tcrdbrnum,
  [
          AC_DEFINE(HAVE_LIBTOKYOTYRANT, 1,
                    [Define to 1 if you have the tokyotyrant library (-ltokyotyrant).])
  ],
  [with_libtokyotyrant="no (symbol tcrdbrnum not found)"],
  [$with_libtokyotyrant_libs])
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

if test "x$with_libtokyotyrant" = "xyes"
then
  BUILD_WITH_LIBTOKYOTYRANT_CPPFLAGS="$with_libtokyotyrant_cppflags"
  BUILD_WITH_LIBTOKYOTYRANT_LDFLAGS="$with_libtokyotyrant_ldflags"
  BUILD_WITH_LIBTOKYOTYRANT_LIBS="$with_libtokyotyrant_libs"
  AC_SUBST(BUILD_WITH_LIBTOKYOTYRANT_CPPFLAGS)
  AC_SUBST(BUILD_WITH_LIBTOKYOTYRANT_LDFLAGS)
  AC_SUBST(BUILD_WITH_LIBTOKYOTYRANT_LIBS)
fi
AM_CONDITIONAL(BUILD_WITH_LIBTOKYOTYRANT, test "x$with_libtokyotyrant" = "xyes")
# }}}

# --with-libudev {{{
with_libudev_cflags=""
with_libudev_ldflags=""
AC_ARG_WITH(libudev, [AS_HELP_STRING([--with-libudev@<:@=PREFIX@:>@], [Path to libudev.])],
[
	if test "x$withval" = "xno"
	then
		with_libudev="no"
	else
		with_libudev="yes"
		if test "x$withval" != "xyes"
		then
			with_libudev_cflags="-I$withval/include"
			with_libudev_ldflags="-L$withval/lib"
			with_libudev="yes"
		fi
	fi
],
[
	if test "x$ac_system" = "xLinux"
	then
		with_libudev="yes"
	else
		with_libudev="no (Linux only library)"
	fi
])
if test "x$with_libudev" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libudev_cflags"

	AC_CHECK_HEADERS(libudev.h, [], [with_libudev="no (libudev.h not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libudev" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libudev_cflags"
	LDFLAGS="$LDFLAGS $with_libudev_ldflags"

	AC_CHECK_LIB(udev, udev_new,
	[
		AC_DEFINE(HAVE_LIBUDEV, 1, [Define to 1 if you have the udev library (-ludev).])
	],
	[with_libudev="no (libudev not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
fi
if test "x$with_libudev" = "xyes"
then
	BUILD_WITH_LIBUDEV_CFLAGS="$with_libudev_cflags"
	BUILD_WITH_LIBUDEV_LDFLAGS="$with_libudev_ldflags"
	AC_SUBST(BUILD_WITH_LIBUDEV_CFLAGS)
	AC_SUBST(BUILD_WITH_LIBUDEV_LDFLAGS)
fi
AM_CONDITIONAL(BUILD_WITH_LIBUDEV, test "x$with_libudev" = "xyes")
# }}}

# --with-libupsclient {{{
with_libupsclient_config=""
with_libupsclient_cflags=""
with_libupsclient_libs=""
AC_ARG_WITH(libupsclient, [AS_HELP_STRING([--with-libupsclient@<:@=PREFIX@:>@], [Path to the upsclient library.])],
[
	if test "x$withval" = "xno"
	then
		with_libupsclient="no"
	else if test "x$withval" = "xyes"
	then
		with_libupsclient="use_pkgconfig"
	else
		if test -x "$withval"
		then
			with_libupsclient_config="$withval"
			with_libupsclient="use_libupsclient_config"
		else if test -x "$withval/bin/libupsclient-config"
		then
			with_libupsclient_config="$withval/bin/libupsclient-config"
			with_libupsclient="use_libupsclient_config"
		else
			AC_MSG_NOTICE([Not checking for libupsclient: Manually configured])
			with_libupsclient_cflags="-I$withval/include"
			with_libupsclient_libs="-L$withval/lib -lupsclient"
			with_libupsclient="yes"
		fi; fi
	fi; fi
],
[with_libupsclient="use_pkgconfig"])

# configure using libupsclient-config
if test "x$with_libupsclient" = "xuse_libupsclient_config"
then
	AC_MSG_NOTICE([Checking for libupsclient using $with_libupsclient_config])
	with_libupsclient_cflags="`$with_libupsclient_config --cflags`"
	if test $? -ne 0
	then
		with_libupsclient="no ($with_libupsclient_config failed)"
	fi
	with_libupsclient_libs="`$with_libupsclient_config --libs`"
	if test $? -ne 0
	then
		with_libupsclient="no ($with_libupsclient_config failed)"
	fi
fi
if test "x$with_libupsclient" = "xuse_libupsclient_config"
then
	with_libupsclient="yes"
fi

# configure using pkg-config
if test "x$with_libupsclient" = "xuse_pkgconfig"
then
	if test "x$PKG_CONFIG" = "x"
	then
		with_libupsclient="no (Don't have pkg-config)"
	fi
fi
if test "x$with_libupsclient" = "xuse_pkgconfig"
then
	AC_MSG_NOTICE([Checking for libupsclient using $PKG_CONFIG])
	$PKG_CONFIG --exists 'libupsclient' 2>/dev/null
	if test $? -ne 0
	then
		with_libupsclient="no (pkg-config doesn't know libupsclient)"
	fi
fi
if test "x$with_libupsclient" = "xuse_pkgconfig"
then
	with_libupsclient_cflags="`$PKG_CONFIG --cflags 'libupsclient'`"
	if test $? -ne 0
	then
		with_libupsclient="no ($PKG_CONFIG failed)"
	fi
	with_libupsclient_libs="`$PKG_CONFIG --libs 'libupsclient'`"
	if test $? -ne 0
	then
		with_libupsclient="no ($PKG_CONFIG failed)"
	fi
fi
if test "x$with_libupsclient" = "xuse_pkgconfig"
then
	with_libupsclient="yes"
fi

# with_libupsclient_cflags and with_libupsclient_libs are set up now, let's do
# the actual checks.
if test "x$with_libupsclient" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libupsclient_cflags"

	AC_CHECK_HEADERS(upsclient.h, [], [with_libupsclient="no (upsclient.h not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libupsclient" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"

	CPPFLAGS="$CPPFLAGS $with_libupsclient_cflags"
	LDFLAGS="$LDFLAGS $with_libupsclient_libs"

	AC_CHECK_LIB(upsclient, upscli_connect,
		     [with_libupsclient="yes"],
		     [with_libupsclient="no (symbol upscli_connect not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
fi
if test "x$with_libupsclient" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libupsclient_cflags"

	AC_CHECK_TYPES([UPSCONN_t, UPSCONN], [], [],
[#include <stdlib.h>
#include <stdio.h>
#include <upsclient.h>])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libupsclient" = "xyes"
then
	BUILD_WITH_LIBUPSCLIENT_CFLAGS="$with_libupsclient_cflags"
	BUILD_WITH_LIBUPSCLIENT_LIBS="$with_libupsclient_libs"
	AC_SUBST(BUILD_WITH_LIBUPSCLIENT_CFLAGS)
	AC_SUBST(BUILD_WITH_LIBUPSCLIENT_LIBS)
fi
# }}}

# --with-libxmms {{{
with_xmms_config="xmms-config"
with_xmms_cflags=""
with_xmms_libs=""
AC_ARG_WITH(libxmms, [AS_HELP_STRING([--with-libxmms@<:@=PREFIX@:>@], [Path to libxmms.])],
[
	if test "x$withval" != "xno" \
		&& test "x$withval" != "xyes"
	then
		if test -f "$withval" && test -x "$withval";
		then
			with_xmms_config="$withval"
		else if test -x "$withval/bin/xmms-config"
		then
			with_xmms_config="$withval/bin/xmms-config"
		fi; fi
		with_libxmms="yes"
	else if test "x$withval" = "xno"
	then
		with_libxmms="no"
	else
		with_libxmms="yes"
	fi; fi
],
[
	with_libxmms="yes"
])
if test "x$with_libxmms" = "xyes"
then
	with_xmms_cflags=`$with_xmms_config --cflags 2>/dev/null`
	xmms_config_status=$?

	if test $xmms_config_status -ne 0
	then
		with_libxmms="no"
	fi
fi
if test "x$with_libxmms" = "xyes"
then
	with_xmms_libs=`$with_xmms_config --libs 2>/dev/null`
	xmms_config_status=$?

	if test $xmms_config_status -ne 0
	then
		with_libxmms="no"
	fi
fi
if test "x$with_libxmms" = "xyes"
then
	AC_CHECK_LIB(xmms, xmms_remote_get_info,
	[
		BUILD_WITH_LIBXMMS_CFLAGS="$with_xmms_cflags"
		BUILD_WITH_LIBXMMS_LIBS="$with_xmms_libs"
		AC_SUBST(BUILD_WITH_LIBXMMS_CFLAGS)
		AC_SUBST(BUILD_WITH_LIBXMMS_LIBS)
	],
	[
		with_libxmms="no"
	],
	[$with_xmms_libs])
fi
with_libxmms_numeric=0
if test "x$with_libxmms" = "xyes"
then
	with_libxmms_numeric=1
fi
AC_DEFINE_UNQUOTED(HAVE_LIBXMMS, [$with_libxmms_numeric], [Define to 1 if you have the 'xmms' library (-lxmms).])
AM_CONDITIONAL(BUILD_WITH_LIBXMMS, test "x$with_libxmms" = "xyes")
# }}}

# --with-libyajl {{{
with_libyajl_cppflags=""
with_libyajl_ldflags=""
AC_ARG_WITH(libyajl, [AS_HELP_STRING([--with-libyajl@<:@=PREFIX@:>@], [Path to libyajl.])],
[
	if test "x$withval" != "xno" && test "x$withval" != "xyes"
	then
		with_libyajl_cppflags="-I$withval/include"
		with_libyajl_ldflags="-L$withval/lib"
		with_libyajl="yes"
	else
		with_libyajl="$withval"
	fi
],
[
	with_libyajl="yes"
])
if test "x$with_libyajl" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libyajl_cppflags"

	AC_CHECK_HEADERS(yajl/yajl_parse.h, [with_libyajl="yes"], [with_libyajl="no (yajl/yajl_parse.h not found)"])
	AC_CHECK_HEADERS(yajl/yajl_version.h)

	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libyajl" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libyajl_cppflags"
	LDFLAGS="$LDFLAGS $with_libyajl_ldflags"

	AC_CHECK_LIB(yajl, yajl_alloc, [with_libyajl="yes"], [with_libyajl="no (Symbol 'yajl_alloc' not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
fi
if test "x$with_libyajl" = "xyes"
then
	BUILD_WITH_LIBYAJL_CPPFLAGS="$with_libyajl_cppflags"
	BUILD_WITH_LIBYAJL_LDFLAGS="$with_libyajl_ldflags"
	BUILD_WITH_LIBYAJL_LIBS="-lyajl"
	AC_SUBST(BUILD_WITH_LIBYAJL_CPPFLAGS)
	AC_SUBST(BUILD_WITH_LIBYAJL_LDFLAGS)
	AC_SUBST(BUILD_WITH_LIBYAJL_LIBS)
	AC_DEFINE(HAVE_LIBYAJL, 1, [Define if libyajl is present and usable.])
fi
AM_CONDITIONAL(BUILD_WITH_LIBYAJL, test "x$with_libyajl" = "xyes")
# }}}

# --with-mic {{{
with_mic_cflags="-I/opt/intel/mic/sysmgmt/sdk/include"
with_mic_ldpath="-L/opt/intel/mic/sysmgmt/sdk/lib/Linux"
with_mic_libs=""
AC_ARG_WITH(mic,[AS_HELP_STRING([--with-mic@<:@=PREFIX@:>@], [Path to Intel MIC Access API.])],
[
	if test "x$withval" = "xno"
	then
		with_mic="no"
	else if test "x$withval" = "xyes"
	then
		with_mic="yes"
	else if test -d "$with_mic/lib"
	then
		AC_MSG_NOTICE([Not checking for Intel Mic: Manually configured])
		with_mic_cflags="-I$withval/include"
		with_mic_ldpath="-L$withval/lib/Linux"
		with_mic_libs="-lMicAccessSDK -lscif -lpthread"
		with_mic="yes"
	fi; fi; fi
],
[with_mic="yes"])
if test "x$with_mic" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_mic_cflags"
	AC_CHECK_HEADERS(MicAccessApi.h,[],[with_mic="no (MicAccessApi not found)"])
	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_mic" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"

	CPPFLAGS="$CPPFLAGS $with_mic_cflags"
	LDFLAGS="$LDFLAGS $with_mic_ldpath"

	AC_CHECK_LIB(MicAccessSDK, MicInitAPI,
			[with_mic_ldpath="$with_mic_ldpath"
			with_mic_libs="-lMicAccessSDK -lscif -lpthread"],
			[with_mic="no (symbol MicInitAPI not found)"],[-lscif -lpthread])

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_mic" = "xyes"
then
	BUILD_WITH_MIC_CPPFLAGS="$with_mic_cflags"
	BUILD_WITH_MIC_LIBPATH="$with_mic_ldpath"
	BUILD_WITH_MIC_LDADD="$with_mic_libs"
	AC_SUBST(BUILD_WITH_MIC_CPPFLAGS)
	AC_SUBST(BUILD_WITH_MIC_LIBPATH)
	AC_SUBST(BUILD_WITH_MIC_LDADD)
fi
#}}}

# --with-libvarnish {{{
with_libvarnish_cppflags=""
with_libvarnish_cflags=""
with_libvarnish_libs=""
AC_ARG_WITH(libvarnish, [AS_HELP_STRING([--with-libvarnish@<:@=PREFIX@:>@], [Path to libvarnish.])],
[
	if test "x$withval" = "xno"
	then
		with_libvarnish="no"
	else if test "x$withval" = "xyes"
	then
		with_libvarnish="use_pkgconfig"
	else if test -d "$with_libvarnish/lib"
	then
		AC_MSG_NOTICE([Not checking for libvarnish: Manually configured])
		with_libvarnish_cflags="-I$withval/include"
		with_libvarnish_libs="-L$withval/lib -lvarnishapi"
		with_libvarnish="yes"
	fi; fi; fi
],
[with_libvarnish="use_pkgconfig"])

# configure using pkg-config
if test "x$with_libvarnish" = "xuse_pkgconfig"
then
	if test "x$PKG_CONFIG" = "x"
	then
		with_libvarnish="no (Don't have pkg-config)"
	fi
fi
if test "x$with_libvarnish" = "xuse_pkgconfig"
then
	AC_MSG_NOTICE([Checking for varnishapi using $PKG_CONFIG])
	$PKG_CONFIG --exists 'varnishapi' 2>/dev/null
	if test $? -ne 0
	then
		with_libvarnish="no (pkg-config doesn't know varnishapi)"
	fi
fi
if test "x$with_libvarnish" = "xuse_pkgconfig"
then
	with_libvarnish_cflags="`$PKG_CONFIG --cflags 'varnishapi'`"
	if test $? -ne 0
	then
		with_libvarnish="no ($PKG_CONFIG failed)"
	fi
	with_libvarnish_libs="`$PKG_CONFIG --libs 'varnishapi'`"
	if test $? -ne 0
	then
		with_libvarnish="no ($PKG_CONFIG failed)"
	fi
fi
if test "x$with_libvarnish" = "xuse_pkgconfig"
then
	with_libvarnish="yes"
fi

# with_libvarnish_cflags and with_libvarnish_libs are set up now, let's do
# the actual checks.
if test "x$with_libvarnish" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"

	CPPFLAGS="$CPPFLAGS $with_libvarnish_cflags"

	AC_CHECK_HEADERS(varnish/vapi/vsc.h,
		[AC_DEFINE([HAVE_VARNISH_V4], [1], [Varnish 4 API support])],
		[AC_CHECK_HEADERS(varnish/vsc.h,
			[AC_DEFINE([HAVE_VARNISH_V3], [1], [Varnish 3 API support])],
			[AC_CHECK_HEADERS(varnish/varnishapi.h,
				[AC_DEFINE([HAVE_VARNISH_V2], [1], [Varnish 2 API support])],
				[with_libvarnish="no (found none of the varnish header files)"])])])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libvarnish" = "xyes"
then
	BUILD_WITH_LIBVARNISH_CFLAGS="$with_libvarnish_cflags"
	BUILD_WITH_LIBVARNISH_LIBS="$with_libvarnish_libs"
	AC_SUBST(BUILD_WITH_LIBVARNISH_CFLAGS)
	AC_SUBST(BUILD_WITH_LIBVARNISH_LIBS)
fi
# }}}

# pkg-config --exists 'libxml-2.0'; pkg-config --exists libvirt {{{
with_libxml2="no (pkg-config isn't available)"
with_libxml2_cflags=""
with_libxml2_ldflags=""
with_libvirt="no (pkg-config isn't available)"
with_libvirt_cflags=""
with_libvirt_ldflags=""
if test "x$PKG_CONFIG" != "x"
then
	$PKG_CONFIG --exists 'libxml-2.0' 2>/dev/null
	if test "$?" = "0"
	then
		with_libxml2="yes"
	else
		with_libxml2="no (pkg-config doesn't know libxml-2.0)"
	fi

	$PKG_CONFIG --exists libvirt 2>/dev/null
	if test "$?" = "0"
	then
		with_libvirt="yes"
	else
		with_libvirt="no (pkg-config doesn't know libvirt)"
	fi
fi
if test "x$with_libxml2" = "xyes"
then
	with_libxml2_cflags="`$PKG_CONFIG --cflags libxml-2.0`"
	if test $? -ne 0
	then
		with_libxml2="no"
	fi
	with_libxml2_ldflags="`$PKG_CONFIG --libs libxml-2.0`"
	if test $? -ne 0
	then
		with_libxml2="no"
	fi
fi
if test "x$with_libxml2" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libxml2_cflags"

	AC_CHECK_HEADERS(libxml/parser.h, [],
		      [with_libxml2="no (libxml/parser.h not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libxml2" = "xyes"
then
	SAVE_CFLAGS="$CFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"

	CFLAGS="$CFLAGS $with_libxml2_cflags"
	LDFLAGS="$LDFLAGS $with_libxml2_ldflags"

	AC_CHECK_LIB(xml2, xmlXPathEval,
		     [with_libxml2="yes"],
		     [with_libxml2="no (symbol xmlXPathEval not found)"])

	CFLAGS="$SAVE_CFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
fi
dnl Add the right compiler flags and libraries.
if test "x$with_libxml2" = "xyes"; then
	BUILD_WITH_LIBXML2_CFLAGS="$with_libxml2_cflags"
	BUILD_WITH_LIBXML2_LIBS="$with_libxml2_ldflags"
	AC_SUBST(BUILD_WITH_LIBXML2_CFLAGS)
	AC_SUBST(BUILD_WITH_LIBXML2_LIBS)
fi
if test "x$with_libvirt" = "xyes"
then
	with_libvirt_cflags="`$PKG_CONFIG --cflags libvirt`"
	if test $? -ne 0
	then
		with_libvirt="no"
	fi
	with_libvirt_ldflags="`$PKG_CONFIG --libs libvirt`"
	if test $? -ne 0
	then
		with_libvirt="no"
	fi
fi
if test "x$with_libvirt" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libvirt_cflags"

	AC_CHECK_HEADERS(libvirt/libvirt.h, [],
		      [with_libvirt="no (libvirt/libvirt.h not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libvirt" = "xyes"
then
	SAVE_CFLAGS="$CFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"

	CFLAGS="$CFLAGS $with_libvirt_cflags"
	LDFLAGS="$LDFLAGS $with_libvirt_ldflags"

	AC_CHECK_LIB(virt, virDomainBlockStats,
		     [with_libvirt="yes"],
		     [with_libvirt="no (symbol virDomainBlockStats not found)"])

	CFLAGS="$SAVE_CFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
fi
dnl Add the right compiler flags and libraries.
if test "x$with_libvirt" = "xyes"; then
	BUILD_WITH_LIBVIRT_CFLAGS="$with_libvirt_cflags"
	BUILD_WITH_LIBVIRT_LIBS="$with_libvirt_ldflags"
	AC_SUBST(BUILD_WITH_LIBVIRT_CFLAGS)
	AC_SUBST(BUILD_WITH_LIBVIRT_LIBS)
fi
# }}}

# $PKG_CONFIG --exists OpenIPMIpthread {{{
with_libopenipmipthread="yes"
with_libopenipmipthread_cflags=""
with_libopenipmipthread_libs=""

AC_MSG_CHECKING([for pkg-config])
temp_result="no"
if test "x$PKG_CONFIG" = "x"
then
	with_libopenipmipthread="no"
	temp_result="no"
else
	temp_result="$PKG_CONFIG"
fi
AC_MSG_RESULT([$temp_result])

if test "x$with_libopenipmipthread" = "xyes"
then
	AC_MSG_CHECKING([for libOpenIPMIpthread])
	$PKG_CONFIG --exists OpenIPMIpthread 2>/dev/null
	if test "$?" != "0"
	then
		with_libopenipmipthread="no (pkg-config doesn't know OpenIPMIpthread)"
	fi
	AC_MSG_RESULT([$with_libopenipmipthread])
fi

if test "x$with_libopenipmipthread" = "xyes"
then
	AC_MSG_CHECKING([for libOpenIPMIpthread CFLAGS])
	temp_result="`$PKG_CONFIG --cflags OpenIPMIpthread`"
	if test "$?" = "0"
	then
		with_libopenipmipthread_cflags="$temp_result"
	else
		with_libopenipmipthread="no ($PKG_CONFIG --cflags OpenIPMIpthread failed)"
		temp_result="$PKG_CONFIG --cflags OpenIPMIpthread failed"
	fi
	AC_MSG_RESULT([$temp_result])
fi

if test "x$with_libopenipmipthread" = "xyes"
then
	AC_MSG_CHECKING([for libOpenIPMIpthread LDFLAGS])
	temp_result="`$PKG_CONFIG --libs OpenIPMIpthread`"
	if test "$?" = "0"
	then
		with_libopenipmipthread_ldflags="$temp_result"
	else
		with_libopenipmipthread="no ($PKG_CONFIG --libs OpenIPMIpthread failed)"
		temp_result="$PKG_CONFIG --libs OpenIPMIpthread failed"
	fi
	AC_MSG_RESULT([$temp_result])
fi

if test "x$with_libopenipmipthread" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libopenipmipthread_cflags"

	AC_CHECK_HEADERS(OpenIPMI/ipmi_smi.h,
			 [with_libopenipmipthread="yes"],
			 [with_libopenipmipthread="no (OpenIPMI/ipmi_smi.h not found)"],
[#include <OpenIPMI/ipmiif.h>
#include <OpenIPMI/ipmi_err.h>
#include <OpenIPMI/ipmi_posix.h>
#include <OpenIPMI/ipmi_conn.h>
])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libopenipmipthread" = "xyes"
then
	BUILD_WITH_OPENIPMI_CFLAGS="$with_libopenipmipthread_cflags"
	BUILD_WITH_OPENIPMI_LIBS="$with_libopenipmipthread_ldflags"
	AC_SUBST(BUILD_WITH_OPENIPMI_CFLAGS)
	AC_SUBST(BUILD_WITH_OPENIPMI_LIBS)
fi
# }}}

# --with-libatasmart {{{
with_libatasmart_cppflags=""
with_libatasmart_ldflags=""
AC_ARG_WITH(libatasmart, [AS_HELP_STRING([--with-libatasmart@<:@=PREFIX@:>@], [Path to libatasmart.])],
[
	if test "x$withval" != "xno" && test "x$withval" != "xyes"
	then
		with_libatasmart_cppflags="-I$withval/include"
		with_libatasmart_ldflags="-L$withval/lib"
		with_libatasmart="yes"
	else
		with_libatasmart="$withval"
	fi
],
[
	if test "x$ac_system" = "xLinux"
	then
		with_libatasmart="yes"
	else
		with_libatasmart="no (Linux only library)"
	fi
])
if test "x$with_libatasmart" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libatasmart_cppflags"

	AC_CHECK_HEADERS(atasmart.h, [with_libatasmart="yes"], [with_libatasmart="no (atasmart.h not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libatasmart" = "xyes"
then
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"
	CPPFLAGS="$CPPFLAGS $with_libatasmart_cppflags"
	LDFLAGS="$LDFLAGS $with_libatasmart_ldflags"

	AC_CHECK_LIB(atasmart, sk_disk_open, [with_libatasmart="yes"], [with_libatasmart="no (Symbol 'sk_disk_open' not found)"])

	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
fi
if test "x$with_libatasmart" = "xyes"
then
	BUILD_WITH_LIBATASMART_CPPFLAGS="$with_libatasmart_cppflags"
	BUILD_WITH_LIBATASMART_LDFLAGS="$with_libatasmart_ldflags"
	BUILD_WITH_LIBATASMART_LIBS="-latasmart"
	AC_SUBST(BUILD_WITH_LIBATASMART_CPPFLAGS)
	AC_SUBST(BUILD_WITH_LIBATASMART_LDFLAGS)
	AC_SUBST(BUILD_WITH_LIBATASMART_LIBS)
	AC_DEFINE(HAVE_LIBATASMART, 1, [Define if libatasmart is present and usable.])
fi
AM_CONDITIONAL(BUILD_WITH_LIBATASMART, test "x$with_libatasmart" = "xyes")
# }}}

PKG_CHECK_MODULES([LIBNOTIFY], [libnotify],
		[with_libnotify="yes"],
		[if test "x$LIBNOTIFY_PKG_ERRORS" = "x"; then
			 with_libnotify="no"
		 else
			 with_libnotify="no ($LIBNOTIFY_PKG_ERRORS)"
		 fi])

# Check for enabled/disabled features
#

# AC_COLLECTD(name, enable/disable, info-text, feature/module)
# ------------------------------------------------------------
dnl
m4_define([my_toupper], [m4_translit([$1], m4_defn([m4_cr_letters]), m4_defn([m4_cr_LETTERS]))])
dnl
AC_DEFUN(
	[AC_COLLECTD],
	[
	m4_if([$1], [], [AC_FATAL([AC_COLLECTD([$1], [$2], [$3], [$4]): 1st argument must not be empty])])dnl
	m4_if(
		[$2],
		[enable],
		[dnl
		m4_define([EnDis],[disabled])dnl
		m4_define([YesNo],[no])dnl
		],dnl
		[m4_if(
			[$2],
			[disable],
			[dnl
			m4_define([EnDis],[enabled])dnl
			m4_define([YesNo],[yes])dnl
			],
			[dnl
			AC_FATAL([AC_COLLECTD([$1], [$2], [$3], [$4]): 2nd argument must be either enable or disable])dnl
			]dnl
		)]dnl
	)dnl
	m4_if([$3], [feature], [],
		[m4_if(
			[$3], [module], [],
			[dnl
			AC_FATAL([AC_COLLECTD([$1], [$2], [$3], [$4]): 3rd argument must be either feature or disable])dnl
			]dnl
		)]dnl
	)dnl
	AC_ARG_ENABLE(
		[$1],
		AS_HELP_STRING([--$2-$1], [$2 $4 (EnDis by def)]),
		[],
		enable_$1='[YesNo]'dnl
	)# AC_ARG_ENABLE
if test "x$enable_$1" = "xno"
then
	collectd_$1=0
else
	if test "x$enable_$1" = "xyes"
	then
		collectd_$1=1
	else
		AC_MSG_NOTICE([please specify either --enable-$1 or --disable-$1; enabling $1.])
		collectd_$1=1
		enable_$1='yes'
	fi
fi
	AC_DEFINE_UNQUOTED([COLLECT_]my_toupper([$1]), [$collectd_$1], [wether or not to enable $3 $4])
	AM_CONDITIONAL([BUILD_]my_toupper([$3])[_]my_toupper([$1]), [test "x$enable_$1" = "xyes"])dnl
	]dnl
)# AC_COLLECTD(name, enable/disable, info-text, feature/module)

# AC_PLUGIN(name, default, info)
# ------------------------------------------------------------
dnl
AC_DEFUN(
  [AC_PLUGIN],
  [
    enable_plugin="no"
    force="no"
    AC_ARG_ENABLE([$1], AS_HELP_STRING([--enable-$1],[$3]),
    [
     if test "x$enableval" = "xyes"
     then
	     enable_plugin="yes"
     else if test "x$enableval" = "xforce"
     then
	     enable_plugin="yes"
	     force="yes"
     else
	     enable_plugin="no (disabled on command line)"
     fi; fi
    ],
    [
	 if test "x$enable_all_plugins" = "xauto"
	 then
	     if test "x$2" = "xyes"
	     then
		     enable_plugin="yes"
	     else
		     enable_plugin="no"
	     fi
	 else
	     enable_plugin="$enable_all_plugins"
	 fi
    ])
    if test "x$enable_plugin" = "xyes"
    then
	    if test "x$2" = "xyes" || test "x$force" = "xyes"
	    then
		    AC_DEFINE([HAVE_PLUGIN_]my_toupper([$1]), 1, [Define to 1 if the $1 plugin is enabled.])
		    if test "x$2" != "xyes"
		    then
			    dependency_warning="yes"
		    fi
	    else # User passed "yes" but dependency checking yielded "no" => Dependency problem.
		    dependency_error="yes"
		    enable_plugin="no (dependency error)"
	    fi
    fi
    AM_CONDITIONAL([BUILD_PLUGIN_]my_toupper([$1]), test "x$enable_plugin" = "xyes")
    enable_$1="$enable_plugin"
  ]
)# AC_PLUGIN(name, default, info)

m4_divert_once([HELP_ENABLE], [
collectd features:])
# FIXME: Remove these calls to `AC_COLLECTD' and then remove that macro.
AC_COLLECTD([debug],     [enable],  [feature], [debugging])
AC_COLLECTD([daemon],    [disable], [feature], [daemon mode])
AC_COLLECTD([getifaddrs],[enable],  [feature], [getifaddrs under Linux])

dependency_warning="no"
dependency_error="no"

plugin_ascent="no"
plugin_barometer="no"
plugin_battery="no"
plugin_bind="no"
plugin_ceph="no"
plugin_cgroups="no"
plugin_conntrack="no"
plugin_contextswitch="no"
plugin_cpu="no"
plugin_cpufreq="no"
plugin_curl_json="no"
plugin_curl_xml="no"
plugin_df="no"
plugin_disk="no"
plugin_drbd="no"
plugin_entropy="no"
plugin_ethstat="no"
plugin_fscache="no"
plugin_interface="no"
plugin_ipmi="no"
plugin_ipvs="no"
plugin_irq="no"
plugin_load="no"
plugin_log_logstash="no"
plugin_memory="no"
plugin_multimeter="no"
plugin_nfs="no"
plugin_numa="no"
plugin_perl="no"
plugin_processes="no"
plugin_protocols="no"
plugin_serial="no"
plugin_swap="no"
plugin_tape="no"
plugin_tcpconns="no"
plugin_ted="no"
plugin_thermal="no"
plugin_turbostat="no"
plugin_uptime="no"
plugin_users="no"
plugin_virt="no"
plugin_vmem="no"
plugin_vserver="no"
plugin_wireless="no"
plugin_zfs_arc="no"
plugin_zookeeper="no"

# Linux
if test "x$ac_system" = "xLinux"
then
	plugin_battery="yes"
	plugin_conntrack="yes"
	plugin_contextswitch="yes"
	plugin_cgroups="yes"
	plugin_cpu="yes"
	plugin_cpufreq="yes"
	plugin_disk="yes"
	plugin_drbd="yes"
	plugin_entropy="yes"
	plugin_fscache="yes"
	plugin_interface="yes"
	plugin_ipc="yes"
	plugin_irq="yes"
	plugin_load="yes"
	plugin_lvm="yes"
	plugin_memory="yes"
	plugin_nfs="yes"
	plugin_numa="yes"
	plugin_processes="yes"
	plugin_protocols="yes"
	plugin_serial="yes"
	plugin_swap="yes"
	plugin_tcpconns="yes"
	plugin_thermal="yes"
	plugin_uptime="yes"
	plugin_vmem="yes"
	plugin_vserver="yes"
	plugin_wireless="yes"
	plugin_zfs_arc="yes"

	if test "x$have_linux_ip_vs_h" = "xyes" || test "x$have_net_ip_vs_h" = "xyes" || test "x$have_ip_vs_h" = "xyes"
	then
		plugin_ipvs="yes"
	fi
	if test "x$c_cv_have_usable_asm_msrindex_h" = "xyes" && test "x$have_cpuid_h" = "xyes"
	then
		plugin_turbostat="yes"
	fi
fi

if test "x$ac_system" = "xOpenBSD"
then
	plugin_tcpconns="yes"
fi

# Mac OS X devices
if test "x$with_libiokit" = "xyes"
then
	plugin_battery="yes"
	plugin_disk="yes"
fi

# AIX

if test "x$ac_system" = "xAIX"
then
        plugin_tcpconns="yes"
        plugin_ipc="yes"
fi

# FreeBSD

if test "x$ac_system" = "xFreeBSD"
then
        plugin_zfs_arc="yes"
fi


if test "x$with_perfstat" = "xyes"
then
	plugin_cpu="yes"
	plugin_contextswitch="yes"
	plugin_disk="yes"
	plugin_memory="yes"
	plugin_swap="yes"
	plugin_interface="yes"
	plugin_load="yes"
	plugin_uptime="yes"
fi

if test "x$with_procinfo" = "xyes"
then
	plugin_processes="yes"
fi

# Solaris
if test "x$with_kstat" = "xyes"
then
	plugin_nfs="yes"
	plugin_processes="yes"
	plugin_uptime="yes"
	plugin_zfs_arc="yes"
fi

if test "x$with_devinfo$with_kstat" = "xyesyes"
then
	plugin_cpu="yes"
	plugin_disk="yes"
	plugin_interface="yes"
	plugin_memory="yes"
	plugin_tape="yes"
fi

# libi2c-dev
with_libi2c="no"
if test "x$ac_system" = "xLinux"
then
AC_CHECK_DECL(i2c_smbus_read_i2c_block_data,
	[with_libi2c="yes"],
	[with_libi2c="no (symbol i2c_smbus_read_i2c_block_data not found - have you installed libi2c-dev ?)"],
	[[#include <stdlib.h>
	#include <linux/i2c-dev.h>]])
fi

if test "x$with_libi2c" = "xyes"
then
	plugin_barometer="yes"
fi


# libstatgrab
if test "x$with_libstatgrab" = "xyes"
then
	plugin_cpu="yes"
	plugin_disk="yes"
	plugin_interface="yes"
	plugin_load="yes"
	plugin_memory="yes"
	plugin_swap="yes"
	plugin_users="yes"
fi

if test "x$with_libcurl" = "xyes" && test "x$with_libxml2" = "xyes"
then
	plugin_ascent="yes"
	if test "x$have_strptime" = "xyes"
	then
		plugin_bind="yes"
	fi
fi

if test "x$with_libopenipmipthread" = "xyes"
then
	plugin_ipmi="yes"
fi

if test "x$with_libcurl" = "xyes" && test "x$with_libyajl" = "xyes"
then
	plugin_curl_json="yes"
fi

if test "x$with_libcurl" = "xyes" && test "x$with_libxml2" = "xyes"
then
	plugin_curl_xml="yes"
fi

if test "x$with_libyajl" = "xyes"
then
	plugin_ceph="yes"
fi

if test "x$have_processor_info" = "xyes"
then
	plugin_cpu="yes"
fi
if test "x$have_sysctl" = "xyes"
then
	plugin_cpu="yes"
	plugin_memory="yes"
	plugin_uptime="yes"
	if test "x$ac_system" = "xDarwin"
	then
		plugin_swap="yes"
	fi
fi
if test "x$have_sysctlbyname" = "xyes"
then
	plugin_contextswitch="yes"
	plugin_cpu="yes"
	plugin_memory="yes"
	plugin_tcpconns="yes"
fi

# Df plugin: Check if we know how to determine mount points first.
#if test "x$have_listmntent" = "xyes"; then
#	plugin_df="yes"
#fi
if test "x$have_getvfsstat" = "xyes" || test "x$have_getfsstat" = "xyes"
then
	plugin_df="yes"
fi
if test "x$c_cv_have_two_getmntent" = "xyes" || test "x$have_getmntent" = "xgen" || test "x$have_getmntent" = "xsun"
then
	plugin_df="yes"
fi
#if test "x$have_getmntent" = "xseq"
#then
#	plugin_df="yes"
#fi
if test "x$c_cv_have_one_getmntent" = "xyes"
then
	plugin_df="yes"
fi

# Df plugin: Check if we have either `statfs' or `statvfs' second.
if test "x$plugin_df" = "xyes"
then
	plugin_df="no"
	if test "x$have_statfs" = "xyes"
	then
		plugin_df="yes"
	fi
	if test "x$have_statvfs" = "xyes"
	then
		plugin_df="yes"
	fi
fi

if test "x$have_linux_sockios_h$have_linux_ethtool_h" = "xyesyes"
then
	plugin_ethstat="yes"
fi

if test "x$have_getifaddrs" = "xyes"
then
	plugin_interface="yes"
fi

if test "x$have_getloadavg" = "xyes"
then
	plugin_load="yes"
fi

if test "x$with_libyajl" = "xyes"
then
	plugin_log_logstash="yes"
fi

if test "x$c_cv_have_libperl$c_cv_have_perl_ithreads" = "xyesyes"
then
	plugin_perl="yes"
fi

# Mac OS X memory interface
if test "x$have_host_statistics" = "xyes"
then
	plugin_memory="yes"
fi

if test "x$have_termios_h" = "xyes"
then
	if test "x$ac_system" != "xAIX"
	then
		plugin_multimeter="yes"
	fi
	plugin_ted="yes"
fi

if test "x$have_thread_info" = "xyes"
then
	plugin_processes="yes"
fi

if test "x$with_kvm_getprocs" = "xyes" && test "x$have_struct_kinfo_proc_freebsd" = "xyes"
then
	plugin_processes="yes"
fi

if test "x$with_kvm_getprocs" = "xyes" && test "x$have_struct_kinfo_proc_openbsd" = "xyes"
then
	plugin_processes="yes"
fi

if test "x$with_kvm_getswapinfo" = "xyes"
then
	plugin_swap="yes"
fi

if test "x$have_swapctl" = "xyes" && test "x$c_cv_have_swapctl_two_args" = "xyes"
then
	plugin_swap="yes"
fi

if test "x$with_kvm_openfiles$with_kvm_nlist" = "xyesyes"
then
	plugin_tcpconns="yes"
fi

if test "x$have_getutent" = "xyes"
then
	plugin_users="yes"
fi
if test "x$have_getutxent" = "xyes"
then
	plugin_users="yes"
fi

if test "x$with_libxml2" = "xyes" && test "x$with_libvirt" = "xyes"
then
	plugin_virt="yes"
fi


m4_divert_once([HELP_ENABLE], [
collectd plugins:])

AC_ARG_ENABLE([all-plugins],
		AS_HELP_STRING([--enable-all-plugins],[enable all plugins (auto by def)]),
		[
		 if test "x$enableval" = "xyes"
		 then
			 enable_all_plugins="yes"
		 else if test "x$enableval" = "xauto"
		 then
			 enable_all_plugins="auto"
		 else
			 enable_all_plugins="no"
		 fi; fi
		],
		[enable_all_plugins="auto"])

m4_divert_once([HELP_ENABLE], [])

AC_PLUGIN([aggregation], [yes],                [Aggregation plugin])
AC_PLUGIN([amqp],        [$with_librabbitmq],  [AMQP output plugin])
AC_PLUGIN([apache],      [$with_libcurl],      [Apache httpd statistics])
AC_PLUGIN([apcups],      [yes],                [Statistics of UPSes by APC])
AC_PLUGIN([apple_sensors], [$with_libiokit],   [Apple's hardware sensors])
AC_PLUGIN([aquaero],     [$with_libaquaero5],  [Aquaero's hardware sensors])
AC_PLUGIN([ascent],      [$plugin_ascent],     [AscentEmu player statistics])
AC_PLUGIN([barometer],   [$plugin_barometer],  [Barometer sensor on I2C])
AC_PLUGIN([battery],     [$plugin_battery],    [Battery statistics])
AC_PLUGIN([bind],        [$plugin_bind],       [ISC Bind nameserver statistics])
AC_PLUGIN([ceph],        [$plugin_ceph],       [Ceph daemon statistics])
AC_PLUGIN([conntrack],   [$plugin_conntrack],  [nf_conntrack statistics])
AC_PLUGIN([contextswitch], [$plugin_contextswitch], [context switch statistics])
AC_PLUGIN([cpufreq],     [$plugin_cpufreq],    [CPU frequency statistics])
AC_PLUGIN([cpu],         [$plugin_cpu],        [CPU usage statistics])
AC_PLUGIN([csv],         [yes],                [CSV output plugin])
AC_PLUGIN([curl],        [$with_libcurl],      [CURL generic web statistics])
AC_PLUGIN([curl_json],   [$plugin_curl_json],    [CouchDB statistics])
AC_PLUGIN([curl_xml],   [$plugin_curl_xml],    [CURL generic xml statistics])
AC_PLUGIN([cgroups],     [$plugin_cgroups],    [CGroups CPU usage accounting])
AC_PLUGIN([dbi],         [$with_libdbi],       [General database statistics])
AC_PLUGIN([df],          [$plugin_df],         [Filesystem usage statistics])
AC_PLUGIN([disk],        [$plugin_disk],       [Disk usage statistics])
AC_PLUGIN([drbd],        [$plugin_drbd],       [DRBD statistics])
AC_PLUGIN([dns],         [$with_libpcap],      [DNS traffic analysis])
AC_PLUGIN([email],       [yes],                [EMail statistics])
AC_PLUGIN([entropy],     [$plugin_entropy],    [Entropy statistics])
AC_PLUGIN([ethstat],     [$plugin_ethstat],    [Stats from NIC driver])
AC_PLUGIN([exec],        [yes],                [Execution of external programs])
AC_PLUGIN([fhcount],     [yes],                [File handles statistics])
AC_PLUGIN([filecount],   [yes],                [Count files in directories])
AC_PLUGIN([fscache],     [$plugin_fscache],    [fscache statistics])
AC_PLUGIN([gmond],       [$with_libganglia],   [Ganglia plugin])
AC_PLUGIN([hddtemp],     [yes],                [Query hddtempd])
AC_PLUGIN([interface],   [$plugin_interface],  [Interface traffic statistics])
AC_PLUGIN([ipc],         [$plugin_ipc],        [IPC statistics])
AC_PLUGIN([ipmi],        [$plugin_ipmi],       [IPMI sensor statistics])
AC_PLUGIN([iptables],    [$with_libiptc],      [IPTables rule counters])
AC_PLUGIN([ipvs],        [$plugin_ipvs],       [IPVS connection statistics])
AC_PLUGIN([irq],         [$plugin_irq],        [IRQ statistics])
AC_PLUGIN([java],        [$with_java],         [Embed the Java Virtual Machine])
AC_PLUGIN([load],        [$plugin_load],       [System load])
AC_PLUGIN([logfile],     [yes],                [File logging plugin])
AC_PLUGIN([log_logstash], [$plugin_log_logstash], [Logstash json_event compatible logging])
AC_PLUGIN([lpar],        [$with_perfstat],     [AIX logical partitions statistics])
AC_PLUGIN([lvm],         [$with_liblvm2app],   [LVM statistics])
AC_PLUGIN([madwifi],     [$have_linux_wireless_h], [Madwifi wireless statistics])
AC_PLUGIN([match_empty_counter], [yes],        [The empty counter match])
AC_PLUGIN([match_hashed], [yes],               [The hashed match])
AC_PLUGIN([match_regex], [yes],                [The regex match])
AC_PLUGIN([match_timediff], [yes],             [The timediff match])
AC_PLUGIN([match_value], [yes],                [The value match])
AC_PLUGIN([mbmon],       [yes],                [Query mbmond])
AC_PLUGIN([md],          [$have_linux_raid_md_u_h], [md (Linux software RAID) devices])
AC_PLUGIN([memcachec],   [$with_libmemcached], [memcachec statistics])
AC_PLUGIN([memcached],   [yes],                [memcached statistics])
AC_PLUGIN([memory],      [$plugin_memory],     [Memory usage])
AC_PLUGIN([mic],         [$with_mic],          [Intel Many Integrated Core stats])
AC_PLUGIN([modbus],      [$with_libmodbus],    [Modbus plugin])
AC_PLUGIN([multimeter],  [$plugin_multimeter], [Read multimeter values])
AC_PLUGIN([mysql],       [$with_libmysql],     [MySQL statistics])
AC_PLUGIN([netapp],      [$with_libnetapp],    [NetApp plugin])
AC_PLUGIN([netlink],     [$with_libmnl],       [Enhanced Linux network statistics])
AC_PLUGIN([network],     [yes],                [Network communication plugin])
AC_PLUGIN([nfs],         [$plugin_nfs],        [NFS statistics])
AC_PLUGIN([nginx],       [$with_libcurl],      [nginx statistics])
AC_PLUGIN([notify_desktop], [$with_libnotify], [Desktop notifications])
AC_PLUGIN([notify_email], [$with_libesmtp],    [Email notifier])
AC_PLUGIN([ntpd],        [yes],                [NTPd statistics])
AC_PLUGIN([numa],        [$plugin_numa],       [NUMA virtual memory statistics])
AC_PLUGIN([nut],         [$with_libupsclient], [Network UPS tools statistics])
AC_PLUGIN([olsrd],       [yes],                [olsrd statistics])
AC_PLUGIN([onewire],     [$with_libowcapi],    [OneWire sensor statistics])
AC_PLUGIN([openldap],    [$with_libldap],      [OpenLDAP statistics])
AC_PLUGIN([openvpn],     [yes],                [OpenVPN client statistics])
AC_PLUGIN([oracle],      [$with_oracle],       [Oracle plugin])
AC_PLUGIN([perl],        [$plugin_perl],       [Embed a Perl interpreter])
AC_PLUGIN([pf],          [$have_net_pfvar_h],  [BSD packet filter (PF) statistics])
# FIXME: Check for libevent, too.
AC_PLUGIN([pinba],       [$have_protoc_c],     [Pinba statistics])
AC_PLUGIN([ping],        [$with_liboping],     [Network latency statistics])
AC_PLUGIN([postgresql],  [$with_libpq],        [PostgreSQL database statistics])
AC_PLUGIN([powerdns],    [yes],                [PowerDNS statistics])
AC_PLUGIN([processes],   [$plugin_processes],  [Process statistics])
AC_PLUGIN([protocols],   [$plugin_protocols],  [Protocol (IP, TCP, ...) statistics])
AC_PLUGIN([python],      [$with_python],       [Embed a Python interpreter])
AC_PLUGIN([redis],       [$with_libhiredis],    [Redis plugin])
AC_PLUGIN([routeros],    [$with_librouteros],  [RouterOS plugin])
AC_PLUGIN([rrdcached],   [$librrd_rrdc_update], [RRDTool output plugin])
AC_PLUGIN([rrdtool],     [$with_librrd],       [RRDTool output plugin])
AC_PLUGIN([sensors],     [$with_libsensors],   [lm_sensors statistics])
AC_PLUGIN([serial],      [$plugin_serial],     [serial port traffic])
AC_PLUGIN([sigrok],      [$with_libsigrok],    [sigrok acquisition sources])
AC_PLUGIN([smart],       [$with_libatasmart],  [SMART statistics])
AC_PLUGIN([snmp],        [$with_libnetsnmp],   [SNMP querying plugin])
AC_PLUGIN([statsd],      [yes],                [StatsD plugin])
AC_PLUGIN([swap],        [$plugin_swap],       [Swap usage statistics])
AC_PLUGIN([syslog],      [$have_syslog],       [Syslog logging plugin])
AC_PLUGIN([table],       [yes],                [Parsing of tabular data])
AC_PLUGIN([tail],        [yes],                [Parsing of logfiles])
AC_PLUGIN([tail_csv],    [yes],                [Parsing of CSV files])
AC_PLUGIN([tape],        [$plugin_tape],       [Tape drive statistics])
AC_PLUGIN([target_notification], [yes],        [The notification target])
AC_PLUGIN([target_replace], [yes],             [The replace target])
AC_PLUGIN([target_scale],[yes],                [The scale target])
AC_PLUGIN([target_set],  [yes],                [The set target])
AC_PLUGIN([target_v5upgrade], [yes],           [The v5upgrade target])
AC_PLUGIN([tcpconns],    [$plugin_tcpconns],   [TCP connection statistics])
AC_PLUGIN([teamspeak2],  [yes],                [TeamSpeak2 server statistics])
AC_PLUGIN([ted],         [$plugin_ted],        [Read The Energy Detective values])
AC_PLUGIN([thermal],     [$plugin_thermal],    [Linux ACPI thermal zone statistics])
AC_PLUGIN([threshold],   [yes],                [Threshold checking plugin])
AC_PLUGIN([tokyotyrant], [$with_libtokyotyrant],  [TokyoTyrant database statistics])
AC_PLUGIN([turbostat],   [$plugin_turbostat],  [Advanced statistic on Intel cpu states])
AC_PLUGIN([unixsock],    [yes],                [Unixsock communication plugin])
AC_PLUGIN([uptime],      [$plugin_uptime],     [Uptime statistics])
AC_PLUGIN([users],       [$plugin_users],      [User statistics])
AC_PLUGIN([uuid],        [yes],                [UUID as hostname plugin])
AC_PLUGIN([varnish],     [$with_libvarnish],   [Varnish cache statistics])
AC_PLUGIN([virt],        [$plugin_virt],       [Virtual machine statistics])
AC_PLUGIN([vmem],        [$plugin_vmem],       [Virtual memory statistics])
AC_PLUGIN([vserver],     [$plugin_vserver],    [Linux VServer statistics])
AC_PLUGIN([wireless],    [$plugin_wireless],   [Wireless statistics])
AC_PLUGIN([write_graphite], [yes],             [Graphite / Carbon output plugin])
AC_PLUGIN([write_http],  [$with_libcurl],      [HTTP output plugin])
AC_PLUGIN([write_kafka],  [$with_librdkafka],  [Kafka output plugin])
AC_PLUGIN([write_log], [yes],                  [Log output plugin])
AC_PLUGIN([write_mongodb], [$with_libmongoc],  [MongoDB output plugin])
AC_PLUGIN([write_redis], [$with_libhiredis],    [Redis output plugin])
AC_PLUGIN([write_riemann], [$have_protoc_c],   [Riemann output plugin])
AC_PLUGIN([write_sensu], [yes],                [Sensu output plugin])
AC_PLUGIN([write_tsdb],  [yes],                [TSDB output plugin])
AC_PLUGIN([xmms],        [$with_libxmms],      [XMMS statistics])
AC_PLUGIN([zfs_arc],     [$plugin_zfs_arc],    [ZFS ARC statistics])
AC_PLUGIN([zookeeper],   [yes],  	       [Zookeeper statistics])

dnl Default configuration file
# Load either syslog or logfile
LOAD_PLUGIN_SYSLOG=""
LOAD_PLUGIN_LOGFILE=""
LOAD_PLUGIN_LOG_LOGSTASH=""

AC_MSG_CHECKING([which default log plugin to load])
default_log_plugin="none"
if test "x$enable_syslog" = "xyes"
then
	default_log_plugin="syslog"
else
	LOAD_PLUGIN_SYSLOG="##"
fi

if test "x$enable_logfile" = "xyes"
then
	if test "x$default_log_plugin" = "xnone"
	then
		default_log_plugin="logfile"
	else
		LOAD_PLUGIN_LOGFILE="#"
	fi
else
	LOAD_PLUGIN_LOGFILE="##"
fi

if test "x$enable_log_logstash" = "xyes"
then
  LOAD_PLUGIN_LOG_LOGSTASH="#"
else
  LOAD_PLUGIN_LOG_LOGSTASH="##"
fi


AC_MSG_RESULT([$default_log_plugin])

AC_SUBST(LOAD_PLUGIN_SYSLOG)
AC_SUBST(LOAD_PLUGIN_LOGFILE)
AC_SUBST(LOAD_PLUGIN_LOG_LOGSTASH)

DEFAULT_LOG_LEVEL="info"
if test "x$enable_debug" = "xyes"
then
	DEFAULT_LOG_LEVEL="debug"
fi
AC_SUBST(DEFAULT_LOG_LEVEL)

# Load only one of rrdtool, network, csv in the default config.
LOAD_PLUGIN_RRDTOOL=""
LOAD_PLUGIN_NETWORK=""
LOAD_PLUGIN_CSV=""

AC_MSG_CHECKING([which default write plugin to load])
default_write_plugin="none"
if test "x$enable_rrdtool" = "xyes"
then
	default_write_plugin="rrdtool"
else
	LOAD_PLUGIN_RRDTOOL="##"
fi

if test "x$enable_network" = "xyes"
then
	if test "x$default_write_plugin" = "xnone"
	then
		default_write_plugin="network"
	else
		LOAD_PLUGIN_NETWORK="#"
	fi
else
	LOAD_PLUGIN_NETWORK="##"
fi

if test "x$enable_csv" = "xyes"
then
	if test "x$default_write_plugin" = "xnone"
	then
		default_write_plugin="csv"
	else
		LOAD_PLUGIN_CSV="#"
	fi
else
	LOAD_PLUGIN_CSV="##"
fi
AC_MSG_RESULT([$default_write_plugin])

AC_SUBST(LOAD_PLUGIN_RRDTOOL)
AC_SUBST(LOAD_PLUGIN_NETWORK)
AC_SUBST(LOAD_PLUGIN_CSV)

dnl ip_vs.h
if test "x$ac_system" = "xLinux" \
	&& test "x$have_linux_ip_vs_h$have_net_ip_vs_h$have_ip_vs_h" = "xnonono"
then
	enable_ipvs="$enable_ipvs (ip_vs.h not found)"
fi

if test "x$ip_vs_h_needs_kernel_cflags" = "xyes"
then
	enable_ipvs="$enable_ipvs (needs $KERNEL_CFLAGS)"
fi

dnl Perl bindings
PERL_BINDINGS_OPTIONS="PREFIX=${prefix}"
AC_ARG_WITH(perl-bindings, [AS_HELP_STRING([--with-perl-bindings@<:@=OPTIONS@:>@], [Options passed to "perl Makefile.PL".])],
[
	if test "x$withval" != "xno" && test "x$withval" != "xyes"
	then
		PERL_BINDINGS_OPTIONS="$withval"
		with_perl_bindings="yes"
	else
		with_perl_bindings="$withval"
	fi
],
[
	if test -n "$perl_interpreter"
	then
		with_perl_bindings="yes"
	else
		with_perl_bindings="no (no perl interpreter found)"
	fi
])
if test "x$with_perl_bindings" = "xyes"
then
	PERL_BINDINGS="perl"
else
	PERL_BINDINGS=""
fi
AC_SUBST(PERL_BINDINGS)
AC_SUBST(PERL_BINDINGS_OPTIONS)

dnl libcollectdclient
LCC_VERSION_MAJOR=`echo $PACKAGE_VERSION | cut -d'.' -f1`
LCC_VERSION_MINOR=`echo $PACKAGE_VERSION | cut -d'.' -f2`
LCC_VERSION_PATCH=`echo $PACKAGE_VERSION | cut -d'.' -f3`

LCC_VERSION_EXTRA=`echo $PACKAGE_VERSION | cut -d'.' -f4-`

LCC_VERSION_STRING="$LCC_VERSION_MAJOR.$LCC_VERSION_MINOR.$LCC_VERSION_PATCH"

AC_SUBST(LCC_VERSION_MAJOR)
AC_SUBST(LCC_VERSION_MINOR)
AC_SUBST(LCC_VERSION_PATCH)
AC_SUBST(LCC_VERSION_EXTRA)
AC_SUBST(LCC_VERSION_STRING)

AC_CONFIG_FILES(src/libcollectdclient/collectd/lcc_features.h)

AC_CONFIG_FILES([Makefile src/Makefile src/daemon/Makefile src/collectd.conf src/libcollectdclient/Makefile src/libcollectdclient/libcollectdclient.pc src/liboconfig/Makefile bindings/Makefile bindings/java/Makefile])
AC_OUTPUT

if test "x$with_librrd" = "xyes" \
	&& test "x$librrd_threadsafe" != "xyes"
then
	with_librrd="yes (warning: librrd is not thread-safe)"
fi

if test "x$with_libperl" = "xyes"
then
	with_libperl="yes (version `$perl_interpreter -MConfig -e 'print $Config{version};'`)"
else
	enable_perl="no (needs libperl)"
fi

if test "x$enable_perl" = "xno" && test "x$c_cv_have_perl_ithreads" = "xno"
then
	enable_perl="no (libperl doesn't support ithreads)"
fi

if test "x$with_perl_bindings" = "xyes" \
	&& test "x$PERL_BINDINGS_OPTIONS" != "x"
then
	with_perl_bindings="yes ($PERL_BINDINGS_OPTIONS)"
fi

cat <<EOF;

Configuration:
  Libraries:
    intel mic . . . . . . $with_mic
    libaquaero5 . . . . . $with_libaquaero5
    libatasmart . . . . . $with_libatasmart
    libcurl . . . . . . . $with_libcurl
    libdbi  . . . . . . . $with_libdbi
    libesmtp  . . . . . . $with_libesmtp
    libganglia  . . . . . $with_libganglia
    libgcrypt . . . . . . $with_libgcrypt
    libhal  . . . . . . . $with_libhal
    libhiredis  . . . . . $with_libhiredis
    libi2c-dev  . . . . . $with_libi2c
    libiokit  . . . . . . $with_libiokit
    libiptc . . . . . . . $with_libiptc
    libjvm  . . . . . . . $with_java
    libkstat  . . . . . . $with_kstat
    libkvm  . . . . . . . $with_libkvm
    libldap . . . . . . . $with_libldap
    liblvm2app  . . . . . $with_liblvm2app
    libmemcached  . . . . $with_libmemcached
    libmnl  . . . . . . . $with_libmnl
    libmodbus . . . . . . $with_libmodbus
    libmongoc . . . . . . $with_libmongoc
    libmysql  . . . . . . $with_libmysql
    libnetapp . . . . . . $with_libnetapp
    libnetsnmp  . . . . . $with_libnetsnmp
    libnotify . . . . . . $with_libnotify
    liboconfig  . . . . . $with_liboconfig
    libopenipmi . . . . . $with_libopenipmipthread
    liboping  . . . . . . $with_liboping
    libowcapi . . . . . . $with_libowcapi
    libpcap . . . . . . . $with_libpcap
    libperfstat . . . . . $with_perfstat
    libperl . . . . . . . $with_libperl
    libpq . . . . . . . . $with_libpq
    libpthread  . . . . . $with_libpthread
    librabbitmq . . . . . $with_librabbitmq
    librdkafka  . . . . . $with_librdkafka
    librouteros . . . . . $with_librouteros
    librrd  . . . . . . . $with_librrd
    libsensors  . . . . . $with_libsensors
    libsigrok   . . . . . $with_libsigrok
    libstatgrab . . . . . $with_libstatgrab
    libtokyotyrant  . . . $with_libtokyotyrant
    libudev . . . . . . . $with_libudev
    libupsclient  . . . . $with_libupsclient
    libvarnish  . . . . . $with_libvarnish
    libvirt . . . . . . . $with_libvirt
    libxml2 . . . . . . . $with_libxml2
    libxmms . . . . . . . $with_libxmms
    libyajl . . . . . . . $with_libyajl
    oracle  . . . . . . . $with_oracle
    protobuf-c  . . . . . $have_protoc_c
    python  . . . . . . . $with_python

  Features:
    daemon mode . . . . . $enable_daemon
    debug . . . . . . . . $enable_debug

  Bindings:
    perl  . . . . . . . . $with_perl_bindings

  Modules:
    aggregation . . . . . $enable_aggregation
    amqp    . . . . . . . $enable_amqp
    apache  . . . . . . . $enable_apache
    apcups  . . . . . . . $enable_apcups
    apple_sensors . . . . $enable_apple_sensors
    aquaero . . . . . . . $enable_aquaero
    ascent  . . . . . . . $enable_ascent
    barometer . . . . . . $enable_barometer
    battery . . . . . . . $enable_battery
    bind  . . . . . . . . $enable_bind
    ceph  . . . . . . . . $enable_ceph
    cgroups . . . . . . . $enable_cgroups
    conntrack . . . . . . $enable_conntrack
    contextswitch . . . . $enable_contextswitch
    cpu . . . . . . . . . $enable_cpu
    cpufreq . . . . . . . $enable_cpufreq
    csv . . . . . . . . . $enable_csv
    curl  . . . . . . . . $enable_curl
    curl_json . . . . . . $enable_curl_json
    curl_xml  . . . . . . $enable_curl_xml
    dbi . . . . . . . . . $enable_dbi
    df  . . . . . . . . . $enable_df
    disk  . . . . . . . . $enable_disk
    dns . . . . . . . . . $enable_dns
    drbd  . . . . . . . . $enable_drbd
    email . . . . . . . . $enable_email
    entropy . . . . . . . $enable_entropy
    ethstat . . . . . . . $enable_ethstat
    exec  . . . . . . . . $enable_exec
    fhcount . . . . . . . $enable_fhcount
    filecount . . . . . . $enable_filecount
    fscache . . . . . . . $enable_fscache
    gmond . . . . . . . . $enable_gmond
    hddtemp . . . . . . . $enable_hddtemp
    interface . . . . . . $enable_interface
    ipc . . . . . . . . . $enable_ipc
    ipmi  . . . . . . . . $enable_ipmi
    iptables  . . . . . . $enable_iptables
    ipvs  . . . . . . . . $enable_ipvs
    irq . . . . . . . . . $enable_irq
    java  . . . . . . . . $enable_java
    load  . . . . . . . . $enable_load
    logfile . . . . . . . $enable_logfile
    log_logstash  . . . . $enable_log_logstash
    lpar  . . . . . . . . $enable_lpar
    lvm . . . . . . . . . $enable_lvm
    madwifi . . . . . . . $enable_madwifi
    match_empty_counter . $enable_match_empty_counter
    match_hashed  . . . . $enable_match_hashed
    match_regex . . . . . $enable_match_regex
    match_timediff  . . . $enable_match_timediff
    match_value . . . . . $enable_match_value
    mbmon . . . . . . . . $enable_mbmon
    md  . . . . . . . . . $enable_md
    memcachec . . . . . . $enable_memcachec
    memcached . . . . . . $enable_memcached
    memory  . . . . . . . $enable_memory
    mic . . . . . . . . . $enable_mic
    modbus  . . . . . . . $enable_modbus
    multimeter  . . . . . $enable_multimeter
    mysql . . . . . . . . $enable_mysql
    netapp  . . . . . . . $enable_netapp
    netlink . . . . . . . $enable_netlink
    network . . . . . . . $enable_network
    nfs . . . . . . . . . $enable_nfs
    nginx . . . . . . . . $enable_nginx
    notify_desktop  . . . $enable_notify_desktop
    notify_email  . . . . $enable_notify_email
    ntpd  . . . . . . . . $enable_ntpd
    numa  . . . . . . . . $enable_numa
    nut . . . . . . . . . $enable_nut
    olsrd . . . . . . . . $enable_olsrd
    onewire . . . . . . . $enable_onewire
    openldap  . . . . . . $enable_openldap
    openvpn . . . . . . . $enable_openvpn
    oracle  . . . . . . . $enable_oracle
    perl  . . . . . . . . $enable_perl
    pf  . . . . . . . . . $enable_pf
    pinba . . . . . . . . $enable_pinba
    ping  . . . . . . . . $enable_ping
    postgresql  . . . . . $enable_postgresql
    powerdns  . . . . . . $enable_powerdns
    processes . . . . . . $enable_processes
    protocols . . . . . . $enable_protocols
    python  . . . . . . . $enable_python
    redis . . . . . . . . $enable_redis
    routeros  . . . . . . $enable_routeros
    rrdcached . . . . . . $enable_rrdcached
    rrdtool . . . . . . . $enable_rrdtool
    sensors . . . . . . . $enable_sensors
    serial  . . . . . . . $enable_serial
    sigrok  . . . . . . . $enable_sigrok
    smart . . . . . . . . $enable_smart
    snmp  . . . . . . . . $enable_snmp
    statsd  . . . . . . . $enable_statsd
    swap  . . . . . . . . $enable_swap
    syslog  . . . . . . . $enable_syslog
    table . . . . . . . . $enable_table
    tail_csv  . . . . . . $enable_tail_csv
    tail  . . . . . . . . $enable_tail
    tape  . . . . . . . . $enable_tape
    target_notification . $enable_target_notification
    target_replace  . . . $enable_target_replace
    target_scale  . . . . $enable_target_scale
    target_set  . . . . . $enable_target_set
    target_v5upgrade  . . $enable_target_v5upgrade
    tcpconns  . . . . . . $enable_tcpconns
    teamspeak2  . . . . . $enable_teamspeak2
    ted . . . . . . . . . $enable_ted
    thermal . . . . . . . $enable_thermal
    threshold . . . . . . $enable_threshold
    tokyotyrant . . . . . $enable_tokyotyrant
    turbostat . . . . . . $enable_turbostat
    unixsock  . . . . . . $enable_unixsock
    uptime  . . . . . . . $enable_uptime
    users . . . . . . . . $enable_users
    uuid  . . . . . . . . $enable_uuid
    varnish . . . . . . . $enable_varnish
    virt  . . . . . . . . $enable_virt
    vmem  . . . . . . . . $enable_vmem
    vserver . . . . . . . $enable_vserver
    wireless  . . . . . . $enable_wireless
    write_graphite  . . . $enable_write_graphite
    write_http  . . . . . $enable_write_http
    write_kafka . . . . . $enable_write_kafka
    write_log . . . . . . $enable_write_log
    write_mongodb . . . . $enable_write_mongodb
    write_redis . . . . . $enable_write_redis
    write_riemann . . . . $enable_write_riemann
    write_sensu . . . . . $enable_write_sensu
    write_tsdb  . . . . . $enable_write_tsdb
    xmms  . . . . . . . . $enable_xmms
    zfs_arc . . . . . . . $enable_zfs_arc
    zookeeper . . . . . . $enable_zookeeper

EOF

if test "x$dependency_error" = "xyes"; then
	AC_MSG_ERROR("Some plugins are missing dependencies - see the summary above for details")
fi

if test "x$dependency_warning" = "xyes"; then
	AC_MSG_WARN("Some plugins seem to have missing dependencies but have been enabled forcibly - see the summary above for details")
fi

# vim: set fdm=marker :
