Summary:	Statistics collection daemon for filling RRD files.
Name:           collectd
Version:	4.2.0
Release:	1.fc6
Source:		http://collectd.org/files/%{name}-%{version}.tar.gz
License:	GPL
Group:		System Environment/Daemons
BuildRoot:	%{_tmppath}/%{name}-%{version}-root
BuildPrereq:	lm_sensors-devel
BuildPrereq:	mysql-devel
BuildPrereq:	rrdtool-devel
BuildPrereq:	net-snmp-devel
Requires:	rrdtool
Requires:	perl-Regexp-Common
Packager:	Florian octo Forster <octo@verplant.org>
Vendor:		Florian octo Forster <octo@verplant.org>

%description
collectd is a small daemon written in C for performance.  It reads various
system  statistics  and updates  RRD files,  creating  them if neccessary.
Since the daemon doesn't need to startup every time it wants to update the
files it's very fast and easy on the system. Also, the statistics are very
fine grained since the files are updated every 10 seconds.

%package apache
Summary:	apache-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, curl
%description apache
This plugin collectd data provided by Apache's `mod_status'.

%package email
Summary:	email-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, spamassassin
%description email
This plugin collectd data provided by spamassassin.

%package mysql
Summary:	mysql-module for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, mysql
%description mysql
MySQL  querying  plugin.  This plugins  provides data of  issued commands,
called handlers and database traffic.

%package sensors
Summary:	libsensors-module for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, lm_sensors
%description sensors
This  plugin  for  collectd  provides  querying  of sensors  supported  by
lm_sensors.

%prep
rm -rf $RPM_BUILD_ROOT
%setup

%build
./configure --prefix=%{_prefix} --sbindir=%{_sbindir} --mandir=%{_mandir} --libdir=%{_libdir} --sysconfdir=%{_sysconfdir}
make

%install
make install DESTDIR=$RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/etc/rc.d/init.d
mkdir -p $RPM_BUILD_ROOT/var/www/cgi-bin
cp src/collectd.conf $RPM_BUILD_ROOT/etc/collectd.conf
cp contrib/fedora/init.d-collectd $RPM_BUILD_ROOT/etc/rc.d/init.d/collectd
cp contrib/collection.cgi $RPM_BUILD_ROOT/var/www/cgi-bin
cp contrib/collection.conf $RPM_BUILD_ROOT/etc/collection.conf
mkdir -p $RPM_BUILD_ROOT/var/lib/collectd

%clean
rm -rf $RPM_BUILD_ROOT

%post
/sbin/chkconfig --add collectd
/sbin/chkconfig collectd on

%preun
if [ "$1" = 0 ]; then
   /sbin/chkconfig collectd off
   /etc/init.d/collectd stop
   /sbin/chkconfig --del collectd
fi
exit 0

%postun
if [ "$1" -ge 1 ]; then
    /etc/init.d/collectd restart
fi
exit 0

