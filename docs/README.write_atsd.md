# write_atsd Plugin

The ATSD Write plugin sends collectd metrics to an [Axibase Time Series Database](https://axibase.com/products/axibase-time-series-database/) server.

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

 **Setting**      | **Required** | **Description**                                                                                                                                        | **Default Value**
------------------|:-------------|:-------------------------------------------------------------------------------------------------------------------------------------------------------|:----------------------
 `AtsdUrl`        | no           | Protocol to transfer data: `tcp` or `udp`, hostname and port of target ATSD server                                                                     | `tcp://localhost:8081`
 `Entity`         | no           | Default entity under which all metrics will be stored. By default (if setting is left commented out), entity will be set to the machine hostname.      | `hostname`
  `ShortHostname` | no           | Convert entity from fully qualified domain name to short name                                                                                          | `false`
 `Prefix`         | no           | Metric prefix to group `collectd` metrics                                                                                                              | `collectd`
 `Cache`          | no           | Name of read plugins whose metrics will be cached. Cache feature is used to save disk space in the database by not resending the same values.          | `-`
 `Interval`       | no           | Time in seconds during which values within the threshold are not sent.                                                                                 | `-`
 `Threshold`      | no           | Deviation threshold, in %, from the previously sent value. If threshold is exceeded, then the value is sent regardless of the cache interval.          | `-`
 `StoreRates`     | no           | If set to true, convert counter values to rates. If set to false counter values are stored as is, i. e. as an increasing integer number.               | true

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
