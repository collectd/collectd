AC_DEFUN([COLLECTD_WITH_PYTHON], [
	with_python_prog=""
	with_python_path="$PATH"
	AC_ARG_WITH(python, [AS_HELP_STRING([--with-python@<:@=PREFIX@:>@], [Path to the python interpreter.])],
	[
	 if test "x$withval" = "xyes" || test "x$withval" = "xno"
	 then
		 with_python="$withval"
	 else if test -x "$withval"
	 then
		 with_python_prog="$withval"
		 with_python_path="`dirname \"$withval\"`$PATH_SEPARATOR$with_python_path"
		 with_python="yes"
	 else if test -d "$withval"
	 then
		 with_python_path="$withval$PATH_SEPARATOR$with_python_path"
		 with_python="yes"
	 else
		 AC_MSG_WARN([Argument not recognized: $withval])
	 fi; fi; fi
	], [with_python="yes"])

	SAVE_PATH="$PATH"
	SAVE_CPPFLAGS="$CPPFLAGS"
	SAVE_LDFLAGS="$LDFLAGS"
	SAVE_LIBS="$LIBS"

	PATH="$with_python_path"

	if test "x$with_python" = "xyes" && test "x$with_python_prog" = "x"
	then
		AC_MSG_CHECKING([for python])
		with_python_prog="`which python 2>/dev/null`"
		if test "x$with_python_prog" = "x"
		then
			AC_MSG_RESULT([not found])
			with_python="no (interpreter not found)"
		else
			AC_MSG_RESULT([$with_python_prog])
		fi
	fi

	if test "x$with_python" = "xyes"
	then
		AC_MSG_CHECKING([for Python CPPFLAGS])
		python_include_path=`echo "import distutils.sysconfig;import sys;sys.stdout.write(distutils.sysconfig.get_python_inc())" | "$with_python_prog" 2>&1`
		python_config_status=$?

		if test "$python_config_status" -ne 0 || test "x$python_include_path" = "x"
		then
			AC_MSG_RESULT([failed with status $python_config_status (output: $python_include_path)])
			with_python="no"
		else
			AC_MSG_RESULT([$python_include_path])
		fi
	fi

	if test "x$with_python" = "xyes"
	then
		CPPFLAGS="-I$python_include_path $CPPFLAGS"
		AC_CHECK_HEADERS(Python.h,
				 [with_python="yes"],
				 [with_python="no ('Python.h' not found)"])
	fi

	if test "x$with_python" = "xyes"
	then
		AC_MSG_CHECKING([for Python LDFLAGS])
		python_library_path=`echo "import distutils.sysconfig;import sys;sys.stdout.write(distutils.sysconfig.get_config_vars(\"LIBDIR\").__getitem__(0))" | "$with_python_prog" 2>&1`
		python_config_status=$?

		if test "$python_config_status" -ne 0 || test "x$python_library_path" = "x"
		then
			AC_MSG_RESULT([failed with status $python_config_status (output: $python_library_path)])
			with_python="no"
		else
			AC_MSG_RESULT([$python_library_path])
		fi
	fi

	if test "x$with_python" = "xyes"
	then
		AC_MSG_CHECKING([for Python LIBS])
		python_library_flags=`echo "import distutils.sysconfig;import sys;sys.stdout.write(distutils.sysconfig.get_config_vars(\"BLDLIBRARY\").__getitem__(0))" | "$with_python_prog" 2>&1`
		python_config_status=$?

		if test "$python_config_status" -ne 0 || test "x$python_library_flags" = "x"
		then
			AC_MSG_RESULT([failed with status $python_config_status (output: $python_library_flags)])
			with_python="no"
		else
			AC_MSG_RESULT([$python_library_flags])
		fi
	fi

	if test "x$with_python" = "xyes"
	then
		LDFLAGS="-L$python_library_path $LDFLAGS"
		LIBS="$python_library_flags $LIBS"

		AC_CHECK_FUNC(PyObject_CallFunction,
			      [with_python="yes"],
			      [with_python="no (Symbol 'PyObject_CallFunction' not found)"])
	fi

	PATH="$SAVE_PATH"
	CPPFLAGS="$SAVE_CPPFLAGS"
	LDFLAGS="$SAVE_LDFLAGS"
	LIBS="$SAVE_LIBS"

	if test "x$with_python" = "xyes"
	then
		BUILD_WITH_PYTHON_CPPFLAGS="-I$python_include_path"
		BUILD_WITH_PYTHON_LDFLAGS="-L$python_library_path"
		BUILD_WITH_PYTHON_LIBS="$python_library_flags"
		AC_SUBST(BUILD_WITH_PYTHON_CPPFLAGS)
		AC_SUBST(BUILD_WITH_PYTHON_LDFLAGS)
		AC_SUBST(BUILD_WITH_PYTHON_LIBS)
	fi
])
