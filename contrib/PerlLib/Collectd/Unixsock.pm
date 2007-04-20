package Collectd::Unixsock;

=head1 NAME

Collectd::Unixsock - Abstraction layer for accessing the functionality by collectd's unixsock plugin.

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

use Carp (qw(cluck confess));
use IO::Socket::UNIX;
use Regexp::Common (qw(number));

return (1);

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

=head1 VALUE IDENTIFIER

The values in the collectd are identified using an five-tupel (host, plugin,
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
	$plugin .= '-' . $args->{'plugin_instance'} if ($args->{'plugin_instance'});
	$type = $args->{'type'};
	$type .= '-' . $args->{'type_instance'} if ($args->{'type_instance'});

	return ("$host/$plugin/$type");
} # _create_identifier

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

sub getval
{
	my $obj = shift;
	my %args = @_;

	my $status;
	my $fh = $obj->{'sock'} or confess;
	my $msg;
	my $identifier;

	my $ret = {};

	$identifier = _create_identifier (\%args) or return;

	$msg = "GETVAL $identifier\n";
	send ($fh, $msg, 0) or confess ("send: $!");

	$msg = undef;
	recv ($fh, $msg, 1024, 0) or confess ("recv: $!");

	($status, $msg) = split (' ', $msg, 2);
	if ($status <= 0)
	{
		$obj->{'error'} = $msg;
		return;
	}

	for (split (' ', $msg))
	{
		my $entry = $_;
		if ($entry =~ m/^(\w+)=($RE{num}{real})$/)
		{
			$ret->{$1} = 0.0 + $2;
		}
	}

	return ($ret);
} # getval

=item I<$obj>-E<gt>B<putval> (I<%identifier>, B<time> => I<$time>, B<values> => [...]);

Submits a value-list to the daemon. If the B<time> argument is omitted
C<time()> is used. The requierd argument B<values> is a reference to an array
of values that is to be submitted. The number of values must match the number
of values expected for the given B<type> (see L<VALUE IDENTIFIER>), though this
is checked by the daemon, not the Perl module. Also, gauge data-sources
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
		my $time = $args{'time'} ? $args{'time'} : time ();
		$values = join (':', $time, map { defined ($_) ? $_ : 'U' } (@{$args{'values'}}));
	}

	$msg = "PUTVAL $identifier $values\n";
	send ($fh, $msg, 0) or confess ("send: $!");
	$msg = undef;
	recv ($fh, $msg, 1024, 0) or confess ("recv: $!");

	($status, $msg) = split (' ', $msg, 2);
	return (1) if ($status == 0);

	$obj->{'error'} = $msg;
	return;
} # putval

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

=head1 AUTHOR

Florian octo Forster E<lt>octo@verplant.orgE<gt>

=cut
