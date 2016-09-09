noinst_LTLIBRARIES += libstackdriver_yajl.la

libstackdriver_yajl_la_SOURCES = \
	src/yajl/stackdriver_yajl_tree.h \
	src/yajl/stackdriver_yajl_parse.h \
	src/yajl/stackdriver_yajl_gen.h \
	src/yajl/stackdriver_yajl_common.h \
	src/yajl/stackdriver_yajl_version.h \
	src/yajl/stackdriver_yajl_parser.h \
	src/yajl/stackdriver_yajl_encode.h \
	src/yajl/stackdriver_yajl_buf.h \
	src/yajl/stackdriver_yajl_alloc.h \
	src/yajl/stackdriver_yajl_bytestack.h \
	src/yajl/stackdriver_yajl_lex.h \
	src/yajl/stackdriver_yajl_version.c \
	src/yajl/stackdriver_yajl.c \
	src/yajl/stackdriver_yajl_encode.c \
	src/yajl/stackdriver_yajl_lex.c \
	src/yajl/stackdriver_yajl_parser.c \
	src/yajl/stackdriver_yajl_buf.c \
	src/yajl/stackdriver_yajl_tree.c \
	src/yajl/stackdriver_yajl_alloc.c \
	src/yajl/stackdriver_yajl_gen.c

libstackdriver_yajl_la_CFLAGS = \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/bson \
	-I$(top_builddir)/src/bson \
	-DBSON_COMPILATION
libstackdriver_yajl_la_LIBADD = 
libstackdriver_yajl_la_LDFLAGS = 
