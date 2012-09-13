#!/usr/bin/perl
#
# collectd - contrib/cussh.pl
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
#

=head1 NAME

cussh - collectd UNIX socket shell

=head1 SYNOPSIS

B<cussh> [I<E<lt>pathE<gt>>]

=head1 DESCRIPTION

B<collectd>'s unixsock plugin allows external programs to access the values it
has collected or received and to submit own values. This is a little
interactive frontend for this plugin.

=head1 OPTIONS

=over 4

=item I<E<lt>pathE<gt>>

The path to the UNIX socket provided by collectd's unixsock plugin. (Default:
F</var/run/collectd-unixsock>)

=back

=cut

use strict;
use warnings;

use Collectd::Unixsock();

{ # main
	my $path = $ARGV[0] || "/var/run/collectd-unixsock";
	my $sock = Collectd::Unixsock->new($path);

	my $cmds = {
		HELP    => \&cmd_help,
		PUTVAL  => \&putval,
		GETVAL  => \&getval,
		GETTHRESHOLD  => \&getthreshold,
		FLUSH   => \&flush,
		LISTVAL => \&listval,
		PUTNOTIF => \&putnotif,
	};

	if (! $sock) {
		print STDERR "Unable to connect to $path!\n";
		exit 1;
	}

	print "cussh version 0.2, Copyright (C) 2007-2008 Sebastian Harl\n"
		. "cussh comes with ABSOLUTELY NO WARRANTY. This is free software,\n"
		. "and you are welcome to redistribute it under certain conditions.\n"
		. "See the GNU General Public License 2 for more details.\n\n";

	while (1) {
		print "cussh> ";
		my $line = <STDIN>;

		last if (! $line);

		chomp $line;

		last if ($line =~ m/^quit$/i);

		my ($cmd) = $line =~ m/^(\w+)\s*/;
		$line = $';

		next if (! $cmd);
		$cmd = uc $cmd;

		my $f = undef;
		if (defined $cmds->{$cmd}) {
			$f = $cmds->{$cmd};
		}
		else {
			print STDERR "ERROR: Unknown command $cmd!\n";
			next;
		}

		if (! $f->($sock, $line)) {
			print STDERR "ERROR: Command failed!\n";
			next;
		}
	}

	$sock->destroy();
	exit 0;
}

