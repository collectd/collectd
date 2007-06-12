#!/usr/bin/perl

=head1 NAME

Collectd - plugin for filling collectd with stats 

=head1 INSTALLATION

Just copy Collectd.pm into your SpamAssassin Plugin path 
(e.g /usr/share/perl5/Mail/SpamAssassin/Plugin/) and
add a loadplugin call into your init.pre file. 

=head1 SYNOPSIS

  loadplugin    Mail::SpamAssassin::Plugin::Collectd

=head1 USER SETTINGS

=over 4

=item collectd_socket [ socket path ]	    (default: /var/run/collectd-email)

Where the collectd socket is

=cut 

=item collectd_buffersize [ size ] (default: 256) 

the email plugin uses a fixed buffer, if a line exceeds this size
it has to be continued in another line. (This is of course handled internally)
If you have changed this setting please get it in sync with the SA Plugin
config. 

=cut 

=item collectd_timeout [ sec ] (default: 2) 

if sending data to to collectd takes too long the connection will be aborted. 

=cut

=item collectd_retries [ tries ] (default: 3)

the collectd plugin uses a tread pool, if this is empty the connection fails,
the SA Plugin then tries to reconnect. With this variable you can indicate how
often it should try. 

=cut

=head1 DESCRIPTION

This modules uses the email plugin of collectd from Sebastian Harl to
collect statistical informations in rrd files to create some nice looking
graphs with rrdtool. They communicate over a unix socket that the collectd
plugin creates. The generated graphs will be placed in /var/lib/collectd/email

=head1 AUTHOR

Alexander Wirt <formorer@formorer.de>

=head1 COPYRIGHT

 Copyright 2006 Alexander Wirt <formorer@formorer.de> 
 
 This program is free software; you can redistribute it and/or modify 
 it under the the terms of either: 

 a) the Apache License 2.0 (http://www.apache.org/licenses/LICENSE-2.0)

 or

 b) the GPL (http://www.gnu.org/copyleft/gpl.html)  

 use whatever you like more. 

=cut

package Mail::SpamAssassin::Plugin::Collectd;

use Mail::SpamAssassin::Plugin;
use Mail::SpamAssassin::Logger;
use strict;
use bytes; 
use warnings;
use Time::HiRes qw(usleep);
use IO::Socket;

use vars qw(@ISA);
@ISA = qw(Mail::SpamAssassin::Plugin);

sub new {
    my ($class, $mailsa) = @_;

    # the usual perlobj boilerplate to create a subclass object
    $class = ref($class) || $class;
    my $self = $class->SUPER::new($mailsa);
    bless ($self, $class);

    # register our config options
    $self->set_config($mailsa->{conf});

    # and return the new plugin object
    return $self;
}

sub set_config {
    my ($self, $conf) = @_;
    my @cmds = ();

    push (@cmds, {
	    setting => 'collectd_buffersize',
	    default => 256,
	    type =>
	    $Mail::SpamAssassin::Conf::CONF_TYPE_NUMERIC,
	});

    push (@cmds, {
	    setting => 'collectd_socket', 
	    default => '/var/run/collectd-email',
	    type => $Mail::SpamAssassin::Conf::CONF_TYPE_STRING,
    });

	push (@cmds, {
			setting => 'collectd_timeout',
			default => 2,
			type =>
			$Mail::SpamAssassin::Conf::CONF_TYPE_NUMERIC,
	});

	push (@cmds, {
			setting => 'collectd_retries',
			default => 3,
			type =>
			$Mail::SpamAssassin::Conf::CONF_TYPE_NUMERIC,
	});


    $conf->{parser}->register_commands(\@cmds);
}

sub check_end {
    my ($self, $params) = @_;
    my $message_status = $params->{permsgstatus};
	#create  new connection to our socket
	eval {
		local $SIG{ALRM} = sub { die "Sending to collectd timed out.\n" }; # NB: \n required

		#generate a timeout
		alarm $self->{main}->{conf}->{collectd_timeout};

		my $sock;
		#try at least $self->{main}->{conf}->{collectd_retries} to get a
		#connection
		for (my $i = 0; $i < $self->{main}->{conf}->{collectd_retries} ; ++$i) {
			last if $sock = new IO::Socket::UNIX
				($self->{main}->{conf}->{collectd_socket});
			#sleep a random value between 0 and 50 microsecs to try for a new
			#thread
			usleep(int(rand(50))); 
		}

		die("could not connect to " .
				$self->{main}->{conf}->{collectd_socket} . ": $! - collectd plugin disabled") unless $sock; 

		$sock->autoflush(1);

		my $score = $message_status->{score};
		#get the size of the message 
		my $body = $message_status->{msg}->{pristine_body};

		my $len = length($body);

		if ($message_status->{score} >= $self->{main}->{conf}->{required_score} ) {
			#hey we have spam
			print $sock "e:spam:$len\n";
		} else {
			print $sock "e:ham:$len\n";
		}
		print $sock "s:$score\n";
		my @tmp_array; 
		my @tests = @{$message_status->{test_names_hit}};

		my $buffersize = $self->{main}->{conf}->{collectd_buffersize}; 
		dbg("collectd: buffersize: $buffersize"); 

		while  (scalar(@tests) > 0) {
		push (@tmp_array, pop(@tests)); 
			if (length(join(',', @tmp_array) . '\n') > $buffersize) {
				push (@tests, pop(@tmp_array)); 
					if (length(join(',', @tmp_array) . '\n') > $buffersize or scalar(@tmp_array) == 0) {
						dbg("collectd: this shouldn't happen. Do you have tests"
							." with names that have more than ~ $buffersize Bytes?");
						return 1; 
					} else {
						dbg ( "collectd: c:" . join(',', @tmp_array) . "\n" ); 
						print $sock "c:" . join(',', @tmp_array) . "\n"; 
						#clean the array
						@tmp_array = ();
					} 
			} elsif ( scalar(@tests) == 0 ) {
				dbg ( "collectd: c:" . join(',', @tmp_array) . '\n' );
				print $sock "c:" . join(',', @tmp_array) . "\n";
			}
		}
		close($sock); 
		alarm 0; 
	};
	if ($@) {
		my $message = $@; 
		chomp($message); 
		info("collectd: $message");
		return -1; 
	}
}

1;

# vim: syntax=perl sw=4 ts=4 noet shiftround
