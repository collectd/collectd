#
# collectd - OpenVZ collectd plugin
# Copyright (C) 2009  Jonathan Kolb
#
# This program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; either version 2 of the License, or (at your option) any later
# version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
#
# Author:
#   Jonathan Kolb <jon at b0g.us>
#

package Collectd::Plugins::OpenVZ;

use strict;
use warnings;

use Collectd qw( :all );

my $vzctl = '/usr/sbin/vzctl';
my $vzlist = '/usr/sbin/vzlist';

# Since OpenVZ is container based, all guests see all the host's CPUs,
# and would report the same data. So we disable CPU by default.
my $enable_interface = 1;
my $enable_cpu       = 0;
my $enable_df        = 1;
my $enable_load      = 1;
my $enable_processes = 1;
my $enable_users     = 1;

# We probably don't care about loopback transfer
my @ignored_interfaces = ( "lo" );

sub interface_read {
    my ($veid, $name) = @_;
    my @rx_fields = qw(if_octets if_packets if_errors drop fifo frame compressed multicast);
    my @tx_fields = qw(if_octets if_packets if_errors drop fifo frame compressed);
    my %v = _build_report_hash($name);

    my @lines = `$vzctl exec $veid cat /proc/net/dev`;

    for my $line (@lines) {
        # skip explanatory text
        next if $line !~ /:/;

        $line =~ s/^\s+|\s+$//g;

        my ($iface, %rx, %tx);

        # read /proc/net/dev fields
        ($iface, @rx{@rx_fields}, @tx{@tx_fields}) = split /[: ]+/, $line;

        # Skip this interface if it is in the ignored list
        next if grep { $iface eq $_ } @ignored_interfaces;

        for my $instance (qw(if_octets if_packets if_errors)) {
            plugin_dispatch_values({
                'plugin'          => 'interface',
                'plugin_instance' => $iface,
                'type'            => $instance,
                'values'          => [ $rx{$instance}, $tx{$instance} ],
                %v,
            });
        }
    }
}

sub cpu_read {
    my $veid = shift;
    my $name = shift;
    my ($key, $val, $i, @lines, @counters);
    my @cpu_instances = ('user', 'nice', 'system', 'idle', 'wait', 'interrupt', 'softirq', 'steal');
    my $last_stat = {};
    my %v = _build_report_hash($name);

    $v{'plugin'} = 'cpu';
    $v{'type'} = 'cpu';

    $i = 0;
    @lines = split(/\n/, `$vzctl exec $veid cat /proc/stat`);
    foreach (@lines) {
        next if (!/^cpu[0-9]/);

        @counters = split(/ +/);
        shift(@counters);

        # Remove once OpenVZ bug 1376 is resolved
        if (48485 == $counters[3]) {
            $counters[3] = $last_stat->{"$veid-$i-idle"};
            $counters[4] = $last_stat->{"$veid-$i-wait"};
        }
        else {
            $last_stat->{"$veid-$i-idle"} = $counters[3];
            $last_stat->{"$veid-$i-wait"} = $counters[4];
        }

        $v{'plugin_instance'} = $i++;
        for ($key = 0; $key <= $#counters; ++$key) {
            $v{'type_instance'} = $cpu_instances[$key];
            $v{'values'} = [ $counters[$key] ];
            plugin_dispatch_values(\%v);
    }
}
}

sub df_read {
    my $veid = shift;
    my $name = shift;
    my ($key, $val, @lines, @parts);
    my %v = _build_report_hash($name);

    $v{'plugin'} = 'df';
    delete $v{'plugin_instance'};
    $v{'type'} = 'df';

    $val = join(' ', map { (split)[1] } split(/\n/, `$vzctl exec $veid cat /proc/mounts`));
    @lines = split(/\n/, `$vzctl exec $veid stat -tf $val`);
    foreach (@lines) {
        @parts = split(/ /);
        next if (0 == $parts[7]);

        $val = substr($parts[0], 1);
        $val = 'root' if ($val =~ /^$/);
        $val =~ s#/#-#g;

        $v{'type_instance'} = $val;
        $v{'values'} = [ $parts[5] * ($parts[6] - $parts[7]), $parts[5] * $parts[7] ];
        plugin_dispatch_values(\%v);
}
}

sub load_read {
    my $veid = shift;
    my $name = shift;
    my ($key, $val, @lines, @parts);
    my %v = _build_report_hash($name);

    $v{'plugin'} = 'load';
    delete $v{'plugin_instance'};
    $v{'type'} = 'load';
    delete $v{'type_instance'};

    @parts = split(/ +/, `$vzctl exec $veid cat /proc/loadavg`);
    $v{'values'} = [ $parts[0], $parts[1], $parts[2] ];
    plugin_dispatch_values(\%v);
}

sub processes_read {
    my $veid = shift;
    my $name = shift;
    my ($key, $val, @lines);
    my %v = _build_report_hash($name);

    my $ps_states = { 'paging' => 0, 'blocked' => 0, 'zombies' => 0, 'stopped' => 0,
        'running' => 0, 'sleeping' => 0 };
    my $state_map = { 'R' => 'running', 'S' => 'sleeping', 'D' => 'blocked',
        'Z' => 'zombies', 'T' => 'stopped', 'W' => 'paging' };

    $v{'plugin'} = 'processes';
    delete $v{'plugin_instance'};
    $v{'type'} = 'ps_state';

    @lines = map { (split)[2] } split(/\n/, `$vzctl exec $veid cat '/proc/[0-9]*/stat'`);
    foreach $key (@lines) {
        ++$ps_states->{$state_map->{$key}};
    }

    foreach $key (keys %{$ps_states}) {
        $v{'type_instance'} = $key;
        $v{'values'} = [ $ps_states->{$key} ];
        plugin_dispatch_values(\%v);
}
}

sub users_read {
    my $veid = shift;
    my $name = shift;
    my ($key, $val, @lines);
    my %v = _build_report_hash($name);

    $v{'plugin'} = 'users';
    delete $v{'plugin_instance'};
    $v{'type'} = 'users';
    delete $v{'type_instance'};

    @lines = split(/\n/, `$vzctl exec $veid w -h`);
    $v{'values'} = [ scalar(@lines) ];
    plugin_dispatch_values(\%v);
}

sub _build_report_hash {
    my $name = shift;
    return (time => time(), interval => plugin_get_interval(), host => $name);
}

sub openvz_read {
    my (@veids, $veid, $name);

    @veids = map { s/ //g; $_; } split(/\n/, `$vzlist -Ho veid`);

    foreach $veid (@veids) {
        ($name = `$vzlist -Ho name $veid`) =~ s/^\s*(.*?)\s*$/$1/;
        ($name = `$vzlist -Ho hostname $veid`) =~ s/^\s*(.*?)\s*$/$1/ if($name =~ /^-$/);
        $name = $veid if ($name =~ /^-$/);

        if($enable_interface) {
            interface_read($veid, $name);
        }

        if($enable_cpu) {
            cpu_read($veid, $name);
        }

        if($enable_df) {
            df_read($veid, $name);
        }

        if($enable_load) {
            load_read($veid, $name);
        }

        if($enable_processes) {
            processes_read($veid, $name);
        }

        if($enable_users) {
            users_read($veid, $name);
        }

        return 1;
    }
}

plugin_register(TYPE_READ, 'OpenVZ', 'openvz_read');

return 1;
