 collectd - System information collection daemon
=================================================
https://collectd.org/

About
-----

  collectd is a small daemon which collects system information periodically
  and provides mechanisms to store and monitor the values in a variety of
  ways.

- [collectd - System information collection daemon](#collectd---system-information-collection-daemon)
  - [About](#about)
  - [Features](#features)
  - [Operation](#operation)
  - [collectd and chkrootkit](#collectd-and-chkrootkit)
  - [Prerequisites](#prerequisites)
  - [Configuring / Compiling / Installing](#configuring--compiling--installing)
  - [Generating the configure script](#generating-the-configure-script)
  - [Building on Windows](#building-on-windows)
  - [Crosscompiling](#crosscompiling)
  - [Contact](#contact)
  - [Author](#author)


Features
--------

  * collectd is able to collect the following data:

    - apache
      Apache server utilization: Number of bytes transferred, number of
      requests handled and detailed scoreboard statistics

    - apcups
      APC UPS Daemon: UPS charge, load, input/output/battery voltage, etc.

    - apple_sensors
      Sensors in Macs running Mac OS X / Darwin: Temperature, fan speed and
      voltage sensors.

    - aquaero
      Various sensors in the Aquaero 5 water cooling board made by Aquacomputer.

    - ascent
      Statistics about Ascent, a free server for the game "World of Warcraft".

    - barometer
      Reads absolute barometric pressure, air pressure reduced to sea level and
      temperature.  Supported sensors are MPL115A2 and MPL3115 from Freescale
      and BMP085 from Bosch.

    - battery
      Batterycharge, -current and voltage of ACPI and PMU based laptop
      batteries.

    - bind
      Name server and resolver statistics from the `statistics-channel`
      interface of BIND 9.5, 9,6 and later.

    - buddyinfo
      Statistics from buddyinfo file about memory fragmentation.

    - capabilities
      Platform capabilities decoded from hardware subsystems, for example from
      SMBIOS using dmidecode.
      <https://www.nongnu.org/dmidecode/>

    - ceph
      Statistics from the Ceph distributed storage system.

    - cgroups
      CPU accounting information for process groups under Linux.

    - chrony
      Chrony daemon statistics: Local clock drift, offset to peers, etc.

    - connectivity
      Event-based interface status.

    - conntrack
      Number of nf_conntrack entries.

    - contextswitch
      Number of context switches done by the operating system.

    - cpu
      CPU utilization: Time spent in the system, user, nice, idle, and related
      states.

    - cpufreq
      CPU frequency (For laptops with speed step or a similar technology)

    - cpusleep
      CPU sleep: Time spent in suspend (For mobile devices which enter suspend automatically)

    - curl
      Parse statistics from websites using regular expressions.

    - curl_json
      Retrieves JSON data via cURL and parses it according to user
      configuration.

    - curl_xml
      Retrieves XML data via cURL and parses it according to user
      configuration.

    - dbi
      Executes SQL statements on various databases and interprets the returned
      data.

    - dcpmm
      Collects Intel Optane DC Presistent Memory (DCPMM) performance and health statistics.

    - df
      Mountpoint usage (Basically the values `df(1)` delivers)

    - disk
      Disk utilization: Sectors read/written, number of read/write actions,
      average time an IO-operation took to complete.

    - dns
      DNS traffic: Query types, response codes, opcodes and traffic/octets
      transferred.

    - dpdkstat
      Collect DPDK interface statistics.
      See docs/BUILD.dpdkstat.md for detailed build instructions.

      This plugin should be compiled with compiler defenses enabled, for
      example -fstack-protector.

    - dpdk_telemetry
      Collect DPDK interface, application and global statistics.
      This plugin can be used as a substitute to dpdkstat plugin.

      This plugin is dependent on DPDK 19.08 release and must be used
      along with the DPDK application.

      Also, this plugin has dependency on Jansson library.

    - drbd
      Collect individual drbd resource statistics.

    - email
      Email statistics: Count, traffic, spam scores and checks.
      See collectd-email(5).

    - entropy
      Amount of entropy available to the system.

    - epics
      Collect data from EPICS message bus.
      <https://epics-controls.org>

    - ethstat
      Network interface card statistics.

    - exec
      Values gathered by a custom program or script.
      See collectd-exec(5).

    - fhcount
      File handles statistics.

    - filecount
      Count the number of files in directories.

    - fscache
      Linux file-system based caching framework statistics.

    - gmond
      Receive multicast traffic from Ganglia instances.

    - gps
      Monitor gps related data through gpsd.

    - gpu_nvidia
      Monitor NVIDIA GPU statistics available through NVML.

    - hddtemp
      Hard disk temperatures using hddtempd.

    - hugepages
      Report the number of used and free hugepages. More info on
      hugepages can be found here:
      https://www.kernel.org/doc/Documentation/vm/hugetlbpage.txt.

      This plugin should be compiled with compiler defenses enabled, for
      example -fstack-protector.

    - infiniband
      Attributes and counters for each port on each IB device.

    - intel_pmu
      The intel_pmu plugin reads performance counters provided by the Linux
      kernel perf interface. The plugin uses jevents library to resolve named
      events to perf events and access perf interface.

    - intel_rdt
      The intel_rdt plugin collects information provided by monitoring features
      of Intel Resource Director Technology (Intel(R) RDT) like Cache Monitoring
      Technology (CMT), Memory Bandwidth Monitoring (MBM). These features
      provide information about utilization of shared resources like last level
      cache occupancy, local memory bandwidth usage, remote memory bandwidth
      usage, instructions per clock.
      <https://01.org/packet-processing/cache-monitoring-technology-memory-bandwidth-monitoring-cache-allocation-technology-code-and-data>

    - interface
      Interface traffic: Number of octets, packets and errors for each
      interface.

    - ipc
      IPC counters: semaphores used, number of allocated segments in shared
      memory and more.

    - ipmi
      IPMI (Intelligent Platform Management Interface) sensors information.

    - ipstats
      IPv4 and IPv6; incoming, outgoing, forwarded counters. FreeBSD only.

    - iptables
      Iptables' counters: Number of bytes that were matched by a certain
      iptables rule.

    - ipvs
      IPVS connection statistics (number of connections, octets and packets
      for each service and destination).
      See http://www.linuxvirtualserver.org/software/index.html.

    - irq
      IRQ counters: Frequency in which certain interrupts occur.

    - java
      Integrates a Java Virtual Machine (JVM) to execute plugins in Java
      bytecode.
      See docs/BUILD.java.md for detailed build instructions.

    - load
      System load average over the last 1, 5 and 15 minutes.

    - lpar
      Detailed CPU statistics of the “Logical Partitions” virtualization
      technique built into IBM's POWER processors.

    - lua
      The Lua plugin implements a Lua interpreter into collectd. This
      makes it possible to write plugins in Lua which are executed by
      collectd without the need to start a heavy interpreter every interval.
      See collectd-lua(5) for details.

    - madwifi
      Queries very detailed usage statistics from wireless LAN adapters and
      interfaces that use the Atheros chipset and the MadWifi driver.

    - mbmon
      Motherboard sensors: temperature, fan speed and voltage information,
      using mbmon(1).

    - mcelog
      Monitor machine check exceptions (hardware errors detected by hardware
      and reported to software) reported by mcelog and generate appropriate
      notifications when machine check exceptions are detected.

    - md
      Linux software-RAID device information (number of active, failed, spare
      and missing disks).

    - memcachec
      Query and parse data from a memcache daemon (memcached).

    - memcached
      Statistics of the memcached distributed caching system.
      <http://www.danga.com/memcached/>

    - memory
      Memory utilization: Memory occupied by running processes, page cache,
      buffer cache and free.

    - mic
      Collects CPU usage, memory usage, temperatures and power consumption from
      Intel Many Integrated Core (MIC) CPUs.

    - mmc
      Reads the life time estimates reported by eMMC 5.0+ devices and some more
      detailed health metrics, like bad block and erase counts or power cycles,
      for micron and sandisk eMMCs and some swissbit mmc Cards (MANFID=0x5D
      OEMID=0x5342).

    - modbus
      Reads values from Modbus/TCP enabled devices. Supports reading values
      from multiple "slaves" so gateway devices can be used.

    - multimeter
      Information provided by serial multimeters, such as the `Metex M-4650CR`.

    - mysql
      MySQL server statistics: Commands issued, handlers triggered, thread
      usage, query cache utilization and traffic/octets sent and received.

    - netapp
      Plugin to query performance values from a NetApp storage system using the
      “Manage ONTAP” SDK provided by NetApp.

    - netlink
      Very detailed Linux network interface and routing statistics. You can get
      (detailed) information on interfaces, qdiscs, classes, and, if you can
      make use of it, filters.

    - network
      Receive values that were collected by other hosts. Large setups will
      want to collect the data on one dedicated machine, and this is the
      plugin of choice for that.

    - nfs
      NFS Procedures: Which NFS command were called how often.

    - nginx
      Collects statistics from `nginx` (speak: engine X), a HTTP and mail
      server/proxy.

    - ntpd
      NTP daemon statistics: Local clock drift, offset to peers, etc.

    - numa
      Information about Non-Uniform Memory Access (NUMA).

    - nut
      Network UPS tools: UPS current, voltage, power, charge, utilisation,
      temperature, etc. See upsd(8).

    - olsrd
      Queries routing information from the “Optimized Link State Routing”
      daemon.

    - onewire (EXPERIMENTAL!)
      Read onewire sensors using the owcapu library of the owfs project.
      Please read in collectd.conf(5) why this plugin is experimental.

    - openldap
      Read monitoring information from OpenLDAP's cn=Monitor subtree.

    - openvpn
      RX and TX of each client in openvpn-status.log (status-version 2).
      <http://openvpn.net/index.php/documentation/howto.html>

    - oracle
      Query data from an Oracle database.

    - ovs_events
      The plugin monitors the link status of Open vSwitch (OVS) connected
      interfaces, dispatches the values to collectd and sends the notification
      whenever the link state change occurs in the OVS database. It requires
      YAJL library to be installed.
      Detailed instructions for installing and setting up Open vSwitch, see
      OVS documentation.
      <http://openvswitch.org/support/dist-docs/INSTALL.rst.html>

    - ovs_stats
      The plugin collects the statistics of OVS connected bridges and
      interfaces. It requires YAJL library to be installed.
      Detailed instructions for installing and setting up Open vSwitch, see
      OVS documentation.
      <http://openvswitch.org/support/dist-docs/INSTALL.rst.html>

    - pcie_errors
      Read errors from PCI Express Device Status and AER extended capabilities.
      <https://www.design-reuse.com/articles/38374/pcie-error-logging-and-handling-on-a-typical-soc.html>

    - perl
      The perl plugin implements a Perl-interpreter into collectd. You can
      write your own plugins in Perl and return arbitrary values using this
      API. See collectd-perl(5).

    - pf
      Query statistics from BSD's packet filter "pf".

    - pinba
      Receive and dispatch timing values from Pinba, a profiling extension for
      PHP.

    - ping
      Network latency: Time to reach the default gateway or another given
      host.

    - postgresql
      PostgreSQL database statistics: active server connections, transaction
      numbers, block IO, table row manipulations.

    - powerdns
      PowerDNS name server statistics.

    - processes
      Process counts: Number of running, sleeping, zombie, ... processes.

    - procevent
      Listens for process starts and exits via netlink.

    - protocols
      Counts various aspects of network protocols such as IP, TCP, UDP, etc.

    - python
      The python plugin implements a Python interpreter into collectd. This
      makes it possible to write plugins in Python which are executed by
      collectd without the need to start a heavy interpreter every interval.
      See collectd-python(5) for details.

    - ras
      The ras plugin gathers and counts errors provided by RASDaemon

    - redis
      The redis plugin gathers information from a Redis server, including:
      uptime, used memory, total connections etc.

    - routeros
      Query interface and wireless registration statistics from RouterOS.

    - rrdcached
      RRDtool caching daemon (RRDcacheD) statistics.

    - sensors
      System sensors, accessed using lm_sensors: Voltages, temperatures and
      fan rotation speeds.

    - serial
      RX and TX of serial interfaces. Linux only; needs root privileges.

    - sigrok
      Uses libsigrok as a backend, allowing any sigrok-supported device
      to have its measurements fed to collectd. This includes multimeters,
      sound level meters, thermometers, and much more.

    - slurm
      Gathers per-partition node and job state information using libslurm,
      as well as internal health statistics.

    - smart
      Collect SMART statistics, notably load cycle count, temperature
      and bad sectors.

    - snmp
      Read values from SNMP (Simple Network Management Protocol) enabled
      network devices such as switches, routers, thermometers, rack monitoring
      servers, etc. See collectd-snmp(5).

    - statsd
      Acts as a StatsD server, reading values sent over the network from StatsD
      clients and calculating rates and other aggregates out of these values.

    - sysevent
      Listens to rsyslog events and submits matched values.

    - swap
      Pages swapped out onto hard disk or whatever is called `swap` by the OS.

    - table
      Parse table-like structured files.

    - tail
      Follows (tails) log files, parses them by lines and submits matched
      values.

    - tail_csv
      Follows (tails) files in CSV format, parses each line and submits
      extracted values.

    - tape
      Bytes and operations read and written on tape devices. Solaris only.

    - tcpconns
      Number of TCP connections to specific local and remote ports.

    - teamspeak2
      TeamSpeak2 server statistics.

    - ted
      Plugin to read values from "the energy detective" (TED).

    - thermal
      Linux ACPI thermal zone information.

    - tokyotyrant
      Reads the number of records and file size from a running Tokyo Tyrant
      server.

    - turbostat
      Reads CPU frequency and C-state residency on modern Intel
      turbo-capable processors.

    - ubi
      Reads the count of bad physical eraseblocks and the current
      maximum erase counter value on UBI volumes.

    - uptime
      System uptime statistics.

    - users
      Users currently logged in.

    - varnish
      Various statistics from Varnish, an HTTP accelerator.

    - virt
      CPU, memory, disk and network I/O statistics from virtual machines.

    - vmem
      Virtual memory statistics, e.g. the number of page-ins/-outs or the
      number of pagefaults.

    - vserver
      System resources used by Linux VServers.
      See <http://linux-vserver.org/>.

    - wireless
      Link quality of wireless cards. Linux only.

    - xencpu
      XEN Hypervisor CPU stats.

    - xmms
      Bitrate and frequency of music played with XMMS.

    - zfs_arc
      Statistics for ZFS' “Adaptive Replacement Cache” (ARC).

    - zone
      Measures the percentage of cpu load per container (zone) under Solaris 10
      and higher

    - zookeeper
      Read data from Zookeeper's MNTR command.

  * Output can be written or sent to various destinations by the following
    plugins:

    - amqp
      Sends JSON-encoded data to an Advanced Message Queuing Protocol (AMQP)
      0.9.1 server, such as RabbitMQ.

    - amqp1
      Sends JSON-encoded data to an Advanced Message Queuing Protocol (AMQP)
      1.0 server, such as Qpid Dispatch Router or Apache Artemis Broker.

    - csv
      Write to comma separated values (CSV) files. This needs lots of
      diskspace but is extremely portable and can be analysed with almost
      every program that can analyse anything. Even Microsoft's Excel.

    - grpc
      Send and receive values over the network using the gRPC framework.

    - lua
      It's possible to implement write plugins in Lua using the Lua
      plugin. See collectd-lua(5) for details.

    - mqtt
      Publishes and subscribes to MQTT topics.

    - network
      Send the data to a remote host to save the data somehow. This is useful
      for large setups where the data should be saved by a dedicated machine.

    - perl
      Of course the values are propagated to plugins written in Perl, too, so
      you can easily do weird stuff with the plugins we didn't dare think of
      ;) See collectd-perl(5).

    - python
      It's possible to implement write plugins in Python using the python
      plugin. See collectd-python(5) for details.

    - rrdcached
      Output to round-robin-database (RRD) files using the RRDtool caching
      daemon (RRDcacheD) - see `rrdcached(1)`. That daemon provides a general
      implementation of the caching done by the `rrdtool` plugin.

    - rrdtool
      Output to round-robin-database (RRD) files using librrd. See rrdtool(1).
      This is likely the most popular destination for such values. Since
      updates to RRD-files are somewhat expensive this plugin can cache
      updates to the files and write a bunch of updates at once, which lessens
      system load a lot.

    - snmp_agent
      Receives and handles queries from SNMP master agent and returns the data
      collected by read plugins. Handles requests only for OIDs specified in
      configuration file. To handle SNMP queries the plugin gets data from
      collectd and translates requested values from collectd's internal format
      to SNMP format.

    - unixsock
      One can query the values from the unixsock plugin whenever they're
      needed. Please read collectd-unixsock(5) for a description on how that's
      done.

    - write_graphite
      Sends data to Carbon, the storage layer of Graphite using TCP or UDP. It
      can be configured to avoid logging send errors (especially useful when
      using UDP).

    - write_http
      Sends the values collected by collectd to a web-server using HTTP POST
      requests. The transmitted data is either in a form understood by the
      Exec plugin or formatted in JSON.

    - write_kafka
      Sends data to Apache Kafka, a distributed queue.

    - write_log
      Writes data to the log

    - write_mongodb
      Sends data to MongoDB, a NoSQL database.

    - write_prometheus
      Publish values using an embedded HTTP server, in a format compatible
      with Prometheus' collectd_exporter.

    - write_redis
      Sends the values to a Redis key-value database server.

    - write_riemann
      Sends data to Riemann, a stream processing and monitoring system.

    - write_sensu
      Sends data to Sensu, a stream processing and monitoring system, via the
      Sensu client local TCP socket.

    - write_syslog
      Sends data in syslog format, using TCP, where the message
      contains the metric in human or JSON format.

    - write_tsdb
      Sends data OpenTSDB, a scalable no master, no shared state time series
      database.

  * Logging is, as everything in collectd, provided by plugins. The following
    plugins keep us informed about what's going on:

    - logfile
      Writes log messages to a file or STDOUT/STDERR.

    - perl
      Log messages are propagated to plugins written in Perl as well.
      See collectd-perl(5).

    - python
      It's possible to implement log plugins in Python using the python plugin.
      See collectd-python(5) for details.

    - syslog
      Logs to the standard UNIX logging mechanism, syslog.

    - log_logstash
      Writes log messages formatted as logstash JSON events.

  * Notifications can be handled by the following plugins:

    - notify_desktop
      Send a desktop notification to a notification daemon, as defined in
      the Desktop Notification Specification. To actually display the
      notifications, notification-daemon is required.
      See http://www.galago-project.org/specs/notification/.

    - notify_email
      Send an E-mail with the notification message to the configured
      recipients.

    - notify_nagios
      Submit notifications as passive check results to a local nagios instance.

    - exec
      Execute a program or script to handle the notification.
      See collectd-exec(5).

    - logfile
      Writes the notification message to a file or STDOUT/STDERR.

    - network
      Send the notification to a remote host to handle it somehow.

    - perl
      Notifications are propagated to plugins written in Perl as well.
      See collectd-perl(5).

    - python
      It's possible to implement notification plugins in Python using the
      python plugin. See collectd-python(5) for details.

  * Value processing can be controlled using the "filter chain" infrastructure
    and "matches" and "targets". The following plugins are available:

    - match_empty_counter
      Match counter values which are currently zero.

    - match_hashed
      Match values using a hash function of the hostname.

    - match_regex
      Match values by their identifier based on regular expressions.

    - match_timediff
      Match values with an invalid timestamp.

    - match_value
      Select values by their data sources' values.

    - target_notification
      Create and dispatch a notification.

    - target_replace
      Replace parts of an identifier using regular expressions.

    - target_scale
      Scale (multiply) values by an arbitrary value.

    - target_set
      Set (overwrite) entire parts of an identifier.

  * Miscellaneous plugins:

    - aggregation
      Selects multiple value lists based on patterns or regular expressions
      and creates new aggregated values lists from those.

    - threshold
      Checks values against configured thresholds and creates notifications if
      values are out of bounds. See collectd-threshold(5) for details.

    - uuid
      Sets the hostname to a unique identifier. This is meant for setups
      where each client may migrate to another physical host, possibly going
      through one or more name changes in the process.

  * Performance: Since collectd is running as a daemon it doesn't spend much
    time starting up again and again. With the exception of the exec plugin no
    processes are forked. Caching in output plugins, such as the rrdtool and
    network plugins, makes sure your resources are used efficiently. Also,
    since collectd is programmed multithreaded it benefits from hyper-threading
    and multicore processors and makes sure that the daemon isn't idle if only
    one plugin waits for an IO-operation to complete.

  * Once set up, hardly any maintenance is necessary. Setup is kept as easy
    as possible and the default values should be okay for most users.


Operation
---------

  * collectd's configuration file can be found at `sysconfdir/collectd.conf`.
    Run `collectd -h` for a list of built-in defaults. See `collectd.conf(5)`
    for a list of options and a syntax description.

  * When the `csv` or `rrdtool` plugins are loaded they'll write the values to
    files. The usual place for these files is beneath `/var/lib/collectd`.

  * When using some of the plugins, collectd needs to run as user root, since
    only root can do certain things, such as craft ICMP packages needed to ping
    other hosts. collectd should NOT be installed setuid root since it can be
    used to overwrite valuable files!

  * Sample scripts to generate graphs reside in `contrib/` in the source
    package or somewhere near `/usr/share/doc/collectd` in most distributions.
    Please be aware that those script are meant as a starting point for your
    own experiments. Some of them require the `RRDs` Perl module.
    (`librrds-perl` on Debian) If you have written a more sophisticated
    solution please share it with us.

  * The RRAs of the automatically created RRD files depend on the `step`
    and `heartbeat` settings given. If change these settings you may need to
    re-create the files, losing all data. Please be aware of that when changing
    the values and read the rrdtool(1) manpage thoroughly.


collectd and chkrootkit
-----------------------

  If you are using the `dns` plugin, `chkrootkit(1)` will report `collectd` as a
  packet sniffer (`<iface>: PACKET SNIFFER(/usr/sbin/collectd[<pid>])`). The
  plugin captures all UDP packets on port 53 to analyze the DNS traffic. In
  this case, collectd is a legitimate sniffer and the report should be
  considered to be a false positive. However, you might want to check that
  this really is collectd and not some other, illegitimate sniffer.


Prerequisites
-------------

  To compile collectd from source you will need:

  * Usual suspects: C compiler, linker, preprocessor, make, ...

    collectd makes use of some common C99 features, e.g. compound literals and
    mixed declarations, and therefore requires a C99 compatible compiler.

    On Debian and Ubuntu, the "build-essential" package should pull in
    everything that's necessary.

  * A POSIX-threads (pthread) implementation.
    Since gathering some statistics is slow (network connections, slow devices,
    etc) collectd is parallelized. The POSIX threads interface is being
    used and should be found in various implementations for hopefully all
    platforms.

  * When building from the Git repository, flex (tokenizer) and bison (parser
    generator) are required. Release tarballs include the generated files – you
    don't need these packages in that case.

  * aerotools-ng (optional)
    Used by the `aquaero` plugin. Currently, the `libaquaero5` library, which
    is used by the `aerotools-ng` toolkit, is not compiled as a shared object
    nor does it feature an installation routine. Therefore, you need to point
    collectd's configure script at the source directory of the `aerotools-ng`
    project.
    <https://github.com/lynix/aerotools-ng>

  * CoreFoundation.framework and IOKit.framework (optional)
    For compiling on Darwin in general and the `apple_sensors` plugin in
    particular.
    <http://developer.apple.com/corefoundation/>

  * CUDA (optional)
    Used by the `gpu_nvidia` plugin
    <https://developer.nvidia.com/cuda-downloads>

  * libatasmart (optional)
    Used by the `smart` plugin.
    <http://git.0pointer.de/?p=libatasmart.git>

  * libcap (optional)
    The `turbostat` plugin can optionally build Linux Capabilities support,
    which avoids full privileges requirement (aka. running as root) to read
    values.
    <http://sites.google.com/site/fullycapable/>

  * libclntsh (optional)
    Used by the `oracle` plugin.

  * libhiredis (optional)
    Used by the `redis` plugin. Please note that you require a 0.10.0 version
    or higher. <https://github.com/redis/hiredis>

  * libcurl (optional)
    If you want to use the `apache`, `ascent`, `bind`, `curl`, `curl_json`,
    `curl_xml`, `nginx`, or `write_http` plugin.
    <http://curl.haxx.se/>

  * libdbi (optional)
    Used by the `dbi` plugin to connect to various databases.
    <http://libdbi.sourceforge.net/>

  * libesmtp (optional)
    For the `notify_email` plugin.
    <http://www.stafford.uklinux.net/libesmtp/>

  * libganglia (optional)
    Used by the `gmond` plugin to process data received from Ganglia.
    <http://ganglia.info/>

  * libgrpc (optional)
    Used by the `grpc` plugin. gRPC requires a C++ compiler supporting the
    C++11 standard.
    <https://grpc.io/>

  * libgcrypt (optional)
    Used by the `network` plugin for encryption and authentication.
    <http://www.gnupg.org/>

  * libgps (optional)
    Used by the `gps` plugin.
    <http://developer.berlios.de/projects/gpsd/>

  * libi2c-dev (optional)
    Used for the plugin `barometer`, provides just the `i2c-dev.h` header file
    for user space i2c development.

  * libiptc (optional)
    For querying iptables counters.
    <http://netfilter.org/>

  * libjansson (optional)
    Parse JSON data. This is used for the `capabilities` and `dpdk_telemetry` plugins.
    <http://www.digip.org/jansson/>

  * libjevents (optional)
    The jevents library is used by the `intel_pmu` plugin to access the Linux
    kernel perf interface.
    Note: the library should be build with `-fPIC` flag to be linked with
    intel_pmu shared object correctly.
    <https://github.com/andikleen/pmu-tools>

  * libjvm (optional)
    Library that encapsulates the Java Virtual Machine (JVM). This library is
    used by the `java` plugin to execute Java bytecode.
    See docs/BUILD.java.md for detailed build instructions.
    <http://openjdk.java.net/> (and others)

  * libldap (optional)
    Used by the `openldap` plugin.
    <http://www.openldap.org/>

  * liblua (optional)
    Used by the `lua` plugin. Currently, Lua 5.1 and later are supported.
    <https://www.lua.org/>

  * libmemcached (optional)
    Used by the `memcachec` plugin to connect to a memcache daemon.
    <http://tangent.org/552/libmemcached.html>

  * libmicrohttpd (optional)
    Used by the write_prometheus plugin to run an http daemon.
    <http://www.gnu.org/software/libmicrohttpd/>

  * libmnl (optional)
    Used by the `netlink` plugin.
    <http://www.netfilter.org/projects/libmnl/>

  * libmodbus (optional)
    Used by the `modbus` plugin to communicate with Modbus/TCP devices. The
    `modbus` plugin works with version 2.0.3 of the library – due to frequent
    API changes other versions may or may not compile cleanly.
    <http://www.libmodbus.org/>

  * libmysqlclient (optional)
    Unsurprisingly used by the `mysql` plugin.
    <http://dev.mysql.com/>

  * libnetapp (optional)
    Required for the `netapp` plugin.
    This library is part of the “Manage ONTAP SDK” published by NetApp.

  * libnetsnmp (optional)
    For the `snmp` and `snmp_agent` plugins.
    <http://www.net-snmp.org/>

  * libnetsnmpagent (optional)
    Required for the `snmp_agent` plugin.
    <http://www.net-snmp.org/>

  * libnotify (optional)
    For the `notify_desktop` plugin.
    <http://www.galago-project.org/>

  * libopenipmi (optional)
    Used by the `ipmi` plugin to prove IPMI devices.
    <http://openipmi.sourceforge.net/>

  * liboping (optional)
    Used by the `ping` plugin to send and receive ICMP packets.
    <http://octo.it/liboping/>

  * libowcapi (optional)
    Used by the `onewire` plugin to read values from onewire sensors (or the
    owserver(1) daemon).
    <http://www.owfs.org/>

  * libpcap (optional)
    Used to capture packets by the `dns` plugin.
    <http://www.tcpdump.org/>

  * libperfstat (optional)
    Used by various plugins to gather statistics under AIX.

  * libperl (optional)
    Obviously used by the `perl` plugin. The library has to be compiled with
    ithread support (introduced in Perl 5.6.0).
    <http://www.perl.org/>

  * libpmwapi (optional)
    Used by the `dcpmm` plugin.
    The library github: https://github.com/intel/intel-pmwatch
    Follow the pmwatch build instructions mentioned for dcpmm plugin and
    use the install path to resolve the dependency here.

  * libpq (optional)
    The PostgreSQL C client library used by the `postgresql` plugin.
    <http://www.postgresql.org/>

  * libpqos (optional)
    The PQoS library for Intel(R) Resource Director Technology used by the
    `intel_rdt` plugin.
    <https://github.com/01org/intel-cmt-cat>

  * libprotobuf, protoc 3.0+ (optional)
    Used by the `grpc` plugin to generate service stubs and code to handle
    network packets of collectd's protobuf-based network protocol.
    <https://developers.google.com/protocol-buffers/>

  * libprotobuf-c, protoc-c (optional)
    Used by the `pinba` plugin to generate a parser for the network packets
    sent by the Pinba PHP extension.
    <http://code.google.com/p/protobuf-c/>

  * libpython (optional)
    Used by the `python` plugin. Currently, Python 2.6 and later and Python 3
    are supported.
    <http://www.python.org/>

  * libqpid-proton (optional)
    Used by the `amqp1` plugin for AMQP 1.0 connections, for example to
    Qdrouterd.
    <http://qpid.apache.org/>

  * librabbitmq (optional; also called “rabbitmq-c”)
    Used by the `amqp` plugin for AMQP 0.9.1 connections, for example to
    RabbitMQ.
    <http://hg.rabbitmq.com/rabbitmq-c/>

  * librdkafka (optional; also called “rdkafka”)
    Used by the `write_kafka` plugin for producing messages and sending them
    to a Kafka broker.
    <https://github.com/edenhill/librdkafka>

  * librouteros (optional)
    Used by the `routeros` plugin to connect to a device running _RouterOS_.
    <http://octo.it/librouteros/>

  * librrd (optional)
    Used by the `rrdtool` and `rrdcached` plugins. The latter requires RRDtool
    client support which was added after version 1.3 of RRDtool. Versions 1.0,
    1.2 and 1.3 are known to work with the `rrdtool` plugin.
    <http://oss.oetiker.ch/rrdtool/>

  * librt, libsocket, libkstat, libdevinfo (optional)
    Various standard Solaris libraries which provide system functions.
    <http://developers.sun.com/solaris/>

  * libsensors (optional)
    To read from `lm_sensors`, see the `sensors` plugin.
    <http://www.lm-sensors.org/>

  * libsigrok (optional)
    Used by the `sigrok` plugin. In addition, `libsigrok` depends on `glib`,
    `libzip`, and optionally (depending on which drivers are enabled) on
    `libusb`, `libftdi` and `libudev`.

  * libslurm (optional)
    Used by the `slurm` plugin.
    <https://slurm.schedmd.com/>

  * libsqlite3 (optional)
    Used by the `ras` plugin.
    <https://sqlite.org/>

  * libstatgrab (optional)
    Used by various plugins to collect statistics on systems other than Linux
    and/or Solaris.
    <http://www.i-scream.org/libstatgrab/>

  * libtokyotyrant (optional)
    Used by the `tokyotyrant` plugin.
    <http://1978th.net/tokyotyrant/>

  * libupsclient/nut (optional)
    For the `nut` plugin which queries nut's `upsd`.
    <http://networkupstools.org/>

  * libvirt (optional)
    Collect statistics from virtual machines.
    <http://libvirt.org/>

  * libxml2 (optional)
    Parse XML data. This is needed for the `ascent`, `bind`, `curl_xml` and
    `virt` plugins.
    <http://xmlsoft.org/>

  * libxen (optional)
    Used by the `xencpu` plugin.
    <http://xenbits.xensource.com/>

  * libxmms (optional)
    <http://www.xmms.org/>

  * libyajl (optional)
    Parse JSON data. This is needed for the `ceph`, `curl_json`, `ovs_events`,
    `ovs_stats` and `log_logstash` plugins.
    <http://github.com/lloyd/yajl>

  * libvarnish (optional)
     Fetches statistics from a Varnish instance. This is needed for the
     `varnish` plugin.
     <http://varnish-cache.org>

  * riemann-c-client (optional)
     For the `write_riemann` plugin.
     <https://github.com/algernon/riemann-c-client>

Configuring / Compiling / Installing
------------------------------------

  To configure, build and install collectd with the default settings, run
  `./configure && make && make install`.  For a complete list of configure
  options and their description, run `./configure --help`.

  By default, the configure script will check for all build dependencies and
  disable all plugins whose requirements cannot be fulfilled (any other plugin
  will be enabled). To enable a plugin, install missing dependencies (see
  section [Prerequisites](#prerequisites) above) and rerun `configure`. If you specify the
  `--enable-<plugin>` configure option, the script will fail if the depen-
  dencies for the specified plugin are not met. In that case you can force the
  plugin to be built using the `--enable-<plugin>=force` configure option.
  This will most likely fail though unless you're working in a very unusual
  setup and you really know what you're doing. If you specify the
  `--disable-<plugin>` configure option, the plugin will not be built. If you
  specify the `--enable-all-plugins` or `--disable-all-plugins` configure
  options, all plugins will be enabled or disabled respectively by default.
  Explicitly enabling or disabling a plugin overwrites the default for the
  specified plugin. These options are meant for package maintainers and should
  not be used in everyday situations.

  By default, collectd will be installed into `/opt/collectd`. You can adjust
  this setting by specifying the `--prefix` configure option - see INSTALL for
  details. If you pass `DESTDIR=<path>` to `make install`, `<path>` will be
  prefixed to all installation directories. This might be useful when creating
  packages for collectd.

Generating the configure script
-------------------------------

Collectd ships with a `build.sh` script to generate the `configure`
script shipped with releases.

To generate the `configure` script, you'll need the following dependencies:

- autoconf
- automake
- flex
- bison
- libtool
- pkg-config

The `build.sh` script takes no arguments.


Building on Windows
-----------------------------------------------

Collectd can be built on Windows using Cygwin, and the result is a binary that
runs natively on Windows. That is, Cygwin is only needed for building, not running,
collectd.

You will need to install the following Cygwin packages:
- automake
- bison
- flex
- git
- libtool
- make
- mingw64-x86_64-dlfcn
- mingw64-x86_64-gcc-core
- mingw64-x86_64-zlib
- pkg-config

To build, just run the `build.sh` script in your Cygwin terminal. By default, it installs
to "C:/Program Files/collectd". You can change the location by setting the INSTALL_DIR
variable:

$ export INSTALL_DIR="C:/some/other/install/directory"
$ ./build.sh

or:

$ INSTALL_DIR="C:/some/other/install/directory" ./build.sh


Crosscompiling
--------------

  To compile correctly collectd needs to be able to initialize static
  variables to NAN (Not A Number). Some C libraries, especially the GNU
  libc, have a problem with that.

  Luckily, with GCC it's possible to work around that problem: One can define
  NAN as being (0.0 / 0.0) and `isnan` as `f != f`. However, to test this
  "implementation" the configure script needs to compile and run a short
  test program. Obviously running a test program when doing a cross-
  compilation is, well, challenging.

  If you run into this problem, you can use the `--with-nan-emulation`
  configure option to force the use of this implementation. We can't promise
  that the compiled binary actually behaves as it should, but since NANs
  are likely never passed to the libm you have a good chance to be lucky.

  Likewise, collectd needs to know the layout of doubles in memory, in order
  to craft uniform network packets over different architectures. For this, it
  needs to know how to convert doubles into the memory layout used by x86. The
  configure script tries to figure this out by compiling and running a few
  small test programs. This is of course not possible when cross-compiling.
  You can use the `--with-fp-layout` option to tell the configure script which
  conversion method to assume. Valid arguments are:

    * `nothing`    (12345678 -> 12345678)
    * `endianflip` (12345678 -> 87654321)
    * `intswap`    (12345678 -> 56781234)


Contact
-------

  Please use GitHub to report bugs and submit pull requests:
  <https://github.com/collectd/collectd/>.
  See CONTRIBUTING.md for details.

  For questions, development information and basically all other concerns please
  send an email to collectd's mailing list at
  <list at collectd.org>.

  For live discussion and more personal contact visit us in IRC, we're in
  channel #collectd on freenode.


Author
------

  Florian octo Forster <octo at collectd.org>,
  Sebastian tokkee Harl <sh at tokkee.org>,
  and many other [authors](AUTHORS) and [contributors](https://github.com/collectd/collectd/graphs/contributors).
