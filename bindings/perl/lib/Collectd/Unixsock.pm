#
# collectd - Collectd::Unixsock
# Copyright (C) 2007,2008  Florian octo Forster
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
#   Florian octo Forster <octo at verplant.org>
#

package Collectd::Unixsock;

=head1 NAME

Collectd::Unixsock - Abstraction layer for accessing the functionality by
collectd's unixsock plugin.

=head1 SYNOPSIS

  use Collectd::Unixsock ();

  my $sock = Collectd::Unixsock->new ($path);

  my $value = $sock->getval (%identifier);
  $sock->putval (%identifier,
                 time => time (),
		 values => [123, 234, 345]);

  $sock->destroy ();

=head1 DESCRIPTION

collectd's unixsock plugin allows external programs to access the values it has
collected or received and to submit own values. This Perl-module is simply a
little abstraction layer over this interface to make it even easier for
programmers to interact with the daemon.

=cut

use strict;
use warnings;

#use constant { NOTIF_FAILURE => 1, NOTIF_WARNING => 2, NOTIF_OKAY => 4 };

use Carp (qw(cluck confess));
use IO::Socket::UNIX;
use Regexp::Common (qw(number));

our $Debug = 0;

return (1);

sub _debug
{
	if (!$Debug)
	{
		return;
	}
	print @_;
}

sub _create_socket
{
	my $path = shift;
	my $sock = IO::Socket::UNIX->new (Type => SOCK_STREAM, Peer => $path);
	if (!$sock)
	{
		cluck ("Cannot open UNIX-socket $path: $!");
		return;
	}
	return ($sock);
} # _create_socket

=head1 VALUE IDENTIFIERS

The values in the collectd are identified using an five-tuple (host, plugin,
plugin-instance, type, type-instance) where only plugin-instance and
type-instance may be NULL (or undefined). Many functions expect an
I<%identifier> hash that has at least the members B<host>, B<plugin>, and
B<type>, possibly completed by B<plugin_instance> and B<type_instance>.

Usually you can pass this hash as follows:

  $obj->method (host => $host, plugin => $plugin, type => $type, %other_args);

=cut

sub _create_identifier
{
	my $args = shift;
	my $host;
	my $plugin;
	my $type;

	if (!$args->{'host'} || !$args->{'plugin'} || !$args->{'type'})
	{
		cluck ("Need `host', `plugin' and `type'");
		return;
	}

	$host = $args->{'host'};
	$plugin = $args->{'plugin'};
	$plugin .= '-' . $args->{'plugin_instance'} if (defined ($args->{'plugin_instance'}));
	$type = $args->{'type'};
	$type .= '-' . $args->{'type_instance'} if (defined ($args->{'type_instance'}));

	return ("$host/$plugin/$type");
} # _create_identifier

sub _parse_identifier
{
	my $string = shift;
	my $host;
	my $plugin;
	my $plugin_instance;
	my $type;
	my $type_instance;
	my $ident;

	($host, $plugin, $type) = split ('/', $string);

	($plugin, $plugin_instance) = split ('-', $plugin, 2);
	($type, $type_instance) = split ('-', $type, 2);

	$ident =
	{
		host => $host,
		plugin => $plugin,
		type => $type
	};
	$ident->{'plugin_instance'} = $plugin_instance if (defined ($plugin_instance));
	$ident->{'type_instance'} = $type_instance if (defined ($type_instance));

	return ($ident);
} # _parse_identifier

sub _escape_argument
{
	my $string = shift;

	if ($string =~ m/^\w+$/)
	{
		return ("$string");
	}

	$string =~ s#\\#\\\\#g;
	$string =~ s#"#\\"#g;
	$string = "\"$string\"";

	return ($string);
}

=head1 PUBLIC METHODS

=over 4

=item I<$obj> = Collectd::Unixsock->B<new> ([I<$path>]);

Creates a new connection to the daemon. The optional I<$path> argument gives
the path to the UNIX socket of the C<unixsock plugin> and defaults to
F</var/run/collectd-unixsock>. Returns the newly created object on success and
false on error.

=cut

sub new
{
	my $pkg = shift;
	my $path = @_ ? shift : '/var/run/collectd-unixsock';
	my $sock = _create_socket ($path) or return;
	my $obj = bless (
		{
			path => $path,
			sock => $sock,
			error => 'No error'
		}, $pkg);
	return ($obj);
} # new

=item I<$res> = I<$obj>-E<gt>B<getval> (I<%identifier>);

Requests a value-list from the daemon. On success a hash-ref is returned with
the name of each data-source as the key and the according value as, well, the
value. On error false is returned.

=cut

