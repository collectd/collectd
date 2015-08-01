AC_DEFUN([COLLECTD_WITH_JAVA], [
	with_java_home="$JAVA_HOME"
	if test "x$with_java_home" = "x"
	then
		with_java_home="/usr/lib/jvm"
	fi
	with_java_vmtype="client"
	with_java_cflags=""
	with_java_libs=""
	JAVAC="$JAVAC"
	JAR="$JAR"
	AC_ARG_WITH(java, [AS_HELP_STRING([--with-java@<:@=PREFIX@:>@], [Path to Java home.])],
	[
		if test "x$withval" = "xno"
		then
			with_java="no"
		else if test "x$withval" = "xyes"
		then
			with_java="yes"
		else
			with_java_home="$withval"
			with_java="yes"
		fi; fi
	],
	[with_java="yes"])
	if test "x$with_java" = "xyes"
	then
		if test -d "$with_java_home"
		then
			AC_MSG_CHECKING([for jni.h])
			TMPVAR=`find -L "$with_java_home" -name jni.h -type f -exec 'dirname' '{}' ';' 2>/dev/null | head -n 1`
			if test "x$TMPVAR" != "x"
			then
				AC_MSG_RESULT([found in $TMPVAR])
				JAVA_CPPFLAGS="$JAVA_CPPFLAGS -I$TMPVAR"
			else
				AC_MSG_RESULT([not found])
			fi

			AC_MSG_CHECKING([for jni_md.h])
			TMPVAR=`find -L "$with_java_home" -name jni_md.h -type f -exec 'dirname' '{}' ';' 2>/dev/null | head -n 1`
			if test "x$TMPVAR" != "x"
			then
				AC_MSG_RESULT([found in $TMPVAR])
				JAVA_CPPFLAGS="$JAVA_CPPFLAGS -I$TMPVAR"
			else
				AC_MSG_RESULT([not found])
			fi

			AC_MSG_CHECKING([for libjvm.so])
			TMPVAR=`find -L "$with_java_home" -name libjvm.so -type f -exec 'dirname' '{}' ';' 2>/dev/null | head -n 1`
			if test "x$TMPVAR" != "x"
			then
				AC_MSG_RESULT([found in $TMPVAR])
				JAVA_LDFLAGS="$JAVA_LDFLAGS -L$TMPVAR -Wl,-rpath -Wl,$TMPVAR"
			else
				AC_MSG_RESULT([not found])
			fi

			if test "x$JAVAC" = "x"
			then
				AC_MSG_CHECKING([for javac])
				TMPVAR=`find -L "$with_java_home" -name javac -type f 2>/dev/null | head -n 1`
				if test "x$TMPVAR" != "x"
				then
					JAVAC="$TMPVAR"
					AC_MSG_RESULT([$JAVAC])
				else
					AC_MSG_RESULT([not found])
				fi
			fi
			if test "x$JAR" = "x"
			then
				AC_MSG_CHECKING([for jar])
				TMPVAR=`find -L "$with_java_home" -name jar -type f 2>/dev/null | head -n 1`
				if test "x$TMPVAR" != "x"
				then
					JAR="$TMPVAR"
					AC_MSG_RESULT([$JAR])
				else
					AC_MSG_RESULT([not found])
				fi
			fi
		else if test "x$with_java_home" != "x"
		then
			AC_MSG_WARN([JAVA_HOME: No such directory: $with_java_home])
		fi; fi
	fi

	if test "x$JAVA_CPPFLAGS" != "x"
	then
		AC_MSG_NOTICE([Building with JAVA_CPPFLAGS set to: $JAVA_CPPFLAGS])
	fi
	if test "x$JAVA_CFLAGS" != "x"
	then
		AC_MSG_NOTICE([Building with JAVA_CFLAGS set to: $JAVA_CFLAGS])
	fi
	if test "x$JAVA_LDFLAGS" != "x"
	then
		AC_MSG_NOTICE([Building with JAVA_LDFLAGS set to: $JAVA_LDFLAGS])
	fi
	if test "x$JAVAC" = "x"
	then
		with_javac_path="$PATH"
		if test "x$with_java_home" != "x"
		then
			with_javac_path="$with_java_home:with_javac_path"
			if test -d "$with_java_home/bin"
			then
				with_javac_path="$with_java_home/bin:with_javac_path"
			fi
		fi

		AC_PATH_PROG(JAVAC, javac, [], "$with_javac_path")
	fi
	if test "x$JAVAC" = "x"
	then
		with_java="no (javac not found)"
	fi
	if test "x$JAR" = "x"
	then
		with_jar_path="$PATH"
		if test "x$with_java_home" != "x"
		then
			with_jar_path="$with_java_home:$with_jar_path"
			if test -d "$with_java_home/bin"
			then
				with_jar_path="$with_java_home/bin:$with_jar_path"
			fi
		fi

		AC_PATH_PROG(JAR, jar, [], "$with_jar_path")
	fi
	if test "x$JAR" = "x"
	then
		with_java="no (jar not found)"
	fi

	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_CFLAGS="$CFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"
	CPPFLAGS="$CPPFLAGS $JAVA_CPPFLAGS"
	CFLAGS="$CFLAGS $JAVA_CFLAGS"
	LDFLAGS="$LDFLAGS $JAVA_LDFLAGS"

	if test "x$with_java" = "xyes"
	then
		AC_CHECK_HEADERS(jni.h, [], [with_java="no (jni.h not found)"])
	fi
	if test "x$with_java" = "xyes"
	then
		AC_CHECK_LIB(jvm, JNI_CreateJavaVM,
		[with_java="yes"],
		[with_java="no (libjvm not found)"],
		[$JAVA_LIBS])
	fi
	if test "x$with_java" = "xyes"
	then
		JAVA_LIBS="$JAVA_LIBS -ljvm"
		AC_MSG_NOTICE([Building with JAVA_LIBS set to: $JAVA_LIBS])
	fi

	CPPFLAGS="$SAVE_CPPFLAGS"
	CFLAGS="$SAVE_CFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"

	AC_SUBST(JAVA_CPPFLAGS)
	AC_SUBST(JAVA_CFLAGS)
	AC_SUBST(JAVA_LDFLAGS)
	AC_SUBST(JAVA_LIBS)
	AM_CONDITIONAL(BUILD_WITH_JAVA, test "x$with_java" = "xyes")
])

