SUBDIRS = libcollectdclient
if BUILD_WITH_OWN_LIBOCONFIG
SUBDIRS += liboconfig
endif

if COMPILER_IS_GCC
AM_CFLAGS = -Wall -Werror
endif

AM_CPPFLAGS = -DPREFIX='"${prefix}"'
AM_CPPFLAGS += -DCONFIGFILE='"${sysconfdir}/${PACKAGE_NAME}.conf"'
AM_CPPFLAGS += -DLOCALSTATEDIR='"${localstatedir}"'
AM_CPPFLAGS += -DPKGLOCALSTATEDIR='"${localstatedir}/lib/${PACKAGE_NAME}"'
if BUILD_FEATURE_DAEMON
AM_CPPFLAGS += -DPIDFILE='"${localstatedir}/run/${PACKAGE_NAME}.pid"'
endif
AM_CPPFLAGS += -DPLUGINDIR='"${pkglibdir}"'
AM_CPPFLAGS += -DPKGDATADIR='"${pkgdatadir}"'

sbin_PROGRAMS = collectd collectdmon
bin_PROGRAMS = collectd-nagios collectdctl

collectd_SOURCES = collectd.c collectd.h \
		   common.c common.h \
		   configfile.c configfile.h \
		   filter_chain.c filter_chain.h \
		   meta_data.c meta_data.h \
		   plugin.c plugin.h \
		   utils_avltree.c utils_avltree.h \
		   utils_cache.c utils_cache.h \
		   utils_complain.c utils_complain.h \
		   utils_heap.c utils_heap.h \
		   utils_ignorelist.c utils_ignorelist.h \
		   utils_llist.c utils_llist.h \
		   utils_parse_option.c utils_parse_option.h \
		   utils_tail_match.c utils_tail_match.h \
		   utils_match.c utils_match.h \
		   utils_subst.c utils_subst.h \
		   utils_tail.c utils_tail.h \
		   utils_time.c utils_time.h \
		   types_list.c types_list.h

collectd_CPPFLAGS =  $(AM_CPPFLAGS) $(LTDLINCL)
collectd_CFLAGS = $(AM_CFLAGS)
collectd_LDFLAGS = -export-dynamic
collectd_LDADD =
collectd_DEPENDENCIES =

# Link to these libraries..
if BUILD_WITH_LIBRT
collectd_LDADD += -lrt
endif
if BUILD_WITH_LIBPOSIX4
collectd_LDADD += -lposix4
endif
if BUILD_WITH_LIBSOCKET
collectd_LDADD += -lsocket
endif
if BUILD_WITH_LIBRESOLV
collectd_LDADD += -lresolv
endif
if BUILD_WITH_LIBPTHREAD
collectd_LDADD += -lpthread
endif
if BUILD_WITH_LIBKSTAT
collectd_LDADD += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
collectd_LDADD += -ldevinfo
endif
if BUILD_AIX
collectd_LDFLAGS += -Wl,-bexpall,-brtllib
collectd_LDADD += -lm
endif

# The daemon needs to call sg_init, so we need to link it against libstatgrab,
# too. -octo
if BUILD_WITH_LIBSTATGRAB
collectd_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)
collectd_LDADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
endif

if BUILD_WITH_OWN_LIBOCONFIG
collectd_LDADD += $(LIBLTDL) liboconfig/liboconfig.la
collectd_DEPENDENCIES += liboconfig/liboconfig.la
else
collectd_LDADD += -loconfig
endif

collectdmon_SOURCES = collectdmon.c
collectdmon_CPPFLAGS = $(AM_CPPFLAGS)

collectd_nagios_SOURCES = collectd-nagios.c
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
collectdctl_LDADD =
if BUILD_WITH_LIBSOCKET
collectdctl_LDADD += -lsocket
endif
if BUILD_AIX
collectdctl_LDADD += -lm
endif
collectdctl_LDADD += libcollectdclient/libcollectdclient.la
collectdctl_DEPENDENCIES = libcollectdclient/libcollectdclient.la


pkglib_LTLIBRARIES = 

BUILT_SOURCES = 
CLEANFILES = 

if BUILD_PLUGIN_AMQP
pkglib_LTLIBRARIES += amqp.la
amqp_la_SOURCES = amqp.c \
		  utils_cmd_putval.c utils_cmd_putval.h \
		  utils_format_json.c utils_format_json.h
amqp_la_LDFLAGS = -module -avoid-version $(BUILD_WITH_LIBRABBITMQ_LDFLAGS)
amqp_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBRABBITMQ_CPPFLAGS)
amqp_la_LIBADD = $(BUILD_WITH_LIBRABBITMQ_LIBS)
collectd_LDADD += "-dlopen" amqp.la
collectd_DEPENDENCIES += amqp.la
endif

if BUILD_PLUGIN_APACHE
pkglib_LTLIBRARIES += apache.la
apache_la_SOURCES = apache.c
apache_la_LDFLAGS = -module -avoid-version
apache_la_CFLAGS = $(AM_CFLAGS)
apache_la_LIBADD =
collectd_LDADD += "-dlopen" apache.la
if BUILD_WITH_LIBCURL
apache_la_CFLAGS += $(BUILD_WITH_LIBCURL_CFLAGS)
apache_la_LIBADD += $(BUILD_WITH_LIBCURL_LIBS)
endif
collectd_DEPENDENCIES += apache.la
endif

if BUILD_PLUGIN_APCUPS
pkglib_LTLIBRARIES += apcups.la
apcups_la_SOURCES = apcups.c
apcups_la_LDFLAGS = -module -avoid-version
apcups_la_LIBADD =
if BUILD_WITH_LIBSOCKET
apcups_la_LIBADD += -lsocket
endif
collectd_LDADD += "-dlopen" apcups.la
collectd_DEPENDENCIES += apcups.la
endif

if BUILD_PLUGIN_APPLE_SENSORS
pkglib_LTLIBRARIES += apple_sensors.la
apple_sensors_la_SOURCES = apple_sensors.c
apple_sensors_la_LDFLAGS = -module -avoid-version
apple_sensors_la_LIBADD = -lIOKit
collectd_LDADD += "-dlopen" apple_sensors.la
collectd_DEPENDENCIES += apple_sensors.la
endif

