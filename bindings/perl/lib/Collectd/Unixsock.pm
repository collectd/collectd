#
# collectd - bindings/buildperl/Collectd/Unixsock.pm
# Copyright (C) 2007,2008  Florian octo Forster
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#
# Authors:
#   Florian Forster <octo at collectd.org>
#

package Collectd::Unixsock;

=head1 NAME

Collectd::Unixsock - Abstraction layer for accessing the functionality by
collectd's unixsock plugin.

=head1 SYNOPSIS

  use Collectd::Unixsock;

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

use Carp qw(cluck confess carp croak);
use IO::Socket::UNIX;
use Scalar::Util qw( looks_like_number );

our $Debug = 0;

sub _debug
{
	print @_ if $Debug;
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

The values in the collectd are identified using a five-tuple (host, plugin,
plugin-instance, type, type-instance) where only plugin instance and type
instance may be undef. Many functions expect an I<%identifier> hash that has at
least the members B<host>, B<plugin>, and B<type>, possibly completed by
B<plugin_instance> and B<type_instance>.

Usually you can pass this hash as follows:

  $self->method (host => $host, plugin => $plugin, type => $type, %other_args);

=cut

sub _create_identifier
{
	my $args = shift;
	my ($host, $plugin, $type);

	if (!$args->{host} || !$args->{plugin} || !$args->{type})
	{
		cluck ("Need `host', `plugin' and `type'");
		return;
	}

	$host = $args->{host};
	$plugin = $args->{plugin};
	$plugin .= '-' . $args->{plugin_instance} if defined $args->{plugin_instance};
	$type = $args->{type};
	$type .= '-' . $args->{type_instance} if defined $args->{type_instance};

	return "$host/$plugin/$type";
} # _create_identifier

sub _parse_identifier
{
	my $string = shift;
	my ($plugin_instance, $type_instance);

	my ($host, $plugin, $type) = split /\//, $string;

	($plugin, $plugin_instance) = split /-/, $plugin, 2;
	($type, $type_instance) = split /-/, $type, 2;

	my $ident =
	{
		host => $host,
		plugin => $plugin,
		type => $type
	};
	$ident->{plugin_instance} = $plugin_instance if defined $plugin_instance;
	$ident->{type_instance} = $type_instance if defined $type_instance;

	return $ident;
} # _parse_identifier

sub _escape_argument
{
	local $_ = shift;

	return $_ if /^\w+$/;

	s#\\#\\\\#g;
	s#"#\\"#g;
	return "\"$_\"";
}

# Send a command on a socket, including any required argument escaping.
# Return a single line of result.
sub _socket_command {
	my ($self, $command, $args) = @_;

	my $fh = $self->{sock} or confess ('object has no filehandle');

    if($args) {
        my $identifier = _create_identifier ($args) or return;
	    $command .= ' ' . _escape_argument ($identifier) . "\n";
    } else {
        $command .= "\n";
    }
	_debug "-> $command";
	$fh->print($command);

	my $response = $fh->getline;
	chomp $response;
	_debug "<- $response\n";
    return $response;
}

# Read any remaining results from a socket and pass them to
# a callback for caller-defined mangling.
sub _socket_chat
{
	my ($self, $msg, $callback, $cbdata) = @_;
	my ($nresults, $ret);
	my $fh = $self->{sock} or confess ('object has no filehandle');

	($nresults, $msg) = split / /, $msg, 2;
	if ($nresults <= 0)
	{
		$self->{error} = $msg;
		return;
	}

	for (1 .. $nresults)
	{
		my $entry = $fh->getline;
		chomp $entry;
		_debug "<- $entry\n";
        $callback->($entry, $cbdata);
	}
	return $cbdata;
}


=head1 PUBLIC METHODS

=over 4

=item I<$self> = Collectd::Unixsock->B<new> ([I<$path>]);

Creates a new connection to the daemon. The optional I<$path> argument gives
the path to the UNIX socket of the C<unixsock plugin> and defaults to
F</var/run/collectd-unixsock>. Returns the newly created object on success and
false on error.

=cut

sub new
{
	my $class = shift;
	my $path = shift || '/var/run/collectd-unixsock';
	my $sock = _create_socket ($path) or return;
	return bless
		{
			path => $path,
			sock => $sock,
			error => 'No error'
		}, $class;
} # new

=item I<$res> = I<$self>-E<gt>B<getval> (I<%identifier>);

Requests a value-list from the daemon. On success a hash-ref is returned with
the name of each data-source as the key and the according value as, well, the
value. On error false is returned.

=cut

sub getval # {{{
{
	my $self = shift;
	my %args = @_;
	my $ret = {};

    my $msg = $self->_socket_command('GETVAL', \%args) or return;
    $self->_socket_chat($msg, sub {
            local $_ = shift;
            my $ret = shift;
            /^(\w+)=NaN$/ and $ret->{$1} = undef, return;
            /^(\w+)=(.*)$/ and looks_like_number($2) and $ret->{$1} = 0 + $2, return;
        }, $ret
    );
	return $ret;
} # }}} sub getval

=item I<$res> = I<$self>-E<gt>B<getthreshold> (I<%identifier>);

Requests a threshold from the daemon. On success a hash-ref is returned with
the threshold data. On error false is returned.

=cut

sub getthreshold # {{{
{
	my $self = shift;
	my %args = @_;
	my $ret = {};

    my $msg = $self->_socket_command('GETTHRESHOLD', \%args) or return;
    $self->_socket_chat($msg, sub {
            local $_ = shift;
            my $ret = shift;
		    /^\s*([^:]+):\s*(.*)/ and do {
			    $1 =~ s/\s*$//;
			    $ret->{$1} = $2;
		    };
        }, $ret
    );
	return $ret;
} # }}} sub getthreshold

=item I<$self>-E<gt>B<putval> (I<%identifier>, B<time> =E<gt> I<$time>, B<values> =E<gt> [...]);

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
	my $self = shift;
	my %args = @_;

	my ($status, $msg, $identifier, $values);
	my $fh = $self->{sock} or confess;

	my $interval = defined $args{interval} ?
    ' interval=' . _escape_argument ($args{interval}) : '';

	$identifier = _create_identifier (\%args) or return;
	if (!$args{values})
	{
		cluck ("Need argument `values'");
		return;
	}

	if (ref ($args{values}))
	{
		my $time;

		if ("ARRAY" ne ref ($args{values}))
		{
			cluck ("Invalid `values' argument (expected an array ref)");
			return;
		}

		if (! scalar @{$args{values}})
		{
			cluck ("Empty `values' array");
			return;
		}

		$time = $args{time} || time;
		$values = join (':', $time, map { defined $_ ? $_ : 'U' } @{$args{values}});
	}
	else
	{
		$values = $args{values};
	}

	$msg = 'PUTVAL '
	. _escape_argument ($identifier)
	. $interval
	. ' ' . _escape_argument ($values) . "\n";
	_debug "-> $msg";
	$fh->print($msg);

	$msg = <$fh>;
	chomp $msg;
	_debug "<- $msg\n";

	($status, $msg) = split / /, $msg, 2;
	return 1 if $status == 0;

	$self->{error} = $msg;
	return;
} # putval

