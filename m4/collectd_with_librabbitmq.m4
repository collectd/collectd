AC_DEFUN([COLLECTD_WITH_LIBRABBITMQ], [
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
])
