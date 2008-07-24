SUBDIRS =
if BUILD_WITH_OWN_LIBIPTC
SUBDIRS += libiptc
endif
if BUILD_WITH_OWN_LIBOCONFIG
SUBDIRS += liboconfig
endif
if BUILD_WITH_OWN_LIBOPING
SUBDIRS += liboping
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
bin_PROGRAMS = collectd-nagios

collectd_SOURCES = collectd.c collectd.h \
		   common.c common.h \
		   configfile.c configfile.h \
		   plugin.c plugin.h \
		   utils_avltree.c utils_avltree.h \
		   utils_cache.c utils_cache.h \
		   utils_complain.c utils_complain.h \
		   utils_ignorelist.c utils_ignorelist.h \
		   utils_llist.c utils_llist.h \
		   utils_tail_match.c utils_tail_match.h \
		   utils_match.c utils_match.h \
		   utils_mount.c utils_mount.h \
		   utils_tail.c utils_tail.h \
		   utils_threshold.c utils_threshold.h \
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

# The daemon needs to call sg_init, so we need to link it against libstatgrab,
# too. -octo
if BUILD_WITH_LIBSTATGRAB
collectd_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)
collectd_LDADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
endif

if BUILD_WITH_OWN_LIBOCONFIG
collectd_LDADD += $(LIBLTDL) liboconfig/liboconfig.la
collectd_DEPENDENCIES += $(LIBLTDL) liboconfig/liboconfig.la
else
collectd_LDADD += -loconfig
endif

collectdmon_SOURCES = collectdmon.c
collectdmon_CPPFLAGS = $(AM_CPPFLAGS)

collectd_nagios_SOURCES = collectd-nagios.c
collectd_nagios_LDFLAGS =
if BUILD_WITH_LIBSOCKET
collectd_nagios_LDFLAGS += -lsocket
endif

pkglib_LTLIBRARIES = 

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
if BUILD_WITH_LIBSOCKET
apcups_la_LDFLAGS += -lsocket
endif
collectd_LDADD += "-dlopen" apcups.la
collectd_DEPENDENCIES += apcups.la
endif

if BUILD_PLUGIN_APPLE_SENSORS
pkglib_LTLIBRARIES += apple_sensors.la
apple_sensors_la_SOURCES = apple_sensors.c
apple_sensors_la_LDFLAGS = -module -avoid-version -lIOKit
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
collectd_LDADD += "-dlopen" apache.la
collectd_DEPENDENCIES += ascent.la
endif

if BUILD_PLUGIN_BATTERY
pkglib_LTLIBRARIES += battery.la
battery_la_SOURCES = battery.c
battery_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBIOKIT
battery_la_LDFLAGS += -lIOKit
endif
collectd_LDADD += "-dlopen" battery.la
collectd_DEPENDENCIES += battery.la
endif

if BUILD_PLUGIN_CPU
pkglib_LTLIBRARIES += cpu.la
cpu_la_SOURCES = cpu.c
cpu_la_CFLAGS = $(AM_CFLAGS)
cpu_la_LDFLAGS = -module -avoid-version
cpu_la_LIBADD = 
if BUILD_WITH_LIBKSTAT
cpu_la_LDFLAGS += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
cpu_la_LDFLAGS += -ldevinfo
endif
if BUILD_WITH_LIBSTATGRAB
cpu_la_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)
cpu_la_LIBADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
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

if BUILD_PLUGIN_DF
pkglib_LTLIBRARIES += df.la
df_la_SOURCES = df.c
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
disk_la_LDFLAGS += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
disk_la_LDFLAGS += -ldevinfo
endif
if BUILD_WITH_LIBIOKIT
disk_la_LDFLAGS += -lIOKit
endif
if BUILD_WITH_LIBSTATGRAB
disk_la_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)  
disk_la_LIBADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
endif
collectd_LDADD += "-dlopen" disk.la
collectd_DEPENDENCIES += disk.la
endif