=item I<$res> = I<$self>-E<gt>B<listval_filter> ( C<%identifier> )

Queries a list of values from the daemon while restricting the results to
certain hosts, plugins etc. The argument may be anything that passes for an
identifier (cf. L<VALUE IDENTIFIERS>), although all fields are optional.
The returned data is in the same format as from C<listval>.

=cut

sub listval_filter
{
	my $self = shift;
    my %args = @_;
	my @ret;
	my $nresults;
	my $fh = $self->{sock} or confess;

    my $pattern =
    (exists $args{host}              ? "$args{host}"             : '[^/]+') .
    (exists $args{plugin}            ? "/$args{plugin}"          : '/[^/-]+') .
	(exists $args{plugin_instance}   ? "-$args{plugin_instance}" : '(?:-[^/]+)?') .
	(exists $args{type}              ? "/$args{type}"            : '/[^/-]+') .
	(exists $args{type_instance}     ? "-$args{type_instance}"   : '(?:-[^/]+)?');
    $pattern = qr/^\d+ $pattern$/;

    my $msg = $self->_socket_command('LISTVAL') or return;
	($nresults, $msg) = split / /, $msg, 2;

    # This could use _socket_chat() but doesn't for speed reasons
	if ($nresults < 0)
	{
		$self->{error} = $msg;
		return;
	}

	for (1 .. $nresults)
	{
		$msg = <$fh>;
		chomp $msg;
		_debug "<- $msg\n";
		next unless $msg =~ $pattern;
		my ($time, $ident) = split / /, $msg, 2;

		$ident = _parse_identifier ($ident);
		$ident->{time} = int $time;

		push (@ret, $ident);
	} # for (i = 0 .. $status)

	return @ret;
} # listval

