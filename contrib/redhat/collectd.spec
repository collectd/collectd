%global _hardened_build 1

Summary:	Statistics collection daemon for filling RRD files
Name:		collectd
Version:	5.1.0
Release:	3%{?dist}
URL:		http://collectd.org
Source:		http://collectd.org/files/%{name}-%{version}.tar.gz
License:	GPLv2
Group:		System Environment/Daemons
BuildRoot:	%{_tmppath}/%{name}-%{version}-root
BuildRequires:	curl-devel, libgcrypt-devel, libxml2-devel, libstatgrab-devel
Vendor:		collectd development team <collectd@verplant.org>

Requires(post):		chkconfig
Requires(preun):	chkconfig, initscripts
Requires(postun):	initscripts

%description
collectd is a small daemon which collects system information periodically and
provides mechanisms to monitor and store the values in a variety of ways. It
is written in C for performance. Since the daemon doesn't need to start up
every time it wants to update the values it's very fast and easy on the
system. Also, the statistics are very fine grained since the files are updated
every 10 seconds.

%package amqp
Summary:	AMQP plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	librabbitmq-devel
%description amqp
The AMQP plugin transmits or receives values collected by collectd via the
Advanced Message Queuing Protocol (AMQP).

%package apache
Summary:	Apache plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
%description apache
This plugin collects data provided by Apache's `mod_status'.

%package ascent
Summary:	Ascent plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
%description ascent
The Ascent plugin reads and parses the statistics page of Ascent, a free and
open-source server software for the game World of Warcraft by Blizzard
Entertainment.

%package bind
Summary:	Bind plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
%description bind
The BIND plugin retrieves this information that's encoded in XML and provided
via HTTP and submits the values to collectd.

%package curl
Summary:	Curl plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
%description curl
The cURL plugin uses libcurl to read files and then parses them according to
the configuration.

%package curl_json
Summary:	Curl_json plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}, curl, yajl
Buildrequires:	yajl-devel
%description curl_json
The cURL-JSON plugin queries JavaScript Object Notation (JSON) data using the
cURL library and parses it according to the user's configuration.

%package curl_xml
Summary:	Curl_xml plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
%description curl_xml
The cURL-XML plugin reads files using libcurl and parses it as Extensible
Markup Language (XML).

%package dbi
Summary:	DBI plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
Buildrequires:	libdbi-devel
%description dbi
The DBI plugin uses libdbi, a database abstraction library, to execute SQL
statements on a database and read back the result.

%package dns
Summary:	DNS plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
Buildrequires:	libpcap-devel
%description dns
The DNS plugin has a similar functionality to dnstop: It uses libpcap to get a
copy of all traffic from/to port UDP/53 (that's the DNS port), interprets the
packets and collects statistics of your DNS traffic.

%package email
Summary:	Email plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}, spamassassin
%description email
This plugin collects data provided by spamassassin.

%package gmond
Summary:	Gmond plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	ganglia-devel
%description gmond
The gmond plugin subscribes to a Multicast group to receive data from gmond,
the client daemon of the Ganglia project.

%package hddtemp
Summary:	Hddtemp plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}, hddtemp
%description hddtemp
The HDDTemp plugin collects the temperature of hard disks. The temperatures are
provided via SMART and queried by the external hddtemp daemon.

%package ipmi
Summary:	IPMI plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	OpenIPMI-devel
%description ipmi
The IPMI plugin uses the OpenIPMI library to read hardware sensors from servers
using the Intelligent Platform Management Interface (IPMI).

%package iptables
Summary:	IPtables plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	iptables-devel
%description iptables
The IP-Tables plugin can gather statistics from your ip_tables based packet
filter (aka. firewall) for both the IPv4 and the IPv6 protocol. It can collect
the byte- and packet-counters of selected rules and submit them to collectd.

%package java
Summary:	Java plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	java-devel, jpackage-utils
Requires:	java, jpackage-utils
%description java
This plugin for collectd allows plugins to be written in Java and executed
in an embedded JVM.

%package libvirt
Summary:	Libvirt plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	libvirt-devel
%description libvirt
This plugin collects information from virtualized guests.

