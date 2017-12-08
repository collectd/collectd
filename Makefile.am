ACLOCAL_AMFLAGS = -I m4
AM_YFLAGS = -d


BUILT_SOURCES = \
	src/libcollectdclient/collectd/lcc_features.h \
	src/liboconfig/parser.h \
	$(dist_man_MANS)


CLEANFILES = \
	.perl-directory-stamp \
	bindings/buildperl/Collectd.pm \
	bindings/buildperl/Collectd/Plugins/OpenVZ.pm \
	bindings/buildperl/Collectd/Unixsock.pm \
	bindings/buildperl/Makefile.PL \
	collectd-api.jar \
	collectd.grpc.pb.cc \
	collectd.grpc.pb.h \
	collectd.pb.cc \
	collectd.pb.h \
	generic-jmx.jar \
	org/collectd/api/*.class \
	org/collectd/java/*.class \
	prometheus.pb-c.c \
	prometheus.pb-c.h \
	src/pinba.pb-c.c \
	src/pinba.pb-c.h \
	types.pb.cc \
	types.pb.h


EXTRA_DIST = \
	bindings/perl/Makefile.PL \
	bindings/perl/lib/Collectd.pm \
	bindings/perl/lib/Collectd/Plugins/Monitorus.pm \
	bindings/perl/lib/Collectd/Plugins/OpenVZ.pm \
	bindings/perl/lib/Collectd/Unixsock.pm \
	bindings/perl/uninstall_mod.pl \
	contrib \
	proto/collectd.proto \
	proto/prometheus.proto \
	proto/types.proto \
	src/collectd-email.pod \
	src/collectd-exec.pod \
	src/collectd-java.pod \
	src/collectd-lua.pod \
	src/collectd-nagios.pod \
	src/collectd-perl.pod \
	src/collectd-python.pod \
	src/collectd-snmp.pod \
	src/collectd-tg.pod \
	src/collectd-threshold.pod \
	src/collectd-unixsock.pod \
	src/collectd.conf.pod \
	src/collectd.pod \
	src/collectdctl.pod \
	src/collectdmon.pod \
	src/pinba.proto \
	src/postgresql_default.conf \
	src/types.db \
	src/types.db.pod \
	src/valgrind.FreeBSD.suppress \
	testwrapper.sh \
	version-gen.sh


dist_man_MANS = \
	src/collectd.1 \
	src/collectd.conf.5 \
	src/collectd-email.5 \
	src/collectd-exec.5 \
	src/collectdctl.1 \
	src/collectd-java.5 \
	src/collectd-lua.5 \
	src/collectdmon.1 \
	src/collectd-nagios.1 \
	src/collectd-perl.5 \
	src/collectd-python.5 \
	src/collectd-snmp.5 \
	src/collectd-tg.1 \
	src/collectd-threshold.5 \
	src/collectd-unixsock.5 \
	src/types.db.5


nodist_pkgconfig_DATA = \
	src/libcollectdclient/libcollectdclient.pc

pkginclude_HEADERS = \
	src/libcollectdclient/collectd/client.h \
	src/libcollectdclient/collectd/lcc_features.h \
	src/libcollectdclient/collectd/network_buffer.h \
	src/libcollectdclient/collectd/network.h \
	src/libcollectdclient/collectd/network_parse.h \
	src/libcollectdclient/collectd/server.h \
	src/libcollectdclient/collectd/types.h

lib_LTLIBRARIES = libcollectdclient.la


sbin_PROGRAMS = \
	collectd \
	collectdmon


bin_PROGRAMS = \
	collectd-nagios \
	collectd-tg \
	collectdctl


noinst_LTLIBRARIES = \
	libavltree.la \
	libcmds.la \
	libcommon.la \
	libformat_graphite.la \
	libformat_json.la \
	libheap.la \
	libignorelist.la \
	liblatency.la \
	liblookup.la \
	libmetadata.la \
	libmount.la \
	liboconfig.la


check_LTLIBRARIES = \
	libplugin_mock.la


check_PROGRAMS = \
	test_common \
	test_format_graphite \
	test_meta_data \
	test_utils_avltree \
	test_utils_cmds \
	test_utils_heap \
	test_utils_latency \
	test_utils_mount \
	test_utils_subst \
	test_utils_time \
	test_utils_vl_lookup \
	test_libcollectd_network_parse


TESTS = $(check_PROGRAMS)

LOG_COMPILER = env VALGRIND="@VALGRIND@" $(abs_srcdir)/testwrapper.sh


jardir = $(pkgdatadir)/java

pkglib_LTLIBRARIES =


PLUGIN_LDFLAGS = \
	-module \
	-avoid-version \
	-export-symbols-regex '\<module_register\>'


AM_CPPFLAGS = \
	-I$(top_srcdir)/src -I$(top_srcdir)/src/daemon \
	-DPREFIX='"${prefix}"' \
	-DCONFIGFILE='"${sysconfdir}/${PACKAGE_NAME}.conf"' \
	-DLOCALSTATEDIR='"${localstatedir}"' \
	-DPKGLOCALSTATEDIR='"${localstatedir}/lib/${PACKAGE_NAME}"' \
	-DPLUGINDIR='"${pkglibdir}"' \
	-DPKGDATADIR='"${pkgdatadir}"'


# Link to these libraries..
COMMON_LIBS = $(PTHREAD_LIBS)
if BUILD_WITH_CAPABILITY
COMMON_LIBS += -lcap
endif
if BUILD_WITH_LIBRT
COMMON_LIBS += -lrt
endif
if BUILD_WITH_LIBPOSIX4
COMMON_LIBS += -lposix4
endif
if BUILD_WITH_LIBSOCKET
COMMON_LIBS += -lsocket
endif
if BUILD_WITH_LIBKSTAT
COMMON_LIBS += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
COMMON_LIBS += -ldevinfo
endif


collectd_SOURCES = \
	src/daemon/collectd.c \
	src/daemon/collectd.h \
	src/daemon/configfile.c \
	src/daemon/configfile.h \
	src/daemon/filter_chain.c \
	src/daemon/filter_chain.h \
	src/daemon/globals.c \
	src/daemon/globals.h \
	src/daemon/meta_data.c \
	src/daemon/meta_data.h \
	src/daemon/plugin.c \
	src/daemon/plugin.h \
	src/daemon/utils_cache.c \
	src/daemon/utils_cache.h \
	src/daemon/utils_complain.c \
	src/daemon/utils_complain.h \
	src/daemon/utils_llist.c \
	src/daemon/utils_llist.h \
	src/daemon/utils_random.c \
	src/daemon/utils_random.h \
	src/daemon/utils_subst.c \
	src/daemon/utils_subst.h \
	src/daemon/utils_time.c \
	src/daemon/utils_time.h \
	src/daemon/types_list.c \
	src/daemon/types_list.h \
	src/daemon/utils_threshold.c \
	src/daemon/utils_threshold.h


collectd_CFLAGS = $(AM_CFLAGS)
collectd_CPPFLAGS = $(AM_CPPFLAGS)
collectd_LDFLAGS = -export-dynamic
collectd_LDADD = \
	libavltree.la \
	libcommon.la \
	libheap.la \
	liboconfig.la \
	-lm \
	$(COMMON_LIBS) \
	$(DLOPEN_LIBS)

if BUILD_FEATURE_DAEMON
collectd_CPPFLAGS += -DPIDFILE='"${localstatedir}/run/${PACKAGE_NAME}.pid"'
endif

# The daemon needs to call sg_init, so we need to link it against libstatgrab,
# too. -octo
if BUILD_WITH_LIBSTATGRAB
collectd_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)
collectd_LDADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
endif


collectdmon_SOURCES = src/collectdmon.c


collectd_nagios_SOURCES = src/collectd-nagios.c
collectd_nagios_CPPFLAGS = $(AM_CPPFLAGS) \
	-I$(srcdir)/src/libcollectdclient \
	-I$(top_builddir)/src/libcollectdclient
collectd_nagios_LDADD = libcollectdclient.la
if BUILD_WITH_LIBSOCKET
collectd_nagios_LDADD += -lsocket
endif
if BUILD_AIX
collectd_nagios_LDADD += -lm
endif


collectdctl_SOURCES = src/collectdctl.c
collectdctl_CPPFLAGS = $(AM_CPPFLAGS) \
	-I$(srcdir)/src/libcollectdclient \
	-I$(top_builddir)/src/libcollectdclient
collectdctl_LDADD = libcollectdclient.la
if BUILD_WITH_LIBSOCKET
collectdctl_LDADD += -lsocket
endif
if BUILD_AIX
collectdctl_LDADD += -lm
endif


collectd_tg_SOURCES = src/collectd-tg.c
collectd_tg_CPPFLAGS = $(AM_CPPFLAGS) \
	-I$(srcdir)/src/libcollectdclient \
	-I$(top_builddir)/src/libcollectdclient
collectd_tg_LDADD = \
	$(PTHREAD_LIBS) \
	libheap.la \
	libcollectdclient.la
if BUILD_WITH_LIBSOCKET
collectd_tg_LDADD += -lsocket
endif
if BUILD_WITH_LIBRT
collectd_tg_LDADD += -lrt
endif
if BUILD_AIX
collectd_tg_LDADD += -lm
endif


test_common_SOURCES = \
	src/daemon/common_test.c \
	src/testing.h
test_common_LDADD = libplugin_mock.la

test_meta_data_SOURCES = \
	src/daemon/meta_data_test.c \
	src/testing.h
test_meta_data_LDADD = libmetadata.la libplugin_mock.la

test_utils_avltree_SOURCES = \
	src/daemon/utils_avltree_test.c \
	src/testing.h
test_utils_avltree_LDADD = libavltree.la $(COMMON_LIBS)

test_utils_heap_SOURCES = \
	src/daemon/utils_heap_test.c \
	src/testing.h
test_utils_heap_LDADD = libheap.la $(COMMON_LIBS)

test_utils_time_SOURCES = \
	src/daemon/utils_time_test.c \
	src/testing.h

test_utils_subst_SOURCES = \
	src/daemon/utils_subst_test.c \
	src/testing.h \
	src/daemon/utils_subst.c \
	src/daemon/utils_subst.h
test_utils_subst_LDADD = libplugin_mock.la

libavltree_la_SOURCES = \
	src/daemon/utils_avltree.c \
	src/daemon/utils_avltree.h

libcommon_la_SOURCES = \
	src/daemon/common.c \
	src/daemon/common.h
libcommon_la_LIBADD = $(COMMON_LIBS)

libheap_la_SOURCES = \
	src/daemon/utils_heap.c \
	src/daemon/utils_heap.h

libignorelist_la_SOURCES = \
	src/utils_ignorelist.c \
	src/utils_ignorelist.h

libmetadata_la_SOURCES = \
	src/daemon/meta_data.c \
	src/daemon/meta_data.h

libplugin_mock_la_SOURCES = \
	src/daemon/plugin_mock.c \
	src/daemon/utils_cache_mock.c \
	src/daemon/utils_complain.c \
	src/daemon/utils_complain.h \
	src/daemon/utils_time.c \
	src/daemon/utils_time.h

libplugin_mock_la_CPPFLAGS = $(AM_CPPFLAGS) -DMOCK_TIME
libplugin_mock_la_LIBADD = libcommon.la libignorelist.la $(COMMON_LIBS)

libformat_graphite_la_SOURCES = \
	src/utils_format_graphite.c \
	src/utils_format_graphite.h

test_format_graphite_SOURCES = \
	src/utils_format_graphite_test.c \
	src/testing.h
test_format_graphite_LDADD = \
	libformat_graphite.la \
	libmetadata.la \
	libplugin_mock.la \
	-lm

libformat_json_la_SOURCES = \
	src/utils_format_json.c \
	src/utils_format_json.h
libformat_json_la_CPPFLAGS  = $(AM_CPPFLAGS)
libformat_json_la_LDFLAGS   = $(AM_LDFLAGS)
libformat_json_la_LIBADD    =
if BUILD_WITH_LIBYAJL
libformat_json_la_CPPFLAGS += $(BUILD_WITH_LIBYAJL_CPPFLAGS)
libformat_json_la_LDFLAGS  += $(BUILD_WITH_LIBYAJL_LDFLAGS)
libformat_json_la_LIBADD   += $(BUILD_WITH_LIBYAJL_LIBS)

check_PROGRAMS += test_format_json

test_format_json_SOURCES = \
	src/utils_format_json_test.c \
	src/testing.h
test_format_json_LDADD = \
	libformat_json.la \
	libmetadata.la \
	libplugin_mock.la \
	-lm
endif

if BUILD_PLUGIN_CEPH
test_plugin_ceph_SOURCES = src/ceph_test.c
test_plugin_ceph_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBYAJL_CPPFLAGS)
test_plugin_ceph_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBYAJL_LDFLAGS)
test_plugin_ceph_LDADD = libplugin_mock.la $(BUILD_WITH_LIBYAJL_LIBS)
check_PROGRAMS += test_plugin_ceph
endif

liblatency_la_SOURCES = \
	src/utils_latency.c \
	src/utils_latency.h \
	src/utils_latency_config.c \
	src/utils_latency_config.h
liblatency_la_LIBADD = \
	libcommon.la \
	-lm

test_utils_latency_SOURCES = \
	src/utils_latency_test.c \
	src/testing.h
test_utils_latency_LDADD = \
	liblatency.la \
	libplugin_mock.la \
	-lm

libcmds_la_SOURCES = \
	src/utils_cmds.c \
	src/utils_cmds.h \
	src/utils_cmd_flush.c \
	src/utils_cmd_flush.h \
	src/utils_cmd_getthreshold.c \
	src/utils_cmd_getthreshold.h \
	src/utils_cmd_getval.c \
	src/utils_cmd_getval.h \
	src/utils_cmd_listval.c \
	src/utils_cmd_listval.h \
	src/utils_cmd_putnotif.c \
	src/utils_cmd_putnotif.h \
	src/utils_cmd_putval.c \
	src/utils_cmd_putval.h \
	src/utils_parse_option.c \
	src/utils_parse_option.h
libcmds_la_LIBADD = \
	libcommon.la \
	libmetadata.la \
	-lm

test_utils_cmds_SOURCES = \
	src/utils_cmds_test.c \
	src/testing.h
test_utils_cmds_LDADD = \
	libcmds.la \
	libplugin_mock.la

liblookup_la_SOURCES = \
	src/utils_vl_lookup.c \
	src/utils_vl_lookup.h
liblookup_la_LIBADD = libavltree.la

test_utils_vl_lookup_SOURCES = \
	src/utils_vl_lookup_test.c \
	src/testing.h
test_utils_vl_lookup_LDADD = \
	liblookup.la \
	libplugin_mock.la
if BUILD_WITH_LIBKSTAT
test_utils_vl_lookup_LDADD += -lkstat
endif

libmount_la_SOURCES = \
	src/utils_mount.c \
	src/utils_mount.h

test_utils_mount_SOURCES = \
	src/utils_mount_test.c \
	src/testing.h
test_utils_mount_LDADD = \
	libmount.la \
	libplugin_mock.la
if BUILD_WITH_LIBKSTAT
test_utils_mount_LDADD += -lkstat
endif


libcollectdclient_la_SOURCES = \
	src/libcollectdclient/client.c \
	src/libcollectdclient/network.c \
	src/libcollectdclient/network_buffer.c \
	src/libcollectdclient/network_parse.c \
	src/libcollectdclient/server.c
libcollectdclient_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(srcdir)/src/libcollectdclient \
	-I$(top_builddir)/src/libcollectdclient \
	-I$(srcdir)/src/daemon
libcollectdclient_la_LDFLAGS = -version-info 2:0:1
libcollectdclient_la_LIBADD = -lm
if BUILD_WITH_LIBGCRYPT
libcollectdclient_la_CPPFLAGS += $(GCRYPT_CPPFLAGS)
libcollectdclient_la_LDFLAGS += $(GCRYPT_LDFLAGS)
libcollectdclient_la_LIBADD += $(GCRYPT_LIBS)
endif

# network_parse_test.c includes network_parse.c, so no need to link with
# libcollectdclient.so.
test_libcollectd_network_parse_SOURCES = src/libcollectdclient/network_parse_test.c
test_libcollectd_network_parse_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(srcdir)/src/libcollectdclient \
	-I$(top_builddir)/src/libcollectdclient
if BUILD_WITH_LIBGCRYPT
test_libcollectd_network_parse_CPPFLAGS += $(GCRYPT_CPPFLAGS)
test_libcollectd_network_parse_LDFLAGS = $(GCRYPT_LDFLAGS)
test_libcollectd_network_parse_LDADD = $(GCRYPT_LIBS)
endif

liboconfig_la_SOURCES = \
	src/liboconfig/oconfig.c \
	src/liboconfig/oconfig.h \
	src/liboconfig/aux_types.h \
	src/liboconfig/scanner.l \
	src/liboconfig/parser.y
liboconfig_la_CPPFLAGS = -I$(srcdir)/src/liboconfig $(AM_CPPFLAGS)
liboconfig_la_LDFLAGS = -avoid-version $(LEXLIB)


if BUILD_PLUGIN_AGGREGATION
pkglib_LTLIBRARIES += aggregation.la
aggregation_la_SOURCES = \
	src/aggregation.c \
	src/utils_vl_lookup.c \
	src/utils_vl_lookup.h
aggregation_la_LDFLAGS = $(PLUGIN_LDFLAGS)
aggregation_la_LIBADD = -lm
endif

if BUILD_PLUGIN_AMQP
pkglib_LTLIBRARIES += amqp.la
amqp_la_SOURCES = src/amqp.c
amqp_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBRABBITMQ_CPPFLAGS)
amqp_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBRABBITMQ_LDFLAGS)
amqp_la_LIBADD = \
	$(BUILD_WITH_LIBRABBITMQ_LIBS) \
	libcmds.la \
	libformat_graphite.la \
	libformat_json.la
endif

if BUILD_PLUGIN_APACHE
pkglib_LTLIBRARIES += apache.la
apache_la_SOURCES = src/apache.c
apache_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBCURL_CFLAGS)
apache_la_LDFLAGS = $(PLUGIN_LDFLAGS)
apache_la_LIBADD = $(BUILD_WITH_LIBCURL_LIBS)
endif


if BUILD_PLUGIN_APCUPS
pkglib_LTLIBRARIES += apcups.la
apcups_la_SOURCES = src/apcups.c
apcups_la_LDFLAGS = $(PLUGIN_LDFLAGS)
apcups_la_LIBADD =
if BUILD_WITH_LIBSOCKET
apcups_la_LIBADD += -lsocket
endif
endif

if BUILD_PLUGIN_APPLE_SENSORS
pkglib_LTLIBRARIES += apple_sensors.la
apple_sensors_la_SOURCES = src/apple_sensors.c
apple_sensors_la_LDFLAGS = $(PLUGIN_LDFLAGS) -framework IOKit
endif

if BUILD_PLUGIN_AQUAERO
pkglib_LTLIBRARIES += aquaero.la
aquaero_la_SOURCES = src/aquaero.c
aquaero_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBAQUAERO5_CFLAGS)
aquaero_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBAQUAERO5_LDFLAGS)
aquaero_la_LIBADD = -laquaero5
endif

if BUILD_PLUGIN_ASCENT
pkglib_LTLIBRARIES += ascent.la
ascent_la_SOURCES = src/ascent.c
ascent_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(BUILD_WITH_LIBCURL_CFLAGS) \
	$(BUILD_WITH_LIBXML2_CFLAGS)
ascent_la_LDFLAGS = $(PLUGIN_LDFLAGS)
ascent_la_LIBADD = \
	$(BUILD_WITH_LIBCURL_LIBS) \
	$(BUILD_WITH_LIBXML2_LIBS)
endif

if BUILD_PLUGIN_BAROMETER
pkglib_LTLIBRARIES += barometer.la
barometer_la_SOURCES = src/barometer.c
barometer_la_LDFLAGS = $(PLUGIN_LDFLAGS)
barometer_la_LIBADD = -lm
endif

if BUILD_PLUGIN_BATTERY
pkglib_LTLIBRARIES += battery.la
battery_la_SOURCES = \
	src/battery.c \
	src/battery_statefs.c
battery_la_LDFLAGS = $(PLUGIN_LDFLAGS)
if BUILD_WITH_LIBIOKIT
battery_la_LDFLAGS += -framework IOKit
endif
endif

if BUILD_PLUGIN_BIND
pkglib_LTLIBRARIES += bind.la
bind_la_SOURCES = src/bind.c
bind_la_CFLAGS = $(AM_CFLAGS) \
	$(BUILD_WITH_LIBCURL_CFLAGS) $(BUILD_WITH_LIBXML2_CFLAGS)
bind_la_LDFLAGS = $(PLUGIN_LDFLAGS)
bind_la_LIBADD = $(BUILD_WITH_LIBCURL_LIBS) $(BUILD_WITH_LIBXML2_LIBS)
endif

if BUILD_PLUGIN_CEPH
pkglib_LTLIBRARIES += ceph.la
ceph_la_SOURCES = src/ceph.c
ceph_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBYAJL_CPPFLAGS)
ceph_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBYAJL_LDFLAGS)
ceph_la_LIBADD = $(BUILD_WITH_LIBYAJL_LIBS)
endif

if BUILD_PLUGIN_CGROUPS
pkglib_LTLIBRARIES += cgroups.la
cgroups_la_SOURCES = src/cgroups.c
cgroups_la_LDFLAGS = $(PLUGIN_LDFLAGS)
cgroups_la_LIBADD = libignorelist.la libmount.la
endif

if BUILD_PLUGIN_CHRONY
pkglib_LTLIBRARIES += chrony.la
chrony_la_SOURCES = src/chrony.c
chrony_la_LDFLAGS = $(PLUGIN_LDFLAGS)
chrony_la_LIBADD = -lm
endif

if BUILD_PLUGIN_CONNTRACK
pkglib_LTLIBRARIES += conntrack.la
conntrack_la_SOURCES = src/conntrack.c
conntrack_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_CONTEXTSWITCH
pkglib_LTLIBRARIES += contextswitch.la
contextswitch_la_SOURCES = src/contextswitch.c
contextswitch_la_LDFLAGS = $(PLUGIN_LDFLAGS)
contextswitch_la_LIBADD =
if BUILD_WITH_PERFSTAT
contextswitch_la_LIBADD += -lperfstat
endif
endif

if BUILD_PLUGIN_CPU
pkglib_LTLIBRARIES += cpu.la
cpu_la_SOURCES = src/cpu.c
cpu_la_CFLAGS = $(AM_CFLAGS)
cpu_la_LDFLAGS = $(PLUGIN_LDFLAGS)
cpu_la_LIBADD =
if BUILD_WITH_LIBKSTAT
cpu_la_LIBADD += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
cpu_la_LIBADD += -ldevinfo
endif
if BUILD_WITH_LIBSTATGRAB
cpu_la_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)
cpu_la_LIBADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
endif
if BUILD_WITH_PERFSTAT
cpu_la_LIBADD += -lperfstat
endif
endif

if BUILD_PLUGIN_CPUFREQ
pkglib_LTLIBRARIES += cpufreq.la
cpufreq_la_SOURCES = src/cpufreq.c
cpufreq_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_CPUSLEEP
pkglib_LTLIBRARIES += cpusleep.la
cpusleep_la_SOURCES = src/cpusleep.c
cpusleep_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_CSV
pkglib_LTLIBRARIES += csv.la
csv_la_SOURCES = src/csv.c
csv_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_CURL
pkglib_LTLIBRARIES += curl.la
curl_la_SOURCES = \
	src/curl.c \
	src/utils_curl_stats.c \
	src/utils_curl_stats.h \
	src/utils_match.c \
	src/utils_match.h
curl_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBCURL_CFLAGS)
curl_la_LDFLAGS = $(PLUGIN_LDFLAGS)
curl_la_LIBADD = liblatency.la $(BUILD_WITH_LIBCURL_LIBS)
endif

if BUILD_PLUGIN_CURL_JSON
pkglib_LTLIBRARIES += curl_json.la
curl_json_la_SOURCES = \
	src/curl_json.c \
	src/utils_curl_stats.c \
	src/utils_curl_stats.h
curl_json_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBCURL_CFLAGS)
curl_json_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBYAJL_CPPFLAGS)
curl_json_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBYAJL_LDFLAGS)
curl_json_la_LIBADD = $(BUILD_WITH_LIBCURL_LIBS) $(BUILD_WITH_LIBYAJL_LIBS)

test_plugin_curl_json_SOURCES = src/curl_json_test.c \
				src/utils_curl_stats.c \
				src/daemon/configfile.c \
				src/daemon/types_list.c
test_plugin_curl_json_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBYAJL_CPPFLAGS)
test_plugin_curl_json_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBYAJL_LDFLAGS)
test_plugin_curl_json_LDADD = libavltree.la liboconfig.la libplugin_mock.la $(BUILD_WITH_LIBCURL_LIBS) $(BUILD_WITH_LIBYAJL_LIBS)
check_PROGRAMS += test_plugin_curl_json
endif

if BUILD_PLUGIN_CURL_XML
pkglib_LTLIBRARIES += curl_xml.la
curl_xml_la_SOURCES = \
	src/curl_xml.c \
	src/utils_curl_stats.c \
	src/utils_curl_stats.h
curl_xml_la_CFLAGS = $(AM_CFLAGS) \
		$(BUILD_WITH_LIBCURL_CFLAGS) $(BUILD_WITH_LIBXML2_CFLAGS)
curl_xml_la_LDFLAGS = $(PLUGIN_LDFLAGS)
curl_xml_la_LIBADD = $(BUILD_WITH_LIBCURL_LIBS) $(BUILD_WITH_LIBXML2_LIBS)
endif

if BUILD_PLUGIN_DBI
pkglib_LTLIBRARIES += dbi.la
dbi_la_SOURCES = \
	src/dbi.c \
	src/utils_db_query.c \
	src/utils_db_query.h
dbi_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBDBI_CPPFLAGS)
dbi_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBDBI_LDFLAGS)
dbi_la_LIBADD = $(BUILD_WITH_LIBDBI_LIBS)
endif

if BUILD_PLUGIN_DF
pkglib_LTLIBRARIES += df.la
df_la_SOURCES = src/df.c
df_la_LDFLAGS = $(PLUGIN_LDFLAGS)
df_la_LIBADD = libignorelist.la libmount.la
endif

if BUILD_PLUGIN_DISK
pkglib_LTLIBRARIES += disk.la
disk_la_SOURCES = src/disk.c
disk_la_CFLAGS = $(AM_CFLAGS)
disk_la_CPPFLAGS = $(AM_CPPFLAGS)
disk_la_LDFLAGS = $(PLUGIN_LDFLAGS)
disk_la_LIBADD = libignorelist.la
if BUILD_WITH_LIBKSTAT
disk_la_LIBADD += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
disk_la_LIBADD += -ldevinfo
endif
if BUILD_WITH_LIBIOKIT
disk_la_LDFLAGS += -framework IOKit
endif
if BUILD_WITH_LIBSTATGRAB
disk_la_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)
disk_la_LIBADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
endif
if BUILD_WITH_LIBUDEV
disk_la_CPPFLAGS += $(BUILD_WITH_LIBUDEV_CPPFLAGS)
disk_la_LDFLAGS += $(BUILD_WITH_LIBUDEV_LDFLAGS)
disk_la_LIBADD += $(BUILD_WITH_LIBUDEV_LIBS)
endif
if BUILD_FREEBSD
disk_la_LIBADD += -ldevstat -lgeom
endif
if BUILD_WITH_PERFSTAT
disk_la_LIBADD += -lperfstat
endif
endif

if BUILD_PLUGIN_DNS
pkglib_LTLIBRARIES += dns.la
dns_la_SOURCES = \
	src/dns.c \
	src/utils_dns.c \
	src/utils_dns.h
dns_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBPCAP_CPPFLAGS)
dns_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBPCAP_LDFLAGS)
dns_la_LIBADD = $(BUILD_WITH_LIBPCAP_LIBS)
endif

if BUILD_PLUGIN_DPDKEVENTS
pkglib_LTLIBRARIES += dpdkevents.la
dpdkevents_la_SOURCES = src/dpdkevents.c src/utils_dpdk.c src/utils_dpdk.h
dpdkevents_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBDPDK_CPPFLAGS)
dpdkevents_la_CFLAGS = $(AM_CFLAGS) $(LIBDPDK_CFLAGS)
dpdkevents_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(LIBDPDK_LDFLAGS)
dpdkevents_la_LIBADD = $(LIBDPDK_LIBS)
endif

if BUILD_PLUGIN_DPDKSTAT
pkglib_LTLIBRARIES += dpdkstat.la
dpdkstat_la_SOURCES = src/dpdkstat.c src/utils_dpdk.c src/utils_dpdk.h
dpdkstat_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBDPDK_CPPFLAGS)
dpdkstat_la_CFLAGS = $(AM_CFLAGS) $(LIBDPDK_CFLAGS)
dpdkstat_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(LIBDPDK_LDFLAGS)
dpdkstat_la_LIBADD = $(LIBDPDK_LIBS)
endif

if BUILD_PLUGIN_DRBD
pkglib_LTLIBRARIES += drbd.la
drbd_la_SOURCES = src/drbd.c
drbd_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_EMAIL
pkglib_LTLIBRARIES += email.la
email_la_SOURCES = src/email.c
email_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_ENTROPY
pkglib_LTLIBRARIES += entropy.la
entropy_la_SOURCES = src/entropy.c
entropy_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_EXEC
pkglib_LTLIBRARIES += exec.la
exec_la_SOURCES = src/exec.c
exec_la_LDFLAGS = $(PLUGIN_LDFLAGS)
exec_la_LIBADD = libcmds.la
endif

if BUILD_PLUGIN_ETHSTAT
pkglib_LTLIBRARIES += ethstat.la
ethstat_la_SOURCES = src/ethstat.c
ethstat_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_FHCOUNT
pkglib_LTLIBRARIES += fhcount.la
fhcount_la_SOURCES = src/fhcount.c
fhcount_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_FILECOUNT
pkglib_LTLIBRARIES += filecount.la
filecount_la_SOURCES = src/filecount.c
filecount_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_FSCACHE
pkglib_LTLIBRARIES += fscache.la
fscache_la_SOURCES = src/fscache.c
fscache_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_GMOND
pkglib_LTLIBRARIES += gmond.la
gmond_la_SOURCES = src/gmond.c
gmond_la_CPPFLAGS = $(AM_CPPFLAGS) $(GANGLIA_CPPFLAGS)
gmond_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(GANGLIA_LDFLAGS)
gmond_la_LIBADD = $(GANGLIA_LIBS)
endif

if BUILD_PLUGIN_GPS
pkglib_LTLIBRARIES += gps.la
gps_la_SOURCES = src/gps.c
gps_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBGPS_CFLAGS)
gps_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBGPS_LDFLAGS)
gps_la_LIBADD = -lpthread $(BUILD_WITH_LIBGPS_LIBS)
endif

if BUILD_PLUGIN_GRPC
pkglib_LTLIBRARIES += grpc.la
grpc_la_SOURCES = src/grpc.cc
nodist_grpc_la_SOURCES = \
	collectd.grpc.pb.cc \
	collectd.pb.cc \
	types.pb.cc
grpc_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBGRPCPP_CPPFLAGS) $(BUILD_WITH_LIBPROTOBUF_CPPFLAGS)
grpc_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBGRPCPP_LDFLAGS) $(BUILD_WITH_LIBPROTOBUF_LDFLAGS)
grpc_la_LIBADD = $(BUILD_WITH_LIBGRPCPP_LIBS) $(BUILD_WITH_LIBPROTOBUF_LIBS)
endif

if BUILD_PLUGIN_HDDTEMP
pkglib_LTLIBRARIES += hddtemp.la
hddtemp_la_SOURCES = src/hddtemp.c
hddtemp_la_LDFLAGS = $(PLUGIN_LDFLAGS)
hddtemp_la_LIBADD =
if BUILD_WITH_LIBSOCKET
hddtemp_la_LIBADD += -lsocket
endif
endif

if BUILD_PLUGIN_HUGEPAGES
pkglib_LTLIBRARIES += hugepages.la
hugepages_la_SOURCES = src/hugepages.c
hugepages_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_INTEL_PMU
pkglib_LTLIBRARIES += intel_pmu.la
intel_pmu_la_SOURCES = src/intel_pmu.c
intel_pmu_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBJEVENTS_CPPFLAGS)
intel_pmu_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBJEVENTS_LDFLAGS)
intel_pmu_la_LIBADD = $(BUILD_WITH_LIBJEVENTS_LIBS)
endif

if BUILD_PLUGIN_INTEL_RDT
pkglib_LTLIBRARIES += intel_rdt.la
intel_rdt_la_SOURCES = src/intel_rdt.c
intel_rdt_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBPQOS_CPPFLAGS)
intel_rdt_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBPQOS_LDFLAGS)
intel_rdt_la_LIBADD = $(BUILD_WITH_LIBPQOS_LIBS)
endif

if BUILD_PLUGIN_INTERFACE
pkglib_LTLIBRARIES += interface.la
interface_la_SOURCES = src/interface.c
interface_la_CFLAGS = $(AM_CFLAGS)
interface_la_LDFLAGS = $(PLUGIN_LDFLAGS)
interface_la_LIBADD = libignorelist.la
if BUILD_WITH_LIBSTATGRAB
interface_la_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)
interface_la_LIBADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
else
if BUILD_WITH_LIBKSTAT
interface_la_LIBADD += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
interface_la_LIBADD += -ldevinfo
endif # BUILD_WITH_LIBDEVINFO
endif # !BUILD_WITH_LIBSTATGRAB
if BUILD_WITH_PERFSTAT
interface_la_LIBADD += -lperfstat
endif
endif # BUILD_PLUGIN_INTERFACE

if BUILD_PLUGIN_IPC
pkglib_LTLIBRARIES += ipc.la
ipc_la_SOURCES = src/ipc.c
ipc_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_IPTABLES
pkglib_LTLIBRARIES += iptables.la
iptables_la_SOURCES = src/iptables.c
iptables_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBIPTC_CPPFLAGS)
iptables_la_LDFLAGS = $(PLUGIN_LDFLAGS)
iptables_la_LIBADD = $(BUILD_WITH_LIBIPTC_LDFLAGS)
endif

if BUILD_PLUGIN_IPMI
pkglib_LTLIBRARIES += ipmi.la
ipmi_la_SOURCES = src/ipmi.c
ipmi_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_OPENIPMI_CFLAGS)
ipmi_la_LDFLAGS = $(PLUGIN_LDFLAGS)
ipmi_la_LIBADD = libignorelist.la $(BUILD_WITH_OPENIPMI_LIBS)
endif

if BUILD_PLUGIN_IPVS
pkglib_LTLIBRARIES += ipvs.la
ipvs_la_SOURCES = src/ipvs.c
ipvs_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_IRQ
pkglib_LTLIBRARIES += irq.la
irq_la_SOURCES = src/irq.c
irq_la_LDFLAGS = $(PLUGIN_LDFLAGS)
irq_la_LIBADD = libignorelist.la
endif

if BUILD_PLUGIN_JAVA
pkglib_LTLIBRARIES += java.la
java_la_SOURCES = src/java.c
java_la_CPPFLAGS = $(AM_CPPFLAGS) $(JAVA_CPPFLAGS)
java_la_CFLAGS = $(AM_CFLAGS) $(JAVA_CFLAGS)
java_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(JAVA_LDFLAGS)
java_la_LIBADD = $(JAVA_LIBS)
endif

if BUILD_PLUGIN_LOAD
pkglib_LTLIBRARIES += load.la
load_la_SOURCES = src/load.c
load_la_CFLAGS = $(AM_CFLAGS)
load_la_LDFLAGS = $(PLUGIN_LDFLAGS)
load_la_LIBADD =
if BUILD_WITH_LIBSTATGRAB
load_la_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)
load_la_LIBADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
endif # BUILD_WITH_LIBSTATGRAB
if BUILD_WITH_PERFSTAT
load_la_LIBADD += -lperfstat
endif
endif # BUILD_PLUGIN_LOAD

if BUILD_PLUGIN_LOGFILE
pkglib_LTLIBRARIES += logfile.la
logfile_la_SOURCES = src/logfile.c
logfile_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_LOG_LOGSTASH
pkglib_LTLIBRARIES += log_logstash.la
log_logstash_la_SOURCES = src/log_logstash.c
log_logstash_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBYAJL_CPPFLAGS)
log_logstash_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBYAJL_LDFLAGS)
log_logstash_la_LIBADD = $(BUILD_WITH_LIBYAJL_LIBS)
endif

if BUILD_PLUGIN_LPAR
pkglib_LTLIBRARIES += lpar.la
lpar_la_SOURCES = src/lpar.c
lpar_la_LDFLAGS = $(PLUGIN_LDFLAGS)
lpar_la_LIBADD = -lperfstat
endif

if BUILD_PLUGIN_LUA
pkglib_LTLIBRARIES += lua.la
lua_la_SOURCES = \
	src/lua.c \
	src/utils_lua.c \
	src/utils_lua.h
lua_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBLUA_CFLAGS)
lua_la_LDFLAGS = $(PLUGIN_LDFLAGS)
lua_la_LIBADD = $(BUILD_WITH_LIBLUA_LIBS)
endif

if BUILD_PLUGIN_LVM
pkglib_LTLIBRARIES += lvm.la
lvm_la_SOURCES = src/lvm.c
lvm_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBLVM2APP_CPPFLAGS)
lvm_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBLVM2APP_LDFLAGS)
lvm_la_LIBADD = $(BUILD_WITH_LIBLVM2APP_LIBS)
endif

if BUILD_PLUGIN_MADWIFI
pkglib_LTLIBRARIES += madwifi.la
madwifi_la_SOURCES = \
	src/madwifi.c \
	src/madwifi.h
madwifi_la_LDFLAGS = $(PLUGIN_LDFLAGS)
madwifi_la_LIBADD = libignorelist.la
endif

if BUILD_PLUGIN_MATCH_EMPTY_COUNTER
pkglib_LTLIBRARIES += match_empty_counter.la
match_empty_counter_la_SOURCES = src/match_empty_counter.c
match_empty_counter_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_MATCH_HASHED
pkglib_LTLIBRARIES += match_hashed.la
match_hashed_la_SOURCES = src/match_hashed.c
match_hashed_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_MATCH_REGEX
pkglib_LTLIBRARIES += match_regex.la
match_regex_la_SOURCES = src/match_regex.c
match_regex_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_MATCH_TIMEDIFF
pkglib_LTLIBRARIES += match_timediff.la
match_timediff_la_SOURCES = src/match_timediff.c
match_timediff_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_MATCH_VALUE
pkglib_LTLIBRARIES += match_value.la
match_value_la_SOURCES = src/match_value.c
match_value_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_MBMON
pkglib_LTLIBRARIES += mbmon.la
mbmon_la_SOURCES = src/mbmon.c
mbmon_la_LDFLAGS = $(PLUGIN_LDFLAGS)
mbmon_la_LIBADD =
if BUILD_WITH_LIBSOCKET
mbmon_la_LIBADD += -lsocket
endif
endif

if BUILD_PLUGIN_MCELOG
pkglib_LTLIBRARIES += mcelog.la
mcelog_la_SOURCES = src/mcelog.c
mcelog_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_MD
pkglib_LTLIBRARIES += md.la
md_la_SOURCES = src/md.c
md_la_LDFLAGS = $(PLUGIN_LDFLAGS)
md_la_LIBADD = libignorelist.la
endif

if BUILD_PLUGIN_MEMCACHEC
pkglib_LTLIBRARIES += memcachec.la
memcachec_la_SOURCES = \
	src/memcachec.c \
	src/utils_match.c \
	src/utils_match.h
memcachec_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBMEMCACHED_CPPFLAGS)
memcachec_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBMEMCACHED_LDFLAGS)
memcachec_la_LIBADD = liblatency.la $(BUILD_WITH_LIBMEMCACHED_LIBS)
endif

if BUILD_PLUGIN_MEMCACHED
pkglib_LTLIBRARIES += memcached.la
memcached_la_SOURCES = src/memcached.c
memcached_la_LDFLAGS = $(PLUGIN_LDFLAGS)
memcached_la_LIBADD =
if BUILD_WITH_LIBSOCKET
memcached_la_LIBADD += -lsocket
endif
endif

if BUILD_PLUGIN_MEMORY
pkglib_LTLIBRARIES += memory.la
memory_la_SOURCES = src/memory.c
memory_la_CFLAGS = $(AM_CFLAGS)
memory_la_LDFLAGS = $(PLUGIN_LDFLAGS)
memory_la_LIBADD =
if BUILD_WITH_LIBKSTAT
memory_la_LIBADD += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
memory_la_LIBADD += -ldevinfo
endif
if BUILD_WITH_LIBSTATGRAB
memory_la_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)
memory_la_LIBADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
endif
if BUILD_WITH_PERFSTAT
memory_la_LIBADD += -lperfstat
endif
endif

if BUILD_PLUGIN_MIC
pkglib_LTLIBRARIES += mic.la
mic_la_SOURCES = src/mic.c
mic_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_MIC_CPPFLAGS)
mic_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_MIC_LDFLAGS)
mic_la_LIBADD = libignorelist.la $(BUILD_WITH_MIC_LIBS)
endif

if BUILD_PLUGIN_MODBUS
pkglib_LTLIBRARIES += modbus.la
modbus_la_SOURCES = src/modbus.c
modbus_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBMODBUS_CFLAGS)
modbus_la_LDFLAGS = $(PLUGIN_LDFLAGS)
modbus_la_LIBADD = $(BUILD_WITH_LIBMODBUS_LIBS)
endif

if BUILD_PLUGIN_MQTT
pkglib_LTLIBRARIES += mqtt.la
mqtt_la_SOURCES = src/mqtt.c
mqtt_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBMOSQUITTO_CPPFLAGS)
mqtt_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBMOSQUITTO_LDFLAGS)
mqtt_la_LIBADD = $(BUILD_WITH_LIBMOSQUITTO_LIBS)
endif

if BUILD_PLUGIN_MULTIMETER
pkglib_LTLIBRARIES += multimeter.la
multimeter_la_SOURCES = src/multimeter.c
multimeter_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_MYSQL
pkglib_LTLIBRARIES += mysql.la
mysql_la_SOURCES = src/mysql.c
mysql_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBMYSQL_CFLAGS)
mysql_la_LDFLAGS = $(PLUGIN_LDFLAGS)
mysql_la_LIBADD = $(BUILD_WITH_LIBMYSQL_LIBS)
endif

if BUILD_PLUGIN_NETAPP
pkglib_LTLIBRARIES += netapp.la
netapp_la_SOURCES = src/netapp.c
netapp_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBNETAPP_CPPFLAGS)
netapp_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(LIBNETAPP_LDFLAGS)
netapp_la_LIBADD = libignorelist.la $(LIBNETAPP_LIBS)
endif

if BUILD_PLUGIN_NETLINK
pkglib_LTLIBRARIES += netlink.la
netlink_la_SOURCES = src/netlink.c
netlink_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBMNL_CFLAGS)
netlink_la_LDFLAGS = $(PLUGIN_LDFLAGS)
netlink_la_LIBADD = $(BUILD_WITH_LIBMNL_LIBS)
endif

if BUILD_PLUGIN_NETWORK
pkglib_LTLIBRARIES += network.la
network_la_SOURCES = \
	src/network.c \
	src/network.h \
	src/utils_fbhash.c \
	src/utils_fbhash.h
network_la_CPPFLAGS = $(AM_CPPFLAGS)
network_la_LDFLAGS = $(PLUGIN_LDFLAGS)
network_la_LIBADD =
if BUILD_WITH_LIBSOCKET
network_la_LIBADD += -lsocket
endif
if BUILD_WITH_LIBGCRYPT
network_la_CPPFLAGS += $(GCRYPT_CPPFLAGS)
network_la_LDFLAGS += $(GCRYPT_LDFLAGS)
network_la_LIBADD += $(GCRYPT_LIBS)
endif
endif

if BUILD_PLUGIN_NFS
pkglib_LTLIBRARIES += nfs.la
nfs_la_SOURCES = src/nfs.c
nfs_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_NGINX
pkglib_LTLIBRARIES += nginx.la
nginx_la_SOURCES = src/nginx.c
nginx_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBCURL_CFLAGS)
nginx_la_LDFLAGS = $(PLUGIN_LDFLAGS)
nginx_la_LIBADD = $(BUILD_WITH_LIBCURL_LIBS)
endif

if BUILD_PLUGIN_NOTIFY_DESKTOP
pkglib_LTLIBRARIES += notify_desktop.la
notify_desktop_la_SOURCES = src/notify_desktop.c
notify_desktop_la_CFLAGS = $(AM_CFLAGS) $(LIBNOTIFY_CFLAGS)
notify_desktop_la_LDFLAGS = $(PLUGIN_LDFLAGS)
notify_desktop_la_LIBADD = $(LIBNOTIFY_LIBS)
endif

if BUILD_PLUGIN_NOTIFY_EMAIL
pkglib_LTLIBRARIES += notify_email.la
notify_email_la_SOURCES = src/notify_email.c
notify_email_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBESMTP_CPPFLAGS)
notify_email_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBESMTP_LDFLAGS)
notify_email_la_LIBADD = $(BUILD_WITH_LIBESMTP_LIBS)
endif

if BUILD_PLUGIN_NOTIFY_NAGIOS
pkglib_LTLIBRARIES += notify_nagios.la
notify_nagios_la_SOURCES = src/notify_nagios.c
notify_nagios_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_NTPD
pkglib_LTLIBRARIES += ntpd.la
ntpd_la_SOURCES = src/ntpd.c
ntpd_la_LDFLAGS = $(PLUGIN_LDFLAGS)
ntpd_la_LIBADD =
if BUILD_WITH_LIBSOCKET
ntpd_la_LIBADD += -lsocket
endif
endif

if BUILD_PLUGIN_NUMA
pkglib_LTLIBRARIES += numa.la
numa_la_SOURCES = src/numa.c
numa_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_NUT
pkglib_LTLIBRARIES += nut.la
nut_la_SOURCES = src/nut.c
nut_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBUPSCLIENT_CFLAGS)
nut_la_LDFLAGS = $(PLUGIN_LDFLAGS)
nut_la_LIBADD = $(BUILD_WITH_LIBUPSCLIENT_LIBS)
endif

if BUILD_PLUGIN_OLSRD
pkglib_LTLIBRARIES += olsrd.la
olsrd_la_SOURCES = src/olsrd.c
olsrd_la_LDFLAGS = $(PLUGIN_LDFLAGS)
olsrd_la_LIBADD =
if BUILD_WITH_LIBSOCKET
olsrd_la_LIBADD += -lsocket
endif
endif

if BUILD_PLUGIN_ONEWIRE
pkglib_LTLIBRARIES += onewire.la
onewire_la_SOURCES = src/onewire.c
onewire_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBOWCAPI_CPPFLAGS)
onewire_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBOWCAPI_LDFLAGS)
onewire_la_LIBADD = libignorelist.la $(BUILD_WITH_LIBOWCAPI_LIBS)
endif

if BUILD_PLUGIN_OPENLDAP
pkglib_LTLIBRARIES += openldap.la
openldap_la_SOURCES = src/openldap.c
openldap_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBLDAP_CPPFLAGS)
openldap_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBLDAP_LDFLAGS)
openldap_la_LIBADD = -lldap
endif

if BUILD_PLUGIN_OPENVPN
pkglib_LTLIBRARIES += openvpn.la
openvpn_la_SOURCES = src/openvpn.c
openvpn_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_ORACLE
pkglib_LTLIBRARIES += oracle.la
oracle_la_SOURCES = \
	src/oracle.c \
	src/utils_db_query.c \
	src/utils_db_query.h
oracle_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_ORACLE_CPPFLAGS)
oracle_la_LIBADD = $(BUILD_WITH_ORACLE_LIBS)
oracle_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_OVS_EVENTS
pkglib_LTLIBRARIES += ovs_events.la
ovs_events_la_SOURCES = \
	src/ovs_events.c \
	src/utils_ovs.c \
	src/utils_ovs.h
ovs_events_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBYAJL_CPPFLAGS)
ovs_events_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBYAJL_LDFLAGS)
ovs_events_la_LIBADD = $(BUILD_WITH_LIBYAJL_LIBS)
endif

if BUILD_PLUGIN_OVS_STATS
pkglib_LTLIBRARIES += ovs_stats.la
ovs_stats_la_SOURCES = \
	src/ovs_stats.c \
	src/utils_ovs.c \
	src/utils_ovs.h
ovs_stats_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBYAJL_CPPFLAGS)
ovs_stats_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBYAJL_LDFLAGS)
ovs_stats_la_LIBADD = $(BUILD_WITH_LIBYAJL_LIBS)
endif

if BUILD_PLUGIN_PERL
pkglib_LTLIBRARIES += perl.la
perl_la_SOURCES = src/perl.c
# Despite C99 providing the "bool" type thru stdbool.h, Perl defines its own
# version of that type if HAS_BOOL is not defined... *sigh*
perl_la_CPPFLAGS = $(AM_CPPFLAGS) -DHAS_BOOL=1
# Despite off_t being 64 bit wide on 64 bit platforms, Perl insist on using
# off64_t which is only exposed when _LARGEFILE64_SOURCE is defined... *sigh*
# On older platforms we also need _REENTRANT. _GNU_SOURCE sets both of these.
perl_la_CPPFLAGS += -D_GNU_SOURCE
perl_la_CFLAGS = $(AM_CFLAGS) \
	$(PERL_CFLAGS) \
	-DXS_VERSION=\"$(VERSION)\" -DVERSION=\"$(VERSION)\"
perl_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(PERL_LDFLAGS)
perl_la_LIBADD = $(PERL_LIBS)
endif

if BUILD_PLUGIN_PF
pkglib_LTLIBRARIES += pf.la
pf_la_SOURCES = src/pf.c
pf_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_PINBA
pkglib_LTLIBRARIES += pinba.la
pinba_la_SOURCES = src/pinba.c
nodist_pinba_la_SOURCES = \
	src/pinba.pb-c.c \
	src/pinba.pb-c.h
pinba_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBPROTOBUF_C_CPPFLAGS)
pinba_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBPROTOBUF_C_LDFLAGS)
pinba_la_LIBADD = $(BUILD_WITH_LIBPROTOBUF_C_LIBS)
endif

if BUILD_PLUGIN_PING
pkglib_LTLIBRARIES += ping.la
ping_la_SOURCES = src/ping.c
ping_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBOPING_CPPFLAGS)
ping_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBOPING_LDFLAGS)
ping_la_LIBADD = -loping -lm
endif

if BUILD_PLUGIN_POSTGRESQL
pkglib_LTLIBRARIES += postgresql.la
postgresql_la_SOURCES = \
	src/postgresql.c \
	src/utils_db_query.c \
	src/utils_db_query.h
postgresql_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBPQ_CPPFLAGS)
postgresql_la_LDFLAGS = $(PLUGIN_LDFLAGS) \
	$(BUILD_WITH_LIBPQ_LDFLAGS)
postgresql_la_LIBADD = $(BUILD_WITH_LIBPQ_LIBS)
endif

if BUILD_PLUGIN_POWERDNS
pkglib_LTLIBRARIES += powerdns.la
powerdns_la_SOURCES = src/powerdns.c
powerdns_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_PYTHON
pkglib_LTLIBRARIES += python.la
python_la_SOURCES = \
	src/python.c \
	src/pyconfig.c \
	src/pyvalues.c \
	src/cpython.h
python_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBPYTHON_CPPFLAGS)
python_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(LIBPYTHON_LDFLAGS)
endif

if HAVE_LIBMNL
noinst_LTLIBRARIES += libtaskstats.la
libtaskstats_la_SOURCES = \
	src/utils_taskstats.c \
	src/utils_taskstats.h
libtaskstats_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBMNL_CFLAGS)
libtaskstats_la_LIBADD = $(BUILD_WITH_LIBMNL_LIBS)
endif

if BUILD_PLUGIN_PROCESSES
pkglib_LTLIBRARIES += processes.la
processes_la_SOURCES = src/processes.c
processes_la_CPPFLAGS = $(AM_CPPFLAGS)
processes_la_LDFLAGS = $(PLUGIN_LDFLAGS)
processes_la_LIBADD =
if BUILD_WITH_LIBKVM_GETPROCS
processes_la_LIBADD += -lkvm
endif
if HAVE_LIBMNL
processes_la_CPPFLAGS += -DHAVE_LIBTASKSTATS=1
processes_la_LIBADD += libtaskstats.la
endif
endif

if BUILD_PLUGIN_PROTOCOLS
pkglib_LTLIBRARIES += protocols.la
protocols_la_SOURCES = src/protocols.c
protocols_la_LDFLAGS = $(PLUGIN_LDFLAGS)
protocols_la_LIBADD = libignorelist.la
endif

if BUILD_PLUGIN_REDIS
pkglib_LTLIBRARIES += redis.la
redis_la_SOURCES = src/redis.c
redis_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBHIREDIS_CPPFLAGS)
redis_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBHIREDIS_LDFLAGS)
redis_la_LIBADD = -lhiredis
endif

if BUILD_PLUGIN_ROUTEROS
pkglib_LTLIBRARIES += routeros.la
routeros_la_SOURCES = src/routeros.c
routeros_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBROUTEROS_CPPFLAGS)
routeros_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBROUTEROS_LDFLAGS)
routeros_la_LIBADD = -lrouteros
endif

if BUILD_PLUGIN_RRDCACHED
pkglib_LTLIBRARIES += rrdcached.la
rrdcached_la_SOURCES = \
	src/rrdcached.c \
	src/utils_rrdcreate.c \
	src/utils_rrdcreate.h
rrdcached_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBRRD_CFLAGS)
rrdcached_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBRRD_LDFLAGS)
rrdcached_la_LIBADD = $(BUILD_WITH_LIBRRD_LIBS)
endif

if BUILD_PLUGIN_RRDTOOL
pkglib_LTLIBRARIES += rrdtool.la
rrdtool_la_SOURCES = \
	src/rrdtool.c \
	src/utils_rrdcreate.c \
	src/utils_rrdcreate.h
rrdtool_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBRRD_CFLAGS)
rrdtool_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBRRD_LDFLAGS)
rrdtool_la_LIBADD = $(BUILD_WITH_LIBRRD_LIBS)
endif

if BUILD_PLUGIN_SENSORS
pkglib_LTLIBRARIES += sensors.la
sensors_la_SOURCES = src/sensors.c
sensors_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBSENSORS_CPPFLAGS)
sensors_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBSENSORS_LDFLAGS)
sensors_la_LIBADD = libignorelist.la $(BUILD_WITH_LIBSENSORS_LIBS)
endif

if BUILD_PLUGIN_SERIAL
pkglib_LTLIBRARIES += serial.la
serial_la_SOURCES = src/serial.c
serial_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_SIGROK
pkglib_LTLIBRARIES += sigrok.la
sigrok_la_SOURCES = src/sigrok.c
sigrok_la_CFLAGS = $(AM_CFLAGS) $(LIBSIGROK_CFLAGS)
sigrok_la_LDFLAGS = $(PLUGIN_LDFLAGS)
sigrok_la_LIBADD = $(LIBSIGROK_LIBS)
endif

if BUILD_PLUGIN_SMART
if BUILD_WITH_LIBUDEV
pkglib_LTLIBRARIES += smart.la
smart_la_SOURCES = src/smart.c
smart_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBATASMART_CPPFLAGS) $(BUILD_WITH_LIBUDEV_CPPFLAGS)
smart_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBATASMART_LDFLAGS) $(BUILD_WITH_LIBUDEV_LDFLAGS)
smart_la_LIBADD = libignorelist.la $(BUILD_WITH_LIBATASMART_LIBS) $(BUILD_WITH_LIBUDEV_LIBS)
endif
endif

if BUILD_PLUGIN_SNMP
pkglib_LTLIBRARIES += snmp.la
snmp_la_SOURCES = src/snmp.c
snmp_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBNETSNMP_CPPFLAGS)
snmp_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBNETSNMP_LDFLAGS)
snmp_la_LIBADD = $(BUILD_WITH_LIBNETSNMP_LIBS)
endif

if BUILD_PLUGIN_SNMP_AGENT
pkglib_LTLIBRARIES += snmp_agent.la
snmp_agent_la_SOURCES = src/snmp_agent.c
snmp_agent_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBNETSNMPAGENT_CPPFLAGS)
snmp_agent_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBNETSNMPAGENT_LDFLAGS)
snmp_agent_la_LIBADD = $(BUILD_WITH_LIBNETSNMPAGENT_LIBS)
endif

if BUILD_PLUGIN_STATSD
pkglib_LTLIBRARIES += statsd.la
statsd_la_SOURCES = src/statsd.c
statsd_la_LDFLAGS = $(PLUGIN_LDFLAGS)
statsd_la_LIBADD = liblatency.la
endif

if BUILD_PLUGIN_SWAP
pkglib_LTLIBRARIES += swap.la
swap_la_SOURCES = src/swap.c
swap_la_CFLAGS = $(AM_CFLAGS)
swap_la_LDFLAGS = $(PLUGIN_LDFLAGS)
swap_la_LIBADD =
if BUILD_WITH_LIBKSTAT
swap_la_LIBADD += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
swap_la_LIBADD += -ldevinfo
endif
if BUILD_WITH_LIBKVM_GETSWAPINFO
swap_la_LIBADD += -lkvm
endif
if BUILD_WITH_LIBSTATGRAB
swap_la_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)
swap_la_LIBADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
endif
if BUILD_WITH_PERFSTAT
swap_la_LIBADD += -lperfstat
endif

endif

if BUILD_PLUGIN_SYNPROXY
pkglib_LTLIBRARIES += synproxy.la
synproxy_la_SOURCES = src/synproxy.c
synproxy_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_SYSLOG
pkglib_LTLIBRARIES += syslog.la
syslog_la_SOURCES = src/syslog.c
syslog_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TABLE
pkglib_LTLIBRARIES += table.la
table_la_SOURCES = src/table.c
table_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TAIL
pkglib_LTLIBRARIES += tail.la
tail_la_SOURCES = \
	src/tail.c \
	src/utils_match.c \
	src/utils_match.h \
	src/utils_tail.c \
	src/utils_tail.h \
	src/utils_tail_match.c \
	src/utils_tail_match.h
tail_la_LDFLAGS = $(PLUGIN_LDFLAGS)
tail_la_LIBADD = liblatency.la
endif

if BUILD_PLUGIN_TAIL_CSV
pkglib_LTLIBRARIES += tail_csv.la
tail_csv_la_SOURCES = \
	src/tail_csv.c \
	src/utils_tail.c \
	src/utils_tail.h
tail_csv_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TAPE
pkglib_LTLIBRARIES += tape.la
tape_la_SOURCES = src/tape.c
tape_la_LDFLAGS = $(PLUGIN_LDFLAGS)
tape_la_LIBADD = -lkstat -ldevinfo
endif

if BUILD_PLUGIN_TARGET_NOTIFICATION
pkglib_LTLIBRARIES += target_notification.la
target_notification_la_SOURCES = src/target_notification.c
target_notification_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TARGET_REPLACE
pkglib_LTLIBRARIES += target_replace.la
target_replace_la_SOURCES = src/target_replace.c
target_replace_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TARGET_SCALE
pkglib_LTLIBRARIES += target_scale.la
target_scale_la_SOURCES = src/target_scale.c
target_scale_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TARGET_SET
pkglib_LTLIBRARIES += target_set.la
target_set_la_SOURCES = src/target_set.c
target_set_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TARGET_V5UPGRADE
pkglib_LTLIBRARIES += target_v5upgrade.la
target_v5upgrade_la_SOURCES = src/target_v5upgrade.c
target_v5upgrade_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TCPCONNS
pkglib_LTLIBRARIES += tcpconns.la
tcpconns_la_SOURCES = src/tcpconns.c
tcpconns_la_LDFLAGS = $(PLUGIN_LDFLAGS)
tcpconns_la_LIBADD =
if BUILD_WITH_LIBKVM_NLIST
tcpconns_la_LIBADD += -lkvm
endif
endif

if BUILD_PLUGIN_TEAMSPEAK2
pkglib_LTLIBRARIES += teamspeak2.la
teamspeak2_la_SOURCES = src/teamspeak2.c
teamspeak2_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TED
pkglib_LTLIBRARIES += ted.la
ted_la_SOURCES = src/ted.c
ted_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_THERMAL
pkglib_LTLIBRARIES += thermal.la
thermal_la_SOURCES = src/thermal.c
thermal_la_LDFLAGS = $(PLUGIN_LDFLAGS)
thermal_la_LIBADD = libignorelist.la
endif

if BUILD_PLUGIN_THRESHOLD
pkglib_LTLIBRARIES += threshold.la
threshold_la_SOURCES = src/threshold.c
threshold_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TOKYOTYRANT
pkglib_LTLIBRARIES += tokyotyrant.la
tokyotyrant_la_SOURCES = src/tokyotyrant.c
tokyotyrant_la_CPPFLAGS  = $(AM_CPPFLAGS) $(BUILD_WITH_LIBTOKYOTYRANT_CPPFLAGS)
tokyotyrant_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBTOKYOTYRANT_LDFLAGS)
tokyotyrant_la_LIBADD  = $(BUILD_WITH_LIBTOKYOTYRANT_LIBS)
if BUILD_WITH_LIBSOCKET
tokyotyrant_la_LIBADD += -lsocket
endif
endif

if BUILD_PLUGIN_TURBOSTAT
pkglib_LTLIBRARIES += turbostat.la
turbostat_la_SOURCES = \
	src/turbostat.c \
	src/msr-index.h
turbostat_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_UNIXSOCK
pkglib_LTLIBRARIES += unixsock.la
unixsock_la_SOURCES = src/unixsock.c
unixsock_la_LDFLAGS = $(PLUGIN_LDFLAGS)
unixsock_la_LIBADD = libcmds.la
endif

if BUILD_PLUGIN_UPTIME
pkglib_LTLIBRARIES += uptime.la
uptime_la_SOURCES = src/uptime.c
uptime_la_CFLAGS = $(AM_CFLAGS)
uptime_la_LDFLAGS = $(PLUGIN_LDFLAGS)
uptime_la_LIBADD =
if BUILD_WITH_LIBKSTAT
uptime_la_LIBADD += -lkstat
endif
if BUILD_WITH_PERFSTAT
uptime_la_LIBADD += -lperfstat
endif
endif

if BUILD_PLUGIN_USERS
pkglib_LTLIBRARIES += users.la
users_la_SOURCES = src/users.c
users_la_CFLAGS = $(AM_CFLAGS)
users_la_LDFLAGS = $(PLUGIN_LDFLAGS)
users_la_LIBADD =
if BUILD_WITH_LIBSTATGRAB
users_la_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)
users_la_LIBADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
endif
endif

if BUILD_PLUGIN_UUID
pkglib_LTLIBRARIES += uuid.la
uuid_la_SOURCES = src/uuid.c
uuid_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_VARNISH
pkglib_LTLIBRARIES += varnish.la
varnish_la_SOURCES = src/varnish.c
varnish_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBVARNISH_CFLAGS)
varnish_la_LDFLAGS = $(PLUGIN_LDFLAGS)
varnish_la_LIBADD = $(BUILD_WITH_LIBVARNISH_LIBS)
endif

if BUILD_PLUGIN_VIRT
pkglib_LTLIBRARIES += virt.la
virt_la_SOURCES = src/virt.c
virt_la_CFLAGS = $(AM_CFLAGS) \
	$(BUILD_WITH_LIBVIRT_CFLAGS) $(BUILD_WITH_LIBXML2_CFLAGS)
virt_la_LDFLAGS = $(PLUGIN_LDFLAGS)
virt_la_LIBADD = libignorelist.la $(BUILD_WITH_LIBVIRT_LIBS) $(BUILD_WITH_LIBXML2_LIBS)

# TODO: enable once we support only modern libvirts which depends on libnl-3
# the libvirt on wheezy is linked in libnl v1, and there is a small leak here,
# triggered by the library initialization. There are no means to avoid it,
# and libvirt switched to libnl3 anyway
#test_plugin_virt_SOURCES = src/virt_test.c
#test_plugin_virt_CPPFLAGS = $(AM_CPPFLAGS) \
#	$(BUILD_WITH_LIBVIRT_CFLAGS) $(BUILD_WITH_LIBXML2_CFLAGS)
#test_plugin_virt_LDFLAGS = $(PLUGIN_LDFLAGS)
#test_plugin_virt_LDADD = libplugin_mock.la \
#	$(BUILD_WITH_LIBVIRT_LIBS) $(BUILD_WITH_LIBXML2_LIBS)
#check_PROGRAMS += test_plugin_virt
#TESTS += test_plugin_virt
endif

if BUILD_PLUGIN_VMEM
pkglib_LTLIBRARIES += vmem.la
vmem_la_SOURCES = src/vmem.c
vmem_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_VSERVER
pkglib_LTLIBRARIES += vserver.la
vserver_la_SOURCES = src/vserver.c
vserver_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_WIRELESS
pkglib_LTLIBRARIES += wireless.la
wireless_la_SOURCES = src/wireless.c
wireless_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_WRITE_GRAPHITE
pkglib_LTLIBRARIES += write_graphite.la
write_graphite_la_SOURCES = src/write_graphite.c
write_graphite_la_LDFLAGS = $(PLUGIN_LDFLAGS)
write_graphite_la_LIBADD = libformat_graphite.la
endif

if BUILD_PLUGIN_WRITE_HTTP
pkglib_LTLIBRARIES += write_http.la
write_http_la_SOURCES = \
	src/write_http.c \
	src/utils_format_kairosdb.c \
	src/utils_format_kairosdb.h
write_http_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBCURL_CFLAGS)
write_http_la_LDFLAGS = $(PLUGIN_LDFLAGS)
write_http_la_LIBADD = libformat_json.la $(BUILD_WITH_LIBCURL_LIBS)
endif

if BUILD_PLUGIN_WRITE_KAFKA
pkglib_LTLIBRARIES += write_kafka.la
write_kafka_la_SOURCES = src/write_kafka.c
write_kafka_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBRDKAFKA_CPPFLAGS)
write_kafka_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBRDKAFKA_LDFLAGS)
write_kafka_la_LIBADD = \
	libcmds.la \
	libformat_graphite.la \
	libformat_json.la \
	$(BUILD_WITH_LIBRDKAFKA_LIBS)
endif

if BUILD_PLUGIN_WRITE_LOG
pkglib_LTLIBRARIES += write_log.la
write_log_la_SOURCES = src/write_log.c
write_log_la_LDFLAGS = $(PLUGIN_LDFLAGS)
write_log_la_LIBADD = libformat_graphite.la libformat_json.la
endif

if BUILD_PLUGIN_WRITE_MONGODB
pkglib_LTLIBRARIES += write_mongodb.la
write_mongodb_la_SOURCES = src/write_mongodb.c
write_mongodb_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBMONGOC_CFLAGS)
write_mongodb_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBMONGOC_LDFLAGS)
endif

if BUILD_PLUGIN_WRITE_PROMETHEUS
pkglib_LTLIBRARIES += write_prometheus.la
write_prometheus_la_SOURCES = src/write_prometheus.c
nodist_write_prometheus_la_SOURCES = \
	prometheus.pb-c.c \
	prometheus.pb-c.h
write_prometheus_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBPROTOBUF_C_CPPFLAGS) $(BUILD_WITH_LIBMICROHTTPD_CPPFLAGS)
write_prometheus_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBPROTOBUF_C_LDFLAGS) $(BUILD_WITH_LIBMICROHTTPD_LDFLAGS)
write_prometheus_la_LIBADD = $(BUILD_WITH_LIBPROTOBUF_C_LIBS) $(BUILD_WITH_LIBMICROHTTPD_LIBS)
endif

if BUILD_PLUGIN_WRITE_REDIS
pkglib_LTLIBRARIES += write_redis.la
write_redis_la_SOURCES = src/write_redis.c
write_redis_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBHIREDIS_CPPFLAGS)
write_redis_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBHIREDIS_LDFLAGS)
write_redis_la_LIBADD = -lhiredis
endif

if BUILD_PLUGIN_WRITE_RIEMANN
pkglib_LTLIBRARIES += write_riemann.la
write_riemann_la_SOURCES = \
	src/write_riemann.c \
	src/write_riemann_threshold.c \
	src/write_riemann_threshold.h
write_riemann_la_CFLAGS = $(AM_CFLAGS) $(LIBRIEMANN_CLIENT_CFLAGS)
write_riemann_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(LIBRIEMANN_CLIENT_LIBS)
endif

if BUILD_PLUGIN_WRITE_SENSU
pkglib_LTLIBRARIES += write_sensu.la
write_sensu_la_SOURCES = src/write_sensu.c
write_sensu_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_WRITE_TSDB
pkglib_LTLIBRARIES += write_tsdb.la
write_tsdb_la_SOURCES = src/write_tsdb.c
write_tsdb_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_XENCPU
pkglib_LTLIBRARIES += xencpu.la
xencpu_la_SOURCES = src/xencpu.c
xencpu_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBXENCTL_CPPFLAGS)
xencpu_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(LIBXENCTL_LDFLAGS)
xencpu_la_LIBADD = -lxenctrl
endif

if BUILD_PLUGIN_XMMS
pkglib_LTLIBRARIES += xmms.la
xmms_la_SOURCES = src/xmms.c
xmms_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBXMMS_CFLAGS)
xmms_la_LDFLAGS = $(PLUGIN_LDFLAGS)
xmms_la_LIBADD = $(BUILD_WITH_LIBXMMS_LIBS)
endif

if BUILD_PLUGIN_ZFS_ARC
pkglib_LTLIBRARIES += zfs_arc.la
zfs_arc_la_SOURCES = src/zfs_arc.c
zfs_arc_la_LDFLAGS = $(PLUGIN_LDFLAGS)
if BUILD_FREEBSD
zfs_arc_la_LIBADD = -lm
endif
if BUILD_SOLARIS
zfs_arc_la_LIBADD = -lkstat
endif
endif

if BUILD_PLUGIN_ZOOKEEPER
pkglib_LTLIBRARIES += zookeeper.la
zookeeper_la_SOURCES = src/zookeeper.c
zookeeper_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_ZONE
pkglib_LTLIBRARIES += zone.la
zone_la_SOURCES = src/zone.c
zone_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

AM_V_POD2MAN_C = $(am__v_POD2MAN_C_@AM_V@)
am__v_POD2MAN_C_ = $(am__v_POD2MAN_C_@AM_DEFAULT_V@)
am__v_POD2MAN_C_0 = @echo "  POD2MAN " $@;
am__v_POD2MAN_C_1 =

.pod.1:
	$(AM_V_POD2MAN_C)pod2man --release=$(VERSION) --center=$(PACKAGE) $< \
		>.pod2man.tmp.$$$$ 2>/dev/null && mv -f .pod2man.tmp.$$$$ $@ || true
	@if grep '\<POD ERRORS\>' $@ >/dev/null 2>&1; \
	then \
		echo "$@ has some POD errors!"; false; \
	fi

.pod.5:
	$(AM_V_POD2MAN_C)pod2man --section=5 --release=$(VERSION) --center=$(PACKAGE) $< \
		>.pod2man.tmp.$$$$ 2>/dev/null && mv -f .pod2man.tmp.$$$$ $@ || true
	@if grep '\<POD ERRORS\>' $@ >/dev/null 2>&1; \
	then \
		echo "$@ has some POD errors!"; false; \
	fi

V_PROTOC = $(v_protoc_@AM_V@)
v_protoc_ = $(v_protoc_@AM_DEFAULT_V@)
v_protoc_0 = @echo "  PROTOC  " $@;

AM_V_PROTOC_C = $(am__v_PROTOC_C_@AM_V@)
am__v_PROTOC_C_ = $(am__v_PROTOC_C_@AM_DEFAULT_V@)
am__v_PROTOC_C_0 = @echo "  PROTOC-C" $@;
am__v_PROTOC_C_1 =

# Protocol buffer for the "pinba" plugin.
if BUILD_PLUGIN_PINBA
BUILT_SOURCES += src/pinba.pb-c.c src/pinba.pb-c.h

src/pinba.pb-c.c src/pinba.pb-c.h: $(srcdir)/src/pinba.proto
	$(AM_V_PROTOC_C)$(PROTOC_C) -I$(srcdir) --c_out . $(srcdir)/src/pinba.proto
endif

# Protocol buffer for the "write_prometheus" plugin.
if BUILD_PLUGIN_WRITE_PROMETHEUS
BUILT_SOURCES += prometheus.pb-c.c prometheus.pb-c.h

prometheus.pb-c.c prometheus.pb-c.h: $(srcdir)/proto/prometheus.proto
	$(AM_V_PROTOC_C)$(PROTOC_C) -I$(srcdir)/proto --c_out=$(builddir) $(srcdir)/proto/prometheus.proto
endif

if HAVE_PROTOC3
if HAVE_GRPC_CPP
BUILT_SOURCES += collectd.grpc.pb.cc collectd.pb.cc types.pb.cc

collectd.grpc.pb.cc: $(srcdir)/proto/collectd.proto $(srcdir)/proto/types.proto
	$(V_PROTOC)$(PROTOC) -I$(srcdir)/proto \
		--grpc_out=$(builddir) --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) $<

collectd.pb.cc: $(srcdir)/proto/collectd.proto $(srcdir)/proto/types.proto
	$(V_PROTOC)$(PROTOC) -I$(srcdir)/proto --cpp_out=$(builddir) $<

types.pb.cc: $(srcdir)/proto/types.proto
	$(V_PROTOC)$(PROTOC) -I$(srcdir)/proto --cpp_out=$(builddir) $<
endif
endif

install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/run
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/lib/$(PACKAGE_NAME)
	$(mkinstalldirs) $(DESTDIR)$(localstatedir)/log
	$(mkinstalldirs) $(DESTDIR)$(sysconfdir)
	if test -e $(DESTDIR)$(sysconfdir)/collectd.conf; \
	then \
		$(INSTALL) -m 0640 $(srcdir)/src/collectd.conf $(DESTDIR)$(sysconfdir)/collectd.conf.pkg-orig; \
	else \
		$(INSTALL) -m 0640 $(srcdir)/src/collectd.conf $(DESTDIR)$(sysconfdir)/collectd.conf; \
	fi; \
	$(mkinstalldirs) $(DESTDIR)$(pkgdatadir)
	$(INSTALL) -m 0644 $(srcdir)/src/types.db $(DESTDIR)$(pkgdatadir)/types.db;
	$(INSTALL) -m 0644 $(srcdir)/src/postgresql_default.conf \
		$(DESTDIR)$(pkgdatadir)/postgresql_default.conf;

uninstall-hook:
	rm -f $(DESTDIR)$(pkgdatadir)/types.db;
	rm -f $(DESTDIR)$(sysconfdir)/collectd.conf
	rm -f $(DESTDIR)$(pkgdatadir)/postgresql_default.conf;

all-local: @PERL_BINDINGS@

install-exec-local:
	[ ! -f buildperl/Makefile ] || ( cd buildperl && $(MAKE) install )

# Perl 'make uninstall' does not work as well as wanted.
# So we do the work here.
uninstall-local:
	@PERL@ -I$(DESTDIR)$(prefix) $(srcdir)/bindings/perl/uninstall_mod.pl Collectd
	find $(DESTDIR)$(prefix) -name "perllocal.pod" -exec rm {} \;

clean-local:
	rm -rf buildperl

perl: buildperl/Makefile
	cd buildperl && $(MAKE)

buildperl/Makefile: .perl-directory-stamp buildperl/Makefile.PL \
	$(top_builddir)/config.status
	@# beautify the output a bit
	@echo 'cd buildperl && @PERL@ Makefile.PL @PERL_BINDINGS_OPTIONS@'
	@cd buildperl && ( if ! @PERL@ Makefile.PL @PERL_BINDINGS_OPTIONS@; then \
			echo ""; \
			echo 'Check whether you have set $$PERL_MM_OPT in your environment and try using ./configure --with-perl-bindings=""'; \
			echo ""; \
		fi )

buildperl/Makefile.PL: .perl-directory-stamp $(top_builddir)/config.status

.perl-directory-stamp:
	if test ! -d buildperl; then \
	  mkdir -p buildperl/Collectd/Plugins; \
	  cp $(srcdir)/bindings/perl/lib/Collectd.pm buildperl/; \
	  cp $(srcdir)/bindings/perl/Makefile.PL buildperl/; \
	  cp $(srcdir)/bindings/perl/lib/Collectd/Unixsock.pm buildperl/Collectd/; \
	  cp $(srcdir)/bindings/perl/lib/Collectd/Plugins/OpenVZ.pm buildperl/Collectd/Plugins/; \
	fi
	touch $@

.PHONY: perl


if BUILD_WITH_JAVA
dist_noinst_JAVA = \
	bindings/java/org/collectd/api/Collectd.java \
	bindings/java/org/collectd/api/CollectdConfigInterface.java \
	bindings/java/org/collectd/api/CollectdFlushInterface.java \
	bindings/java/org/collectd/api/CollectdInitInterface.java \
	bindings/java/org/collectd/api/CollectdLogInterface.java \
	bindings/java/org/collectd/api/CollectdMatchFactoryInterface.java \
	bindings/java/org/collectd/api/CollectdMatchInterface.java \
	bindings/java/org/collectd/api/CollectdNotificationInterface.java \
	bindings/java/org/collectd/api/CollectdReadInterface.java \
	bindings/java/org/collectd/api/CollectdShutdownInterface.java \
	bindings/java/org/collectd/api/CollectdTargetFactoryInterface.java \
	bindings/java/org/collectd/api/CollectdTargetInterface.java \
	bindings/java/org/collectd/api/CollectdWriteInterface.java \
	bindings/java/org/collectd/api/DataSet.java \
	bindings/java/org/collectd/api/DataSource.java \
	bindings/java/org/collectd/api/Notification.java \
	bindings/java/org/collectd/api/OConfigItem.java \
	bindings/java/org/collectd/api/OConfigValue.java \
	bindings/java/org/collectd/api/PluginData.java \
	bindings/java/org/collectd/api/ValueList.java \
	bindings/java/org/collectd/java/GenericJMX.java \
	bindings/java/org/collectd/java/GenericJMXConfConnection.java \
	bindings/java/org/collectd/java/GenericJMXConfMBean.java \
	bindings/java/org/collectd/java/GenericJMXConfValue.java \
	bindings/java/org/collectd/java/JMXMemory.java

collectd-api.jar: $(JAVA_TIMESTAMP_FILE)
	$(JAR) cf $(JARFLAGS) $@ org/collectd/api/*.class

generic-jmx.jar: $(JAVA_TIMESTAMP_FILE)
	$(JAR) cf $(JARFLAGS) $@ org/collectd/java/*.class

jar_DATA = collectd-api.jar generic-jmx.jar
endif
