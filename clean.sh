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
&& rm -f INSTALL \
&& rm -f -r src/.deps \
&& rm -f -r src/.libs \
&& rm -f src/*.o \
&& rm -f src/*.la \
&& rm -f src/*.lo \
&& rm -f src/collectd.1 \
&& rm -f src/collectd.conf \
&& rm -f src/collectd.conf.in \
&& rm -f src/collectdctl \
&& rm -f src/collectd-tg \
&& rm -f src/collectd-nagios \
&& rm -f src/collectdmon \
&& rm -f src/config.h \
&& rm -f src/config.h.in \
&& rm -f src/config.h.in~ \
&& rm -f src/Makefile \
&& rm -f src/Makefile.in \
&& rm -f src/stamp-h1 \
&& rm -f src/stamp-h1.in \
&& rm -f src/*.pb-c.c \
&& rm -f src/*.pb-c.h \
&& rm -f src/Makefile.in \
&& rm -f -r src/daemon/.deps \
&& rm -f -r src/daemon/.libs \
&& rm -f src/daemon/*.o \
&& rm -f src/daemon/*.la \
&& rm -f src/daemon/*.lo \
&& rm -f src/daemon/collectd \
&& rm -f src/daemon/Makefile.in \
&& rm -f src/daemon/Makefile \
&& rm -f src/liboconfig/*.o \
&& rm -f src/liboconfig/*.la \
&& rm -f src/liboconfig/*.lo \
&& rm -f -r src/liboconfig/.libs \
&& rm -f -r src/liboconfig/Makefile \
&& rm -f -r src/liboconfig/Makefile.in \
&& rm -f -r src/liboconfig/parser.c \
&& rm -f -r src/liboconfig/parser.h \
&& rm -f -r src/liboconfig/scanner.c \
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
&& rm -f src/libcollectdclient/*.lo \
&& rm -f src/libcollectdclient/Makefile \
&& rm -f src/libcollectdclient/Makefile.in \
&& rm -f src/libcollectdclient/collectd/lcc_features.h \
&& rm -f src/libcollectdclient/libcollectdclient.pc \
&& rm -f bindings/Makefile \
&& rm -f bindings/Makefile.in \
&& rm -f -r bindings/java/.libs \
&& rm -f bindings/java/Makefile \
&& rm -f bindings/java/Makefile.in \
&& rm -f bindings/java/java-build-stamp \
&& rm -f bindings/java/org/collectd/api/*.class \
&& rm -f bindings/java/org/collectd/java/*.class \
&& rm -f bindings/.perl-directory-stamp \
&& rm -f -r bindings/buildperl
