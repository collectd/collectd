Linux Telemetry Plugins
=======================

These Collectd plugins enable detailed monitoring of a typical Linux server 
at device and sub-system levels. These python based plugins extend base Collectd 
to monitor disk, flash, RAID, virtual memory, NUMA nodes, memory allocation
zones, and buddy allocator metrics.

Following plugins are being provided:

1. Diskstats
2. Fusion-io
3. Vmstats
4. Buddyinfo
5. Zoneinfo

Except for fusion-io plugin, all others gather system level metrics
through procfs from corresponding locations: /proc/diskstats,
/proc/vmstats, /proc/buddyinfo, and /proc/zoneinfo.

Installation
------------

Installation process is tested on Red Hat Enterprise Linux Server
release 6.5. It will need to be adapted for other variants of Linux.

###Step 1. Base collectd installation with python:

Base collectd is available from:

https://collectd.org/download.shtml

Current collectd version is 5. There are multiple ways to install including building from source. For
many cloud environments, RPM installation will be relevant. Three RPM
packages are needed for all of the above python plugins to work:

1. collectd-5.4.0-105.el6.x86_64
2. libcollectdclient-5.4.0-105.el6.x86_64
3. collectd-python-5.4.0-105.el6.x86_64


Ensure that your yum repo has above three RPMs, which should be
installed by:

`yum --nogpgcheck -y install collectd libcollectdclient collectd-python`

Verify the installation by running 

`sudo rpm -qa | grep collectd`,

which should show that above three packages are installed.

###Step 2. Installation of telemetry plugins:

Run the install script:

`sudo ./install.sh`

This install script assumes that base collectd installation through
yum resulted in:

1. /etc/collectd.conf
2. /etc/collectd.d (for plugin in conf files)
3. /usr/share/collectd/types.db
4. /usr/share/collectd/plugins/python (for python plugins)

Depending on the RPM packaging, it is not guaranteed that above will
be true in all cases. In those cases, either base installation or
telemetry plugin installation script will require adjustments.


###Step 3. Start/restart collectd:

`service collectd start` (or `restart` if already running)

Testing
-------

In order to test the measurements that these plugins gather, enable
CSV plugin from collectd.conf to print the values in text files. You
can choose any one of several available collectd supported tools to forward the
measurements from target hosts to an aggregation point in a cloud
environment for continuous remote monitoring.

Plugin specific information is available in plugins .py files.