%package memcachec
Summary:	Memcachec plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	libmemcached-devel
%description memcachec
The Memcachec plugin uses libmemcached to read statistics from a Memcached
instance. Note that another plugin, named `memcached', exists and does a
similar job, without requiring the installation of libmemcached.

%package mysql
Summary:	MySQL plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	mysql-devel
%description mysql
MySQL querying plugin. This plugin provides data of issued commands, called
handlers and database traffic.

%package nginx
Summary:	Nginx plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
%description nginx
This plugin gets data provided by nginx.

%package notify_desktop
Summary:	Notify_desktop plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	libnotify-devel
%description notify_desktop
The Notify Desktop plugin uses libnotify to display notifications to the user
via the desktop notification specification, i. e. on an X display.

%package notify_email
Summary:	Notify_email plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	libesmtp-devel
%description notify_email
The Notify Email plugin uses libESMTP to send notifications to a configured
email address.

%package nut
Summary:	Nut plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	nut-devel
%description nut
This plugin for collectd provides Network UPS Tools support.

%package perl
Summary:	Perl plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
Requires:	perl(:MODULE_COMPAT_%(eval "`%{__perl} -V:version`"; echo $version))
BuildRequires:	perl-ExtUtils-Embed
%description perl
The Perl plugin embeds a Perl interpreter into collectd and exposes the
application programming interface (API) to Perl-scripts.

%package pinba
Summary:	Pinba plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	protobuf-c-devel
%description pinba
The Pinba plugin receives and dispatches timing values from Pinba, a profiling
extension for PHP.

%package ping
Summary:	Ping plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	liboping-devel
%description ping
The Ping plugin measures network latency using ICMP “echo requests”, usually
known as “ping”.

%package postgresql
Summary:	PostgreSQL plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	postgresql-devel
%description postgresql
The PostgreSQL plugin connects to and executes SQL statements on a PostgreSQL
database.

%package python
Summary:	Python plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	python-devel
%description python
The Python plugin embeds a Python interpreter into collectd and exposes the
application programming interface (API) to Python-scripts.

%package rrdtool
Summary:	RRDtool plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	rrdtool-devel
%description rrdtool
The RRDtool plugin writes values to RRD-files using librrd.

%package sensors
Summary:	Sensors plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	lm_sensors-devel
%description sensors
This plugin for collectd provides querying of sensors supported by lm_sensors.

%package snmp
Summary:	SNMP plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	net-snmp-devel
%description snmp
This plugin for collectd allows querying of network equipment using SNMP.

%package varnish
Summary:	Varnish plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
BuildRequires:	varnish-libs-devel
%description varnish
The Varnish plugin collects information about Varnish, an HTTP accelerator.

%package write_http
Summary:	Write-HTTP plugin for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
%description write_http
The Write-HTTP plugin sends the values collected by collectd to a web-server
using HTTP POST requests.

%package contrib
Summary:	Contrib files for collectd
Group:		System Environment/Daemons
Requires:	%{name}%{?_isa} = %{version}-%{release}
%description contrib
All the files found under contrib/ in the source tree are bundled in this
package.

%package -n libcollectdclient
Summary:	Collectd client library
%description -n libcollectdclient
Collectd client library

%package -n libcollectdclient-devel
Summary:	Development files for libcollectdclient
Requires:	pkgconfig
Requires:	libcollectdclient%{?_isa} = %{version}-%{release}
%description -n libcollectdclient-devel
Development files for libcollectdclient


%prep
%setup -q


%build
%configure CFLAGS="%{optflags} -DLT_LAZY_OR_NOW='RTLD_LAZY|RTLD_GLOBAL'" \
	--disable-static \
	--with-java=%{java_home}/ \
	--with-perl-bindings=INSTALLDIRS=vendor

%{__make} %{?_smp_mflags}


%install
rm -rf %{buildroot}
%{__make} install DESTDIR=%{buildroot}
%{__install} -Dp -m 0755 contrib/redhat/init.d-collectd %{buildroot}%{_initrddir}/collectd
%{__install} -Dp -m0644 src/collectd.conf %{buildroot}%{_sysconfdir}/collectd.conf
%{__install} -d %{buildroot}%{sharedstatedir}/collectd/
%{__install} -d %{buildroot}%{_sysconfdir}/collectd.d/

### Clean up docs
find contrib/ -type f -exec %{__chmod} a-x {} \;
# *.la files shouldn't be distributed.
rm -f %{buildroot}/%{_libdir}/{collectd/,}*.la

# Move the Perl examples to a separate directory.
mkdir perl-examples
find contrib -name '*.p[lm]' -exec mv {} perl-examples/ \;

# Modify config for Red Hat based Distros
sed -i 's:#BaseDir *"/usr/var/lib/collectd":BaseDir "%{_sharedstatedir}/collectd":' %{buildroot}%{_sysconfdir}/collectd.conf
sed -i 's:#PIDFile *"/usr/var/run/collectd.pid":PIDFile "%{_localstatedir}/run/collectd.pid":' %{buildroot}%{_sysconfdir}/collectd.conf
sed -i 's:#PluginDir *"/usr/lib/collectd":PluginDir "%{_libdir}/collectd":' %{buildroot}%{_sysconfdir}/collectd.conf

# Remove Perl hidden .packlist files.
find %{buildroot} -type f -name .packlist -delete
# Remove Perl temporary file perllocal.pod
find %{buildroot} -type f -name perllocal.pod -delete


%clean
rm -rf %{buildroot}

%post
/sbin/chkconfig --add collectd

%preun
if [ $1 -eq 0 ]; then
	/sbin/service collectd stop &>/dev/null
	/sbin/chkconfig --del collectd
fi

%postun
if [ $1 -ge 1 ]; then
	/sbin/service collectd condrestart &>/dev/null || :
fi

%post -n libcollectdclient -p /sbin/ldconfig
%postun -n libcollectdclient -p /sbin/ldconfig


%files
%doc AUTHORS COPYING ChangeLog README
%config(noreplace) %{_sysconfdir}/collectd.conf
%{_initrddir}/collectd
%{_sbindir}/collectd
%{_bindir}/collectd-nagios
%{_bindir}/collectdctl
%{_sbindir}/collectdmon
%{_datadir}/collectd/
%{_sharedstatedir}/collectd
%{_mandir}/man1/collectd-nagios.1.gz
%{_mandir}/man1/collectd.1.gz
%{_mandir}/man1/collectdctl.1.gz
%{_mandir}/man1/collectdmon.1.gz
%{_mandir}/man5/collectd-email.5.gz
%{_mandir}/man5/collectd-exec.5.gz
%{_mandir}/man5/collectd-threshold.5.gz
%{_mandir}/man5/collectd-unixsock.5.gz
%{_mandir}/man5/collectd.conf.5.gz
%{_mandir}/man5/types.db.5.gz

# all plugins bundled with the main collectd package
%{_libdir}/%{name}/apcups.so
%{_libdir}/%{name}/battery.so
%{_libdir}/%{name}/conntrack.so
%{_libdir}/%{name}/contextswitch.so
%{_libdir}/%{name}/cpufreq.so
%{_libdir}/%{name}/cpu.so
%{_libdir}/%{name}/csv.so
%{_libdir}/%{name}/df.so
%{_libdir}/%{name}/disk.so
%{_libdir}/%{name}/ethstat.so
%{_libdir}/%{name}/entropy.so
%{_libdir}/%{name}/email.so
%{_libdir}/%{name}/exec.so
%{_libdir}/%{name}/filecount.so
%{_libdir}/%{name}/fscache.so
%{_libdir}/%{name}/interface.so
%{_libdir}/%{name}/iptables.so
%{_libdir}/%{name}/ipvs.so
%{_libdir}/%{name}/irq.so
%{_libdir}/%{name}/load.so
%{_libdir}/%{name}/logfile.so
%{_libdir}/%{name}/madwifi.so
%{_libdir}/%{name}/match_empty_counter.so
%{_libdir}/%{name}/match_hashed.so
%{_libdir}/%{name}/match_regex.so
%{_libdir}/%{name}/match_timediff.so
%{_libdir}/%{name}/match_value.so
%{_libdir}/%{name}/mbmon.so
%{_libdir}/%{name}/md.so
%{_libdir}/%{name}/memcached.so
%{_libdir}/%{name}/memory.so
%{_libdir}/%{name}/multimeter.so
%{_libdir}/%{name}/network.so
%{_libdir}/%{name}/nfs.so
%{_libdir}/%{name}/ntpd.so
%{_libdir}/%{name}/numa.so
%{_libdir}/%{name}/openvpn.so
%{_libdir}/%{name}/olsrd.so
%{_libdir}/%{name}/powerdns.so
%{_libdir}/%{name}/processes.so
%{_libdir}/%{name}/protocols.so
%{_libdir}/%{name}/serial.so
%{_libdir}/%{name}/swap.so
%{_libdir}/%{name}/syslog.so
%{_libdir}/%{name}/table.so
%{_libdir}/%{name}/tail.so
%{_libdir}/%{name}/target_notification.so
%{_libdir}/%{name}/target_replace.so
%{_libdir}/%{name}/target_scale.so
%{_libdir}/%{name}/target_set.so
%{_libdir}/%{name}/target_v5upgrade.so
%{_libdir}/%{name}/tcpconns.so
%{_libdir}/%{name}/teamspeak2.so
%{_libdir}/%{name}/ted.so
%{_libdir}/%{name}/thermal.so
%{_libdir}/%{name}/threshold.so
%{_libdir}/%{name}/unixsock.so
%{_libdir}/%{name}/uptime.so
%{_libdir}/%{name}/users.so
%{_libdir}/%{name}/uuid.so
%{_libdir}/%{name}/vmem.so
%{_libdir}/%{name}/vserver.so
%{_libdir}/%{name}/wireless.so
%{_libdir}/%{name}/write_graphite.so

# All plugins not built because of dependencies on libraries not available in
# RHEL or EPEL:
# plugin modbus disabled, requires libmodbus
# plugin netlink disabled, requires libnetlink.h
# plugin numa disabled, requires libnetapp
# plugin onewire disabled, requires libowfs
# plugin oracle disabled, requires Oracle
# plugin redis disabled, requires credis
# plugin routeros disabled, requires librouteros
# plugin rrdcached disabled, requires rrdtool >= 1.4
# plugin tokyotyrant disabled, requires tcrdb.h
# plugin write_mongodb disabled, requires libmongoc
# plugin write_redis disabled, requires credis
# plugin xmms disabled, requires xmms


%files -n libcollectdclient-devel
%{_includedir}/collectd/client.h
%{_includedir}/collectd/lcc_features.h
%{_libdir}/libcollectdclient.so
%{_libdir}/pkgconfig/libcollectdclient.pc

%files -n libcollectdclient
%{_libdir}/libcollectdclient.so.*


%files amqp
%{_libdir}/%{name}/amqp.so

%files apache
%{_libdir}/%{name}/apache.so

%files ascent
%{_libdir}/%{name}/ascent.so

%files bind
%{_libdir}/%{name}/bind.so

%files curl
%{_libdir}/%{name}/curl.so

%files curl_json
%{_libdir}/%{name}/curl_json.so

%files curl_xml
%{_libdir}/%{name}/curl_xml.so

%files dns
%{_libdir}/%{name}/dns.so

%files dbi
%{_libdir}/%{name}/dbi.so

%files email
%{_libdir}/%{name}/email.so

%files gmond
%{_libdir}/%{name}/gmond.so

%files hddtemp
%{_libdir}/%{name}/hddtemp.so

%files ipmi
%{_libdir}/%{name}/ipmi.so

%files iptables
%{_libdir}/%{name}/iptables.so

%files java
%{_mandir}/man5/collectd-java.5.gz
%{_datarootdir}/collectd/java/collectd-api.jar
%{_datarootdir}/collectd/java/generic-jmx.jar
%{_libdir}/%{name}/java.so

%files libvirt
%{_libdir}/%{name}/libvirt.so

%files memcachec
%{_libdir}/%{name}/memcachec.so

%files mysql
%{_libdir}/%{name}/mysql.so

%files nginx
%{_libdir}/%{name}/nginx.so

%files notify_desktop
%{_libdir}/%{name}/notify_desktop.so

%files notify_email
%{_libdir}/%{name}/notify_email.so

%files nut
%{_libdir}/%{name}/nut.so

%files perl
%doc perl-examples/*
%{perl_vendorlib}/Collectd.pm
%{perl_vendorlib}/Collectd/
%{_mandir}/man3/Collectd::Unixsock.3pm.gz
%{_mandir}/man5/collectd-perl.5.gz
%{_libdir}/%{name}/perl.so

%files pinba
%{_libdir}/%{name}/pinba.so

%files ping
%{_libdir}/%{name}/ping.so

%files postgresql
%{_datarootdir}/collectd/postgresql_default.conf
%{_libdir}/%{name}/postgresql.so

%files python
%{_mandir}/man5/collectd-python.5.gz
%{_libdir}/%{name}/python.so

%files rrdtool
%{_libdir}/%{name}/rrdtool.so

%files sensors
%{_libdir}/%{name}/sensors.so

%files snmp
%{_mandir}/man5/collectd-snmp.5.gz
%{_libdir}/%{name}/snmp.so

%files varnish
%{_libdir}/%{name}/varnish.so

%files write_http
%{_libdir}/%{name}/write_http.so

%files contrib
%doc contrib/

%changelog
* Sun Nov 18 2012 Ruben Kerkhof <ruben@tilaa.nl> 5.1.0-3
- Follow Fedora Packaging Guidelines in java subpackage

* Sat Nov 17 2012 Ruben Kerkhof <ruben@tilaa.nl> 5.1.0-2
- Move perl stuff to perl_vendorlib
- Replace hardcoded paths with macros
- Remove unneccesary Requires
- Removed .a and .la files
- Some other small cleanups

* Fri Nov 16 2012 Marc Fournier <marc.fournier@camptocamp.com> 5.1.0-1
- New upstream version
- Changes to support 5.1.0
- Enabled all buildable plugins based on libraries available on EL6 + EPEL
- All plugins requiring external libraries are now shipped in seperate
  packages.
- No longer treat Java plugin as an exception, correctly set $JAVA_HOME during
  the build process + ensure build deps are installed.
- Dropped per-plugin configuration files, as they tend to diverge from upstream
  defaults.
- Moved perl stuff to /usr/share/perl5/
- Don't alter Interval and ReadThreads by default, let the user change this
  himself.
- Initscript improvements:
  * checks configuration before (re)starting, based on debian's initscript
  * use /etc/sysconfig instdead of /etc/default
  * include optional $ARGS in arguments passed to collectd.
- Drop collection.cgi from main package, as it's been obsoleted by collection3
- Moved contrib/ to its own package, to avoid cluttering the main package with
  non-essential stuff.
- Replaced BuildPrereq by BuildRequires

* Tue Jan 03 2011 Monetate <jason.stelzer@monetate.com> 5.0.1
- New upstream version
- Changes to support 5.0.1

* Tue Jan 04 2010 Rackspace <stu.hood@rackspace.com> 4.9.0
- New upstream version
- Changes to support 4.9.0
- Added support for Java/GenericJMX plugin

* Mon Mar 17 2008 RightScale <support@rightscale.com> 4.3.1
- New upstream version
- Changes to support 4.3.1
- Added More Prereqs to support more plugins
- Added support for perl plugin

* Mon Aug 06 2007 Kjell Randa <Kjell.Randa@broadpark.no> 4.0.6
- New upstream version

* Wed Jul 25 2007 Kjell Randa <Kjell.Randa@broadpark.no> 4.0.5
- New major releas
- Changes to support 4.0.5

* Wed Jan 11 2007 Iain Lea <iain@bricbrac.de> 3.11.0-0
- fixed spec file to build correctly on fedora core
- added improved init.d script to work with chkconfig
- added %%post and %%postun to call chkconfig automatically

* Sun Jul 09 2006 Florian octo Forster <octo@verplant.org> 3.10.0-1
- New upstream version

* Tue Jun 25 2006 Florian octo Forster <octo@verplant.org> 3.9.4-1
- New upstream version

* Tue Jun 01 2006 Florian octo Forster <octo@verplant.org> 3.9.3-1
- New upstream version

* Tue May 09 2006 Florian octo Forster <octo@verplant.org> 3.9.2-1
- New upstream version

* Tue May 09 2006 Florian octo Forster <octo@verplant.org> 3.8.5-1
- New upstream version

* Fri Apr 21 2006 Florian octo Forster <octo@verplant.org> 3.9.1-1
- New upstream version

* Fri Apr 14 2006 Florian octo Forster <octo@verplant.org> 3.9.0-1
- New upstream version
- Added the `apache' package.