if BUILD_PLUGIN_ASCENT
pkglib_LTLIBRARIES += ascent.la
ascent_la_SOURCES = ascent.c
ascent_la_LDFLAGS = -module -avoid-version
ascent_la_CFLAGS = $(AM_CFLAGS) \
		$(BUILD_WITH_LIBCURL_CFLAGS) $(BUILD_WITH_LIBXML2_CFLAGS)
ascent_la_LIBADD = $(BUILD_WITH_LIBCURL_LIBS) $(BUILD_WITH_LIBXML2_LIBS)
collectd_LDADD += "-dlopen" ascent.la
collectd_DEPENDENCIES += ascent.la
endif

if BUILD_PLUGIN_BATTERY
pkglib_LTLIBRARIES += battery.la
battery_la_SOURCES = battery.c
battery_la_LDFLAGS = -module -avoid-version
battery_la_LIBADD =
if BUILD_WITH_LIBIOKIT
battery_la_LIBADD += -lIOKit
endif
collectd_LDADD += "-dlopen" battery.la
collectd_DEPENDENCIES += battery.la
endif

if BUILD_PLUGIN_BIND
pkglib_LTLIBRARIES += bind.la
bind_la_SOURCES = bind.c
bind_la_LDFLAGS = -module -avoid-version
bind_la_CFLAGS = $(AM_CFLAGS) \
		$(BUILD_WITH_LIBCURL_CFLAGS) $(BUILD_WITH_LIBXML2_CFLAGS)
bind_la_LIBADD = $(BUILD_WITH_LIBCURL_LIBS) $(BUILD_WITH_LIBXML2_LIBS)
collectd_LDADD += "-dlopen" bind.la
collectd_DEPENDENCIES += bind.la
endif

if BUILD_PLUGIN_CONNTRACK
pkglib_LTLIBRARIES += conntrack.la
conntrack_la_SOURCES = conntrack.c
conntrack_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" conntrack.la
collectd_DEPENDENCIES += conntrack.la
endif

if BUILD_PLUGIN_CONTEXTSWITCH
pkglib_LTLIBRARIES += contextswitch.la
contextswitch_la_SOURCES = contextswitch.c
contextswitch_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" contextswitch.la
collectd_DEPENDENCIES += contextswitch.la
endif

if BUILD_PLUGIN_CPU
pkglib_LTLIBRARIES += cpu.la
cpu_la_SOURCES = cpu.c
cpu_la_CFLAGS = $(AM_CFLAGS)
cpu_la_LDFLAGS = -module -avoid-version
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
collectd_LDADD += "-dlopen" cpu.la
collectd_DEPENDENCIES += cpu.la
endif

if BUILD_PLUGIN_CPUFREQ
pkglib_LTLIBRARIES += cpufreq.la
cpufreq_la_SOURCES = cpufreq.c
cpufreq_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" cpufreq.la
collectd_DEPENDENCIES += cpufreq.la
endif

if BUILD_PLUGIN_CSV
pkglib_LTLIBRARIES += csv.la
csv_la_SOURCES = csv.c
csv_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" csv.la
collectd_DEPENDENCIES += csv.la
endif

if BUILD_PLUGIN_CURL
pkglib_LTLIBRARIES += curl.la
curl_la_SOURCES = curl.c
curl_la_LDFLAGS = -module -avoid-version
curl_la_CFLAGS = $(AM_CFLAGS)
curl_la_LIBADD =
collectd_LDADD += "-dlopen" curl.la
if BUILD_WITH_LIBCURL
curl_la_CFLAGS += $(BUILD_WITH_LIBCURL_CFLAGS)
curl_la_LIBADD += $(BUILD_WITH_LIBCURL_LIBS)
endif
collectd_DEPENDENCIES += curl.la
endif

if BUILD_PLUGIN_CURL_JSON
pkglib_LTLIBRARIES += curl_json.la
curl_json_la_SOURCES = curl_json.c
curl_json_la_CFLAGS = $(AM_CFLAGS)
curl_json_la_LDFLAGS = -module -avoid-version $(BUILD_WITH_LIBYAJL_LDFLAGS)
curl_json_la_CPPFLAGS = $(BUILD_WITH_LIBYAJL_CPPFLAGS)
curl_json_la_LIBADD = $(BUILD_WITH_LIBYAJL_LIBS)
if BUILD_WITH_LIBCURL
curl_json_la_CFLAGS += $(BUILD_WITH_LIBCURL_CFLAGS)
curl_json_la_LIBADD += $(BUILD_WITH_LIBCURL_LIBS)
endif
collectd_LDADD += "-dlopen" curl_json.la
collectd_DEPENDENCIES += curl_json.la
endif

if BUILD_PLUGIN_CURL_XML
pkglib_LTLIBRARIES += curl_xml.la
curl_xml_la_SOURCES = curl_xml.c
curl_xml_la_LDFLAGS = -module -avoid-version
curl_xml_la_CFLAGS = $(AM_CFLAGS) \
		$(BUILD_WITH_LIBCURL_CFLAGS) $(BUILD_WITH_LIBXML2_CFLAGS)
curl_xml_la_LIBADD = $(BUILD_WITH_LIBCURL_LIBS) $(BUILD_WITH_LIBXML2_LIBS)
collectd_LDADD += "-dlopen" curl_xml.la
collectd_DEPENDENCIES += curl_xml.la
endif

if BUILD_PLUGIN_DBI
pkglib_LTLIBRARIES += dbi.la
dbi_la_SOURCES = dbi.c \
		 utils_db_query.c utils_db_query.h
dbi_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBDBI_CPPFLAGS)
dbi_la_LDFLAGS = -module -avoid-version $(BUILD_WITH_LIBDBI_LDFLAGS)
dbi_la_LIBADD = $(BUILD_WITH_LIBDBI_LIBS)
collectd_LDADD += "-dlopen" dbi.la
collectd_DEPENDENCIES += dbi.la
endif

if BUILD_PLUGIN_DF
pkglib_LTLIBRARIES += df.la
df_la_SOURCES = df.c utils_mount.c utils_mount.h
df_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" df.la
collectd_DEPENDENCIES += df.la
endif