if BUILD_PLUGIN_DNS
pkglib_LTLIBRARIES += dns.la
dns_la_SOURCES = dns.c utils_dns.c utils_dns.h
dns_la_LDFLAGS = -module -avoid-version -lpcap -lpthread
collectd_LDADD += "-dlopen" dns.la
collectd_DEPENDENCIES += dns.la
endif

if BUILD_PLUGIN_EMAIL
pkglib_LTLIBRARIES += email.la
email_la_SOURCES = email.c
email_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBPTHREAD
email_la_LDFLAGS += -lpthread
endif
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
if BUILD_WITH_LIBPTHREAD
exec_la_LDFLAGS += -lpthread
endif
collectd_LDADD += "-dlopen" exec.la
collectd_DEPENDENCIES += exec.la
endif

if BUILD_PLUGIN_HDDTEMP
pkglib_LTLIBRARIES += hddtemp.la
hddtemp_la_SOURCES = hddtemp.c
hddtemp_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBSOCKET
hddtemp_la_LDFLAGS += -lsocket
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
interface_la_LDFLAGS += -lkstat
endif
if BUILD_WITH_LIBDEVINFO
interface_la_LDFLAGS += -ldevinfo
endif # BUILD_WITH_LIBDEVINFO
endif # !BUILD_WITH_LIBSTATGRAB
endif # BUILD_PLUGIN_INTERFACE

if BUILD_PLUGIN_IPTABLES
pkglib_LTLIBRARIES += iptables.la
iptables_la_SOURCES = iptables.c
iptables_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_OWN_LIBIPTC
iptables_la_LIBADD  = libiptc/libiptc.la
iptables_la_DEPENDENCIES = libiptc/libiptc.la
else
iptables_la_LDFLAGS += -liptc
endif
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
endif # BUILD_PLUGIN_LOAD

if BUILD_PLUGIN_LOGFILE
pkglib_LTLIBRARIES += logfile.la
logfile_la_SOURCES = logfile.c
logfile_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" logfile.la
collectd_DEPENDENCIES += logfile.la
endif

if BUILD_PLUGIN_MBMON
pkglib_LTLIBRARIES += mbmon.la
mbmon_la_SOURCES = mbmon.c
mbmon_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBSOCKET
mbmon_la_LDFLAGS += -lsocket
endif
collectd_LDADD += "-dlopen" mbmon.la
collectd_DEPENDENCIES += mbmon.la
endif

if BUILD_PLUGIN_MEMCACHED
pkglib_LTLIBRARIES += memcached.la
memcached_la_SOURCES = memcached.c
memcached_la_LDFLAGS = -module -avoid-version
memcached_la_LIBADD =
if BUILD_WITH_LIBSOCKET
memcached_la_LDFLAGS += -lsocket
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
network_la_SOURCES = network.c network.h
network_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBSOCKET
network_la_LDFLAGS += -lsocket
endif
if BUILD_WITH_LIBPTHREAD
network_la_LDFLAGS += -lpthread
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
notify_desktop_la_LDFLAGS = -module -avoid-version $(LIBNOTIFY_LIBS)
collectd_LDADD += "-dlopen" notify_desktop.la
collectd_DEPENDENCIES += notify_desktop.la
endif

if BUILD_PLUGIN_NOTIFY_EMAIL
pkglib_LTLIBRARIES += notify_email.la
notify_email_la_SOURCES = notify_email.c
notify_email_la_LDFLAGS = -L/usr/local/lib -lesmtp -lssl -lcrypto -pthread -module -avoid-version
collectd_LDADD += "-dlopen" notify_email.la
collectd_DEPENDENCIES += notify_email.la
endif

if BUILD_PLUGIN_NTPD
pkglib_LTLIBRARIES += ntpd.la
ntpd_la_SOURCES = ntpd.c
ntpd_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_LIBSOCKET
ntpd_la_LDFLAGS += -lsocket
endif
collectd_LDADD += "-dlopen" ntpd.la
collectd_DEPENDENCIES += ntpd.la
endif

