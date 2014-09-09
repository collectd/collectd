# /usr/share/doc/collectd/examples/MyPlugin.pm
#
# A Perl plugin template for collectd.
#
# Written by Sebastian Harl <sh@tokkee.org>
#
# This is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation; only version 2 of the License is applicable.

# Notes:
# - each of the functions below (and the corresponding plugin_register call)
#   is optional

package Collectd::Plugin::MyPlugin;

use strict;
use warnings;

use Collectd qw( :all );

# data set definition:
# see section "DATA TYPES" in collectd-perl(5) for details
#
# NOTE: If you're defining a custom data-set, you have to make that known to
# any servers as well. Else, the server is not able to store values using the
# type defined by that data-set.
# It is strongly recommended to use one of the types and data-sets pre-defined
# in the types.db file.
my $dataset =
[
	{
		name => 'my_ds',
		type => DS_TYPE_GAUGE,
		min  => 0,
		max  => 65535,
	},
];

# This code is executed after loading the plugin to register it with collectd.
plugin_register (TYPE_LOG, 'myplugin', 'my_log');
plugin_register (TYPE_NOTIF, 'myplugin', 'my_notify');
plugin_register (TYPE_DATASET, 'mytype', $dataset);
plugin_register (TYPE_INIT, 'myplugin', 'my_init');
plugin_register (TYPE_READ, 'myplugin', 'my_read');
plugin_register (TYPE_WRITE, 'myplugin', 'my_write');
plugin_register (TYPE_SHUTDOWN, 'myplugin', 'my_shutdown');

# For each of the functions below see collectd-perl(5) for details about
# arguments and the like.

# This function is called once upon startup to initialize the plugin.
sub my_init
{
	# open sockets, initialize data structures, ...

	# A false return value indicates an error and causes the plugin to be
	# disabled.
	return 1;
} # my_init ()

# This function is called in regular intervals to collectd the data.
sub my_read
{
	# value list to dispatch to collectd:
	# see section "DATA TYPES" in collectd-perl(5) for details
	my $vl = {};

	# do the magic to read the data:
	# the number of values has to match the number of data sources defined in
	# the registered data set. The type used here (in this example:
	# "mytype") must be defined in the types.db, see types.db(5) for
	# details, or registered as "TYPE_DATASET".
	$vl->{'values'} = [ rand(65535) ];
	$vl->{'plugin'} = 'myplugin';
	$vl->{'type'}   = 'mytype';
	# any other elements are optional

	# dispatch the values to collectd which passes them on to all registered
	# write functions
	plugin_dispatch_values ($vl);

	# A false return value indicates an error and the plugin will be skipped
	# for an increasing amount of time.
	return 1;
} # my_read ()

# This function is called after values have been dispatched to collectd.
sub my_write
{
	my $type = shift;
	my $ds   = shift;
	my $vl   = shift;

	if (scalar (@$ds) != scalar (@{$vl->{'values'}})) {
		plugin_log (LOG_WARNING, "DS number does not match values length");
		return;
	}

	for (my $i = 0; $i < scalar (@$ds); ++$i) {
		# do the magic to output the data
		print "$vl->{'host'}: $vl->{'plugin'}: ";

		if (defined $vl->{'plugin_instance'}) {
			print "$vl->{'plugin_instance'}: ";
		}

		print "$type: ";

		if (defined $vl->{'type_instance'}) {
			print "$vl->{'type_instance'}: ";
		}

		print "$vl->{'values'}->[$i]\n";
	}
	return 1;
} # my_write()

# This function is called before shutting down collectd.
sub my_shutdown
{
	# close sockets, ...
	return 1;
} # my_shutdown ()

# This function is called when plugin_log () has been used.
sub my_log
{
	my $level = shift;
	my $msg   = shift;

	print "LOG: $level - $msg\n";
	return 1;
} # my_log ()

# This function is called when plugin_dispatch_notification () has been used
sub my_notify
{
	my $notif = shift;

	my ($sec, $min, $hour, $mday, $mon, $year) = localtime ($notif->{'time'});

	printf "NOTIF (%04d-%02d-%02d %02d:%02d:%02d): %d - ",
			$year + 1900, $mon + 1, $mday, $hour, $min, $sec,
			$notif->{'severity'};

	if (defined $notif->{'host'}) {
		print "$notif->{'host'}: ";
	}

	if (defined $notif->{'plugin'}) {
		print "$notif->{'plugin'}: ";
	}

	if (defined $notif->{'plugin_instance'}) {
		print "$notif->{'plugin_instance'}: ";
	}

	if (defined $notif->{'type'}) {
		print "$notif->{'type'}: ";
	}

	if (defined $notif->{'type_instance'}) {
		print "$notif->{'type_instance'}: ";
	}

	print "$notif->{'message'}\n";
	return 1;
} # my_notify ()

