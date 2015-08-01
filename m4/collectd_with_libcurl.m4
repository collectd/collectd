AC_DEFUN([COLLECTD_WITH_LIBCURL], [
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
])