if BUILD_PLUGIN_DISK
pkglib_LTLIBRARIES += disk.la
disk_la_SOURCES = disk.c
disk_la_CFLAGS = $(AM_CFLAGS)
disk_la_LDFLAGS = -module -avoid-version
disk_la_LIBADD = 
if BUILD_WITH_LIBKSTAT
disk_la_LIBADD += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
disk_la_LIBADD += -ldevinfo
endif
if BUILD_WITH_LIBIOKIT
disk_la_LIBADD += -lIOKit
endif
if BUILD_WITH_LIBSTATGRAB
disk_la_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)  
disk_la_LIBADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
endif
if BUILD_WITH_PERFSTAT
disk_la_LIBADD += -lperfstat
endif
collectd_LDADD += "-dlopen" disk.la
collectd_DEPENDENCIES += disk.la
endif

if BUILD_PLUGIN_DNS
pkglib_LTLIBRARIES += dns.la
dns_la_SOURCES = dns.c utils_dns.c utils_dns.h
dns_la_LDFLAGS = -module -avoid-version
dns_la_LIBADD = -lpcap -lpthread
collectd_LDADD += "-dlopen" dns.la
collectd_DEPENDENCIES += dns.la
endif

if BUILD_PLUGIN_EMAIL
pkglib_LTLIBRARIES += email.la
email_la_SOURCES = email.c
email_la_LDFLAGS = -module -avoid-version
email_la_LIBADD = -lpthread
collectd_LDADD += "-dlopen" email.la
collectd_DEPENDENCIES += email.la
endif

if BUILD_PLUGIN_ENTROPY
pkglib_LTLIBRARIES += entropy.la
entropy_la_SOURCES = entropy.c
entropy_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" entropy.la
collectd_DEPENDENCIES += entropy.la
endif

if BUILD_PLUGIN_EXEC
pkglib_LTLIBRARIES += exec.la
exec_la_SOURCES = exec.c \
		  utils_cmd_putnotif.c utils_cmd_putnotif.h \
		  utils_cmd_putval.c utils_cmd_putval.h
exec_la_LDFLAGS = -module -avoid-version
exec_la_LIBADD = -lpthread
collectd_LDADD += "-dlopen" exec.la
collectd_DEPENDENCIES += exec.la
endif

if BUILD_PLUGIN_ETHSTAT
pkglib_LTLIBRARIES += ethstat.la
ethstat_la_SOURCES = ethstat.c
ethstat_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" ethstat.la
collectd_DEPENDENCIES += ethstat.la
endif

if BUILD_PLUGIN_FILECOUNT
pkglib_LTLIBRARIES += filecount.la
filecount_la_SOURCES = filecount.c
filecount_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" filecount.la
collectd_DEPENDENCIES += filecount.la
endif

if BUILD_PLUGIN_GMOND
pkglib_LTLIBRARIES += gmond.la
gmond_la_SOURCES = gmond.c
gmond_la_CPPFLAGS = $(AM_CPPFLAGS) $(GANGLIA_CPPFLAGS)
gmond_la_LDFLAGS = -module -avoid-version $(GANGLIA_LDFLAGS)
gmond_la_LIBADD = $(GANGLIA_LIBS)
collectd_LDADD += "-dlopen" gmond.la
collectd_DEPENDENCIES += gmond.la
endif

if BUILD_PLUGIN_HDDTEMP
pkglib_LTLIBRARIES += hddtemp.la
hddtemp_la_SOURCES = hddtemp.c
hddtemp_la_LDFLAGS = -module -avoid-version
hddtemp_la_LIBADD =
if BUILD_WITH_LIBSOCKET
hddtemp_la_LIBADD += -lsocket
endif
collectd_LDADD += "-dlopen" hddtemp.la
collectd_DEPENDENCIES += hddtemp.la
endif

if BUILD_PLUGIN_INTERFACE
pkglib_LTLIBRARIES += interface.la
interface_la_SOURCES = interface.c
interface_la_CFLAGS = $(AM_CFLAGS)
interface_la_LDFLAGS = -module -avoid-version
interface_la_LIBADD =
collectd_LDADD += "-dlopen" interface.la
collectd_DEPENDENCIES += interface.la
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

if BUILD_PLUGIN_IPTABLES
pkglib_LTLIBRARIES += iptables.la
iptables_la_SOURCES = iptables.c
iptables_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBIPTC_CPPFLAGS)
iptables_la_LDFLAGS = -module -avoid-version $(BUILD_WITH_LIBIPTC_LDFLAGS)
iptables_la_LIBADD = -liptc
collectd_LDADD += "-dlopen" iptables.la
collectd_DEPENDENCIES += iptables.la
endif

if BUILD_PLUGIN_IPMI
pkglib_LTLIBRARIES += ipmi.la
ipmi_la_SOURCES = ipmi.c
ipmi_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_OPENIPMI_CFLAGS)
ipmi_la_LDFLAGS = -module -avoid-version
ipmi_la_LIBADD = $(BUILD_WITH_OPENIPMI_LIBS)
collectd_LDADD += "-dlopen" ipmi.la
collectd_DEPENDENCIES += ipmi.la
endif

if BUILD_PLUGIN_IPVS
pkglib_LTLIBRARIES += ipvs.la
ipvs_la_SOURCES = ipvs.c
if IP_VS_H_NEEDS_KERNEL_CFLAGS
ipvs_la_CFLAGS = $(AM_CFLAGS) $(KERNEL_CFLAGS)
endif
ipvs_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" ipvs.la
collectd_DEPENDENCIES += ipvs.la
endif

if BUILD_PLUGIN_IRQ
pkglib_LTLIBRARIES += irq.la
irq_la_SOURCES = irq.c
irq_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" irq.la
collectd_DEPENDENCIES += irq.la
endif

if BUILD_PLUGIN_JAVA
pkglib_LTLIBRARIES += java.la
java_la_SOURCES = java.c
java_la_CPPFLAGS = $(AM_CPPFLAGS) $(JAVA_CPPFLAGS)
java_la_CFLAGS = $(AM_CFLAGS) $(JAVA_CFLAGS)
java_la_LDFLAGS = -module -avoid-version $(JAVA_LDFLAGS)
java_la_LIBADD = $(JAVA_LIBS)
collectd_LDADD += "-dlopen" java.la
collectd_DEPENDENCIES += java.la
endif

if BUILD_PLUGIN_LIBVIRT
pkglib_LTLIBRARIES += libvirt.la
libvirt_la_SOURCES = libvirt.c
libvirt_la_CFLAGS = $(AM_CFLAGS) \
		$(BUILD_WITH_LIBVIRT_CFLAGS) $(BUILD_WITH_LIBXML2_CFLAGS)
