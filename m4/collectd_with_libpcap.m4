AC_DEFUN([COLLECTD_WITH_LIBPCAP], [
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
])