* Thu Mar 14 2006 Florian octo Forster <octo@verplant.org> 3.8.2-1
- New upstream version

* Thu Mar 13 2006 Florian octo Forster <octo@verplant.org> 3.8.1-1
- New upstream version

* Thu Mar 09 2006 Florian octo Forster <octo@verplant.org> 3.8.0-1
- New upstream version

* Sat Feb 18 2006 Florian octo Forster <octo@verplant.org> 3.7.2-1
- Include `tape.so' so the build doesn't terminate because of missing files..
- New upstream version

* Sat Feb 04 2006 Florian octo Forster <octo@verplant.org> 3.7.1-1
- New upstream version

* Mon Jan 30 2006 Florian octo Forster <octo@verplant.org> 3.7.0-1
- New upstream version
- Removed the extra `hddtemp' package

* Tue Jan 24 2006 Florian octo Forster <octo@verplant.org> 3.6.2-1
- New upstream version

* Fri Jan 20 2006 Florian octo Forster <octo@verplant.org> 3.6.1-1
- New upstream version

* Fri Jan 20 2006 Florian octo Forster <octo@verplant.org> 3.6.0-1
- New upstream version
- Added config file, `collectd.conf(5)', `df.so'
- Added package `collectd-mysql', dependency on `mysqlclient10 | mysql'

