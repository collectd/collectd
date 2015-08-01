AC_DEFUN([COLLECTD_WITH_LIBSENSORS], [
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
])