libvirt_la_LIBADD = $(BUILD_WITH_LIBVIRT_LIBS) $(BUILD_WITH_LIBXML2_LIBS)
libvirt_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" libvirt.la
collectd_DEPENDENCIES += libvirt.la
endif

if BUILD_PLUGIN_LOAD
pkglib_LTLIBRARIES += load.la
load_la_SOURCES = load.c
load_la_CFLAGS = $(AM_CFLAGS)
load_la_LDFLAGS = -module -avoid-version
load_la_LIBADD =
collectd_LDADD += "-dlopen" load.la
collectd_DEPENDENCIES += load.la
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
logfile_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" logfile.la
collectd_DEPENDENCIES += logfile.la
endif

if BUILD_PLUGIN_LPAR
pkglib_LTLIBRARIES += lpar.la
lpar_la_SOURCES = lpar.c
lpar_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" lpar.la
collectd_DEPENDENCIES += lpar.la
lpar_la_LIBADD = -lperfstat
endif

if BUILD_PLUGIN_MADWIFI
pkglib_LTLIBRARIES += madwifi.la
madwifi_la_SOURCES = madwifi.c madwifi.h
madwifi_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" madwifi.la
collectd_DEPENDENCIES += madwifi.la
endif

if BUILD_PLUGIN_MATCH_EMPTY_COUNTER
pkglib_LTLIBRARIES += match_empty_counter.la
match_empty_counter_la_SOURCES = match_empty_counter.c
match_empty_counter_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" match_empty_counter.la
collectd_DEPENDENCIES += match_empty_counter.la
endif

if BUILD_PLUGIN_MATCH_HASHED
pkglib_LTLIBRARIES += match_hashed.la
match_hashed_la_SOURCES = match_hashed.c
match_hashed_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" match_hashed.la
collectd_DEPENDENCIES += match_hashed.la
endif

if BUILD_PLUGIN_MATCH_REGEX
pkglib_LTLIBRARIES += match_regex.la
match_regex_la_SOURCES = match_regex.c
match_regex_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" match_regex.la
collectd_DEPENDENCIES += match_regex.la
endif

if BUILD_PLUGIN_MATCH_TIMEDIFF
pkglib_LTLIBRARIES += match_timediff.la
match_timediff_la_SOURCES = match_timediff.c
match_timediff_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" match_timediff.la
collectd_DEPENDENCIES += match_timediff.la
endif

if BUILD_PLUGIN_MATCH_VALUE
pkglib_LTLIBRARIES += match_value.la
match_value_la_SOURCES = match_value.c
match_value_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" match_value.la
collectd_DEPENDENCIES += match_value.la
endif

if BUILD_PLUGIN_MBMON
pkglib_LTLIBRARIES += mbmon.la
mbmon_la_SOURCES = mbmon.c
mbmon_la_LDFLAGS = -module -avoid-version
mbmon_la_LIBADD =
if BUILD_WITH_LIBSOCKET
mbmon_la_LIBADD += -lsocket
endif
collectd_LDADD += "-dlopen" mbmon.la
collectd_DEPENDENCIES += mbmon.la
endif

if BUILD_PLUGIN_MD
pkglib_LTLIBRARIES += md.la
md_la_SOURCES = md.c
md_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" md.la
collectd_DEPENDENCIES += md.la
endif

if BUILD_PLUGIN_MEMCACHEC
pkglib_LTLIBRARIES += memcachec.la
memcachec_la_SOURCES = memcachec.c
memcachec_la_LDFLAGS = -module -avoid-version $(BUILD_WITH_LIBMEMCACHED_LDFLAGS)
memcachec_la_CPPFLAGS = $(BUILD_WITH_LIBMEMCACHED_CPPFLAGS)
memcachec_la_LIBADD = $(BUILD_WITH_LIBMEMCACHED_LIBS)
collectd_LDADD += "-dlopen" memcachec.la
collectd_DEPENDENCIES += memcachec.la
endif

if BUILD_PLUGIN_MEMCACHED
pkglib_LTLIBRARIES += memcached.la
memcached_la_SOURCES = memcached.c
memcached_la_LDFLAGS = -module -avoid-version
memcached_la_LIBADD =
if BUILD_WITH_LIBSOCKET
memcached_la_LIBADD += -lsocket
endif
collectd_LDADD += "-dlopen" memcached.la
collectd_DEPENDENCIES += memcached.la
endif

if BUILD_PLUGIN_MEMORY
pkglib_LTLIBRARIES += memory.la
memory_la_SOURCES = memory.c
memory_la_CFLAGS = $(AM_CFLAGS)
memory_la_LDFLAGS = -module -avoid-version
memory_la_LIBADD =
collectd_LDADD += "-dlopen" memory.la
collectd_DEPENDENCIES += memory.la
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

if BUILD_PLUGIN_MODBUS
pkglib_LTLIBRARIES += modbus.la
modbus_la_SOURCES = modbus.c
modbus_la_LDFLAGS = -module -avoid-version
modbus_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBMODBUS_CFLAGS)
modbus_la_LIBADD = $(BUILD_WITH_LIBMODBUS_LIBS)
collectd_LDADD += "-dlopen" modbus.la
collectd_DEPENDENCIES += modbus.la
endif

if BUILD_PLUGIN_MULTIMETER
pkglib_LTLIBRARIES += multimeter.la
multimeter_la_SOURCES = multimeter.c
multimeter_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" multimeter.la
collectd_DEPENDENCIES += multimeter.la
endif

if BUILD_PLUGIN_MYSQL
pkglib_LTLIBRARIES += mysql.la
mysql_la_SOURCES = mysql.c
mysql_la_LDFLAGS = -module -avoid-version
mysql_la_CFLAGS = $(AM_CFLAGS)
mysql_la_LIBADD =
collectd_LDADD += "-dlopen" mysql.la
if BUILD_WITH_LIBMYSQL
mysql_la_CFLAGS += $(BUILD_WITH_LIBMYSQL_CFLAGS)
mysql_la_LIBADD += $(BUILD_WITH_LIBMYSQL_LIBS)
endif
collectd_DEPENDENCIES += mysql.la
endif

