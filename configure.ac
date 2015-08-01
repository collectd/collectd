dnl Process this file with autoconf to produce a configure script.
AC_INIT([collectd],[m4_esyscmd(./version-gen.sh)])
AC_CONFIG_SRCDIR(src/)
AC_CONFIG_HEADERS(src/config.h)
AC_CONFIG_AUX_DIR([libltdl/config])
AC_CONFIG_MACRO_DIR([m4])

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

	AC_MSG_CHECKING([whether compiler builds 64bit binaries])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([
			   #ifndef _LP64
			   # error "Compiler not in 64bit mode."
			   #endif
			   ])],
			   [AC_MSG_RESULT([yes])],
			   [
			    AC_MSG_RESULT([no])
			    AC_MSG_NOTICE([Solaris detected. Please consider building a 64-bit binary.])
			   ])
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

have_ip6_ext="no"
AC_CHECK_TYPES([struct ip6_ext], [have_ip6_ext="yes"], [have_ip6_ext="no"],
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

if test "x$have_ip6_ext" = "xno"; then
	SAVE_CFLAGS="$CFLAGS"
	CFLAGS="$CFLAGS -DSOLARIS2=8"

	AC_CHECK_TYPES([struct ip6_ext],
		       [have_ip6_ext="yes, with -DSOLARIS2=8"],
		       [have_ip6_ext="no"],
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

	if test "x$have_ip6_ext" = "xno"; then
		CFLAGS="$SAVE_CFLAGS"
	fi
fi

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
  AC_CACHE_CHECK([whether asm/msr-index.h has MSR_PKG_C10_RESIDENCY],
                 [c_cv_have_usable_asm_msrindex_h],
                 AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[[
#include<asm/msr-index.h>
]]],
[[[
int y = MSR_PKG_C10_RESIDENCY;
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

COLLECTD_WITH_LIBKVM
COLLECTD_WITH_LIBAQUAERO5
COLLECTD_WITH_LIBREDIS
COLLECTD_WITH_LIBCURL
COLLECTD_WITH_LIBDBI
COLLECTD_WITH_LIBESMTP
COLLECTD_WITH_LIBGANGLIA
COLLECTD_WITH_LIBGCRYPT
COLLECTD_WITH_LIBIPTC
COLLECTD_WITH_JAVA
COLLECTD_WITH_LIBLDAP
COLLECTD_WITH_LIBLVM2APP
COLLECTD_WITH_LIBMEMCACHED
COLLECTD_WITH_LIBMODBUS
COLLECTD_WITH_LIBMONGOC
COLLECTD_WITH_LIBMOSQUITTO
COLLECTD_WITH_LIBMYSQL
COLLECTD_WITH_LIBMNL
COLLECTD_WITH_LIBNETAPP
COLLECTD_WITH_LIBNETSNMP
COLLECTD_WITH_LIBOCONFIG
COLLECTD_WITH_LIBOPING
COLLECTD_WITH_ORACLE
COLLECTD_WITH_LIBOWCAPI
COLLECTD_WITH_LIBPCAP
COLLECTD_WITH_LIBPERL
COLLECTD_WITH_LIBPQ
COLLECTD_WITH_LIBPTHREAD
COLLECTD_WITH_PYTHON
COLLECTD_WITH_LIBRABBITMQ
COLLECTD_WITH_LIBRDKAFKA
COLLECTD_WITH_LIBROUTEROS
COLLECTD_WITH_LIBRRD
COLLECTD_WITH_LIBSENSORS
COLLECTD_WITH_LIBSIGROK
COLLECTD_WITH_LIBSTATGRAB
COLLECTD_WITH_LIBTOKYOTYRANT
COLLECTD_WITH_LIBUDEV
COLLECTD_WITH_LIBUPSCLIENT
COLLECTD_WITH_LIBXMMS
COLLECTD_WITH_LIBYAJL
COLLECTD_WITH_MIC
COLLECTD_WITH_LIBVARNISH
COLLECTD_WITH_LIBXML2
COLLECTD_WITH_LIBVIRT
COLLECTD_WITH_LIBOPENIPMI
COLLECTD_WITH_LIBATASMART
COLLECTD_WITH_LIBNOTIFY

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
plugin_zone="no"
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
	plugin_zone="yes"
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
AC_PLUGIN([mqtt],        [$with_libmosquitto], [MQTT output plugin])
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
AC_PLUGIN([zone],        [$plugin_zone],       [Solaris container statistics])
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
    libmosquitto  . . . . $with_libmosquitto
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
    mqtt  . . . . . . . . $enable_mqtt
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
    zone  . . . . . . . . $enable_zone
    zookeeper . . . . . . $enable_zookeeper

EOF

if test "x$dependency_error" = "xyes"; then
	AC_MSG_ERROR("Some plugins are missing dependencies - see the summary above for details")
fi

if test "x$dependency_warning" = "xyes"; then
	AC_MSG_WARN("Some plugins seem to have missing dependencies but have been enabled forcibly - see the summary above for details")
fi

# vim: set fdm=marker :
