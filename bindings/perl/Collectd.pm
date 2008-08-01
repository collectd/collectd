# collectd - Collectd.pm
# Copyright (C) 2007, 2008  Sebastian Harl
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; only version 2 of the License is applicable.
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
#   Sebastian Harl <sh at tokkee.org>

package Collectd;

use strict;
use warnings;

use Config;

use threads;
use threads::shared;

BEGIN {
	if (! $Config{'useithreads'}) {
		die "Perl does not support ithreads!";
	}
}

require Exporter;

our @ISA = qw( Exporter );

our %EXPORT_TAGS = (
	'plugin' => [ qw(
			plugin_register
			plugin_unregister
			plugin_dispatch_values
			plugin_flush
			plugin_flush_one
			plugin_flush_all
			plugin_dispatch_notification
			plugin_log
	) ],
	'types' => [ qw(
			TYPE_INIT
			TYPE_READ
			TYPE_WRITE
			TYPE_SHUTDOWN
			TYPE_LOG
			TYPE_NOTIF
			TYPE_FLUSH
			TYPE_DATASET
	) ],
	'ds_types' => [ qw(
			DS_TYPE_COUNTER
			DS_TYPE_GAUGE
	) ],
	'log' => [ qw(
			ERROR
			WARNING
			NOTICE
			INFO
			DEBUG
			LOG_ERR
			LOG_WARNING
			LOG_NOTICE
			LOG_INFO
			LOG_DEBUG
	) ],
	'notif' => [ qw(
			NOTIF_FAILURE
			NOTIF_WARNING
			NOTIF_OKAY
	) ],
	'globals' => [ qw(
			$hostname_g
			$interval_g
	) ],
);

{
	my %seen;
	push @{$EXPORT_TAGS{'all'}}, grep {! $seen{$_}++ } @{$EXPORT_TAGS{$_}}
		foreach keys %EXPORT_TAGS;
}

# global variables
our $hostname_g;
our $interval_g;

Exporter::export_ok_tags ('all');

my @plugins : shared = ();

my %types = (
	TYPE_INIT,     "init",
	TYPE_READ,     "read",
	TYPE_WRITE,    "write",
	TYPE_SHUTDOWN, "shutdown",
	TYPE_LOG,      "log",
	TYPE_NOTIF,    "notify",
	TYPE_FLUSH,    "flush"
);

foreach my $type (keys %types) {
	$plugins[$type] = &share ({});
}

sub _log {
	my $caller = shift;
	my $lvl    = shift;
	my $msg    = shift;

	if ("Collectd" eq $caller) {
		$msg = "perl: $msg";
	}
	return plugin_log ($lvl, $msg);
}

sub ERROR   { _log (scalar caller, LOG_ERR,     shift); }
sub WARNING { _log (scalar caller, LOG_WARNING, shift); }
sub NOTICE  { _log (scalar caller, LOG_NOTICE,  shift); }
sub INFO    { _log (scalar caller, LOG_INFO,    shift); }
sub DEBUG   { _log (scalar caller, LOG_DEBUG,   shift); }

sub plugin_call_all {
	my $type = shift;

	my %plugins;

	our $cb_name = undef;

	if (! defined $type) {
		return;
	}

	if (TYPE_LOG != $type) {
		DEBUG ("Collectd::plugin_call: type = \"$type\", args=\"@_\"");
	}

	if (! defined $plugins[$type]) {
		ERROR ("Collectd::plugin_call: unknown type \"$type\"");
		return;
	}

	{
		lock %{$plugins[$type]};
		%plugins = %{$plugins[$type]};
	}

	foreach my $plugin (keys %plugins) {
		my $p = $plugins{$plugin};

		my $status = 0;

		if ($p->{'wait_left'} > 0) {
			$p->{'wait_left'} -= $interval_g;
		}

		next if ($p->{'wait_left'} > 0);

		$cb_name = $p->{'cb_name'};
		$status = call_by_name (@_);

		if (! $status) {
			my $err = undef;

			if ($@) {
				$err = $@;
			}
			else {
				$err = "callback returned false";
			}

			if (TYPE_LOG != $type) {
				ERROR ("Execution of callback \"$cb_name\" failed: $err");
			}

			$status = 0;
		}

		if ($status) {
			$p->{'wait_left'} = 0;
			$p->{'wait_time'} = $interval_g;
		}
		elsif (TYPE_READ == $type) {
			if ($p->{'wait_time'} < $interval_g) {
				$p->{'wait_time'} = $interval_g;
			}

			$p->{'wait_left'} = $p->{'wait_time'};
			$p->{'wait_time'} *= 2;

			if ($p->{'wait_time'} > 86400) {
				$p->{'wait_time'} = 86400;
			}

			WARNING ("${plugin}->read() failed with status $status. "
				. "Will suspend it for $p->{'wait_left'} seconds.");
		}
		elsif (TYPE_INIT == $type) {
			ERROR ("${plugin}->init() failed with status $status. "
				. "Plugin will be disabled.");

			foreach my $type (keys %types) {
				plugin_unregister ($type, $plugin);
			}
		}
		elsif (TYPE_LOG != $type) {
			WARNING ("${plugin}->$types{$type}() failed with status $status.");
		}
	}
	return 1;
}

