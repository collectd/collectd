SUBDIRS = libcollectdclient
if BUILD_WITH_OWN_LIBOCONFIG
SUBDIRS += liboconfig
endif
SUBDIRS += daemon

PLUGIN_LDFLAGS = -module -avoid-version -export-symbols-regex '\<module_register\>'

if COMPILER_IS_GCC
AM_CFLAGS = -Wall -Werror
endif

AM_CPPFLAGS = -I$(srcdir)/daemon
AM_CPPFLAGS += -DPREFIX='"${prefix}"'
AM_CPPFLAGS += -DCONFIGFILE='"${sysconfdir}/${PACKAGE_NAME}.conf"'
AM_CPPFLAGS += -DLOCALSTATEDIR='"${localstatedir}"'
AM_CPPFLAGS += -DPKGLOCALSTATEDIR='"${localstatedir}/lib/${PACKAGE_NAME}"'
if BUILD_FEATURE_DAEMON
AM_CPPFLAGS += -DPIDFILE='"${localstatedir}/run/${PACKAGE_NAME}.pid"'
endif
AM_CPPFLAGS += -DPLUGINDIR='"${pkglibdir}"'
AM_CPPFLAGS += -DPKGDATADIR='"${pkgdatadir}"'

AUTOMAKE_OPTIONS = subdir-objects

sbin_PROGRAMS = collectdmon
bin_PROGRAMS = collectd-nagios collectdctl collectd-tg

collectdmon_SOURCES = collectdmon.c
collectdmon_CPPFLAGS = $(AM_CPPFLAGS)

collectd_nagios_SOURCES = collectd-nagios.c
collectd_nagios_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src/libcollectdclient/collectd -I$(top_builddir)/src/libcollectdclient/collectd
collectd_nagios_LDADD =
if BUILD_WITH_LIBSOCKET
collectd_nagios_LDADD += -lsocket
endif
if BUILD_AIX
collectd_nagios_LDADD += -lm
endif

collectd_nagios_LDADD += libcollectdclient/libcollectdclient.la
collectd_nagios_DEPENDENCIES = libcollectdclient/libcollectdclient.la


collectdctl_SOURCES = collectdctl.c
collectdctl_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src/libcollectdclient/collectd -I$(top_builddir)/src/libcollectdclient/collectd
collectdctl_LDADD =
if BUILD_WITH_LIBSOCKET
collectdctl_LDADD += -lsocket
endif
if BUILD_AIX
collectdctl_LDADD += -lm
endif
collectdctl_LDADD += libcollectdclient/libcollectdclient.la
collectdctl_DEPENDENCIES = libcollectdclient/libcollectdclient.la

collectd_tg_SOURCES = collectd-tg.c \
		      daemon/utils_heap.c daemon/utils_heap.h
collectd_tg_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src/libcollectdclient/collectd -I$(top_builddir)/src/libcollectdclient/collectd
collectd_tg_LDADD =
if BUILD_WITH_LIBSOCKET
collectd_tg_LDADD += -lsocket
endif
if BUILD_WITH_LIBRT
collectd_tg_LDADD += -lrt
endif
if BUILD_AIX
collectd_tg_LDADD += -lm
endif
if BUILD_WITH_LIBPTHREAD
collectd_tg_LDADD += -lpthread
endif
collectd_tg_LDADD += libcollectdclient/libcollectdclient.la
collectd_tg_DEPENDENCIES = libcollectdclient/libcollectdclient.la


pkglib_LTLIBRARIES =

BUILT_SOURCES =
CLEANFILES =

if BUILD_PLUGIN_AGGREGATION
pkglib_LTLIBRARIES += aggregation.la
aggregation_la_SOURCES = aggregation.c \
                         utils_vl_lookup.c utils_vl_lookup.h
aggregation_la_LDFLAGS = $(PLUGIN_LDFLAGS)
aggregation_la_LIBADD = -lm
endif

if BUILD_PLUGIN_AMQP
pkglib_LTLIBRARIES += amqp.la
amqp_la_SOURCES = amqp.c \
		  utils_cmd_putval.c utils_cmd_putval.h \
		  utils_format_graphite.c utils_format_graphite.h \
		  utils_format_json.c utils_format_json.h
amqp_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBRABBITMQ_LDFLAGS)
amqp_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBRABBITMQ_CPPFLAGS)
amqp_la_LIBADD = $(BUILD_WITH_LIBRABBITMQ_LIBS)
endif

if BUILD_PLUGIN_APACHE
pkglib_LTLIBRARIES += apache.la
apache_la_SOURCES = apache.c
apache_la_LDFLAGS = $(PLUGIN_LDFLAGS)
apache_la_CFLAGS = $(AM_CFLAGS)
apache_la_LIBADD =
if BUILD_WITH_LIBCURL
apache_la_CFLAGS += $(BUILD_WITH_LIBCURL_CFLAGS)
apache_la_LIBADD += $(BUILD_WITH_LIBCURL_LIBS)
endif
endif

if BUILD_PLUGIN_APCUPS
pkglib_LTLIBRARIES += apcups.la
apcups_la_SOURCES = apcups.c
apcups_la_LDFLAGS = $(PLUGIN_LDFLAGS)
apcups_la_LIBADD =
if BUILD_WITH_LIBSOCKET
apcups_la_LIBADD += -lsocket
endif
endif

if BUILD_PLUGIN_APPLE_SENSORS
pkglib_LTLIBRARIES += apple_sensors.la
apple_sensors_la_SOURCES = apple_sensors.c
apple_sensors_la_LDFLAGS = $(PLUGIN_LDFLAGS)
apple_sensors_la_LDFLAGS += -framework IOKit
endif

if BUILD_PLUGIN_AQUAERO
pkglib_LTLIBRARIES += aquaero.la
aquaero_la_SOURCES = aquaero.c
aquaero_la_LDFLAGS = $(PLUGIN_LDFLAGS)
aquaero_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBAQUAERO5_CFLAGS)
aquaero_la_LIBADD = $(BUILD_WITH_LIBAQUAERO5_LDFLAGS) -laquaero5
endif

if BUILD_PLUGIN_ASCENT
pkglib_LTLIBRARIES += ascent.la
ascent_la_SOURCES = ascent.c
ascent_la_LDFLAGS = $(PLUGIN_LDFLAGS)
ascent_la_CFLAGS = $(AM_CFLAGS) \
		$(BUILD_WITH_LIBCURL_CFLAGS) $(BUILD_WITH_LIBXML2_CFLAGS)
