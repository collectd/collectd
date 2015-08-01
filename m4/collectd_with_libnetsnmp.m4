AC_DEFUN([COLLECTD_WITH_LIBNETSNMP], [
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
])