if BUILD_PLUGIN_NUT
pkglib_LTLIBRARIES += nut.la
nut_la_SOURCES = nut.c
nut_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBUPSCLIENT_CFLAGS)
nut_la_LDFLAGS = -module -avoid-version -lpthread $(BUILD_WITH_LIBUPSCLIENT_LIBS)
collectd_LDADD += "-dlopen" nut.la
collectd_DEPENDENCIES += nut.la
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

if BUILD_PLUGIN_PERL
pkglib_LTLIBRARIES += perl.la
perl_la_SOURCES = perl.c
# Despite C99 providing the "bool" type thru stdbool.h, Perl defines its own
# version of that type if HAS_BOOL is not defined... *sigh*
perl_la_CPPFLAGS = $(AM_CPPFLAGS) -DHAS_BOOL=1
perl_la_CFLAGS  = $(AM_CFLAGS) \
		$(PERL_CFLAGS) \
		-DXS_VERSION=\"$(VERSION)\" -DVERSION=\"$(VERSION)\"
perl_la_LDFLAGS = -module -avoid-version \
		$(PERL_LDFLAGS)
collectd_LDADD += "-dlopen" perl.la
collectd_DEPENDENCIES += perl.la
endif

if BUILD_PLUGIN_PING
pkglib_LTLIBRARIES += ping.la
ping_la_SOURCES = ping.c
ping_la_LDFLAGS = -module -avoid-version
if BUILD_WITH_OWN_LIBOPING
ping_la_LIBADD  = liboping/liboping.la
ping_la_DEPENDENCIES = liboping/liboping.la
else
ping_la_LDFLAGS += -loping
endif
collectd_LDADD += "-dlopen" ping.la
collectd_DEPENDENCIES += ping.la
endif

if BUILD_PLUGIN_POSTGRESQL
pkglib_LTLIBRARIES += postgresql.la
postgresql_la_SOURCES = postgresql.c
postgresql_la_CPPFLAGS = $(AM_CPPFLAGS) $(BUILD_WITH_LIBPQ_CPPFLAGS)
postgresql_la_LDFLAGS = -module -avoid-version \
		$(BUILD_WITH_LIBPQ_LDFLAGS) -lpq
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

if BUILD_PLUGIN_PROCESSES
pkglib_LTLIBRARIES += processes.la
processes_la_SOURCES = processes.c
processes_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" processes.la
collectd_DEPENDENCIES += processes.la
endif

if BUILD_PLUGIN_QMAIL
pkglib_LTLIBRARIES += qmail.la
qmail_la_SOURCES = qmail.c
qmail_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" qmail.la
collectd_DEPENDENCIES += qmail.la
endif

if BUILD_PLUGIN_RRDTOOL
pkglib_LTLIBRARIES += rrdtool.la
rrdtool_la_SOURCES = rrdtool.c
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
if BUILD_WITH_LIBKVM
swap_la_LIBADD += -lkvm
endif
if BUILD_WITH_LIBSTATGRAB
swap_la_CFLAGS += $(BUILD_WITH_LIBSTATGRAB_CFLAGS)
swap_la_LIBADD += $(BUILD_WITH_LIBSTATGRAB_LDFLAGS)
endif
endif

if BUILD_PLUGIN_SYSLOG
pkglib_LTLIBRARIES += syslog.la
syslog_la_SOURCES = syslog.c
syslog_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" syslog.la
collectd_DEPENDENCIES += syslog.la
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
tape_la_LDFLAGS = -module -avoid-version -lkstat -ldevinfo
collectd_LDADD += "-dlopen" tape.la
collectd_DEPENDENCIES += tape.la
endif

if BUILD_PLUGIN_TCPCONNS
pkglib_LTLIBRARIES += tcpconns.la
tcpconns_la_SOURCES = tcpconns.c
tcpconns_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" tcpconns.la
collectd_DEPENDENCIES += tcpconns.la
endif