sub tokenize {
	my $line     = shift || return;
	my $line_ptr = $line;
	my @line     = ();

	my $token_pattern = qr/[^"\s]+|"[^"]+"/;

	while (my ($token) = $line_ptr =~ m/^($token_pattern)\s+/) {
		$line_ptr = $';
		push @line, $token;
	}

	if ($line_ptr =~ m/^$token_pattern$/) {
		push @line, $line_ptr;
	}
	else {
		my ($token) = split m/ /, $line_ptr, 1;
		print STDERR "Failed to parse line: $line\n";
		print STDERR "Parse error near token \"$token\".\n";
		return;
	}

	foreach my $l (@line) {
		if ($l =~ m/^"(.*)"$/) {
			$l = $1;
		}
	}
	return @line;
}

sub getid {
	my $string = shift || return;

	my ($h, $p, $pi, $t, $ti) =
		$string =~ m#^([^/]+)/([^/-]+)(?:-([^/]+))?/([^/-]+)(?:-([^/]+))?\s*#;
	$string = $';

	return if ((! $h) || (! $p) || (! $t));

	my %id = ();

	($id{'host'}, $id{'plugin'}, $id{'type'}) = ($h, $p, $t);

	$id{'plugin_instance'} = $pi if defined ($pi);
	$id{'type_instance'} = $ti if defined ($ti);
	return \%id;
}

sub putid {
	my $ident = shift || return;

	my $string;

	$string = $ident->{'host'} . "/" . $ident->{'plugin'};

	if (defined $ident->{'plugin_instance'}) {
		$string .= "-" . $ident->{'plugin_instance'};
	}

	$string .= "/" . $ident->{'type'};

	if (defined $ident->{'type_instance'}) {
		$string .= "-" . $ident->{'type_instance'};
	}
	return $string;
}

=head1 COMMANDS

=over 4

=item B<HELP>

=cut

sub cmd_help {
	my $sock = shift;
	my $line = shift || '';

	my @line = tokenize($line);
	my $cmd = shift (@line);

	my %text = (
		help => <<HELP,
Available commands:
  HELP
  PUTVAL
  GETVAL
  GETTHRESHOLD
  FLUSH
  LISTVAL
  PUTNOTIF

See the embedded Perldoc documentation for details. To do that, run:
  perldoc $0
HELP
		putval => <<HELP,
PUTVAL <id> <value0> [<value1> ...]

Submits a value to the daemon.
HELP
		getval => <<HELP,
GETVAL <id>

Retrieves the current value or values from the daemon.
HELP
		flush => <<HELP,
FLUSH [plugin=<plugin>] [timeout=<timeout>] [identifier=<id>] [...]

Sends a FLUSH command to the daemon.
HELP
		listval => <<HELP,
LISTVAL

Prints a list of available values.
HELP
		putnotif => <<HELP
PUTNOTIF severity=<severity> [...] message=<message>

Sends a notifications message to the daemon.
HELP
	);

	if (!$cmd)
	{
		$cmd = 'help';
	}
	if (!exists ($text{$cmd}))
	{
		print STDOUT "Unknown command: " . uc ($cmd) . "\n\n";
		$cmd = 'help';
	}

	print STDOUT $text{$cmd};

	return 1;
} # cmd_help

=item B<PUTVAL> I<Identifier> I<Valuelist>

=cut

sub putval {
	my $sock = shift || return;
	my $line = shift || return;

	my @line = tokenize($line);

	my $id;
	my $ret;

	if (! @line) {
		return;
	}

	if (scalar(@line) < 2) {
		print STDERR "Synopsis: PUTVAL <id> <value0> [<value1> ...]" . $/;
		return;
	}

	$id = getid($line[0]);

	if (! $id) {
		print STDERR "Invalid id \"$line[0]\"." . $/;
		return;
	}

	my ($time, @values) = split m/:/, $line;
	$ret = $sock->putval(%$id, time => $time, values => \@values);

	if (! $ret) {
		print STDERR "socket error: " . $sock->{'error'} . $/;
	}
	return $ret;
}

=item B<GETVAL> I<Identifier>

=cut

sub getval {
	my $sock = shift || return;
	my $line = shift || return;

	my @line = tokenize($line);

	my $id;
	my $vals;

	if (! @line) {
		return;
	}

	if (scalar(@line) < 1) {
		print STDERR "Synopsis: GETVAL <id>" . $/;
		return;
	}

	$id = getid($line[0]);

	if (! $id) {
		print STDERR "Invalid id \"$line[0]\"." . $/;
		return;
	}

	$vals = $sock->getval(%$id);

	if (! $vals) {
		print STDERR "socket error: " . $sock->{'error'} . $/;
		return;
	}

	foreach my $key (keys %$vals) {
		print "\t$key: $vals->{$key}\n";
	}
	return 1;
}

=item B<GETTHRESHOLD> I<Identifier>

=cut

sub getthreshold {
	my $sock = shift || return;
	my $line = shift || return;

	my @line = tokenize($line);

	my $id;
	my $vals;

	if (! @line) {
		return;
	}

	if (scalar(@line) < 1) {
		print STDERR "Synopsis: GETTHRESHOLD <id>" . $/;
		return;
	}

	$id = getid($line[0]);

	if (! $id) {
		print STDERR "Invalid id \"$line[0]\"." . $/;
		return;
	}

	$vals = $sock->getthreshold(%$id);

	if (! $vals) {
		print STDERR "socket error: " . $sock->{'error'} . $/;
		return;
	}

	foreach my $key (keys %$vals) {
		print "\t$key: $vals->{$key}\n";
	}
	return 1;
}

=item B<FLUSH> [B<timeout>=I<$timeout>] [B<plugin>=I<$plugin>[ ...]]

=cut

sub flush {
	my $sock = shift || return;
	my $line = shift;

	my @line = tokenize($line);

	my $res;

	if (! $line) {
		$res = $sock->flush();
	}
	else {
		my %args = ();

		foreach my $i (@line) {
			my ($option, $value) = $i =~ m/^([^=]+)=(.+)$/;
			next if (! ($option && $value));

			if ($option eq "plugin") {
				push @{$args{"plugins"}}, $value;
			}
			elsif ($option eq "timeout") {
				$args{"timeout"} = $value;
			}
			elsif ($option eq "identifier") {
				my $id = getid ($value);
				if (!$id)
				{
					print STDERR "Not a valid identifier: \"$value\"\n";
					next;
				}
				push @{$args{"identifier"}}, $id;
			}
			else {
				print STDERR "Invalid option \"$option\".\n";
				return;
			}
		}

		$res = $sock->flush(%args);
	}

	if (! $res) {
		print STDERR "socket error: " . $sock->{'error'} . $/;
	}
	return $res;
}

=item B<LISTVAL>

=cut

sub listval {
	my $sock = shift || return;
	my $line = shift;

	my @res;

	if ($line ne "") {
		print STDERR "Synopsis: LISTVAL" . $/;
		return;
	}

	@res = $sock->listval();

	if (! @res) {
		print STDERR "socket error: " . $sock->{'error'} . $/;
		return;
	}

	foreach my $ident (@res) {
		print $ident->{'time'} . " " . putid($ident) . $/;
	}
	return 1;
}

=item B<PUTNOTIF> [[B<severity>=I<$severity>] [B<message>=I<$message>] [ ...]]

=cut

sub putnotif {
	my $sock = shift || return;
	my $line = shift || return;

	my @line = tokenize($line);

	my $ret;

	my (%values) = ();
	foreach my $i (@line) {
		my ($key, $val) = split m/=/, $i, 2;
		if ($key && $val) {
			$values{$key} = $val;
		}
		else {
			$values{'message'} = defined($values{'message'})
				? ($values{'message'} . ' ' . $key)
				: $key;
		}
	}
	$values{'time'} ||= time();

	$ret = $sock->putnotif(%values);
	if (! $ret) {
		print STDERR "socket error: " . $sock->{'error'} . $/;
	}
	return $ret;
}

=back

These commands follow the exact same syntax as described in
L<collectd-unixsock(5)>.

=head1 SEE ALSO

L<collectd(1)>, L<collectd-unisock(5)>

=head1 AUTHOR

Written by Sebastian Harl E<lt>sh@tokkee.orgE<gt>.

B<collectd> has been written by Florian Forster and others.

=head1 COPYRIGHT

Copyright (C) 2007 Sebastian Harl.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; only version 2 of the License is applicable.

=cut

# vim: set sw=4 ts=4 tw=78 noexpandtab :
