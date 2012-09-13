# collectd - Collectd.pm
# Copyright (C) 2007-2009  Sebastian Harl
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
			plugin_write
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
			TYPE_CONFIG
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
	'filter_chain' => [ qw(
			fc_register
			FC_MATCH_NO_MATCH
			FC_MATCH_MATCHES
			FC_TARGET_CONTINUE
			FC_TARGET_STOP
			FC_TARGET_RETURN
	) ],
	'fc_types' => [ qw(
			FC_MATCH
			FC_TARGET
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
my @fc_plugins : shared = ();
my %cf_callbacks : shared = ();

my %types = (
	TYPE_CONFIG,   "config",
	TYPE_INIT,     "init",
	TYPE_READ,     "read",
	TYPE_WRITE,    "write",
	TYPE_SHUTDOWN, "shutdown",
	TYPE_LOG,      "log",
	TYPE_NOTIF,    "notify",
	TYPE_FLUSH,    "flush"
);

my %fc_types = (
	FC_MATCH,  "match",
	FC_TARGET, "target"
);

my %fc_exec_names = (
	FC_MATCH,  "match",
	FC_TARGET, "invoke"
);

my %fc_cb_types = (
	FC_CB_EXEC, "exec",
	FC_CB_CREATE, "create",
	FC_CB_DESTROY, "destroy"
);

foreach my $type (keys %types) {
	$plugins[$type] = &share ({});
}

foreach my $type (keys %fc_types) {
	$fc_plugins[$type] = &share ({});
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
		DEBUG ("Collectd::plugin_call: type = \"$type\" ("
			. $types{$type} . "), args=\""
			. join(', ', map { defined($_) ? $_ : '<undef>' } @_) . "\"");
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
		. "type = \"$type\" (" . $types{$type}
		. "), name = \"$name\", data = \"$data\"");

	if (! ((defined $type) && (defined $name) && (defined $data))) {
		ERROR ("Usage: Collectd::plugin_register (type, name, data)");
		return;
	}

	if ((! defined $plugins[$type]) && (TYPE_DATASET != $type)
			&& (TYPE_CONFIG != $type)) {
		ERROR ("Collectd::plugin_register: Invalid type \"$type\"");
		return;
	}

	if ((TYPE_DATASET == $type) && ("ARRAY" eq ref $data)) {
		return plugin_register_data_set ($name, $data);
	}
	elsif ((TYPE_CONFIG == $type) && (! ref $data)) {
		my $pkg = scalar caller;

		if ($data !~ m/^$pkg\:\:/) {
			$data = $pkg . "::" . $data;
		}

		lock %cf_callbacks;
		$cf_callbacks{$name} = $data;
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

	DEBUG ("Collectd::plugin_unregister: type = \"$type\" ("
		. $types{$type} . "), name = \"$name\"");

	if (! ((defined $type) && (defined $name))) {
		ERROR ("Usage: Collectd::plugin_unregister (type, name)");
		return;
	}

	if (TYPE_DATASET == $type) {
		return plugin_unregister_data_set ($name);
	}
	elsif (TYPE_CONFIG == $type) {
		lock %cf_callbacks;
		delete $cf_callbacks{$name};
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

sub plugin_write {
	my %args = @_;

	my @plugins    = ();
	my @datasets   = ();
	my @valuelists = ();

	if (! defined $args{'valuelists'}) {
		ERROR ("Collectd::plugin_write: Missing 'valuelists' argument.");
		return;
	}

	DEBUG ("Collectd::plugin_write:"
		. (defined ($args{'plugins'}) ? " plugins = $args{'plugins'}" : "")
		. (defined ($args{'datasets'}) ? " datasets = $args{'datasets'}" : "")
		. " valueslists = $args{'valuelists'}");

	if (defined ($args{'plugins'})) {
		if ("ARRAY" eq ref ($args{'plugins'})) {
			@plugins = @{$args{'plugins'}};
		}
		else {
			@plugins = ($args{'plugins'});
		}
	}
	else {
		@plugins = (undef);
	}

	if ("ARRAY" eq ref ($args{'valuelists'})) {
		@valuelists = @{$args{'valuelists'}};
	}
	else {
		@valuelists = ($args{'valuelists'});
	}

	if (defined ($args{'datasets'})) {
		if ("ARRAY" eq ref ($args{'datasets'})) {
			@datasets = @{$args{'datasets'}};
		}
		else {
			@datasets = ($args{'datasets'});
		}
	}
	else {
		@datasets = (undef) x scalar (@valuelists);
	}

	if ($#datasets != $#valuelists) {
		ERROR ("Collectd::plugin_write: Invalid number of datasets.");
		return;
	}

	foreach my $plugin (@plugins) {
		for (my $i = 0; $i < scalar (@valuelists); ++$i) {
			_plugin_write ($plugin, $datasets[$i], $valuelists[$i]);
		}
	}
}

sub plugin_flush {
	my %args = @_;

	my $timeout = -1;
	my @plugins = ();
	my @ids     = ();

	DEBUG ("Collectd::plugin_flush:"
		. (defined ($args{'timeout'}) ? " timeout = $args{'timeout'}" : "")
		. (defined ($args{'plugins'}) ? " plugins = $args{'plugins'}" : "")
		. (defined ($args{'identifiers'})
			? " identifiers = $args{'identifiers'}" : ""));

	if (defined ($args{'timeout'}) && ($args{'timeout'} > 0)) {
		$timeout = $args{'timeout'};
	}

	if (defined ($args{'plugins'})) {
		if ("ARRAY" eq ref ($args{'plugins'})) {
			@plugins = @{$args{'plugins'}};
		}
		else {
			@plugins = ($args{'plugins'});
		}
	}
	else {
		@plugins = (undef);
	}

	if (defined ($args{'identifiers'})) {
		if ("ARRAY" eq ref ($args{'identifiers'})) {
			@ids = @{$args{'identifiers'}};
		}
		else {
			@ids = ($args{'identifiers'});
		}
	}
	else {
		@ids = (undef);
	}

	foreach my $plugin (@plugins) {
		foreach my $id (@ids) {
			_plugin_flush($plugin, $timeout, $id);
		}
	}
}

sub fc_call {
	my $type    = shift;
	my $name    = shift;
	my $cb_type = shift;

	my %proc;

	our $cb_name = undef;
	my  $status;

	if (! ((defined $type) && (defined $name) && (defined $cb_type))) {
		ERROR ("Usage: Collectd::fc_call(type, name, cb_type, ...)");
		return;
	}

	if (! defined $fc_plugins[$type]) {
		ERROR ("Collectd::fc_call: Invalid type \"$type\"");
		return;
	}

	if (! defined $fc_plugins[$type]->{$name}) {
		ERROR ("Collectd::fc_call: Unknown "
			. ($type == FC_MATCH ? "match" : "target")
			. " \"$name\"");
		return;
	}

	DEBUG ("Collectd::fc_call: "
		. "type = \"$type\" (" . $fc_types{$type}
		. "), name = \"$name\", cb_type = \"$cb_type\" ("
		. $fc_cb_types{$cb_type} . ")");

	{
		lock %{$fc_plugins[$type]};
		%proc = %{$fc_plugins[$type]->{$name}};
	}

	if (FC_CB_EXEC == $cb_type) {
		$cb_name = $proc{$fc_exec_names{$type}};
	}
	elsif (FC_CB_CREATE == $cb_type) {
		if (defined $proc{'create'}) {
			$cb_name = $proc{'create'};
		}
		else {
			return 1;
		}
	}
	elsif (FC_CB_DESTROY == $cb_type) {
		if (defined $proc{'destroy'}) {
			$cb_name = $proc{'destroy'};
		}
		else {
			return 1;
		}
	}

	$status = call_by_name (@_);

	if ($status < 0) {
		my $err = undef;

		if ($@) {
			$err = $@;
		}
		else {
			$err = "callback returned false";
		}

		ERROR ("Execution of fc callback \"$cb_name\" failed: $err");
		return;
	}
	return $status;
}

sub fc_register {
	my $type = shift;
	my $name = shift;
	my $proc = shift;

	my %fc : shared;

	DEBUG ("Collectd::fc_register: "
		. "type = \"$type\" (" . $fc_types{$type}
		. "), name = \"$name\", proc = \"$proc\"");

	if (! ((defined $type) && (defined $name) && (defined $proc))) {
		ERROR ("Usage: Collectd::fc_register(type, name, proc)");
		return;
	}

	if (! defined $fc_plugins[$type]) {
		ERROR ("Collectd::fc_register: Invalid type \"$type\"");
		return;
	}

	if (("HASH" ne ref ($proc)) || (! defined $proc->{$fc_exec_names{$type}})
			|| ("" ne ref ($proc->{$fc_exec_names{$type}}))) {
		ERROR ("Collectd::fc_register: Invalid proc.");
		return;
	}

	for my $p (qw( create destroy )) {
		if ((defined $proc->{$p}) && ("" ne ref ($proc->{$p}))) {
			ERROR ("Collectd::fc_register: Invalid proc.");
			return;
		}
	}

	%fc = %$proc;

	foreach my $p (keys %fc) {
		my $pkg = scalar caller;

		if ($p !~ m/^(create|destroy|$fc_exec_names{$type})$/) {
			next;
		}

		if ($fc{$p} !~ m/^$pkg\:\:/) {
			$fc{$p} = $pkg . "::" . $fc{$p};
		}
	}

	lock %{$fc_plugins[$type]};
	if (defined $fc_plugins[$type]->{$name}) {
		WARNING ("Collectd::fc_register: Overwriting previous "
			. "definition of match \"$name\".");
	}

	if (! _fc_register ($type, $name)) {
		ERROR ("Collectd::fc_register: Failed to register \"$name\".");
		return;
	}

	$fc_plugins[$type]->{$name} = \%fc;
	return 1;
}

sub _plugin_dispatch_config {
	my $plugin = shift;
	my $config = shift;

	our $cb_name = undef;

	if (! (defined ($plugin) && defined ($config))) {
		return;
	}

	if (! defined $cf_callbacks{$plugin}) {
		WARNING ("Found a configuration for the \"$plugin\" plugin, but "
			. "the plugin isn't loaded or didn't register "
			. "a configuration callback.");
		return;
	}

	{
		lock %cf_callbacks;
		$cb_name = $cf_callbacks{$plugin};
	}
	call_by_name ($config);
}

1;

# vim: set sw=4 ts=4 tw=78 noexpandtab :