ascent_la_LIBADD = $(BUILD_WITH_LIBCURL_LIBS) $(BUILD_WITH_LIBXML2_LIBS)
endif

if BUILD_PLUGIN_BAROMETER
pkglib_LTLIBRARIES += barometer.la
barometer_la_SOURCES = barometer.c
barometer_la_LDFLAGS = $(PLUGIN_LDFLAGS)
barometer_la_LIBADD = -lm
endif

if BUILD_PLUGIN_BATTERY
pkglib_LTLIBRARIES += battery.la
battery_la_SOURCES = battery.c
battery_la_LDFLAGS = $(PLUGIN_LDFLAGS)
battery_la_LIBADD =
if BUILD_WITH_LIBIOKIT
battery_la_LDFLAGS += -framework IOKit
endif
endif

if BUILD_PLUGIN_BIND
pkglib_LTLIBRARIES += bind.la
bind_la_SOURCES = bind.c
bind_la_LDFLAGS = $(PLUGIN_LDFLAGS)
bind_la_CFLAGS = $(AM_CFLAGS) \
		 $(BUILD_WITH_LIBCURL_CFLAGS) $(BUILD_WITH_LIBXML2_CFLAGS)
bind_la_LIBADD = $(BUILD_WITH_LIBCURL_LIBS) $(BUILD_WITH_LIBXML2_LIBS)
endif

if BUILD_PLUGIN_CEPH
pkglib_LTLIBRARIES += ceph.la
ceph_la_SOURCES = ceph.c
ceph_la_CFLAGS = $(AM_CFLAGS)
ceph_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBYAJL_LDFLAGS)
ceph_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBYAJL_CPPFLAGS)
ceph_la_LIBADD = $(BUILD_WITH_LIBYAJL_LIBS)
endif

if BUILD_PLUGIN_CGROUPS
pkglib_LTLIBRARIES += cgroups.la
cgroups_la_SOURCES = cgroups.c \
		     utils_ignorelist.c utils_ignorelist.h \
		     utils_mount.c utils_mount.h
cgroups_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_CONNTRACK
pkglib_LTLIBRARIES += conntrack.la
conntrack_la_SOURCES = conntrack.c
conntrack_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_CONTEXTSWITCH
pkglib_LTLIBRARIES += contextswitch.la
contextswitch_la_SOURCES = contextswitch.c
contextswitch_la_LDFLAGS = $(PLUGIN_LDFLAGS)
contextswitch_la_LIBADD =
if BUILD_WITH_PERFSTAT
contextswitch_la_LIBADD += -lperfstat
endif
endif

if BUILD_PLUGIN_CPU
pkglib_LTLIBRARIES += cpu.la
cpu_la_SOURCES = cpu.c
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
cpufreq_la_SOURCES = cpufreq.c
cpufreq_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_CSV
pkglib_LTLIBRARIES += csv.la
csv_la_SOURCES = csv.c
csv_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_CURL
pkglib_LTLIBRARIES += curl.la
curl_la_SOURCES = curl.c
curl_la_LDFLAGS = $(PLUGIN_LDFLAGS)
curl_la_CFLAGS = $(AM_CFLAGS)
curl_la_LIBADD =
if BUILD_WITH_LIBCURL
curl_la_CFLAGS += $(BUILD_WITH_LIBCURL_CFLAGS)
curl_la_LIBADD += $(BUILD_WITH_LIBCURL_LIBS)
endif
endif

if BUILD_PLUGIN_CURL_JSON
pkglib_LTLIBRARIES += curl_json.la
curl_json_la_SOURCES = curl_json.c
curl_json_la_CFLAGS = $(AM_CFLAGS)
curl_json_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBYAJL_LDFLAGS)
curl_json_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBYAJL_CPPFLAGS)
curl_json_la_LIBADD = $(BUILD_WITH_LIBYAJL_LIBS)
if BUILD_WITH_LIBCURL
curl_json_la_CFLAGS += $(BUILD_WITH_LIBCURL_CFLAGS)
curl_json_la_LIBADD += $(BUILD_WITH_LIBCURL_LIBS)
endif
endif

if BUILD_PLUGIN_CURL_XML
pkglib_LTLIBRARIES += curl_xml.la
curl_xml_la_SOURCES = curl_xml.c
curl_xml_la_LDFLAGS = $(PLUGIN_LDFLAGS)
curl_xml_la_CFLAGS = $(AM_CFLAGS) \
		$(BUILD_WITH_LIBCURL_CFLAGS) $(BUILD_WITH_LIBXML2_CFLAGS)
curl_xml_la_LIBADD = $(BUILD_WITH_LIBCURL_LIBS) $(BUILD_WITH_LIBXML2_LIBS)
endif

if BUILD_PLUGIN_DBI
pkglib_LTLIBRARIES += dbi.la
dbi_la_SOURCES = dbi.c \
		 utils_db_query.c utils_db_query.h
dbi_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBDBI_CPPFLAGS)
dbi_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBDBI_LDFLAGS)
dbi_la_LIBADD = $(BUILD_WITH_LIBDBI_LIBS)
endif

if BUILD_PLUGIN_DF
pkglib_LTLIBRARIES += df.la
df_la_SOURCES = df.c \
		utils_ignorelist.c utils_ignorelist.h \
		utils_mount.c utils_mount.h
df_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_DISK
pkglib_LTLIBRARIES += disk.la
disk_la_SOURCES = disk.c \
		  utils_ignorelist.c utils_ignorelist.h
disk_la_CFLAGS = $(AM_CFLAGS)
disk_la_LDFLAGS = $(PLUGIN_LDFLAGS)
disk_la_LIBADD =
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
disk_la_LIBADD += -ludev
endif
if BUILD_WITH_PERFSTAT
disk_la_LIBADD += -lperfstat
endif
endif

if BUILD_PLUGIN_DNS
pkglib_LTLIBRARIES += dns.la
dns_la_SOURCES = dns.c utils_dns.c utils_dns.h
dns_la_LDFLAGS = $(PLUGIN_LDFLAGS)
dns_la_LIBADD = -lpcap -lpthread
endif

if BUILD_PLUGIN_DRBD
pkglib_LTLIBRARIES += drbd.la
drbd_la_SOURCES = drbd.c
drbd_la_LDFLAGS = $(PLUGIN_LDFLAGS)
drbd_la_LIBADD = -lpthread
endif

if BUILD_PLUGIN_EMAIL
pkglib_LTLIBRARIES += email.la
email_la_SOURCES = email.c
email_la_LDFLAGS = $(PLUGIN_LDFLAGS)
email_la_LIBADD = -lpthread
endif

