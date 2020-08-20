#!/usr/sbin/dtrace -s
#pragma D option quiet
/*
#
# This file is part of the Syneto/CollectdPlugins package.
#
# (c) Syneto (office@syneto.net)
#
# For the full copyright and license information, please view the LICENSE
# file that was distributed with this source code.
#
# This is a Collectd plugin written as a dtrace script. It collects IOPS, bandwidth, and average blocksize information
# about all datasets on the system. It will, however, output only a shortened version of the path for each dataset.
# The output format is: <pool_name>_<parent_folder>_<dataset>
# This script can not be directly run by collectd.
# See zfs_iostat.sh for details about running it.
# It requires the exec Collectd plugin so it can be executed by Collectd.
# Only one process is loaded, at the beginning, when collectd starts.
# Collectd will read the process' output periodically.
#
# For information about configuring exec plugins in Collectd, see
# http://collectd.org/documentation/manpages/collectd.conf.5.shtml#plugin_exec
#
# For information about Collectd's plain text protocol, see
# https://collectd.org/wiki/index.php/Plain_text_protocol
#
# The plugin must not run as root. "nobody" is a good candidate but feel free to use your favorit user.
#
# <Plugin exec>
#    Exec "someone_who_can_sudo" "/path/to/zfs_iostat.sh"
# </Plugin>
#
*/

dmu_buf_hold_array_by_dnode:entry
/args[0]->dn_objset->os_dsl_dataset && args[3]/ /* Reads */
{
        this->ds = stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_myname);
        this->parent = (args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent != NULL) ? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_myname) : "";
        this->parent2 = (this->parent != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent != NULL)
        	? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_myname) : "";
        this->parent3 = (this->parent2 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent != NULL)
        	? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_myname) : "";
        this->parent4 = (this->parent3 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent != NULL)
			? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_myname) : "";
		this->parent5 = (this->parent4 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent != NULL)
			? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_myname) : "";
		this->parent6 = (this->parent5 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent != NULL)
        	? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_myname) : "";
        this->parent7 = (this->parent6 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent != NULL)
            ? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_myname) : "";
		this->parent8 = (this->parent7 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent != NULL)
			? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_myname) : "";
		this->parent9 = (this->parent8 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent != NULL)
        	? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_myname) : "";
        this->parent10 = (this->parent9 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent != NULL)
            ? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_myname) : "";
		this->path = (this->parent != "") ? strjoin(strjoin(this->parent,"#"), this->ds) : this->ds;
		this->path = (this->parent2 != "") ? strjoin(strjoin(this->parent2,"#"), this->path) : this->path;
        this->path = (this->parent3 != "") ? strjoin(strjoin(this->parent3,"#"), this->path) : this->path;
        this->path = (this->parent4 != "") ? strjoin(strjoin(this->parent4,"#"), this->path) : this->path;
        this->path = (this->parent5 != "") ? strjoin(strjoin(this->parent5,"#"), this->path) : this->path;
        this->path = (this->parent6 != "") ? strjoin(strjoin(this->parent6,"#"), this->path) : this->path;
        this->path = (this->parent7 != "") ? strjoin(strjoin(this->parent7,"#"), this->path) : this->path;
        this->path = (this->parent8 != "") ? strjoin(strjoin(this->parent8,"#"), this->path) : this->path;
        this->path = (this->parent9 != "") ? strjoin(strjoin(this->parent9,"#"), this->path) : this->path;
        this->path = (this->parent10 != "") ? strjoin(strjoin(this->parent10,"#"), this->path) : this->path;
        this->collectd_path = strjoin($$1, strjoin("/zfs_iostat-",this->path));

        @ior[this->collectd_path] = count();
        @tpr[this->collectd_path] = sum(args[2]);
        @bsr[this->collectd_path] = avg(args[2]);
}

dmu_buf_hold_array_by_dnode:entry
/args[0]->dn_objset->os_dsl_dataset && !args[3]/ /* Writes */
{
        this->ds = stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_myname);
		this->parent = (args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent != NULL) ? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_myname) : "";
		this->parent2 = (this->parent != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent != NULL)
			? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_myname) : "";
		this->parent3 = (this->parent2 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent != NULL)
			? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_myname) : "";
		this->parent4 = (this->parent3 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent != NULL)
			? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_myname) : "";
		this->parent5 = (this->parent4 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent != NULL)
			? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_myname) : "";
		this->parent6 = (this->parent5 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent != NULL)
			? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_myname) : "";
		this->parent7 = (this->parent6 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent != NULL)
			? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_myname) : "";
		this->parent8 = (this->parent7 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent != NULL)
			? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_myname) : "";
		this->parent9 = (this->parent8 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent != NULL)
			? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_myname) : "";
		this->parent10 = (this->parent9 != "" && args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent != NULL)
			? stringof(args[0]->dn_objset->os_dsl_dataset->ds_dir->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_parent->dd_myname) : "";
		this->path = (this->parent != "") ? strjoin(strjoin(this->parent,"#"), this->ds) : this->ds;
		this->path = (this->parent2 != "") ? strjoin(strjoin(this->parent2,"#"), this->path) : this->path;
		this->path = (this->parent3 != "") ? strjoin(strjoin(this->parent3,"#"), this->path) : this->path;
		this->path = (this->parent4 != "") ? strjoin(strjoin(this->parent4,"#"), this->path) : this->path;
		this->path = (this->parent5 != "") ? strjoin(strjoin(this->parent5,"#"), this->path) : this->path;
		this->path = (this->parent6 != "") ? strjoin(strjoin(this->parent6,"#"), this->path) : this->path;
		this->path = (this->parent7 != "") ? strjoin(strjoin(this->parent7,"#"), this->path) : this->path;
		this->path = (this->parent8 != "") ? strjoin(strjoin(this->parent8,"#"), this->path) : this->path;
		this->path = (this->parent9 != "") ? strjoin(strjoin(this->parent9,"#"), this->path) : this->path;
		this->path = (this->parent10 != "") ? strjoin(strjoin(this->parent10,"#"), this->path) : this->path;
		this->collectd_path = strjoin($$1, strjoin("/zfs_iostat-",this->path));

        @iow[this->collectd_path] = count();
        @tpw[this->collectd_path] = sum(args[2]);
        @bsw[this->collectd_path] = avg(args[2]);
}

tick-1sec,END
{
        printa("PUTVAL %s/gauge-iops-read N:%@d\n", @ior);
        printa("PUTVAL %s/gauge-iops-write N:%@d\n", @iow);
        printa("PUTVAL %s/gauge-bandwidth-read N:%@d\n", @tpr);
        printa("PUTVAL %s/gauge-bandwidth-write N:%@d\n", @tpw);
        printa("PUTVAL %s/gauge-avgblksize-read N:%@d\n", @bsr);
        printa("PUTVAL %s/gauge-avgblksize-write N:%@d\n", @bsw);
        trunc(@ior); trunc(@tpr); trunc(@iow); trunc(@tpw); trunc(@bsr); trunc(@bsw);
}