sub getval # {{{
{
	my $obj = shift;
	my %args = @_;

	my $status;
	my $fh = $obj->{'sock'} or confess ('object has no filehandle');
	my $msg;
	my $identifier;

	my $ret = {};

	$identifier = _create_identifier (\%args) or return;

	$msg = 'GETVAL ' . _escape_argument ($identifier) . "\n";
	_debug "-> $msg";
	print $fh $msg;

	$msg = <$fh>;
	chomp ($msg);
	_debug "<- $msg\n";

	($status, $msg) = split (' ', $msg, 2);
	if ($status <= 0)
	{
		$obj->{'error'} = $msg;
		return;
	}

	for (my $i = 0; $i < $status; $i++)
	{
		my $entry = <$fh>;
		chomp ($entry);
		_debug "<- $entry\n";

		if ($entry =~ m/^(\w+)=NaN$/)
		{
			$ret->{$1} = undef;
		}
		elsif ($entry =~ m/^(\w+)=($RE{num}{real})$/)
		{
			$ret->{$1} = 0.0 + $2;
		}
	}

	return ($ret);
} # }}} sub getval

=item I<$res> = I<$obj>-E<gt>B<getthreshold> (I<%identifier>);

Requests a threshold from the daemon. On success a hash-ref is returned with
the threshold data. On error false is returned.

=cut

sub getthreshold # {{{
{
	my $obj = shift;
	my %args = @_;

	my $status;
	my $fh = $obj->{'sock'} or confess ('object has no filehandle');
	my $msg;
	my $identifier;

	my $ret = {};

	$identifier = _create_identifier (\%args) or return;

	$msg = 'GETTHRESHOLD ' . _escape_argument ($identifier) . "\n";
	_debug "-> $msg";
	print $fh $msg;

	$msg = <$fh>;
	chomp ($msg);
	_debug "<- $msg\n";

	($status, $msg) = split (' ', $msg, 2);
	if ($status <= 0)
	{
		$obj->{'error'} = $msg;
		return;
	}

	for (my $i = 0; $i < $status; $i++)
	{
		my $entry = <$fh>;
		chomp ($entry);
		_debug "<- $entry\n";

		if ($entry =~ m/^([^:]+):\s*(\S.*)$/)
		{
			my $key = $1;
			my $value = $2;

			$key =~ s/^\s+//;
			$key =~ s/\s+$//;

			$ret->{$key} = $value;
		}
	}

	return ($ret);
} # }}} sub getthreshold

=item I<$obj>-E<gt>B<putval> (I<%identifier>, B<time> =E<gt> I<$time>, B<values> =E<gt> [...]);

Submits a value-list to the daemon. If the B<time> argument is omitted
C<time()> is used. The required argument B<values> is a reference to an array
of values that is to be submitted. The number of values must match the number
of values expected for the given B<type> (see L<VALUE IDENTIFIERS>), though
this is checked by the daemon, not the Perl module. Also, gauge data-sources
(e.E<nbsp>g. system-load) may be C<undef>. Returns true upon success and false
otherwise.

=cut

sub putval
{
	my $obj = shift;
	my %args = @_;

	my $status;
	my $fh = $obj->{'sock'} or confess;
	my $msg;
	my $identifier;
	my $values;
	my $interval = "";

	if (defined $args{'interval'})
	{
		$interval = ' interval='
		. _escape_argument ($args{'interval'});
	}

	$identifier = _create_identifier (\%args) or return;
	if (!$args{'values'})
	{
		cluck ("Need argument `values'");
		return;
	}

	if (!ref ($args{'values'}))
	{
		$values = $args{'values'};
	}
	else
	{
		my $time;

		if ("ARRAY" ne ref ($args{'values'}))
		{
			cluck ("Invalid `values' argument (expected an array ref)");
			return;
		}

		if (! scalar @{$args{'values'}})
		{
			cluck ("Empty `values' array");
			return;
		}

		$time = $args{'time'} ? $args{'time'} : time ();
		$values = join (':', $time, map { defined ($_) ? $_ : 'U' } (@{$args{'values'}}));
	}

	$msg = 'PUTVAL '
	. _escape_argument ($identifier)
	. $interval
	. ' ' . _escape_argument ($values) . "\n";
	_debug "-> $msg";
	print $fh $msg;

	$msg = <$fh>;
	chomp ($msg);
	_debug "<- $msg\n";

	($status, $msg) = split (' ', $msg, 2);
	return (1) if ($status == 0);

	$obj->{'error'} = $msg;
	return;
} # putval

=item I<$res> = I<$obj>-E<gt>B<listval> ()

Queries a list of values from the daemon. The list is returned as an array of
hash references, where each hash reference is a valid identifier. The C<time>
member of each hash holds the epoch value of the last update of that value.

=cut

sub listval
{
	my $obj = shift;
	my $msg;
	my @ret = ();
	my $status;
	my $fh = $obj->{'sock'} or confess;

	_debug "LISTVAL\n";
	print $fh "LISTVAL\n";

	$msg = <$fh>;
	chomp ($msg);
	_debug "<- $msg\n";
	($status, $msg) = split (' ', $msg, 2);
	if ($status < 0)
	{
		$obj->{'error'} = $msg;
		return;
	}

	for (my $i = 0; $i < $status; $i++)
	{
		my $time;
		my $ident;

		$msg = <$fh>;
		chomp ($msg);
		_debug "<- $msg\n";

		($time, $ident) = split (' ', $msg, 2);

		$ident = _parse_identifier ($ident);
		$ident->{'time'} = int ($time);

		push (@ret, $ident);
	} # for (i = 0 .. $status)

	return (@ret);
} # listval

