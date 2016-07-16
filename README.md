This is a branch from which Sailfish releases are made. See https://github.com/rinigus/collectd/tree/sailfish/contrib/sailfish for details of the port. 

For description of collectd, see https://raw.githubusercontent.com/rinigus/collectd/sailfish/README

For Sailfish port related issues, file them under this branch (branch sailfish). 

## Sailfish-specific features

* The following plugins were developed for Sailfish and other mobile devices
  * CPU sleep: showhs how much the device is in sleep
  * statefs_battery: battery charge, current, energy, power consumption, temperature, time to low/full, voltage
* In the default configuration:
   * Only active network interfaces are reported
   * Only physical disk traffic is reported

## Updates from earlier versions

When upgrading from previous version, please note:

* collectd will be stopped and disabled on boot. You would have to start and reenable it (contributions welcome to make upgrade a bit smarter).
* if you made any changes in /etc/collectd.conf the configuration file will not be overwritten. The default the configuration file will be then with the ending .rpmsave. Compare two configuration files and enable new plugins, if you wish.
* if you wish to delete some recorded data types: stop collectd; go to /home/nemo/.local/share/collectd/Jolla and delete the sets; start collectd.


### For early adopters: 

in the early versions, all disk partitions and all network interface traffic was reported. To reduce the number of irrelevant stats, stop collectd, go to /home/nemo/.local/share/collectd/Jolla and delete irrelevant subdirectories. With the default configuration active (check if /etc/collectd.conf.rpmsave exists and if its different from the current one), you should be all set.

## Development

When developing new plugin/feature, do it against collectd master and later merge here. Alternatively, you could develop against this branch and make your commits so that Sailfish configuration changes are done in separate commits. That way we can contribute back to upstream by either full pull request or cherry-picking commits. 
