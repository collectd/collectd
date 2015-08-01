AC_DEFUN([COLLECTD_WITH_LIBMNL], [
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
])
