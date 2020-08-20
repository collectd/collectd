#!/usr/local/bin/ruby
#
#
# This file is part of the Syneto/CollectdPlugins package.
#
# (c) Syneto (office@syneto.net)
#
# For the full copyright and license information, please view the LICENSE
# file that was distributed with this source code.
#
# This is a Collectd plugin that calls iostat infinately at the interval specified in collectd.conf.
# It requires the exec Collectd plugin so it can be executed by Collectd.
# Only one process is loaded, at the begining, when collectd starts.
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
#    Exec "nobody" "/path/to/iostat_illumos.sh"
# </Plugin>
#

trap("TERM") { killChildAndExit }

def killChildAndExit
	puts "Killing child process: " + @iostatProcess.pid.to_s
	Process.kill("KILL", @iostatProcess.pid)
	abort("Caught TERM. Exiting...")
end

def sendToCollectd(device, type, metric, value)
	puts "PUTVAL " + HOSTNAME.chomp + "/iostat-" + type + "-" + device + "/gauge-" + metric + " interval=" + INTERVAL.to_s + " N:" + value.to_s
end

def validValue(value)
	return true if value.to_i >= 0 and value.to_i < VALUE_THRESHOLD
	return false
end

def getPools
	zpoolListProcess = IO.popen("zpool list -H -o name")
	pools = zpoolListProcess.readlines
	zpoolListProcess.close
	return pools
end

HOSTNAME = ENV['COLLECTD_HOSTNAME'] ? ENV['COLLECTD_HOSTNAME'] : `hostname`.gsub(/\./, "_")
INTERVAL = ENV['COLLECTD_INTERVAL'] ? ENV['COLLECTD_INTERVAL'].to_i : 10
VALUE_THRESHOLD = 7200000

@iostatProcess = IO.popen("iostat -xn " + INTERVAL.to_s)
while line = @iostatProcess.gets do
	pools = getPools
	if  (line =~ /device/)
		#puts "Debug: Skipped line:" + line
	else
		iops_read, iops_write, kb_read, kb_write, wait, actv, wsvc_t, asvc_t, perc_wait, perc_busy, device = line.split
		next if device.nil?
		type = pools.include?(device + "\n") ? "pool" : "disk"
		sendToCollectd device, type, "iops_read-rs", iops_read if validValue iops_read
		sendToCollectd device, type, "iops_write-ws", iops_write if validValue iops_write
		sendToCollectd device, type, "bandwidth_read-krs", kb_read.to_i*1024
		sendToCollectd device, type, "bandwidth_write-kws", kb_write.to_i*1024
		sendToCollectd device, type, "wait_transactions-wait", wait if validValue wait
		sendToCollectd device, type, "active_transactions-actv", actv if validValue actv
		sendToCollectd device, type, "wait_avg_service_time-wsvc_t", wsvc_t if validValue wsvc_t
		sendToCollectd device, type, "active_avg_service_time-asvc_t", asvc_t if validValue asvc_t
		sendToCollectd device, type, "wait_percent-w", perc_wait if validValue perc_wait
		sendToCollectd device, type, "active_percent-b", perc_busy if validValue perc_busy
	end
end
@iostatProcess.close