if BUILD_PLUGIN_ENTROPY
pkglib_LTLIBRARIES += entropy.la
entropy_la_SOURCES = entropy.c
entropy_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_EXEC
pkglib_LTLIBRARIES += exec.la
exec_la_SOURCES = exec.c \
		  utils_cmd_putnotif.c utils_cmd_putnotif.h \
		  utils_cmd_putval.c utils_cmd_putval.h \
		  utils_parse_option.h utils_parse_option.c
exec_la_LDFLAGS = $(PLUGIN_LDFLAGS)
exec_la_LIBADD = -lpthread
endif

if BUILD_PLUGIN_ETHSTAT
pkglib_LTLIBRARIES += ethstat.la
ethstat_la_SOURCES = ethstat.c
ethstat_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_FILECOUNT
pkglib_LTLIBRARIES += filecount.la
filecount_la_SOURCES = filecount.c
filecount_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_GMOND
pkglib_LTLIBRARIES += gmond.la
gmond_la_SOURCES = gmond.c
gmond_la_CPPFLAGS = $(AM_CPPFLAGS) $(GANGLIA_CPPFLAGS)
gmond_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(GANGLIA_LDFLAGS)
gmond_la_LIBADD = $(GANGLIA_LIBS)
endif

if BUILD_PLUGIN_HDDTEMP
pkglib_LTLIBRARIES += hddtemp.la
hddtemp_la_SOURCES = hddtemp.c
hddtemp_la_LDFLAGS = $(PLUGIN_LDFLAGS)
hddtemp_la_LIBADD =
if BUILD_WITH_LIBSOCKET
hddtemp_la_LIBADD += -lsocket
endif
endif

if BUILD_PLUGIN_INTERFACE
pkglib_LTLIBRARIES += interface.la
interface_la_SOURCES = interface.c \
		       utils_ignorelist.c utils_ignorelist.h
interface_la_CFLAGS = $(AM_CFLAGS)
interface_la_LDFLAGS = $(PLUGIN_LDFLAGS)
interface_la_LIBADD =
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
ipc_la_SOURCES = ipc.c
ipc_la_CFLAGS = $(AM_CFLAGS)
ipc_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_IPTABLES
pkglib_LTLIBRARIES += iptables.la
iptables_la_SOURCES = iptables.c
iptables_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBIPTC_CPPFLAGS)
iptables_la_LDFLAGS = $(PLUGIN_LDFLAGS)
iptables_la_LIBADD = $(BUILD_WITH_LIBIPTC_LDFLAGS)
endif

if BUILD_PLUGIN_IPMI
pkglib_LTLIBRARIES += ipmi.la
ipmi_la_SOURCES = ipmi.c \
		  utils_ignorelist.c utils_ignorelist.h
ipmi_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_OPENIPMI_CFLAGS)
ipmi_la_LDFLAGS = $(PLUGIN_LDFLAGS)
ipmi_la_LIBADD = $(BUILD_WITH_OPENIPMI_LIBS)
endif

if BUILD_PLUGIN_IPVS
pkglib_LTLIBRARIES += ipvs.la
ipvs_la_SOURCES = ipvs.c
if IP_VS_H_NEEDS_KERNEL_CFLAGS
ipvs_la_CFLAGS = $(AM_CFLAGS) $(KERNEL_CFLAGS)
endif
ipvs_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_IRQ
pkglib_LTLIBRARIES += irq.la
irq_la_SOURCES = irq.c \
		 utils_ignorelist.c utils_ignorelist.h
irq_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_JAVA
pkglib_LTLIBRARIES += java.la
java_la_SOURCES = java.c
java_la_CPPFLAGS = $(AM_CPPFLAGS) $(JAVA_CPPFLAGS)
java_la_CFLAGS = $(AM_CFLAGS) $(JAVA_CFLAGS)
java_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(JAVA_LDFLAGS)
java_la_LIBADD = $(JAVA_LIBS)
endif

if BUILD_PLUGIN_LOAD
pkglib_LTLIBRARIES += load.la
load_la_SOURCES = load.c
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
logfile_la_SOURCES = logfile.c
logfile_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_LOG_LOGSTASH
pkglib_LTLIBRARIES += log_logstash.la
log_logstash_la_SOURCES = log_logstash.c
log_logstash_la_CFLAGS = $(AM_CFLAGS)
log_logstash_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBYAJL_LDFLAGS)
log_logstash_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBYAJL_CPPFLAGS)
log_logstash_la_LIBADD = $(BUILD_WITH_LIBYAJL_LIBS)
endif

if BUILD_PLUGIN_LPAR
pkglib_LTLIBRARIES += lpar.la
lpar_la_SOURCES = lpar.c
lpar_la_LDFLAGS = $(PLUGIN_LDFLAGS)
lpar_la_LIBADD = -lperfstat
endif

if BUILD_PLUGIN_LVM
pkglib_LTLIBRARIES += lvm.la
lvm_la_SOURCES = lvm.c
lvm_la_LDFLAGS = $(PLUGIN_LDFLAGS)
lvm_la_LIBADD = $(BUILD_WITH_LIBLVM2APP_LIBS)
endif

if BUILD_PLUGIN_MADWIFI
pkglib_LTLIBRARIES += madwifi.la
madwifi_la_SOURCES = madwifi.c madwifi.h \
		     utils_ignorelist.c utils_ignorelist.h
madwifi_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_MATCH_EMPTY_COUNTER
pkglib_LTLIBRARIES += match_empty_counter.la
match_empty_counter_la_SOURCES = match_empty_counter.c
match_empty_counter_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_MATCH_HASHED
pkglib_LTLIBRARIES += match_hashed.la
match_hashed_la_SOURCES = match_hashed.c
match_hashed_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_MATCH_REGEX
pkglib_LTLIBRARIES += match_regex.la
match_regex_la_SOURCES = match_regex.c
match_regex_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_MATCH_TIMEDIFF
pkglib_LTLIBRARIES += match_timediff.la
match_timediff_la_SOURCES = match_timediff.c
match_timediff_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_MATCH_VALUE
pkglib_LTLIBRARIES += match_value.la
match_value_la_SOURCES = match_value.c
match_value_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_MBMON
pkglib_LTLIBRARIES += mbmon.la
mbmon_la_SOURCES = mbmon.c
mbmon_la_LDFLAGS = $(PLUGIN_LDFLAGS)
mbmon_la_LIBADD =
if BUILD_WITH_LIBSOCKET
mbmon_la_LIBADD += -lsocket
endif
endif