=item I<$res> = I<$self>-E<gt>B<listval> ()

Queries a list of values from the daemon. The list is returned as an array of
hash references, where each hash reference is a valid identifier. The C<time>
member of each hash holds the epoch value of the last update of that value.

=cut

sub listval
{
	my $self = shift;
	my $nresults;
	my @ret;
	my $fh = $self->{sock} or confess;

    my $msg = $self->_socket_command('LISTVAL') or return;
	($nresults, $msg) = split / /, $msg, 2;

    # This could use _socket_chat() but doesn't for speed reasons
	if ($nresults < 0)
	{
		$self->{error} = $msg;
		return;
	}

	for (1 .. $nresults)
	{
		$msg = <$fh>;
		chomp $msg;
		_debug "<- $msg\n";

		my ($time, $ident) = split / /, $msg, 2;

		$ident = _parse_identifier ($ident);
		$ident->{time} = int $time;

		push (@ret, $ident);
	} # for (i = 0 .. $status)

	return @ret;
} # listval

=item I<$res> = I<$self>-E<gt>B<putnotif> (B<severity> =E<gt> I<$severity>, B<message> =E<gt> I<$message>, ...);

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
	my $self = shift;
	my %args = @_;

	my $status;
	my $fh = $self->{sock} or confess;

	my $msg; # message sent to the socket
	
    for my $arg (qw( message severity ))
    {
        cluck ("Need argument `$arg'"), return unless $args{$arg};
    }
	$args{severity} = lc $args{severity};
	if (($args{severity} ne 'failure')
		&& ($args{severity} ne 'warning')
		&& ($args{severity} ne 'okay'))
	{
		cluck ("Invalid `severity: " . $args{severity});
		return;
	}

	$args{time} ||= time;
	
	$msg = 'PUTNOTIF '
	. join (' ', map { $_ . '=' . _escape_argument ($args{$_}) } keys %args)
	. "\n";

	_debug "-> $msg";
	$fh->print($msg);

	$msg = <$fh>;
	chomp $msg;
	_debug "<- $msg\n";

	($status, $msg) = split / /, $msg, 2;
	return 1 if $status == 0;

	$self->{error} = $msg;
	return;
} # putnotif

=item I<$self>-E<gt>B<flush> (B<timeout> =E<gt> I<$timeout>, B<plugins> =E<gt> [...], B<identifier>  =E<gt> [...]);

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
	my $self  = shift;
	my %args = @_;

	my $fh = $self->{sock} or confess;

	my $status = 0;
	my $msg    = "FLUSH";

    $msg .= " timeout=$args{timeout}" if defined $args{timeout};

	if ($args{plugins})
	{
		foreach my $plugin (@{$args{plugins}})
		{
			$msg .= " plugin=" . $plugin;
		}
	}

	if ($args{identifier})
	{
		for my $identifier (@{$args{identifier}})
		{
			my $ident_str;

			if (ref ($identifier) ne 'HASH')
			{
				cluck ("The argument of the `identifier' "
					. "option must be an array of hashrefs.");
				return;
			}

			$ident_str = _create_identifier ($identifier) or return;
			$msg .= ' identifier=' . _escape_argument ($ident_str);
		}
	}

	$msg .= "\n";

	_debug "-> $msg";
	$fh->print($msg);

	$msg = <$fh>;
	chomp ($msg);
	_debug "<- $msg\n";

	($status, $msg) = split / /, $msg, 2;
	return 1 if $status == 0;

	$self->{error} = $msg;
	return;
}

sub error
{
	return shift->{error};
}

=item I<$self>-E<gt>destroy ();

Closes the socket before the object is destroyed. This function is also
automatically called then the object goes out of scope.

=back

=cut

sub destroy
{
	my $self = shift;
	if ($self->{sock})
	{
		close $self->{sock};
		delete $self->{sock};
	}
}

sub DESTROY
{
	my $self = shift;
	$self->destroy ();
}

=head1 SEE ALSO

L<collectd(1)>,
L<collectd.conf(5)>,
L<collectd-unixsock(5)>

=head1 AUTHOR

Florian octo Forster E<lt>octo@collectd.orgE<gt>

=cut
1;
# vim: set fdm=marker :
