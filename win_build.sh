set -e

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
      sendto \
      gettimeofday \
      time_r \
      sys_stat \
      fcntl-h \
      sys_resource \
      sys_wait \
      setlocale \
      strtok_r \
      poll \
      recv

  cd ${TOP_SRCDIR}/_build_aux/_gnulib
  ./configure --host=mingw32 LIBS="-lws2_32 -lpthread"
  make 
  cd gllib

  # We have to rebuild libnug.a to get the list of *.o files to build a dll later
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

./configure --prefix="C:/opt" --disable-all-plugins \
  --host="mingw32" \
  --with-libcurl="${LIBCURL_DIR}" \
  --enable-logfile \
  --enable-target_replace \
  --enable-target_set \
  --enable-match_regex \
  --enable-write_http \
  --enable-write_log \
  --enable-wmi

cp ${GNULIB_DIR}/../config.h src/gnulib_config.h
echo "#include <config.h.in>" >> src/gnulib_config.h

# TODO: find a sane way to set LTCFLAGS for libtool
cp libtool libtool_bak
sed -i "s%\$LTCC \$LTCFLAGS\(.*cwrapper.*\)%\$LTCC \1%" libtool

make

echo "Only 'make install' left"