if BUILD_PLUGIN_MD
pkglib_LTLIBRARIES += md.la
md_la_SOURCES = md.c \
		utils_ignorelist.c utils_ignorelist.h
md_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_MEMCACHEC
pkglib_LTLIBRARIES += memcachec.la
memcachec_la_SOURCES = memcachec.c
memcachec_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBMEMCACHED_LDFLAGS)
memcachec_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBMEMCACHED_CPPFLAGS)
memcachec_la_LIBADD = $(BUILD_WITH_LIBMEMCACHED_LIBS)
endif

if BUILD_PLUGIN_MEMCACHED
pkglib_LTLIBRARIES += memcached.la
memcached_la_SOURCES = memcached.c
memcached_la_LDFLAGS = $(PLUGIN_LDFLAGS)
memcached_la_LIBADD =
if BUILD_WITH_LIBSOCKET
memcached_la_LIBADD += -lsocket
endif
endif

if BUILD_PLUGIN_MEMORY
pkglib_LTLIBRARIES += memory.la
memory_la_SOURCES = memory.c
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
mic_la_SOURCES = mic.c \
		 utils_ignorelist.c utils_ignorelist.h
mic_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_MIC_LIBPATH)
mic_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_MIC_CPPFLAGS)
mic_la_LIBADD = $(BUILD_WITH_MIC_LDADD)
endif

if BUILD_PLUGIN_MODBUS
pkglib_LTLIBRARIES += modbus.la
modbus_la_SOURCES = modbus.c
modbus_la_LDFLAGS = $(PLUGIN_LDFLAGS)
modbus_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBMODBUS_CFLAGS)
modbus_la_LIBADD = $(BUILD_WITH_LIBMODBUS_LIBS)
endif

if BUILD_PLUGIN_MULTIMETER
pkglib_LTLIBRARIES += multimeter.la
multimeter_la_SOURCES = multimeter.c
multimeter_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_MYSQL
pkglib_LTLIBRARIES += mysql.la
mysql_la_SOURCES = mysql.c
mysql_la_LDFLAGS = $(PLUGIN_LDFLAGS)
mysql_la_CFLAGS = $(AM_CFLAGS)
mysql_la_LIBADD =
if BUILD_WITH_LIBMYSQL
mysql_la_CFLAGS += $(BUILD_WITH_LIBMYSQL_CFLAGS)
mysql_la_LIBADD += $(BUILD_WITH_LIBMYSQL_LIBS)
endif
endif

if BUILD_PLUGIN_NETAPP
pkglib_LTLIBRARIES += netapp.la
netapp_la_SOURCES = netapp.c \
		    utils_ignorelist.c utils_ignorelist.h
netapp_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBNETAPP_CPPFLAGS)
netapp_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(LIBNETAPP_LDFLAGS)
netapp_la_LIBADD = $(LIBNETAPP_LIBS)
endif

if BUILD_PLUGIN_NETLINK
pkglib_LTLIBRARIES += netlink.la
netlink_la_SOURCES = netlink.c
netlink_la_LDFLAGS = $(PLUGIN_LDFLAGS)
netlink_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBMNL_CFLAGS)
netlink_la_LIBADD = $(BUILD_WITH_LIBMNL_LIBS)
endif

if BUILD_PLUGIN_NETWORK
pkglib_LTLIBRARIES += network.la
network_la_SOURCES = network.c network.h \
		     utils_fbhash.c utils_fbhash.h
network_la_CPPFLAGS = $(AM_CPPFLAGS)
network_la_LDFLAGS = $(PLUGIN_LDFLAGS)
network_la_LIBADD = -lpthread
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
nfs_la_SOURCES = nfs.c
nfs_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_FSCACHE
pkglib_LTLIBRARIES += fscache.la
fscache_la_SOURCES = fscache.c
fscache_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_NGINX
pkglib_LTLIBRARIES += nginx.la
nginx_la_SOURCES = nginx.c
nginx_la_CFLAGS = $(AM_CFLAGS)
nginx_la_LIBADD =
nginx_la_LDFLAGS = $(PLUGIN_LDFLAGS)
if BUILD_WITH_LIBCURL
nginx_la_CFLAGS += $(BUILD_WITH_LIBCURL_CFLAGS)
nginx_la_LIBADD += $(BUILD_WITH_LIBCURL_LIBS)
endif
endif

if BUILD_PLUGIN_NOTIFY_DESKTOP
pkglib_LTLIBRARIES += notify_desktop.la
notify_desktop_la_SOURCES = notify_desktop.c
notify_desktop_la_CFLAGS = $(AM_CFLAGS) $(LIBNOTIFY_CFLAGS)
notify_desktop_la_LDFLAGS = $(PLUGIN_LDFLAGS)
notify_desktop_la_LIBADD = $(LIBNOTIFY_LIBS)
endif

if BUILD_PLUGIN_NOTIFY_EMAIL
pkglib_LTLIBRARIES += notify_email.la
notify_email_la_SOURCES = notify_email.c
notify_email_la_LDFLAGS = $(PLUGIN_LDFLAGS)
notify_email_la_LIBADD = -lesmtp -lssl -lcrypto -lpthread -ldl
endif

if BUILD_PLUGIN_NTPD
pkglib_LTLIBRARIES += ntpd.la
ntpd_la_SOURCES = ntpd.c
ntpd_la_LDFLAGS = $(PLUGIN_LDFLAGS)
ntpd_la_LIBADD =
if BUILD_WITH_LIBSOCKET
ntpd_la_LIBADD += -lsocket
endif
endif

if BUILD_PLUGIN_NUMA
pkglib_LTLIBRARIES += numa.la
numa_la_SOURCES = numa.c
numa_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_NUT
pkglib_LTLIBRARIES += nut.la
nut_la_SOURCES = nut.c
nut_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBUPSCLIENT_CFLAGS)
nut_la_LDFLAGS = $(PLUGIN_LDFLAGS)
nut_la_LIBADD = -lpthread $(BUILD_WITH_LIBUPSCLIENT_LIBS)
endif

if BUILD_PLUGIN_OLSRD
pkglib_LTLIBRARIES += olsrd.la
olsrd_la_SOURCES = olsrd.c
olsrd_la_LDFLAGS = $(PLUGIN_LDFLAGS)
olsrd_la_LIBADD =
if BUILD_WITH_LIBSOCKET
olsrd_la_LIBADD += -lsocket
endif
endif

