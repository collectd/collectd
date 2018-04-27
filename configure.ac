dnl Process this file with autoconf to produce a configure script.
AC_PREREQ([2.60])
AC_INIT([collectd],[m4_esyscmd(./version-gen.sh)])
AC_CONFIG_SRCDIR(src/target_set.c)
AC_CONFIG_HEADERS(src/config.h)
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])

dnl older automake's default of ARFLAGS=cru is noisy on newer binutils;
dnl we don't really need the 'u' even in older toolchains.  Then there is
dnl older libtool, which spelled it AR_FLAGS
m4_divert_text([DEFAULTS], [: "${ARFLAGS=cr} ${AR_FLAGS=cr}"])

LT_INIT([dlopen disable-static])

AM_INIT_AUTOMAKE([subdir-objects tar-pax dist-bzip2 no-dist-gzip foreign])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AC_LANG(C)

AC_PREFIX_DEFAULT("/opt/collectd")

AC_SYS_LARGEFILE

#
# Checks for programs.
#
AC_PROG_CC_C99([],
  [AC_MSG_ERROR([No compiler found that supports C99])]
)

AX_COMPILER_VENDOR

AC_PROG_CXX
AC_PROG_CPP
AC_PROG_EGREP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AM_PROG_CC_C_O

AC_PROG_LEX
AC_PROG_YACC

AC_PATH_PROG([VALGRIND], [valgrind])

# Warn when pkg.m4 is missing
m4_pattern_forbid([^_?PKG_[A-Z_]+$],[*** pkg.m4 missing, please install pkg-config])

PKG_PROG_PKG_CONFIG

AC_CACHE_CHECK([if Bison is the parser generator],
  [collectd_cv_prog_bison],
  [
    AS_IF([$YACC --version 2>/dev/null | $EGREP -q '^bison '],
      [collectd_cv_prog_bison=yes],
      [collectd_cv_prog_bison=no]
    )
  ]
)

if test "x$collectd_cv_prog_bison" = "xno" && test ! -f "${srcdir}/src/liboconfig/parser.c"; then
  AC_MSG_ERROR([bison is missing and you do not have ${srcdir}/src/liboconfig/parser.c. Please install bison])
fi

if test "x$lt_cv_dlopen" = "xno"; then
  AC_MSG_ERROR([Your system does not support dlopen])
fi

AC_SUBST([DLOPEN_LIBS], [$lt_cv_dlopen_libs])


AC_MSG_CHECKING([for kernel type ($host_os)])
case $host_os in
  *aix*)
    AC_DEFINE([KERNEL_AIX], [1], [True if program is to be compiled for a AIX kernel])
    ac_system="AIX"
    ;;
  *darwin*)
    AC_DEFINE([KERNEL_DARWIN], [1], [True if program is to be compiled for a Darwin kernel])
    ac_system="Darwin"
    ;;
  *freebsd*)
    AC_DEFINE([KERNEL_FREEBSD], [1], [True if program is to be compiled for a FreeBSD kernel])
    ac_system="FreeBSD"
    ;;
  *linux*)
    AC_DEFINE([KERNEL_LINUX], [1], [True if program is to be compiled for a Linux kernel])
    ac_system="Linux"
    ;;
  *netbsd*)
    AC_DEFINE([KERNEL_NETBSD], [1], [True if program is to be compiled for a NetBSD kernel])
    ac_system="NetBSD"
    ;;
  *openbsd*)
    AC_DEFINE([KERNEL_OPENBSD], [1], [True if program is to be compiled for an OpenBSD kernel])
    ac_system="OpenBSD"
    ;;
  *solaris*)
    AC_DEFINE([KERNEL_SOLARIS], [1], [True if program is to be compiled for a Solaris kernel])
    ac_system="Solaris"
    ;;
  *)
    ac_system="unknown"
    ;;
esac
AC_MSG_RESULT([$ac_system])

AM_CONDITIONAL([BUILD_AIX], [test "x$ac_system" = "xAIX"])
AM_CONDITIONAL([BUILD_DARWIN], [test "x$ac_system" = "xDarwin"])
AM_CONDITIONAL([BUILD_FREEBSD], [test "x$ac_system" = "xFreeBSD"])
AM_CONDITIONAL([BUILD_LINUX], [test "x$ac_system" = "xLinux"])
AM_CONDITIONAL([BUILD_OPENBSD], [test "x$ac_system" = "xOpenBSD"])
AM_CONDITIONAL([BUILD_SOLARIS], [test "x$ac_system" = "xSolaris"])

if test "x$ac_system" = "xSolaris"; then
  AC_DEFINE([_POSIX_PTHREAD_SEMANTICS], [1], [Define to enforce POSIX thread semantics under Solaris.])
  AC_DEFINE([_REENTRANT], [1], [Define to enable reentrancy interfaces.])

  AC_MSG_CHECKING([whether compiler builds 64bit binaries])
  AC_COMPILE_IFELSE(
    [
      AC_LANG_PROGRAM(
        [
          #ifndef _LP64
          # error "Compiler not in 64bit mode."
          #endif
        ]
      )
    ],
    [AC_MSG_RESULT([yes])],
    [
      AC_MSG_RESULT([no])
      AC_MSG_NOTICE([Solaris detected. Please consider building a 64-bit binary.])
    ]
  )
fi

if test "x$ac_system" = "xAIX"; then
  AC_DEFINE([_THREAD_SAFE_ERRNO], [1], [Define to use the thread-safe version of errno under AIX.])
fi

# Where to install .pc files.
pkgconfigdir="${libdir}/pkgconfig"
AC_SUBST([pkgconfigdir])

#
# Checks for header files.
#
AC_HEADER_SYS_WAIT
AC_HEADER_DIRENT

AC_CHECK_HEADERS_ONCE([ \
  arpa/inet.h \
  endian.h \
  fcntl.h \
  fnmatch.h \
  fs_info.h \
  fshelp.h \
  grp.h \
  kstat.h \
  kvm.h \
  libgen.h \
  mntent.h \
  mnttab.h \
  netdb.h \
  paths.h \
  poll.h \
  pthread_np.h \
  pwd.h \
  regex.h \
  sys/endian.h \
  sys/fs_types.h \
  sys/fstyp.h \
  sys/ioctl.h \
  sys/isa_defs.h \
  sys/mntent.h \
  sys/mnttab.h \
  sys/param.h \
  sys/resource.h \
  sys/select.h \
  sys/socket.h \
  sys/statfs.h \
  sys/statvfs.h \
  sys/types.h \
  sys/un.h \
  sys/vfs.h \
  sys/vfstab.h \
  sys/vmmeter.h \
  syslog.h \
  wordexp.h
])

if test "x$ac_system" = "xNetBSD"; then
  # For entropy plugin on newer NetBSD
  AC_CHECK_HEADERS([sys/rndio.h], [], [],
    [[
      #if HAVE_SYS_TYPES_H
      # include <sys/types.h>
      #endif
      #if HAVE_SYS_IOCTL_H
      # include <sys/ioctl.h>
      #endif
      #if HAVE_SYS_PARAM_H
      # include <sys/param.h>
      #endif
    ]]
  )
fi

# For ping library
AC_CHECK_HEADERS([netinet/in_systm.h], [], [],
  [[
    #include <stdint.h>
    #if HAVE_SYS_TYPES_H
    # include <sys/types.h>
    #endif
  ]]
)

AC_CHECK_HEADERS([netinet/in.h], [], [],
  [[
    #include <stdint.h>
    #if HAVE_SYS_TYPES_H
    # include <sys/types.h>
    #endif
    #if HAVE_NETINET_IN_SYSTM_H
    # include <netinet/in_systm.h>
    #endif
  ]]
)

AC_CHECK_HEADERS([netinet/ip.h], [], [],
  [[
    #include <stdint.h>
    #if HAVE_SYS_TYPES_H
    # include <sys/types.h>
    #endif
    #if HAVE_NETINET_IN_SYSTM_H
    # include <netinet/in_systm.h>
    #endif
    #if HAVE_NETINET_IN_H
    # include <netinet/in.h>
    #endif
  ]]
)

AC_CHECK_HEADERS([netinet/ip_icmp.h], [], [],
  [[
    #include <stdint.h>
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
  ]]
)

AC_CHECK_HEADERS([netinet/ip_var.h], [], [],
  [[
    #include <stdint.h>
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
  ]]
)

AC_CHECK_HEADERS([netinet/ip6.h], [], [],
  [[
    #include <stdint.h>
    #if HAVE_SYS_TYPES_H
    # include <sys/types.h>
    #endif
    #if HAVE_NETINET_IN_SYSTM_H
    # include <netinet/in_systm.h>
    #endif
    #if HAVE_NETINET_IN_H
    # include <netinet/in.h>
    #endif
  ]]
)

AC_CHECK_HEADERS([netinet/icmp6.h], [], [],
  [[
    #include <stdint.h>
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
  ]]
)

AC_CHECK_HEADERS([netinet/tcp.h], [], [],
  [[
    #include <stdint.h>
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
  ]]
)

AC_CHECK_HEADERS([netinet/udp.h], [], [],
  [[
    #include <stdint.h>
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
  ]]
)

# For cpu modules
AC_CHECK_HEADERS([sys/dkstat.h])
if test "x$ac_system" = "xDarwin"; then
  AC_CHECK_HEADERS(
    [ \
      mach/mach_init.h \
      mach/host_priv.h \
      mach/mach_error.h \
      mach/mach_host.h \
      mach/mach_port.h \
      mach/mach_types.h \
      mach/message.h \
      mach/processor_set.h \
      mach/processor.h \
      mach/processor_info.h \
      mach/task.h \
      mach/thread_act.h \
      mach/vm_region.h \
      mach/vm_map.h \
      mach/vm_prot.h \
      mach/vm_statistics.h \
      mach/kern_return.h \
      CoreFoundation/CoreFoundation.h \
      IOKit/IOKitLib.h \
      IOKit/IOTypes.h \
      IOKit/ps/IOPSKeys.h \
      IOKit/IOBSD.h \
      IOKit/storage/IOBlockStorageDriver.h
    ]
  )

  # For the battery plugin
  AC_CHECK_HEADERS([IOKit/ps/IOPowerSources.h], [], [],
    [[
      #if HAVE_IOKIT_IOKITLIB_H
      #  include <IOKit/IOKitLib.h>
      #endif
      #if HAVE_IOKIT_IOTYPES_H
      #  include <IOKit/IOTypes.h>
      #endif
    ]]
  )
fi

AC_CHECK_HEADERS([sys/sysctl.h], [], [],
  [[
    #if HAVE_SYS_TYPES_H
    #  include <sys/types.h>
    #endif
    #if HAVE_SYS_PARAM_H
    # include <sys/param.h>
    #endif
  ]]
)

# For interface plugin
AC_CHECK_HEADERS([ifaddrs.h])
AC_CHECK_HEADERS([net/if.h], [], [],
  [[
    #if HAVE_SYS_TYPES_H
    #  include <sys/types.h>
    #endif
    #if HAVE_SYS_SOCKET_H
    #  include <sys/socket.h>
    #endif
  ]]
)

if test "x$ac_system" = "xLinux"; then
  # For hddtemp module
  AC_CHECK_HEADERS([linux/major.h])

  # For md module (Linux only)
  AC_CHECK_HEADERS([linux/raid/md_u.h],
    [have_linux_raid_md_u_h="yes"],
    [have_linux_raid_md_u_h="no"],
    [[
      #include <sys/ioctl.h>
      #include <linux/major.h>
      #include <linux/types.h>
    ]]
  )
  AC_CHECK_HEADERS([sys/sysmacros.h])

  AC_CHECK_HEADERS([linux/wireless.h],
    [have_linux_wireless_h="yes"],
    [have_linux_wireless_h="no"],
    [[
      #include <dirent.h>
      #include <sys/ioctl.h>
      #include <sys/socket.h>
    ]]
  )

  AC_CHECK_HEADERS([linux/if.h], [], [],
    [[
      #if HAVE_SYS_TYPES_H
      #  include <sys/types.h>
      #endif
      #if HAVE_SYS_SOCKET_H
      #  include <sys/socket.h>
      #endif
    ]]
  )

  AC_CHECK_HEADERS([linux/inet_diag.h], [], [],
    [[
      #if HAVE_SYS_TYPES_H
      #  include <sys/types.h>
      #endif
      #if HAVE_SYS_SOCKET_H
      #  include <sys/socket.h>
      #endif
    ]]
  )

  AC_CHECK_HEADERS([linux/netdevice.h], [], [],
    [[
      #if HAVE_SYS_TYPES_H
      #  include <sys/types.h>
      #endif
      #if HAVE_SYS_SOCKET_H
      #  include <sys/socket.h>
      #endif
      #if HAVE_LINUX_IF_H
      # include <linux/if.h>
      #endif
    ]]
  )

  # For ethstat module
  AC_CHECK_HEADERS([linux/sockios.h],
    [have_linux_sockios_h="yes"],
    [have_linux_sockios_h="no"],
    [[
      #if HAVE_SYS_IOCTL_H
      # include <sys/ioctl.h>
      #endif
      #if HAVE_NET_IF_H
      # include <net/if.h>
      #endif
    ]]
  )

  AC_CHECK_HEADERS([linux/ethtool.h],
    [have_linux_ethtool_h="yes"],
    [have_linux_ethtool_h="no"],
    [[
      #if HAVE_SYS_IOCTL_H
      # include <sys/ioctl.h>
      #endif
      #if HAVE_NET_IF_H
      # include <net/if.h>
      #endif
      #if HAVE_LINUX_SOCKIOS_H
      # include <linux/sockios.h>
      #endif
    ]]
  )

  # For ipvs module
  AC_CHECK_HEADERS_ONCE([linux/ip_vs.h])

  # For the email plugin
  AC_CHECK_HEADERS([linux/un.h], [], [],
    [[
      #if HAVE_SYS_SOCKET_H
      #  include <sys/socket.h>
      #endif
    ]]
  )
  # For the turbostat plugin
  AC_CHECK_HEADERS([cpuid.h],
    [have_cpuid_h="yes"],
    [have_cpuid_h="no (cpuid.h not found)"]
  )

  AC_CHECK_HEADERS([sys/capability.h],
    [have_capability="yes"],
    [have_capability="no (<sys/capability.h> not found)"]
  )

  if test "x$have_capability" = "xyes"; then
    AC_CHECK_LIB([cap], [cap_get_proc],
      [have_capability="yes"],
      [have_capability="no (cap_get_proc() not found)"]
    )
  fi

  if test "x$have_capability" = "xyes"; then
    AC_CHECK_DECL([CAP_IS_SUPPORTED],
      [have_capability="yes"],
      [have_capability="no (CAP_IS_SUPPORTED not found)"],
      [[#include <sys/capability.h>]]
    )
  fi

  if test "x$have_capability" = "xyes"; then
    AC_DEFINE([HAVE_CAPABILITY], [1], [Define to 1 if you have cap_get_proc() (-lcap).])
  fi

else
  have_linux_raid_md_u_h="no"
  have_linux_wireless_h="no"
fi

AM_CONDITIONAL([BUILD_WITH_CAPABILITY], [test "x$have_capability" = "xyes"])

# For the swap module
have_sys_swap_h="yes"
AC_CHECK_HEADERS([sys/swap.h vm/anon.h],
  [],
  [have_sys_swap_h="no"],
  [[
    #undef _FILE_OFFSET_BITS
    #undef _LARGEFILE64_SOURCE
    #if HAVE_SYS_TYPES_H
    #  include <sys/types.h>
    #endif
    #if HAVE_SYS_PARAM_H
    # include <sys/param.h>
    #endif
  ]]
)

# For load module
# For the processes plugin
# For users module
AC_CHECK_HEADERS([sys/loadavg.h linux/config.h utmp.h utmpx.h])

# For quota module
AC_CHECK_HEADERS([sys/ucred.h], [], [],
  [[
    #if HAVE_SYS_TYPES_H
    #  include <sys/types.h>
    #endif
    #if HAVE_SYS_PARAM_H
    # include <sys/param.h>
    #endif
  ]]
)

# For mount interface
AC_CHECK_HEADERS([sys/mount.h], [], [],
  [[
    #if HAVE_SYS_TYPES_H
    #  include <sys/types.h>
    #endif
    #if HAVE_SYS_PARAM_H
    # include <sys/param.h>
    #endif
  ]]
)

# --enable-xfs {{{
AC_ARG_ENABLE([xfs],
  [AS_HELP_STRING([--enable-xfs], [xfs support in df plugin @<:@default=yes@:>@])],
  [],
  [enable_xfs="auto"]
)

if test "x$enable_xfs" != "xno"; then
  AC_CHECK_HEADERS([xfs/xqm.h],
    [],
    [
      if test "x$enable_xfs" = "xyes"; then
        AC_MSG_ERROR([xfs/xqm.h not found])
      fi
    ],
    [[#define _GNU_SOURCE]]
  )
fi

# }}}

# For the dns plugin
AC_CHECK_HEADERS([arpa/nameser.h])
AC_CHECK_HEADERS([arpa/nameser_compat.h], [], [],
  [[
    #if HAVE_ARPA_NAMESER_H
    # include <arpa/nameser.h>
    #endif
  ]]
)

AC_CHECK_HEADERS([net/if_arp.h], [], [],
  [[
    #if HAVE_SYS_SOCKET_H
    # include <sys/socket.h>
    #endif
  ]]
)

AC_CHECK_HEADERS([net/ppp_defs.h])
AC_CHECK_HEADERS([net/if_ppp.h], [], [],
  [[
    #if HAVE_NET_PPP_DEFS_H
    # include <net/ppp_defs.h>
    #endif
  ]]
)

AC_CHECK_HEADERS([netinet/if_ether.h], [], [],
  [[
    #include <stdint.h>
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
  ]]
)

AC_CHECK_HEADERS([net/pfvar.h],
  [have_net_pfvar_h="yes"],
  [have_net_pfvar_h="no"],
  [[
    #if HAVE_SYS_IOCTL_H
    # include <sys/ioctl.h>
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
  ]]
)

# For the multimeter plugin
AC_CHECK_HEADERS([termios.h],
  [have_termios_h="yes"],
  [have_termios_h="no"]
)

# For cpusleep plugin
AC_CACHE_CHECK([whether clock_boottime and clock_monotonic are supported],
  [c_cv_have_clock_boottime_monotonic],
  [
    AC_COMPILE_IFELSE(
      [AC_LANG_PROGRAM(
        [[#include <time.h>]],
        [[
          struct timespec b, m;
          clock_gettime(CLOCK_BOOTTIME, &b );
          clock_gettime(CLOCK_MONOTONIC, &m );
        ]]
      )
      ],
      [c_cv_have_clock_boottime_monotonic="yes"],
      [c_cv_have_clock_boottime_monotonic="no"]
    )
  ]
)


#
# Checks for typedefs, structures, and compiler characteristics.
#
AC_C_CONST
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_UID_T
AC_HEADER_TIME

test_cxx_flags() {
  AC_LANG_PUSH([C++])
  AC_LANG_CONFTEST(
    [AC_LANG_SOURCE([[int main(void){}]]) ]
  )
  $CXX -c conftest.cpp $CXXFLAGS $@ > /dev/null 2> /dev/null
  ret=$?
  rm -f conftest.o
  AC_LANG_POP([C++])
  return $ret
}

#
# Checks for library functions.
#
AC_CHECK_FUNCS_ONCE([ \
    asprintf \
    closelog \
    getaddrinfo \
    getgrnam_r \
    getnameinfo \
    getpwnam_r \
    gettimeofday \
    if_indextoname \
    openlog \
    regcomp \
    regerror \
    regexec \
    regfree \
    select \
    setenv \
    setgroups \
    strcasecmp \
    strdup \
    strncasecmp \
    sysconf
  ]
)

AC_FUNC_STRERROR_R

SAVE_CFLAGS="$CFLAGS"
# Emulate behavior of src/Makefile.am
if test "x$GCC" = "xyes"; then
  CFLAGS="$CFLAGS -Wall -Werror"
fi

AC_CACHE_CHECK([for strtok_r],
  [c_cv_have_strtok_r_default],
  [
    AC_LINK_IFELSE(
      [
        AC_LANG_PROGRAM(
          [[
            #include <stdlib.h>
            #include <stdio.h>
            #include <string.h>
          ]],
          [[
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
          ]]
        )
      ],
      [c_cv_have_strtok_r_default="yes"],
      [c_cv_have_strtok_r_default="no"]
    )
  ]
)

if test "x$c_cv_have_strtok_r_default" = "xno"; then
  CFLAGS="$CFLAGS -D_REENTRANT=1"

  AC_CACHE_CHECK([if strtok_r needs _REENTRANT],
    [c_cv_have_strtok_r_reentrant],
    [
      AC_LINK_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
              #include <stdlib.h>
              #include <stdio.h>
              #include <string.h>
            ]],
            [[
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
            ]]
          )
        ],
        [c_cv_have_strtok_r_reentrant="yes"],
        [AC_MSG_FAILURE([strtok_r is not available. Please file a bugreport!])]
      )
    ]
  )
fi

CFLAGS="$SAVE_CFLAGS"
if test "x$c_cv_have_strtok_r_reentrant" = "xyes"; then
  CFLAGS="$CFLAGS -D_REENTRANT=1"
fi

AC_CHECK_FUNCS([socket],
  [],
  [
    AC_CHECK_LIB([socket], [socket],
      [socket_needs_socket="yes"],
      [AC_MSG_ERROR([cannot find socket() in libsocket])]
    )
  ]
)
AM_CONDITIONAL([BUILD_WITH_LIBSOCKET], [test "x$socket_needs_socket" = "xyes"])

clock_gettime_needs_posix4="no"
AC_CHECK_FUNCS([clock_gettime],
  [have_clock_gettime="yes"],
  [have_clock_gettime="no"]
)

if test "x$have_clock_gettime" = "xno"; then
  AC_CHECK_LIB([rt], [clock_gettime],
    [
      clock_gettime_needs_rt="yes"
      have_clock_gettime="yes"
    ]
  )
fi

if test "x$have_clock_gettime" = "xno"; then
  AC_CHECK_LIB([posix4], [clock_gettime],
    [
      clock_gettime_needs_posix4="yes"
      have_clock_gettime="yes"
    ]
  )
fi

if test "x$have_clock_gettime" = "xyes"; then
  AC_DEFINE([HAVE_CLOCK_GETTIME], [1], [Define if the clock_gettime(2) function is available.])
fi

AC_CHECK_FUNCS([nanosleep], [],
  AC_CHECK_LIB([rt], [nanosleep],
    [nanosleep_needs_rt="yes"],
    [
      AC_CHECK_LIB([posix4], [nanosleep],
        [nanosleep_needs_posix4="yes"],
        [AC_MSG_ERROR([cannot find nanosleep])]
      )
    ]
  )
)

AM_CONDITIONAL([BUILD_WITH_LIBRT], [test "x$clock_gettime_needs_rt" = "xyes" || test "x$nanosleep_needs_rt" = "xyes"])
AM_CONDITIONAL([BUILD_WITH_LIBPOSIX4], [test "x$clock_gettime_needs_posix4" = "xyes" || test "x$nanosleep_needs_posix4" = "xyes"])

AC_CHECK_FUNCS([getifaddrs], [have_getifaddrs="yes"], [have_getifaddrs="no"])
AC_CHECK_FUNCS([getloadavg], [have_getloadavg="yes"], [have_getloadavg="no"])
AC_CHECK_FUNCS([getutent], [have_getutent="yes"], [have_getutent="no"])
AC_CHECK_FUNCS([getutxent], [have_getutxent="yes"], [have_getutxent="no"])
AC_CHECK_FUNCS([host_statistics], [have_host_statistics="yes"], [have_host_statistics="no"])
AC_CHECK_FUNCS([processor_info], [have_processor_info="yes"], [have_processor_info="no"])
AC_CHECK_FUNCS([statfs], [have_statfs="yes"], [have_statfs="no"])
AC_CHECK_FUNCS([statvfs], [have_statvfs="yes"], [have_statvfs="no"])
AC_CHECK_FUNCS([sysctl], [have_sysctl="yes"], [have_sysctl="no"])
AC_CHECK_FUNCS([sysctlbyname], [have_sysctlbyname="yes"], [have_sysctlbyname="no"])
AC_CHECK_FUNCS([syslog], [have_syslog="yes"], [have_syslog="no"])
AC_CHECK_FUNCS([thread_info], [have_thread_info="yes"], [have_thread_info="no"])

# Check for strptime {{{
if test "x$GCC" = "xyes"; then
  SAVE_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS -Wall -Wextra -Werror"
fi

AC_CHECK_FUNCS([strptime], [have_strptime="yes"], [have_strptime="no"])
if test "x$have_strptime" = "xyes"; then
  AC_CACHE_CHECK([whether strptime is exported by default],
    [c_cv_have_strptime_default],
    [
      AC_COMPILE_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[#include <time.h>]],
            [[
              struct tm stm;
              (void)strptime ("2010-12-30%13:42:42", "%Y-%m-%dT%T", &stm);
            ]]
          )
        ],
        [c_cv_have_strptime_default="yes"],
        [c_cv_have_strptime_default="no"])
    ]
  )
fi

if test "x$have_strptime" = "xyes" && test "x$c_cv_have_strptime_default" = "xno"; then
  AC_CACHE_CHECK([whether strptime needs standards mode],
    [c_cv_have_strptime_standards],
    [
      AC_COMPILE_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
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
            ]],
            [[
              struct tm stm;
              (void)strptime ("2010-12-30%13:42:42", "%Y-%m-%dT%T", &stm);
            ]]
          )
        ],
        [c_cv_have_strptime_standards="yes"],
        [c_cv_have_strptime_standards="no"]
      )
    ]
  )

  if test "x$c_cv_have_strptime_standards" = "xyes"; then
    AC_DEFINE([STRPTIME_NEEDS_STANDARDS], [1],
      [Set to true if strptime is only exported in X/Open mode (GNU libc).]
    )
  else
    have_strptime="no"
  fi
fi

if test "x$GCC" = "xyes"; then
  CFLAGS="$SAVE_CFLAGS"
fi
# }}} Check for strptime

# Check for timegm {{{

# These checks need -Werror because implicit function declarations are only a
# warning ...
SAVE_CFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -Werror"

AC_CACHE_CHECK([for timegm],
  [c_cv_have_timegm],
  AC_LINK_IFELSE(
    [AC_LANG_PROGRAM(
[[[
#if STRPTIME_NEEDS_STANDARDS
# ifndef _ISOC99_SOURCE
#  define _ISOC99_SOURCE 1
# endif
# ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200112L
# endif
# ifndef _XOPEN_SOURCE
#  define _XOPEN_SOURCE 500
# endif
#endif
#include <time.h>
]]],
[[[
 time_t t = timegm(&(struct tm){0});
 if (t == ((time_t) -1)) {
   return 1;
 }
]]]
    )],
    [c_cv_have_timegm="yes"],
    [c_cv_have_timegm="no"]
  )
)

if test "x$c_cv_have_timegm" != "xyes"
then
  AC_CACHE_CHECK([for timegm with _BSD_SOURCE],
    [c_cv_have_timegm_bsd],
    AC_LINK_IFELSE(
      [AC_LANG_PROGRAM(
[[[
#if STRPTIME_NEEDS_STANDARDS
# ifndef _ISOC99_SOURCE
#  define _ISOC99_SOURCE 1
# endif
# ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200112L
# endif
# ifndef _XOPEN_SOURCE
#  define _XOPEN_SOURCE 500
# endif
#endif
#ifndef _BSD_SOURCE
# define _BSD_SOURCE 1
#endif
#include <time.h>
]]],
[[[
 time_t t = timegm(&(struct tm){0});
 if (t == ((time_t) -1)) {
   return 1;
 }
]]]
      )],
      [c_cv_have_timegm_bsd="yes"
       c_cv_have_timegm="yes"],
      [c_cv_have_timegm_bsd="no"]
    )
  )
fi

if test "x$c_cv_have_timegm" = "xyes"
then
  AC_DEFINE(HAVE_TIMEGM, 1, [Define if the timegm(3) function is available.])
  if test "x$c_cv_have_timegm_bsd" = "xyes"
  then
    AC_DEFINE(TIMEGM_NEEDS_BSD, 1, [Set to true if timegm is only exported in BSD mode.])
  fi
fi

CFLAGS="$SAVE_CFLAGS"
# }}} Check for timegm

AC_MSG_CHECKING([for sysctl kern.cp_times])
if test -x /sbin/sysctl; then
  /sbin/sysctl kern.cp_times >/dev/null 2>&1
  if test $? -eq 0; then
    AC_MSG_RESULT([yes])
    AC_DEFINE([HAVE_SYSCTL_KERN_CP_TIMES], [1], [Define if sysctl supports kern.cp_times])
  else
    AC_MSG_RESULT([no])
  fi
else
  AC_MSG_RESULT([no])
fi

AC_MSG_CHECKING([for sysctl kern.cp_time])
if test -x /sbin/sysctl; then
  /sbin/sysctl kern.cp_time >/dev/null 2>&1
  if test $? -eq 0
  then
    AC_MSG_RESULT([yes])
    AC_DEFINE([HAVE_SYSCTL_KERN_CP_TIME], [1], [Define if sysctl supports kern.cp_time])
  else
    AC_MSG_RESULT([no])
  fi
else
  AC_MSG_RESULT([no])
fi

