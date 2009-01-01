#! /bin/sh

set -x

true \
&& rm -f aclocal.m4 \
&& rm -f -r autom4te.cache \
&& rm -f collectd-*.tar.bz2 \
&& rm -f collectd-*.tar.gz \
&& rm -f compile \
&& rm -f config.guess \
&& rm -f config.log \
&& rm -f config.status \
&& rm -f config.sub \
&& rm -f configure \
&& rm -f depcomp \
&& rm -f install-sh \
&& rm -f -r libltdl \
&& rm -f libtool \
&& rm -f ltmain.sh \
&& rm -f Makefile \
&& rm -f Makefile.in \
&& rm -f missing \
&& rm -f -r src/.deps \
&& rm -f -r src/.libs \
&& rm -f src/*.o \
&& rm -f src/*.la \
&& rm -f src/*.lo \
&& rm -f src/collectd \
&& rm -f src/collectd.1 \
&& rm -f src/config.h \
&& rm -f src/config.h.in \
&& rm -f src/config.h.in~ \
&& rm -f src/Makefile \
&& rm -f src/Makefile.in \
&& rm -f src/stamp-h1 \
&& rm -f src/stamp-h1.in \
&& rm -f -r src/libping/.libs \
&& rm -f src/libping/*.o \
&& rm -f src/libping/*.la \
&& rm -f src/libping/*.lo \
&& rm -f src/libping/config.h \
&& rm -f src/libping/config.h.in \
&& rm -f src/libping/Makefile \
&& rm -f src/libping/Makefile.in \
&& rm -f src/libping/stamp-h2 \
&& rm -f -r src/libcollectdclient/.libs \
&& rm -f src/libcollectdclient/*.o \
&& rm -f src/libcollectdclient/*.la \
&& rm -f src/libcollectdclient/*.lo