if BUILD_PLUGIN_ONEWIRE
pkglib_LTLIBRARIES += onewire.la
onewire_la_SOURCES = onewire.c \
		     utils_ignorelist.c utils_ignorelist.h
onewire_la_CFLAGS = $(AM_CFLAGS)
onewire_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBOWCAPI_CPPFLAGS)
onewire_la_LIBADD = $(BUILD_WITH_LIBOWCAPI_LIBS)
onewire_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_OPENLDAP
pkglib_LTLIBRARIES += openldap.la
openldap_la_SOURCES = openldap.c
openldap_la_LDFLAGS = -module -avoid-version $(BUILD_WITH_LIBLDAP_LDFLAGS)
openldap_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBLDAP_CPPFLAGS)
openldap_la_LIBADD = -lldap
endif

if BUILD_PLUGIN_OPENVPN
pkglib_LTLIBRARIES += openvpn.la
openvpn_la_SOURCES = openvpn.c
openvpn_la_CFLAGS = $(AM_CFLAGS)
openvpn_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_ORACLE
pkglib_LTLIBRARIES += oracle.la
oracle_la_SOURCES = oracle.c \
	utils_db_query.c utils_db_query.h
oracle_la_CFLAGS = $(AM_CFLAGS)
oracle_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_ORACLE_CFLAGS)
oracle_la_LIBADD = $(BUILD_WITH_ORACLE_LIBS)
oracle_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_PERL
pkglib_LTLIBRARIES += perl.la
perl_la_SOURCES = perl.c
# Despite C99 providing the "bool" type thru stdbool.h, Perl defines its own
# version of that type if HAS_BOOL is not defined... *sigh*
perl_la_CPPFLAGS = $(AM_CPPFLAGS) -DHAS_BOOL=1
perl_la_CFLAGS  = $(AM_CFLAGS) \
		$(PERL_CFLAGS) \
		-DXS_VERSION=\"$(VERSION)\" -DVERSION=\"$(VERSION)\"
# Work-around for issues #41 and #42 - Perl 5.10 incorrectly introduced
# __attribute__nonnull__(3) for Perl_load_module().
if HAVE_BROKEN_PERL_LOAD_MODULE
perl_la_CFLAGS += -Wno-nonnull
endif
perl_la_LDFLAGS = $(PLUGIN_LDFLAGS) \
		$(PERL_LDFLAGS)
perl_la_LIBADD = $(PERL_LIBS)
endif

if BUILD_PLUGIN_PF
pkglib_LTLIBRARIES += pf.la
pf_la_SOURCES = pf.c
pf_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_PINBA
pkglib_LTLIBRARIES += pinba.la
pinba_la_SOURCES = pinba.c
nodist_pinba_la_SOURCES = pinba.pb-c.c pinba.pb-c.h
pinba_la_LDFLAGS = $(PLUGIN_LDFLAGS)
pinba_la_LIBADD = -lprotobuf-c
endif

if BUILD_PLUGIN_PING
pkglib_LTLIBRARIES += ping.la
ping_la_SOURCES = ping.c
ping_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBOPING_CPPFLAGS)
ping_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBOPING_LDFLAGS)
ping_la_LIBADD = -loping -lm
endif

if BUILD_PLUGIN_POSTGRESQL
pkglib_LTLIBRARIES += postgresql.la
postgresql_la_SOURCES = postgresql.c \
		 utils_db_query.c utils_db_query.h
postgresql_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBPQ_CPPFLAGS)
postgresql_la_LDFLAGS = $(PLUGIN_LDFLAGS) \
		$(BUILD_WITH_LIBPQ_LDFLAGS)
postgresql_la_LIBADD = -lpq
endif

if BUILD_PLUGIN_POWERDNS
pkglib_LTLIBRARIES += powerdns.la
powerdns_la_SOURCES = powerdns.c
powerdns_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_PYTHON
pkglib_LTLIBRARIES += python.la
python_la_SOURCES = python.c pyconfig.c pyvalues.c cpython.h
python_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_PYTHON_CPPFLAGS)
python_la_CFLAGS = $(AM_CFLAGS)
if COMPILER_IS_GCC
python_la_CFLAGS += -fno-strict-aliasing -Wno-strict-aliasing
endif
python_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_PYTHON_LDFLAGS)
python_la_LIBADD = $(BUILD_WITH_PYTHON_LIBS)
endif

if BUILD_PLUGIN_PROCESSES
pkglib_LTLIBRARIES += processes.la
processes_la_SOURCES = processes.c
processes_la_LDFLAGS = $(PLUGIN_LDFLAGS)
processes_la_LIBADD =
if BUILD_WITH_LIBKVM_GETPROCS
processes_la_LIBADD += -lkvm
endif
endif

if BUILD_PLUGIN_PROTOCOLS
pkglib_LTLIBRARIES += protocols.la
protocols_la_SOURCES = protocols.c \
		       utils_ignorelist.c utils_ignorelist.h
protocols_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_REDIS
pkglib_LTLIBRARIES += redis.la
redis_la_SOURCES = redis.c
redis_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBHIREDIS_LDFLAGS)
redis_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBHIREDIS_CPPFLAGS)
redis_la_LIBADD = -lhiredis
endif

if BUILD_PLUGIN_ROUTEROS
pkglib_LTLIBRARIES += routeros.la
routeros_la_SOURCES = routeros.c
routeros_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBROUTEROS_CPPFLAGS)
routeros_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBROUTEROS_LDFLAGS)
routeros_la_LIBADD = -lrouteros
endif

if BUILD_PLUGIN_RRDCACHED
pkglib_LTLIBRARIES += rrdcached.la
rrdcached_la_SOURCES = rrdcached.c utils_rrdcreate.c utils_rrdcreate.h
rrdcached_la_LDFLAGS = $(PLUGIN_LDFLAGS)
rrdcached_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBRRD_CFLAGS)
rrdcached_la_LIBADD = $(BUILD_WITH_LIBRRD_LDFLAGS)
endif

if BUILD_PLUGIN_RRDTOOL
pkglib_LTLIBRARIES += rrdtool.la
rrdtool_la_SOURCES = rrdtool.c utils_rrdcreate.c utils_rrdcreate.h
rrdtool_la_LDFLAGS = $(PLUGIN_LDFLAGS)
rrdtool_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBRRD_CFLAGS)
rrdtool_la_LIBADD = $(BUILD_WITH_LIBRRD_LDFLAGS)
endif

