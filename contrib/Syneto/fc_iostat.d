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
# This is a Collectd plugin written as a dtrace script. It collects IOPS and transfer rate for fiber channel.
# The output format is: remote_initiator_name
# This script can not be directly run by collectd.
# See fc_iostat.sh for details about running it.
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
# The plugin must not run as root. "nobody" is a good candidate but feel free to use your favorite user.
#
# <Plugin exec>
#    Exec "someone_who_can_sudo" "/path/to/fc_iostat.sh"
# </Plugin>
#
*/

fc:::xfer-start
{
    this->ci_remote_path = strjoin(strjoin($$1,"/fc_iostat-"), args[0]->ci_remote);

	@transfer_rate[this->ci_remote_path] = avg(args[4]->fcx_len);
    @iops[this->ci_remote_path] = count();
}

tick-1sec,END
{
	printa("PUTVAL %s/gauge-transfer-rate N:%@d\n", @transfer_rate);
	printa("PUTVAL %s/gauge-iops N:%@d\n", @iops);
	trunc(@transfer_rate);
	trunc(@iops);
}