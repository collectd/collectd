Summary:	Statistics collection daemon for filling RRD files.
Name:           collectd
Version:	3.11.1
Release:	0.sl10.1
Source:		http://collectd.org/files/%{name}-%{version}.tar.gz
Source1:	collectd-init.d
License:	GPL
Group:		System Environment/Daemons
BuildRoot:	%{_tmppath}/%{name}-%{version}-root
BuildPrereq:	curl-devel, sensors, mysql-devel, rrdtool, libpcap
Requires:	rrdtool
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
This plugin collects data provided by Apache's `mod_status'.

%package dns
Summary:	dns-plugin for collectd.
Group:		System Environment/Daemons
Requires:	collectd = %{version}, libpcap
%description dns
This plugin collects information about DNS traffic, queries and responses.

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
Requires:	collectd = %{version}, sensors
%description sensors
This  plugin  for  collectd  provides  querying  of sensors  supported  by
lm_sensors.

%prep
rm -rf $RPM_BUILD_ROOT
%setup

%build
./configure --prefix=%{_prefix} --sbindir=%{_sbindir} --mandir=%{_mandir} --libdir=%{_libdir} --sysconfdir=%{_sysconfdir} --localstatedir=%{_localstatedir}
make

%install
make install DESTDIR=$RPM_BUILD_ROOT
cp src/collectd.conf $RPM_BUILD_ROOT/etc/collectd.conf
mkdir -p $RPM_BUILD_ROOT/var/lib/collectd
rm -f $RPM_BUILD_ROOT%{_libdir}/%{name}/*.a
rm -f $RPM_BUILD_ROOT%{_libdir}/%{name}/*.la
mkdir -p $RPM_BUILD_ROOT%{_sysconfdir}/init.d
cp %{SOURCE1} $RPM_BUILD_ROOT%{_sysconfdir}/init.d/collectd

%clean
rm -rf $RPM_BUILD_ROOT

%post
chkconfig collectd on
/etc/init.d/collectd start

%preun
/etc/init.d/collectd stop
chkconfig collectd off

%files
%defattr(-,root,root)
%doc AUTHORS COPYING ChangeLog INSTALL NEWS README
%doc contrib
%config /etc/collectd.conf
%attr(0755,root,root) /etc/init.d/collectd
%attr(0755,root,root) %{_sbindir}/collectd
%attr(0444,root,root) %{_mandir}/man1/*
%attr(0444,root,root) %{_mandir}/man5/*
%attr(0444,root,root) %{_libdir}/%{name}/apcups.so
%attr(0444,root,root) %{_libdir}/%{name}/apple_sensors.so
%attr(0444,root,root) %{_libdir}/%{name}/battery.so
%attr(0444,root,root) %{_libdir}/%{name}/cpu.so
%attr(0444,root,root) %{_libdir}/%{name}/cpufreq.so
%attr(0444,root,root) %{_libdir}/%{name}/df.so
%attr(0444,root,root) %{_libdir}/%{name}/disk.so
%attr(0444,root,root) %{_libdir}/%{name}/email.so
%attr(0444,root,root) %{_libdir}/%{name}/hddtemp.so
%attr(0444,root,root) %{_libdir}/%{name}/irq.so
%attr(0444,root,root) %{_libdir}/%{name}/load.so
%attr(0444,root,root) %{_libdir}/%{name}/mbmon.so
%attr(0444,root,root) %{_libdir}/%{name}/memory.so
%attr(0444,root,root) %{_libdir}/%{name}/multimeter.so
%attr(0444,root,root) %{_libdir}/%{name}/nfs.so
%attr(0444,root,root) %{_libdir}/%{name}/ntpd.so
%attr(0444,root,root) %{_libdir}/%{name}/ping.so
%attr(0444,root,root) %{_libdir}/%{name}/processes.so
%attr(0444,root,root) %{_libdir}/%{name}/serial.so
%attr(0444,root,root) %{_libdir}/%{name}/swap.so
%attr(0444,root,root) %{_libdir}/%{name}/tape.so
%attr(0444,root,root) %{_libdir}/%{name}/traffic.so
%attr(0444,root,root) %{_libdir}/%{name}/users.so
%attr(0444,root,root) %{_libdir}/%{name}/vserver.so
%attr(0444,root,root) %{_libdir}/%{name}/wireless.so

%dir /var/lib/collectd

%files apache
%attr(0444,root,root) %{_libdir}/%{name}/apache.so

%files dns
%attr(0444,root,root) %{_libdir}/%{name}/dns.so

%files mysql
%attr(0444,root,root) %{_libdir}/%{name}/mysql.so

%files sensors
%attr(0444,root,root) %{_libdir}/%{name}/sensors.so

%changelog
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
