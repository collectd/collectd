package Collectd::Unixsock;

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