if BUILD_PLUGIN_SENSORS
pkglib_LTLIBRARIES += sensors.la
sensors_la_SOURCES = sensors.c \
		     utils_ignorelist.c utils_ignorelist.h
sensors_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBSENSORS_CFLAGS)
sensors_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBSENSORS_LDFLAGS)
sensors_la_LIBADD = -lsensors
endif

if BUILD_PLUGIN_SERIAL
pkglib_LTLIBRARIES += serial.la
serial_la_SOURCES = serial.c
serial_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_SIGROK
pkglib_LTLIBRARIES += sigrok.la
sigrok_la_SOURCES = sigrok.c
sigrok_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBSIGROK_CFLAGS)
sigrok_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBSIGROK_LDFLAGS)
sigrok_la_LIBADD = -lsigrok
endif

if BUILD_PLUGIN_SMART
if BUILD_WITH_LIBUDEV
pkglib_LTLIBRARIES += smart.la
smart_la_SOURCES = smart.c \
		   utils_ignorelist.c utils_ignorelist.h
smart_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBATASMART_CPPFLAGS)
smart_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBATASMART_LDFLAGS)
smart_la_LIBADD = $(BUILD_WITH_LIBATASMART_LIBS) -ludev
endif
endif

if BUILD_PLUGIN_SNMP
pkglib_LTLIBRARIES += snmp.la
snmp_la_SOURCES = snmp.c
snmp_la_LDFLAGS = $(PLUGIN_LDFLAGS)
snmp_la_CFLAGS = $(AM_CFLAGS)
snmp_la_LIBADD =
if BUILD_WITH_LIBNETSNMP
snmp_la_CFLAGS += $(BUILD_WITH_LIBSNMP_CFLAGS)
snmp_la_LIBADD += $(BUILD_WITH_LIBSNMP_LIBS)
endif
if BUILD_WITH_LIBPTHREAD
snmp_la_LIBADD += -lpthread
endif
endif

if BUILD_PLUGIN_STATSD
pkglib_LTLIBRARIES += statsd.la
statsd_la_SOURCES = statsd.c \
                    utils_latency.h utils_latency.c
statsd_la_LDFLAGS = $(PLUGIN_LDFLAGS)
statsd_la_LIBADD = -lpthread
endif

if BUILD_PLUGIN_SWAP
pkglib_LTLIBRARIES += swap.la
swap_la_SOURCES = swap.c
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

if BUILD_PLUGIN_SYSLOG
pkglib_LTLIBRARIES += syslog.la
syslog_la_SOURCES = syslog.c
syslog_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TABLE
pkglib_LTLIBRARIES += table.la
table_la_SOURCES = table.c
table_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TAIL
pkglib_LTLIBRARIES += tail.la
tail_la_SOURCES = tail.c
tail_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TAIL_CSV
pkglib_LTLIBRARIES += tail_csv.la
tail_csv_la_SOURCES = tail_csv.c
tail_csv_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TAPE
pkglib_LTLIBRARIES += tape.la
tape_la_SOURCES = tape.c
tape_la_LDFLAGS = $(PLUGIN_LDFLAGS)
tape_la_LIBADD = -lkstat -ldevinfo
endif

if BUILD_PLUGIN_TARGET_NOTIFICATION
pkglib_LTLIBRARIES += target_notification.la
target_notification_la_SOURCES = target_notification.c
target_notification_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TARGET_REPLACE
pkglib_LTLIBRARIES += target_replace.la
target_replace_la_SOURCES = target_replace.c
target_replace_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TARGET_SCALE
pkglib_LTLIBRARIES += target_scale.la
target_scale_la_SOURCES = target_scale.c
target_scale_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TARGET_SET
pkglib_LTLIBRARIES += target_set.la
target_set_la_SOURCES = target_set.c
target_set_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TARGET_V5UPGRADE
pkglib_LTLIBRARIES += target_v5upgrade.la
target_v5upgrade_la_SOURCES = target_v5upgrade.c
target_v5upgrade_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TCPCONNS
pkglib_LTLIBRARIES += tcpconns.la
tcpconns_la_SOURCES = tcpconns.c
tcpconns_la_LDFLAGS = $(PLUGIN_LDFLAGS)
tcpconns_la_LIBADD =
if BUILD_WITH_LIBKVM_NLIST
tcpconns_la_LIBADD += -lkvm
endif
endif

if BUILD_PLUGIN_TEAMSPEAK2
pkglib_LTLIBRARIES += teamspeak2.la
teamspeak2_la_SOURCES = teamspeak2.c
teamspeak2_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TED
pkglib_LTLIBRARIES += ted.la
ted_la_SOURCES = ted.c
ted_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_THERMAL
pkglib_LTLIBRARIES += thermal.la
thermal_la_SOURCES = thermal.c \
		     utils_ignorelist.c utils_ignorelist.h
thermal_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_THRESHOLD
pkglib_LTLIBRARIES += threshold.la
threshold_la_SOURCES = threshold.c
threshold_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_TOKYOTYRANT
pkglib_LTLIBRARIES += tokyotyrant.la
tokyotyrant_la_SOURCES = tokyotyrant.c
tokyotyrant_la_CPPFLAGS  = $(AM_CPPFLAGS) $(BUILD_WITH_LIBTOKYOTYRANT_CPPFLAGS)
tokyotyrant_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBTOKYOTYRANT_LDFLAGS)
tokyotyrant_la_LIBADD  = $(BUILD_WITH_LIBTOKYOTYRANT_LIBS)
if BUILD_WITH_LIBSOCKET
tokyotyrant_la_LIBADD += -lsocket
endif
endif

if BUILD_PLUGIN_UNIXSOCK
pkglib_LTLIBRARIES += unixsock.la
unixsock_la_SOURCES = unixsock.c \
		      utils_cmd_flush.h utils_cmd_flush.c \
		      utils_cmd_getval.h utils_cmd_getval.c \
		      utils_cmd_getthreshold.h utils_cmd_getthreshold.c \
		      utils_cmd_listval.h utils_cmd_listval.c \
		      utils_cmd_putval.h utils_cmd_putval.c \
		      utils_cmd_putnotif.h utils_cmd_putnotif.c \
		      utils_parse_option.h utils_parse_option.c
unixsock_la_LDFLAGS = $(PLUGIN_LDFLAGS)
unixsock_la_LIBADD = -lpthread
endif