if BUILD_PLUGIN_NETAPP
pkglib_LTLIBRARIES += netapp.la
netapp_la_SOURCES = netapp.c
netapp_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBNETAPP_CPPFLAGS)
netapp_la_LDFLAGS = -module -avoid-version $(LIBNETAPP_LDFLAGS)
netapp_la_LIBADD = $(LIBNETAPP_LIBS)
collectd_LDADD += "-dlopen" netapp.la
collectd_DEPENDENCIES += netapp.la
endif

if BUILD_PLUGIN_NETLINK
pkglib_LTLIBRARIES += netlink.la
netlink_la_SOURCES = netlink.c
netlink_la_LDFLAGS = -module -avoid-version
netlink_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBNETLINK_CFLAGS)
netlink_la_LIBADD = $(BUILD_WITH_LIBNETLINK_LIBS)
collectd_LDADD += "-dlopen" netlink.la
collectd_DEPENDENCIES += netlink.la
endif

if BUILD_PLUGIN_NETWORK
pkglib_LTLIBRARIES += network.la
network_la_SOURCES = network.c network.h \
		     utils_fbhash.c utils_fbhash.h
network_la_CPPFLAGS = $(AM_CPPFLAGS)
network_la_LDFLAGS = -module -avoid-version
network_la_LIBADD = -lpthread
if BUILD_WITH_LIBSOCKET
network_la_LIBADD += -lsocket
endif
if BUILD_WITH_LIBGCRYPT
network_la_CPPFLAGS += $(GCRYPT_CPPFLAGS)
network_la_LDFLAGS += $(GCRYPT_LDFLAGS)
network_la_LIBADD += $(GCRYPT_LIBS)
endif
collectd_LDADD += "-dlopen" network.la
collectd_DEPENDENCIES += network.la
endif

if BUILD_PLUGIN_NFS
pkglib_LTLIBRARIES += nfs.la
nfs_la_SOURCES = nfs.c
nfs_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" nfs.la
collectd_DEPENDENCIES += nfs.la
endif

if BUILD_PLUGIN_FSCACHE
pkglib_LTLIBRARIES += fscache.la
fscache_la_SOURCES = fscache.c
fscache_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" fscache.la
collectd_DEPENDENCIES += fscache.la
endif

if BUILD_PLUGIN_NGINX
pkglib_LTLIBRARIES += nginx.la
nginx_la_SOURCES = nginx.c
nginx_la_CFLAGS = $(AM_CFLAGS)
nginx_la_LIBADD =
nginx_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBCURL
nginx_la_CFLAGS += $(BUILD_WITH_LIBCURL_CFLAGS)
nginx_la_LIBADD += $(BUILD_WITH_LIBCURL_LIBS)
endif
collectd_LDADD += "-dlopen" nginx.la
collectd_DEPENDENCIES += nginx.la
endif

if BUILD_PLUGIN_NOTIFY_DESKTOP
pkglib_LTLIBRARIES += notify_desktop.la
notify_desktop_la_SOURCES = notify_desktop.c
notify_desktop_la_CFLAGS = $(AM_CFLAGS) $(LIBNOTIFY_CFLAGS)
notify_desktop_la_LDFLAGS = -module -avoid-version
notify_desktop_la_LIBADD = $(LIBNOTIFY_LIBS)
collectd_LDADD += "-dlopen" notify_desktop.la
collectd_DEPENDENCIES += notify_desktop.la
endif

if BUILD_PLUGIN_NOTIFY_EMAIL
pkglib_LTLIBRARIES += notify_email.la
notify_email_la_SOURCES = notify_email.c
notify_email_la_LDFLAGS = -module -avoid-version
notify_email_la_LIBADD = -lesmtp -lssl -lcrypto -lpthread -ldl
collectd_LDADD += "-dlopen" notify_email.la
collectd_DEPENDENCIES += notify_email.la
endif

if BUILD_PLUGIN_NTPD
pkglib_LTLIBRARIES += ntpd.la
ntpd_la_SOURCES = ntpd.c
ntpd_la_LDFLAGS = -module -avoid-version
ntpd_la_LIBADD =
if BUILD_WITH_LIBSOCKET
ntpd_la_LIBADD += -lsocket
endif
collectd_LDADD += "-dlopen" ntpd.la
collectd_DEPENDENCIES += ntpd.la
endif

if BUILD_PLUGIN_NUMA
pkglib_LTLIBRARIES += numa.la
numa_la_SOURCES = numa.c
numa_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" numa.la
collectd_DEPENDENCIES += numa.la
endif

if BUILD_PLUGIN_NUT
pkglib_LTLIBRARIES += nut.la
nut_la_SOURCES = nut.c
nut_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBUPSCLIENT_CFLAGS)
nut_la_LDFLAGS = -module -avoid-version
nut_la_LIBADD = -lpthread $(BUILD_WITH_LIBUPSCLIENT_LIBS)
collectd_LDADD += "-dlopen" nut.la
collectd_DEPENDENCIES += nut.la
endif

if BUILD_PLUGIN_OLSRD
pkglib_LTLIBRARIES += olsrd.la
olsrd_la_SOURCES = olsrd.c
olsrd_la_LDFLAGS = -module -avoid-version
olsrd_la_LIBADD = 
if BUILD_WITH_LIBSOCKET
olsrd_la_LIBADD += -lsocket
endif
collectd_LDADD += "-dlopen" olsrd.la
collectd_DEPENDENCIES += olsrd.la
endif

if BUILD_PLUGIN_ONEWIRE
pkglib_LTLIBRARIES += onewire.la
onewire_la_SOURCES = onewire.c
onewire_la_CFLAGS = $(AM_CFLAGS)
onewire_la_CPPFLAGS = $(BUILD_WITH_LIBOWCAPI_CPPFLAGS)
onewire_la_LIBADD = $(BUILD_WITH_LIBOWCAPI_LIBS)
onewire_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" onewire.la
collectd_DEPENDENCIES += onewire.la
endif

if BUILD_PLUGIN_OPENVPN
pkglib_LTLIBRARIES += openvpn.la
openvpn_la_SOURCES = openvpn.c
openvpn_la_CFLAGS = $(AM_CFLAGS)
openvpn_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" openvpn.la
collectd_DEPENDENCIES += openvpn.la
endif

if BUILD_PLUGIN_ORACLE
pkglib_LTLIBRARIES += oracle.la
oracle_la_SOURCES = oracle.c \
	utils_db_query.c utils_db_query.h
