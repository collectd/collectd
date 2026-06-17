# Introduction

This is a [collectd](http://www.collectd.org/) plugin that
collects statistics from a vCenter.

# Requirements

* Collectd 4.9 or later (for the Python plugin)
* Python 2.4 or later
* Pyvmomi 5.5 or later (https://github.com/vmware/pyvmomi)

# Configuration

The plugin has some configuration options. This is done by passing
parameters via the <Module> config section in your Collectd config. The
following parameters are recognized:

* User - the username for authentication
* Password - the password for authentication
* Host - hostname or IP address of the vCenter server
* Port - the port of the vCenter server defaults to 443

The following is an example Collectd configuration for this plugin:

    <LoadPlugin python>
        Globals true
    </LoadPlugin>
    <Plugin python>
        # vcenter.py is at /usr/lib/collectd/python
        ModulePath "/usr/lib/collectd/python"
        LogTraces true
        Import vcenter
        <Module vcenter>
            User "username"
            Host "hostname"
            Password "password"
        </Module>
    </Plugin>

The data-sets in vcenter_types.db need to be added to the types.db file
given by the collectd.conf TypesDB directive. See the types.db(5) man
page for more information.
