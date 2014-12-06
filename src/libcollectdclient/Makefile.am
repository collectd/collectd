AUTOMAKE_OPTIONS = foreign no-dependencies

if COMPILER_IS_GCC
AM_CFLAGS = -Wall -Werror
endif

pkginclude_HEADERS = collectd/client.h collectd/network.h collectd/network_buffer.h collectd/lcc_features.h
lib_LTLIBRARIES = libcollectdclient.la
nodist_pkgconfig_DATA = libcollectdclient.pc

BUILT_SOURCES = collectd/lcc_features.h

libcollectdclient_la_SOURCES = client.c network.c network_buffer.c
libcollectdclient_la_CPPFLAGS = $(AM_CPPFLAGS) \
				-I$(top_srcdir)/src/libcollectdclient/collectd \
				-I$(top_builddir)/src/libcollectdclient/collectd \
				-I$(top_srcdir)/src/daemon
libcollectdclient_la_LDFLAGS = -version-info 1:0:0
libcollectdclient_la_LIBADD = 
if BUILD_WITH_LIBGCRYPT
libcollectdclient_la_CPPFLAGS += $(GCRYPT_CPPFLAGS)
libcollectdclient_la_LDFLAGS += $(GCRYPT_LDFLAGS)
libcollectdclient_la_LIBADD += $(GCRYPT_LIBS)
endif