# Collectd::plugin_register (type, name, data).
#
# type:
#   init, read, write, shutdown, data set
#
# name:
#   name of the plugin
#
# data:
#   reference to the plugin's subroutine that does the work or the data set
#   definition
sub plugin_register {
	my $type = shift;
	my $name = shift;
	my $data = shift;

	DEBUG ("Collectd::plugin_register: "
		. "type = \"$type\", name = \"$name\", data = \"$data\"");

	if (! ((defined $type) && (defined $name) && (defined $data))) {
		ERROR ("Usage: Collectd::plugin_register (type, name, data)");
		return;
	}

	if ((! defined $plugins[$type]) && (TYPE_DATASET != $type)) {
		ERROR ("Collectd::plugin_register: Invalid type \"$type\"");
		return;
	}

	if ((TYPE_DATASET == $type) && ("ARRAY" eq ref $data)) {
		return plugin_register_data_set ($name, $data);
	}
	elsif ((TYPE_DATASET != $type) && (! ref $data)) {
		my $pkg = scalar caller;

		my %p : shared;

		if ($data !~ m/^$pkg\:\:/) {
			$data = $pkg . "::" . $data;
		}

		%p = (
			wait_time => $interval_g,
			wait_left => 0,
			cb_name   => $data,
		);

		lock %{$plugins[$type]};
		$plugins[$type]->{$name} = \%p;
	}
	else {
		ERROR ("Collectd::plugin_register: Invalid data.");
		return;
	}
	return 1;
}

sub plugin_unregister {
	my $type = shift;
	my $name = shift;

	DEBUG ("Collectd::plugin_unregister: type = \"$type\", name = \"$name\"");

	if (! ((defined $type) && (defined $name))) {
		ERROR ("Usage: Collectd::plugin_unregister (type, name)");
		return;
	}

	if (TYPE_DATASET == $type) {
		return plugin_unregister_data_set ($name);
	}
	elsif (defined $plugins[$type]) {
		lock %{$plugins[$type]};
		delete $plugins[$type]->{$name};
	}
	else {
		ERROR ("Collectd::plugin_unregister: Invalid type.");
		return;
	}
}

sub plugin_flush {
	my %args = @_;

	my $timeout = -1;

	DEBUG ("Collectd::plugin_flush:"
		. (defined ($args{'timeout'}) ? " timeout = $args{'timeout'}" : "")
		. (defined ($args{'plugins'}) ? " plugins = $args{'plugins'}" : ""));

	if (defined ($args{'timeout'}) && ($args{'timeout'} > 0)) {
		$timeout = $args{'timeout'};
	}

	if (! defined $args{'plugins'}) {
		plugin_flush_all ($timeout);
	}
	else {
		if ("ARRAY" eq ref ($args{'plugins'})) {
			foreach my $plugin (@{$args{'plugins'}}) {
				plugin_flush_one ($timeout, $plugin);
			}
		}
		else {
			plugin_flush_one ($timeout, $args{'plugins'});
		}
	}
}

1;

# vim: set sw=4 ts=4 tw=78 noexpandtab :