=item I<$res> = I<$obj>-E<gt>B<putnotif> (B<severity> =E<gt> I<$severity>, B<message> =E<gt> I<$message>, ...);

Submits a notification to the daemon.

Valid options are:

=over 4

=item B<severity>

Sets the severity of the notification. The value must be one of the following
strings: C<failure>, C<warning>, or C<okay>. Case does not matter. This option
is mandatory.

=item B<message>

Sets the message of the notification. This option is mandatory.

=item B<time>

Sets the time. If omitted, C<time()> is used.

=item I<Value identifier>

All the other fields of the value identifiers, B<host>, B<plugin>,
B<plugin_instance>, B<type>, and B<type_instance>, are optional. When given,
the notification is associated with the performance data of that identifier.
For more details, please see L<collectd-unixsock(5)>.

=back

=cut

sub putnotif
{
	my $obj = shift;
	my %args = @_;

	my $status;
	my $fh = $obj->{'sock'} or confess;

	my $msg; # message sent to the socket
	
	if (!$args{'message'})
	{
		cluck ("Need argument `message'");
		return;
	}
	if (!$args{'severity'})
	{
		cluck ("Need argument `severity'");
		return;
	}
	$args{'severity'} = lc ($args{'severity'});
	if (($args{'severity'} ne 'failure')
		&& ($args{'severity'} ne 'warning')
		&& ($args{'severity'} ne 'okay'))
	{
		cluck ("Invalid `severity: " . $args{'severity'});
		return;
	}

	if (!$args{'time'})
	{
		$args{'time'} = time ();
	}
	
	$msg = 'PUTNOTIF '
	. join (' ', map { $_ . '=' . _escape_argument ($args{$_}) } (keys %args))
	. "\n";

	_debug "-> $msg";
	print $fh $msg;

	$msg = <$fh>;
	chomp ($msg);
	_debug "<- $msg\n";

	($status, $msg) = split (' ', $msg, 2);
	return (1) if ($status == 0);

	$obj->{'error'} = $msg;
	return;
} # putnotif

=item I<$obj>-E<gt>B<flush> (B<timeout> =E<gt> I<$timeout>, B<plugins> =E<gt> [...], B<identifier>  =E<gt> [...]);

Flush cached data.

Valid options are:

=over 4

=item B<timeout>

If this option is specified, only data older than I<$timeout> seconds is
flushed.

=item B<plugins>

If this option is specified, only the selected plugins will be flushed. The
argument is a reference to an array of strings.

=item B<identifier>

If this option is specified, only the given identifier(s) will be flushed. The
argument is a reference to an array of identifiers. Identifiers, in this case,
are hash references and have the members as outlined in L<VALUE IDENTIFIERS>.

=back

=cut

sub flush
{
	my $obj  = shift;
	my %args = @_;

	my $fh = $obj->{'sock'} or confess;

	my $status = 0;
	my $msg    = "FLUSH";

	if (defined ($args{'timeout'}))
	{
		$msg .= " timeout=" . $args{'timeout'};
	}

	if ($args{'plugins'})
	{
		foreach my $plugin (@{$args{'plugins'}})
		{
			$msg .= " plugin=" . $plugin;
		}
	}

	if ($args{'identifier'})
	{
		for (@{$args{'identifier'}})
		{
			my $identifier = $_;
			my $ident_str;

			if (ref ($identifier) ne 'HASH')
			{
				cluck ("The argument of the `identifier' "
					. "option must be an array reference "
					. "of hash references.");
				return;
			}

			$ident_str = _create_identifier ($identifier);
			if (!$ident_str)
			{
				return;
			}

			$msg .= ' identifier=' . _escape_argument ($ident_str);
		}
	}

	$msg .= "\n";

	_debug "-> $msg";
	print $fh $msg;

	$msg = <$fh>;
	chomp ($msg);
	_debug "<- $msg\n";

	($status, $msg) = split (' ', $msg, 2);
	return (1) if ($status == 0);

	$obj->{'error'} = $msg;
	return;
}

sub error
{
	my $obj = shift;
	if ($obj->{'error'})
	{
		return ($obj->{'error'});
	}
	return;
}

=item I<$obj>-E<gt>destroy ();

Closes the socket before the object is destroyed. This function is also
automatically called then the object goes out of scope.

=back

=cut

sub destroy
{
	my $obj = shift;
	if ($obj->{'sock'})
	{
		close ($obj->{'sock'});
		delete ($obj->{'sock'});
	}
}

sub DESTROY
{
	my $obj = shift;
	$obj->destroy ();
}

=head1 SEE ALSO

L<collectd(1)>,
L<collectd.conf(5)>,
L<collectd-unixsock(5)>

=head1 AUTHOR

Florian octo Forster E<lt>octo@verplant.orgE<gt>

=cut

# vim: set fdm=marker :