oracle_la_CFLAGS = $(AM_CFLAGS)
oracle_la_CPPFLAGS = $(BUILD_WITH_ORACLE_CFLAGS)
oracle_la_LIBADD = $(BUILD_WITH_ORACLE_LIBS)
oracle_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" oracle.la
collectd_DEPENDENCIES += oracle.la
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
perl_la_LDFLAGS = -module -avoid-version \
		$(PERL_LDFLAGS)
collectd_LDADD += "-dlopen" perl.la
collectd_DEPENDENCIES += perl.la
endif

if BUILD_PLUGIN_PINBA
BUILT_SOURCES += pinba.pb-c.c pinba.pb-c.h
CLEANFILES += pinba.pb-c.c pinba.pb-c.h
pkglib_LTLIBRARIES += pinba.la
pinba_la_SOURCES = pinba.c
pinba_la_LDFLAGS = -module -avoid-version
pinba_la_LIBADD = -lprotobuf-c
collectd_LDADD += "-dlopen" pinba.la
collectd_DEPENDENCIES += pinba.la
endif

if BUILD_PLUGIN_PING
pkglib_LTLIBRARIES += ping.la
ping_la_SOURCES = ping.c
ping_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBOPING_CPPFLAGS)
ping_la_LDFLAGS = -module -avoid-version $(BUILD_WITH_LIBOPING_LDFLAGS)
ping_la_LIBADD = -loping -lm
collectd_LDADD += "-dlopen" ping.la
collectd_DEPENDENCIES += ping.la
endif

if BUILD_PLUGIN_POSTGRESQL
pkglib_LTLIBRARIES += postgresql.la
postgresql_la_SOURCES = postgresql.c \
		 utils_db_query.c utils_db_query.h
postgresql_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBPQ_CPPFLAGS)
postgresql_la_LDFLAGS = -module -avoid-version \
		$(BUILD_WITH_LIBPQ_LDFLAGS)
postgresql_la_LIBADD = -lpq
collectd_LDADD += "-dlopen" postgresql.la
collectd_DEPENDENCIES += postgresql.la
endif

if BUILD_PLUGIN_POWERDNS
pkglib_LTLIBRARIES += powerdns.la
powerdns_la_SOURCES = powerdns.c
powerdns_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" powerdns.la
collectd_DEPENDENCIES += powerdns.la
endif

if BUILD_PLUGIN_PYTHON
pkglib_LTLIBRARIES += python.la
python_la_SOURCES = python.c pyconfig.c pyvalues.c cpython.h
python_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_PYTHON_CPPFLAGS)
python_la_CFLAGS = $(AM_CFLAGS)
if COMPILER_IS_GCC
python_la_CFLAGS += -fno-strict-aliasing -Wno-strict-aliasing
endif
python_la_LDFLAGS = -module -avoid-version $(BUILD_WITH_PYTHON_LDFLAGS)
python_la_LIBADD = $(BUILD_WITH_PYTHON_LIBS)
collectd_LDADD += "-dlopen" python.la
collectd_DEPENDENCIES += python.la
endif

if BUILD_PLUGIN_PROCESSES
pkglib_LTLIBRARIES += processes.la
processes_la_SOURCES = processes.c
processes_la_LDFLAGS = -module -avoid-version
processes_la_LIBADD =
collectd_LDADD += "-dlopen" processes.la
collectd_DEPENDENCIES += processes.la
if BUILD_WITH_LIBKVM_GETPROCS
processes_la_LIBADD += -lkvm
endif
endif

if BUILD_PLUGIN_PROTOCOLS
pkglib_LTLIBRARIES += protocols.la
protocols_la_SOURCES = protocols.c
protocols_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" protocols.la
collectd_DEPENDENCIES += protocols.la
endif

if BUILD_PLUGIN_REDIS
pkglib_LTLIBRARIES += redis.la
redis_la_SOURCES = redis.c
redis_la_LDFLAGS = -module -avoid-version $(BUILD_WITH_LIBCREDIS_LDFLAGS)
redis_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBCREDIS_CPPFLAGS)
redis_la_LIBADD = -lcredis
collectd_LDADD += "-dlopen" redis.la
collectd_DEPENDENCIES += redis.la
endif

if BUILD_PLUGIN_ROUTEROS
pkglib_LTLIBRARIES += routeros.la
routeros_la_SOURCES = routeros.c
routeros_la_CPPFLAGS = $(BUILD_WITH_LIBROUTEROS_CPPFLAGS)
routeros_la_LDFLAGS = -module -avoid-version $(BUILD_WITH_LIBROUTEROS_LDFLAGS)
routeros_la_LIBADD = -lrouteros
collectd_LDADD += "-dlopen" routeros.la
collectd_DEPENDENCIES += routeros.la
endif

if BUILD_PLUGIN_RRDCACHED
pkglib_LTLIBRARIES += rrdcached.la
rrdcached_la_SOURCES = rrdcached.c utils_rrdcreate.c utils_rrdcreate.h
rrdcached_la_LDFLAGS = -module -avoid-version
rrdcached_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBRRD_CFLAGS)
rrdcached_la_LIBADD = $(BUILD_WITH_LIBRRD_LDFLAGS)
collectd_LDADD += "-dlopen" rrdcached.la
collectd_DEPENDENCIES += rrdcached.la
endif

if BUILD_PLUGIN_RRDTOOL
pkglib_LTLIBRARIES += rrdtool.la
rrdtool_la_SOURCES = rrdtool.c utils_rrdcreate.c utils_rrdcreate.h
rrdtool_la_LDFLAGS = -module -avoid-version
rrdtool_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBRRD_CFLAGS)
rrdtool_la_LIBADD = $(BUILD_WITH_LIBRRD_LDFLAGS)
collectd_LDADD += "-dlopen" rrdtool.la
collectd_DEPENDENCIES += rrdtool.la
endif

if BUILD_PLUGIN_SENSORS
pkglib_LTLIBRARIES += sensors.la
sensors_la_SOURCES = sensors.c
sensors_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBSENSORS_CFLAGS)
sensors_la_LDFLAGS = -module -avoid-version $(BUILD_WITH_LIBSENSORS_LDFLAGS)
sensors_la_LIBADD = -lsensors
collectd_LDADD += "-dlopen" sensors.la
collectd_DEPENDENCIES += sensors.la
endif