* Wed Dec 07 2005 Florian octo Forster <octo@verplant.org> 3.5.0-1
- New upstream version

* Sat Nov 26 2005 Florian octo Forster <octo@verplant.org> 3.4.0-1
- New upstream version

* Sat Nov 05 2005 Florian octo Forster <octo@verplant.org> 3.3.0-1
- New upstream version

* Tue Oct 26 2005 Florian octo Forster <octo@verplant.org> 3.2.0-1
- New upstream version
- Added statement to remove the `*.la' files. This fixes a problem when
  `Unpackaged files terminate build' is in effect.
- Added `processes.so*' to the main package

* Fri Oct 14 2005 Florian octo Forster <octo@verplant.org> 3.1.0-1
- New upstream version
- Added package `collectd-hddtemp'

* Fri Sep 30 2005 Florian octo Forster <octo@verplant.org> 3.0.0-1
- New upstream version
- Split the package into `collectd' and `collectd-sensors'

* Fri Sep 16 2005 Florian octo Forster <octo@verplant.org> 2.1.0-1
- New upstream version

* Mon Sep 10 2005 Florian octo Forster <octo@verplant.org> 2.0.0-1
- New upstream version

* Mon Aug 29 2005 Florian octo Forster <octo@verplant.org> 1.8.0-1
- New upstream version

* Sun Aug 25 2005 Florian octo Forster <octo@verplant.org> 1.7.0-1
- New upstream version

* Sun Aug 21 2005 Florian octo Forster <octo@verplant.org> 1.6.0-1
- New upstream version

* Sun Jul 17 2005 Florian octo Forster <octo@verplant.org> 1.5.1-1
- New upstream version

* Sun Jul 17 2005 Florian octo Forster <octo@verplant.org> 1.5-1
- New upstream version

* Mon Jul 11 2005 Florian octo Forster <octo@verplant.org> 1.4.2-1
- New upstream version

* Sat Jul 09 2005 Florian octo Forster <octo@verplant.org> 1.4-1
- Built on RedHat 7.3
