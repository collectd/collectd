AC_DEFUN([COLLECTD_WITH_LIBOPENIPMI], [
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
])