%files
%defattr(-,root,root)
%doc AUTHORS COPYING ChangeLog INSTALL NEWS README
%attr(0644,root,root) %config(noreplace) /etc/collectd.conf
%attr(0644,root,root) %config(noreplace) /etc/collection.conf
%attr(0755,root,root) /etc/rc.d/init.d/collectd
%attr(0755,root,root) /var/www/cgi-bin/collection.cgi
%attr(0755,root,root) %{_sbindir}/collectd
%attr(0755,root,root) %{_bindir}/collectd-nagios
%attr(0644,root,root) %{_mandir}/man1/*
%attr(0644,root,root) %{_mandir}/man5/*

%attr(0644,root,root) /usr/lib/perl5/5.8.8/i386-linux-thread-multi/perllocal.pod
%attr(0644,root,root) /usr/lib/perl5/site_perl/5.8.8/Collectd.pm
%attr(0644,root,root) /usr/lib/perl5/site_perl/5.8.8/Collectd/Unixsock.pm
%attr(0644,root,root) /usr/lib/perl5/site_perl/5.8.8/i386-linux-thread-multi/auto/Collectd/.packlist
%attr(0644,root,root) %{_mandir}/man3/Collectd::Unixsock.3pm.gz

%attr(0644,root,root) %{_libdir}/%{name}/apcups.so*
%attr(0644,root,root) %{_libdir}/%{name}/apcups.la

# FIXME!!!
#%attr(0644,root,root) %{_libdir}/%{name}/apple_sensors.so*
#%attr(0644,root,root) %{_libdir}/%{name}/apple_sensors.la

%attr(0644,root,root) %{_libdir}/%{name}/battery.so*
%attr(0644,root,root) %{_libdir}/%{name}/battery.la

%attr(0644,root,root) %{_libdir}/%{name}/conntrack.so*
%attr(0644,root,root) %{_libdir}/%{name}/conntrack.la

%attr(0644,root,root) %{_libdir}/%{name}/cpufreq.so*
%attr(0644,root,root) %{_libdir}/%{name}/cpufreq.la

%attr(0644,root,root) %{_libdir}/%{name}/cpu.so*
%attr(0644,root,root) %{_libdir}/%{name}/cpu.la

%attr(0644,root,root) %{_libdir}/%{name}/csv.so*
%attr(0644,root,root) %{_libdir}/%{name}/csv.la

%attr(0644,root,root) %{_libdir}/%{name}/df.so*
%attr(0644,root,root) %{_libdir}/%{name}/df.la

%attr(0644,root,root) %{_libdir}/%{name}/disk.so*
%attr(0644,root,root) %{_libdir}/%{name}/disk.la

%attr(0644,root,root) %{_libdir}/%{name}/dns.so*
%attr(0644,root,root) %{_libdir}/%{name}/dns.la

%attr(0644,root,root) %{_libdir}/%{name}/entropy.so*
%attr(0644,root,root) %{_libdir}/%{name}/entropy.la

%attr(0644,root,root) %{_libdir}/%{name}/exec.so*
%attr(0644,root,root) %{_libdir}/%{name}/exec.la

%attr(0644,root,root) %{_libdir}/%{name}/hddtemp.so*
%attr(0644,root,root) %{_libdir}/%{name}/hddtemp.la

%attr(0644,root,root) %{_libdir}/%{name}/interface.so*
%attr(0644,root,root) %{_libdir}/%{name}/interface.la

%attr(0644,root,root) %{_libdir}/%{name}/iptables.so*
%attr(0644,root,root) %{_libdir}/%{name}/iptables.la

%attr(0644,root,root) %{_libdir}/%{name}/irq.so*
%attr(0644,root,root) %{_libdir}/%{name}/irq.la

%attr(0644,root,root) %{_libdir}/%{name}/load.so*
%attr(0644,root,root) %{_libdir}/%{name}/load.la

%attr(0644,root,root) %{_libdir}/%{name}/logfile.so*
%attr(0644,root,root) %{_libdir}/%{name}/logfile.la

%attr(0644,root,root) %{_libdir}/%{name}/mbmon.so*
%attr(0644,root,root) %{_libdir}/%{name}/mbmon.la

%attr(0644,root,root) %{_libdir}/%{name}/memcached.so*
%attr(0644,root,root) %{_libdir}/%{name}/memcached.la

%attr(0644,root,root) %{_libdir}/%{name}/memory.so*
%attr(0644,root,root) %{_libdir}/%{name}/memory.la

%attr(0644,root,root) %{_libdir}/%{name}/multimeter.so*
%attr(0644,root,root) %{_libdir}/%{name}/multimeter.la

%attr(0644,root,root) %{_libdir}/%{name}/network.so*
%attr(0644,root,root) %{_libdir}/%{name}/network.la

%attr(0644,root,root) %{_libdir}/%{name}/nfs.so*
%attr(0644,root,root) %{_libdir}/%{name}/nfs.la

%attr(0644,root,root) %{_libdir}/%{name}/nginx.so*
%attr(0644,root,root) %{_libdir}/%{name}/nginx.la

%attr(0644,root,root) %{_libdir}/%{name}/ntpd.so*
%attr(0644,root,root) %{_libdir}/%{name}/ntpd.la

# FIXME!!!
#%attr(0644,root,root) %{_libdir}/%{name}/nut.so*
#%attr(0644,root,root) %{_libdir}/%{name}/nut.la

%attr(0644,root,root) %{_libdir}/%{name}/perl.so*
%attr(0644,root,root) %{_libdir}/%{name}/perl.la

%attr(0644,root,root) %{_libdir}/%{name}/ping.so*
%attr(0644,root,root) %{_libdir}/%{name}/ping.la

%attr(0644,root,root) %{_libdir}/%{name}/processes.so*
%attr(0644,root,root) %{_libdir}/%{name}/processes.la

%attr(0644,root,root) %{_libdir}/%{name}/rrdtool.so*
%attr(0644,root,root) %{_libdir}/%{name}/rrdtool.la

%attr(0644,root,root) %{_libdir}/%{name}/serial.so*
%attr(0644,root,root) %{_libdir}/%{name}/serial.la

%attr(0644,root,root) %{_libdir}/%{name}/swap.so*
%attr(0644,root,root) %{_libdir}/%{name}/swap.la

%attr(0644,root,root) %{_libdir}/%{name}/snmp.so*
%attr(0644,root,root) %{_libdir}/%{name}/snmp.la

%attr(0644,root,root) %{_libdir}/%{name}/syslog.so*
%attr(0644,root,root) %{_libdir}/%{name}/syslog.la

# FIXME!!!
#%attr(0644,root,root) %{_libdir}/%{name}/tape.so*
#%attr(0644,root,root) %{_libdir}/%{name}/tape.la

%attr(0644,root,root) %{_libdir}/%{name}/tcpconns.so*
%attr(0644,root,root) %{_libdir}/%{name}/tcpconns.la

%attr(0644,root,root) %{_libdir}/%{name}/unixsock.so*
%attr(0644,root,root) %{_libdir}/%{name}/unixsock.la

%attr(0644,root,root) %{_libdir}/%{name}/users.so*
%attr(0644,root,root) %{_libdir}/%{name}/users.la

%attr(0644,root,root) %{_libdir}/%{name}/vserver.so*
%attr(0644,root,root) %{_libdir}/%{name}/vserver.la

%attr(0644,root,root) %{_libdir}/%{name}/wireless.so*
%attr(0644,root,root) %{_libdir}/%{name}/wireless.la

%attr(0644,root,root) %{_datadir}/%{name}/types.db

%dir /var/lib/collectd

%files apache
%attr(0644,root,root) %{_libdir}/%{name}/apache.so*
%attr(0644,root,root) %{_libdir}/%{name}/apache.la

%files email
%attr(0644,root,root) %{_libdir}/%{name}/email.so*
%attr(0644,root,root) %{_libdir}/%{name}/email.la

%files mysql
%attr(0644,root,root) %{_libdir}/%{name}/mysql.so*
%attr(0644,root,root) %{_libdir}/%{name}/mysql.la

%files sensors
%attr(0644,root,root) %{_libdir}/%{name}/sensors.so*
%attr(0644,root,root) %{_libdir}/%{name}/sensors.la

%changelog
* Wed Oct 31 2007 Iain Lea <iain@bricbrac.de> 4.2.0
- New major release
- Changes to support 4.2.0 (ie. contrib/collection.conf)

* Mon Aug 06 2007 Kjell Randa <Kjell.Randa@broadpark.no> 4.0.6
- New upstream version

* Wed Jul 25 2007 Kjell Randa <Kjell.Randa@broadpark.no> 4.0.5
- New major release
- Changes to support 4.0.5 

* Wed Jan 11 2007 Iain Lea <iain@bricbrac.de> 3.11.0-0
- fixed spec file to build correctly on fedora core
- added improved init.d script to work with chkconfig
- added %post and %postun to call chkconfig automatically

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
