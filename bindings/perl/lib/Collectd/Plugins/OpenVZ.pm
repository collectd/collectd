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

my @cpu_instances = ('user', 'nice', 'system', 'idle', 'wait', 'interrupt', 'softirq', 'steal');
my @if_instances = ('if_octets', 'if_packets', 'if_errors');
my $vzctl = '/usr/sbin/vzctl';
my $vzlist = '/usr/sbin/vzlist';

my $last_stat = {};

sub openvz_read
{
    my %v = (time => time(), interval => $interval_g);
    my (@veids, $veid, $name, $key, $val, $i, @lines, @parts, @counters);

    @veids = map { s/ //g; $_; } split(/\n/, `$vzlist -Ho veid`);

    foreach $veid (@veids)
    {
        ($name = `$vzlist -Ho name $veid`) =~ s/^\s*(.*?)\s*$/$1/;
        $name = $veid if ($name =~ /^-$/);

        $v{'host'} = $name;

        #####################################################################
        # interface

        $v{'plugin'} = 'interface';
        delete $v{'plugin_instance'};

        @lines = split(/\n/, `$vzctl exec $veid cat /proc/net/dev`);
        foreach (@lines)
        {
            next if (!/:/);                

            @parts = split(/:/);
            ($key = $parts[0]) =~ s/^\s*(.*?)\s*$/$1/;
            ($val = $parts[1]) =~ s/^\s*(.*?)\s*$/$1/;
            @counters = split(/ +/, $val);

            $v{'type_instance'} = $key;
            for ($key = 0; $key <= $#if_instances; ++$key)
            {
                $v{'type'} = $if_instances[$key];
                $v{'values'} = [ $counters[$key], $counters[$key + 8] ];
                plugin_dispatch_values(\%v);
            }
        }

        #####################################################################
        # cpu

        $v{'plugin'} = 'cpu';
        $v{'type'} = 'cpu';

        $i = 0;
        @lines = split(/\n/, `$vzctl exec $veid cat /proc/stat`);
        foreach (@lines)
        {
            next if (!/^cpu[0-9]/);

            @counters = split(/ +/);
            shift(@counters);

            # Remove once OpenVZ bug 1376 is resolved
            if (48485 == $counters[3])
            {
                $counters[3] = $last_stat->{"$veid-$i-idle"};
                $counters[4] = $last_stat->{"$veid-$i-wait"};
            }
            else
            {
                $last_stat->{"$veid-$i-idle"} = $counters[3];
                $last_stat->{"$veid-$i-wait"} = $counters[4];
            }

            $v{'plugin_instance'} = $i++;
            for ($key = 0; $key <= $#counters; ++$key)
            {
                $v{'type_instance'} = $cpu_instances[$key];
                $v{'values'} = [ $counters[$key] ];
                plugin_dispatch_values(\%v);
            }
        }

        #####################################################################
        # df

        $v{'plugin'} = 'df';
        delete $v{'plugin_instance'};
        $v{'type'} = 'df';

        $val = join(' ', map { (split)[1] } split(/\n/, `$vzctl exec $veid cat /proc/mounts`));
        @lines = split(/\n/, `$vzctl exec $veid stat -tf $val`);
        foreach (@lines)
        {
            @parts = split(/ /);
            next if (0 == $parts[7]);

            $val = substr($parts[0], 1);
            $val = 'root' if ($val =~ /^$/);
            $val =~ s#/#-#g;

            $v{'type_instance'} = $val;
            $v{'values'} = [ $parts[5] * ($parts[6] - $parts[7]), $parts[5] * $parts[7] ];
            plugin_dispatch_values(\%v);
        }

        #####################################################################
        # load

        $v{'plugin'} = 'load';
        delete $v{'plugin_instance'};
        $v{'type'} = 'load';
        delete $v{'type_instance'};

        @parts = split(/ +/, `$vzctl exec $veid cat /proc/loadavg`);
        $v{'values'} = [ $parts[0], $parts[1], $parts[2] ];
        plugin_dispatch_values(\%v);

        #####################################################################
        # processes

        my $ps_states = { 'paging' => 0, 'blocked' => 0, 'zombies' => 0, 'stopped' => 0,
            'running' => 0, 'sleeping' => 0 };
        my $state_map = { 'R' => 'running', 'S' => 'sleeping', 'D' => 'blocked',
            'Z' => 'zombies', 'T' => 'stopped', 'W' => 'paging' };

        $v{'plugin'} = 'processes';
        delete $v{'plugin_instance'};
        $v{'type'} = 'ps_state';

        @lines = map { (split)[2] } split(/\n/, `$vzctl exec $veid cat '/proc/[0-9]*/stat'`);
        foreach $key (@lines)
        {
            ++$ps_states->{$state_map->{$key}};
        }

        foreach $key (keys %{$ps_states})
        {
            $v{'type_instance'} = $key;
            $v{'values'} = [ $ps_states->{$key} ];
            plugin_dispatch_values(\%v);
        }

        #####################################################################
        # users

        $v{'plugin'} = 'users';
        delete $v{'plugin_instance'};
        $v{'type'} = 'users';
        delete $v{'type_instance'};

        @lines = split(/\n/, `$vzctl exec $veid w -h`);
        $v{'values'} = [ scalar(@lines) ];
        plugin_dispatch_values(\%v);
    }

    return 1;
}

plugin_register(TYPE_READ, 'OpenVZ', 'openvz_read');

return 1;
