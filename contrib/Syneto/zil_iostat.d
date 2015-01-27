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
# This is a Collectd plugin written as a dtrace script. It collects IOPS and transfer rate for ZIL.
# This script can not be directly run by collectd.
# See zil_iostat.sh for details about running it.
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
#    Exec "someone_who_can_sudo" "/path/to/zil_iostat.sh"
# </Plugin>
#
*/

fbt::zil_lwb_write_start:entry
{
	pool_name = (string) args[0]->zl_dmu_pool->dp_spa->spa_name;
	plugin_path = strjoin(strjoin($$1,"/zil_iostat-"), pool_name);
	@nused[plugin_path] = avg(args[1]->lwb_nused);
	@size[plugin_path] = avg(args[1]->lwb_sz);
	@syncops[plugin_path] = count();
}

profile:::tick-1sec,END
{
	printa("PUTVAL %s/gauge-rate-data-written N:%@d\n", @nused);
	printa("PUTVAL %s/gauge-rate-buffer-written N:%@d\n", @size);
	printa("PUTVAL %s/gauge-iops N:%@d\n", @syncops);
	trunc(@nused);
	trunc(@size);
	trunc(@syncops);
}