if BUILD_PLUGIN_SERIAL
pkglib_LTLIBRARIES += serial.la
serial_la_SOURCES = serial.c
serial_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" serial.la
collectd_DEPENDENCIES += serial.la
endif

if BUILD_PLUGIN_SNMP
pkglib_LTLIBRARIES += snmp.la
snmp_la_SOURCES = snmp.c
snmp_la_LDFLAGS = -module -avoid-version
snmp_la_CFLAGS = $(AM_CFLAGS)
snmp_la_LIBADD =
if BUILD_WITH_LIBNETSNMP
snmp_la_CFLAGS += $(BUILD_WITH_LIBSNMP_CFLAGS)
snmp_la_LIBADD += $(BUILD_WITH_LIBSNMP_LIBS)
endif
if BUILD_WITH_LIBPTHREAD
snmp_la_LIBADD += -lpthread
endif
collectd_LDADD += "-dlopen" snmp.la
collectd_DEPENDENCIES += snmp.la
endif

if BUILD_PLUGIN_SWAP
pkglib_LTLIBRARIES += swap.la
swap_la_SOURCES = swap.c
swap_la_CFLAGS = $(AM_CFLAGS)
swap_la_LDFLAGS = -module -avoid-version
swap_la_LIBADD =
collectd_LDADD += "-dlopen" swap.la
collectd_DEPENDENCIES += swap.la
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
syslog_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" syslog.la
collectd_DEPENDENCIES += syslog.la
endif

if BUILD_PLUGIN_TABLE
pkglib_LTLIBRARIES += table.la
table_la_SOURCES = table.c
table_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" table.la
collectd_DEPENDENCIES += table.la
endif

if BUILD_PLUGIN_TAIL
pkglib_LTLIBRARIES += tail.la
tail_la_SOURCES = tail.c
tail_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" tail.la
collectd_DEPENDENCIES += tail.la
endif

if BUILD_PLUGIN_TAPE
pkglib_LTLIBRARIES += tape.la
tape_la_SOURCES = tape.c
tape_la_LDFLAGS = -module -avoid-version
tape_la_LIBADD = -lkstat -ldevinfo
collectd_LDADD += "-dlopen" tape.la
collectd_DEPENDENCIES += tape.la
endif

if BUILD_PLUGIN_TARGET_NOTIFICATION
pkglib_LTLIBRARIES += target_notification.la
target_notification_la_SOURCES = target_notification.c
target_notification_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" target_notification.la
collectd_DEPENDENCIES += target_notification.la
endif

if BUILD_PLUGIN_TARGET_REPLACE
pkglib_LTLIBRARIES += target_replace.la
target_replace_la_SOURCES = target_replace.c
target_replace_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" target_replace.la
collectd_DEPENDENCIES += target_replace.la
endif

if BUILD_PLUGIN_TARGET_SCALE
pkglib_LTLIBRARIES += target_scale.la
target_scale_la_SOURCES = target_scale.c
target_scale_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" target_scale.la
collectd_DEPENDENCIES += target_scale.la
endif

if BUILD_PLUGIN_TARGET_SET
pkglib_LTLIBRARIES += target_set.la
target_set_la_SOURCES = target_set.c
target_set_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" target_set.la
collectd_DEPENDENCIES += target_set.la
endif

if BUILD_PLUGIN_TARGET_V5UPGRADE
pkglib_LTLIBRARIES += target_v5upgrade.la
target_v5upgrade_la_SOURCES = target_v5upgrade.c
target_v5upgrade_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" target_v5upgrade.la
collectd_DEPENDENCIES += target_v5upgrade.la
endif

if BUILD_PLUGIN_TCPCONNS
pkglib_LTLIBRARIES += tcpconns.la
tcpconns_la_SOURCES = tcpconns.c
tcpconns_la_LDFLAGS = -module -avoid-version
tcpconns_la_LIBADD =
collectd_LDADD += "-dlopen" tcpconns.la
collectd_DEPENDENCIES += tcpconns.la
if BUILD_WITH_LIBKVM_NLIST
tcpconns_la_LIBADD += -lkvm
endif
endif

if BUILD_PLUGIN_TEAMSPEAK2
pkglib_LTLIBRARIES += teamspeak2.la
teamspeak2_la_SOURCES = teamspeak2.c
teamspeak2_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" teamspeak2.la
collectd_DEPENDENCIES += teamspeak2.la
endif

if BUILD_PLUGIN_TED
pkglib_LTLIBRARIES += ted.la
ted_la_SOURCES = ted.c
ted_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" ted.la
collectd_DEPENDENCIES += ted.la
endif

if BUILD_PLUGIN_THERMAL
pkglib_LTLIBRARIES += thermal.la
thermal_la_SOURCES = thermal.c
thermal_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" thermal.la
collectd_DEPENDENCIES += thermal.la
endif

if BUILD_PLUGIN_THRESHOLD
pkglib_LTLIBRARIES += threshold.la
threshold_la_SOURCES = threshold.c
threshold_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" threshold.la
collectd_DEPENDENCIES += threshold.la
endif

if BUILD_PLUGIN_TOKYOTYRANT
pkglib_LTLIBRARIES += tokyotyrant.la
tokyotyrant_la_SOURCES = tokyotyrant.c
tokyotyrant_la_CPPFLAGS  = $(AM_CPPFLAGS) $(BUILD_WITH_LIBTOKYOTYRANT_CPPFLAGS)
tokyotyrant_la_LDFLAGS = -module -avoid-version $(BUILD_WITH_LIBTOKYOTYRANT_LDFLAGS)
tokyotyrant_la_LIBADD  = $(BUILD_WITH_LIBTOKYOTYRANT_LIBS)
if BUILD_WITH_LIBSOCKET
tokyotyrant_la_LIBADD += -lsocket
endif
collectd_LDADD += "-dlopen" tokyotyrant.la
collectd_DEPENDENCIES += tokyotyrant.la
endif

if BUILD_PLUGIN_UNIXSOCK
pkglib_LTLIBRARIES += unixsock.la
unixsock_la_SOURCES = unixsock.c \
		      utils_cmd_flush.h utils_cmd_flush.c \
		      utils_cmd_getval.h utils_cmd_getval.c \
		      utils_cmd_listval.h utils_cmd_listval.c \
		      utils_cmd_putval.h utils_cmd_putval.c \
		      utils_cmd_putnotif.h utils_cmd_putnotif.c
