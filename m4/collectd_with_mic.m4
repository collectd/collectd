AC_DEFUN([COLLECTD_WITH_MIC], [
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
])
