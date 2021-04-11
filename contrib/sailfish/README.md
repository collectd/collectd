
# collectd port for Sailfish OS

This is a README for Sailfish port of collectd. The port has been
developed for Sailfish, but should work for other Mer-based
distributions. The port includes changes in the daemon to take into
account that the mobile OS is suspended frequently, provides
configuration and systemd unit files with the settings that have been
tuned for Sailfish.


## Using collectd

Read about collectd on the main project page.

Install collectd with all its dependencies. By default, the collectd
daemon will be enabled and started by systemd for user nemo. Its
configuration file is at /etc/collectd.conf and you have to have root
permissions to edit it. However, in contrast to usual collectd.conf
permissions, others can read this file as well.

To avoid excessive wear of permanent storage in mobile, collectd RRD
databases are stored in /tmp/collectd. Before starting the daemon,
this directory is created and contents copied from
/home/nemo/.local/share/collectd . After the daemon is stopped,
contents of /tmp/collectd is copied back to
/home/nemo/.local/share/collectd . Such movement of the collected data
is performed by /usr/bin/collectd2tmpfs script that is run by systemd.

One can consider adding syncing the data from /tmp/collectd if
needed. For that, relevant systemd configuration files could be added
by user.

If you want to change the time interval used to collect the data, note
that limited intervals are supported by keepalive library used in this
port. All intervals are listed in the relevant code starting from 

https://git.merproject.org/mer-core/nemo-keepalive/blob/master/lib-glib/keepalive-backgroundactivity.h#L69

as a "BACKGROUND ACTIVITY FREQUENCY X" enum. Note that the device will
be brought out from suspend at each interval. As usual for collectd,
if you change the interval, delete all your collected data and start
from new (if you use RRD).

As a GUI for Sailfish, a new application has been written: https://github.com/rinigus/systemdatascope . This application allows to see the recorded datasets and start/stop the daemon.

To analyze collected data, you could transfer it to Linux PC by using
rrd-sync script provided with rrdtool package in Sailfish
[packaging code is at https://github.com/rinigus/pkg-rrdtool ]. This
script allows to move RRDs between different architectures by dumping
them to XML and restoring on target machine (target should have
rrdtool installed). After transferring the data, use GUIs developed
for Linux. For example, use collectd-web with its runserver.py (adjust
path in cgi-bin/collection.modified.cgi of collectd-web and make
collection.conf as needed). Alternatively, https://github.com/rinigus/systemdatascope can be used in Linux as well.


## Implementation details

The changes in the code were done in src/daemon/collectd.c . Instead
of calling "do\_loop", glib main loop was created and used. By using
keepalive library ( https://git.merproject.org/mer-core/nemo-keepalive
), background activity interval was specified and, through glib, its
calling the callback function "do\_shot". In the end of "do\_shot", a
continuation of the loop is requested by calling
"background\_activity\_wait". Note that after calling
"background\_activity\_wait", device could suspend again.

The initialization, signal callbacks, and cleanup on shutdown were
rewritten as well. To avoid compilation warnings, "do\_loop" is
preprocessed out if HAVE\_KEEPALIVE\_GLIB is defined.

As far as I understand, the main problem with the implementation is
caused by multi-threaded approach to reading the data by
collectd. From reading the code, I am not sure whether after exiting
plugin\_read\_all all plugins have finished reading. For fully correct
implementation, that should be the case. If it is not the case, it
would be great if someone with knowledge of collectd would step in and
help to implement this call as needed.

In practice, on Nexus 4 SFOS port, every wakeup takes at least 6
seconds and within that time all plugins have been updated, as far as
I could see from the graphs.



## Packaging and helper scripts for collectd in Sailfish.

Here, a larger package with all selected modules included is made. If
needed, additional packages can be generated with modules missing from
the main package. For that, RPM spec file has to be modified.

To simplify the builds, `src/liboconfig/parser.c`
`src/liboconfig/parser.h` were added to sources. Thus, on updates,
those files may have to be regenerated. Addition was imposed due to
build errors using SDK on PC.

The build required preparation of the source in full Linux PC (I used
Gentoo) and building later in MER SDK. When building directly from git
sources on MER SDK, I had problems with bison (google pointed to some
bison bug). So, to build:

* clone git repo

* In full Linux host, not MER SDK, prepare the source files:
  * ./build.sh
  * configure
  * make -j4
  * make clean

* in MER SDK, make RPM as usual:
  * mb2 -t SailfishOS-i486 -s contrib/sailfish/collectd.spec build -j4



## Using on other Mer-based distributions

The installation script (RPM spec) uses user 'nemo' for some of its
actions. This would have to be adjusted if the user name changes in
Sailfish and on other Mer-based distributions.