unixsock_la_LDFLAGS = -module -avoid-version
unixsock_la_LIBADD = -lpthread
collectd_LDADD += "-dlopen" unixsock.la
collectd_DEPENDENCIES += unixsock.la
endif

if BUILD_PLUGIN_UPTIME
pkglib_LTLIBRARIES += uptime.la
uptime_la_SOURCES = uptime.c
uptime_la_CFLAGS = $(AM_CFLAGS)
uptime_la_LDFLAGS = -module -avoid-version
uptime_la_LIBADD =
if BUILD_WITH_LIBKSTAT
uptime_la_LIBADD += -lkstat
endif
collectd_LDADD += "-dlopen" uptime.la
collectd_DEPENDENCIES += uptime.la
endif

if BUILD_PLUGIN_USERS
pkglib_LTLIBRARIES += users.la
users_la_SOURCES = users.c
users_la_CFLAGS = $(AM_CFLAGS)
users_la_LDFLAGS = -module -avoid-version
users_la_LIBADD =
if BUILD_WITH_LIBSTATGRAB
users_la_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)
users_la_LIBADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
endif
collectd_LDADD += "-dlopen" users.la
collectd_DEPENDENCIES += users.la
endif

if BUILD_PLUGIN_UUID
pkglib_LTLIBRARIES += uuid.la
uuid_la_SOURCES = uuid.c
uuid_la_CFLAGS  = $(AM_CFLAGS) $(BUILD_WITH_LIBHAL_CFLAGS)
uuid_la_LIBADD  = $(BUILD_WITH_LIBHAL_LIBS)
uuid_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" uuid.la
collectd_DEPENDENCIES += uuid.la
endif

if BUILD_PLUGIN_VARNISH
pkglib_LTLIBRARIES += varnish.la
varnish_la_SOURCES = varnish.c
varnish_la_LDFLAGS = -module -avoid-version
varnish_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBVARNISH_CFLAGS)
varnish_la_LIBADD = $(BUILD_WITH_LIBVARNISH_LIBS)
collectd_LDADD += "-dlopen" varnish.la
collectd_DEPENDENCIES += varnish.la
endif

if BUILD_PLUGIN_VMEM
pkglib_LTLIBRARIES += vmem.la
vmem_la_SOURCES = vmem.c
vmem_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" vmem.la
collectd_DEPENDENCIES += vmem.la
endif

if BUILD_PLUGIN_VSERVER
pkglib_LTLIBRARIES += vserver.la
vserver_la_SOURCES = vserver.c
vserver_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" vserver.la
collectd_DEPENDENCIES += vserver.la
endif

if BUILD_PLUGIN_WIRELESS
pkglib_LTLIBRARIES += wireless.la
wireless_la_SOURCES = wireless.c
wireless_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" wireless.la
collectd_DEPENDENCIES += wireless.la
endif

if BUILD_PLUGIN_WRITE_GRAPHITE
pkglib_LTLIBRARIES += write_graphite.la
write_graphite_la_SOURCES = write_graphite.c \
			utils_format_json.c utils_format_json.h
write_graphite_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" write_graphite.la
collectd_DEPENDENCIES += write_graphite.la
endif

if BUILD_PLUGIN_WRITE_HTTP
pkglib_LTLIBRARIES += write_http.la
write_http_la_SOURCES = write_http.c \
			utils_format_json.c utils_format_json.h
write_http_la_LDFLAGS = -module -avoid-version
write_http_la_CFLAGS = $(AM_CFLAGS)
write_http_la_LIBADD =
collectd_LDADD += "-dlopen" write_http.la
if BUILD_WITH_LIBCURL
write_http_la_CFLAGS += $(BUILD_WITH_LIBCURL_CFLAGS)
write_http_la_LIBADD += $(BUILD_WITH_LIBCURL_LIBS)
endif
collectd_DEPENDENCIES += write_http.la
endif

if BUILD_PLUGIN_WRITE_MONGODB
pkglib_LTLIBRARIES += write_mongodb.la
write_mongodb_la_SOURCES = write_mongodb.c
write_mongodb_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBMONGOC_CPPFLAGS)
write_mongodb_la_LDFLAGS = -module -avoid-version $(BUILD_WITH_LIBMONGOC_LDFLAGS)
write_mongodb_la_LIBADD = -lmongoc
collectd_LDADD += "-dlopen" write_mongodb.la
collectd_DEPENDENCIES += write_mongodb.la
endif

if BUILD_PLUGIN_WRITE_REDIS
pkglib_LTLIBRARIES += write_redis.la
write_redis_la_SOURCES = write_redis.c
write_redis_la_LDFLAGS = -module -avoid-version $(BUILD_WITH_LIBCREDIS_LDFLAGS)
write_redis_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBCREDIS_CPPFLAGS)
write_redis_la_LIBADD = -lcredis
collectd_LDADD += "-dlopen" write_redis.la
collectd_DEPENDENCIES += write_redis.la
endif

if BUILD_PLUGIN_XMMS
pkglib_LTLIBRARIES += xmms.la
xmms_la_SOURCES = xmms.c
xmms_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBXMMS_CFLAGS)
xmms_la_LDFLAGS = -module -avoid-version
xmms_la_LIBADD = $(BUILD_WITH_LIBXMMS_LIBS)
collectd_LDADD += "-dlopen" xmms.la
collectd_DEPENDENCIES += xmms.la
endif

if BUILD_PLUGIN_ZFS_ARC
pkglib_LTLIBRARIES += zfs_arc.la
zfs_arc_la_SOURCES = zfs_arc.c
zfs_arc_la_CFLAGS = $(AM_CFLAGS)
zfs_arc_la_LDFLAGS = -module -avoid-version
zfs_arc_la_LIBADD = -lkstat
collectd_LDADD += "-dlopen" zfs_arc.la
collectd_DEPENDENCIES += zfs_arc.la
endif

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
		collectd-threshold.5 \
		collectd-unixsock.5 \
		types.db.5

#collectd_1_SOURCES = collectd.pod

EXTRA_DIST = types.db pinba.proto

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

pinba.pb-c.c pinba.pb-c.h: pinba.proto
	protoc-c --c_out $(builddir) pinba.proto

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