if BUILD_PLUGIN_UPTIME
pkglib_LTLIBRARIES += uptime.la
uptime_la_SOURCES = uptime.c
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
users_la_SOURCES = users.c
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
uuid_la_SOURCES = uuid.c
uuid_la_CFLAGS  = $(AM_CFLAGS) $(BUILD_WITH_LIBHAL_CFLAGS)
uuid_la_LIBADD  = $(BUILD_WITH_LIBHAL_LIBS)
uuid_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_VARNISH
pkglib_LTLIBRARIES += varnish.la
varnish_la_SOURCES = varnish.c
varnish_la_LDFLAGS = $(PLUGIN_LDFLAGS)
varnish_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBVARNISH_CFLAGS)
varnish_la_LIBADD = $(BUILD_WITH_LIBVARNISH_LIBS)
endif

if BUILD_PLUGIN_VIRT
pkglib_LTLIBRARIES += virt.la
virt_la_SOURCES = virt.c \
		  utils_ignorelist.c utils_ignorelist.h
virt_la_CFLAGS = $(AM_CFLAGS) \
		$(BUILD_WITH_LIBVIRT_CFLAGS) $(BUILD_WITH_LIBXML2_CFLAGS)
virt_la_LIBADD = $(BUILD_WITH_LIBVIRT_LIBS) $(BUILD_WITH_LIBXML2_LIBS)
virt_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_VMEM
pkglib_LTLIBRARIES += vmem.la
vmem_la_SOURCES = vmem.c
vmem_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_VSERVER
pkglib_LTLIBRARIES += vserver.la
vserver_la_SOURCES = vserver.c
vserver_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_WIRELESS
pkglib_LTLIBRARIES += wireless.la
wireless_la_SOURCES = wireless.c
wireless_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_WRITE_GRAPHITE
pkglib_LTLIBRARIES += write_graphite.la
write_graphite_la_SOURCES = write_graphite.c \
                        utils_format_graphite.c utils_format_graphite.h \
                        utils_format_json.c utils_format_json.h
write_graphite_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_WRITE_HTTP
pkglib_LTLIBRARIES += write_http.la
write_http_la_SOURCES = write_http.c \
			utils_format_json.c utils_format_json.h
write_http_la_LDFLAGS = $(PLUGIN_LDFLAGS)
write_http_la_CFLAGS = $(AM_CFLAGS)
write_http_la_LIBADD =
if BUILD_WITH_LIBCURL
write_http_la_CFLAGS += $(BUILD_WITH_LIBCURL_CFLAGS)
write_http_la_LIBADD += $(BUILD_WITH_LIBCURL_LIBS)
endif
endif

if BUILD_PLUGIN_WRITE_KAFKA
pkglib_LTLIBRARIES += write_kafka.la
write_kafka_la_SOURCES = write_kafka.c \
                        utils_format_graphite.c utils_format_graphite.h \
                        utils_format_json.c utils_format_json.h \
                        utils_cmd_putval.c utils_cmd_putval.h \
                        utils_crc32.c utils_crc32.h
write_kafka_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBRDKAFKA_LDFLAGS)
write_kafka_la_LIBADD = $(BUILD_WITH_LIBRDKAFKA_LIBS)
endif

if BUILD_PLUGIN_WRITE_LOG
pkglib_LTLIBRARIES += write_log.la
write_log_la_SOURCES = write_log.c \
                        utils_format_graphite.c utils_format_graphite.h
write_log_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_WRITE_MONGODB
pkglib_LTLIBRARIES += write_mongodb.la
write_mongodb_la_SOURCES = write_mongodb.c
write_mongodb_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBMONGOC_CPPFLAGS)
write_mongodb_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBMONGOC_LDFLAGS)
write_mongodb_la_LIBADD = -lmongoc
endif

if BUILD_PLUGIN_WRITE_REDIS
pkglib_LTLIBRARIES += write_redis.la
write_redis_la_SOURCES = write_redis.c
write_redis_la_LDFLAGS = $(PLUGIN_LDFLAGS) $(BUILD_WITH_LIBHIREDIS_LDFLAGS)
write_redis_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBHIREDIS_CPPFLAGS)
write_redis_la_LIBADD = -lhiredis
endif

if BUILD_PLUGIN_WRITE_RIEMANN
pkglib_LTLIBRARIES += write_riemann.la
write_riemann_la_SOURCES = write_riemann.c write_riemann_threshold.c
nodist_write_riemann_la_SOURCES = riemann.pb-c.c riemann.pb-c.h
write_riemann_la_LDFLAGS = $(PLUGIN_LDFLAGS)
write_riemann_la_LIBADD = -lprotobuf-c
endif

if BUILD_PLUGIN_WRITE_SENSU
pkglib_LTLIBRARIES += write_sensu.la
write_sensu_la_SOURCES = write_sensu.c
write_sensu_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_WRITE_TSDB
pkglib_LTLIBRARIES += write_tsdb.la
write_tsdb_la_SOURCES = write_tsdb.c
write_tsdb_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

if BUILD_PLUGIN_XMMS
pkglib_LTLIBRARIES += xmms.la
xmms_la_SOURCES = xmms.c
xmms_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBXMMS_CFLAGS)
xmms_la_LDFLAGS = $(PLUGIN_LDFLAGS)
xmms_la_LIBADD = $(BUILD_WITH_LIBXMMS_LIBS)
endif

if BUILD_PLUGIN_ZFS_ARC
pkglib_LTLIBRARIES += zfs_arc.la
zfs_arc_la_SOURCES = zfs_arc.c
zfs_arc_la_CFLAGS = $(AM_CFLAGS)
zfs_arc_la_LDFLAGS = $(PLUGIN_LDFLAGS)
if BUILD_FREEBSD
zfs_arc_la_LIBADD = -lm
else
if BUILD_LINUX
# zfs_arc requires no library on linux
else
# solaris
zfs_arc_la_LIBADD = -lkstat
endif
endif
endif

if BUILD_PLUGIN_ZOOKEEPER
pkglib_LTLIBRARIES += zookeeper.la
zookeeper_la_SOURCES = zookeeper.c
zookeeper_la_LDFLAGS = $(PLUGIN_LDFLAGS)
endif

EXTRA_DIST = types.db

# Collectd.conf generation
EXTRA_DIST += collectd.conf.in.in
BUILT_SOURCES += collectd.conf
CLEANFILES += collectd.conf collectd.conf.in

do_subst_conf = sed -e 's,[@]datadir[@],$(datadir),g' \
	-e 's,[@]libdir[@],$(libdir),g' \
	-e 's,[@]localstatedir[@],$(localstatedir),g' \
	-e 's,[@]prefix[@],$(prefix),g' \
	-e 's,[@]exec_prefix[@],$(exec_prefix),g'

