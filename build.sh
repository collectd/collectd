#! /bin/sh

GLOBAL_ERROR_INDICATOR=0

check_for_application ()
{
	for PROG in "$@"
	do
		which "$PROG" >/dev/null 2>&1
		if test $? -ne 0; then
			cat >&2 <<EOF
WARNING: \`$PROG' not found!
    Please make sure that \`$PROG' is installed and is in one of the
    directories listed in the PATH environment variable.
EOF
			GLOBAL_ERROR_INDICATOR=1
		fi
	done
}

build_linux ()
{
	echo "Building for Linux..."
	check_for_application lex bison autoheader aclocal automake autoconf pkg-config

	libtoolize=""
	libtoolize --version >/dev/null 2>/dev/null
	if test $? -eq 0
	then
		libtoolize=libtoolize
	else
		glibtoolize --version >/dev/null 2>/dev/null
		if test $? -eq 0
		then
			libtoolize=glibtoolize
		else
			cat >&2 <<EOF
	WARNING: Neither \`libtoolize' nor \`glibtoolize' have been found!
	    Please make sure that one of them is installed and is in one of the
	    directories listed in the PATH environment variable.
EOF
			GLOBAL_ERROR_INDICATOR=1
		fi
	 fi

	if test "$GLOBAL_ERROR_INDICATOR" != "0"
	then
		exit 1
	fi

	set -x

	autoheader \
	&& aclocal \
	&& $libtoolize --copy --force \
	&& automake --add-missing --copy \
	&& autoconf
}

build_windows ()
{
	set -e

	echo "Building for Windows..."
	check_for_application git automake make flex bison pkg-config wget
	#check_for_application mingw64-x86_64-gcc-core git automake make flex bison pkg-config mingw64-x86_64-zlib wget mingw64-x86_64-dlfcn

	export CC=/usr/bin/x86_64-w64-mingw32-gcc.exe

	TOP_SRCDIR=`pwd`

	mkdir -p _build_aux

	# build gnulib
	pushd _build_aux
	if [ -d "_gnulib" ]; then
	  echo "Assuming that gnulib is already built, because _gnulib exists."
	else
	  git clone git://git.savannah.gnu.org/gnulib.git
	  cd gnulib
	  git checkout 2f8140bc8ce5501e31dcc665b42b5df64f84c20c
	  ./gnulib-tool --create-testdir \
	      --source-base=lib \
	      --dir=${TOP_SRCDIR}/_build_aux/_gnulib \
	      canonicalize-lgpl \
	      regex \
	      sys_socket \
	      nanosleep \
	      netdb \
	      net_if \
	      sendto \
	      gettimeofday \
	      getsockopt \
	      time_r \
	      sys_stat \
	      fcntl-h \
	      sys_resource \
	      sys_wait \
	      setlocale \
	      strtok_r \
	      poll \
	      recv \
	      net_if

	  cd ${TOP_SRCDIR}/_build_aux/_gnulib
	  ./configure --host="mingw32" LIBS="-lws2_32 -lpthread"
	  make 
	  cd gllib

	  # We have to rebuild libgnu.a to get the list of *.o files to build a dll later
	  rm libgnu.a
	  OBJECT_LIST=`make V=1 | grep "ar" | cut -d' ' -f4-`
	  $CC -shared -o libgnu.dll $OBJECT_LIST -lws2_32 -lpthread
	  rm libgnu.a # get rid of it, to use libgnu.dll
	fi
	popd


	# build libtool
	pushd _build_aux
	if [ -d "_libtool" ]; then
	  echo "Assuming that libtool is already built, because _libtool exists."
	else
	  wget http://ftpmirror.gnu.org/libtool/libtool-2.4.6.tar.gz
	  tar xf libtool-2.4.6.tar.gz
	  cd libtool-2.4.6
	  ./configure --host="mingw32" --prefix="${TOP_SRCDIR}/_build_aux/_libtool"
	  make
	  make install
	fi
	popd


	# build libcurl
	pushd _build_aux
	if [ -d "_libcurl" ]; then
	  echo "Assuming that libcurl is already built, because _libcurl exists."
	else
	  wget http://curl.haxx.se/download/curl-7.44.0.tar.gz
	  tar xf curl-7.44.0.tar.gz
	  cd curl-7.44.0
	  ./configure --host="mingw32" --with-winssl --prefix="${TOP_SRCDIR}/_build_aux/_libcurl"
	  make
	  make install
	fi
	popd

	#INSTALL_DIR="C:/Program Files/collectd"
	#INSTALL_DIR="C:/opt"
	#INSTALL_DIR="${TOP_SRCDIR}/opt"
	#INSTALL_DIR="C:/PROGRA~1/Google/monitoringatcorp"
	: ${INSTALL_DIR:="C:/opt"}
	MINGW_ROOT="/usr/x86_64-w64-mingw32/sys-root/mingw"
	LIBTOOL_DIR="${TOP_SRCDIR}/_build_aux/_libtool"
	LIBCURL_DIR="${TOP_SRCDIR}/_build_aux/_libcurl"
	GNULIB_DIR="${TOP_SRCDIR}/_build_aux/_gnulib/gllib"

	autoheader
	aclocal -I ${LIBTOOL_DIR}/share/aclocal
	${LIBTOOL_DIR}/bin/libtoolize --ltdl --copy --force
	automake --add-missing --copy
	autoconf

	export LDFLAGS="-L${GNULIB_DIR} -L${LIBTOOL_DIR}/bin -L${LIBTOOL_DIR}/lib"
	export LIBS="-lgnu"
	export CFLAGS="-Drestrict=__restrict -I${GNULIB_DIR}"

	#./configure --datarootdir="${INSTALL_DIR}" --disable-all-plugins \
	./configure --prefix="${INSTALL_DIR}" --disable-all-plugins \
	  --host="mingw32" \
	  --with-libcurl="${LIBCURL_DIR}" \
	  --enable-logfile \
	  --enable-target_replace \
	  --enable-target_set \
	  --enable-match_regex \
	  --enable-network \
	  --enable-syslog \
	  --enable-write_http \
	  --enable-write_log \
	  --enable-wmi

	cp ${GNULIB_DIR}/../config.h src/gnulib_config.h
	echo "#include <config.h.in>" >> src/gnulib_config.h

	# TODO: find a sane way to set LTCFLAGS for libtool
	cp libtool libtool_bak
	sed -i "s%\$LTCC \$LTCFLAGS\(.*cwrapper.*\)%\$LTCC \1%" libtool

	make #datadir="."
	make install

	cp "${INSTALL_DIR}"/bin/*.dll "${INSTALL_DIR}/sbin"
	cp .libs/*.dll "${INSTALL_DIR}/lib/collectd"
	cp "${GNULIB_DIR}/libgnu.dll" "${LIBTOOL_DIR}/bin/libltdl-7.dll" "${LIBCURL_DIR}/bin/libcurl-4.dll" "${INSTALL_DIR}/sbin"
	cp "${MINGW_ROOT}"/bin/{zlib1.dll,libwinpthread-1.dll,libdl.dll} "${INSTALL_DIR}/sbin"
	cp "${INSTALL_DIR}"/sbin/*.dll "${INSTALL_DIR}/lib/collectd"
	cp "collectd.conf" "$INSTALL_DIR/etc"

	#DEST_DIR="C:/Program Files/collectd"
	#mkdir -p "${DEST_DIR}"
	#cp -r "${INSTALL_DIR}"/* "${DEST_DIR}"

	echo "Done"
}

if test "${OSTYPE}" = "cygwin"; then
	build_windows
else
	build_linux
fi