AC_CHECK_FUNCS([swapctl], [have_swapctl="yes"], [have_swapctl="no"])
if test "x$have_swapctl" = "xyes"; then
  AC_CACHE_CHECK([whether swapctl takes two arguments],
    [c_cv_have_swapctl_two_args],
    [
      AC_COMPILE_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
              #if HAVE_SYS_SWAP_H && !defined(_LP64) && _FILE_OFFSET_BITS == 64
              #  undef _FILE_OFFSET_BITS
              #  undef _LARGEFILE64_SOURCE
              #endif
              #include <sys/stat.h>
              #include <sys/param.h>
              #include <sys/swap.h>
              #include <unistd.h>
            ]],
            [[int num = swapctl(0, NULL);]]
          )
        ],
        [c_cv_have_swapctl_two_args="yes"],
        [c_cv_have_swapctl_two_args="no"]
      )
    ]
  )

  AC_CACHE_CHECK([whether swapctl takes three arguments],
    [c_cv_have_swapctl_three_args],
    [
      AC_COMPILE_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
              #if HAVE_SYS_SWAP_H && !defined(_LP64) && _FILE_OFFSET_BITS == 64
              #  undef _FILE_OFFSET_BITS
              #  undef _LARGEFILE64_SOURCE
              #endif
              #include <sys/stat.h>
              #include <sys/param.h>
              #include <sys/swap.h>
              #include <unistd.h>
            ]],
            [[int num = swapctl(0, NULL, 0);]]
          )
        ],
        [c_cv_have_swapctl_three_args="yes"],
        [c_cv_have_swapctl_three_args="no"]
      )
    ]
  )
fi

# Check for different versions of `swapctl' here..
if test "x$have_swapctl" = "xyes"; then
  if test "x$c_cv_have_swapctl_two_args" = "xyes"; then
    AC_DEFINE([HAVE_SWAPCTL_TWO_ARGS], [1], [Define if the function swapctl exists and takes two arguments.])
  fi

  if test "x$c_cv_have_swapctl_three_args" = "xyes"; then
    AC_DEFINE([HAVE_SWAPCTL_THREE_ARGS], [1], [Define if the function swapctl exists and takes three arguments.])
  fi
fi

# Check for NAN
AC_ARG_WITH([nan-emulation],
  [AS_HELP_STRING([--with-nan-emulation], [use emulated NAN. For crosscompiling only.])],
  [
    if test "x$withval" = "xno"; then
      nan_type="none"
    else if test "x$withval" = "xyes"; then
      nan_type="zero"
    else
      nan_type="$withval"
    fi; fi
  ],
  [nan_type="none"]
)

if test "x$nan_type" = "xnone"; then
  AC_CACHE_CHECK([whether NAN is defined by default],
    [c_cv_have_nan_default],
    [
      AC_COMPILE_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
              #include <stdlib.h>
              #include <math.h>
              static double foo = NAN;
            ]],
            [[
              if (isnan (foo))
                return 0;
              return 1;
            ]]
          )
        ],
        [c_cv_have_nan_default="yes"],
        [c_cv_have_nan_default="no"]
      )
    ]
  )
fi

if test "x$c_cv_have_nan_default" = "xyes"; then
  nan_type="default"
fi

if test "x$nan_type" = "xnone"; then
  AC_CACHE_CHECK([whether NAN is defined by __USE_ISOC99],
    [c_cv_have_nan_isoc],
    [
      AC_COMPILE_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
              #include <stdlib.h>
              #define __USE_ISOC99 1
              #include <math.h>
              static double foo = NAN;
            ]],
            [[
              if (isnan (foo))
                return 0;
              return 1;
            ]]
          )
        ],
        [c_cv_have_nan_isoc="yes"],
        [c_cv_have_nan_isoc="no"]
      )
    ]
  )
fi

if test "x$c_cv_have_nan_isoc" = "xyes"; then
  nan_type="isoc99"
fi

if test "x$nan_type" = "xnone"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS -lm"
  AC_CACHE_CHECK([whether NAN can be defined by 0/0],
    [c_cv_have_nan_zero],
    [
      AC_RUN_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
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
            ]],
            [[
              if (isnan (foo))
                return 0;
              return 1;
            ]]
          )
        ],
        [c_cv_have_nan_zero="yes"],
        [c_cv_have_nan_zero="no"]
      )
    ]
  )
  LDFLAGS=$SAVE_LDFLAGS
fi

if test "x$c_cv_have_nan_zero" = "xyes"; then
  nan_type="zero"
fi

if test "x$nan_type" = "xdefault"; then
  AC_DEFINE([NAN_STATIC_DEFAULT], [1],
    [Define if NAN is defined by default and can initialize static variables.]
  )
else if test "x$nan_type" = "xisoc99"; then
  AC_DEFINE([NAN_STATIC_ISOC], [1],
    [Define if NAN is defined by __USE_ISOC99 and can initialize static variables.]
  )
else if test "x$nan_type" = "xzero"; then
  AC_DEFINE([NAN_ZERO_ZERO], [1],
    [Define if NAN can be defined as (0.0 / 0.0)]
  )
else
  AC_MSG_ERROR([Didn't find out how to statically initialize variables to NAN. Sorry.])
fi; fi; fi

AC_ARG_WITH([fp-layout],
  [
    AS_HELP_STRING([--with-fp-layout],
      [set the memory layout of doubles. For crosscompiling only.]
    )
  ],
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
  [fp_layout_type="unknown"]
)

if test "x$fp_layout_type" = "xunknown"; then
  AC_CACHE_CHECK([if doubles are stored in x86 representation],
    [c_cv_fp_layout_need_nothing],
    [
      AC_RUN_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
              #include <stdlib.h>
              #include <stdio.h>
              #include <string.h>
              #include <stdint.h>
              #include <inttypes.h>
              #include <stdbool.h>
            ]],
            [[
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
                return 0;
              return 1;
            ]]
          )
        ],
        [c_cv_fp_layout_need_nothing="yes"],
        [c_cv_fp_layout_need_nothing="no"]
      )
    ]
  )
fi

if test "x$c_cv_fp_layout_need_nothing" = "xyes"; then
  fp_layout_type="nothing"
fi

if test "x$fp_layout_type" = "xunknown"; then
  AC_CACHE_CHECK([if endianflip converts to x86 representation],
    [c_cv_fp_layout_need_endianflip],
    [
      AC_RUN_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
              #include <stdlib.h>
              #include <stdio.h>
              #include <string.h>
              #include <stdint.h>
              #include <inttypes.h>
              #include <stdbool.h>
              #define endianflip(A) ((((uint64_t)(A) & 0xff00000000000000LL) >> 56) | \
                                     (((uint64_t)(A) & 0x00ff000000000000LL) >> 40) | \
                                     (((uint64_t)(A) & 0x0000ff0000000000LL) >> 24) | \
                                     (((uint64_t)(A) & 0x000000ff00000000LL) >> 8)  | \
                                     (((uint64_t)(A) & 0x00000000ff000000LL) << 8)  | \
                                     (((uint64_t)(A) & 0x0000000000ff0000LL) << 24) | \
                                     (((uint64_t)(A) & 0x000000000000ff00LL) << 40) | \
                                     (((uint64_t)(A) & 0x00000000000000ffLL) << 56))
            ]],
            [[
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
                return 0;
              return 1;
            ]]
          )
        ],
        [c_cv_fp_layout_need_endianflip="yes"],
        [c_cv_fp_layout_need_endianflip="no"]
      ]
    )
  )
fi

if test "x$c_cv_fp_layout_need_endianflip" = "xyes"; then
  fp_layout_type="endianflip"
fi

if test "x$fp_layout_type" = "xunknown"; then
  AC_CACHE_CHECK([if intswap converts to x86 representation],
    [c_cv_fp_layout_need_intswap],
    [
      AC_RUN_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
              #include <stdlib.h>
              #include <stdio.h>
              #include <string.h>
              #include <stdint.h>
              #include <inttypes.h>
              #include <stdbool.h>
              #define intswap(A)    ((((uint64_t)(A) & 0xffffffff00000000LL) >> 32) | \
                                     (((uint64_t)(A) & 0x00000000ffffffffLL) << 32))
            ]],
            [[
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
                return 0;
              return 1;
            ]]
          )
        ],
        [c_cv_fp_layout_need_intswap="yes"],
        [c_cv_fp_layout_need_intswap="no"]
      )
    ]
  )
fi

if test "x$c_cv_fp_layout_need_intswap" = "xyes"; then
  fp_layout_type="intswap"
fi

if test "x$fp_layout_type" = "xnothing"; then
  AC_DEFINE([FP_LAYOUT_NEED_NOTHING], [1],
    [Define if doubles are stored in x86 representation.]
  )
else if test "x$fp_layout_type" = "xendianflip"; then
  AC_DEFINE([FP_LAYOUT_NEED_ENDIANFLIP], [1],
    [Define if endianflip is needed to convert to x86 representation.]
  )
else if test "x$fp_layout_type" = "xintswap"; then
  AC_DEFINE([FP_LAYOUT_NEED_INTSWAP], [1],
    [Define if intswap is needed to convert to x86 representation.]
  )
else
  AC_MSG_ERROR([Didn't find out how doubles are stored in memory. Sorry.])
fi; fi; fi

# For cpusleep plugin
AC_CACHE_CHECK([whether clock_boottime and clock_monotonic are supported],
  [c_cv_have_clock_boottime_monotonic],
  [
    AC_COMPILE_IFELSE(
      [AC_LANG_PROGRAM(
        [[#include <time.h>]],
        [[
          struct timespec b, m;
          clock_gettime(CLOCK_BOOTTIME, &b );
          clock_gettime(CLOCK_MONOTONIC, &m );
        ]]
      )
      ],
      [c_cv_have_clock_boottime_monotonic="yes"],
      [c_cv_have_clock_boottime_monotonic="no"]
    )
  ]
)

# --with-useragent {{{
AC_ARG_WITH([useragent],
  [AS_HELP_STRING([--with-useragent@<:@=AGENT@:>@], [User agent to use on http requests])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      AC_DEFINE_UNQUOTED([COLLECTD_USERAGENT], ["$withval"], [User agent for http requests])
    fi
  ]
)

# }}}

# --with-data-max-name-len {{{
AC_ARG_WITH([data-max-name-len],
  [AS_HELP_STRING([--with-data-max-name-len@<:@=VALUE@:>@], [Maximum length of data buffers])],
  [
    if test "x$withval" != "x" && test $withval -gt 0; then
      AC_DEFINE_UNQUOTED([DATA_MAX_NAME_LEN], [$withval], [Maximum length of data buffers])
    else
      AC_MSG_ERROR([DATA_MAX_NAME_LEN must be a positive integer -- $withval given])
    fi
  ],
  [AC_DEFINE([DATA_MAX_NAME_LEN], [128], [Maximum length of data buffers])]
)
# }}}

AC_CHECK_FUNCS([getfsstat], [have_getfsstat="yes"], [have_getfsstat="no"])
AC_CHECK_FUNCS(getvfsstat, [have_getvfsstat="yes"], [have_getvfsstat="no"])
AC_CHECK_FUNCS(listmntent, [have_listmntent="yes"], [have_listmntent="no"])
AC_CHECK_FUNCS(getmntent_r, [have_getmntent_r="yes"], [have_getmntent_r="no"])

AC_CHECK_FUNCS(getmntent, [have_getmntent="libc"], [have_getmntent="no"])
if test "x$have_getmntent" = "xno"; then
  AC_CHECK_LIB([sun], [getmntent],
    [have_getmntent="sun"],
    [have_gemntent="no"]
  )
fi

if test "x$have_getmntent" = "xno"; then
  AC_CHECK_LIB([seq], [getmntent],
    [have_getmntent="seq"],
    [have_getmntent="no"]
  )
fi

if test "x$have_getmntent" = "xno"; then
  AC_CHECK_LIB([gen], [getmntent],
    [have_getmntent="gen"],
    [have_getmntent="no"]
  )
fi

if test "x$have_getmntent" = "xlibc"; then
  AC_CACHE_CHECK([whether getmntent takes one argument],
    [c_cv_have_one_getmntent],
    [
      AC_COMPILE_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[#include "$srcdir/src/utils_mount.h"]],
            [[
              FILE *fh;
              struct mntent *me;
              fh = setmntent ("/etc/mtab", "r");
              me = getmntent (fh);
              return(me->mnt_passno);
            ]]
          )
        ],
        [c_cv_have_one_getmntent="yes"],
        [c_cv_have_one_getmntent="no"]
      )
    ]
  )

  AC_CACHE_CHECK([whether getmntent takes two arguments],
    [c_cv_have_two_getmntent],
    [
      AC_COMPILE_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[#include "$srcdir/src/utils_mount.h"]],
            [[
              FILE *fh;
              struct mnttab mt;
              int status;
              fh = fopen ("/etc/mnttab", "r");
              status = getmntent (fh, &mt);
              return(status);
            ]]
          )
        ],
        [c_cv_have_two_getmntent="yes"],
        [c_cv_have_two_getmntent="no"]
      )
    ]
  )
fi

# Check for different versions of `getmntent' here..

if test "x$have_getmntent" = "xlibc"; then
  if test "x$c_cv_have_one_getmntent" = "xyes"; then
    AC_DEFINE([HAVE_ONE_GETMNTENT], [1],
      [Define if the function getmntent exists and takes one argument.]
    )
  fi

  if test "x$c_cv_have_two_getmntent" = "xyes"; then
    AC_DEFINE([HAVE_TWO_GETMNTENT], [1],
      [Define if the function getmntent exists and takes two arguments.]
    )
  fi
fi

if test "x$have_getmntent" = "xsun"; then
  AC_DEFINE([HAVE_SUN_GETMNTENT], [1],
    [Define if the function getmntent exists. It is the version from libsun.]
  )
fi

if test "x$have_getmntent" = "xgen"; then
  AC_DEFINE([HAVE_GEN_GETMNTENT], [1],
    [Define if the function getmntent exists. It is the version from libgen.]
  )
fi

# Check for htonll
AC_CACHE_CHECK([whether htonll is defined],
  [c_cv_have_htonll],
  [
    AC_LINK_IFELSE(
      [
        AC_LANG_PROGRAM(
          [[
            #include <inttypes.h>
            #include <sys/types.h>
            #include <netinet/in.h>
          ]],
          [[return htonll(0);]]
        )
      ],
      [c_cv_have_htonll="yes"],
      [c_cv_have_htonll="no"]
    )
  ]
)

if test "x$c_cv_have_htonll" = "xyes"; then
  AC_DEFINE([HAVE_HTONLL], [1], [Define if the function htonll exists.])
fi

# Check for structures
AC_CHECK_MEMBERS([struct if_data.ifi_ibytes, struct if_data.ifi_opackets, struct if_data.ifi_ierrors],
  [AC_DEFINE([HAVE_STRUCT_IF_DATA], [1], [Define if struct if_data exists and is usable.])],
  [],
  [[
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <net/if.h>
  ]]
)

AC_CHECK_MEMBERS([struct net_device_stats.rx_bytes, struct net_device_stats.tx_packets, struct net_device_stats.rx_errors],
  [AC_DEFINE([HAVE_STRUCT_NET_DEVICE_STATS], [1], [Define if struct net_device_stats exists and is usable.])],
  [],
  [[
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <linux/if.h>
    #include <linux/netdevice.h>
  ]]
)

