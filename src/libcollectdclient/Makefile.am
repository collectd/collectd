AUTOMAKE_OPTIONS = foreign no-dependencies

if COMPILER_IS_GCC
AM_CFLAGS = -Wall -Werror
endif

pkginclude_HEADERS = client.h
lib_LTLIBRARIES = libcollectdclient.la

libcollectdclient_la_SOURCES = client.c
libcollectdclient_la_LDFLAGS = -version-info 0:0:0
