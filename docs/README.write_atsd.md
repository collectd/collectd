# write_atsd Plugin

The ATSD Write plugin sends collectd metrics to an [Axibase Time Series Database](https://axibase.com/products/axibase-time-series-database/) server.

## Run from Binary

Binary releases are available [here](https://github.com/axibase/atsd-collectd-plugin/releases/tag/5.5.1-atsd-binary).

* Pre-installation:

```ls
sudo apt-get install libltdl7                           # for Ubuntu 16.04
sudo yum install libtool-ltdl-devel yajl initscripts    # for Centos 7
```

* To run from a binary release, download and install it:

```ls
sudo dpkg -i ubuntu_1*.04_amd64.deb              # for Ubuntu
sudo rpm -Uvh sles_1*_amd64.rpm                  # for SLES
sudo rpm -Uvh centos_ *_amd64.rpm                # for Centos
```

* Replace `${ATSD_HOSTNAME}` with the hostname or IP address of the target ATSD server:
```ls
sudo sed -i s,atsd_url,tcp://${ATSD_HOSTNAME}:8081,g /opt/collectd/etc/collectd.conf # for Ubuntu
sudo sed -i s,atsd_url,tcp://${ATSD_HOSTNAME}:8081,g /etc/collectd.conf              # for others
```

* Start the service:

```
sudo service collectd-axibase start
```

* Statistics will be sent to `tcp://${ATSD_HOSTNAME}:8081` with default entity - fully qualified domain name (FQDN) of machine. To get it run:
```
hostname --fqdn
```



## Configuration

```
#LoadPlugin write_atsd
#...
#<Plugin write_atsd>
#     <Node "atsd">
#         AtsdUrl "atsd_url"
#         ShortHostname true
#         <Cache "df">
#              Interval 300
#              Threshold 0
#         </Cache>
#         <Cache "disk">
#              Interval 300
#              Threshold 0
#         </Cache>
#     </Node>
# </Plugin>
```

### Settings:

 **Setting**              | **Required** | **Description**   | **Default Value**
----------------------|:----------|:-------------------------|:----------------
 `AtsdUrl`     	      | yes      | Protocol to transfer data: `tcp` or `udp`, hostname and port of target ATSD server| `tcp://localhost:8081`
 `Entity`             | no       | Default entity under which all metrics will be stored. By default (if setting is left commented out), entity will be set to the machine hostname.                                                                    | `hostname`
  `ShortHostname`             | no       | Convert entity from fully qualified domain name to short name | `false`
 `Prefix`             | no       | Metric prefix to group `collectd` metrics                                                     | `collectd.`
 `Cache`             | no       | Name of read plugins whose metrics will be cached.<br>Cache feature is used to save disk space in the database by not resending the same values. | `-`
 `Interval`             | no       | Time in seconds during which values within the threshold are not sent. | `-`
 `Threshold`             | no       | Deviation threshold, in %, from the previously sent value. If threshold is exceeded, then the value is sent regardless of the cache interval.    | `-`


### Sample Configuration File

```xml
LoadPlugin aggregation
LoadPlugin contextswitch
LoadPlugin cpu
LoadPlugin df
LoadPlugin disk
LoadPlugin entropy
LoadPlugin interface
LoadPlugin load
LoadPlugin logfile
LoadPlugin memory
LoadPlugin processes
LoadPlugin swap
LoadPlugin syslog
LoadPlugin uptime
LoadPlugin users
LoadPlugin write_atsd
LoadPlugin vmem

# The following configuration aggregates the CPU statistics from all CPUs
# into one set using the average consolidation function.
<Plugin aggregation>
  <Aggregation>
    Plugin "cpu"
    Type "cpu"
    GroupBy "Host"
    GroupBy "TypeInstance"
    CalculateAverage true
  </Aggregation>
</Plugin>

# The following configuration collects data from all filesystems: 
# the number of free, reserved and used inodes is reported in addition to
# the usual metrics, the values are relative percentage. 
<Plugin df>
    IgnoreSelected true
    ReportInodes true
    ValuesPercentage true
</Plugin>

# The following configuration collects performance statistics from all 
# hard-disks and, where supported, partitions.
<Plugin disk>
    IgnoreSelected true
</Plugin>

# The following configuration collects information about the network traffic,
# packets per second and errors from all network interfaces exclude beginning with lo* and veth*.
<Plugin interface>
    Interface "/^lo*/"
    Interface "/^veth*/"
    IgnoreSelected true
</Plugin>

# The following configuration sets the log-level and the file to write
# log messages to; all lines are prefixed by the severity of the log
# message and by the current time.
<Plugin logfile>
    LogLevel info
    File "/var/log/collectd.log"
    Timestamp true
    PrintSeverity true
</Plugin>

# The following configuration sets the log-level info.
<Plugin syslog>
   LogLevel info
</Plugin>

# The following configuration connects to ATSD server on localhost
# via TCP and sends data via port 8081. The data will be sent with
# Entity "entity" and Prefix "collectd".
<Plugin write_atsd>
     <Node "atsd">
         AtsdUrl "udp://atsd_hostname:8082"
         <Cache "df">
              Interval 300
              Threshold 1
         </Cache>
         <Cache "disk">
              Interval 300
              Threshold 1
         </Cache>
     </Node>
 </Plugin>

# The following configuration enables verbose collection of information
# about the usage of virtual memory
<Plugin vmem>
         Verbose true
</Plugin>
```

### Sample commands sent by ATSD Write plugin:

```ls
series e:nurswgsvl007 ms:1437658049000 m:collectd.cpu.aggregation.idle.average=99.500014
series e:nurswgsvl007 ms:1437658049000 m:collectd.contextswitch.contextswitch=68.128436
series e:nurswgsvl007 ms:1437658049000 m:collectd.cpu.busy=0.301757 t:instance=0
series e:nurswgsvl007 ms:1437658049000 m:collectd.df.space.free=11977220096 t:instance=/
series e:nurswgsvl007 ms:1437658049000 m:collectd.disk.disk_io_time.io_time=17.602089 t:instance=sda
series e:nurswgsvl007 ms:1437658049000 m:collectd.entropy.available=896
series e:nurswgsvl007 ms:1437658049000 m:collectd.interface.if_octets.received=322.393744 t:instance=eth0
series e:nurswgsvl007 ms:1437658049000 m:collectd.load.loadavg.1m=0.08
series e:nurswgsvl007 ms:1437658049000 m:collectd.memory.used=332271616
series e:nurswgsvl007 ms:1437658049000 m:collectd.processes.sleeping=177
series e:nurswgsvl007 ms:1437658049000 m:collectd.memory.swap_used=139268096
series e:nurswgsvl007 ms:1437658049000 m:collectd.uptime.uptime=1185
series e:nurswgsvl007 ms:1437658049000 m:collectd.users.logged_in=4
...
```

## `exec` Plugin Processing

The [`exec`](https://collectd.org/documentation/manpages/collectd-exec.5.shtml) plugin invokes a custom script that  generates `PUTVAL` commands targeting the following format: 

```ls
PUTVAL $HOSTNAME/exec-{plugin-instance}/gauge-{type-instance} N:{value}
```

The `PUTVAL` commands are then parsed by the collectd daemon and are converted by the `write_atsd` plugin into ATSD's network API commands.

```ls
series e:$HOSTNAME m:collectd.{plugin-instance}={value} t:instance={type-instance}
```

However, if the `{type-instance}` field contains `;`-separated key=value pairs, the `write_atsd` plugin performs additional processing of this field to split it into separate series tags.

```ls
PUTVAL $HOSTNAME/exec-{plugin-instance}/gauge-{tag_key1=tag_value1;tag_key2=tag_value2} N:1479189638358
```

```ls
series e:$HOSTNAME m:collectd.{plugin-instance}={value} t:tag_key1=tag_value1 t:tag_key2=tag_value2
```

This processing is enabled subject to the following conditions:

* The `type_instance` field contains an equal sign.
* Each key/value pair consists of key, followed by equal sign, followed by value.
* Key/value pairs should be separated by semicolon `;`.

## Install From Sources

### Ubuntu / Debian

NOTE: Installation should be performed under the root user.

Install collectd dependencies:

```ls
apt-get install gcc make automake flex bison pkg-config libtool git
```

Clone Axibase collectd GitHub repository:

```ls
git clone https://github.com/axibase/atsd-collectd-plugin.git
```

Build and install collectd:

```ls
cd atsd-collectd-plugin/
./build.sh
./configure
make
make install
```

Download collectd.init file:

```ls
wget -O /etc/init.d/collectd https://raw.githubusercontent.com/martin-magakian/collectd-script/master/collectd.init
```

Change permissions to collectd directory:

```ls
chmod 744 /etc/init.d/collectd
```

All plugins are included by default, so there is no need to install them separately.

Enable ‘write_atsd’ plugin: open collectd configuration file  and set the correct configuration settings as described above:

```ls
sudo vim /opt/collectd/etc/collectd.conf
```

Restart collectd:

```ls
sudo service collectd restart
```

Check collectd daemon status:

```ls
sudo service collectd status
```
Correct response:

```ls
collectd (32615) is running
```

View collectd portal in ATSD:

Open ATSD UI and find your collectd entity on the Entities tab, click on the portal icon next to its name to open the portal (example screenshot and Chart Lab of collectd portal is below).

NOTE: steps to installing collectd on a remote machine (to send data into ATSD) are the same as described in this guide.


### CentOS / RedHat
 
NOTE: Installation should be performed under the root user.

Install Dependencies:

```ls
yum -y install libcurl libcurl-devel rrdtool rrdtool-devel rrdtool-perl libgcrypt-devel gcc make gcc-c++ git flex bison libtool byacc libtool-ltdl-devel perl perl-ExtUtils-CBuilder perl-ExtUtils-MakeMaker perl-ExtUtils-Embed
```

Download collectd, make and install it:

```ls
git clone https://github.com/axibase/atsd-collectd-plugin.git
cd atsd-collectd-plugin/
./build.sh
./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var --libdir=/usr/lib --mandir=/usr/share/man --enable-all-plugins
make
make install
```

Copy the default init.d script:

```ls
cp contrib/redhat/init.d-collectd /etc/init.d/collectd
```

Set permissions:

```ls
chmod +x /etc/init.d/collectd
```

All plugins are included in the distribution, so you don’t have to install them separately.

Enable write_atsd plugin: open collectd configuration file and setup it as above.

```ls
vim /etc/collectd.conf
```

Start collectd:

```ls
service collectd start
```

Check collectd daemon status:

```ls
service collectd status
```

Correct response:

```ls
collectd (32615) is running.
```