AC_CHECK_MEMBERS([struct inet_diag_req.id, struct inet_diag_req.idiag_states],
  [AC_DEFINE([HAVE_STRUCT_LINUX_INET_DIAG_REQ], [1], [Define if struct inet_diag_req exists and is usable.])],
  [],
  [[#include <linux/inet_diag.h>]]
)

AC_CHECK_MEMBERS([struct ip_mreqn.imr_ifindex], [],
  [],
  [[
    #include <netinet/in.h>
    #include <net/if.h>
  ]]
)

AC_CHECK_MEMBERS([struct kinfo_proc.ki_pid, struct kinfo_proc.ki_rssize, struct kinfo_proc.ki_rusage],
  [
    AC_DEFINE([HAVE_STRUCT_KINFO_PROC_FREEBSD], [1], [Define if struct kinfo_proc exists in the FreeBSD variant.])
    have_struct_kinfo_proc_freebsd="yes"
  ],
  [],
  [[
    #include <kvm.h>
    #include <sys/param.h>
    #include <sys/sysctl.h>
    #include <sys/user.h>
  ]]
)

AC_CHECK_MEMBERS([struct kinfo_proc.p_pid, struct kinfo_proc.p_vm_rssize],
  [
    AC_DEFINE([HAVE_STRUCT_KINFO_PROC_OPENBSD], [1], [Define if struct kinfo_proc exists in the OpenBSD variant.])
    have_struct_kinfo_proc_openbsd="yes"
  ],
  [],
  [[
    #include <sys/param.h>
    #include <sys/sysctl.h>
    #include <kvm.h>
  ]]
)

AC_CHECK_MEMBERS([struct kinfo_proc2.p_pid, struct kinfo_proc2.p_uru_maxrss],
  [
    AC_DEFINE([HAVE_STRUCT_KINFO_PROC2_NETBSD], [1], [Define if struct kinfo_proc2 exists in the NetBSD variant.])
    have_struct_kinfo_proc2_netbsd="yes"
  ],
  [],
  [[
    #include <sys/param.h>
    #include <sys/sysctl.h>
    #include <kvm.h>
  ]]
)

AC_CHECK_MEMBERS([struct udphdr.uh_dport, struct udphdr.uh_sport],
  [],
  [],
  [[
    #define _BSD_SOURCE
    #define _DEFAULT_SOURCE
    #include <stdint.h>
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
  ]]
)

AC_CHECK_MEMBERS([struct udphdr.dest, struct udphdr.source],
  [],
  [],
  [[
    #define _BSD_SOURCE
    #define _DEFAULT_SOURCE
    #include <stdint.h>
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
  ]]
)

AC_CHECK_MEMBERS([kstat_io_t.nwritten, kstat_io_t.writes, kstat_io_t.nwrites, kstat_io_t.wtime],
  [],
  [],
  [[# include <kstat.h>]]
)

# check for pthread_setname_np
SAVE_LDFLAGS="$LDFLAGS"
LDFLAGS="$LDFLAGS -lpthread"

AC_MSG_CHECKING([for pthread_setname_np])
have_pthread_setname_np="no"
AC_LINK_IFELSE(
  [
    AC_LANG_PROGRAM(
      [[
        #define _GNU_SOURCE
        #include <pthread.h>
      ]],
      [[pthread_setname_np((pthread_t) {0}, "conftest");]]
    )
  ],
  [
    have_pthread_setname_np="yes"
    AC_DEFINE(HAVE_PTHREAD_SETNAME_NP, 1, [pthread_setname_np() is available.])
  ]
)

AC_MSG_RESULT([$have_pthread_setname_np])

# check for pthread_set_name_np(3) (FreeBSD)
AC_MSG_CHECKING([for pthread_set_name_np])
have_pthread_set_name_np="no"
AC_LINK_IFELSE(
  [
    AC_LANG_PROGRAM(
      [[#include <pthread_np.h>]],
      [[pthread_set_name_np((pthread_t) {0}, "conftest");]]
    )
   ],
  [
    have_pthread_set_name_np="yes"
    AC_DEFINE(HAVE_PTHREAD_SET_NAME_NP, 1, [pthread_set_name_np() is available.])
  ]
)
AC_MSG_RESULT([$have_pthread_set_name_np])

LDFLAGS="$SAVE_LDFLAGS"

AC_CHECK_TYPES([struct ip6_ext],
  [have_ip6_ext="yes"],
  [have_ip6_ext="no"],
  [[
    #include <stdint.h>
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
  ]]
)

if test "x$have_ip6_ext" = "xno"; then
  SAVE_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS -DSOLARIS2=8"
  AC_CHECK_TYPES([struct ip6_ext],
    [have_ip6_ext="yes, with -DSOLARIS2=8"],
    [have_ip6_ext="no"],
    [[
      #include <stdint.h>
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
    ]]
  )
  if test "x$have_ip6_ext" = "xno"; then
    CFLAGS="$SAVE_CFLAGS"
  fi
fi

# libi2c-dev
if test "x$ac_system" = "xLinux"; then
  AC_CHECK_DECL([i2c_smbus_read_i2c_block_data],
    [with_libi2c="yes"],
    [with_libi2c="no (symbol i2c_smbus_read_i2c_block_data not found - have you installed libi2c-dev ?)"],
    [[
      #include <stdlib.h>
      #include <linux/i2c-dev.h>
    ]]
  )
else
  with_libi2c="no (Linux only)"
fi

#
# Checks for libraries begin here
#

# Check for libpthread
SAVE_LIBS="$LIBS"
AC_CHECK_LIB([pthread],
  [pthread_create],
  [],
  [AC_MSG_ERROR([Symbol 'pthread_create' not found in libpthread])],
  []
)
PTHREAD_LIBS="$LIBS"
LIBS="$SAVE_LIBS"
AC_SUBST([PTHREAD_LIBS])

AC_CHECK_HEADERS([pthread.h],
  [],
  [AC_MSG_ERROR([pthread.h not found])]
)

m4_divert_once([HELP_WITH], [
Collectd additional packages:])

if test "x$ac_system" = "xAIX"; then
  with_perfstat="yes"
  with_procinfo="yes"
else
  with_perfstat="no (AIX only)"
  with_procinfo="no (AIX only)"
fi

if test "x$with_perfstat" = "xyes"; then
  AC_CHECK_LIB([perfstat], [perfstat_reset],
    [with_perfstat="yes"],
    [with_perfstat="no (perfstat not found)"]
  )
fi

if test "x$with_perfstat" = "xyes"; then
  AC_DEFINE([HAVE_PERFSTAT], [1], [Define to 1 if you have the 'perfstat' library (-lperfstat)])
  # struct members pertaining to donation have been added to libperfstat somewhere between AIX5.3ML5 and AIX5.3ML9
  AC_CHECK_MEMBER([perfstat_partition_type_t.b.donate_enabled],
    [],
    [],
    [[#include <libperfstat.h]]
  )
  if test "x$av_cv_member_perfstat_partition_type_t_b_donate_enabled" = "xyes"; then
    AC_DEFINE([PERFSTAT_SUPPORTS_DONATION], [1], [Define to 1 if your version of the 'perfstat' library supports donation])
  fi
fi
AM_CONDITIONAL([BUILD_WITH_PERFSTAT], [test "x$with_perfstat" = "xyes"])

# Processes plugin under AIX.
if test "x$with_procinfo" = "xyes"; then
  AC_CHECK_HEADERS([procinfo.h],
    [AC_DEFINE([HAVE_PROCINFO_H], [1], [Define to 1 if you have the procinfo.h])],
    [with_procinfo="no (procinfo.h not found)"]
  )
fi

if test "x$ac_system" = "xSolaris"; then
  with_kstat="yes"
  with_devinfo="yes"
else
  with_kstat="no (Solaris only)"
  with_devinfo="no (Solaris only)"
fi

if test "x$with_kstat" = "xyes"; then
  AC_CHECK_LIB([kstat], [kstat_open],
    [with_kstat="yes"],
    [with_kstat="no (libkstat not found)"]
  )
fi

if test "x$with_kstat" = "xyes"; then
  AC_CHECK_LIB([devinfo], [di_init],
    [with_devinfo="yes"],
    [with_devinfo="no (not found)"]
  )
  AC_CHECK_HEADERS([kstat.h],
    [AC_DEFINE(HAVE_LIBKSTAT, [1], [Define to 1 if you have the 'kstat' library (-lkstat)])],
    [with_kstat="no (kstat.h not found)"]
  )
fi

AM_CONDITIONAL([BUILD_WITH_LIBDEVINFO], [test "x$with_devinfo" = "xyes"])
AM_CONDITIONAL([BUILD_WITH_LIBKSTAT], [test "x$with_kstat" = "xyes"])

if test "x$ac_system" = "xDarwin"; then
  with_libiokit="yes"
else
  with_libiokit="no"
fi
AM_CONDITIONAL([BUILD_WITH_LIBIOKIT], [test "x$with_libiokit" = "xyes"])

with_libkvm="no"
AC_CHECK_LIB([kvm], [kvm_getprocs],
  [with_kvm_getprocs="yes"],
  [with_kvm_getprocs="no"]
)

if test "x$with_kvm_getprocs" = "xyes"; then
  AC_DEFINE([HAVE_LIBKVM_GETPROCS], [1],
    [Define to 1 if you have the 'kvm' library with the 'kvm_getprocs' symbol (-lkvm)]
  )
  with_libkvm="yes"
fi

AM_CONDITIONAL([BUILD_WITH_LIBKVM_GETPROCS], [test "x$with_kvm_getprocs" = "xyes"])

AC_CHECK_LIB([kvm], [kvm_getswapinfo],
  [with_kvm_getswapinfo="yes"],
  [with_kvm_getswapinfo="no"]
)

if test "x$with_kvm_getswapinfo" = "xyes"; then
  AC_DEFINE([HAVE_LIBKVM_GETSWAPINFO], [1],
    [Define to 1 if you have the 'kvm' library with the 'kvm_getswapinfo' symbol (-lkvm)]
  )
  with_libkvm="yes"
fi

AM_CONDITIONAL([BUILD_WITH_LIBKVM_GETSWAPINFO], [test "x$with_kvm_getswapinfo" = "xyes"])

AC_CHECK_LIB([kvm], [kvm_nlist],
  [with_kvm_nlist="yes"],
  [with_kvm_nlist="no"]
)

if test "x$with_kvm_nlist" = "xyes"; then
  AC_CHECK_HEADERS([bsd/nlist.h nlist.h])
  AC_DEFINE([HAVE_LIBKVM_NLIST], [1],
    [Define to 1 if you have the 'kvm' library with the 'kvm_nlist' symbol (-lkvm)]
  )
  with_libkvm="yes"
fi

AM_CONDITIONAL([BUILD_WITH_LIBKVM_NLIST], [test "x$with_kvm_nlist" = "xyes"])

AC_CHECK_LIB([kvm], [kvm_openfiles],
  [with_kvm_openfiles="yes"],
  [with_kvm_openfiles="no"]
)

if test "x$with_kvm_openfiles" = "xyes"; then
  AC_DEFINE([HAVE_LIBKVM_NLIST], [1],
    [Define to 1 if you have the 'kvm' library with the 'kvm_openfiles' symbol (-lkvm)]
  )
  with_libkvm="yes"
fi

# --with-libaquaero5 {{{
AC_ARG_WITH([libaquaero5],
  [AS_HELP_STRING([--with-libaquaero5@<:@=PREFIX@:>@], [Path to aquatools-ng source code.])],
  [
    if test "x$withval" = "xyes"; then
      with_libaquaero5="yes"
    else if test "x$withval" = "xno"; then
      with_libaquaero5="no"
    else
      with_libaquaero5="yes"
      LIBAQUAERO5_CFLAGS="$LIBAQUAERO5_CFLAGS -I$withval/src"
      LIBAQUAERO5_LDFLAGS="$LIBAQUAERO5_LDFLAGS -L$withval/obj"
    fi; fi
  ],
  [with_libaquaero5="yes"]
)

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"
CPPFLAGS="$CPPFLAGS $LIBAQUAERO5_CFLAGS"
LDFLAGS="$LDFLAGS $LIBAQUAERO5_LDFLAGS"

if test "x$with_libaquaero5" = "xyes"; then
  if test "x$LIBAQUAERO5_CFLAGS" != "x"; then
    AC_MSG_NOTICE([libaquaero5 CPPFLAGS: $LIBAQUAERO5_CFLAGS])
  fi
  AC_CHECK_HEADERS([libaquaero5.h],
    [with_libaquaero5="yes"],
    [with_libaquaero5="no (libaquaero5.h not found)"]
  )
fi

if test "x$with_libaquaero5" = "xyes"; then
  if test "x$LIBAQUAERO5_LDFLAGS" != "x"; then
    AC_MSG_NOTICE([libaquaero5 LDFLAGS: $LIBAQUAERO5_LDFLAGS])
  fi
  AC_CHECK_LIB([aquaero5], libaquaero5_poll,
    [with_libaquaero5="yes"],
    [with_libaquaero5="no (symbol 'libaquaero5_poll' not found)"]
  )
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

if test "x$with_libaquaero5" = "xyes"; then
  BUILD_WITH_LIBAQUAERO5_CFLAGS="$LIBAQUAERO5_CFLAGS"
  BUILD_WITH_LIBAQUAERO5_LDFLAGS="$LIBAQUAERO5_LDFLAGS"
fi
AC_SUBST([BUILD_WITH_LIBAQUAERO5_CFLAGS])
AC_SUBST([BUILD_WITH_LIBAQUAERO5_LDFLAGS])
# }}}

# --with-libhiredis {{{
AC_ARG_WITH([libhiredis],
  [AS_HELP_STRING([--with-libhiredis@<:@=PREFIX@:>@], [Path to libhiredis.])],
  [
    if test "x$withval" = "xyes"; then
      with_libhiredis="yes"
    else if test "x$withval" = "xno"; then
      with_libhiredis="no"
    else
      with_libhiredis="yes"
      LIBHIREDIS_CPPFLAGS="$LIBHIREDIS_CPPFLAGS -I$withval/include"
      LIBHIREDIS_LDFLAGS="$LIBHIREDIS_LDFLAGS -L$withval/lib"
    fi; fi
  ],
  [with_libhiredis="yes"]
)

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"
CPPFLAGS="$CPPFLAGS $LIBHIREDIS_CPPFLAGS"
LDFLAGS="$LDFLAGS $LIBHIREDIS_LDFLAGS"

if test "x$with_libhiredis" = "xyes"; then
  if test "x$LIBHIREDIS_CPPFLAGS" != "x"; then
    AC_MSG_NOTICE([libhiredis CPPFLAGS: $LIBHIREDIS_CPPFLAGS])
  fi
  AC_CHECK_HEADERS([hiredis/hiredis.h],
    [with_libhiredis="yes"],
    [with_libhiredis="no (hiredis.h not found)"]
  )
fi

if test "x$with_libhiredis" = "xyes"; then
  if test "x$LIBHIREDIS_LDFLAGS" != "x"; then
    AC_MSG_NOTICE([libhiredis LDFLAGS: $LIBHIREDIS_LDFLAGS])
  fi
  AC_CHECK_LIB([hiredis], [redisCommand],
    [with_libhiredis="yes"],
    [with_libhiredis="no (symbol 'redisCommand' not found)"]
  )
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

if test "x$with_libhiredis" = "xyes"; then
  BUILD_WITH_LIBHIREDIS_CPPFLAGS="$LIBHIREDIS_CPPFLAGS"
  BUILD_WITH_LIBHIREDIS_LDFLAGS="$LIBHIREDIS_LDFLAGS"
fi

AC_SUBST([BUILD_WITH_LIBHIREDIS_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBHIREDIS_LDFLAGS])
# }}}

# --with-libcurl {{{
with_curl_config="curl-config"
with_curl_cflags=""
with_curl_libs=""
AC_ARG_WITH(libcurl,
  [AS_HELP_STRING([--with-libcurl@<:@=PREFIX@:>@], [Path to libcurl.])],
  [
    if test "x$withval" = "xno"; then
      with_libcurl="no"
    else if test "x$withval" = "xyes"; then
      with_libcurl="yes"
    else
      if test -f "$withval" && test -x "$withval"; then
        with_curl_config="$withval"
        with_libcurl="yes"
      else if test -x "$withval/bin/curl-config"; then
        with_curl_config="$withval/bin/curl-config"
        with_libcurl="yes"
      fi; fi
      with_libcurl="yes"
    fi; fi
  ],
  [with_libcurl="yes"]
)

if test "x$with_libcurl" = "xyes"; then
  with_curl_cflags=`$with_curl_config --cflags 2>/dev/null`
  curl_config_status=$?

  if test $curl_config_status -ne 0; then
    with_libcurl="no ($with_curl_config failed)"
  else
    SAVE_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $with_curl_cflags"

    AC_CHECK_HEADERS([curl/curl.h],
      [with_libcurl="yes"],
      [with_libcurl="no (curl/curl.h not found)"]
    )

    CPPFLAGS="$SAVE_CPPFLAGS"
  fi
fi

if test "x$with_libcurl" = "xyes"; then
  with_curl_libs=`$with_curl_config --libs 2>/dev/null`
  curl_config_status=$?

  if test $curl_config_status -ne 0; then
    with_libcurl="no ($with_curl_config failed)"
  else
    AC_CHECK_LIB([curl], [curl_easy_init],
      [with_libcurl="yes"],
      [with_libcurl="no (symbol 'curl_easy_init' not found)"],
      [$with_curl_libs]
    )

    AC_CHECK_DECL([CURLOPT_USERNAME],
      [have_curlopt_username="yes"],
      [have_curlopt_username="no"],
      [[#include <curl/curl.h>]]
    )

    AC_CHECK_DECL(CURLOPT_TIMEOUT_MS,
      [have_curlopt_timeout="yes"],
      [have_curlopt_timeout="no"],
      [[#include <curl/curl.h>]]
    )
  fi
fi

if test "x$with_libcurl" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  SAVE_LDFLAGS="$LDFLAGS"
  CPPFLAGS="$CPPFLAGS $with_curl_cflags"
  LDFLAGS="$LDFLAGS $with_curl_libs"
  AC_CACHE_CHECK([for CURLINFO_APPCONNECT_TIME],
    [c_cv_have_curlinfo_appconnect_time],
    [
      AC_LINK_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[#include <curl/curl.h>]],
            [[
              int val = CURLINFO_APPCONNECT_TIME;
              return val;
            ]]
          )
        ],
        [c_cv_have_curlinfo_appconnect_time="yes"],
        [c_cv_have_curlinfo_appconnect_time="no"]
      )
    ]
  )
  CPPFLAGS="$SAVE_CPPFLAGS"
  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$c_cv_have_curlinfo_appconnect_time" = "xyes"; then
  AC_DEFINE([HAVE_CURLINFO_APPCONNECT_TIME], [1],
    [Define if curl.h defines CURLINFO_APPCONNECT_TIME.]
  )
fi

if test "x$with_libcurl" = "xyes"; then
  BUILD_WITH_LIBCURL_CFLAGS="$with_curl_cflags"
  BUILD_WITH_LIBCURL_LIBS="$with_curl_libs"

  if test "x$have_curlopt_username" = "xyes"; then
    AC_DEFINE([HAVE_CURLOPT_USERNAME], [1],
      [Define if libcurl supports CURLOPT_USERNAME option.]
    )
  fi

  if test "x$have_curlopt_timeout" = "xyes"; then
    AC_DEFINE([HAVE_CURLOPT_TIMEOUT_MS], [1],
      [Define if libcurl supports CURLOPT_TIMEOUT_MS option.]
    )
  fi
fi

AC_SUBST(BUILD_WITH_LIBCURL_CFLAGS)
AC_SUBST(BUILD_WITH_LIBCURL_LIBS)
# }}}

# --with-libdbi {{{
AC_ARG_WITH([libdbi],
  [AS_HELP_STRING([--with-libdbi@<:@=PREFIX@:>@], [Path to libdbi.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libdbi_cppflags="-I$withval/include"
      with_libdbi_ldflags="-L$withval/lib"
      with_libdbi="yes"
    else
      with_libdbi="$withval"
    fi
  ],
  [with_libdbi="yes"]
)

if test "x$with_libdbi" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libdbi_cppflags"

  AC_CHECK_HEADERS([dbi/dbi.h],
    [with_libdbi="yes"],
    [with_libdbi="no (dbi/dbi.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libdbi" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libdbi_ldflags"

  AC_CHECK_LIB([dbi], [dbi_initialize],
    [with_libdbi="yes"],
    [with_libdbi="no (Symbol 'dbi_initialize' not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

BUILD_WITH_LIBDBI_CPPFLAGS="$with_libdbi_cppflags"
BUILD_WITH_LIBDBI_LDFLAGS="$with_libdbi_ldflags"
BUILD_WITH_LIBDBI_LIBS="-ldbi"
AC_SUBST(BUILD_WITH_LIBDBI_CPPFLAGS)
AC_SUBST(BUILD_WITH_LIBDBI_LDFLAGS)
AC_SUBST(BUILD_WITH_LIBDBI_LIBS)
# }}}

# --with-libdpdk {{{
AC_ARG_VAR([LIBDPDK_CPPFLAGS], [Preprocessor flags for libdpdk])
AC_ARG_VAR([LIBDPDK_CFLAGS], [Compiler flags for libdpdk])
AC_ARG_VAR([LIBDPDK_LDFLAGS], [Linker flags for libdpdk])
AC_ARG_VAR([LIBDPDK_LIBS], [Libraries to link for libdpdk])

AC_ARG_WITH([libdpdk],
  [AS_HELP_STRING([--without-libdpdk], [Disable libdpdk.])],
  [with_libdpdk="$withval"],
  [with_libdpdk="yes"]
)

if test "x$with_libdpdk" != "xno"; then
  PKG_CHECK_MODULES([DPDK], [libdpdk], [],
    [AC_MSG_NOTICE([no DPDK pkg-config, using defaults])])
  if test "x$LIBDPDK_CPPFLAGS" = "x"; then
    LIBDPDK_CPPFLAGS="-I/usr/include/dpdk"
  fi
  if test "x$LIBDPDK_CFLAGS" = "x"; then
    LIBDPDK_CFLAGS="$DPDK_CFLAGS"
    LIBDPDK_CPPFLAGS="$LIBDPDK_CPPFLAGS $DPDK_CFLAGS"
  fi
  if test "x$LIBDPDK_LIBS" = "x"; then
    if test "x$DPDK_LIBS" != "x"; then
      LIBDPDK_LIBS="$DPDK_LIBS"
    else
      LIBDPDK_LIBS="-ldpdk"
    fi
  fi
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$LIBDPDK_CPPFLAGS $CPPFLAGS"
  SAVE_CFLAGS="$CFLAGS"
  CFLAGS="$LIBDPDK_CFLAGS $CFLAGS"
  AC_CHECK_HEADERS([rte_config.h],
    [
      with_libdpdk="yes"
      AC_PREPROC_IFELSE(
        [
          AC_LANG_SOURCE(
            [[
              #include <rte_version.h>
              #if RTE_VERSION < RTE_VERSION_NUM(16,7,0,0)
              #error "required DPDK >= 16.07"
              #endif
            ]]
          )
        ],
        [dpdk_keepalive="yes"],
        [dpdk_keepalive="no (DPDK version < 16.07)"]
      )
    ],
    [with_libdpdk="no (rte_config.h not found)"]
  )
  CPPFLAGS="$SAVE_CPPFLAGS"
  CFLAGS="$SAVE_CFLAGS"
fi

if test "x$with_libdpdk" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LIBDPDK_LDFLAGS $LDFLAGS"
  AC_CHECK_LIB([dpdk], [rte_eal_init],
    [with_libdpdk="yes"],
    [with_libdpdk="no (symbol 'rte_eal_init' not found)"]
  )
  LDFLAGS="$SAVE_LDFLAGS"
fi

# }}}

# --with-libesmtp {{{
AC_ARG_WITH([libesmtp],
  [AS_HELP_STRING([--with-libesmtp@<:@=PREFIX@:>@], [Path to libesmtp.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libesmtp_cppflags="-I$withval/include"
      with_libesmtp_ldflags="-L$withval/lib"
      with_libesmtp="yes"
    else
      with_libesmtp="$withval"
    fi
  ],
  [with_libesmtp="yes"]
)

if test "x$with_libesmtp" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libesmtp_cppflags"

  AC_CHECK_HEADERS([libesmtp.h],
    [with_libesmtp="yes"],
    [with_libesmtp="no (libesmtp.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libesmtp" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_esmtp_ldflags"

  AC_CHECK_LIB([esmtp], [smtp_create_session],
    [with_libesmtp="yes"],
    [with_libesmtp="no (Symbol 'smtp_create_session' not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

BUILD_WITH_LIBESMTP_CPPFLAGS="$with_libesmtp_cppflags"
BUILD_WITH_LIBESMTP_LDFLAGS="$with_libesmtp_ldflags"
BUILD_WITH_LIBESMTP_LIBS="-lesmtp"
AC_SUBST(BUILD_WITH_LIBESMTP_CPPFLAGS)
AC_SUBST(BUILD_WITH_LIBESMTP_LDFLAGS)
AC_SUBST(BUILD_WITH_LIBESMTP_LIBS)
# }}}

# --with-libganglia {{{
AC_ARG_WITH([libganglia],
  [AS_HELP_STRING([--with-libganglia@<:@=PREFIX@:>@], [Path to libganglia.])],
  [
    if test -f "$withval" && test -x "$withval"; then
      with_libganglia_config="$withval"
      with_libganglia="yes"
    else if test -f "$withval/bin/ganglia-config" && test -x "$withval/bin/ganglia-config"; then
      with_libganglia_config="$withval/bin/ganglia-config"
      with_libganglia="yes"
    else if test -d "$withval"; then
      GANGLIA_CPPFLAGS="-I$withval/include"
      GANGLIA_LDFLAGS="-L$withval/lib"
      with_libganglia="yes"
    else
      with_libganglia="$withval"
    fi; fi; fi
  ],
  [with_libganglia="yes"]
)

if test "x$with_libganglia" = "xyes"; then
  if test "x$with_libganglia_config" != "x"; then
    if test "x$GANGLIA_CPPFLAGS" = "x"; then
      GANGLIA_CPPFLAGS=`"$with_libganglia_config" --cflags 2>/dev/null`
    fi

    if test "x$GANGLIA_LDFLAGS" = "x"; then
      GANGLIA_LDFLAGS=`"$with_libganglia_config" --ldflags 2>/dev/null`
    fi

    if test "x$GANGLIA_LIBS" = "x"; then
      GANGLIA_LIBS=`"$with_libganglia_config" --libs 2>/dev/null`
    fi
  else
    GANGLIA_LIBS="-lganglia"
  fi
fi

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"
CPPFLAGS="$CPPFLAGS $GANGLIA_CPPFLAGS"
LDFLAGS="$LDFLAGS $GANGLIA_LDFLAGS"

if test "x$with_libganglia" = "xyes"; then
  AC_CHECK_HEADERS([gm_protocol.h],
    [with_libganglia="yes"],
    [with_libganglia="no (gm_protocol.h not found)"]
  )
fi

if test "x$with_libganglia" = "xyes"; then
  AC_CHECK_LIB([ganglia], [xdr_Ganglia_value_msg],
    [with_libganglia="yes"],
    [with_libganglia="no (symbol xdr_Ganglia_value_msg not found)"]
  )
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

AC_SUBST(GANGLIA_CPPFLAGS)
AC_SUBST(GANGLIA_LDFLAGS)
AC_SUBST(GANGLIA_LIBS)
# }}}

# --with-libgcrypt {{{
GCRYPT_CPPFLAGS="$GCRYPT_CPPFLAGS"
GCRYPT_LDFLAGS="$GCRYPT_LDFLAGS"
GCRYPT_LIBS="$GCRYPT_LIBS"
AC_ARG_WITH([libgcrypt],
  [AS_HELP_STRING([--with-libgcrypt@<:@=PREFIX@:>@], [Path to libgcrypt.])],
  [
    if test -f "$withval" && test -x "$withval"; then
      with_libgcrypt_config="$withval"
      with_libgcrypt="yes"
    else if test -f "$withval/bin/gcrypt-config" && test -x "$withval/bin/gcrypt-config"; then
      with_libgcrypt_config="$withval/bin/gcrypt-config"
      with_libgcrypt="yes"
    else if test -d "$withval"; then
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
  ]
)

if test "x$with_libgcrypt" = "xyes" && test "x$with_libgcrypt_config" != "x"; then
  if test "x$GCRYPT_CPPFLAGS" = "x"; then
    GCRYPT_CPPFLAGS=`"$with_libgcrypt_config" --cflags 2>/dev/null`
  fi

  if test "x$GCRYPT_LIBS" = "x"; then
    GCRYPT_LIBS=`"$with_libgcrypt_config" --libs 2>/dev/null`
  fi
fi

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"
SAVE_LIBS="$LIBS"
CPPFLAGS="$CPPFLAGS $GCRYPT_CPPFLAGS"
LDFLAGS="$LDFLAGS $GCRYPT_LDFLAGS"
LIBS="$LIBS $GCRYPT_LIBS"

if test "x$with_libgcrypt" = "xyes"; then
  if test "x$GCRYPT_CPPFLAGS" != "x"; then
    AC_MSG_NOTICE([gcrypt CPPFLAGS: $GCRYPT_CPPFLAGS])
  fi
  AC_CHECK_HEADERS([gcrypt.h],
    [with_libgcrypt="yes"],
    [with_libgcrypt="no (gcrypt.h not found)"]
  )
fi

if test "x$with_libgcrypt" = "xyes"; then
  AC_CHECK_LIB(gcrypt, gcry_md_hash_buffer,
    [with_libgcrypt="yes"],
    [with_libgcrypt="no (symbol gcry_md_hash_buffer not found)"]
  )
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"
LIBS="$SAVE_LIBS"

AC_SUBST([GCRYPT_CPPFLAGS])
AC_SUBST([GCRYPT_LDFLAGS])
AC_SUBST([GCRYPT_LIBS])
AM_CONDITIONAL([BUILD_WITH_LIBGCRYPT], [test "x$with_libgcrypt" = "xyes"])
# }}}

# --with-libgps {{{
AC_ARG_WITH([libgps],
  [AS_HELP_STRING([--with-libgps@<:@=PREFIX@:>@], [Path to libgps.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libgps_cflags="-I$withval/include"
      with_libgps_ldflags="-L$withval/lib"
      with_libgps="yes"
    else
      with_libgps="$withval"
    fi
  ],
  [with_libgps="yes"]
)

if test "x$with_libgps" = "xyes"; then
  SAVE_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS $with_libgps_cflags"

  AC_CHECK_HEADERS([gps.h],
    [with_libgps="yes"],
    [with_libgps="no (gps.h not found)"]
  )

  CFLAGS="$SAVE_CFLAGS"
fi

if test "x$with_libgps" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libgps_ldflags"

  AC_CHECK_LIB([gps], [gps_open],
    [with_libgps="yes"],
    [with_libgps="no (symbol gps_open not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libgps" = "xyes"; then
  BUILD_WITH_LIBGPS_CFLAGS="$with_libgps_cflags"
  BUILD_WITH_LIBGPS_LDFLAGS="$with_libgps_ldflags"
  BUILD_WITH_LIBGPS_LIBS="-lgps"
fi

AC_SUBST([BUILD_WITH_LIBGPS_CFLAGS])
AC_SUBST([BUILD_WITH_LIBGPS_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBGPS_LIBS])

# }}}

# --with-libgrpc++ {{{
AC_ARG_WITH([libgrpc++],
  [AS_HELP_STRING([--with-libgrpc++@<:@=PREFIX@:>@], [Path to libgrpc++.])],
  [
    with_grpcpp="$withval"
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libgrpcpp_cppflags="-I$withval/include"
      with_libgrpcpp_ldflags="-L$withval/lib"
      with_libgrpcpp_bin="$withval/bin"
      with_libgrpcpp="yes"
    fi
    if test "x$withval" = "xno"; then
      with_libgrpcpp="no (disabled on command line)"
    fi
  ],
  [withval="yes"]
)
if test "x$withval" = "xyes"; then
PKG_CHECK_MODULES([GRPCPP], [grpc++],
  [with_libgrpcpp="yes"],
  [with_libgrpcpp="no (pkg-config could not find libgrpc++)"]
)
fi

if test "x$withval" != "xno"; then
  AC_MSG_CHECKING([whether $CXX accepts -std=c++11])
  if test_cxx_flags -std=c++11; then
    AC_MSG_RESULT([yes])
  else
    AC_MSG_RESULT([no])
    with_libgrpcpp="no (requires C++11 support)"
  fi
fi

if test "x$with_libgrpcpp" = "xyes"; then
  AC_LANG_PUSH(C++)
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="-std=c++11 $with_libgrpcpp_cppflags $GRPCPP_CFLAGS $CPPFLAGS"
  AC_CHECK_HEADERS([grpc++/grpc++.h],
    [with_libgrpcpp="yes"],
    [with_libgrpcpp="no (<grpc++/grpc++.h> not found)"]
  )
  CPPFLAGS="$SAVE_CPPFLAGS"
  AC_LANG_POP(C++)
fi

if test "x$with_libgrpcpp" = "xyes"; then
  AC_LANG_PUSH(C++)
  SAVE_CPPFLAGS="$CPPFLAGS"
  SAVE_LDFLAGS="$LDFLAGS"
  SAVE_LIBS="$LIBS"
  CPPFLAGS="-std=c++11 $with_libgrpcpp_cppflags $GRPCPP_CFLAGS $CPPFLAGS"
  LDFLAGS="$with_libgrpcpp_ldflags"
  if test "x$GRPCPP_LIBS" = "x"; then
    LIBS="-lgrpc++"
  else
    LIBS="$GRPCPP_LIBS"
  fi
  AC_LINK_IFELSE(
    [
      AC_LANG_PROGRAM(
        [[#include <grpc++/grpc++.h>]],
        [[grpc::ServerBuilder sb;]]
      )
    ],
    [
      with_libgrpcpp="yes"
      if test "x$GRPCPP_LIBS" = "x"; then
        GRPCPP_LIBS="-lgrpc++"
      fi
    ],
    [with_libgrpcpp="no (libgrpc++ not found)"]
  )
  CPPFLAGS="$SAVE_CPPFLAGS"
  LDFLAGS="$SAVE_LDFLAGS"
  LIBS="$SAVE_LIBS"
  AC_LANG_POP(C++)
fi

BUILD_WITH_LIBGRPCPP_CPPFLAGS="-std=c++11 $with_libgrpcpp_cppflags $GRPCPP_CFLAGS"
BUILD_WITH_LIBGRPCPP_LDFLAGS="$with_libgrpcpp_ldflags"
BUILD_WITH_LIBGRPCPP_LIBS="$GRPCPP_LIBS"
AC_SUBST([BUILD_WITH_LIBGRPCPP_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBGRPCPP_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBGRPCPP_LIBS])
# }}}

AC_ARG_VAR([GRPC_CPP_PLUGIN], [path to the grpc_cpp_plugin binary])
if test "x$with_libgrpcpp_bin" = "x"; then
  AC_PATH_PROG([GRPC_CPP_PLUGIN], [grpc_cpp_plugin])
else
  AC_PATH_PROG([GRPC_CPP_PLUGIN], [grpc_cpp_plugin], [], "$with_libgrpcpp_bin:$PATH")
fi
AM_CONDITIONAL([HAVE_GRPC_CPP], [test "x$GRPC_CPP_PLUGIN" != "x"])

# --with-libiptc {{{
AC_ARG_WITH([libiptc],
  [AS_HELP_STRING([--with-libiptc@<:@=PREFIX@:>@], [Path to libiptc.])],
  [
    if test "x$withval" = "xyes"; then
      with_libiptc="pkgconfig"
    else if test "x$withval" = "xno"; then
      with_libiptc="no"
    else
      with_libiptc="yes"
      with_libiptc_cflags="-I$withval/include"
      with_libiptc_libs="-L$withval/lib"
    fi; fi
  ],
  [
    if test "x$ac_system" = "xLinux"; then
      with_libiptc="pkgconfig"
    else
      with_libiptc="no (Linux only)"
    fi
  ]
)

if test "x$with_libiptc" = "xpkgconfig"; then
  $PKG_CONFIG --exists 'libiptc' 2>/dev/null
  if test $? -ne 0; then
    with_libiptc="no (pkg-config doesn't know libiptc)"
  fi
fi

if test "x$with_libiptc" = "xpkgconfig"; then
  with_libiptc_cflags="`$PKG_CONFIG --cflags 'libiptc'`"
  if test $? -ne 0; then
    with_libiptc="no ($PKG_CONFIG failed)"
  fi

  with_libiptc_libs="`$PKG_CONFIG --libs 'libiptc'`"
  if test $? -ne 0; then
    with_libiptc="no ($PKG_CONFIG failed)"
  fi
fi

SAVE_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $with_libiptc_cflags"

# check whether the header file for libiptc is available.
if test "x$with_libiptc" = "xpkgconfig"; then
  AC_CHECK_HEADERS([libiptc/libiptc.h libiptc/libip6tc.h],
    [],
    [with_libiptc="no (header file missing)"]
  )
fi

# If the header file is available, check for the required type declaractions.
# They may be missing in old versions of libiptc. In that case, they will be
# declared in the iptables plugin.
if test "x$with_libiptc" = "xpkgconfig"; then
  AC_CHECK_TYPES([iptc_handle_t, ip6tc_handle_t], [], [])
fi

# Check for the iptc_init symbol in the library.
# This could be in iptc or ip4tc
if test "x$with_libiptc" = "xpkgconfig"; then
  SAVE_LIBS="$LIBS"
  AC_SEARCH_LIBS([iptc_init], [iptc ip4tc],
    [with_libiptc="pkgconfig"],
    [with_libiptc="no"],
    [$with_libiptc_libs]
  )
  LIBS="$SAVE_LIBS"
fi

if test "x$with_libiptc" = "xpkgconfig"; then
  with_libiptc="yes"
fi

CPPFLAGS="$SAVE_CPPFLAGS"

if test "x$with_libiptc" = "xyes"; then
  BUILD_WITH_LIBIPTC_CPPFLAGS="$with_libiptc_cflags"
  BUILD_WITH_LIBIPTC_LDFLAGS="$with_libiptc_libs"
fi
AC_SUBST([BUILD_WITH_LIBIPTC_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBIPTC_LDFLAGS])
# }}}

# --with-java {{{
with_java_home="$JAVA_HOME"
if test "x$with_java_home" = "x"; then
  with_java_home="/usr/lib/jvm"
fi

JAVAC="$JAVAC"
JAR="$JAR"
AC_ARG_WITH([java],
  [AS_HELP_STRING([--with-java@<:@=PREFIX@:>@], [Path to Java home.])],
  [
    if test "x$withval" = "xno"; then
      with_java="no"
    else if test "x$withval" = "xyes"; then
      with_java="yes"
    else
      with_java_home="$withval"
      with_java="yes"
    fi; fi
  ],
  [with_java="yes"]
)

AX_COMPARE_VERSION([$am__api_version],[lt],[1.12],
  [JAVA_TIMESTAMP_FILE="classdist_noinst.stamp"],
  [JAVA_TIMESTAMP_FILE="classnoinst.stamp"])
if test "x$with_java" = "xyes"; then
  if test -d "$with_java_home"; then
    AC_MSG_CHECKING([for jni.h])
    TMPVAR=`find -L "$with_java_home" -name jni.h -type f -exec 'dirname' '{}' ';' 2>/dev/null | LC_ALL=C sort | head -n 1`
    if test "x$TMPVAR" != "x"; then
      AC_MSG_RESULT([found in $TMPVAR])
      JAVA_CPPFLAGS="$JAVA_CPPFLAGS -I$TMPVAR"
    else
      AC_MSG_RESULT([not found])
    fi

    AC_MSG_CHECKING([for jni_md.h])
    TMPVAR=`find -L "$with_java_home" -name jni_md.h -type f -exec 'dirname' '{}' ';' 2>/dev/null | LC_ALL=C sort | head -n 1`
    if test "x$TMPVAR" != "x"; then
      AC_MSG_RESULT([found in $TMPVAR])
      JAVA_CPPFLAGS="$JAVA_CPPFLAGS -I$TMPVAR"
    else
      AC_MSG_RESULT([not found])
    fi

    AC_MSG_CHECKING([for libjvm.so])
    TMPVAR=`find -L "$with_java_home" -type f \( -name libjvm.so -o -name libjvm.dylib \) -exec 'dirname' '{}' ';' 2>/dev/null | LC_ALL=C sort | head -n 1`
    if test "x$TMPVAR" != "x"; then
      AC_MSG_RESULT([found in $TMPVAR])
      JAVA_LDFLAGS="$JAVA_LDFLAGS -L$TMPVAR -Wl,-rpath -Wl,$TMPVAR"
    else
      AC_MSG_RESULT([not found])
    fi

    if test "x$JAVAC" = "x"; then
      AC_MSG_CHECKING([for javac])
      TMPVAR=`find -L "$with_java_home" -name javac -type f 2>/dev/null | LC_ALL=C sort | head -n 1`
      if test "x$TMPVAR" != "x"; then
        JAVAC="$TMPVAR"
        AC_MSG_RESULT([$JAVAC])
      else
        AC_MSG_RESULT([not found])
      fi
    fi

    if test "x$JAR" = "x"; then
      AC_MSG_CHECKING([for jar])
      TMPVAR=`find -L "$with_java_home" -name jar -type f 2>/dev/null | LC_ALL=C sort | head -n 1`
      if test "x$TMPVAR" != "x"; then
        JAR="$TMPVAR"
        AC_MSG_RESULT([$JAR])
      else
        AC_MSG_RESULT([not found])
      fi
    fi
  else if test "x$with_java_home" != "x"; then
    AC_MSG_WARN([JAVA_HOME: No such directory: $with_java_home])
  fi; fi
fi

if test "x$JAVA_CPPFLAGS" != "x"; then
  AC_MSG_NOTICE([Building with JAVA_CPPFLAGS set to: $JAVA_CPPFLAGS])
fi
if test "x$JAVA_CFLAGS" != "x"; then
  AC_MSG_NOTICE([Building with JAVA_CFLAGS set to: $JAVA_CFLAGS])
fi
if test "x$JAVA_LDFLAGS" != "x"; then
  AC_MSG_NOTICE([Building with JAVA_LDFLAGS set to: $JAVA_LDFLAGS])
fi
if test "x$JAVA_LIBS" != "x"; then
  AC_MSG_NOTICE([Building with JAVA_LIBS set to: $JAVA_LIBS])
fi
if test "x$JAVAC" = "x"; then
  with_javac_path="$PATH"
  if test "x$with_java_home" != "x"; then
    with_javac_path="$with_java_home:with_javac_path"
    if test -d "$with_java_home/bin"; then
      with_javac_path="$with_java_home/bin:with_javac_path"
    fi
  fi

  AC_PATH_PROG([JAVAC], [javac], [], "$with_javac_path")
fi

if test "x$JAVAC" = "x"; then
  with_java="no (javac not found)"
fi

if test "x$JAR" = "x"; then
  with_jar_path="$PATH"
  if test "x$with_java_home" != "x"; then
    with_jar_path="$with_java_home:$with_jar_path"
    if test -d "$with_java_home/bin"; then
      with_jar_path="$with_java_home/bin:$with_jar_path"
    fi
  fi

  AC_PATH_PROG([JAR], [jar], [], "$with_jar_path")
fi

if test "x$JAR" = "x"; then
  with_java="no (jar not found)"
fi

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_CFLAGS="$CFLAGS"
SAVE_LDFLAGS="$LDFLAGS"
SAVE_LIBS="$LIBS"
CPPFLAGS="$CPPFLAGS $JAVA_CPPFLAGS"
CFLAGS="$CFLAGS $JAVA_CFLAGS"
LDFLAGS="$LDFLAGS $JAVA_LDFLAGS"
LIBS="$LIBS $JAVA_LIBS"

if test "x$with_java" = "xyes"; then
  AC_CHECK_HEADERS([jni.h],
    [with_jave="yes"],
    [with_java="no (jni.h not found)"])
fi

if test "x$with_java" = "xyes"; then
  AC_CHECK_LIB([jvm], [JNI_CreateJavaVM],
    [with_java="yes"],
    [with_java="no (Symbol 'JNI_CreateJavaVM' not found)"],
    [$JAVA_LIBS $PTHREAD_LIBS]
  )
fi

if test "x$with_java" = "xyes"; then
  JAVA_LIBS="$JAVA_LIBS -ljvm"
  AC_MSG_NOTICE([Building with JAVA_LIBS set to: $JAVA_LIBS])
fi

CPPFLAGS="$SAVE_CPPFLAGS"
CFLAGS="$SAVE_CFLAGS"
LDFLAGS="$SAVE_LDFLAGS"
LIBS="$SAVE_LIBS"

AC_SUBST([JAVA_CPPFLAGS])
AC_SUBST([JAVA_CFLAGS])
AC_SUBST([JAVA_LDFLAGS])
AC_SUBST([JAVA_LIBS])
AC_SUBST([JAVA_TIMESTAMP_FILE])
AM_CONDITIONAL([BUILD_WITH_JAVA], [test "x$with_java" = "xyes"])
# }}}

# --with-libldap {{{
AC_ARG_WITH([libldap],
  [AS_HELP_STRING([--with-libldap@<:@=PREFIX@:>@], [Path to libldap.])],
  [
    if test "x$withval" = "xyes"; then
      with_libldap="yes"
    else if test "x$withval" = "xno"; then
      with_libldap="no"
    else
      with_libldap="yes"
      LIBLDAP_CPPFLAGS="$LIBLDAP_CPPFLAGS -I$withval/include"
      LIBLDAP_LDFLAGS="$LIBLDAP_LDFLAGS -L$withval/lib"
    fi; fi
  ],
  [with_libldap="yes"]
)

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"

CPPFLAGS="$CPPFLAGS $LIBLDAP_CPPFLAGS"
LDFLAGS="$LDFLAGS $LIBLDAP_LDFLAGS"

if test "x$with_libldap" = "xyes"; then
  if test "x$LIBLDAP_CPPFLAGS" != "x"; then
    AC_MSG_NOTICE([libldap CPPFLAGS: $LIBLDAP_CPPFLAGS])
  fi

  AC_CHECK_HEADERS([ldap.h],
    [with_libldap="yes"],
    [with_libldap="no ('ldap.h' not found)"]
  )
fi

if test "x$with_libldap" = "xyes"; then
  if test "x$LIBLDAP_LDFLAGS" != "x"; then
    AC_MSG_NOTICE([libldap LDFLAGS: $LIBLDAP_LDFLAGS])
  fi

  AC_CHECK_LIB([ldap], [ldap_initialize],
    [with_libldap="yes"],
    [with_libldap="no (symbol 'ldap_initialize' not found)"]
  )
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

if test "x$with_libldap" = "xyes"
then
  BUILD_WITH_LIBLDAP_CPPFLAGS="$LIBLDAP_CPPFLAGS"
  BUILD_WITH_LIBLDAP_LDFLAGS="$LIBLDAP_LDFLAGS"
fi
AC_SUBST([BUILD_WITH_LIBLDAP_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBLDAP_LDFLAGS])
# }}}

# --with-liblua {{{
AC_ARG_VAR([LIBLUA_PKG_CONFIG_NAME], [Name of liblua used by pkg-config])
if test "x$LIBLUA_PKG_CONFIG_NAME" != "x"
then
  PKG_CHECK_MODULES([LUA], [$LIBLUA_PKG_CONFIG_NAME],
    [with_liblua="yes"],
    [with_liblua="no"]
  )
else
  PKG_CHECK_MODULES([LUA], [lua],
    [with_liblua="yes"],
    [
      PKG_CHECK_MODULES([LUA], [lua-5.3],
        [with_liblua="yes"],
        [
          PKG_CHECK_MODULES([LUA], [lua5.3],
            [with_liblua="yes"],
            [
              PKG_CHECK_MODULES([LUA], [lua-5.2],
                [with_liblua="yes"],
                [
                  PKG_CHECK_MODULES([LUA], [lua5.2],
                    [with_liblua="yes"],
                    [
                      PKG_CHECK_MODULES([LUA], [lua-5.1],
                        [with_liblua="yes"],
                        [
                          PKG_CHECK_MODULES([LUA], [lua5.1],
                            [with_liblua="yes"],
                            [with_liblua="no (pkg-config cannot find liblua)"]
                          )
                        ]
                      )
                    ]
                  )
                ]
              )
            ]
          )
        ]
      )
    ]
  )
fi

if test "x$with_liblua" = "xyes"
then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $LUA_CFLAGS"

  AC_CHECK_HEADERS([lua.h lauxlib.h lualib.h],
    [with_liblua="yes"],
    [with_liblua="no (header not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_liblua" = "xyes"
then
  SAVE_LIBS="$LIBS"
  LIBS="$LIBS $LUA_LIBS"

  AC_CHECK_FUNC([lua_settop],
    [with_liblua="yes"],
    [with_liblua="no (symbol 'lua_settop' not found)"]
  )

  LIBS="$SAVE_LIBS"
fi

if test "x$with_liblua" = "xyes"
then
    BUILD_WITH_LIBLUA_CFLAGS="$LUA_CFLAGS"
    BUILD_WITH_LIBLUA_LIBS="$LUA_LIBS"
fi
AC_SUBST(BUILD_WITH_LIBLUA_CFLAGS)
AC_SUBST(BUILD_WITH_LIBLUA_LIBS)
# }}}

# --with-liblvm2app {{{
AC_ARG_WITH([liblvm2app],
  [AS_HELP_STRING([--with-liblvm2app@<:@=PREFIX@:>@], [Path to liblvm2app.])],
  [
    if test "x$withval" = "xno"; then
      with_liblvm2app="no"
    else
      with_liblvm2app="yes"
      if test "x$withval" != "xyes"; then
        with_liblvm2app_cppflags="-I$withval/include"
        with_liblvm2app_ldflags="-L$withval/lib"
      fi
    fi
  ],
  [
    if test "x$ac_system" = "xLinux"; then
      with_liblvm2app="yes"
    else
      with_liblvm2app="no (Linux only library)"
    fi
  ]
)

if test "x$with_liblvm2app" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_liblvm2app_cppflags"

  AC_CHECK_HEADERS([lvm2app.h],
    [with_liblvm2app="yes"],
    [with_liblvm2app="no (lvm2app.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_liblvm2app" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  SAVE_LDFLAGS="$LDFLAGS"
  CPPFLAGS="$CPPFLAGS $with_liblvm2app_cppflags"
  LDFLAGS="$LDFLAGS $with_liblvm2app_ldflags"

  AC_CHECK_LIB([lvm2app], [lvm_lv_get_property],
    [with_liblvm2app="yes"],
    [with_liblvm2app="no (Symbol 'lvm_lv_get_property' not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_liblvm2app" = "xyes"; then
  BUILD_WITH_LIBLVM2APP_CPPFLAGS="$with_liblvm2app_cppflags"
  BUILD_WITH_LIBLVM2APP_LDFLAGS="$with_liblvm2app_ldflags"
  BUILD_WITH_LIBLVM2APP_LIBS="-llvm2app"
fi

AC_SUBST([BUILD_WITH_LIBLVM2APP_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBLVM2APP_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBLVM2APP_LIBS])
# }}}

# --with-libmemcached {{{
AC_ARG_WITH([libmemcached],
  [AS_HELP_STRING([--with-libmemcached@<:@=PREFIX@:>@], [Path to libmemcached.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libmemcached_cppflags="-I$withval/include"
      with_libmemcached_ldflags="-L$withval/lib"
      with_libmemcached="yes"
    else
      with_libmemcached="$withval"
    fi
  ],
  [with_libmemcached="yes"]
)

if test "x$with_libmemcached" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libmemcached_cppflags"

  AC_CHECK_HEADERS([libmemcached/memcached.h],
    [with_libmemcached="yes"],
    [with_libmemcached="no (libmemcached/memcached.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libmemcached" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  SAVE_LDFLAGS="$LDFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libmemcached_cppflags"
  LDFLAGS="$LDFLAGS $with_libmemcached_ldflags"

  AC_CHECK_LIB([memcached], [memcached_create],
    [with_libmemcached="yes"],
    [with_libmemcached="no (Symbol 'memcached_create' not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libmemcached" = "xyes"; then
  BUILD_WITH_LIBMEMCACHED_CPPFLAGS="$with_libmemcached_cppflags"
  BUILD_WITH_LIBMEMCACHED_LDFLAGS="$with_libmemcached_ldflags"
  BUILD_WITH_LIBMEMCACHED_LIBS="-lmemcached"
fi

AC_SUBST([BUILD_WITH_LIBMEMCACHED_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBMEMCACHED_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBMEMCACHED_LIBS])
# }}}

# --with-libmicrohttpd {{{
with_libmicrohttpd_cppflags=""
with_libmicrohttpd_ldflags=""
AC_ARG_WITH([libmicrohttpd], [AS_HELP_STRING([--with-libmicrohttpd@<:@=PREFIX@:>@], [Path to libmicrohttpd.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libmicrohttpd_cppflags="-I$withval/include"
      with_libmicrohttpd_ldflags="-L$withval/lib"
      with_libmicrohttpd="yes"
    fi
    if test "x$withval" = "xno"; then
      with_libmicrohttpd="no (disabled on command line)"
    fi
  ],
  [withval="yes"]
)
if test "x$withval" = "xyes"; then
  PKG_CHECK_MODULES([MICROHTTPD], [libmicrohttpd],
    [with_libmicrohttpd="yes"],
    [with_libmicrohttpd="no (pkg-config could not find libmicrohttpd)"]
  )
fi

if test "x$MICROHTTPD_LIBS" = "x"; then
  MICROHTTPD_LIBS="-lmicrohttpd"
fi

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"
SAVE_LIBS="$LIBS"
CPPFLAGS="$with_libmicrohttpd_cppflags $MICROHTTPD_CFLAGS"
LDFLAGS="$with_libmicrohttpd_ldflags $LDFLAGS"
LIBS="$LIBS $MICROHTTPD_LIBS"

if test "x$with_libmicrohttpd" = "xyes"; then
  AC_CHECK_HEADERS([microhttpd.h],
   [with_libmicrohttpd="yes"],
   [with_libmicrohttpd="no (<microhttpd.h> not found)"]
  )
fi

if test "x$with_libmicrohttpd" = "xyes"; then
  AC_CHECK_LIB([microhttpd], [MHD_start_daemon],
    [with_libmicrohttpd="yes"],
    [with_libmicrohttpd="no (libmicrohttpd not found)"]
  )
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"
LIBS="$SAVE_LIBS"

BUILD_WITH_LIBMICROHTTPD_CPPFLAGS="$with_libmicrohttpd_cppflags $MICROHTTPD_CFLAGS"
BUILD_WITH_LIBMICROHTTPD_LDFLAGS="$with_libmicrohttpd_ldflags"
BUILD_WITH_LIBMICROHTTPD_LIBS="$MICROHTTPD_LIBS"
AC_SUBST([BUILD_WITH_LIBMICROHTTPD_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBMICROHTTPD_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBMICROHTTPD_LIBS])
# }}}

# --with-libmodbus {{{
AC_ARG_WITH([libmodbus],
  [AS_HELP_STRING([--with-libmodbus@<:@=PREFIX@:>@], [Path to the modbus library.])],
  [
    if test "x$withval" = "xno"; then
      with_libmodbus="no"
    else if test "x$withval" = "xyes"; then
      with_libmodbus="use_pkgconfig"
    else if test -d "$with_libmodbus/lib"; then
      AC_MSG_NOTICE([Not checking for libmodbus: Manually configured])
      with_libmodbus_cflags="-I$withval/include"
      with_libmodbus_libs="-L$withval/lib -lmodbus"
      with_libmodbus="yes"
    fi; fi; fi
  ],
  [with_libmodbus="use_pkgconfig"]
)

# configure using pkg-config
if test "x$with_libmodbus" = "xuse_pkgconfig"; then
  AC_MSG_NOTICE([Checking for libmodbus using $PKG_CONFIG])
  $PKG_CONFIG --exists 'libmodbus' 2>/dev/null
  if test $? -ne 0; then
    with_libmodbus="no (pkg-config doesn't know libmodbus)"
  fi
fi

if test "x$with_libmodbus" = "xuse_pkgconfig"; then
  with_libmodbus_cflags="`$PKG_CONFIG --cflags 'libmodbus'`"
  if test $? -ne 0; then
    with_libmodbus="no ($PKG_CONFIG failed)"
  fi

  with_libmodbus_libs="`$PKG_CONFIG --libs 'libmodbus'`"
  if test $? -ne 0; then
    with_libmodbus="no ($PKG_CONFIG failed)"
  fi
fi

if test "x$with_libmodbus" = "xuse_pkgconfig"; then
  with_libmodbus="yes"
fi

if test "x$with_libmodbus" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libmodbus_cflags"

  AC_CHECK_HEADERS([modbus.h],
    [with_libmodbus="yes"],
    [with_libmodbus="no (modbus.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libmodbus" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libmodbus_libs"

  AC_CHECK_LIB([modbus], [modbus_connect],
    [with_libmodbus="yes"],
    [with_libmodbus="no (symbol modbus_connect not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libmodbus" = "xyes"; then
  BUILD_WITH_LIBMODBUS_CFLAGS="$with_libmodbus_cflags"
  BUILD_WITH_LIBMODBUS_LIBS="$with_libmodbus_libs"
fi

AC_SUBST([BUILD_WITH_LIBMODBUS_CFLAGS])
AC_SUBST([BUILD_WITH_LIBMODBUS_LIBS])
# }}}

# --with-libmongoc {{{
AC_ARG_WITH([libmongoc],
  [AS_HELP_STRING([--with-libmongoc@<:@=PREFIX@:>@], [Path to libmongoc.])],
  [
    if test "x$withval" = "xyes"; then
      with_libmongoc="yes"
    else if test "x$withval" = "xno"; then
      with_libmongoc="no"
    else
      with_libmongoc="no"
    fi; fi
  ],
  [with_libmongoc="yes"]
)

if test "x$with_libmongoc" = "xyes"; then
  PKG_CHECK_MODULES([LIBMONGOC], [libmongoc-1.0],
    [with_libmongoc="yes"],
    [with_libmongoc="no (pkg-config could not find libmongoc)"]
  )
fi

if test "x$with_libmongoc" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"

  CPPFLAGS="$CPPFLAGS $LIBMONGOC_CFLAGS"

  if test "x$CPPFLAGS" != "x"; then
    AC_MSG_NOTICE([libmongoc CPPFLAGS: $LIBMONGOC_CFLAGS])
  fi

  AC_CHECK_HEADERS([mongoc.h],
    [with_libmongoc="yes"],
    [with_libmongoc="no ('mongoc.h' not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libmongoc" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  SAVE_LDFLAGS="$LDFLAGS"

  CPPFLAGS="$CPPFLAGS $LIBMONGOC_CFLAGS"
  LDFLAGS="$LDFLAGS $LIBMONGOC_LDFLAGS"

  if test "x$LIBMONGOC_LDFLAGS" != "x"; then
    AC_MSG_NOTICE([libmongoc LDFLAGS: $LIBMONGOC_LDFLAGS])
  fi

  AC_CHECK_LIB([mongoc-1.0], [mongoc_init],
    [with_libmongoc="yes"],
    [with_libmongoc="no (symbol 'mongoc_init' not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libmongoc" = "xyes"; then
  BUILD_WITH_LIBMONGOC_CFLAGS="$LIBMONGOC_CFLAGS"
  BUILD_WITH_LIBMONGOC_LDFLAGS="$LIBMONGOC_LDFLAGS"
fi

AC_SUBST([BUILD_WITH_LIBMONGOC_CFLAGS])
AC_SUBST([BUILD_WITH_LIBMONGOC_LDFLAGS])
# }}}

# --with-libmosquitto {{{
AC_ARG_WITH([libmosquitto],
  [AS_HELP_STRING([--with-libmosquitto@<:@=PREFIX@:>@], [Path to libmosquitto.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libmosquitto_cppflags="-I$withval/include"
      with_libmosquitto_ldflags="-L$withval/lib"
      with_libmosquitto="yes"
    else
      with_libmosquitto="$withval"
    fi
  ],
  [with_libmosquitto="yes"]
)

if test "x$with_libmosquitto" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libmosquitto_cppflags"

  AC_CHECK_HEADERS([mosquitto.h],
    [with_libmosquitto="yes"],
    [with_libmosquitto="no (mosquitto.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libmosquitto" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libmosquitto_ldflags"

  AC_CHECK_LIB([mosquitto], [mosquitto_connect],
    [with_libmosquitto="yes"],
    [with_libmosquitto="no (libmosquitto not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libmosquitto" = "xyes"; then
  BUILD_WITH_LIBMOSQUITTO_CPPFLAGS="$with_libmosquitto_cppflags"
  BUILD_WITH_LIBMOSQUITTO_LDFLAGS="$with_libmosquitto_ldflags"
  BUILD_WITH_LIBMOSQUITTO_LIBS="-lmosquitto"
fi

AC_SUBST([BUILD_WITH_LIBMOSQUITTO_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBMOSQUITTO_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBMOSQUITTO_LIBS])
# }}}

# --with-libmysql {{{
with_mysql_config="mysql_config"
AC_ARG_WITH([libmysql],
  [AS_HELP_STRING([--with-libmysql@<:@=PREFIX@:>@], [Path to libmysql.])],
  [
    if test "x$withval" = "xno"; then
      with_libmysql="no"
    else if test "x$withval" = "xyes"; then
      with_libmysql="yes"
    else
      if test -f "$withval" && test -x "$withval"; then
        with_mysql_config="$withval"
      else if test -x "$withval/bin/mysql_config"; then
        with_mysql_config="$withval/bin/mysql_config"
      fi; fi
      with_libmysql="yes"
    fi; fi
  ],
  [with_libmysql="yes"]
)

if test "x$with_libmysql" = "xyes"; then
  with_mysql_cflags=`$with_mysql_config --include 2>/dev/null`
  if test $? -ne 0; then
    with_libmysql="no ($with_mysql_config failed)"
  else
    SAVE_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $with_mysql_cflags"

    AC_CHECK_HEADERS([mysql.h],
      [have_mysql_h="yes"],
      [have_mysql_h="no"]
    )

    if test "x$have_mysql_h" != "xyes"; then
      AC_CHECK_HEADERS([mysql/mysql.h],
        [have_mysql_mysql_h="yes"],
        [],
      )
    fi

    if test "x$have_mysql_h" != "xyes" && test "x$have_mysql_mysql_h" != "xyes"; then
      with_libmysql="no (mysql.h not found)"
    fi

    CPPFLAGS="$SAVE_CPPFLAGS"
  fi
fi

if test "x$with_libmysql" = "xyes"; then
  with_mysql_libs=`$with_mysql_config --libs_r 2>/dev/null`
  if test $? -ne 0; then
    with_libmysql="no ($with_mysql_config failed)"
  else
    SAVE_LIBS="$LIBS"
    LIBS="$with_mysql_libs"

    AC_SEARCH_LIBS([mysql_get_server_version],
      [],
      [with_libmysql="yes"],
      [with_libmysql="no (symbol 'mysql_get_server_version' not found in ${LIBS})"],
      []
    )
    LIBS="$SAVE_LIBS"
  fi
fi

if test "x$with_libmysql" = "xyes"; then
  BUILD_WITH_LIBMYSQL_CFLAGS="$with_mysql_cflags"
  BUILD_WITH_LIBMYSQL_LIBS="$with_mysql_libs"
fi

AC_SUBST([BUILD_WITH_LIBMYSQL_CFLAGS])
AC_SUBST([BUILD_WITH_LIBMYSQL_LIBS])
# }}}

# --with-libmnl {{{
AC_ARG_WITH([libmnl],
  [AS_HELP_STRING([--with-libmnl@<:@=PREFIX@:>@], [Path to libmnl.])],
  [
    if test "x$withval" = "xyes"; then
      with_libmnl="yes"
     else if test "x$withval" = "xno"; then
       with_libmnl="no"
     else
       if test -d "$withval/include"; then
         with_libmnl_cflags="-I$withval/include"
         with_libmnl_libs="-L$withval/lib -lmnl"
         with_libmnl="yes"
       else
         AC_MSG_ERROR("no such directory: $withval/include")
       fi
     fi; fi
  ],
  [
    if test "x$ac_system" = "xLinux"; then
      with_libmnl="yes"
    else
      with_libmnl="no (Linux only library)"
    fi
  ]
)

if test "x$with_libmnl" = "xyes"; then
  if $PKG_CONFIG --exists libmnl 2>/dev/null; then
    with_libmnl_cflags="$with_libmnl_ldflags `$PKG_CONFIG --cflags libmnl`"
    with_libmnl_libs="$with_libmnl_libs `$PKG_CONFIG --libs libmnl`"
  fi

  AC_CHECK_HEADERS([libmnl.h libmnl/libmnl.h],
    [
      with_libmnl="yes"
      break
    ],
    [],
    [[
      #include <stdio.h>
      #include <sys/types.h>
      #include <asm/types.h>
      #include <sys/socket.h>
      #include <linux/netlink.h>
      #include <linux/rtnetlink.h>]]
  )

  AC_CHECK_HEADERS([linux/gen_stats.h linux/pkt_sched.h],
    [],
    [],
    [[
      #include <stdio.h>
      #include <sys/types.h>
      #include <asm/types.h>
      #include <sys/socket.h>
    ]]
  )

  AC_COMPILE_IFELSE(
    [
      AC_LANG_PROGRAM(
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
          return retval;
        ]]
      )
    ],
    [AC_DEFINE([HAVE_TCA_STATS2], [1], [True if the enum-member TCA_STATS2 exists])]
  )

  AC_COMPILE_IFELSE(
    [
      AC_LANG_PROGRAM(
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
          return retval;
        ]]
      )
    ],
    [AC_DEFINE([HAVE_TCA_STATS], 1, [True if the enum-member TCA_STATS exists])]
  )

  AC_CHECK_MEMBERS([struct rtnl_link_stats64.tx_window_errors],
    [AC_DEFINE(HAVE_RTNL_LINK_STATS64, 1, [Define if struct rtnl_link_stats64 exists and is usable.])],
    [],
    [[#include <linux/if_link.h>]]
  )

  AC_CHECK_LIB([mnl], [mnl_nlmsg_get_payload],
    [with_libmnl="yes"],
    [with_libmnl="no (symbol 'mnl_nlmsg_get_payload' not found)"],
    [$with_libmnl_libs]
  )
fi

if test "x$with_libmnl" = "xyes"; then
  BUILD_WITH_LIBMNL_CFLAGS="$with_libmnl_cflags"
  BUILD_WITH_LIBMNL_LIBS="$with_libmnl_libs"
fi
AC_SUBST([BUILD_WITH_LIBMNL_CFLAGS])
AC_SUBST([BUILD_WITH_LIBMNL_LIBS])
AM_CONDITIONAL([HAVE_LIBMNL], [test "x$with_libmnl" = "xyes"])
# }}}

# --with-libnetapp {{{
AC_ARG_VAR([LIBNETAPP_CPPFLAGS], [C preprocessor flags required to build with libnetapp])
AC_ARG_VAR([LIBNETAPP_LDFLAGS],  [Linker flags required to build with libnetapp])
AC_ARG_VAR([LIBNETAPP_LIBS],     [Other libraries required to link against libnetapp])
AC_ARG_WITH([libnetapp],
  [AS_HELP_STRING([--with-libnetapp@<:@=PREFIX@:>@], [Path to libnetapp.])],
  [
   if test -d "$withval"; then
     LIBNETAPP_CPPFLAGS="$LIBNETAPP_CPPFLAGS -I$withval/include"
     LIBNETAPP_LDFLAGS="$LIBNETAPP_LDFLAGS -L$withval/lib"
     with_libnetapp="yes"
   else
     with_libnetapp="$withval"
   fi
  ],
  [with_libnetapp="yes"]
)

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"
CPPFLAGS="$CPPFLAGS $LIBNETAPP_CPPFLAGS"
LDFLAGS="$LDFLAGS $LIBNETAPP_LDFLAGS"

if test "x$with_libnetapp" = "xyes"; then
  if test "x$LIBNETAPP_CPPFLAGS" != "x"; then
    AC_MSG_NOTICE([netapp CPPFLAGS: $LIBNETAPP_CPPFLAGS])
  fi
  AC_CHECK_HEADERS([netapp_api.h],
    [with_libnetapp="yes"],
    [with_libnetapp="no (netapp_api.h not found)"]
  )
fi

if test "x$with_libnetapp" = "xyes"; then
  if test "x$LIBNETAPP_LDFLAGS" != "x"; then
    AC_MSG_NOTICE([netapp LDFLAGS: $LIBNETAPP_LDFLAGS])
  fi

  if test "x$LIBNETAPP_LIBS" = "x"; then
    LIBNETAPP_LIBS="$PTHREAD_LIBS -lxml -ladt -lssl -lm -lcrypto -lz"
  fi

  AC_MSG_NOTICE([netapp LIBS: $LIBNETAPP_LIBS])

  AC_CHECK_LIB([netapp], [na_server_invoke_elem],
    [with_libnetapp="yes"],
    [with_libnetapp="no (symbol na_server_invoke_elem not found)"],
    [$LIBNETAPP_LIBS]
  )

  LIBNETAPP_LIBS="-lnetapp $LIBNETAPP_LIBS"
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

AC_SUBST([LIBNETAPP_CPPFLAGS])
AC_SUBST([LIBNETAPP_LDFLAGS])
AC_SUBST([LIBNETAPP_LIBS])
# }}}

# --with-libnetsnmp {{{
AC_ARG_WITH([libnetsnmp],
  [AS_HELP_STRING([--with-libnetsnmp@<:@=PREFIX@:>@], [Path to libnetsnmp.])],
  [
    if test "x$withval" = "xno"; then
     with_libnetsnmp="no"
    else if test "x$withval" = "xyes"; then
     with_libnetsnmp="yes"
    else
      with_libnetsnmp_cppflags="-I$withval/include"
      with_libnetsnmp_ldflags="-I$withval/lib"
      with_libnetsnmp="yes"
    fi; fi
  ],
  [with_libnetsnmp="yes"]
)

if test "x$with_libnetsnmp" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libnetsnmp_cppflags"

  AC_CHECK_HEADERS([net-snmp/net-snmp-config.h],
    [with_libnetsnmp="yes"],
    [with_libnetsnmp="no (net-snmp/net-snmp-config.h not found)"]
  )

  AC_CHECK_HEADERS([net-snmp/net-snmp-includes.h],
    [with_libnetsnmp="yes"],
    [with_libnetsnmp="no (net-snmp/net-snmp-includes.h not found)"],
    [[
      #if HAVE_NET_SNMP_NET_SNMP_CONFIG_H
      # include <net-snmp/net-snmp-config.h>
      #endif
    ]]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libnetsnmp" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libnetsnmp_ldflags"

  AC_CHECK_LIB([netsnmp], [init_snmp],
    [with_libnetsmp="yes"],
    [with_libnetsnmp="no (libnetsnmp not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libnetsnmp" = "xyes"; then
  BUILD_WITH_LIBNETSNMP_CPPFLAGS="$with_libnetsnmp_cppflags"
  BUILD_WITH_LIBNETSNMP_LDFLAGS="$with_libnetsnmp_ldflags"
  BUILD_WITH_LIBNETSNMP_LIBS="-lnetsnmp"
fi

AC_SUBST([BUILD_WITH_LIBNETSNMP_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBNETSNMP_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBNETSNMP_LIBS])
# }}}

# --with-libnetsmpagent {{{
AC_ARG_WITH([libnetsnmpagent],
  [AS_HELP_STRING([--with-libnetsnmpagent@<:@=PREFIX@:>@], [Path to libnetsnmpagent.])],
  [
    if test "x$withval" = "xno"; then
      with_libnetsnmpagent="no"
    else if test "x$withval" = "xyes"; then
      with_libnetsnmpagent="yes"
    else
      with_libnetsnmpagent_cppflags="-I$withval/include"
      with_libnetsnmpagent_ldflags="-I$withval/lib"
      with_libnetsnmpagent="yes"
    fi; fi
  ],
  [with_libnetsnmpagent="yes"]
)

if test "x$with_libnetsnmpagent" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libnetsnmpagent_cppflags"

  AC_CHECK_HEADERS([net-snmp/agent/net-snmp-agent-includes.h],
    [],
    [with_libnetsnmpagent="no (net-snmp/agent/net-snmp-agent-includes.h not found)"],
    [[
      #if HAVE_NET_SNMP_NET_SNMP_CONFIG_H
      # include <net-snmp/net-snmp-config.h>
      #endif
      #if HAVE_NET_SNMP_NET_SNMP_INCLUDES_H
      # include <net-snmp/net-snmp-includes.h>
      #endif
    ]]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libnetsnmpagent" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libnetsnmpagent_ldflags"

  # older versions of libnetsnmpagent fail to link
  # against the helpers library, so do that explicitly
  AC_CHECK_LIB([netsnmphelpers], [netsnmp_init_helpers],
    [libnetsnmphelpers="-lnetsnmphelpers"],
    [libnetsnmphelpers=""]
  )

  AC_CHECK_LIB([netsnmpagent], [init_agent],
    [with_libnetsnmpagent="yes"],
    [with_libnetsnmpagent="no (libnetsnmpagent not found)"],
    [$libnetsnmphelpers]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libnetsnmpagent" = "xyes"; then
  BUILD_WITH_LIBNETSNMPAGENT_LIBS="-lnetsnmpagent $libnetsnmphelpers"
fi

AC_SUBST([BUILD_WITH_LIBNETSNMPAGENT_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBNETSNMPAGENT_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBNETSNMPAGENT_LIBS])
# }}}

# --with-liboping {{{
AC_ARG_WITH([liboping],
  [AS_HELP_STRING([--with-liboping@<:@=PREFIX@:>@], [Path to liboping.])],
  [
    if test "x$withval" = "xyes"; then
      with_liboping="yes"
    else if test "x$withval" = "xno"; then
      with_liboping="no"
    else
      with_liboping="yes"
      LIBOPING_CPPFLAGS="-I$withval/include"
      LIBOPING_LDFLAGS="-L$withval/lib"
    fi; fi
  ],
  [with_liboping="yes"]
)

SAVE_CPPFLAGS="$CPPFLAGS"
SAVE_LDFLAGS="$LDFLAGS"
CPPFLAGS="$CPPFLAGS $LIBOPING_CPPFLAGS"
LDFLAGS="$LDFLAGS $LIBOPING_LDFLAGS"

if test "x$with_liboping" = "xyes"; then
  AC_CHECK_HEADERS([oping.h],
    [with_liboping="yes"],
    [with_liboping="no (oping.h not found)"]
  )
fi

if test "x$with_liboping" = "xyes"; then
  AC_CHECK_LIB([oping], [ping_construct],
    [with_liboping="yes"],
    [with_liboping="no (symbol 'ping_construct' not found)"]
  )
fi

CPPFLAGS="$SAVE_CPPFLAGS"
LDFLAGS="$SAVE_LDFLAGS"

if test "x$with_liboping" = "xyes"; then
  BUILD_WITH_LIBOPING_CPPFLAGS="$LIBOPING_CPPFLAGS"
  BUILD_WITH_LIBOPING_LDFLAGS="$LIBOPING_LDFLAGS"
fi

AC_SUBST([BUILD_WITH_LIBOPING_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBOPING_LDFLAGS])
# }}}

# --with-oracle {{{
AC_ARG_WITH([oracle],
  [AS_HELP_STRING([--with-oracle@<:@=ORACLE_HOME@:>@], [Path to Oracle.])],
  [
    if test "x$withval" = "xyes"; then
      if test "x$ORACLE_HOME" = "x"; then
        AC_MSG_WARN([Use of the Oracle library has been forced, but the environment variable ORACLE_HOME is not set.])
      fi
      with_oracle="yes"
    else if test "x$withval" = "xno"; then
      with_oracle="no"
    else
      with_oracle="yes"
      ORACLE_HOME="$withval"
    fi; fi
  ],
  [
    if test "x$ORACLE_HOME" = "x"; then
      with_oracle="no (ORACLE_HOME is not set)"
    else
      with_oracle="yes"
    fi
  ]
)

if test "x$ORACLE_HOME" != "x"; then
  with_oracle_cppflags="-I$ORACLE_HOME/rdbms/public"
  if test -e "$ORACLE_HOME/lib/ldflags"; then
    with_oracle_libs=`cat "$ORACLE_HOME/lib/ldflags"`
  fi
  with_oracle_libs="-L$ORACLE_HOME/lib -lclntsh"
fi

if test "x$with_oracle" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_oracle_cppflags"

  AC_CHECK_HEADERS([oci.h],
    [with_oracle="yes"],
    [with_oracle="no (oci.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_oracle" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  SAVE_LIBS="$LIBS"
  CPPFLAGS="$CPPFLAGS $with_oracle_cppflags"
  LIBS="$LIBS $with_oracle_libs"

  AC_CHECK_FUNC([OCIEnvCreate],
    [with_oracle="yes"],
    [with_oracle="no (Symbol 'OCIEnvCreate' not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
  LIBS="$SAVE_LIBS"
fi

if test "x$with_oracle" = "xyes"; then
  BUILD_WITH_ORACLE_CPPFLAGS="$with_oracle_cppflags"
  BUILD_WITH_ORACLE_LIBS="$with_oracle_libs"
fi

AC_SUBST([BUILD_WITH_ORACLE_CPPFLAGS])
AC_SUBST([BUILD_WITH_ORACLE_LIBS])
# }}}

# --with-libowcapi {{{
AC_ARG_WITH([libowcapi],
  [AS_HELP_STRING([--with-libowcapi@<:@=PREFIX@:>@], [Path to libowcapi.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libowcapi_cppflags="-I$withval/include"
      with_libowcapi_ldflags="-L$withval/lib"
      with_libowcapi="yes"
    else
      with_libowcapi="$withval"
    fi
  ],
  [with_libowcapi="yes"]
)

if test "x$with_libowcapi" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libowcapi_cppflags"

  AC_CHECK_HEADERS([owcapi.h],
    [with_libowcapi="yes"],
    [with_libowcapi="no (owcapi.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libowcapi" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libowcapi_ldflags"

  AC_CHECK_LIB([owcapi], [OW_get],
    [with_libowcapi="yes"],
    [with_libowcapi="no (libowcapi not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libowcapi" = "xyes"; then
  BUILD_WITH_LIBOWCAPI_CPPFLAGS="$with_libowcapi_cppflags"
  BUILD_WITH_LIBOWCAPI_LDFLAGS="$with_libowcapi_ldflags"
  BUILD_WITH_LIBOWCAPI_LIBS="-lowcapi"
fi

AC_SUBST([BUILD_WITH_LIBOWCAPI_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBOWCAPI_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBOWCAPI_LIBS])
# }}}

# --with-libpcap {{{
AC_ARG_WITH([libpcap],
  [AS_HELP_STRING([--with-libpcap@<:@=PREFIX@:>@], [Path to libpcap.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libpcap_cppflags="-I$withval/include"
      with_libpcap_ldflags="$LDFLAGS -L$withval/lib"
      with_libpcap="yes"
    else
      with_libpcap="$withval"
    fi
  ],
  [with_libpcap="yes"]
)

if test "x$with_libpcap" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libpcap_cppflags"

  AC_CHECK_HEADERS([pcap.h],
    [with_libpcap="yes"],
    [with_libpcap="no (pcap.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libpcap" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libpcap_ldflags"

  AC_CHECK_LIB([pcap], [pcap_open_live],
    [with_libpcap="yes"],
    [with_libpcap="no (libpcap not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libpcap" = "xyes"; then
  AC_CACHE_CHECK([whether libpcap has PCAP_ERROR_IFACE_NOT_UP],
    [c_cv_libpcap_have_pcap_error_iface_not_up],
    [
      AC_COMPILE_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[#include <pcap.h>]],
            [[
              int val = PCAP_ERROR_IFACE_NOT_UP;
              return(val);
            ]]
          )
        ],
        [c_cv_libpcap_have_pcap_error_iface_not_up="yes"],
        [c_cv_libpcap_have_pcap_error_iface_not_up="no"]
      )
    ]
  )
fi

if test "x$c_cv_libpcap_have_pcap_error_iface_not_up" != "xyes"; then
  with_libpcap="no (pcap.h misses PCAP_ERROR_IFACE_NOT_UP)"
fi

if test "x$with_libpcap" = "xyes"; then
  BUILD_WITH_LIBPCAP_CPPFLAGS="$with_libpcap_cppflags"
  BUILD_WITH_LIBPCAP_LDFLAGS="$with_libpcap_ldflags"
  BUILD_WITH_LIBPCAP_LIBS="-lpcap"
fi

AC_SUBST([BUILD_WITH_LIBPCAP_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBPCAP_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBPCAP_LIBS])
# }}}

# --with-libperl {{{
AC_ARG_WITH([libperl],
  [AS_HELP_STRING([--with-libperl@<:@=PREFIX@:>@], [Path to libperl.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      LDFLAGS="$LDFLAGS -L$withval/lib"
      CPPFLAGS="$CPPFLAGS -I$withval/include"
      with_libperl="yes"
    else
      with_libperl="$withval"
    fi
  ],
  [with_libperl="yes"]
)

AC_ARG_VAR([PERL], [path to Perl interpreter])
AC_PATH_PROG([PERL], [perl])

if test "x$PERL" = "x"; then
  with_libperl="no (no Perl interpreter found)"
fi

if test "x$with_libperl" = "xyes"; then
  SAVE_CFLAGS="$CFLAGS"
  SAVE_LIBS="$LIBS"
  dnl ARCHFLAGS="" -> disable multi -arch on OSX (see Config_heavy.pl:fetch_string)
  PERL_CFLAGS=`ARCHFLAGS="" $PERL -MExtUtils::Embed -e perl_inc`
  PERL_LIBS=`ARCHFLAGS="" $PERL -MExtUtils::Embed -e ldopts`
  CFLAGS="$CFLAGS $PERL_CFLAGS"
  LIBS="$LIBS $PERL_LIBS"

  AC_CACHE_CHECK([for libperl],
    [c_cv_have_libperl],
    [
      AC_LINK_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
              #define PERL_NO_GET_CONTEXT
              #include <EXTERN.h>
              #include <perl.h>
              #include <XSUB.h>
            ]],
            [[
              dTHX;
              load_module (PERL_LOADMOD_NOIMPORT,
                newSVpv ("Collectd::Plugin::FooBar", 24),
                Nullsv);
            ]]
          )
        ],
        [c_cv_have_libperl="yes"],
        [c_cv_have_libperl="no"]
      )
    ]
  )

  CFLAGS="$SAVE_CFLAGS"
  LIBS="$SAVE_LIBS"

  if test "x$c_cv_have_libperl" = "xno"; then
    with_libperl="no"
  fi
fi

if test "x$with_libperl" = "xyes"; then
  SAVE_CFLAGS="$CFLAGS"
  SAVE_LIBS="$LIBS"
  CFLAGS="$CFLAGS $PERL_CFLAGS"
  LIBS="$LIBS $PERL_LIBS"

  AC_CACHE_CHECK([if Perl supports ithreads],
    [c_cv_have_perl_ithreads],
    [
      AC_LINK_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
              #include <EXTERN.h>
              #include <perl.h>
              #include <XSUB.h>

              #if !defined(USE_ITHREADS)
              # error "Perl does not support ithreads!"
              #endif /* !defined(USE_ITHREADS) */
            ]],
            []
          )
        ],
        [c_cv_have_perl_ithreads="yes"],
        [c_cv_have_perl_ithreads="no"]
      )
    ]
  )

  CFLAGS="$SAVE_CFLAGS"
  LIBS="$SAVE_LIBS"
fi

if test "x$with_libperl" = "xyes"; then
  # trigger an error if Perl_load_module*() uses __attribute__nonnull__(3)
  # (see issues #41 and #42)
  SAVE_CFLAGS="$CFLAGS"
  SAVE_LIBS="$LIBS"
  CFLAGS="$CFLAGS $PERL_CFLAGS -Wall -Werror"
  LIBS="$LIBS $PERL_LIBS"

  AC_CACHE_CHECK([for broken Perl_load_module()],
    [c_cv_have_broken_perl_load_module],
    [
      AC_LINK_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
              #define PERL_NO_GET_CONTEXT
              #include <EXTERN.h>
              #include <perl.h>
              #include <XSUB.h>
            ]],
            [[
              dTHX;
              load_module (PERL_LOADMOD_NOIMPORT,
                newSVpv ("Collectd::Plugin::FooBar", 24),
                Nullsv);
            ]]
          )
        ],
        [c_cv_have_broken_perl_load_module="no"],
        [c_cv_have_broken_perl_load_module="yes"]
      )
    ]
  )

  CFLAGS="$SAVE_CFLAGS"
  LIBS="$SAVE_LIBS"
fi

if test "x$c_cv_have_broken_perl_load_module" = "xyes"; then
  PERL_CFLAGS="$PERL_CFLAGS -Wno-nonnull"
fi

if test "x$with_libperl" = "xyes"; then
  SAVE_CFLAGS="$CFLAGS"
  SAVE_LIBS="$LIBS"
  CFLAGS="$CFLAGS $PERL_CFLAGS"
  LIBS="$LIBS $PERL_LIBS"

  AC_CHECK_MEMBER(
    [struct mgvtbl.svt_local],
    [have_struct_mgvtbl_svt_local="yes"],
    [have_struct_mgvtbl_svt_local="no"],
    [[
      #include <EXTERN.h>
      #include <perl.h>
      #include <XSUB.h>
    ]]
  )

  if test "x$have_struct_mgvtbl_svt_local" = "xyes"; then
    AC_DEFINE([HAVE_PERL_STRUCT_MGVTBL_SVT_LOCAL], [1], [Define if Perls struct mgvtbl has member svt_local.])
  fi

  CFLAGS="$SAVE_CFLAGS"
  LIBS="$SAVE_LIBS"
fi
AC_SUBST([PERL_CFLAGS])
AC_SUBST([PERL_LIBS])

# }}}

# --with-libpq {{{
with_pg_config="pg_config"
AC_ARG_WITH([libpq],
  [AS_HELP_STRING([--with-libpq@<:@=PREFIX@:>@], [Path to libpq.])],
  [
    if test "x$withval" = "xno" || test "x$withval" = "xyes"; then
      with_libpq="$withval"
    else
      if test -f "$withval" && test -x "$withval"; then
        with_pg_config="$withval"
      else if test -x "$withval/bin/pg_config"; then
        with_pg_config="$withval/bin/pg_config"
      fi; fi
      with_libpq="yes"
    fi
  ],
  [with_libpq="yes"]
)

if test "x$with_libpq" = "xyes"; then
  with_libpq_includedir=`$with_pg_config --includedir 2> /dev/null`
  pg_config_status=$?

  if test $pg_config_status -eq 0; then
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

  AC_CHECK_HEADERS([libpq-fe.h],
    [with_libpq="yes"],
    [with_libpq="no (libpq-fe.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libpq" = "xyes"; then
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

  AC_CHECK_LIB([pq], [PQserverVersion],
    [with_libpq="yes"],
    [with_libpq="no (symbol 'PQserverVersion' not found)"])

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libpq" = "xyes"; then
  BUILD_WITH_LIBPQ_CPPFLAGS="$with_libpq_cppflags"
  BUILD_WITH_LIBPQ_LDFLAGS="$with_libpq_ldflags"
  BUILD_WITH_LIBPQ_LIBS="-lpq"
fi

AC_SUBST([BUILD_WITH_LIBPQ_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBPQ_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBPQ_LIBS])
# }}}

# --with-libpqos {{{
AC_ARG_WITH([libpqos],
  [AS_HELP_STRING([--with-libpqos@<:@=PREFIX@:>@], [Path to libpqos.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libpqos_cppflags="-I$withval/include"
      with_libpqos_ldflags="-L$withval/lib"
      with_libpqos="yes"
    else
      with_libpqos="$withval"
    fi
  ],
  [with_libpqos="yes"]
)

if test "x$with_libpqos" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libpqos_cppflags"

  AC_CHECK_HEADERS([pqos.h],
    [with_libpqos="yes"],
    [with_libpqos="no (pqos.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libpqos" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libpqos_ldflags"

  AC_CHECK_LIB([pqos], [pqos_init],
    [with_libpqos="yes"],
    [with_libpqos="no (Can't find libpqos)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libpqos" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libpqos_cppflags"
  AC_PREPROC_IFELSE(
    [
      AC_LANG_SOURCE(
        [[
          #include <pqos.h>
          #if PQOS_VERSION < 106
          #error "required PQOS version >= 1.06"
          #endif
        ]]
      )
    ],
    [with_libpqos="yes"],
    [with_libpqos="no (pqos library version 1.06 or higher is required)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libpqos" = "xyes"; then
  BUILD_WITH_LIBPQOS_CPPFLAGS="$with_libpqos_cppflags"
  BUILD_WITH_LIBPQOS_LDFLAGS="$with_libpqos_ldflags"
  BUILD_WITH_LIBPQOS_LIBS="-lpqos"
fi

AC_SUBST([BUILD_WITH_LIBPQOS_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBPQOS_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBPQOS_LIBS])
# }}}

# --with-libjevents {{{
with_libjevents_cppflags=""
with_libjevents_ldflags=""
AC_ARG_WITH([libjevents],
  [AS_HELP_STRING([--with-libjevents@<:@=PREFIX@:>@], [Path to libjevents.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libjevents_cppflags="-I$withval/include"
      with_libjevents_ldflags="-L$withval/lib"
      with_libjevents="yes"
    else
      with_libjevents="$withval"
    fi
  ],
  [with_libjevents="yes"]
)

if test "x$with_libjevents" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libjevents_cppflags"

  AC_CHECK_HEADERS([jevents.h], [with_libjevents="yes"], [with_libjevents="no (jevents.h not found)"])

  CPPFLAGS="$SAVE_CPPFLAGS"
fi
if test "x$with_libjevents" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libjevents_ldflags"

  AC_CHECK_LIB([jevents], [json_events], [with_libjevents="yes"], [with_libjevents="no (Can't find libjevents)"])

  LDFLAGS="$SAVE_LDFLAGS"
fi
if test "x$with_libjevents" = "xyes"; then
  BUILD_WITH_LIBJEVENTS_CPPFLAGS="$with_libjevents_cppflags"
  BUILD_WITH_LIBJEVENTS_LDFLAGS="$with_libjevents_ldflags"
  BUILD_WITH_LIBJEVENTS_LIBS="-ljevents"
fi
AC_SUBST([BUILD_WITH_LIBJEVENTS_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBJEVENTS_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBJEVENTS_LIBS])
# }}}

# --with-libprotobuf {{{
with_libprotobuf_cppflags=""
with_libprotobuf_ldflags=""
AC_ARG_WITH([libprotobuf],
  [AS_HELP_STRING([--with-libprotobuf@<:@=PREFIX@:>@], [Path to libprotobuf.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libprotobuf_cppflags="-I$withval/include"
      with_libprotobuf_ldflags="-L$withval/lib"
      with_libprotobuf="yes"
    fi
    if test "x$withval" = "xno"; then
      with_libprotobuf="no (disabled on command line)"
    fi
  ],
  [withval="yes"]
)

if test "x$withval" = "xyes"; then
  PKG_CHECK_MODULES([PROTOBUF], [protobuf],
    [with_libprotobuf="yes"],
    [with_libprotobuf="no (pkg-config could not find libprotobuf)"]
  )
fi

if test "x$withval" != "xno"; then
  SAVE_LDFLAGS="$LDFLAGS"
  SAVE_LIBS="$LIBS"
  LDFLAGS="$with_libprotobuf_ldflags"
  LIBS="$PROTOBUF_LIBS $LIBS"
  AC_LANG_PUSH([C++])
  AC_CHECK_LIB([protobuf], [main],
    [
      SAVE_CPPFLAGS="$CPPFLAGS"
      CPPFLAGS="$with_libprotobuf_cppflags $PROTOBUF_CFLAGS"
      if test "x$PROTOBUF_LIBS" = "x"
      then
        PROTOBUF_LIBS="-lprotobuf"
      fi
      AC_CHECK_HEADERS([google/protobuf/util/time_util.h],
        [with_libprotobuf="yes"],
        [with_libprotobuf="no (<google/protobuf/util/time_util.h> not found)"]
      )
      CPPFLAGS="$SAVE_CPPFLAGS"
    ],
    [with_libprotobuf="no (libprotobuf not found)"]
  )
  AC_LANG_POP([C++])
  LDFLAGS="$SAVE_LDFLAGS"
  LIBS="$SAVE_LIBS"
fi

BUILD_WITH_LIBPROTOBUF_CPPFLAGS="$with_libprotobuf_cppflags $PROTOBUF_CFLAGS"
BUILD_WITH_LIBPROTOBUF_LDFLAGS="$with_libprotobuf_ldflags"
BUILD_WITH_LIBPROTOBUF_LIBS="$PROTOBUF_LIBS"
AC_SUBST([BUILD_WITH_LIBPROTOBUF_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBPROTOBUF_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBPROTOBUF_LIBS])
# }}}

AC_ARG_VAR([PROTOC], [path to the protoc binary])
AC_PATH_PROG([PROTOC], [protoc])
have_protoc3="no"
if test "x$PROTOC" != "x"; then
  AC_MSG_CHECKING([for protoc 3.0.0+])
  if $PROTOC --version | $EGREP libprotoc.3 >/dev/null; then
    protoc3="yes (`$PROTOC --version`)"
    have_protoc3="yes"
  else
    protoc3="no (`$PROTOC --version`)"
  fi
  AC_MSG_RESULT([$protoc3])
fi
AM_CONDITIONAL([HAVE_PROTOC3], [test "x$have_protoc3" = "xyes"])

# --with-libprotobuf-c {{{
AC_ARG_WITH([libprotobuf-c],
  [AS_HELP_STRING([--with-libprotobuf-c@<:@=PREFIX@:>@], [Path to libprotobuf-c.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libprotobuf_c_cppflags="-I$withval/include"
      with_libprotobuf_c_ldflags="-L$withval/lib"
      with_libprotobuf_c="yes"
    fi

    if test "x$withval" = "xno"; then
      with_libprotobuf_c="no (disabled on command line)"
    fi
  ],
  [withval="yes"]
)

if test "x$withval" = "xyes"; then
  PKG_CHECK_MODULES([PROTOBUF_C], [libprotobuf-c],
    [with_libprotobuf_c="yes"],
    [with_libprotobuf_c="no (pkg-config could not find libprotobuf-c)"]
  )
fi

if test "x$withval" != "xno"; then
  SAVE_LDFLAGS="$LDFLAGS"
  SAVE_LIBS="$LIBS"
  LDFLAGS="$with_libprotobuf_c_ldflags"
  LIBS="$PROTOBUF_C_LIBS $LIBS"
  AC_CHECK_LIB([protobuf-c], [protobuf_c_message_pack],
    [
      SAVE_CPPFLAGS="$CPPFLAGS"
      CPPFLAGS="$with_libprotobuf_c_cppflags $PROTOBUF_C_CFLAGS"
      if test "x$PROTOBUF_C_LIBS" = "x"; then
        PROTOBUF_C_LIBS="-lprotobuf-c"
      fi

      AC_CHECK_HEADERS([protobuf-c/protobuf-c.h google/protobuf-c/protobuf-c.h],
        [
          with_libprotobuf_c="yes"
          break
        ],
        [with_libprotobuf_c="no (<protobuf-c.h> not found)"]
      )

      CPPFLAGS="$SAVE_CPPFLAGS"
    ],
    [with_libprotobuf_c="no (libprotobuf-c not found)"]
  )
  LDFLAGS="$SAVE_LDFLAGS"
  LIBS="$SAVE_LIBS"
fi

BUILD_WITH_LIBPROTOBUF_C_CPPFLAGS="$with_libprotobuf_c_cppflags $PROTOBUF_C_CFLAGS"
BUILD_WITH_LIBPROTOBUF_C_LDFLAGS="$with_libprotobuf_c_ldflags"
BUILD_WITH_LIBPROTOBUF_C_LIBS="$PROTOBUF_C_LIBS"
AC_SUBST([BUILD_WITH_LIBPROTOBUF_C_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBPROTOBUF_C_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBPROTOBUF_C_LIBS])
# }}}

AC_ARG_VAR([PROTOC_C], [path to the protoc-c binary])
AC_PATH_PROG([PROTOC_C], [protoc-c])
if test "x$PROTOC_C" = "x"
then
  have_protoc_c="no (protoc-c compiler not found)"
else
  have_protoc_c="yes"
fi

# --with-libpython {{{
AC_ARG_VAR([LIBPYTHON_CPPFLAGS], [Preprocessor flags for libpython])
AC_ARG_VAR([LIBPYTHON_LDFLAGS], [Linker flags for libpython])
AC_ARG_VAR([LIBPYTHON_LIBS], [Libraries for libpython])

AC_ARG_WITH([libpython],
  [AS_HELP_STRING([--with-libpython], [if we should build with libpython @<:@default=yes@:>@])],
  [with_libpython="$withval"],
  [with_libpython="check"]
)

if test "$with_libpython" != "no"; then
  if test "$LIBPYTHON_CPPFLAGS" = "" && test "$LIBPYTHON_LDFLAGS" = ""; then
    AC_ARG_VAR([PYTHON_CONFIG], [path to python-config])
    AC_PATH_PROGS([PYTHON_CONFIG],
      [python3-config python2-config python-config]
    )
    if test "$PYTHON_CONFIG" = ""; then
      if test "$with_libpython" = "yes"; then
        AC_MSG_ERROR([Unable to find python-config])
      fi
      with_libpython="no"
    fi
  fi
fi

if test "$PYTHON_CONFIG" != ""; then
  LIBPYTHON_CPPFLAGS="`${PYTHON_CONFIG} --includes`"
  if test $? -ne 0; then
    with_libpython="no"
  fi
  LIBPYTHON_LDFLAGS="`${PYTHON_CONFIG} --ldflags`"
  if test $? -ne 0; then
    with_libpython="no"
  fi
  LIBPYTHON_LIBS="`${PYTHON_CONFIG} --libs`"
  if test $? -ne 0; then
    with_libpython="no"
  fi
fi

if test "$with_libpython" != "xno"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  SAVE_LDFLAGS="$LDFLAGS"
  SAVE_LIBS="$LIBS"
  CPPFLAGS="$LIBPYTHON_CPPFLAGS $CPPFLAGS"
  LDFLAGS="$LIBPYTHON_LDFLAGS $LDFLAGS"
  LIBS="$LIBPYTHON_LIBS $LIBS"

  AC_CHECK_HEADERS([Python.h],
    [
      AC_MSG_CHECKING([for libpython])
      AC_LINK_IFELSE([AC_LANG_PROGRAM(
          [[#include <Python.h>]],
          [[Py_Initialize();]])
        ],
        [with_libpython="yes"],
        [with_libpython="no"]
      )
      AC_MSG_RESULT([$with_libpython])
    ],
    [with_libpython="no"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
  LDFLAGS="$SAVE_LDFLAGS"
  LIBS="$SAVE_LIBS"
fi
# }}} --with-libpython

# --with-librabbitmq {{{
AC_ARG_WITH([librabbitmq],
  [AS_HELP_STRING([--with-librabbitmq@<:@=PREFIX@:>@], [Path to librabbitmq.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_librabbitmq_cppflags="-I$withval/include"
      with_librabbitmq_ldflags="-L$withval/lib"
      with_librabbitmq="yes"
    else
      with_librabbitmq="$withval"
    fi
  ],
  [with_librabbitmq="yes"]
)

if test "x$with_librabbitmq" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_librabbitmq_cppflags"

  AC_CHECK_HEADERS([amqp.h],
    [with_librabbitmq="yes"],
    [with_librabbitmq="no (amqp.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_librabbitmq" = "xyes"; then
  # librabbitmq up to version 0.9.1 provides "library_errno", later
  # versions use "library_error". The library does not provide a version
  # macro :(.

  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_librabbitmq_cppflags"

  AC_CHECK_MEMBERS([amqp_rpc_reply_t.library_errno],
    [],
    [],
    [[
      #include <stdlib.h>
      #include <stdio.h>
      #include <stdint.h>
      #include <inttypes.h>
      #include <amqp.h>
    ]]
  )
  CPPFLAGS="$SAVE_CPPFLAGS"

  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_librabbitmq_ldflags"

  AC_CHECK_LIB([rabbitmq], [amqp_basic_publish],
    [with_librabbitmq="yes"],
    [with_librabbitmq="no (Symbol 'amqp_basic_publish' not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_librabbitmq" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  SAVE_LDFLAGS="$LDFLAGS"
  SAVE_LIBS="$LIBS"
  CPPFLAGS="$CPPFLAGS $with_librabbitmq_cppflags"
  LDFLAGS="$LDFLAGS $with_librabbitmq_ldflags"
  LIBS="-lrabbitmq"

  AC_CHECK_HEADERS([amqp_tcp_socket.h amqp_socket.h])
  AC_CHECK_FUNC([amqp_tcp_socket_new],
    [
      AC_DEFINE([HAVE_AMQP_TCP_SOCKET], [1],
        [Define if librabbitmq provides the new TCP socket interface.])
    ]
  )

  AC_CHECK_DECLS([amqp_socket_close],
    [],
    [],
    [[
      #include <amqp.h>
      #ifdef HAVE_AMQP_TCP_SOCKET_H
      # include <amqp_tcp_socket.h>
      #endif
      #ifdef HAVE_AMQP_SOCKET_H
      # include <amqp_socket.h>
      #endif
    ]]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
  LDFLAGS="$SAVE_LDFLAGS"
  LIBS="$SAVE_LIBS"
fi

if test "x$with_librabbitmq" = "xyes"; then
  BUILD_WITH_LIBRABBITMQ_CPPFLAGS="$with_librabbitmq_cppflags"
  BUILD_WITH_LIBRABBITMQ_LDFLAGS="$with_librabbitmq_ldflags"
  BUILD_WITH_LIBRABBITMQ_LIBS="-lrabbitmq"
fi

AC_SUBST(BUILD_WITH_LIBRABBITMQ_CPPFLAGS)
AC_SUBST(BUILD_WITH_LIBRABBITMQ_LDFLAGS)
AC_SUBST(BUILD_WITH_LIBRABBITMQ_LIBS)

# }}}

# --with-librdkafka {{{
AC_ARG_WITH([librdkafka],
  [AS_HELP_STRING([--with-librdkafka@<:@=PREFIX@:>@], [Path to librdkafka.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_librdkafka_cppflags="-I$withval/include"
      with_librdkafka_ldflags="-L$withval/lib"
      with_librdkafka_rpath="$withval/lib"
      with_librdkafka="yes"
    else
      with_librdkafka="$withval"
    fi
  ],
  [with_librdkafka="yes"]
)

if test "x$with_librdkafka" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_librdkafka_cppflags"

  AC_CHECK_HEADERS([librdkafka/rdkafka.h],
    [with_librdkafka="yes"],
    [with_librdkafka="no (librdkafka/rdkafka.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_librdkafka" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_librdkafka_ldflags"

  AC_CHECK_LIB([rdkafka], [rd_kafka_new],
    [with_librdkafka="yes"],
    [with_librdkafka="no (Symbol 'rd_kafka_new' not found)"])

  AC_CHECK_LIB([rdkafka], [rd_kafka_conf_set_log_cb],
    [with_librdkafka_log_cb="yes"],
    [with_librdkafka_log_cb="no"])

  AC_CHECK_LIB([rdkafka], [rd_kafka_set_logger],
    [with_librdkafka_logger="yes"],
    [with_librdkafka_logger="no"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_librdkafka" = "xyes"; then
  BUILD_WITH_LIBRDKAFKA_CPPFLAGS="$with_librdkafka_cppflags"
  BUILD_WITH_LIBRDKAFKA_LDFLAGS="$with_librdkafka_ldflags"

  if test "x$with_librdkafka_rpath" != "x"; then
    BUILD_WITH_LIBRDKAFKA_LIBS="-Wl,-rpath,$with_librdkafka_rpath -lrdkafka"
  else
    BUILD_WITH_LIBRDKAFKA_LIBS="-lrdkafka"
  fi

  if test "x$with_librdkafka_log_cb" = "xyes"; then
    AC_DEFINE(HAVE_LIBRDKAFKA_LOG_CB, 1, [Define if librdkafka log facility is present and usable.])
  else if test "x$with_librdkafka_logger" = "xyes"; then
    AC_DEFINE(HAVE_LIBRDKAFKA_LOGGER, 1, [Define if librdkafka log facility is present and usable.])
  fi; fi
fi

AC_SUBST([BUILD_WITH_LIBRDKAFKA_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBRDKAFKA_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBRDKAFKA_LIBS])
# }}}

# --with-librouteros {{{
AC_ARG_WITH([librouteros],
  [AS_HELP_STRING([--with-librouteros@<:@=PREFIX@:>@], [Path to librouteros.])],
  [
    if test "x$withval" = "xyes" || test "x$withval" = "xno"; then
      with_librouteros="$witval"
    else
      with_librouteros_cppflags="-I$withval/include"
      with_librouteros_ldflags="-L$withval/lib"
      with_librouteros="yes"
   fi
  ],
  [with_librouteros="yes"]
)

if test "x$with_librouteros" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_librouteros_cppflags"

  AC_CHECK_HEADERS([routeros_api.h],
    [with_librouteros="yes"],
    [with_librouteros="no (routeros_api.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_librouteros" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_librouteros_ldflags"

  AC_CHECK_LIB([routeros], [ros_interface],
    [with_librouteros="yes"],
    [with_librouteros="no (symbol 'ros_interface' not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_librouteros" = "xyes"; then
  BUILD_WITH_LIBROUTEROS_CPPFLAGS="$with_librouteros_cppflags"
  BUILD_WITH_LIBROUTEROS_LDFLAGS="$with_librouteros_ldflags"
fi

AC_SUBST([BUILD_WITH_LIBROUTEROS_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBROUTEROS_LDFLAGS])
# }}}

# --with-librrd {{{
librrd_threadsafe="no"
librrd_rrdc_update="no"
AC_ARG_WITH([librrd],
  [AS_HELP_STRING([--with-librrd@<:@=PREFIX@:>@], [Path to rrdtool.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      librrd_cflags="-I$withval/include"
      librrd_ldflags="-L$withval/lib"
      with_librrd="yes"
    else
      with_librrd="$withval"
    fi
  ],
  [with_librrd="yes"]
)

if test "x$with_librrd" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $librrd_ldflags"
  PKG_CHECK_MODULES([RRD], [librrd >= 1.6.0],
    [
      AC_CHECK_LIB([rrd], [rrd_update_r],
        [librrd_threadsafe="yes"],
        [:]
      )
      AC_CHECK_LIB([rrd], [rrdc_update],
        [librrd_rrdc_update="yes"],
        [:]
      )
    ],[:]
  )
  LDFLAGS="$SAVE_LDFLAGS"

  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $RRD_CFLAGS $librrd_cflags"

  AC_CHECK_HEADERS([rrd.h],, [with_librrd="no (rrd.h not found)"])

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_librrd" = "xyes" && test "x$librrd_threadsafe" = "xno"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $librrd_ldflags"

  AC_CHECK_LIB([rrd_th], [rrd_update_r],
    [
      librrd_ldflags="$librrd_ldflags -lrrd_th"
      librrd_threadsafe="yes"
      AC_CHECK_LIB([rrd_th], [rrdc_update],
        [librrd_rrdc_update="yes"],
        [:],
      )
    ],
    [:]
  )
  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_librrd" = "xyes" && test "x$librrd_threadsafe" = "xno"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $librrd_ldflags"

  AC_CHECK_LIB([rrd], [rrd_update],
    [
      librrd_ldflags="$librrd_ldflags -lrrd"
      AC_CHECK_LIB([rrd], [rrdc_update],
        [librrd_rrdc_update="yes"],
        [:]
      )
    ],
    [with_librrd="no (symbol 'rrd_update' not found)"]
  )
  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_librrd" = "xyes"; then
  BUILD_WITH_LIBRRD_CFLAGS="$RRD_CFLAGS $librrd_cflags"
  BUILD_WITH_LIBRRD_LDFLAGS="$librrd_ldflags"
  BUILD_WITH_LIBRRD_LIBS="$RRD_LIBS"
fi

if test "x$librrd_threadsafe" = "xyes"; then
  AC_DEFINE([HAVE_THREADSAFE_LIBRRD], [1],
    [Define to 1 if the rrd library is thread-safe]
  )
fi

AC_SUBST([BUILD_WITH_LIBRRD_CFLAGS])
AC_SUBST([BUILD_WITH_LIBRRD_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBRRD_LIBS])
# }}}

# --with-libsensors {{{
AC_ARG_WITH([libsensors],
  [AS_HELP_STRING([--with-libsensors@<:@=PREFIX@:>@], [Path to lm_sensors.])],
  [
    if test "x$withval" = "xno" || test "x$withval" = "xyes"; then
      with_libsensors="$withval"
    else
      with_sensors_cppflags="-I$withval/include"
      with_sensors_ldflags="-L$withval/lib"
      with_libsensors="yes"
    fi
  ],
  [
    if test "x$ac_system" = "xLinux"; then
      with_libsensors="yes"
    else
      with_libsensors="no (Linux only library)"
    fi
  ]
)

if test "x$with_libsensors" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_sensors_cppflags"

  AC_CHECK_HEADERS([sensors/sensors.h],
    [with_libsensors="yes"],
    [with_libsensors="no (sensors/sensors.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libsensors" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_sensors_ldflags"

  AC_CHECK_LIB([sensors], [sensors_init],
    [with_libsensors="yes"],
    [with_libsensors="no (libsensors not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libsensors" = "xyes"; then
  BUILD_WITH_LIBSENSORS_CPPFLAGS="$with_sensors_cppflags"
  BUILD_WITH_LIBSENSORS_LDFLAGS="$with_sensors_ldflags"
  BUILD_WITH_LIBSENSORS_LIBS="-lsensors"
fi

AC_SUBST([BUILD_WITH_LIBSENSORS_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBSENSORS_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBSENSORS_LIBS])

# }}}

# libsigrok {{{
AC_SUBST([LIBSIGROK_CFLAGS])
AC_SUBST([LIBSIGROK_LIBS])
PKG_CHECK_MODULES([LIBSIGROK], [libsigrok < 0.4],
  [with_libsigrok="yes"],
  [with_libsigrok="no (pkg-config could not find libsigrok)"]
)
# }}}

# --with-libstatgrab {{{
AC_ARG_WITH([libstatgrab],
  [AS_HELP_STRING([--with-libstatgrab@<:@=PREFIX@:>@], [Path to libstatgrab.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
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

if test "x$with_libstatgrab" = "xyes" && test "x$with_libstatgrab_pkg_config" = "xyes"; then
  AC_MSG_CHECKING([pkg-config for libstatgrab])
  temp_result="found"
  $PKG_CONFIG --exists libstatgrab 2>/dev/null
  if test "$?" != "0"; then
    with_libstatgrab_pkg_config="no"
    with_libstatgrab="no (pkg-config doesn't know libstatgrab)"
    temp_result="not found"
  fi
  AC_MSG_RESULT([$temp_result])
fi

if test "x$with_libstatgrab" = "xyes" && test "x$with_libstatgrab_pkg_config" = "xyes" && test "x$with_libstatgrab_cflags" = "x"; then
  AC_MSG_CHECKING([for libstatgrab CFLAGS])
  temp_result="`$PKG_CONFIG --cflags libstatgrab`"
  if test "$?" = "0"; then
    with_libstatgrab_cflags="$temp_result"
  else
    with_libstatgrab="no ($PKG_CONFIG --cflags libstatgrab failed)"
    temp_result="$PKG_CONFIG --cflags libstatgrab failed"
  fi
  AC_MSG_RESULT([$temp_result])
fi

if test "x$with_libstatgrab" = "xyes" && test "x$with_libstatgrab_pkg_config" = "xyes" && test "x$with_libstatgrab_ldflags" = "x"; then
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

if test "x$with_libstatgrab" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libstatgrab_cflags"

  AC_CHECK_HEADERS([statgrab.h],
    [with_libstatgrab="yes"],
    [with_libstatgrab="no (statgrab.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libstatgrab" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libstatgrab_ldflags"

  AC_CHECK_LIB([statgrab], [sg_init],
    [with_libstatgrab="yes"],
    [with_libstatgrab="no (symbol sg_init not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libstatgrab" = "xyes"; then
  SAVE_CFLAGS="$CFLAGS"
  SAVE_LDFLAGS="$LDFLAGS"
  SAVE_LIBS="$LIBS"

  CFLAGS="$CFLAGS $with_libstatgrab_cflags"
  LDFLAGS="$LDFLAGS $with_libstatgrab_ldflags"
  LIBS="-lstatgrab $LIBS"

  AC_CACHE_CHECK([if libstatgrab >= 0.90],
    [c_cv_have_libstatgrab_0_90],
    [
      AC_LINK_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
              #include <stdio.h>
              #include <statgrab.h>
            ]],
            [[
              if (sg_init()) return 0;
            ]]
          )
        ],
        [c_cv_have_libstatgrab_0_90="no"],
        [c_cv_have_libstatgrab_0_90="yes"]
      )
    ]
  )

  CFLAGS="$SAVE_CFLAGS"
  LDFLAGS="$SAVE_LDFLAGS"
  LIBS="$SAVE_LIBS"
fi

AM_CONDITIONAL([BUILD_WITH_LIBSTATGRAB], [test "x$with_libstatgrab" = "xyes"])

if test "x$with_libstatgrab" = "xyes"; then
  AC_DEFINE([HAVE_LIBSTATGRAB], [1],
    [Define to 1 if you have the 'statgrab' library (-lstatgrab)]
  )

  if test "x$c_cv_have_libstatgrab_0_90" = "xyes"; then
    AC_DEFINE([HAVE_LIBSTATGRAB_0_90], [1],
      [Define to 1 if libstatgrab version >= 0.90]
    )
  fi

  BUILD_WITH_LIBSTATGRAB_CFLAGS="$with_libstatgrab_cflags"
  BUILD_WITH_LIBSTATGRAB_LDFLAGS="$with_libstatgrab_ldflags"

fi

AC_SUBST([BUILD_WITH_LIBSTATGRAB_CFLAGS])
AC_SUBST([BUILD_WITH_LIBSTATGRAB_LDFLAGS])
# }}}

# --with-libtokyotyrant {{{
AC_ARG_WITH([libtokyotyrant],
  [AS_HELP_STRING([--with-libtokyotyrant@<:@=PREFIX@:>@], [Path to libtokyotyrant.])],
  [
    if test "x$withval" = "xno" || test "x$withval" = "xyes"; then
      with_libtokyotyrant="$withval"
    else
      with_libtokyotyrant_cppflags="-I$withval/include"
      with_libtokyotyrant_ldflags="-L$withval/include"
      with_libtokyotyrant_libs="-ltokyotyrant"
      with_libtokyotyrant="yes"
    fi
  ],
  [with_libtokyotyrant="yes"]
)

if test "x$with_libtokyotyrant" = "xyes"; then
  if $PKG_CONFIG --exists tokyotyrant; then
    with_libtokyotyrant_cppflags="$with_libtokyotyrant_cppflags `$PKG_CONFIG --cflags tokyotyrant`"
    with_libtokyotyrant_ldflags="$with_libtokyotyrant_ldflags `$PKG_CONFIG --libs-only-L tokyotyrant`"
    with_libtokyotyrant_libs="$with_libtokyotyrant_libs `$PKG_CONFIG --libs-only-l tokyotyrant`"
  fi
fi

if test "x$with_libtokyotyrant" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libtokyotyrant_cppflags"

  AC_CHECK_HEADERS([tcrdb.h],
    [with_libtokyotyrant="yes"],
    [with_libtokyotyrant="no (tcrdb.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libtokyotyrant" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libtokyotyrant_ldflags"

  AC_CHECK_LIB([tokyotyrant], [tcrdbrnum],
    [with_libtokyotyrant="yes"],
    [with_libtokyotyrant="no (symbol tcrdbrnum not found)"],
    [$with_libtokyotyrant_libs]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libtokyotyrant" = "xyes"; then
  BUILD_WITH_LIBTOKYOTYRANT_CPPFLAGS="$with_libtokyotyrant_cppflags"
  BUILD_WITH_LIBTOKYOTYRANT_LDFLAGS="$with_libtokyotyrant_ldflags"
  BUILD_WITH_LIBTOKYOTYRANT_LIBS="$with_libtokyotyrant_libs"
fi
AC_SUBST([BUILD_WITH_LIBTOKYOTYRANT_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBTOKYOTYRANT_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBTOKYOTYRANT_LIBS])
# }}}

# --with-libudev {{{
AC_ARG_WITH([libudev],
  [AS_HELP_STRING([--with-libudev@<:@=PREFIX@:>@], [Path to libudev.])],
  [
    if test "x$withval" = "xno" || test "x$withval" = "xyes"; then
      with_libudev="$withval"
    else
      with_libudev_cppflags="-I$withval/include"
      with_libudev_ldflags="-L$withval/lib"
      with_libudev="yes"
    fi
  ],
  [
    if test "x$ac_system" = "xLinux"; then
      with_libudev="yes"
    else
      with_libudev="no (Linux only library)"
    fi
  ]
)

if test "x$with_libudev" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libudev_cppflags"

  AC_CHECK_HEADERS([libudev.h],
    [with_libudev="yes"],
    [with_libudev="no (libudev.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libudev" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libudev_ldflags"

  AC_CHECK_LIB([udev], [udev_new],
    [with_libudev="yes"],
    [with_libudev="no (libudev not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libudev" = "xyes"; then
  BUILD_WITH_LIBUDEV_CPPFLAGS="$with_libudev_cppflags"
  BUILD_WITH_LIBUDEV_LDFLAGS="$with_libudev_ldflags"
  BUILD_WITH_LIBUDEV_LIBS="-ludev"
fi

AC_SUBST([BUILD_WITH_LIBUDEV_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBUDEV_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBUDEV_LIBS])

AM_CONDITIONAL([BUILD_WITH_LIBUDEV], [test "x$with_libudev" = "xyes"])
# }}}

# --with-libupsclient {{{
with_libupsclient_config=""
AC_ARG_WITH([libupsclient],
  [AS_HELP_STRING([--with-libupsclient@<:@=PREFIX@:>@], [Path to the upsclient library.])],
  [
    if test "x$withval" = "xno"; then
      with_libupsclient="no"
    else if test "x$withval" = "xyes"; then
      with_libupsclient="use_pkgconfig"
    else
      if test -x "$withval"; then
        with_libupsclient_config="$withval"
        with_libupsclient="use_libupsclient_config"
      else if test -x "$withval/bin/libupsclient-config"; then
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
  [with_libupsclient="use_pkgconfig"]
)

# configure using libupsclient-config
if test "x$with_libupsclient" = "xuse_libupsclient_config"; then
  with_libupsclient_cflags="`$with_libupsclient_config --cflags`"
  if test $? -ne 0; then
    with_libupsclient="no ($with_libupsclient_config failed)"
  fi
  with_libupsclient_libs="`$with_libupsclient_config --libs`"
  if test $? -ne 0; then
    with_libupsclient="no ($with_libupsclient_config failed)"
  fi
fi

if test "x$with_libupsclient" = "xuse_libupsclient_config"; then
  with_libupsclient="yes"
fi

# configure using pkg-config
if test "x$with_libupsclient" = "xuse_pkgconfig"; then
  AC_MSG_NOTICE([Checking for libupsclient using $PKG_CONFIG])
  $PKG_CONFIG --exists 'libupsclient' 2>/dev/null
  if test $? -ne 0; then
    with_libupsclient="no (pkg-config doesn't know libupsclient)"
  fi
fi

if test "x$with_libupsclient" = "xuse_pkgconfig"; then
  with_libupsclient_cflags="`$PKG_CONFIG --cflags 'libupsclient'`"
  if test $? -ne 0; then
    with_libupsclient="no ($PKG_CONFIG failed)"
  fi

  with_libupsclient_libs="`$PKG_CONFIG --libs 'libupsclient'`"
  if test $? -ne 0; then
    with_libupsclient="no ($PKG_CONFIG failed)"
  fi
fi

if test "x$with_libupsclient" = "xuse_pkgconfig"; then
  with_libupsclient="yes"
fi

if test "x$with_libupsclient" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libupsclient_cflags"

  AC_CHECK_HEADERS([upsclient.h],
    [with_libupsclient="yes"],
    [with_libupsclient="no (upsclient.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libupsclient" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libupsclient_libs"

  AC_CHECK_LIB([upsclient], [upscli_connect],
    [with_libupsclient="yes"],
    [with_libupsclient="no (symbol upscli_connect not found)"]
  )

  AC_CHECK_LIB([upsclient], [upscli_init],
    [AC_DEFINE([HAVE_UPSCLI_INIT], [1], [Define when upscli_init() (since version 2-7) is available.])]
  )

  AC_CHECK_LIB([upsclient], [upscli_tryconnect],
    [AC_DEFINE([HAVE_UPSCLI_TRYCONNECT], [1], [Define when upscli_tryconnect() (since version 2.6.2) is available.])]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libupsclient" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libupsclient_cflags"

  AC_CHECK_TYPES([UPSCONN_t, UPSCONN],
    [],
    [],
    [[
      #include <stdlib.h>
      #include <stdio.h>
      #include <upsclient.h>
    ]]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libupsclient" = "xyes"; then
  BUILD_WITH_LIBUPSCLIENT_CFLAGS="$with_libupsclient_cflags"
  BUILD_WITH_LIBUPSCLIENT_LIBS="$with_libupsclient_libs"
fi

AC_SUBST([BUILD_WITH_LIBUPSCLIENT_CFLAGS])
AC_SUBST([BUILD_WITH_LIBUPSCLIENT_LIBS])
# }}}

# --with-libxenctrl {{{
AC_ARG_WITH([libxenctrl],
  [AS_HELP_STRING([--with-libxenctrl@<:@=PREFIX@:>@], [Path to libxenctrl.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libxenctrl_cppflags="-I$withval/include"
      with_libxenctrl_ldflags="-L$withval/lib"
      with_libxenctrl="yes"
    else
      with_libxenctrl="$withval"
    fi
  ],
  [with_libxenctrl="yes"]
)

if test "x$with_libxenctrl" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libxenctrl_cppflags"

  AC_CHECK_HEADERS([xenctrl.h],
    [with_libxenctrl="yes"],
    [with_libxenctrl="no (xenctrl.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libxenctrl" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libxenctrl_ldflags"

  #Xen versions older than 3.4 has no xc_getcpuinfo()
  AC_CHECK_LIB([xenctrl], [xc_getcpuinfo],
    [with_libxenctrl="yes"],
    [with_libxenctrl="no (symbol 'xc_getcpuinfo' not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

LIBXENCTL_CPPFLAGS="$with_libxenctl_cppflags"
LIBXENCTL_LDFLAGS="$with_libxenctl_ldflags"
AC_SUBST([LIBXENCTL_CPPFLAGS])
AC_SUBST([LIBXENCTL_LDFLAGS])
# }}}

# --with-libxmms {{{
with_xmms_config="xmms-config"
AC_ARG_WITH([libxmms],
  [AS_HELP_STRING([--with-libxmms@<:@=PREFIX@:>@], [Path to libxmms.])],
  [
    if test "x$withval" = "xno" || test "x$withval" = "xyes"; then
      with_libxmms="$withval"
    else
      if test -f "$withval" && test -x "$withval"; then
        with_xmms_config="$withval"
      else if test -x "$withval/bin/xmms-config"; then
        with_xmms_config="$withval/bin/xmms-config"
      fi; fi
      with_libxmms="yes"
    fi
  ],
  [with_libxmms="yes"]
)

if test "x$with_libxmms" = "xyes"; then
  with_xmms_cflags=`$with_xmms_config --cflags 2>/dev/null`
  if test $? -ne 0; then
    with_libxmms="no"
  fi
fi

if test "x$with_libxmms" = "xyes"; then
  with_xmms_libs=`$with_xmms_config --libs 2>/dev/null`
  if test $? -ne 0; then
    with_libxmms="no"
  fi
fi

if test "x$with_libxmms" = "xyes"; then
  SAVE_CFLAGS="$CFLAGS"
  CFLAGS="$with_xmms_cflags"

  AC_CHECK_HEADER([xmmsctrl.h],
    [with_libxmms="yes"],
    [with_libxmms="no"],
  )

  CFLAGS="$SAVE_CFLAGS"
fi

if test "x$with_libxmms" = "xyes"; then
  SAVE_LIBS="$LIBS"
  LIBS="$with_xmms_libs"

  AC_CHECK_LIB([xmms], [xmms_remote_get_info],
    [with_libxmss="yes"],
    [with_libxmms="no"],
    [$with_xmms_libs]

  )

  LIBS="$SAVE_LIBS"
fi

BUILD_WITH_LIBXMMS_CFLAGS="$with_xmms_cflags"
BUILD_WITH_LIBXMMS_LIBS="$with_xmms_libs"

AC_SUBST([BUILD_WITH_LIBXMMS_CFLAGS])
AC_SUBST([BUILD_WITH_LIBXMMS_LIBS])
# }}}

# --with-libyajl {{{
AC_ARG_WITH([libyajl],
  [AS_HELP_STRING([--with-libyajl@<:@=PREFIX@:>@], [Path to libyajl.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libyajl_cppflags="-I$withval/include"
      with_libyajl_ldflags="-L$withval/lib"
      with_libyajl="yes"
    else
      with_libyajl="$withval"
    fi
  ],
  [with_libyajl="yes"]
)

if test "x$with_libyajl" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libyajl_cppflags"

  AC_CHECK_HEADERS([yajl/yajl_parse.h],
    [with_libyajl="yes"],
    [with_libyajl="no (yajl/yajl_parse.h not found)"]
  )

  AC_CHECK_HEADERS([yajl/yajl_tree.h],
    [with_libyajl2="yes"],
    [with_libyajl2="no (yajl/yajl_tree.h not found)"]
  )

  AC_CHECK_HEADERS([yajl/yajl_version.h])

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libyajl" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libyajl_ldflags"

  AC_CHECK_LIB([yajl], [yajl_alloc],
    [with_libyajl="yes"],
    [with_libyajl="no (Symbol 'yajl_alloc' not found)"]
  )

  AC_CHECK_LIB([yajl], [yajl_tree_parse],
    [with_libyajl2="yes"],
    [with_libyajl2="no (Symbol 'yajl_tree_parse' not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libyajl" = "xyes"; then
  BUILD_WITH_LIBYAJL_CPPFLAGS="$with_libyajl_cppflags"
  BUILD_WITH_LIBYAJL_LDFLAGS="$with_libyajl_ldflags"
  BUILD_WITH_LIBYAJL_LIBS="-lyajl"
  AC_DEFINE([HAVE_LIBYAJL], [1], [Define if libyajl is present and usable.])
fi

AC_SUBST([BUILD_WITH_LIBYAJL_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBYAJL_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBYAJL_LIBS])

AM_CONDITIONAL([BUILD_WITH_LIBYAJL], [test "x$with_libyajl" = "xyes"])
# }}}

# --with-mic {{{
with_mic_cppflags="-I/opt/intel/mic/sysmgmt/sdk/include"
with_mic_ldflags="-L/opt/intel/mic/sysmgmt/sdk/lib/Linux"
with_mic_libs="-lMicAccessSDK -scif"
AC_ARG_WITH([mic],
  [AS_HELP_STRING([--with-mic@<:@=PREFIX@:>@], [Path to Intel MIC Access API.])],
  [
    if test "x$withval" = "xno" || test "x$withval" = "xyes"; then
      with_mic="$withval"
    else if test -d "$with_mic/lib"; then
      with_mic_cppflags="-I$withval/include"
      with_mic_ldflags="-L$withval/lib/Linux"
      with_mic="yes"
    fi; fi
  ],
  [with_mic="yes"]
)

if test "x$with_mic" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_mic_cppflags"

  AC_CHECK_HEADERS([MicAccessApi.h],
    [with_mic="yes"],
    [with_mic="no (MicAccessApi not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_mic" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_mic_ldflags"

  AC_CHECK_LIB([MicAccessSDK], [MicInitAPI],
    [with_mic="yes"],
    [with_mic="no (symbol MicInitAPI not found)"],
    [$PTHREAD_LIBS -lscif]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_mic" = "xyes"; then
  BUILD_WITH_MIC_CPPFLAGS="$with_mic_cppflags"
  BUILD_WITH_MIC_LDFLAGS="$with_mic_ldflags"
  BUILD_WITH_MIC_LIBS="$with_mic_libs"
fi
AC_SUBST([BUILD_WITH_MIC_CPPFLAGS])
AC_SUBST([BUILD_WITH_MIC_LDFLAGS])
AC_SUBST([BUILD_WITH_MIC_LIBS])
#}}}

# --with-libvarnish {{{
AC_ARG_WITH([libvarnish],
  [AS_HELP_STRING([--with-libvarnish@<:@=PREFIX@:>@], [Path to libvarnish.])],
  [
    if test "x$withval" = "xno"; then
      with_libvarnish="no"
    else if test "x$withval" = "xyes"; then
      with_libvarnish="use_pkgconfig"
    else if test -d "$with_libvarnish/lib"; then
      with_libvarnish_cflags="-I$withval/include"
      with_libvarnish_libs="-L$withval/lib -lvarnishapi"
      with_libvarnish="yes"
    fi; fi; fi
  ],
  [with_libvarnish="use_pkgconfig"]
)

# configure using pkg-config
if test "x$with_libvarnish" = "xuse_pkgconfig"; then
  $PKG_CONFIG --exists 'varnishapi' 2>/dev/null
  if test $? -ne 0; then
    with_libvarnish="no (pkg-config doesn't know varnishapi)"
  fi
fi

if test "x$with_libvarnish" = "xuse_pkgconfig"; then
  with_libvarnish_cflags="`$PKG_CONFIG --cflags 'varnishapi'`"
  if test $? -ne 0; then
    with_libvarnish="no ($PKG_CONFIG failed)"
  fi

  with_libvarnish_libs="`$PKG_CONFIG --libs 'varnishapi'`"
  if test $? -ne 0; then
    with_libvarnish="no ($PKG_CONFIG failed)"
  fi
fi
if test "x$with_libvarnish" = "xuse_pkgconfig"; then
  with_libvarnish="yes"
fi

if test "x$with_libvarnish" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libvarnish_cflags"

  $PKG_CONFIG --atleast-version=5.2 'varnishapi' 2>/dev/null
  if test $? -eq 0; then
    AC_DEFINE([HAVE_VARNISH_V5], [1], [Varnish 5 API support])
  else
    AC_CHECK_HEADERS([vapi/vsc.h],
      [AC_DEFINE([HAVE_VARNISH_V4], [1], [Varnish 4 API support])],
      [
        AC_CHECK_HEADERS([vsc.h],
          [AC_DEFINE([HAVE_VARNISH_V3], [1], [Varnish 3 API support]) ],
          [
            AC_CHECK_HEADERS([varnishapi.h],
              [AC_DEFINE([HAVE_VARNISH_V2], [1], [Varnish 2 API support])],
              [with_libvarnish="no (found none of the varnish header files)"]
            )
          ]
        )
      ]
    )
  fi

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libvarnish" = "xyes"; then
  BUILD_WITH_LIBVARNISH_CFLAGS="$with_libvarnish_cflags"
  BUILD_WITH_LIBVARNISH_LIBS="$with_libvarnish_libs"
fi

AC_SUBST([BUILD_WITH_LIBVARNISH_CFLAGS])
AC_SUBST([BUILD_WITH_LIBVARNISH_LIBS])
# }}}

# pkg-config --exists 'libxml-2.0'; pkg-config --exists libvirt {{{
$PKG_CONFIG --exists 'libxml-2.0' 2>/dev/null
if test $? -eq 0; then
  with_libxml2="yes"
else
  with_libxml2="no (pkg-config doesn't know libxml-2.0)"
fi

$PKG_CONFIG --exists libvirt 2>/dev/null
if test $? = 0; then
  with_libvirt="yes"
else
  with_libvirt="no (pkg-config doesn't know libvirt)"
fi

if test "x$with_libxml2" = "xyes"; then
  with_libxml2_cflags="`$PKG_CONFIG --cflags libxml-2.0`"
  if test $? -ne 0; then
    with_libxml2="no"
  fi

  with_libxml2_ldflags="`$PKG_CONFIG --libs libxml-2.0`"
  if test $? -ne 0; then
    with_libxml2="no"
  fi
fi

if test "x$with_libxml2" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libxml2_cflags"

  AC_CHECK_HEADERS([libxml/parser.h],
    [with_libxml2="yes"],
    [with_libxml2="no (libxml/parser.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libxml2" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libxml2_ldflags"

  AC_CHECK_LIB([xml2], [xmlXPathEval],
    [with_libxml2="yes"],
    [with_libxml2="no (symbol xmlXPathEval not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libxml2" = "xyes"; then
  BUILD_WITH_LIBXML2_CFLAGS="$with_libxml2_cflags"
  BUILD_WITH_LIBXML2_LIBS="$with_libxml2_ldflags"
fi

AC_SUBST([BUILD_WITH_LIBXML2_CFLAGS])
AC_SUBST([BUILD_WITH_LIBXML2_LIBS])

if test "x$with_libvirt" = "xyes"; then
  with_libvirt_cflags="`$PKG_CONFIG --cflags libvirt`"
  if test $? -ne 0; then
    with_libvirt="no"
  fi

  with_libvirt_ldflags="`$PKG_CONFIG --libs libvirt`"
  if test $? -ne 0; then
    with_libvirt="no"
  fi
fi

if test "x$with_libvirt" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libvirt_cflags"

  AC_CHECK_HEADERS([libvirt/libvirt.h],
    [with_libvirt="yes"],
    [with_libvirt="no (libvirt/libvirt.h not found)"]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libvirt" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libvirt_ldflags"

  AC_CHECK_LIB([virt], [virDomainBlockStats],
    [with_libvirt="yes"],
    [with_libvirt="no (symbol virDomainBlockStats not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libvirt" = "xyes"; then
  BUILD_WITH_LIBVIRT_CFLAGS="$with_libvirt_cflags"
  BUILD_WITH_LIBVIRT_LIBS="$with_libvirt_ldflags"
fi

AC_SUBST([BUILD_WITH_LIBVIRT_CFLAGS])
AC_SUBST([BUILD_WITH_LIBVIRT_LIBS])
# }}}

# $PKG_CONFIG --exists OpenIPMIpthread {{{
with_libopenipmipthread="yes"
AC_MSG_CHECKING([for libOpenIPMIpthread])
$PKG_CONFIG --exists OpenIPMIpthread 2>/dev/null
if test $? -ne 0; then
  with_libopenipmipthread="no (pkg-config doesn't know OpenIPMIpthread)"
fi
AC_MSG_RESULT([$with_libopenipmipthread])

if test "x$with_libopenipmipthread" = "xyes"; then
  AC_MSG_CHECKING([for libOpenIPMIpthread CFLAGS])
  temp_result="`$PKG_CONFIG --cflags OpenIPMIpthread`"
  if test $? -eq 0; then
    with_libopenipmipthread_cflags="$temp_result"
  else
    with_libopenipmipthread="no ($PKG_CONFIG --cflags OpenIPMIpthread failed)"
    temp_result="$PKG_CONFIG --cflags OpenIPMIpthread failed"
  fi
  AC_MSG_RESULT([$temp_result])
fi

if test "x$with_libopenipmipthread" = "xyes"; then
  AC_MSG_CHECKING([for libOpenIPMIpthread LDFLAGS])
  temp_result="`$PKG_CONFIG --libs OpenIPMIpthread`"
  if test $? -eq 0; then
    with_libopenipmipthread_ldflags="$temp_result"
  else
    with_libopenipmipthread="no ($PKG_CONFIG --libs OpenIPMIpthread failed)"
    temp_result="$PKG_CONFIG --libs OpenIPMIpthread failed"
  fi
  AC_MSG_RESULT([$temp_result])
fi

if test "x$with_libopenipmipthread" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libopenipmipthread_cflags"

  AC_CHECK_HEADERS([OpenIPMI/ipmi_smi.h],
    [with_libopenipmipthread="yes"],
    [with_libopenipmipthread="no (OpenIPMI/ipmi_smi.h not found)"],
    [[
      #include <OpenIPMI/ipmiif.h>
      #include <OpenIPMI/ipmi_err.h>
      #include <OpenIPMI/ipmi_posix.h>
      #include <OpenIPMI/ipmi_conn.h>
    ]]
  )

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libopenipmipthread" = "xyes"; then
  BUILD_WITH_OPENIPMI_CFLAGS="$with_libopenipmipthread_cflags"
  BUILD_WITH_OPENIPMI_LIBS="$with_libopenipmipthread_ldflags"
fi

AC_SUBST([BUILD_WITH_OPENIPMI_CFLAGS])
AC_SUBST([BUILD_WITH_OPENIPMI_LIBS])
# }}}

# --with-libatasmart {{{
AC_ARG_WITH([libatasmart],
  [AS_HELP_STRING([--with-libatasmart@<:@=PREFIX@:>@], [Path to libatasmart.])],
  [
    if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
      with_libatasmart_cppflags="-I$withval/include"
      with_libatasmart_ldflags="-L$withval/lib"
      with_libatasmart="yes"
    else
      with_libatasmart="$withval"
    fi
  ],
  [
    if test "x$ac_system" = "xLinux"; then
      with_libatasmart="yes"
    else
      with_libatasmart="no (Linux only library)"
    fi
  ]
)

if test "x$with_libatasmart" = "xyes"; then
  SAVE_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $with_libatasmart_cppflags"

  AC_CHECK_HEADERS([atasmart.h],
    [with_libatasmart="yes"],
    [with_libatasmart="no (atasmart.h not found)"])

  CPPFLAGS="$SAVE_CPPFLAGS"
fi

if test "x$with_libatasmart" = "xyes"; then
  SAVE_LDFLAGS="$LDFLAGS"
  LDFLAGS="$LDFLAGS $with_libatasmart_ldflags"

  AC_CHECK_LIB([atasmart], [sk_disk_open],
    [with_libatasmart="yes"],
    [with_libatasmart="no (Symbol 'sk_disk_open' not found)"]
  )

  LDFLAGS="$SAVE_LDFLAGS"
fi

if test "x$with_libatasmart" = "xyes"; then
  BUILD_WITH_LIBATASMART_CPPFLAGS="$with_libatasmart_cppflags"
  BUILD_WITH_LIBATASMART_LDFLAGS="$with_libatasmart_ldflags"
  BUILD_WITH_LIBATASMART_LIBS="-latasmart"
fi

AC_SUBST([BUILD_WITH_LIBATASMART_CPPFLAGS])
AC_SUBST([BUILD_WITH_LIBATASMART_LDFLAGS])
AC_SUBST([BUILD_WITH_LIBATASMART_LIBS])
# }}}

PKG_CHECK_MODULES([LIBNOTIFY], [libnotify],
  [with_libnotify="yes"],
  [with_libnotify="no (pkg-config doesn't know libnotify)"]
)

PKG_CHECK_MODULES([LIBRIEMANN_CLIENT], [riemann-client >= 1.6.0],
 [with_libriemann_client="yes"],
 [with_libriemann_client="no (pkg-config doesn't know libriemann-client)"]
)

# Check for enabled/disabled features
#

# AC_COLLECTD(name, enable/disable, info-text, feature/module)
# ------------------------------------------------------------
dnl
m4_define([my_toupper],[m4_translit([$1], m4_defn([m4_cr_letters]), m4_defn([m4_cr_LETTERS]))])
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
  if test "x$enable_$1" = "xno"; then
    collectd_$1=0
  else
    if test "x$enable_$1" = "xyes"; then
      collectd_$1=1
    else
      AC_MSG_NOTICE([please specify either --enable-$1 or --disable-$1; enabling $1.])
      collectd_$1=1
      enable_$1='yes'
    fi
  fi
  AC_DEFINE_UNQUOTED([COLLECT_]my_toupper([$1]), [$collectd_$1], [whether or not to enable $3 $4])
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
    AC_ARG_ENABLE([$1],
      [AS_HELP_STRING([--enable-$1],[$3])],
      [
        if test "x$enableval" = "xyes"; then
          enable_plugin="yes"
        else if test "x$enableval" = "xforce"; then
          enable_plugin="yes"
          force="yes"
        else
          enable_plugin="no (disabled on command line)"
        fi; fi
      ],
      [
        if test "x$enable_all_plugins" = "xauto"; then
          if test "x$2" = "xyes"; then
            enable_plugin="yes"
          else
            enable_plugin="$2"
          fi
        else
          enable_plugin="$enable_all_plugins"
        fi
      ]
    )
    if test "x$enable_plugin" = "xyes"; then
      if test "x$2" = "xyes" || test "x$force" = "xyes"; then
        AC_DEFINE([HAVE_PLUGIN_]my_toupper([$1]), 1, [Define to 1 if the $1 plugin is enabled.])
        if test "x$2" != "xyes"; then
          dependency_warning="yes"
        fi
      else # User passed "yes" but dependency checking yielded "no" => Dependency problem.
        dependency_error="yes"
        enable_plugin="$2 (dependency error)"
      fi
    fi
    AM_CONDITIONAL([BUILD_PLUGIN_]my_toupper([$1]), [test "x$enable_plugin" = "xyes"])
    enable_$1="$enable_plugin"
  ]
)# AC_PLUGIN(name, default, info)

m4_divert_once([HELP_ENABLE], [
collectd features:])
# FIXME: Remove these calls to `AC_COLLECTD' and then remove that macro.
AC_COLLECTD([debug],     [enable],  [feature], [debugging])
AC_COLLECTD([daemon],    [disable], [feature], [daemon mode])
AC_COLLECTD([getifaddrs],[enable],  [feature], [getifaddrs under Linux])
AC_COLLECTD([werror],    [disable], [feature], [building with -Werror])

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
plugin_cpusleep="no"
plugin_curl_json="no"
plugin_curl_xml="no"
plugin_df="no"
plugin_disk="no"
plugin_drbd="no"
plugin_dpdkevents="no"
plugin_dpdkstat="no"
plugin_entropy="no"
plugin_ethstat="no"
plugin_fhcount="no"
plugin_fscache="no"
plugin_gps="no"
plugin_grpc="no"
plugin_hugepages="no"
plugin_intel_pmu="no"
plugin_intel_rdt="no"
plugin_interface="no"
plugin_ipc="no"
plugin_ipmi="no"
plugin_ipvs="no"
plugin_irq="no"
plugin_load="no"
plugin_log_logstash="no"
plugin_mcelog="no"
plugin_memory="no"
plugin_multimeter="no"
plugin_nfs="no"
plugin_numa="no"
plugin_ovs_events="no"
plugin_ovs_stats="no"
plugin_perl="no"
plugin_pinba="no"
plugin_processes="no"
plugin_protocols="no"
plugin_python="no"
plugin_serial="no"
plugin_smart="no"
plugin_swap="no"
plugin_synproxy="no"
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
plugin_write_prometheus="no"
plugin_xencpu="no"
plugin_zfs_arc="no"
plugin_zone="no"
plugin_zookeeper="no"

# Linux
if test "x$ac_system" = "xLinux"; then
  plugin_battery="yes"
  plugin_cgroups="yes"
  plugin_conntrack="yes"
  plugin_contextswitch="yes"
  plugin_cpu="yes"
  plugin_cpufreq="yes"
  plugin_disk="yes"
  plugin_drbd="yes"
  plugin_entropy="yes"
  plugin_fhcount="yes"
  plugin_fscache="yes"
  plugin_hugepages="yes"
  plugin_interface="yes"
  plugin_ipc="yes"
  plugin_irq="yes"
  plugin_load="yes"
  plugin_lvm="yes"
  plugin_mcelog="yes"
  plugin_memory="yes"
  plugin_nfs="yes"
  plugin_numa="yes"
  plugin_processes="yes"
  plugin_protocols="yes"
  plugin_serial="yes"
  plugin_swap="yes"
  plugin_synproxy="yes"
  plugin_tcpconns="yes"
  plugin_thermal="yes"
  plugin_uptime="yes"
  plugin_vmem="yes"
  plugin_vserver="yes"
  plugin_wireless="yes"
  plugin_zfs_arc="yes"

  if test "x$ac_cv_header_linux_ip_vs_h" = "xyes"; then
    plugin_ipvs="yes"
  fi

  if test "x$have_cpuid_h" = "xyes"; then
    plugin_turbostat="yes"
  fi

  if test "x$c_cv_have_clock_boottime_monotonic" = "xyes"; then
    plugin_cpusleep="yes"
  fi

  if test "x$with_libyajl" = "xyes" && test "x$with_libyajl2" = "xyes"; then
    plugin_ovs_events="yes"
    plugin_ovs_stats="yes"
  fi
fi

if test "x$ac_system" = "xOpenBSD"; then
  plugin_tcpconns="yes"
fi

if test "x$ac_system" = "xNetBSD"; then
  plugin_disk="yes"
  plugin_entropy="yes"
  plugin_irq="yes"
  plugin_processes="yes"
fi

# Mac OS X devices
if test "x$with_libiokit" = "xyes"; then
  plugin_battery="yes"
  plugin_disk="yes"
fi

# AIX

if test "x$ac_system" = "xAIX"; then
  plugin_ipc="yes"
  plugin_tcpconns="yes"
fi

# FreeBSD

if test "x$ac_system" = "xFreeBSD"; then
  plugin_disk="yes"
  plugin_zfs_arc="yes"
fi


if test "x$with_perfstat" = "xyes"; then
  plugin_contextswitch="yes"
  plugin_cpu="yes"
  plugin_disk="yes"
  plugin_interface="yes"
  plugin_load="yes"
  plugin_memory="yes"
  plugin_swap="yes"
  plugin_uptime="yes"
fi

if test "x$with_procinfo" = "xyes"; then
  plugin_processes="yes"
fi

# Solaris
if test "x$with_kstat" = "xyes"; then
  plugin_nfs="yes"
  plugin_processes="yes"
  plugin_uptime="yes"
  plugin_zfs_arc="yes"
  plugin_zone="yes"
fi

if test "x$with_devinfo" = "xyes" && test "x$with_kstat" = "xyes"; then
  plugin_cpu="yes"
  plugin_disk="yes"
  plugin_interface="yes"
  plugin_memory="yes"
  plugin_tape="yes"
fi

if test "x$with_libi2c" = "xyes"; then
  plugin_barometer="yes"
fi


# libstatgrab
if test "x$with_libstatgrab" = "xyes"; then
  plugin_cpu="yes"
  plugin_disk="yes"
  plugin_interface="yes"
  plugin_load="yes"
  plugin_memory="yes"
  plugin_swap="yes"
  plugin_users="yes"
fi

if test "x$with_libcurl" = "xyes" && test "x$with_libxml2" = "xyes"; then
  plugin_ascent="yes"
  if test "x$have_strptime" = "xyes"; then
    plugin_bind="yes"
  fi
fi

if test "x$with_libopenipmipthread" = "xyes"; then
  plugin_ipmi="yes"
fi

if test "x$with_libcurl" = "xyes" && test "x$with_libyajl" = "xyes"; then
  plugin_curl_json="yes"
fi

if test "x$with_libcurl" = "xyes" && test "x$with_libxml2" = "xyes"; then
  plugin_curl_xml="yes"
fi

if test "x$with_libyajl" = "xyes"; then
  plugin_ceph="yes"
fi

if test "x$have_processor_info" = "xyes"; then
  plugin_cpu="yes"
fi

if test "x$have_sysctl" = "xyes"; then
  plugin_cpu="yes"
  plugin_memory="yes"
  plugin_uptime="yes"
  if test "x$ac_system" = "xDarwin"; then
    plugin_swap="yes"
  fi
fi

if test "x$have_sysctlbyname" = "xyes"; then
  plugin_contextswitch="yes"
  plugin_cpu="yes"
  plugin_memory="yes"
  plugin_tcpconns="yes"
fi

if test "x$have_getvfsstat" = "xyes" || test "x$have_getfsstat" = "xyes"; then
  plugin_df="yes"
fi

if test "x$c_cv_have_two_getmntent" = "xyes" || test "x$have_getmntent" = "xgen" || test "x$have_getmntent" = "xsun"; then
  plugin_df="yes"
fi

if test "x$c_cv_have_one_getmntent" = "xyes"; then
  plugin_df="yes"
fi

if test "x$c_cv_have_getmntent_r" = "xyes"; then
  plugin_df="yes"
fi

if test "x$plugin_df" = "xyes"; then
  plugin_df="no"
  if test "x$have_statfs" = "xyes"; then
    plugin_df="yes"
  fi

  if test "x$have_statvfs" = "xyes"; then
    plugin_df="yes"
  fi
fi

if test "x$have_linux_sockios_h" = "xyes" && test "x$have_linux_ethtool_h" = "xyes"; then
  plugin_ethstat="yes"
fi

if test "x$with_libgps" = "xyes"; then
  plugin_gps="yes"
fi

plugin_grpc="yes"
if test "x$GRPC_CPP_PLUGIN" = "x"; then
  plugin_grpc="no (grpc_cpp_plugin not found)"
fi
if test "x$have_protoc3" != "xyes"; then
  plugin_grpc="no (protoc3 not found)"
fi
if test "x$with_libprotobuf" != "xyes"; then
  plugin_grpc="no (libprotobuf not found)"
fi
if test "x$with_libgrpcpp" != "xyes"; then
  plugin_grpc="no (libgrpc++ not found)"
fi

if test "x$have_getifaddrs" = "xyes"; then
  plugin_interface="yes"
fi

if test "x$have_getloadavg" = "xyes"; then
  plugin_load="yes"
fi

if test "x$with_libyajl" = "xyes"; then
  plugin_log_logstash="yes"
fi

if test "x$with_libperl" = "xyes" && test "x$c_cv_have_perl_ithreads" = "xyes"; then
  plugin_perl="yes"
fi

if test "x$have_protoc_c" = "xyes" && test "x$with_libprotobuf_c" = "xyes"; then
  plugin_pinba="yes"
  if test "x$with_libmicrohttpd" = "xyes"; then
    plugin_write_prometheus="yes"
  fi
fi

# Mac OS X memory interface
if test "x$have_host_statistics" = "xyes"; then
  plugin_memory="yes"
fi

if test "x$have_termios_h" = "xyes"; then
  if test "x$ac_system" != "xAIX"; then
    plugin_multimeter="yes"
  fi
  plugin_ted="yes"
fi

if test "x$have_thread_info" = "xyes"; then
  plugin_processes="yes"
fi

if test "x$with_kvm_getprocs" = "xyes" && test "x$have_struct_kinfo_proc_freebsd" = "xyes"; then
  plugin_processes="yes"
fi

if test "x$with_kvm_getprocs" = "xyes" && test "x$have_struct_kinfo_proc_openbsd" = "xyes"; then
  plugin_processes="yes"
fi

if test "x$with_libpython" != "xno"; then
  plugin_python="yes"
fi

if test "x$with_libatasmart" = "xyes" && test "x$with_libudev" = "xyes"; then
  plugin_smart="yes"
fi

if test "x$with_kvm_getswapinfo" = "xyes"; then
  plugin_swap="yes"
fi

if test "x$have_swapctl" = "xyes" && test "x$c_cv_have_swapctl_two_args" = "xyes"; then
  plugin_swap="yes"
fi

if test "x$have_swapctl" = "xyes" && test "x$c_cv_have_swapctl_three_args" = "xyes"; then
  plugin_swap="yes"
fi

if test "x$with_kvm_openfiles = "xyes" && $with_kvm_nlist" = "xyes"; then
  plugin_tcpconns="yes"
fi

if test "x$have_getutent" = "xyes"; then
  plugin_users="yes"
fi

if test "x$have_getutxent" = "xyes"; then
  plugin_users="yes"
fi

if test "x$with_libxml2" = "xyes" && test "x$with_libvirt" = "xyes"; then
  plugin_virt="yes"
fi

if test "x$with_libxenctrl" = "xyes"; then
  plugin_xencpu="yes"
fi

if test "x$with_libdpdk" = "xyes"; then
  plugin_dpdkevents="$dpdk_keepalive"
  plugin_dpdkstat="yes"
fi

m4_divert_once([HELP_ENABLE], [
collectd plugins:])

AC_ARG_ENABLE([all-plugins],
  [AS_HELP_STRING([--enable-all-plugins], [enable all plugins @<:@default=yes@:>@])],
  [
     if test "x$enableval" = "xyes"; then
       enable_all_plugins="yes"
     else if test "x$enableval" = "xauto"; then
       enable_all_plugins="auto"
     else
       enable_all_plugins="no"
     fi; fi
  ],
  [enable_all_plugins="auto"]
)

m4_divert_once([HELP_ENABLE], [])

AC_PLUGIN([aggregation],         [yes],                     [Aggregation plugin])
AC_PLUGIN([amqp],                [$with_librabbitmq],       [AMQP output plugin])
AC_PLUGIN([apache],              [$with_libcurl],           [Apache httpd statistics])
AC_PLUGIN([apcups],              [yes],                     [Statistics of UPSes by APC])
AC_PLUGIN([apple_sensors],       [$with_libiokit],          [Apple hardware sensors])
AC_PLUGIN([aquaero],             [$with_libaquaero5],       [Aquaero hardware sensors])
AC_PLUGIN([ascent],              [$plugin_ascent],          [AscentEmu player statistics])
AC_PLUGIN([barometer],           [$plugin_barometer],       [Barometer sensor on I2C])
AC_PLUGIN([battery],             [$plugin_battery],         [Battery statistics])
AC_PLUGIN([bind],                [$plugin_bind],            [ISC Bind nameserver statistics])
AC_PLUGIN([ceph],                [$plugin_ceph],            [Ceph daemon statistics])
AC_PLUGIN([cgroups],             [$plugin_cgroups],         [CGroups CPU usage accounting])
AC_PLUGIN([chrony],              [yes],                     [Chrony statistics])
AC_PLUGIN([conntrack],           [$plugin_conntrack],       [nf_conntrack statistics])
AC_PLUGIN([contextswitch],       [$plugin_contextswitch],   [context switch statistics])
AC_PLUGIN([cpu],                 [$plugin_cpu],             [CPU usage statistics])
AC_PLUGIN([cpufreq],             [$plugin_cpufreq],         [CPU frequency statistics])
AC_PLUGIN([cpusleep],            [$plugin_cpusleep],        [CPU sleep statistics])
AC_PLUGIN([csv],                 [yes],                     [CSV output plugin])
AC_PLUGIN([curl],                [$with_libcurl],           [CURL generic web statistics])
AC_PLUGIN([curl_json],           [$plugin_curl_json],       [CouchDB statistics])
AC_PLUGIN([curl_xml],            [$plugin_curl_xml],        [CURL generic xml statistics])
AC_PLUGIN([dbi],                 [$with_libdbi],            [General database statistics])
AC_PLUGIN([df],                  [$plugin_df],              [Filesystem usage statistics])
AC_PLUGIN([disk],                [$plugin_disk],            [Disk usage statistics])
AC_PLUGIN([dns],                 [$with_libpcap],           [DNS traffic analysis])
AC_PLUGIN([dpdkevents],          [$plugin_dpdkevents],      [Events from DPDK])
AC_PLUGIN([dpdkstat],            [$plugin_dpdkstat],        [Stats from DPDK])
AC_PLUGIN([drbd],                [$plugin_drbd],            [DRBD statistics])
AC_PLUGIN([email],               [yes],                     [EMail statistics])
AC_PLUGIN([entropy],             [$plugin_entropy],         [Entropy statistics])
AC_PLUGIN([ethstat],             [$plugin_ethstat],         [Stats from NIC driver])
AC_PLUGIN([exec],                [yes],                     [Execution of external programs])
AC_PLUGIN([fhcount],             [$plugin_fhcount],         [File handles statistics])
AC_PLUGIN([filecount],           [yes],                     [Count files in directories])
AC_PLUGIN([fscache],             [$plugin_fscache],         [fscache statistics])
AC_PLUGIN([gmond],               [$with_libganglia],        [Ganglia plugin])
AC_PLUGIN([gps],                 [$plugin_gps],             [GPS plugin])
AC_PLUGIN([grpc],                [$plugin_grpc],            [gRPC plugin])
AC_PLUGIN([hddtemp],             [yes],                     [Query hddtempd])
AC_PLUGIN([hugepages],           [$plugin_hugepages],       [Hugepages statistics])
AC_PLUGIN([intel_pmu],           [$with_libjevents],        [Intel performance monitor plugin])
AC_PLUGIN([intel_rdt],           [$with_libpqos],           [Intel RDT monitor plugin])
AC_PLUGIN([interface],           [$plugin_interface],       [Interface traffic statistics])
AC_PLUGIN([ipc],                 [$plugin_ipc],             [IPC statistics])
AC_PLUGIN([ipmi],                [$plugin_ipmi],            [IPMI sensor statistics])
AC_PLUGIN([iptables],            [$with_libiptc],           [IPTables rule counters])
AC_PLUGIN([ipvs],                [$plugin_ipvs],            [IPVS connection statistics])
AC_PLUGIN([irq],                 [$plugin_irq],             [IRQ statistics])
AC_PLUGIN([java],                [$with_java],              [Embed the Java Virtual Machine])
AC_PLUGIN([load],                [$plugin_load],            [System load])
AC_PLUGIN([log_logstash],        [$plugin_log_logstash],    [Logstash json_event compatible logging])
AC_PLUGIN([logfile],             [yes],                     [File logging plugin])
AC_PLUGIN([lpar],                [$with_perfstat],          [AIX logical partitions statistics])
AC_PLUGIN([lua],                 [$with_liblua],            [Lua plugin])
AC_PLUGIN([lvm],                 [$with_liblvm2app],        [LVM statistics])
AC_PLUGIN([madwifi],             [$have_linux_wireless_h],  [Madwifi wireless statistics])
AC_PLUGIN([match_empty_counter], [yes],                     [The empty counter match])
AC_PLUGIN([match_hashed],        [yes],                     [The hashed match])
AC_PLUGIN([match_regex],         [yes],                     [The regex match])
AC_PLUGIN([match_timediff],      [yes],                     [The timediff match])
AC_PLUGIN([match_value],         [yes],                     [The value match])
AC_PLUGIN([mbmon],               [yes],                     [Query mbmond])
AC_PLUGIN([mcelog],              [$plugin_mcelog],          [Machine Check Exceptions notifications])
AC_PLUGIN([md],                  [$have_linux_raid_md_u_h], [md (Linux software RAID) devices])
AC_PLUGIN([memcachec],           [$with_libmemcached],      [memcachec statistics])
AC_PLUGIN([memcached],           [yes],                     [memcached statistics])
AC_PLUGIN([memory],              [$plugin_memory],          [Memory usage])
AC_PLUGIN([mic],                 [$with_mic],               [Intel Many Integrated Core stats])
AC_PLUGIN([modbus],              [$with_libmodbus],         [Modbus plugin])
AC_PLUGIN([mqtt],                [$with_libmosquitto],      [MQTT output plugin])
AC_PLUGIN([multimeter],          [$plugin_multimeter],      [Read multimeter values])
AC_PLUGIN([mysql],               [$with_libmysql],          [MySQL statistics])
AC_PLUGIN([netapp],              [$with_libnetapp],         [NetApp plugin])
AC_PLUGIN([netlink],             [$with_libmnl],            [Enhanced Linux network statistics])
AC_PLUGIN([network],             [yes],                     [Network communication plugin])
AC_PLUGIN([nfs],                 [$plugin_nfs],             [NFS statistics])
AC_PLUGIN([nginx],               [$with_libcurl],           [nginx statistics])
AC_PLUGIN([notify_desktop],      [$with_libnotify],         [Desktop notifications])
AC_PLUGIN([notify_email],        [$with_libesmtp],          [Email notifier])
AC_PLUGIN([notify_nagios],       [yes],                     [Nagios notification plugin])
AC_PLUGIN([ntpd],                [yes],                     [NTPd statistics])
AC_PLUGIN([numa],                [$plugin_numa],            [NUMA virtual memory statistics])
AC_PLUGIN([nut],                 [$with_libupsclient],      [Network UPS tools statistics])
AC_PLUGIN([olsrd],               [yes],                     [olsrd statistics])
AC_PLUGIN([onewire],             [$with_libowcapi],         [OneWire sensor statistics])
AC_PLUGIN([openldap],            [$with_libldap],           [OpenLDAP statistics])
AC_PLUGIN([openvpn],             [yes],                     [OpenVPN client statistics])
AC_PLUGIN([oracle],              [$with_oracle],            [Oracle plugin])
AC_PLUGIN([ovs_events],          [$plugin_ovs_events],      [OVS events plugin])
AC_PLUGIN([ovs_stats],           [$plugin_ovs_stats],       [OVS statistics plugin])
AC_PLUGIN([perl],                [$plugin_perl],            [Embed a Perl interpreter])
AC_PLUGIN([pf],                  [$have_net_pfvar_h],       [BSD packet filter (PF) statistics])
# FIXME: Check for libevent, too.
AC_PLUGIN([pinba],               [$plugin_pinba],           [Pinba statistics])
AC_PLUGIN([ping],                [$with_liboping],          [Network latency statistics])
AC_PLUGIN([postgresql],          [$with_libpq],             [PostgreSQL database statistics])
AC_PLUGIN([powerdns],            [yes],                     [PowerDNS statistics])
AC_PLUGIN([processes],           [$plugin_processes],       [Process statistics])
AC_PLUGIN([protocols],           [$plugin_protocols],       [Protocol (IP, TCP, ...) statistics])
AC_PLUGIN([python],              [$plugin_python],          [Embed a Python interpreter])
AC_PLUGIN([redis],               [$with_libhiredis],        [Redis plugin])
AC_PLUGIN([routeros],            [$with_librouteros],       [RouterOS plugin])
AC_PLUGIN([rrdcached],           [$librrd_rrdc_update],     [RRDTool output plugin])
AC_PLUGIN([rrdtool],             [$with_librrd],            [RRDTool output plugin])
AC_PLUGIN([sensors],             [$with_libsensors],        [lm_sensors statistics])
AC_PLUGIN([serial],              [$plugin_serial],          [serial port traffic])
AC_PLUGIN([sigrok],              [$with_libsigrok],         [sigrok acquisition sources])
AC_PLUGIN([smart],               [$plugin_smart],           [SMART statistics])
AC_PLUGIN([snmp],                [$with_libnetsnmp],        [SNMP querying plugin])
AC_PLUGIN([snmp_agent],          [$with_libnetsnmpagent],   [SNMP agent plugin])
AC_PLUGIN([statsd],              [yes],                     [StatsD plugin])
AC_PLUGIN([swap],                [$plugin_swap],            [Swap usage statistics])
AC_PLUGIN([synproxy],            [$plugin_synproxy],        [Synproxy stats plugin])
AC_PLUGIN([syslog],              [$have_syslog],            [Syslog logging plugin])
AC_PLUGIN([table],               [yes],                     [Parsing of tabular data])
AC_PLUGIN([tail],                [yes],                     [Parsing of logfiles])
AC_PLUGIN([tail_csv],            [yes],                     [Parsing of CSV files])
AC_PLUGIN([tape],                [$plugin_tape],            [Tape drive statistics])
AC_PLUGIN([target_notification], [yes],                     [The notification target])
AC_PLUGIN([target_replace],      [yes],                     [The replace target])
AC_PLUGIN([target_scale],        [yes],                     [The scale target])
AC_PLUGIN([target_set],          [yes],                     [The set target])
AC_PLUGIN([target_v5upgrade],    [yes],                     [The v5upgrade target])
AC_PLUGIN([tcpconns],            [$plugin_tcpconns],        [TCP connection statistics])
AC_PLUGIN([teamspeak2],          [yes],                     [TeamSpeak2 server statistics])
AC_PLUGIN([ted],                 [$plugin_ted],             [Read The Energy Detective values])
AC_PLUGIN([thermal],             [$plugin_thermal],         [Linux ACPI thermal zone statistics])
AC_PLUGIN([threshold],           [yes],                     [Threshold checking plugin])
AC_PLUGIN([tokyotyrant],         [$with_libtokyotyrant],    [TokyoTyrant database statistics])
AC_PLUGIN([turbostat],           [$plugin_turbostat],       [Advanced statistic on Intel cpu states])
AC_PLUGIN([unixsock],            [yes],                     [Unixsock communication plugin])
AC_PLUGIN([uptime],              [$plugin_uptime],          [Uptime statistics])
AC_PLUGIN([users],               [$plugin_users],           [User statistics])
AC_PLUGIN([uuid],                [yes],                     [UUID as hostname plugin])
AC_PLUGIN([varnish],             [$with_libvarnish],        [Varnish cache statistics])
AC_PLUGIN([virt],                [$plugin_virt],            [Virtual machine statistics])
AC_PLUGIN([vmem],                [$plugin_vmem],            [Virtual memory statistics])
AC_PLUGIN([vserver],             [$plugin_vserver],         [Linux VServer statistics])
AC_PLUGIN([wireless],            [$plugin_wireless],        [Wireless statistics])
AC_PLUGIN([write_graphite],      [yes],                     [Graphite / Carbon output plugin])
AC_PLUGIN([write_http],          [$with_libcurl],           [HTTP output plugin])
AC_PLUGIN([write_kafka],         [$with_librdkafka],        [Kafka output plugin])
AC_PLUGIN([write_log],           [yes],                     [Log output plugin])
AC_PLUGIN([write_mongodb],       [$with_libmongoc],         [MongoDB output plugin])
AC_PLUGIN([write_prometheus],    [$plugin_write_prometheus], [Prometheus write plugin])
AC_PLUGIN([write_redis],         [$with_libhiredis],        [Redis output plugin])
AC_PLUGIN([write_riemann],       [$with_libriemann_client], [Riemann output plugin])
AC_PLUGIN([write_sensu],         [yes],                     [Sensu output plugin])
AC_PLUGIN([write_tsdb],          [yes],                     [TSDB output plugin])
AC_PLUGIN([xencpu],              [$plugin_xencpu],          [Xen Host CPU usage])
AC_PLUGIN([xmms],                [$with_libxmms],           [XMMS statistics])
AC_PLUGIN([zfs_arc],             [$plugin_zfs_arc],         [ZFS ARC statistics])
AC_PLUGIN([zone],                [$plugin_zone],            [Solaris container statistics])
AC_PLUGIN([zookeeper],           [yes],                     [Zookeeper statistics])

dnl Default configuration file
# Load either syslog or logfile
LOAD_PLUGIN_SYSLOG=""
LOAD_PLUGIN_LOGFILE=""
LOAD_PLUGIN_LOG_LOGSTASH=""

AC_MSG_CHECKING([which default log plugin to load])
default_log_plugin="none"
if test "x$enable_syslog" = "xyes"; then
  default_log_plugin="syslog"
else
  LOAD_PLUGIN_SYSLOG="##"
fi

if test "x$enable_logfile" = "xyes"; then
  if test "x$default_log_plugin" = "xnone"; then
    default_log_plugin="logfile"
  else
    LOAD_PLUGIN_LOGFILE="#"
  fi
else
  LOAD_PLUGIN_LOGFILE="##"
fi

if test "x$enable_log_logstash" = "xyes"; then
  LOAD_PLUGIN_LOG_LOGSTASH="#"
else
  LOAD_PLUGIN_LOG_LOGSTASH="##"
fi

AC_MSG_RESULT([$default_log_plugin])

AC_SUBST([LOAD_PLUGIN_SYSLOG])
AC_SUBST([LOAD_PLUGIN_LOGFILE])
AC_SUBST([LOAD_PLUGIN_LOG_LOGSTASH])

if test "x$enable_debug" = "xyes"; then
  DEFAULT_LOG_LEVEL="debug"
else
  DEFAULT_LOG_LEVEL="info"
fi
AC_SUBST([DEFAULT_LOG_LEVEL])

# Load only one of rrdtool, network, csv in the default config.
LOAD_PLUGIN_RRDTOOL=""
LOAD_PLUGIN_NETWORK=""
LOAD_PLUGIN_CSV=""

AC_MSG_CHECKING([which default write plugin to load])
default_write_plugin="none"
if test "x$enable_rrdtool" = "xyes"; then
  default_write_plugin="rrdtool"
else
  LOAD_PLUGIN_RRDTOOL="##"
fi

if test "x$enable_network" = "xyes"; then
  if test "x$default_write_plugin" = "xnone"; then
    default_write_plugin="network"
  else
    LOAD_PLUGIN_NETWORK="#"
  fi
else
  LOAD_PLUGIN_NETWORK="##"
fi

if test "x$enable_csv" = "xyes"; then
  if test "x$default_write_plugin" = "xnone"; then
    default_write_plugin="csv"
  else
    LOAD_PLUGIN_CSV="#"
  fi
else
  LOAD_PLUGIN_CSV="##"
fi
AC_MSG_RESULT([$default_write_plugin])

AC_SUBST([LOAD_PLUGIN_RRDTOOL])
AC_SUBST([LOAD_PLUGIN_NETWORK])
AC_SUBST([LOAD_PLUGIN_CSV])

dnl Perl bindings
PERL_BINDINGS_OPTIONS="PREFIX=${prefix}"
AC_ARG_WITH(perl-bindings, [AS_HELP_STRING([--with-perl-bindings@<:@=OPTIONS@:>@], [Options passed to "perl Makefile.PL".])],
[
  if test "x$withval" != "xno" && test "x$withval" != "xyes"; then
    PERL_BINDINGS_OPTIONS="$withval"
    with_perl_bindings="yes"
  else
    with_perl_bindings="$withval"
  fi
],
[
  if test "x$PERL" != "x"; then
    with_perl_bindings="yes"
  else
    with_perl_bindings="no (no perl interpreter found)"
  fi
])

if test "x$with_perl_bindings" = "xyes"; then
  AC_MSG_CHECKING([for the ExtUtils::MakeMaker module])
  if $PERL -MExtUtils::MakeMaker -e '' 2>/dev/null; then
    AC_MSG_RESULT([yes])
  else
    AC_MSG_RESULT([no])
    with_perl_bindings="no (ExtUtils::MakeMaker not found)"
  fi
fi

if test "x$with_perl_bindings" = "xyes"; then
  PERL_BINDINGS="perl"
else
  PERL_BINDINGS=""
fi

AC_SUBST([PERL_BINDINGS])
AC_SUBST([PERL_BINDINGS_OPTIONS])

dnl libcollectdclient
LCC_VERSION_MAJOR=`echo $PACKAGE_VERSION | cut -d'.' -f1`
LCC_VERSION_MINOR=`echo $PACKAGE_VERSION | cut -d'.' -f2`
LCC_VERSION_PATCH=`echo $PACKAGE_VERSION | cut -d'.' -f3`

LCC_VERSION_EXTRA=`echo $PACKAGE_VERSION | cut -d'.' -f4-`

LCC_VERSION_STRING="$LCC_VERSION_MAJOR.$LCC_VERSION_MINOR.$LCC_VERSION_PATCH"

AC_SUBST([LCC_VERSION_MAJOR])
AC_SUBST([LCC_VERSION_MINOR])
AC_SUBST([LCC_VERSION_PATCH])
AC_SUBST([LCC_VERSION_EXTRA])
AC_SUBST([LCC_VERSION_STRING])

AC_CONFIG_FILES([src/libcollectdclient/collectd/lcc_features.h])

AM_CFLAGS="-Wall"
AM_CXXFLAGS="-Wall"
if test "x$enable_werror" != "xno"; then
  AM_CFLAGS="$AM_CFLAGS -Werror"
  AM_CXXFLAGS="$AM_CXXFLAGS -Werror"
fi

AC_SUBST([AM_CFLAGS])
AC_SUBST([AM_CXXFLAGS])

AC_CONFIG_FILES([ \
  Makefile \
  src/collectd.conf \
  src/libcollectdclient/libcollectdclient.pc \
])

AC_OUTPUT

if test "x$with_librrd" = "xyes" && test "x$librrd_threadsafe" != "xyes"; then
  with_librrd="yes (warning: librrd is not thread-safe)"
fi

if test "x$with_libperl" = "xyes"; then
  with_libperl="yes (version `$PERL -MConfig -e 'print $Config{version};'`)"
else
  enable_perl="no (needs libperl)"
fi

if test "x$enable_perl" = "xno" && test "x$c_cv_have_perl_ithreads" = "xno"; then
  enable_perl="no (libperl doesn't support ithreads)"
fi

if test "x$with_perl_bindings" = "xyes" && test "x$PERL_BINDINGS_OPTIONS" != "x"; then
  with_perl_bindings="yes ($PERL_BINDINGS_OPTIONS)"
fi

AC_MSG_RESULT()
AC_MSG_RESULT([Configuration:])
AC_MSG_RESULT([  Build:])
AC_MSG_RESULT([    Platform  . . . . . . $ac_system])
AC_MSG_RESULT([    Compiler vendor . . . $ax_cv_c_compiler_vendor])
AC_MSG_RESULT([    CC  . . . . . . . . . $CC])
AC_MSG_RESULT([    CFLAGS  . . . . . . . $AM_CFLAGS $CFLAGS])
AC_MSG_RESULT([    CXXFLAGS  . . . . . . $AM_CXXFLAGS $CXXFLAGS])
AC_MSG_RESULT([    CPP . . . . . . . . . $CPP])
AC_MSG_RESULT([    CPPFLAGS  . . . . . . $CPPFLAGS])
AC_MSG_RESULT([    GRPC_CPP_PLUGIN . . . $GRPC_CPP_PLUGIN])
AC_MSG_RESULT([    LD  . . . . . . . . . $LD])
AC_MSG_RESULT([    LDFLAGS . . . . . . . $LDFLAGS])
AC_MSG_RESULT([    PROTOC  . . . . . . . $PROTOC])
AC_MSG_RESULT([    YACC  . . . . . . . . $YACC])
AC_MSG_RESULT([    YFLAGS  . . . . . . . $YFLAGS])
AC_MSG_RESULT()
AC_MSG_RESULT([  Libraries:])
AC_MSG_RESULT([    intel mic . . . . . . $with_mic])
AC_MSG_RESULT([    libaquaero5 . . . . . $with_libaquaero5])
AC_MSG_RESULT([    libatasmart . . . . . $with_libatasmart])
AC_MSG_RESULT([    libcurl . . . . . . . $with_libcurl])
AC_MSG_RESULT([    libdbi  . . . . . . . $with_libdbi])
AC_MSG_RESULT([    libdpdk . . . . . . . $with_libdpdk])
AC_MSG_RESULT([    libesmtp  . . . . . . $with_libesmtp])
AC_MSG_RESULT([    libganglia  . . . . . $with_libganglia])
AC_MSG_RESULT([    libgcrypt . . . . . . $with_libgcrypt])
AC_MSG_RESULT([    libgps  . . . . . . . $with_libgps])
AC_MSG_RESULT([    libgrpc++ . . . . . . $with_libgrpcpp])
AC_MSG_RESULT([    libhiredis  . . . . . $with_libhiredis])
AC_MSG_RESULT([    libi2c-dev  . . . . . $with_libi2c])
AC_MSG_RESULT([    libiokit  . . . . . . $with_libiokit])
AC_MSG_RESULT([    libiptc . . . . . . . $with_libiptc])
AC_MSG_RESULT([    libjevents  . . . . . $with_libjevents])
AC_MSG_RESULT([    libjvm  . . . . . . . $with_java])
AC_MSG_RESULT([    libkstat  . . . . . . $with_kstat])
AC_MSG_RESULT([    libkvm  . . . . . . . $with_libkvm])
AC_MSG_RESULT([    libldap . . . . . . . $with_libldap])
AC_MSG_RESULT([    liblua  . . . . . . . $with_liblua])
AC_MSG_RESULT([    liblvm2app  . . . . . $with_liblvm2app])
AC_MSG_RESULT([    libmemcached  . . . . $with_libmemcached])
AC_MSG_RESULT([    libmicrohttpd . . . . $with_libmicrohttpd])
AC_MSG_RESULT([    libmnl  . . . . . . . $with_libmnl])
AC_MSG_RESULT([    libmodbus . . . . . . $with_libmodbus])
AC_MSG_RESULT([    libmongoc . . . . . . $with_libmongoc])
AC_MSG_RESULT([    libmosquitto  . . . . $with_libmosquitto])
AC_MSG_RESULT([    libmysql  . . . . . . $with_libmysql])
AC_MSG_RESULT([    libnetapp . . . . . . $with_libnetapp])
AC_MSG_RESULT([    libnetsnmp  . . . . . $with_libnetsnmp])
AC_MSG_RESULT([    libnetsnmpagent . . . $with_libnetsnmpagent])
AC_MSG_RESULT([    libnotify . . . . . . $with_libnotify])
AC_MSG_RESULT([    libopenipmi . . . . . $with_libopenipmipthread])
AC_MSG_RESULT([    liboping  . . . . . . $with_liboping])
AC_MSG_RESULT([    libowcapi . . . . . . $with_libowcapi])
AC_MSG_RESULT([    libpcap . . . . . . . $with_libpcap])
AC_MSG_RESULT([    libperfstat . . . . . $with_perfstat])
AC_MSG_RESULT([    libperl . . . . . . . $with_libperl])
AC_MSG_RESULT([    libpq . . . . . . . . $with_libpq])
AC_MSG_RESULT([    libpqos . . . . . . . $with_libpqos])
AC_MSG_RESULT([    libprotobuf . . . . . $with_libprotobuf])
AC_MSG_RESULT([    libprotobuf-c . . . . $with_libprotobuf_c])
AC_MSG_RESULT([    libpython . . . . . . $with_libpython])
AC_MSG_RESULT([    librabbitmq . . . . . $with_librabbitmq])
AC_MSG_RESULT([    libriemann-client . . $with_libriemann_client])
AC_MSG_RESULT([    librdkafka  . . . . . $with_librdkafka])
AC_MSG_RESULT([    librouteros . . . . . $with_librouteros])
AC_MSG_RESULT([    librrd  . . . . . . . $with_librrd])
AC_MSG_RESULT([    libsensors  . . . . . $with_libsensors])
AC_MSG_RESULT([    libsigrok   . . . . . $with_libsigrok])
AC_MSG_RESULT([    libstatgrab . . . . . $with_libstatgrab])
AC_MSG_RESULT([    libtokyotyrant  . . . $with_libtokyotyrant])
AC_MSG_RESULT([    libudev . . . . . . . $with_libudev])
AC_MSG_RESULT([    libupsclient  . . . . $with_libupsclient])
AC_MSG_RESULT([    libvarnish  . . . . . $with_libvarnish])
AC_MSG_RESULT([    libvirt . . . . . . . $with_libvirt])
AC_MSG_RESULT([    libxenctrl  . . . . . $with_libxenctrl])
AC_MSG_RESULT([    libxml2 . . . . . . . $with_libxml2])
AC_MSG_RESULT([    libxmms . . . . . . . $with_libxmms])
AC_MSG_RESULT([    libyajl . . . . . . . $with_libyajl])
AC_MSG_RESULT([    oracle  . . . . . . . $with_oracle])
AC_MSG_RESULT([    protobuf-c  . . . . . $have_protoc_c])
AC_MSG_RESULT([    protoc 3  . . . . . . $have_protoc3])
AC_MSG_RESULT()
AC_MSG_RESULT([  Features:])
AC_MSG_RESULT([    daemon mode . . . . . $enable_daemon])
AC_MSG_RESULT([    debug . . . . . . . . $enable_debug])
AC_MSG_RESULT()
AC_MSG_RESULT([  Bindings:])
AC_MSG_RESULT([    perl  . . . . . . . . $with_perl_bindings])
AC_MSG_RESULT()
AC_MSG_RESULT([  Modules:])
AC_MSG_RESULT([    aggregation . . . . . $enable_aggregation])
AC_MSG_RESULT([    amqp    . . . . . . . $enable_amqp])
AC_MSG_RESULT([    apache  . . . . . . . $enable_apache])
AC_MSG_RESULT([    apcups  . . . . . . . $enable_apcups])
AC_MSG_RESULT([    apple_sensors . . . . $enable_apple_sensors])
AC_MSG_RESULT([    aquaero . . . . . . . $enable_aquaero])
AC_MSG_RESULT([    ascent  . . . . . . . $enable_ascent])
AC_MSG_RESULT([    barometer . . . . . . $enable_barometer])
AC_MSG_RESULT([    battery . . . . . . . $enable_battery])
AC_MSG_RESULT([    bind  . . . . . . . . $enable_bind])
AC_MSG_RESULT([    ceph  . . . . . . . . $enable_ceph])
AC_MSG_RESULT([    cgroups . . . . . . . $enable_cgroups])
AC_MSG_RESULT([    chrony. . . . . . . . $enable_chrony])
AC_MSG_RESULT([    conntrack . . . . . . $enable_conntrack])
AC_MSG_RESULT([    contextswitch . . . . $enable_contextswitch])
AC_MSG_RESULT([    cpu . . . . . . . . . $enable_cpu])
AC_MSG_RESULT([    cpufreq . . . . . . . $enable_cpufreq])
AC_MSG_RESULT([    cpusleep  . . . . . . $enable_cpusleep])
AC_MSG_RESULT([    csv . . . . . . . . . $enable_csv])
AC_MSG_RESULT([    curl  . . . . . . . . $enable_curl])
AC_MSG_RESULT([    curl_json . . . . . . $enable_curl_json])
AC_MSG_RESULT([    curl_xml  . . . . . . $enable_curl_xml])
AC_MSG_RESULT([    dbi . . . . . . . . . $enable_dbi])
AC_MSG_RESULT([    df  . . . . . . . . . $enable_df])
AC_MSG_RESULT([    disk  . . . . . . . . $enable_disk])
AC_MSG_RESULT([    dns . . . . . . . . . $enable_dns])
AC_MSG_RESULT([    dpdkevents. . . . . . $enable_dpdkevents])
AC_MSG_RESULT([    dpdkstat  . . . . . . $enable_dpdkstat])
AC_MSG_RESULT([    drbd  . . . . . . . . $enable_drbd])
AC_MSG_RESULT([    email . . . . . . . . $enable_email])
AC_MSG_RESULT([    entropy . . . . . . . $enable_entropy])
AC_MSG_RESULT([    ethstat . . . . . . . $enable_ethstat])
AC_MSG_RESULT([    exec  . . . . . . . . $enable_exec])
AC_MSG_RESULT([    fhcount . . . . . . . $enable_fhcount])
AC_MSG_RESULT([    filecount . . . . . . $enable_filecount])
AC_MSG_RESULT([    fscache . . . . . . . $enable_fscache])
AC_MSG_RESULT([    gmond . . . . . . . . $enable_gmond])
AC_MSG_RESULT([    gps . . . . . . . . . $enable_gps])
AC_MSG_RESULT([    grpc  . . . . . . . . $enable_grpc])
AC_MSG_RESULT([    hddtemp . . . . . . . $enable_hddtemp])
AC_MSG_RESULT([    hugepages . . . . . . $enable_hugepages])
AC_MSG_RESULT([    intel_pmu . . . . . . $enable_intel_pmu])
AC_MSG_RESULT([    intel_rdt . . . . . . $enable_intel_rdt])
AC_MSG_RESULT([    interface . . . . . . $enable_interface])
AC_MSG_RESULT([    ipc . . . . . . . . . $enable_ipc])
AC_MSG_RESULT([    ipmi  . . . . . . . . $enable_ipmi])
AC_MSG_RESULT([    iptables  . . . . . . $enable_iptables])
AC_MSG_RESULT([    ipvs  . . . . . . . . $enable_ipvs])
AC_MSG_RESULT([    irq . . . . . . . . . $enable_irq])
AC_MSG_RESULT([    java  . . . . . . . . $enable_java])
AC_MSG_RESULT([    load  . . . . . . . . $enable_load])
AC_MSG_RESULT([    logfile . . . . . . . $enable_logfile])
AC_MSG_RESULT([    log_logstash  . . . . $enable_log_logstash])
AC_MSG_RESULT([    lpar  . . . . . . . . $enable_lpar])
AC_MSG_RESULT([    lua . . . . . . . . . $enable_lua])
AC_MSG_RESULT([    lvm . . . . . . . . . $enable_lvm])
AC_MSG_RESULT([    madwifi . . . . . . . $enable_madwifi])
AC_MSG_RESULT([    match_empty_counter . $enable_match_empty_counter])
AC_MSG_RESULT([    match_hashed  . . . . $enable_match_hashed])
AC_MSG_RESULT([    match_regex . . . . . $enable_match_regex])
AC_MSG_RESULT([    match_timediff  . . . $enable_match_timediff])
AC_MSG_RESULT([    match_value . . . . . $enable_match_value])
AC_MSG_RESULT([    mbmon . . . . . . . . $enable_mbmon])
AC_MSG_RESULT([    mcelog  . . . . . . . $enable_mcelog])
AC_MSG_RESULT([    md  . . . . . . . . . $enable_md])
AC_MSG_RESULT([    memcachec . . . . . . $enable_memcachec])
AC_MSG_RESULT([    memcached . . . . . . $enable_memcached])
AC_MSG_RESULT([    memory  . . . . . . . $enable_memory])
AC_MSG_RESULT([    mic . . . . . . . . . $enable_mic])
AC_MSG_RESULT([    modbus  . . . . . . . $enable_modbus])
AC_MSG_RESULT([    mqtt  . . . . . . . . $enable_mqtt])
AC_MSG_RESULT([    multimeter  . . . . . $enable_multimeter])
AC_MSG_RESULT([    mysql . . . . . . . . $enable_mysql])
AC_MSG_RESULT([    netapp  . . . . . . . $enable_netapp])
AC_MSG_RESULT([    netlink . . . . . . . $enable_netlink])
AC_MSG_RESULT([    network . . . . . . . $enable_network])
AC_MSG_RESULT([    nfs . . . . . . . . . $enable_nfs])
AC_MSG_RESULT([    nginx . . . . . . . . $enable_nginx])
AC_MSG_RESULT([    notify_desktop  . . . $enable_notify_desktop])
AC_MSG_RESULT([    notify_email  . . . . $enable_notify_email])
AC_MSG_RESULT([    notify_nagios . . . . $enable_notify_nagios])
AC_MSG_RESULT([    ntpd  . . . . . . . . $enable_ntpd])
AC_MSG_RESULT([    numa  . . . . . . . . $enable_numa])
AC_MSG_RESULT([    nut . . . . . . . . . $enable_nut])
AC_MSG_RESULT([    olsrd . . . . . . . . $enable_olsrd])
AC_MSG_RESULT([    onewire . . . . . . . $enable_onewire])
AC_MSG_RESULT([    openldap  . . . . . . $enable_openldap])
AC_MSG_RESULT([    openvpn . . . . . . . $enable_openvpn])
AC_MSG_RESULT([    oracle  . . . . . . . $enable_oracle])
AC_MSG_RESULT([    ovs_events  . . . . . $enable_ovs_events])
AC_MSG_RESULT([    ovs_stats . . . . . . $enable_ovs_stats])
AC_MSG_RESULT([    perl  . . . . . . . . $enable_perl])
AC_MSG_RESULT([    pf  . . . . . . . . . $enable_pf])
AC_MSG_RESULT([    pinba . . . . . . . . $enable_pinba])
AC_MSG_RESULT([    ping  . . . . . . . . $enable_ping])
AC_MSG_RESULT([    postgresql  . . . . . $enable_postgresql])
AC_MSG_RESULT([    powerdns  . . . . . . $enable_powerdns])
AC_MSG_RESULT([    processes . . . . . . $enable_processes])
AC_MSG_RESULT([    protocols . . . . . . $enable_protocols])
AC_MSG_RESULT([    python  . . . . . . . $enable_python])
AC_MSG_RESULT([    redis . . . . . . . . $enable_redis])
AC_MSG_RESULT([    routeros  . . . . . . $enable_routeros])
AC_MSG_RESULT([    rrdcached . . . . . . $enable_rrdcached])
AC_MSG_RESULT([    rrdtool . . . . . . . $enable_rrdtool])
AC_MSG_RESULT([    sensors . . . . . . . $enable_sensors])
AC_MSG_RESULT([    serial  . . . . . . . $enable_serial])
AC_MSG_RESULT([    sigrok  . . . . . . . $enable_sigrok])
AC_MSG_RESULT([    smart . . . . . . . . $enable_smart])
AC_MSG_RESULT([    snmp  . . . . . . . . $enable_snmp])
AC_MSG_RESULT([    snmp_agent  . . . . . $enable_snmp_agent])
AC_MSG_RESULT([    statsd  . . . . . . . $enable_statsd])
AC_MSG_RESULT([    swap  . . . . . . . . $enable_swap])
AC_MSG_RESULT([    synproxy  . . . . . . $enable_synproxy])
AC_MSG_RESULT([    syslog  . . . . . . . $enable_syslog])
AC_MSG_RESULT([    table . . . . . . . . $enable_table])
AC_MSG_RESULT([    tail_csv  . . . . . . $enable_tail_csv])
AC_MSG_RESULT([    tail  . . . . . . . . $enable_tail])
AC_MSG_RESULT([    tape  . . . . . . . . $enable_tape])
AC_MSG_RESULT([    target_notification . $enable_target_notification])
AC_MSG_RESULT([    target_replace  . . . $enable_target_replace])
AC_MSG_RESULT([    target_scale  . . . . $enable_target_scale])
AC_MSG_RESULT([    target_set  . . . . . $enable_target_set])
AC_MSG_RESULT([    target_v5upgrade  . . $enable_target_v5upgrade])
AC_MSG_RESULT([    tcpconns  . . . . . . $enable_tcpconns])
AC_MSG_RESULT([    teamspeak2  . . . . . $enable_teamspeak2])
AC_MSG_RESULT([    ted . . . . . . . . . $enable_ted])
AC_MSG_RESULT([    thermal . . . . . . . $enable_thermal])
AC_MSG_RESULT([    threshold . . . . . . $enable_threshold])
AC_MSG_RESULT([    tokyotyrant . . . . . $enable_tokyotyrant])
AC_MSG_RESULT([    turbostat . . . . . . $enable_turbostat])
AC_MSG_RESULT([    unixsock  . . . . . . $enable_unixsock])
AC_MSG_RESULT([    uptime  . . . . . . . $enable_uptime])
AC_MSG_RESULT([    users . . . . . . . . $enable_users])
AC_MSG_RESULT([    uuid  . . . . . . . . $enable_uuid])
AC_MSG_RESULT([    varnish . . . . . . . $enable_varnish])
AC_MSG_RESULT([    virt  . . . . . . . . $enable_virt])
AC_MSG_RESULT([    vmem  . . . . . . . . $enable_vmem])
AC_MSG_RESULT([    vserver . . . . . . . $enable_vserver])
AC_MSG_RESULT([    wireless  . . . . . . $enable_wireless])
AC_MSG_RESULT([    write_graphite  . . . $enable_write_graphite])
AC_MSG_RESULT([    write_http  . . . . . $enable_write_http])
AC_MSG_RESULT([    write_kafka . . . . . $enable_write_kafka])
AC_MSG_RESULT([    write_log . . . . . . $enable_write_log])
AC_MSG_RESULT([    write_mongodb . . . . $enable_write_mongodb])
AC_MSG_RESULT([    write_prometheus. . . $enable_write_prometheus])
AC_MSG_RESULT([    write_redis . . . . . $enable_write_redis])
AC_MSG_RESULT([    write_riemann . . . . $enable_write_riemann])
AC_MSG_RESULT([    write_sensu . . . . . $enable_write_sensu])
AC_MSG_RESULT([    write_tsdb  . . . . . $enable_write_tsdb])
AC_MSG_RESULT([    xencpu  . . . . . . . $enable_xencpu])
AC_MSG_RESULT([    xmms  . . . . . . . . $enable_xmms])
AC_MSG_RESULT([    zfs_arc . . . . . . . $enable_zfs_arc])
AC_MSG_RESULT([    zone  . . . . . . . . $enable_zone])
AC_MSG_RESULT([    zookeeper . . . . . . $enable_zookeeper])
AC_MSG_RESULT()

if test "x$dependency_error" = "xyes"; then
  AC_MSG_ERROR("Some plugins are missing dependencies - see the summary above for details")
fi

if test "x$dependency_warning" = "xyes"; then
  AC_MSG_WARN("Some plugins seem to have missing dependencies but have been enabled forcibly - see the summary above for details")
fi

# vim: set fdm=marker sw=2 sts=2 ts=2 et :
