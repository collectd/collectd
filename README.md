This is a branch from which Sailfish releases are made. See https://github.com/rinigus/collectd/tree/sailfish/contrib/sailfish for details of the port. 

For description of collectd, see https://raw.githubusercontent.com/rinigus/collectd/sailfish/README

For Sailfish port related issues, file them under this branch (branch sailfish). For up-to-date manual page describing available configuration options, see https://github.com/rinigus/collectd/blob/sailfish/src/collectd.conf.pod .

## Sailfish-specific features

* The following plugins were developed for Sailfish and other mobile devices
  * CPU sleep: showhs how much the device is in sleep
  * Used CPU frequencies and idle states
  * Device suspend attempts (successful and failed)
  * statefs_battery: battery charge, current, energy, power consumption, temperature, time to low/full, voltage
  * There are several new plugins that cover aspects of cellular, WiFi, and Bluetooth radios performance: whether active, signal strength for cellular and used internet radio. For cellular, signal strength is recorded for each used wireless mobile telecommunications technology separately.
  * radio: follows WiFi, Bluetooth, and, if supported by kernel, other radio switches (hardware and software) to determine if the radio is active. 
  
* In the default configuration:
   * Only active network interfaces are reported
   * Only physical disk traffic is reported

## Updates from earlier versions

When upgrading from previous version, please note:

* collectd will be stopped and disabled on boot. You would have to start and reenable it (contributions welcome to make upgrade a bit smarter).
* if you made any changes in /etc/collectd.conf the configuration file will not be overwritten. The default configuration file will be then with the ending .rpmnew. Compare two configuration files and enable new plugins, if you wish.
* if you wish to delete some recorded data types: stop collectd; go to /home/nemo/.local/share/collectd/Jolla and delete the sets; start collectd.


### Upgrade notes for users of versions 2016.07.17-6 and earlier

In short, for default configuration users, please delete the old datasets and start with the new default configuration (will delete earlier data). For that, stop collectd and remove /home/nemo/.local/share/collectd ( in terminal: rm -rf /home/nemo/.local/share/collectd ) and start collectd again. While the data will be lost from earlier recordings, new databases will be initialized allowing you to record data for 1 year.

If you modified RRD plugin section in your configuration, then please check if RRDs are actually covering the expected period.

Explanation: While the default settings were used to setup databases, at least on Nexus 4 SFOS port, the datasets sections that were supposed to cover long-term statistics were initialized wrong. As a result, while space was used to cover a year of stats, the presented statistics were limited to 1 week. For users of even earlier versions, there are also logging of inactive internet interfaces and disk partitions traffic. In the later versions, only active internet interfaces and physical disk traffic are logged.

With the default configuration active (check if /etc/collectd.conf.rpmsave or rpmnew exists and if its different from the current one), you should be all set.

## Development

When developing new plugin/feature, do it against collectd master and later merge here. Alternatively, you could develop against this branch and make your commits so that Sailfish configuration changes are done in separate commits. That way we can contribute back to upstream by either full pull request or cherry-picking commits. 
