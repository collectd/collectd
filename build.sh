#!/bin/sh

GLOBAL_ERROR_INDICATOR=0

check_for_application()
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

setup_libtool()
{
    libtoolize=""
    libtoolize --version >/dev/null 2>/dev/null
    if test $? -eq 0; then
        libtoolize=libtoolize
    else
        glibtoolize --version >/dev/null 2>/dev/null
        if test $? -eq 0; then
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

    if test "$GLOBAL_ERROR_INDICATOR" != "0"; then
        exit 1
    fi
}

build()
{
    echo "Building..."
    check_for_application lex bison autoheader aclocal automake autoconf pkg-config
    setup_libtool

    set -x
    autoheader \
    && aclocal -I m4 \
    && $libtoolize --copy --force \
    && automake --add-missing --copy \
    && autoconf
}

build_cygwin()
{
    echo "Building for Cygwin..."
    check_for_application aclocal autoconf autoheader automake bison flex git make pkg-config x86_64-w64-mingw32-gcc
    setup_libtool

    set -e

    : ${INSTALL_DIR:="C:/PROGRA~1/collectd"}
    : ${LIBDIR:="${INSTALL_DIR}"}
    : ${BINDIR:="${INSTALL_DIR}"}
    : ${SBINDIR:="${INSTALL_DIR}"}
    : ${SYSCONFDIR:="${INSTALL_DIR}"}
    : ${LOCALSTATEDIR:="${INSTALL_DIR}"}
    : ${DATAROOTDIR:="${INSTALL_DIR}"}
    : ${DATADIR:="${INSTALL_DIR}"}

    echo "Installing collectd to ${INSTALL_DIR}."
    TOP_SRCDIR=$(pwd)
    MINGW_ROOT="/usr/x86_64-w64-mingw32/sys-root/mingw"
    GNULIB_DIR="${TOP_SRCDIR}/_build_aux/_gnulib/gllib"

    export CC="x86_64-w64-mingw32-gcc"

    mkdir -p _build_aux

    cd _build_aux
    if [ -d "_gnulib" ]; then
        echo "Assuming that gnulib is already built, because _gnulib exists."
    else
        git clone git://git.savannah.gnu.org/gnulib.git
        cd gnulib
        git checkout 2f8140bc8ce5501e31dcc665b42b5df64f84c20c
        ./gnulib-tool \
          --create-testdir \
          --source-base=lib \
          --dir=${TOP_SRCDIR}/_build_aux/_gnulib \
          canonicalize-lgpl \
          fcntl-h \
          getsockopt \
          gettimeofday \
          nanosleep \
          netdb \
          net_if \
          poll \
          recv \
          regex \
          sendto \
          setlocale \
          strtok_r \
          sys_resource \
          sys_socket \
          sys_stat \
          sys_wait \
          time_r

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
    cd "${TOP_SRCDIR}"

    set -x
    autoheader \
    && aclocal -I /usr/share/aclocal \
    && $libtoolize --copy --force \
    && automake --add-missing --copy \
    && autoconf

    export LDFLAGS="-L${GNULIB_DIR}"
    export LIBS="-lgnu"
    export CFLAGS="-Drestrict=__restrict -I${GNULIB_DIR}"

    ./configure \
      --prefix="${INSTALL_DIR}" \
      --libdir="${LIBDIR}" \
      --bindir="${BINDIR}" \
      --sbindir="${SBINDIR}" \
      --sysconfdir="${SYSCONFDIR}" \
      --localstatedir="${LOCALSTATEDIR}" \
      --datarootdir="${DATAROOTDIR}" \
      --datarootdir="${DATADIR}" \
      --disable-all-plugins \
      --host="mingw32" \
      --enable-logfile

    cp ${GNULIB_DIR}/../config.h src/gnulib_config.h
    echo "#include <config.h.in>" >> src/gnulib_config.h

    cp libtool libtool_bak
    sed -i "s%\$LTCC \$LTCFLAGS\(.*cwrapper.*\)%\$LTCC \1%" libtool

    make
    make install

    cp "${GNULIB_DIR}/libgnu.dll" "${INSTALL_DIR}"
    cp "${MINGW_ROOT}/bin/zlib1.dll" "${INSTALL_DIR}"
    cp "${MINGW_ROOT}/bin/libwinpthread-1.dll" "${INSTALL_DIR}"
    cp "${MINGW_ROOT}/bin/libdl.dll" "${INSTALL_DIR}"

    echo "Done"
}

os_name="$(uname)"
if test ${os_name:0:6} = "CYGWIN"; then
    build_cygwin
else
    build
fi

