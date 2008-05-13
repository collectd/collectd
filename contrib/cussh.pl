#!/usr/bin/perl
#
# collectd - contrib/cussh.pl
# Copyright (C) 2007-2008  Sebastian Harl
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
		PUTVAL  => \&putval,
		GETVAL  => \&getval,
		FLUSH   => \&flush,
		LISTVAL => \&listval,
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

sub getid {
	my $string = shift || return;

	print $$string . $/;
	my ($h, $p, $pi, $t, $ti) =
		$$string =~ m#^([^/]+)/([^/-]+)(?:-([^/]+))?/([^/-]+)(?:-([^/]+))?\s*#;
	$$string = $';

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

=item B<GETVAL> I<Identifier>

=cut

sub putval {
	my $sock = shift || return;
	my $line = shift || return;

	my $id = getid(\$line);

	if (! $id) {
		print STDERR $sock->{'error'} . $/;
		return;
	}

	my ($time, @values) = split m/:/, $line;
	return $sock->putval(%$id, time => $time, values => \@values);
}

=item B<PUTVAL> I<Identifier> I<Valuelist>

=cut

sub getval {
	my $sock = shift || return;
	my $line = shift || return;

	my $id = getid(\$line);

	if (! $id) {
		print STDERR $sock->{'error'} . $/;
		return;
	}

	my $vals = $sock->getval(%$id);

	if (! $vals) {
		print STDERR $sock->{'error'} . $/;
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

	my $res;

	if (! $line) {
		$res = $sock->flush();
	}
	else {
		my %args = ();

		foreach my $i (split m/ /, $line) {
			my ($option, $value) = $i =~ m/^([^=]+)=(.+)$/;
			next if (! ($option && $value));

			if ($option eq "plugin") {
				push @{$args{"plugins"}}, $value;
			}
			elsif ($option eq "timeout") {
				$args{"timeout"} = $value;
			}
			else {
				print STDERR "Invalid option \"$option\".\n";
				return;
			}
		}

		$res = $sock->flush(%args);
	}

	if (! $res) {
		print STDERR $sock->{'error'} . $/;
		return;
	}
	return 1;
}

=item B<LISTVAL>

=cut

sub listval {
	my $sock = shift || return;

	my @res;

	@res = $sock->listval();

	if (! @res) {
		print STDERR $sock->{'error'} . $/;
		return;
	}

	foreach my $ident (@res) {
		print $ident->{'time'} . " " . putid($ident) . $/;
	}
	return 1;
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