if BUILD_PLUGIN_TEAMSPEAK2
pkglib_LTLIBRARIES += teamspeak2.la
teamspeak2_la_SOURCES = teamspeak2.c
teamspeak2_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" teamspeak2.la
collectd_DEPENDENCIES += teamspeak2.la
endif

if BUILD_PLUGIN_THERMAL
pkglib_LTLIBRARIES += thermal.la
thermal_la_SOURCES = thermal.c
thermal_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" thermal.la
collectd_DEPENDENCIES += thermal.la
endif

if BUILD_PLUGIN_UNIXSOCK
pkglib_LTLIBRARIES += unixsock.la
unixsock_la_SOURCES = unixsock.c \
		      utils_cmd_flush.h utils_cmd_flush.c \
		      utils_cmd_getval.h utils_cmd_getval.c \
		      utils_cmd_listval.h utils_cmd_listval.c \
		      utils_cmd_putval.h utils_cmd_putval.c \
		      utils_cmd_putnotif.h utils_cmd_putnotif.c
unixsock_la_LDFLAGS = -module -avoid-version -lpthread
collectd_LDADD += "-dlopen" unixsock.la
collectd_DEPENDENCIES += unixsock.la
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
uuid_la_CFLAGS  = $(BUILD_WITH_LIBHAL_CFLAGS)
uuid_la_LIBADD  = $(BUILD_WITH_LIBHAL_LIBS)
uuid_la_LDFLAGS = -module -avoid-version
collectd_LDADD += "-dlopen" uuid.la
collectd_DEPENDENCIES += uuid.la
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

if BUILD_PLUGIN_XMMS
pkglib_LTLIBRARIES += xmms.la
xmms_la_SOURCES = xmms.c
xmms_la_CFLAGS = $(AM_CFLAGS) $(BUILD_WITH_LIBXMMS_CFLAGS)
xmms_la_LDFLAGS = -module -avoid-version $(BUILD_WITH_LIBXMMS_LIBS)
collectd_LDADD += "-dlopen" xmms.la
collectd_DEPENDENCIES += xmms.la
endif


dist_man_MANS = collectd.1 collectd-nagios.1 collectd.conf.5 \
		collectd-email.5 collectd-exec.5 collectd-perl.5 \
		collectd-snmp.5 collectd-unixsock.5 collectdmon.1 \
		types.db.5

#collectd_1_SOURCES = collectd.pod

EXTRA_DIST = types.db

EXTRA_DIST += collectd-email.pod collectd-exec.pod collectd-nagios.pod \
	collectd-perl.pod collectd-snmp.pod collectd-unixsock.pod \
	collectd.conf.pod collectd.pod collectdmon.pod types.db.pod \
	postgresql_default.conf

.pod.1:
	pod2man --release=$(VERSION) --center=$(PACKAGE) $< >.pod2man.tmp 2>/dev/null && mv -f .pod2man.tmp $@ || true

.pod.5:
	pod2man --section=5 --release=$(VERSION) --center=$(PACKAGE) $< >.pod2man.tmp 2>/dev/null && mv -f .pod2man.tmp $@ || true

install-exec-hook:
	$(mkinstalldirs) $(DESTDIR)$(sysconfdir)
	if test -e $(DESTDIR)$(sysconfdir)/collectd.conf; \
	then \
		$(INSTALL) -m 0640 collectd.conf $(DESTDIR)$(sysconfdir)/collectd.conf.pkg-orig; \
	else \
		$(INSTALL) -m 0640 collectd.conf $(DESTDIR)$(sysconfdir)/collectd.conf; \
	fi; \
	cp -f $(srcdir)/types.db $(DESTDIR)$(pkglibdir)/;
	$(INSTALL) -D -m 0644 $(srcdir)/postgresql_default.conf \
		$(DESTDIR)$(pkgdatadir)/postgresql_default.conf;