collectd.conf.in: collectd.conf.in.in Makefile
	$(do_subst_conf) < $(srcdir)/collectd.conf.in.in > collectd.conf.in

collectd.conf: collectd.conf.in Makefile
	$(top_srcdir)/config.status --file=collectd.conf

BUILT_SOURCES += $(dist_man_MANS)

dist_man_MANS = collectd.1 \
		collectd.conf.5 \
		collectd-email.5 \
		collectd-exec.5 \
		collectdctl.1 \
		collectd-java.5 \
		collectdmon.1 \
		collectd-nagios.1 \
		collectd-perl.5 \
		collectd-python.5 \
		collectd-snmp.5 \
		collectd-tg.1 \
		collectd-threshold.5 \
		collectd-unixsock.5 \
		types.db.5

#collectd_1_SOURCES = collectd.pod

EXTRA_DIST +=   collectd.conf.pod \
		collectd-email.pod \
		collectd-exec.pod \
		collectdctl.pod \
		collectd-java.pod \
		collectdmon.pod \
		collectd-nagios.pod \
		collectd-perl.pod \
		collectd-python.pod \
		collectd.pod \
		collectd-snmp.pod \
		collectd-tg.pod \
		collectd-threshold.pod \
		collectd-unixsock.pod \
		postgresql_default.conf \
		types.db.pod

.pod.1:
	pod2man --release=$(VERSION) --center=$(PACKAGE) $< \
		>.pod2man.tmp.$$$$ 2>/dev/null && mv -f .pod2man.tmp.$$$$ $@ || true
	@if grep '\<POD ERRORS\>' $@ >/dev/null 2>&1; \
	then \
		echo "$@ has some POD errors!"; false; \
	fi

.pod.5:
	pod2man --section=5 --release=$(VERSION) --center=$(PACKAGE) $< \
		>.pod2man.tmp.$$$$ 2>/dev/null && mv -f .pod2man.tmp.$$$$ $@ || true
	@if grep '\<POD ERRORS\>' $@ >/dev/null 2>&1; \
	then \
		echo "$@ has some POD errors!"; false; \
	fi

# Protocol buffer for the "pinba" plugin.
EXTRA_DIST += pinba.proto
if HAVE_PROTOC_C
CLEANFILES += pinba.pb-c.c pinba.pb-c.h
BUILT_SOURCES += pinba.pb-c.c pinba.pb-c.h

pinba.pb-c.c pinba.pb-c.h: pinba.proto
	protoc-c -I$(srcdir) --c_out . $(srcdir)/pinba.proto
endif

# Protocol buffer for the "write_riemann" plugin.
EXTRA_DIST += riemann.proto
if HAVE_PROTOC_C
CLEANFILES += riemann.pb-c.c riemann.pb-c.h

BUILT_SOURCES += riemann.pb-c.c riemann.pb-c.h

riemann.pb-c.c riemann.pb-c.h: riemann.proto
	protoc-c -I$(srcdir) --c_out . $(srcdir)/riemann.proto
endif

install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(sysconfdir)
	if test -e $(DESTDIR)$(sysconfdir)/collectd.conf; \
	then \
		$(INSTALL) -m 0640 collectd.conf $(DESTDIR)$(sysconfdir)/collectd.conf.pkg-orig; \
	else \
		$(INSTALL) -m 0640 collectd.conf $(DESTDIR)$(sysconfdir)/collectd.conf; \
	fi; \
	$(mkinstalldirs) $(DESTDIR)$(pkgdatadir)
	$(INSTALL) -m 0644 $(srcdir)/types.db $(DESTDIR)$(pkgdatadir)/types.db;
	$(INSTALL) -m 0644 $(srcdir)/postgresql_default.conf \
		$(DESTDIR)$(pkgdatadir)/postgresql_default.conf;

uninstall-hook:
	rm -f $(DESTDIR)$(pkgdatadir)/types.db;
	rm -f $(DESTDIR)$(sysconfdir)/collectd.conf
	rm -f $(DESTDIR)$(pkgdatadir)/postgresql_default.conf;

check_PROGRAMS = test_common test_utils_avltree test_utils_heap test_utils_mount test_utils_vl_lookup

test_common_SOURCES = tests/test_common.c \
                      daemon/common.h daemon/common.c \
                      tests/mock/plugin.c \
                      tests/mock/utils_cache.c \
                      tests/mock/utils_time.c
test_common_CPPFLAGS = $(AM_CPPFLAGS) $(LTDLINCL)
test_common_LDFLAGS = -export-dynamic
test_common_LDADD =

test_utils_avltree_SOURCES = tests/test_utils_avltree.c \
                             daemon/utils_avltree.c daemon/utils_avltree.h
test_utils_avltree_CPPFLAGS = $(AM_CPPFLAGS) $(LTDLINCL)
test_utils_avltree_LDFLAGS = -export-dynamic
test_utils_avltree_LDADD =

test_utils_heap_SOURCES = tests/test_utils_heap.c \
                          daemon/utils_heap.c daemon/utils_heap.h
test_utils_heap_CPPFLAGS = $(AM_CPPFLAGS) $(LTDLINCL)
test_utils_heap_LDFLAGS = -export-dynamic
test_utils_heap_LDADD =

test_utils_mount_SOURCES = tests/test_utils_mount.c \
                           utils_mount.c utils_mount.h \
                           daemon/common.c daemon/common.h \
                           tests/mock/plugin.c \
                           tests/mock/utils_cache.c \
                           tests/mock/utils_time.c
test_utils_mount_CPPFLAGS = $(AM_CPPFLAGS) $(LTDLINCL)
test_utils_mount_LDFLAGS = -export-dynamic
test_utils_mount_LDADD =

test_utils_vl_lookup_SOURCES = tests/test_utils_vl_lookup.c \
                               utils_vl_lookup.h utils_vl_lookup.c \
                               daemon/utils_avltree.c daemon/utils_avltree.h \
                               daemon/common.c daemon/common.h \
                               tests/mock/plugin.c \
                               tests/mock/utils_cache.c \
                               tests/mock/utils_time.c
test_utils_vl_lookup_CPPFLAGS = $(AM_CPPFLAGS) $(LTDLINCL)
test_utils_vl_lookup_LDFLAGS = -export-dynamic
test_utils_vl_lookup_LDADD =

TESTS = test_common test_utils_avltree test_utils_heap test_utils_mount test_utils_vl_lookup
